# P8 sesión 8: log asíncrono, grabación del sniffer sin bloqueo y detector de divergencia (implementación)

10 de octubre de 2026, rama de trabajo desde `opcion-d` `4cfbea3`. Build del juego **1.0.0.2976** (base
`0x140000000`, RVA). Parte de [p8-s8-congelacion.md](p8-s8-congelacion.md) §2–§4. No se lanzó el juego ni se desplegó
nada. **[V]** = comprobado (código, tests); **[E]** = deducción; **[H]** = hipótesis a medir en vivo.

## 1. Log asíncrono (cierra §2 de la sesión 8)

### 1.1 Qué cambia

Antes, `log::write` tomaba `g_mutex` y hacía el `WriteFile` dentro: un `WriteFile` de 8,8 s en Shadow paró a la vez
el hilo de red (y con él el túnel), el worker de diagnóstico y el hilo de presentación del juego.

Ahora (`src/mod/log.{h,cpp}`, lógica pura en `src/mod/log_ring.h`):

- **Productores** (cualquier hilo, también los del juego): formatean en un búfer de pila de 1 KB (`std::format_to_n`;
  solo una línea más larga usa `std::string`), escriben el prefijo a mano (`[hh:mm:ss.mmm] TAG [Ttid] `) y copian la
  línea entera en un **anillo fijo de 4 MB** en la `.bss` del mod (nunca el heap del juego, sin asignar). El candado
  (SRW) solo cubre esa copia; nunca hay E/S con él.
- **Un hilo escritor** (cada 25 ms, o antes si la cola pasa de 1 MB): toma `[tail, head)` **sin ningún candado**
  (`head` se publica con *release* después de los bytes, los productores nunca escriben ahí y `tail` solo avanza:
  CAS al máximo, *release*), hace `WriteFile` y luego mueve `tail`. **Ningún consumidor toma nunca el candado de
  los productores** (revisión del 10-10). Cada pasada escribe como mucho 1 MB y acaba en fin de línea; las pasadas
  se encadenan como mucho 50 ms y entre ellas mira la parada y la salida. Mide cada escritura: una de ≥ 1 s deja su
  propia línea `Log writer: one write of N bytes ... took X ms`.
- `open()` vuelve a abrir tras un `close()` (rota otra vez); si el escritor anterior sigue dentro de un `WriteFile`,
  `open()` devuelve `false` en vez de abrir debajo. El escritor es el dueño del handle y lo cierra al pararse.
- **Anillo lleno:** la línea se **descarta y se cuenta**. La siguiente línea que cabe (o el escritor si nadie
  escribe) pone antes `WRN ... Log: N line(s) dropped here (the log ring was full; M in all)`, justo donde se
  perdieron. Se respeta el orden de lo que se guarda (el de entrada al anillo).
- **Línea > 16 KB** (o 1/8 del anillo): se corta y se marca ` [line cut: N bytes]`.
- **Rotación:** igual que antes (`cdmp.log` → `cdmp.1.log` … `cdmp.5.log`).
- Cada 5 s, en el informe del hilo de red: `Log: lines N, dropped D, queued Q KB (max H KB of 4096 KB), writes W
  (slowest X ms, >= 250 ms S), write errors E`. **Es la medida directa del disco de Shadow.**

### 1.2 Volcado al cerrar y en los caminos de crash (acotado)

| Camino | Qué hace |
|---|---|
| `log::flush(ms)` | **Solo espera** (como mucho `ms`) a que el escritor escriba lo encolado antes de la llamada. El llamante nunca escribe la cola ni toma candados (revisión del 10-10: en un filtro de excepción el hilo puede tener cualquier cosa y el disco puede colgarse). Si el escritor no existe o es el propio hilo, vuelve en el acto. |
| `log::emergency` (filtros de excepción, `game_fault_continue`) | Primero `flush(50 ms)` (un fallo que el juego luego maneja casi no cuesta), luego su línea directa con `WriteFile`, sin asignar y sin ningún candado. |
| `guest_s3` captura de crash | `flush(400 ms)` al salir de `capture()` (destructor `Leave`) y otro antes de la línea final + `TerminateProcess` de `end_after_capture`: con el de `emergency`, ≤ ~0,85 s de espera de log en todo el camino. |
| `guest.cpp end_without_host` (adiós sin anfitrión, no es un fallo) | `flush(1000 ms)` antes de `TerminateProcess`. |
| Cierre del proceso (`DLL_PROCESS_DETACH` con `reserved ≠ 0`) | `log::flush_at_exit()`. No da por muertos a los demás hilos: marca la salida (el escritor no empieza otra pasada), da ≤ 100 ms a la pasada en curso y escribe la cola desde `DllMain` **sin candado** (el lado consumidor no lo usa; `tail` no retrocede). Si los dos escriben a la vez, alguna línea puede salir dos veces, nunca cortada. |
| `cdmp::stop()` (anfitrión de pruebas) | `log::close()`: `flush(3 s)`, para el escritor (espera ≤ 3 s); el escritor vacía lo que queda (≤ 2 s) y cierra el fichero; si sigue dentro de un `WriteFile`, lo cierra cuando vuelva. |

Riesgo aceptado: si el proceso lo termina otro (`TerminateProcess` del juego, crashpad, el administrador de tareas),
se pierde lo que esté en el anillo (como mucho ~25 ms de líneas en marcha normal; más si el disco va lento). Antes se
perdía lo mismo... y además el juego esperaba al disco.

### 1.3 El sniffer (`cdmp_packets_*`)

**¿Su E/S podía bloquear hilos del juego o el hilo de red? No directamente [V código]:** los hilos del juego solo
copian el paquete en su anillo de 64 MB con un SRW (descartando y contando si está lleno) y el hilo de red del mod no
lo toca. Pero el **mismo hilo** que escribía los ficheros (`fwrite` con búferes de 1/4 MB y `fflush` cada 10 s)
también decodifica, alimenta el *tap* (en el anfitrión, el libro de `observer_seq_copy` y `host_scene_diag`), la
captura del coop y `capture_tick`/`puppet_tick`/`game_errors_drain`. Un disco lento paraba todo eso y, con el anillo
lleno, el tap perdía paquetes.

**Aplicada la misma idea:** con `record=1`, el texto y el `.bin` van a dos anillos `LineRing` del mod (**1 MB + 4
MB**, `VirtualAlloc`, solo si se graba) y los escribe un **hilo de disco propio** (cada 20 ms, pasadas de ≤ 1 MB por
fichero, `fflush` tras cada pasada, búferes CRT de 64/256 KB). Pequeños a propósito (revisión del 10-10): al morir el
proceso se pierde lo encolado, así que la cola guarda ~1–2 s de un disco lento, no minutos; con un disco sano está
vacía cada 20 ms. Si se llenan:

- Se pierde la línea o el registro `.bin` entero (nunca medio) y se cuenta. Las líneas y registros normales dejan
  libres 4 KB de reserva para los avisos.
- **`.bin`:** en el sitio de lo perdido va un **registro de hueco** (`capture::gap_record`): un registro S>C **vacío**
  (`dir 2, size 0, copied 0`), `session = 0xFFFFFFFF`, `thread` = registros perdidos. El formato no cambia:
  `tools/scripts/analyze_capture.py` salta los registros de menos de 5 bytes, `capture::Reader` lo lee como un
  registro S>C entero y vacío (`Record::is_gap()`) y la repetición de `cdmp_peer` descarta los S>C que no se
  analizan. Si ni el hueco cabe, queda pendiente y lo que viene detrás también se pierde (el hueco va primero).
- **Texto:** `# DISK QUEUE: N text line(s) and M .bin record(s) not recorded so far (...), slowest write X ms`, con
  la reserva; si ni el aviso cabe, no se da por avisado y se repite en las siguientes estadísticas.
- `sniffer_install` corre una vez por proceso (`late_init` de `mod.cpp`); aun así, un segundo arranque reutiliza
  los anillos y empieza de cero (parada, contadores, cola).

**Coste de `record=1` en Shadow** (no se ha tocado; decisión del usuario): ~0,3 MB/s, 310 MB en 16 min de la sesión 8
(texto + `.bin`), el mayor escritor del invitado y la sospecha principal [H] de los `WriteFile` de 5–9 s del log
(compite con `cdmp.log` en el mismo disco y la misma caché). Con este cambio ya no puede parar el juego, el hilo de red
ni el log; lo que sigue costando es ancho de banda de disco (y, si el disco no da, registros perdidos y contados). Con
`record=0` se mantienen contadores, estadísticas y tap.

## 2. Nada de log pesado en hilos del juego

- `hud.cpp` `Overlay diag` (hilo de presentación, cada 5 s): sin `std::format` anidado ni `std::string`; el contador
  de fotogramas sale de `render::status_frames_drawn()` (un atómico) en vez de `status()` (mutex + copia de dos
  `std::string`). Con el log asíncrono es formatear en pila y una copia al anillo.
- Auditoría de los demás `log::` alcanzables desde hooks/hilos del juego (subagente, solo lectura): ninguno por
  fotograma; todos con tope (`take_line`, `note`, presupuestos) salvo `Inject C>S` del sniffer (una línea por paquete
  inyectado, ruta del coop antiguo) y los de identidad/sondas que formatean los argumentos antes del tope (solo con
  sus claves de sonda). Con el log asíncrono ninguno espera al disco; no se tocaron (fuera de alcance y en ficheros de
  otros agentes). Lo más pesado en un hilo del juego sigue siendo la captura de crash de `guest_s3` (formatea en el
  filtro), que solo corre al morir el proceso.

## 3. Detector de divergencia (`[session2] divergence_probe = 0|1`, los dos PCs)

En `remote_motion` (mismo hilo y mismas muestras de 30 ms que `dismount_probe`; funciona sin él). Solo lectura.

- **Canal propio:** el hilo de red publica cada posición nueva del otro jugador (`ClientSession::remote_state`, la del
  HUD) con `remote_motion_channel_position` (un SRW y unos floats; con la clave a 0, una carga atómica).
- **Memoria:** el personaje del otro jugador que ya sigue la sonda (`0xA01…` con usuario dueño, no el local),
  posición `TS+0xB4` + sector·1000. El canal no dice qué actor es: **cada candidato tiene su propio vigilante**
  (revisión del 10-10: elegir el más cercano podía esconder al real detrás de otro) y la clave va en cada línea;
  numeración de episodios y presupuesto de líneas compartidos. Normalmente hay un solo candidato.
- **Distancia:** a la **estela** del canal de los últimos 600 ms (segmentos entre posiciones), no solo a la última.
  Motivo: el actor en memoria va 200 ms (retardo del juego para `0xA`) + latencia por detrás; corriendo a ~7 m/s la
  distancia a la última posición es ~1,6 m siempre (montado, ~3 m). Un personaje que sigue el camino tarde queda
  sobre la estela; uno congelado se aleja de ella. Se registran las dos. Canal sin posición nueva en 2 s: no se compara.
- **Disparo:** estela > 1,5 m durante > 1 s → `WRN RemoteMotion divergence #N [k] +T ms ...` una vez por segundo y
  `... #N END key 0x… after T ms ...` al acabar (vuelta bajo el umbral, o fin de la comparación). Picos ≤ 1 s solo
  se cuentan. Máximo 900 líneas por sesión, **las de fin incluidas**.
- **Cada línea:** clave y actor; memoria vs canal (distancia a la última y a la estela, posiciones en la estela,
  antigüedad); método (`mc+0xF0`) y su `Enter`; acciones de las dos capas; **modo de control** con la función del
  juego `0x88E8E0` (sus bytes, su tabla y su llamada se verifican antes, y como en `climb_facts` solo si
  `[CC+8] == actor` y `[mc+0x68] == actor`; si no, `?` y no se lee el búfer); **búfer `mc+0x3D0`**: `count`,
  `done`, `ts[0]`, `ts[último]`, `now−delay` (reloj de fotograma `exe+0x6C8CD68/70` + sesgo `exe+0x6D69438`, retardo
  `exe+0x6CB9548`) y su diferencia con `ts[último]`/`ts[0]`; **si hubo update**: la muestra mostrada `+0x18` (que cada
  update con `T > ts[0]` reescribe con `ts = T`) se movió en el último segundo, o `NO for N ms`; y el **cliente local**:
  método y acciones de su propio personaje.
- **¿Indicador barato de diálogo/escena en el cliente local? No hay uno verificado.** Se registra el método del
  personaje local (13 = Sequencer como aproximación) y su acción de capa 0 (en la sesión 8 el diálogo del anfitrión
  fue la acción `0x3E005FA1`), para cruzar con los `InteractionDoStepReq`/`SetAvoidByCutScene` de la captura.
- **"Si hubo update"** sale del propio búfer, sin tocar `remote_interp` (lo trabaja otro agente). Si `T ≤ ts[0]`, el
  update corre pero no reescribe la muestra mostrada: la línea dirá `NO` y `now−delay` < `ts[0]` lo explica.
- Resumen de 60 s (`RemoteMotion summary`): `divergence_probe on: channel positions N, compared N, no fresh channel N,
  largest X m, episodes N (lines N, not logged N), short spikes <= 1000 ms N`.

Activado en `cdmp.p8-s7-host.ini`, `cdmp.p8-s7-shadow.ini` y `cdmp.p8-noche-local.ini`.

## 4. Menos log en Shadow (`cdmp.p8-s7-shadow.ini`)

A 0: `guest_s3_diag` (historial S3 cada 5 s; la captura de crash sigue con `guest_crash_dump=1`), P8Probe
(`load_diag`, `s1_diag`; también apaga `GuestLoad`) y `guest_scene_diag` (G1–G4). Siguen: `guest_seq_route` y
`guest_seq_watch` (con `load_diag=0`, el punto 2322 lo engancha `guest_seq_route` por su cuenta [V código,
`owner_of`]; los 2661/2981/2810 siguen por el hook de `guest_stage_diag=1`), `guest_stage_diag`, `guest_actor_diag`,
la captura de cierres y todo el movimiento. `record` del sniffer sin tocar (coste en §1.3).

## 5. Tests y build [V]

`.\scripts\build.ps1`: sin avisos nuevos en `src/`; **32/32** tests (los 31 de antes y `optd_log_ring`).

- `optd_log_ring` (nuevo): orden y líneas enteras; vuelta del anillo (200 rondas); lleno → descartar, contar y nota
  en su sitio; nota del escritor; corte de líneas largas; registros crudos y su reserva; **el consumidor no toma
  nunca el candado** (un candado que falla si lo toma); `tail` monótono; pasadas acotadas en fin de línea; **un
  escritor bloqueado 1 s no retiene a 4 productores** (peor `push` medido ~0,3 ms; cada hilo en orden; notas =
  descartes); y `log.cpp` sobre un fichero en `%TEMP%`: rotación, 4 hilos × 2000 líneas sin pérdidas y en orden,
  línea > 1 KB, `flush`, `emergency` detrás de lo encolado, `close`, **reapertura** y **vaciado de salida con el
  escritor vivo** (todas las líneas, ninguna cortada).
- `optd` (capturas): un registro de hueco entre dos registros se lee con el lector de siempre.
- `optd_remote_motion` (+7): estela del canal (corriendo tarde = 0 m, congelado, desviado, canal viejo, quieto), el
  vigilante (pico corto, 1 línea/s, fin, fin por comparación perdida, umbral exacto), un vigilante por candidato,
  presupuesto con las líneas de fin, la identidad CC/mc antes de llamar al juego, `read_interp` sobre memoria falsa,
  el vigilante de la muestra mostrada y el texto de la línea.
- Prueba sin juego `cdmp_host` + `cdmp_peer`: el log se abre, rota, escribe el informe `Log:` y se cierra limpio.

## 6. Qué mirar en la próxima prueba

1. `Log:` cada 5 s en los dos PCs: `dropped 0`; en Shadow, `slowest` y `>= 250 ms` dicen si el disco sigue lento. Si
   hay una línea `Log writer: one write ... took X ms`, el disco se paró X ms **y nadie esperó**: el `5 s report`,
   `S3` y `Overlay diag` deben seguir a su ritmo y el túnel no debe caer.
2. `cdmp_packets_*.log` de Shadow: líneas `# DISK QUEUE` = la grabación perdió registros por disco lento.
3. `RemoteMotion divergence #N` durante los diálogos del anfitrión (A/B de §4 de la sesión 8): en el anfitrión, si el
   invitado en memoria se separa del canal y si el update sigue moviendo la muestra mostrada; en el invitado, el método
   y la acción del anfitrión mientras habla.

## 7. Ficheros

`src/mod/log.{h,cpp}`, `src/mod/log_ring.h` (nuevo), `src/mod/dllmain.cpp`, `src/mod/mod.cpp` (publicación del canal,
línea `Log:`, `Config`), `src/mod/config.{h,cpp}`, `src/mod/net/troc_sniffer.cpp`, `src/mod/render/{hud.cpp,
dx12_hook.{h,cpp}}`, `src/mod/session2/{guest.cpp, guest_s3.cpp, remote_motion.{h,cpp}, remote_motion_logic.h}`,
`tests/optd_log_ring_tests.cpp` (nuevo), `tests/optd_remote_motion_tests.cpp`, `CMakeLists.txt` y los tres `.ini`.
