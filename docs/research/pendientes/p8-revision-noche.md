# P8: revisión independiente de la noche del 10-10 (rango `9a2266b..0a6a66f`, rama `opcion-d`)

Revisión de solo lectura: sin cambios de código, sin commits y sin lanzar el juego. El código se contrastó con el
PE de solo lectura `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (capstone 5.0.7, Python `-I`, límites de
función sacados de `.pdata`) y con el código fuente de safetyhook que se compila (`build\_deps\safetyhook-src`).
Las RVA son del build 1.0.0.2976.

**[V]** = comprobado aquí (desensamblado o lectura del código). **[E]** = inferencia o hipótesis.

## 0. Resumen

- **No hay ningún hallazgo bloqueante.** La inyección por el NetworkThread copia bien la ruta nativa
  (asignación, empuje y liberación). Las dos llamadas a emisores nativos (2322 y 2810) construyen y destruyen
  el buscador exactamente como el juego. El descompresor del túnel resiste entradas malformadas.
- **Hay 5 hallazgos importantes:**
  1. Los `__except(EXECUTE_HANDLER)` alrededor de código del juego que toma candados.
  2. Las lecturas rápidas de C4, que están activas por defecto y desarman páginas de guarda.
  3. La regla de hilos de `child_hold`: cambia D-53 sin una clave y puede dejar una liberación sin plazo.
  4. El VEH de safetyhook, que retiene para siempre las páginas enganchadas. Es una hipótesis para el bloqueo
     del primer arranque.
  5. Dos ventanas pequeñas en `observer_hold_acks`.
- Lo demás son hallazgos menores y de robustez (§3). La lista de lo comprobado correcto está en §4.

## 1. Bloqueantes

Ninguno.

## 2. Importantes

### I1. `__except(EXCEPTION_EXECUTE_HANDLER)` alrededor de código del juego que toma candados: un fallo se convierte en un bloqueo silencioso o en un estado a medias

**Dónde:**
- `src/mod/session2/server.cpp:196-215`: `note_exception` devuelve siempre `EXECUTE_HANDLER`.
- `server.cpp:1082-1105`: `push_request`, en el paso `Push` = `0x35DF50`.
- `src/mod/session2/observer_scene_copy.cpp:217-225`: `guarded_emit`, que envuelve `0x2A4E4C0`.
- `src/mod/session2/observer_milestone.cpp:258-265`: `guarded_emit`, que envuelve `0x2999D80`.

**Evidencia [V]:**
- `0x35DF50` es el *push* de cola: toma el candado de Q0 (vf8) y señala.
- `0x2A4E4C0` hace lo siguiente:
  - escribe el contexto de envío TLS (`[TLS+0x250]+0x2B8 = 0`);
  - llena `ctx+0x2B0` mediante `finder->vf1`;
  - busca el *scope* (`0x2961CA0`);
  - construye y envía el paquete (`0x2A4E52E` y siguientes).

Una excepción a mitad de cualquiera de las dos funciones deja tomado el candado que tuviera. También puede
dejar el contexto de envío o un paquete a medio construir. El filtro la absorbe:
- el NetworkThread o el hilo lógico del servidor siguen corriendo con ese estado;
- el siguiente que pida ese candado se queda colgado sin volcado.

`p8-heap-inyeccion.md` §4.3 ya lo reconoce para Q0. El riesgo es el mismo en los dos emisores.

**Arreglo concreto:**
- Atrapar solo lo que es nuestro:
  - en `push_request`, solo el paso `Fill` (escrituras nuestras en un búfer recién asignado);
  - en `game_alloc`, solo `Alloc` (`0x47EF9B0` no tiene candado propio, §3 del documento del heap).
- Para `Push`, `BuildSession` y los dos `guarded_emit`, el filtro hace lo siguiente:
  - guarda la nota (ya lo hace sin asignar memoria);
  - devuelve `EXCEPTION_CONTINUE_SEARCH`.
  
  Así el fallo llega como cierre real, con su volcado de WER o de la captura propia, en el sitio verdadero.
  No queda un servidor colgado más tarde.
- Si se prefiere mantener el «parar y seguir», hay que congelar a la vez **todo** lo que llama al juego desde
  el mod (inyección, `observer_scene_copy`, `observer_milestone`, `child_hold`) y escribir una línea `ERR`
  que diga que puede haber un candado tomado.

### I2. Lecturas rápidas de C4 activas por defecto, con `fast_read`, que absorbe `STATUS_GUARD_PAGE_VIOLATION` sin volver a armar la guarda

**Dónde:**
- `src/mod/game/safe_memory.cpp:55-74`: `fast_read`, con `__except(EXECUTE_HANDLER)` para cualquier código.
- `src/mod/session2/guest_lifecycle.cpp:23-25`: `probe_read`.
- `src/mod/config.h`: `s2_c4_safe_reads = false`, que **por defecto** usa la lectura rápida.
- `server.cpp:1519-1600`: `server_capture_user2`, `server_host_field` y `server_barrier_read`.
- `fast_read` lo usan también `ui_pause.cpp:108-118` (en el hilo principal, cada fotograma con escala UI) y
  `child_hold` (`FastReads`).

**Evidencia [V código, E efecto]:**
- `safe_read` descartaba las páginas con `PAGE_GUARD` (`safe_memory.cpp:20`). `fast_read` las lee
  directamente.
- Si un puntero viejo o mal leído cae en la página de guarda de la pila de **otro** hilo, ocurre lo siguiente:
  1. el núcleo quita la guarda y lanza `0x80000001` en el hilo lector;
  2. el `__except` lo absorbe y la lectura «falla limpia»;
  3. el hilo dueño, cuando su pila crezca, no encuentra guarda;
  4. al pasar a la página reservada siguiente cae con una AV, en un sitio y un momento sin relación.
- La probabilidad es baja: casi todo lo leído es heap de mimalloc. El efecto es un cierre imposible de atribuir.

**Regla del proyecto:** «cambios de comportamiento tras claves desactivadas por defecto». Aquí es al revés: la
lectura nueva va por defecto y la antigua es la opcional.

**Arreglo concreto:**
- Volver a armar la guarda en el filtro de `fast_read`:
  ```cpp
  __except (fast_read_filter(GetExceptionInformation()))
  // fast_read_filter: si ExceptionCode == STATUS_GUARD_PAGE_VIOLATION, la dirección es
  // ExceptionInformation[1]; VirtualQuery de esa página y
  // VirtualProtect(página, 1, mbi.Protect | PAGE_GUARD, &old). Después, EXECUTE_HANDLER.
  ```
  Es barato, porque solo se ejecuta en el caso raro.
- Además, documentar `c4_safe_reads=0` como cambio por defecto. Otra opción es invertir el nombre de la clave
  (`c4_fast_reads=1`) para cumplir la regla.

### I3. `child_hold`: la regla de hilos cambia D-53 y `observer_hold` sin una clave y hace que la liberación dependa de un `NotifyAppear` nativo futuro

**Dónde:**
- `src/mod/session2/child_hold.cpp:975-1013`: `native_appear_thread` y `note_native_caller`.
- `child_hold.cpp:1466-1486`: la cola de `notify_detour`.
- `child_hold.cpp:1674-1677`: liberación aplazada de personajes.
- `child_hold.cpp:1983-1986`: NPC.
- `sweep_children`: «sweeps refused» con `count_deferred`.

**Evidencia:**
- [V] Las direcciones de retorno `0x2AF90F0` y `0x2AF92D6` corresponden a los dos únicos
  `call 0x2B000C0` (`E8 D0 6F 00 00` en `0x2AF90EB` y `E8 EA 6D 00 00` en `0x2AF92D1`). Los dos están en
  `0x2AF8AF0..0x2AF9A4A` según `.pdata`. La regla en sí está bien construida.
- [V código] Antes, D-53 liberaba en el hilo del manejador de `StartActionReq`. Ahora, si ese hilo no está en
  `g_native_tids`, la liberación espera a la **cola del siguiente `NotifyAppear` nativo**. Esto incluye:
  - el tope de 15 s de `character_hold`;
  - los *timeouts* de NPC: el respaldo de `ServerRunThread` solo suelta, nunca libera.
- [E] `NotifyAppear` nativo lo provocan los cambios de sector y de observadores. Con los dos jugadores quietos
  puede no llegar en mucho tiempo:
  - un hijo o un NPC retenido queda invisible para el otro sin plazo;
  - el «nunca soltar en silencio» de `character_hold` (§8.1 de `p8-character-hold.md`) deja de tener un
    tope real.
- El cambio afecta a `child_hold=1` aunque `character_hold=0` («D-53 cambia un poco», §9 del documento). Va
  contra la regla de las claves.

**Arreglo concreto:**
1. Poner la regla de hilos tras su propia clave (`notify_thread_rule`, desactivada por defecto) o tras
   `character_hold`, y dejar D-53 como estaba hasta medirlo con Shadow.
2. Sea cual sea la decisión: una línea `WRN` cuando una entrada lleve aplazada más de N s (2 s, por ejemplo)
   sin ningún `NotifyAppear` nativo. Debe incluir la edad y el hilo del manejador. Así la sesión 7 mide el
   coste en lugar de esconderlo.
3. Pendiente de verificar [E]: la cola de `notify_detour` llama a `NotifyAppear` mientras el llamador
   `0x2AF8AF0` está a mitad de su trabajo.
   - No se comprobó si ese llamador abre un *scope* de envío (`ScopeSenderAck` 2042) alrededor de la llamada.
   - Si lo abre, los 2005 de la liberación irían dentro del 2042. `observer_hold_acks` no los ve, y los
     paquetes retenidos acabarían como `Orphan` y se descartarían a los 2 s.
   - Vigilar en vivo el contador `2042 bundles while holding`.

### I4. El VEH de safetyhook retiene para siempre las páginas enganchadas. Hipótesis para el bloqueo de 2 min del primer arranque con `ui_pause_block=force`

**Dónde:**
- `build\_deps\safetyhook-src\src\os.windows.cpp`:
  - `:173`: `AddVectoredExceptionHandler(1, …)`, el **primero** de la cadena;
  - `:213-224`: `add_trap`, sin ningún `erase` en todo el archivo;
  - `:230-256`: el manejador.
- `src/mod/session2/ui_pause.cpp:401-423`: instalación en cuanto aparecen los bytes, con el juego arrancando.

**Evidencia [V código]:**
- Cada `enable` de un `InlineHook` o `MidHook` llama a `trap_threads`. Este registra las páginas de origen y
  de trampolín, y no las borra nunca.
- Desde entonces, **cualquier** AV de cualquier hilo cuya dirección (`ExceptionInformation[1]`) caiga en esas
  páginas recibe `EXCEPTION_CONTINUE_EXECUTION` (`:242-244`). El hilo repite la instrucción indefinidamente.
- Además, el manejador toma `std::mutex` en cada AV del proceso, incluidas las de nuestras lecturas SEH.

**[E]** `ui_pause` añade la página de `0x35CD990`, que el hilo principal ejecuta cada fotograma. Si algo, el
protector por ejemplo, provoca una AV a propósito sobre esa página (comprobación de integridad, cambio de
protección, recifrado), el hilo principal queda girando para siempre en `KiUserExceptionDispatcher`. Eso daría
justo «sin fotogramas» sin cierre.

No se encontró en estático otro mecanismo por el que el gancho bloquee:
- el desvío no toma candados;
- `walk_views` está acotado;
- el anillo no bloquea.

La otra explicación es ajena al mod: la activación o primera comprobación del protector en un primer arranque.
Que no se reprodujera en el segundo arranque encaja con las dos.

**Arreglo y diagnóstico:**
1. Si vuelve a pasar: `procdump -ma` del juego colgado.
   - Mirar si el hilo principal está en `KiUserExceptionDispatcher` → `safetyhook::trap_handler`, con `Rip`
     dentro de `exe+0x35CD000..0x35CE000`.
   - Si es así, la hipótesis queda confirmada.
2. Instalar `ui_pause` **después** de que el mundo exista (por ejemplo, cuando `LocalSource` dé `in_game`, o
   con el reloj `0x6C8CD35` en marcha), no en el arranque. En `force` además no hay motivo para tocar la
   pantalla de título.
3. Endurecer safetyhook (parche local): devolver `CONTINUE_EXECUTION` en una página trampa solo **mientras**
   haya un `trap_threads` en curso (un contador atómico), y borrar la trampa al terminar.

### I5. `observer_hold_acks`: dos ventanas en las que un paquete por actor puede adelantarse a su 2005

1. **2005 sin nada retenido** (`src/mod/session2/observer_hold_acks.cpp:157-160`).
   - `announce()` borra las entradas de esas claves bajo `g_mu` y devuelve vacío → `AckRoute::Pass`.
   - `take_frame` empuja el 2005 **fuera** del candado (`server.cpp:468-491`, la rama `else` de `Pass`).
   - Entre las dos cosas, otro hilo puede hacer `offer` de un 2404 o 2723 de esa clave. Como ya no está
     retenida, pasa y entra en la cola **antes** que el 2005. Es justo el fallo que la clave arregla.
   - **Arreglo:** si `announce` ha borrado al menos una entrada, empujar el 2005 bajo el candado y devolver
     `Pushed`, también con `after` vacío.
2. **`begin` después del reparto nativo** (`src/mod/session2/child_hold.cpp:1392`).
   - `observer_hold_acks_begin` se llama al final de `record()`, cuando las llamadas nativas del paso 1/2
     (`call_notify`) ya han salido.
   - Un paquete por actor del NPC retenido que otro hilo emita en ese intervalo pasa sin retener.
   - **Arreglo:** registrar las claves en `prepare()`, que ya conoce los veredictos, y soltarlas si la
     división se abandona (`reservation.give_back`).

Relacionado y menor: `AckHold::begin` en `Again` no refresca `since_ms`
(`observer_hold_acks_logic.h:180-183`), y `expire` lo usa (`:319`). Una clave retenida otra vez varias veces
caduca a los 40 s de la **primera** retención, y sus paquetes se pierden aunque su 2005 llegue después.
**Arreglo:** `e.since_ms = now_ms` en `Again`, o caducar por `first_ms`.

## 3. Menores

### Inyección (foco 1)

- **M1. La parada no se vuelve a comprobar dentro de la vuelta** (`server.cpp:1131-1150`). `pump_body` solo
  mira `g_halted_epoch` al principio. Si otra parte para la inyección mientras tanto (por ejemplo,
  `build_session` en `ServerRunThread`), esa vuelta sigue empujando hasta 256 búferes al juego, que puede estar
  dañado.
  - **Arreglo:** `if (g_halted_epoch.load()) break;` al principio del `while`.
- **M2. Ventana «sacado pero aún no contado»** (`server.cpp:1131-1141` frente a `:1595`).
  - Tras el `pop`, `g_q_size` ya bajó, pero `g_pb_pushed` todavía no ha subido. La barrera de C4 puede ver
    `early 0` y `pushed == seen`.
  - Hoy no se puede aprovechar: tras pedir el logout solo entran el 2549 y el 2001, y la barrera espera al 2001.
  - **Arreglo de robustez:** un contador `in_hand` que suba bajo `g_q_mu` en el `pop` y baje tras el empuje, y
    sumarlo a `r.early`.
- **M3. Instalación del gancho distinta a la de los otros** (`server.cpp:896`).
  - `g_pump_hook` usa `safetyhook::create_mid` directamente, sin `p8_install_mid` (no compara
    `original_bytes().size()` ni llama a `in_real_code`).
  - No es grave: los 39 bytes están comprobados y el único salto a ellos cae en el primer byte, así que
    incluso el salto largo de 14 bytes sería correcto.
  - **Arreglo:** usar `p8_install_mid(…, patch 7, kNetLoopPumpAob, …)` por coherencia.
- **M4. Vida de `ServerNet`** (`server.cpp:1121`).
  - Cada vuelta lee `[exe+0x6D69680]` y empuja a su `+0x20`. Si el anfitrión vuelve al título y el servidor
    en proceso se destruye con Req en cola, el empuje iría a una Q0 liberada.
  - Ya existía con el hilo del mod; ahora es otro hilo [E].
  - **Arreglo:** entregar solo si `server_net` es el `ServerNet` sobre el que se construyó la sesión 2 de la
    época (`same_server()`) y si existe `g_epoch_session[epoch]`.
- **M5. `g_phase.on_req` antes del encolado definitivo** (`server.cpp`, `inject`).
  - Si el segundo `push` da `Full` por la carrera con el hilo espejo, la fase ya avanzó y la Req se vuelve a
    ofrecer: dos llamadas a `on_req`.
  - **Arreglo:** mover `on_req` después del `push` con éxito, siempre antes de que el NetworkThread pueda
    entregarla (sigue siendo así, porque la entrega es posterior).

### Emisores nativos (foco 2)

- **M6.** El `on_delete` de `observer_stage` registra como «2810 nativo» la llamada anidada de nuestra copia,
  porque la ve su mismo gancho de entrada. Es ruido en el libro de medida.
  - **Arreglo:** saltar el evento si `observer_scene_copy` está en `t_in_copy`, o marcarlo.

### Túnel (foco 4)

- **M7.** `TrocLink` intercepta el opcode 2 (*caps*) en **los dos** sentidos (`troc_link.cpp:29-34`), aunque
  `troc_frame.h` dice «solo en lotes ToHostServer». Es inofensivo, porque ningún Ack del juego usa el opcode
  2, pero conviene filtrar por la dirección de entrada.
- **M8.** `decode_message` reserva hasta 256 KB (`out.assign`) **antes** de validar.
  - Con entrada remota, un par malicioso puede forzar 256 KB por mensaje y su descompresión O(n).
  - El coste está acotado por el ritmo del transporte.
  - Es aceptable; anotar el límite en el documento de seguridad del túnel.

### Salida del invitado y captura (foco 6)

- **M9.** `crash_uef` llama a `crashpad_present()` (`guest_s3.cpp:1088-1096, 1135`) **dentro** del filtro de
  nivel superior tras un cierre: `std::wstring`, `GetModuleFileNameW` y `GetFileAttributesW`.
  - Con el heap dañado, que es justo un caso típico (`_mi_page_malloc`), puede fallar dentro del filtro. El
  proceso terminaría sin la línea «ENDS NOW».
  - **Arreglo:** calcularlo una vez en `install_crash` y guardarlo en un atómico.
- **M10.** Dos cambios de comportamiento del invitado sin clave propia (regla):
  - `end_without_host` (`guest.cpp`, `TerminateProcess` con un campo cargado);
  - el `Terminate` tras la captura (este solo con `guest_crash_dump=1`, que ya es clave).
  
  El primero está justificado (evita `0x2045A6A`) y solo actúa en modo remoto, pero conviene una clave
  (`guest_goodbye_end=1`) o anotarlo como excepción a la regla.
  - Detalle: `field` es `out_3093 != 0`, acumulado y nunca puesto a cero (`guest.cpp:884`). Hoy no importa,
    porque una conexión remota nueva tras el título queda `end_remote`.

### Otros

- **M11.** `take_frame` (`server.cpp:494`): en el caso `Spill`, `p` ya se ha movido y
  `host_scene_diag_on_out` recibe un paquete vacío. Solo afecta al diagnóstico.

## 4. Lo comprobado y correcto

### Foco 1, inyección por el NetworkThread [V]

- **El punto del gancho.** `0x35BAD4` está en el bucle del NetworkThread (`0x35B800..0x35BE8B`, `.pdata`):
  - se llega por el *timeout* o la señal de `WaitForSingleObject(Q1, 16 ms)` (`0x35B9D2`, `je 0x35BAD4` en
    `0x35BAC6` o caída tras `cmp ebx,0x102`);
  - `r15` = `ClientNet` (`0x35B822`);
  - en ese punto no hay candado tomado (`vf+0x10` en `0x35BAC1` lo acaba de soltar).
- **Los 39 bytes del AOB** coinciden con el desensamblado. El parche de 5 bytes cubre solo la primera
  instrucción, de 7 bytes.
- **Elección del asignador idéntica a la nativa.** `game_alloc_raw` replica
  `gs:[0x58] → [_tls_index] → +0x1FD`:
  - PostPacket `0x35C692..0x35C6BA` usa el mismo tamaño `0x8030` y alineación `0x10`;
  - el propio NetworkThread hace lo mismo en `0x35B841..0x35B877`;
  - `0x47EF9B0` lee `_tls_index` en `0x6C88C50` y llama a `0x47EF648` sin candado.
- **Liberación local [V].**
  - `ServerRunThread` (`0x27734EA` *pop* Q0; `0x277352F`: solo si `[pb+0x800E] != 0` trata el búfer como
    petición interna y lee `[pb+0x8010]` como puntero) llama a `OnRecvPacket` `0x27735D3` y devuelve el pb
    con `0x35DF50` a `[ServerNet+0x78]` (`0x27735E4`). No lo libera.
  - El NetworkThread vacía Q3 `[ClientNet+0x28]` (`0x35BB7B`) y libera `[pb+0x8028]` con `0x47F02BC` (o
    `0x47F03BC` si `+0x1FD`) en `0x35BBB2..0x35BBC0`.
  - `push_request` escribe `+0x800E = 0`, `+0x8028 = pb` y el *magic*. El servidor nunca sigue la etiqueta
    `+0x8010`.
- **Orden y cotas.**
  - Una sola FIFO, vaciada solo por el NetworkThread, de uno en uno.
  - Como mucho 256 por vuelta y 512 en vuelo (`may_hand`, sin desbordamiento).
  - Cola de 65 536 Req / 32 MB, con reserva de control.
  - `Full` y `NotReady` retienen, y `g_early` mantiene el orden con *backoff* de 20 ms.
  - Ninguna pausa provoca el logout.
- **Época.**
  - `drop_other_epochs` al empezar época.
  - Comprobación por elemento de `p.epoch` frente a `g_want_epoch` antes de empujar.
  - La parada nunca se rearma y la cola se vacía en `server_inject_tick`.
- **Ritmo del 2001.** `RetryPacer` (250 ms, 240 intentos, una línea cada 5 s; la primera, en el acto). Se
  reinicia en cada `server_logout_user2` nuevo. No hay 2001 tras una parada.
- **Nunca los dos ganchos en un proceso.** En `mod.cpp:830-845`, con `guest=1` se pone `session2 = false`, y
  la instalación del anfitrión va en `late_init` solo con `session2`. `guest=1` con etapas del anfitrión se
  rechaza.

### Foco 2, emisores nativos [V]

- **2322.** La receta nativa `0x287F194..0x287F1DA` construye ToTarget en la pila, llama a `0x2999D80` y
  vuelve a poner la vtable base. `call_native` hace lo mismo. El constructor `0x2A757A0` y la vf0
  `0x2A75740` están desensamblados y coinciden.
- **2810.**
  - La receta nativa es `0x28797BB..0x2879826`: ToTarget de `[r12+0x18]`, luego
    `0x2A4E4C0(actor, &finder, &[rbp+0x320], rdi, &[rbp+0x338])` y la vtable base en `0x287981F`.
  - `on_delete` hace lo mismo con el personaje del usuario 2, y comprueba que `finder+0x10` es la sesión 2
    viva (destinatario correcto).
  - La quinta en `[rsp+0x28]` en la entrada es correcta.
  - El serializador solo lee `[r8]`, `[r9]` y el quinto argumento (`0x2A4E5C2` y siguientes).
- **Reentrada en su propio gancho de entrada.**
  - La copia corre en el *mid-hook* de la entrada, **antes** del cuerpo nativo.
  - La llamada anidada vuelve a pasar por el gancho; `t_in_copy` la deja salir sin hacer nada, se ejecuta el
    prólogo reubicado y sale el 2810.
  - Después el nativo vuelve a poner `[ctx+0x2B8] = 0` (`0x2A4E505`) y rellena su lista: no hay mezcla en el
    contexto TLS.
  - El *stub* de mid-hook de safetyhook guarda el contexto en la pila (reentrante).
- **Agrupación 2042.** Se rechaza si hay *scope* abierto (`+0x2AC`/`+0x2A0`, bytes `0x2961CBA` comprobados), si
  falta la marca de reino o si falta el contexto de envío.

### Foco 3, retenciones [V código]

- **Candados.** Orden `child_hold::g_mu` → (nada del juego) y `oha::g_mu` → candado de `g_outgoing`.
  - `record()` suelta `g_mu` antes de `observer_hold_acks_begin`.
  - `char_pass` y `release_npcs` llaman a `call_notify` sin `g_mu`.
  - No hay inversión de candados.
- **Crecimiento acotado:**
  - `AckHold`: 1024 claves, 32 paquetes y 32 KB por clave, 4 MB en total;
  - tablas de 16 (personajes) y del tamaño de NPC;
  - 8 hilos nativos.
- **Limpieza** por época (`server_begin_epoch`, servidor sustituido), por usuario 2 ausente o saliendo (tick
  de red) y por soltado.
- `character_hold` solo retiene `[[actor+0xA0]+0xD0] == actor` y nunca para el dueño.

### Foco 4, túnel [V]

- **`lz::decompress`:**
  - nunca escribe más allá de `raw_size` ni lee más allá de la entrada;
  - `read_length` está acotado por `raw_size`, sin desbordamiento;
  - `offset` es distinto de 0 y menor o igual que `op`;
  - la copia es byte a byte (solapada);
  - cada vuelta consume al menos 1 byte, así que termina;
  - el último token tiene que ser de literales y `op == raw_size`;
  - `raw` está acotado en 256 KB y el lote interior tiene que repetir la dirección.
- **Compresor:** las coincidencias se comprueban dentro de `kMaxOffset` antes de extenderlas hacia atrás, y
  siempre se emite el token final.
- **Negociación por conexión** (`ClientSession::epoch() ≥ 1` al conectar):
  - el anfitrión solo comprime con `tunnel_compress=1` y si el otro extremo lo anunció en la conexión
    **actual**;
  - el `reset` de `TunnelSender` en el cambio de época descarta un lote ya comprimido;
  - un anfitrión viejo descarta el *caps* como Req desconocida;
  - un invitado viejo no anuncia y recibe en crudo;
  - el receptor acepta las dos formas.
- **CPU:** como mucho 1 MB comprimido por `flush`; la regresión dio un tick máximo de 0,1 ms.

### Foco 5, `ui_pause` [V]

- `0x35CD990..0x35CDAA1` (`.pdata`) devuelve el valor en `xmm0`, guarda `rbx`, `rbp`, `rsi`, `rdi`, `r14` y
  `xmm6`, y su único argumento es `rcx`.
- Los dos llamadores (`0xAD143C` y `0xAD14D6`, en `0xAD1300`) solo usan `xmm0` y registros no volátiles tras la
  llamada (`xmm7`, `xmm10`-`xmm12`, `r15`, `rbx`, `rdi`).
- Ningún salto interno cae en `0x35CD991..0x35CD9AD`.
- Las fronteras `{5,10,15,20,22,26,30}` son correctas.
- El desvío es ABI estándar.
- `_ReturnAddress()` es la del llamador, porque el gancho entra por `jmp`.
- La carrera de `g_live` (cada 100 ms, *relaxed*) es benigna.

### Foco 6

- `crash_veh` devuelve siempre `CONTINUE_SEARCH`.
- `crash_uef` solo termina con una AV, `guest_crash_dump=1` y sin `crashpad_handler.exe`. En los demás casos
  encadena. No absorbe nada.
- `rearm_crash_filter` con `t_in_uef` evita el bucle.
- La reescritura del `jmp` de `game_errors` con `trap_threads(target, target, 5)` es correcta.

### Reglas del proyecto

- Ningún gancho nuevo en `.xtls` ni en *thunks*:
  - `0x35BAD4`, `0x35CD990` y los puntos H1-H3 y G1-G4 están en `.rsrc`;
  - `p8_install_mid` exige `in_real_code`, bytes iguales y el tamaño exacto del parche.
- Las sondas H y G son de solo lectura (ningún `ctx.x =`).
- El *tap* del *sniffer* corre en el hilo escritor.
- Las claves nuevas están desactivadas por defecto, salvo lo dicho en I2, I3 y M10.

## 5. Qué mirar en la sesión 7 (Shadow)

- `describe`:
  - `NotifyAppear threads N (other callers 0, …); releases deferred to one: children A, NPC passes B`;
  - en el bloque de personajes, `deferred to a NotifyAppear thread C`.
  
  Si A, B o C crecen con jugadores quietos, se confirma I3.
- `observer hold acks … 2042 bundles while holding` y `discarded: … released, but no 2005 for it was seen`:
  ver I3, punto 3, e I5.
- `inject queue … not yet taken max ≤ 512`, `EXCEPTIONS` ausente y `TLS+0x1FD set` ausente.
- Si el juego se queda sin fotogramas: `procdump -ma` antes de cerrarlo (I4).

## 6. Correcciones tras la revisión de la noche (2026-10-10, sin commit, para integrar)

Hechas sobre `opcion-d` (HEAD `7a26a45`) sin lanzar el juego ni tocar `bin64`. Cada cambio de comportamiento
va tras una clave apagada por defecto, salvo las dos excepciones razonadas (§6.2 y §6.7, M10). Estático contra
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (capstone, Python `-I`, `.pdata`). **[V]** comprobado aquí;
**[E]** inferencia.

### 6.0 Resumen

| Hallazgo | Qué se hizo | Clave (por defecto) | Test |
|---|---|---|---|
| I1 | Solo se atrapan los pasos propios; dentro del código del juego: nota + línea `ERR` sin asignar + `EXCEPTION_CONTINUE_SEARCH` | — (corrección) | `optd_inject` |
| I2 | El filtro de `fast_read`/`safe_read` vuelve a armar `PAGE_GUARD`; solo AV, `IN_PAGE_ERROR` y guarda; lo demás sigue | `c4_safe_reads` (0, justificado) | `optd_c4` |
| I3 | La regla de hilos va tras su propia clave; `WRN` si una liberación espera > 2 s; 2042 comprobado en estático | `notify_thread_rule` (0) | `optd_character_hold` |
| I4 | `ui_pause` se instala con el jugador en el mundo; safetyhook parcheado (copia en `third_party`); diagnóstico de arranque | — | `optd_hook_traps` (nuevo) |
| I5 | 2005 bajo el candado aunque no haya nada retenido; claves registradas antes de las llamadas nativas; `since_ms` se refresca | — (corrección) | `optd_scene_acks`, `optd_character_hold` |
| M1–M7, M9 | Hechos (§6.6) | — | `optd`, `optd_c4` |
| M10 | Clave propia | `guest_goodbye_end` (**1**, excepción razonada) | — |
| M8, M11 | M8 anotado (aceptable); M11 no hecho (solo diagnóstico) | — | — |

Build: `.\scripts\build.ps1` → **27/27 tests** (`100% tests passed, 0 tests failed out of 27`).

### 6.1 I1: el código del juego ya no se «absorbe»

**Regla nueva** (`src/mod/game/game_fault.{h,cpp}`, `fault_line.h`): el mod atrapa solo lo suyo. Dentro de código
del juego que puede tener un candado tomado o un estado a medias, el filtro:

1. apaga lo suyo (para que, si algo más arriba lo maneja, el mod no vuelva a llamar a ese código);
2. escribe una línea `ERR` directamente con `WriteFile` (`log::emergency`: sin asignar memoria y sin el candado
   del registro; el prefijo se formatea a mano en la pila):
   `<quién>: exception 0x… at exe+0x… (read|write|execute of 0x…) on thread N while <paso>: NOT handled by the
   mod (the game's code may hold a lock or a half-built state): passed on to the game's handlers as a real crash`;
3. devuelve `EXCEPTION_CONTINUE_SEARCH`.

| Sitio | Paso | Antes | Ahora |
|---|---|---|---|
| `server.cpp` `game_alloc` | `0x47EF9B0` (heap del hilo, sin candado propio [V] §3 de `p8-heap-inyeccion.md`) | atrapado | **atrapado** (`Alloc`), parada |
| | `0x47EFA78` (heap global, bajo su `CRITICAL_SECTION`) | atrapado | **sigue** (`AllocGlobal`) |
| `push_request` | relleno del búfer (nuestras escrituras) | atrapado | **atrapado** (`Fill`), parada |
| | `0x35DF50` (candado de Q0) | atrapado | **sigue** (`Push`) |
| `build_session` | `memset` y nuestras escrituras | atrapado | **atrapado** (`BuildSession`) |
| | `0x1436EE0`, constructor de `NwSession` | atrapado | **sigue** (`BuildCtor`) |
| `guarded_push` (Send de la sesión 2 → Q2) | `0x35DF50` | atrapado | **sigue** |
| `call_to_near` (relay_fix) | `ToNear::vf1` `0x2A75860` (toma el candado del actor) | atrapado | **sigue**; antes apaga el relay_fix |
| `observer_scene_copy` | `0x2A757A0` (ToTarget) | atrapado | **atrapado** (copia apagada, línea `copied`) |
| | `0x2A4E4C0` (emisor 2810) | atrapado | **sigue**; antes apaga la copia |
| `observer_milestone` | `0x2A757A0` | atrapado | **atrapado** (`FaultCtor`) |
| | `0x2999D80` (emisor 2322) | atrapado | **sigue**; antes apaga la emisión |

Evidencia de qué es «nuestro» [V]:

- `0x1436EE0` asigna cinco bloques con la misma elección `TLS+0x1FD ? 0x47EFA78 : 0x47EF9B0` (`0x1436F61`,
  `0x1436FF8`, `0x1437048`, `0x1437114`, `0x14371A4`) y, si una asignación falla, lanza ella misma
  `RaiseException(0xA0000002)` (`0x1436F85`). Antes, esa excepción del propio juego se habría absorbido.
- `0x2A757A0` solo escribe el buscador (en nuestra pila) y llama a `actor->vf(0xE8)`, que en todas las clases de
  actor del servidor es un captador sin candado: `ServerActor`/`ServerNormalInGameActor` `0x3600B0`
  (`xor eax,eax`), `ServerChildOnlyInGameActor` `0xAD83F0` (`mov rax,[rcx+0xD0]`), `ServerUserActor`
  `0x2A7D9E0` (`mov rax,[rcx+0x1F8]`). Un fallo ahí es un puntero de actor malo: se atrapa y se apaga.

La nota de `server.cpp` (`g_exc`, «EXCEPTIONS N (HALTED)») se sigue guardando también en los pasos del juego, y
`g_halted_epoch` se pone en el filtro: si algo más arriba maneja la excepción, la inyección ya está parada. El
texto de la nota dice «inside the game's code: passed on (a real crash)». `describe` de la sesión 2 añade
`GAME FAULTS passed on N` (solo se ve si el proceso sobrevive).

**[E] Qué pasa después del `CONTINUE_SEARCH`.** Los ganchos *mid* de safetyhook no registran información de
desenrollado para su *stub*; el recorrido de marcos que haga Windows a través de él puede no llegar al filtro de
nivel superior del juego (Sentry/crashpad) y acabar como excepción de segunda oportunidad (WER o depurador). En
todos los casos la línea `ERR` ya está escrita, en el sitio y el hilo verdaderos. Si se reproduce, un
`procdump -ma -e` del juego da el volcado.

### 6.2 I2: páginas de guarda

**Elección (con evidencia):** volver a armar la guarda en el filtro. «No consumirla» no es posible sin consultar
la página antes (el coste que `fast_read` quita): es el propio núcleo quien quita `PAGE_GUARD` al tocarla
(«a guard page provides a one-shot alarm», Microsoft Learn, *Creating Guard Pages*). El filtro común de las
lecturas del mod (`safe_memory.cpp`, `read_filter`):

- `EXCEPTION_ACCESS_VIOLATION`, `EXCEPTION_IN_PAGE_ERROR` → la lectura falla limpia;
- `EXCEPTION_GUARD_PAGE` → `VirtualQuery` de la página (ya sin `PAGE_GUARD`) y
  `VirtualProtect(página, 1, Protect | PAGE_GUARD)`; la lectura falla. La página queda como estaba, y el resultado
  es el de `safe_read`, que rechaza esas páginas sin tocarlas;
- cualquier otro código → `EXCEPTION_CONTINUE_SEARCH` (ya no se absorbe todo).

Contadores: `describe` de la sesión 2 añade `GUARD PAGES touched by the mod's reads: re-armed N, could not
re-arm M` si alguno no es 0.

**Test [V]** (`optd_c4`, `test_guard_page_reads`): sobre una página `PAGE_READWRITE | PAGE_GUARD`,
`safe_read` la rechaza sin tocarla; cuatro `fast_read` (tipada y por bytes) fallan y la guarda sigue armada
después de cada una; un acceso crudo posterior **todavía** levanta `STATUS_GUARD_PAGE_VIOLATION` y después la
página queda normal; una página reservada sin confirmar sigue fallando por AV.

**`c4_safe_reads` sigue a 0 por defecto (justificación).** La regla del proyecto es para cambios de
comportamiento. Con el filtro nuevo, `fast_read` y `safe_read` dan lo mismo para cada estado de página
(confirmada legible: el valor; libre, reservada o `PAGE_NOACCESS`: fallo; guarda: fallo y la página como
estaba). Solo cambia el coste. Volver a `VirtualQuery` por defecto traería de vuelta el problema medido
(vueltas de 250–920 ms del hilo de red, `p8-tunel-rendimiento.md` §2.3; hoy `optd_c4`: 0,035 us con SEH frente a
234 us con `VirtualQuery` en 256 MB). Diferencia residual [E]: con `fast_read` una lectura de un puntero viejo
genera una excepción de primera oportunidad que ven los VEH (los del juego y el de safetyhook, que ya no toma su
candado para las AV de lectura, §6.4); `child_hold` y `ui_pause` ya leían así. La clave sigue para el control
(`c4_safe_reads=1`), y el arranque lo dice en una línea.

### 6.3 I3: la regla de hilos tras su propia clave; `WRN`; 2042

**Clave** `[session2] notify_thread_rule` (anfitrión, **0 por defecto**; `config.{h,cpp}`,
`child_hold_set_thread_rule` antes de instalar):

- **0** = los hilos de antes de la regla, D-53 igual que antes de `737a0d7`:
  - tras `StartActionReq`, la liberación (hijos, NPC y personajes) va en el hilo del manejador (reino servidor);
  - el respaldo del hilo receptor solo libera hijos si es el último hilo que llamó a `NotifyAppear`
    (`g_appear_tid`, como antes);
  - no hay barrido de hijos en la cola de `NotifyAppear` (los NPC y personajes ya tenían su pasada allí).
- **1** = la regla de la noche (solo hilos que el juego usó de forma nativa para `NotifyAppear`).

**Aviso**, con las dos opciones (`hold::DeferWatch`, sin candados): una liberación debida que espera a un hilo
que pueda llamar a `NotifyAppear` abre un episodio. A los 2 s, una línea:
`WRN Session2 child hold: a due release has waited N ms for a thread that may call NotifyAppear (first left on
thread T; K deferral(s); children/NPCs/characters) | notify_thread_rule=X; native NotifyAppear threads M`. Cuando
corre una pasada que sí puede liberar: `Session2 child hold: the deferred release(s) ran after N ms`. `describe`
añade `notify_thread_rule X | … | longest wait of a due release for such a thread N ms (open now M ms)`.
Con 0 también puede salir, por la pasada de respaldo del hilo receptor, que solo suelta (ya antes).

**2042 alrededor del `NotifyAppear` nativo, comprobado en estático [V]:**

- El mecanismo de agrupación del contexto de envío (`ctx = [TLS+0x250]`):
  - `0x2961CA0` busca: `+0x2AC` (modo, 1 desde el constructor del contexto `0x2BB8880`, `0x2BB8B71`), `+0x2A0`
    (cuántos agrupadores), `+0x298` (la lista);
  - `0x2961E90` (y `0x2961DE0`) mete un paquete en un agrupador y pone `+0x2A8` (el agrupador en curso);
  - `0x2961F60` vacía el agrupador: escribe el opcode `0x7FA` (2042) en `0x296207B` y lo envía;
  - `0x2962220` cierra el agrupador en curso y pone `+0x2A8 = -1`;
  - `0x2961E90` y `0x2962220` tienen 507 llamadores cada uno (los emisores de Ack).
- El llamador `0x2AF8AF0..0x2AF9A4A`: sus 18 lecturas de `gs:[0x58]` son todas la elección `TLS+0x1FD` antes de
  liberar; ninguna lee `[TLS+0x250]`, y ninguna de sus llamadas directas es una de las funciones de arriba. Lo
  mismo en sus dos llamadores, `0x2AF8090` y `0x2AF86E0` (el `+0x250` de su llamada `0x2AF9A50` es un campo de
  objeto, `[rdi+0x250]`, no el TLS).
- **Conclusión:** el llamador no abre un agrupador alrededor del `NotifyAppear` nativo. El 2005 de una liberación
  en la cola del gancho encuentra el mismo estado de agrupación que el 2005 nativo que acaba de salir.
- **[E]** Un agrupador abierto más arriba en la pila no se puede descartar en estático (no se encontró dónde se
  registran los agrupadores en `+0x298`). El contador `2042 bundles while holding` sigue siendo la prueba en vivo.

### 6.4 I4: `ui_pause` y el VEH de safetyhook

**a) Cuándo se instala `ui_pause`.** El gancho de `0x35CD990` entra solo cuando el actor controlado del jugador
local existe durante 3 s seguidos (`WorldSystem+0x30` con la vtable `ClientActorManager`, `+0x50` con la vtable
`ClientChildOnlyInGameActor`; lecturas con `VirtualQuery`). Nunca durante el arranque ni en la pantalla de título
(tampoco con `force`). Líneas:

- `UiPause: waiting for the local player in the world before hooking … | game clock … | safetyhook trap handler: …`;
- cada minuto mientras espera (o al momento, como `WRN`, si un hilo falló en una página parcheada):
  `UiPause start-up: still waiting for the world after N s | game clock (0x1416B80) ±X ms in Y ms of wall time |
  safetyhook trap handler: redirected …, retried … | passed on: execute …, read/write … | patching now … | game
  faults passed on …`;
- `UiPause: the local player is in the world (N s after the game's code was ready): installing the hook`.

**b) Diagnóstico de «sin fotogramas».** Con el gancho puesto, si el bucle principal no llama a `0x35CD990` en
10 s: `WRN UiPause: NO FRAMES for N s (… minimized, or the main thread is stuck) | <reloj y trampas> | if the game
does not come back: a full dump before closing it (procdump -ma)`, y `UiPause: frames again …` al volver. Para
distinguir un bloqueo: si `retried` o `passed on` crecen, es una página parcheada; si están quietos y el reloj del
juego no avanza, es otra cosa (el protector o el juego). **[E]** No se ha comprobado si el reloj
`0x6C8CD68/0x6C8CD70` avanza en la pantalla de título.

**c) safetyhook parcheado.** `third_party/safetyhook-patch/os.windows.cpp` es el archivo de safetyhook v0.7.0 con
el manejador de trampas corregido (`os.windows.cpp.patch` es el diff, y `LICENSE` la licencia BSL-1.0). El
`CMakeLists.txt` lo compila **en lugar de** `src/os.windows.cpp` del árbol descargado: no modifica `build/_deps`,
así que un `configure` limpio lo reproduce. Antes comprueba el SHA-256 del original (normalizado a LF): si cambia
el `GIT_TAG`, el `configure` se para hasta rehacer la copia. Cambios:

- `trap_threads` solo pone las páginas en `PAGE_READWRITE` (no ejecutables) mientras escribe, así que solo
  provoca fallos de **ejecución** (`ExceptionInformation[0] == 8`).
- Un fallo de lectura o escritura en una página «trampa» ya no se reintenta nunca (`CONTINUE_SEARCH`). Se decide
  sin el candado del manejador; solo se cuenta si el candado está libre.
- Un fallo de ejecución dentro de los bytes parcheados se redirige al trampolín como antes, salvo que la
  dirección no cambie, la página no sea ejecutable y no haya parche en curso.
- Un fallo de ejecución en otro sitio de una página trampa se reintenta solo mientras hay un `trap_threads` en
  curso (contador atómico) o si la página ya es ejecutable (fallo nacido durante el parche y atendido después);
  si no, `CONTINUE_SEARCH`.
- Las entradas de trampa **no se borran**: borrar al terminar abriría una carrera (un hilo que falló durante el
  parche y cuyo manejador corre después necesita su `fix_ip`). Con las reglas de arriba, mantenerlas ya no puede
  dejar un hilo girando.
- Contadores para el diagnóstico (`cdmp_trap_stats`, `src/mod/game/hook_traps.h`).

**Test [V]** `optd_hook_traps` (nuevo): un gancho *inline* real sobre una función en una página propia RWX.

- Lectura de la página puesta en `PAGE_NOACCESS` → llega al `__except` del test (`read/write passed on +1`).
- Llamada a otra función de la página puesta en `PAGE_READWRITE` → también llega (`execute passed on +1`).
- El gancho sigue funcionando y se quita.
- **Con el `os.windows.cpp` original** (mismo test compilado contra él): se queda girando y el perro guardián lo
  termina a los 20 s (`exit 3 after 20 s`). Es la confirmación de la hipótesis de I4 en pequeño.

### 6.5 I5: las dos ventanas de `observer_hold_acks`

1. **2005 sin nada retenido.** `AckHold::announce` cuenta las claves con retención (`matched`). Si ha terminado al
   menos una, aunque no hubiera nada retenido, la ruta empuja el 2005 **bajo su candado** y devuelve `Pushed`.
   Un 2404/2723 que otro hilo ofrezca después ya no puede entrar en la cola antes que su 2005. Contador en
   `describe`: `holds ended by a 2005 with nothing held (queued under the lock) N`.
2. **Registro antes del reparto nativo.** `notify_detour` registra las claves que la división retiene al usuario
   2 (NPC y personaje, observador = sesión 2 viva) **antes** de las llamadas nativas de los pasos 1 y 2
   (`pre_begin_acks`, con los veredictos de `prepare()`). `record()` ya no las vuelve a registrar. Una clave
   registrada nueva que al final no queda en la tabla (tabla llena tras la división) se suelta
   (`observer_hold_acks_drop`). Una que ya estaba retenida (`Again`) se deja.
3. **`since_ms`.** `AckHold::begin` en `Again` lo pone a `now`: los 40 s cuentan desde la última retención.

Tests [V]: `optd_scene_acks` (`matched`; `test_hold_again_refreshes_expiry`; el test de caducidad existente se
ajustó a la regla nueva: la clave retenida otra vez en 5003 caduca a 5003 + 40 s, no a 5000 + 40 s) y
`optd_character_hold`:

- un 2697 del personaje del anfitrión emitido «por otro hilo» durante el `NotifyAppear` del dueño queda retenido
  y sale justo después de su 2005;
- un 2005 que cierra una retención vacía sale por la ruta (`Pushed`); uno sin retención, `Pass`.

### 6.6 Menores

- **M1** `pump_body` comprueba `g_halted_epoch` en cada vuelta del bucle.
- **M2** `g_in_hand`: sube bajo `g_q_mu` en el `pop` y baja tras contar el empuje; la barrera de C4 lo suma a
  `r.early`.
- **M3** `0x35BAD4` se instala con `p8_install_mid` en el anfitrión **y** en el invitado (`guest.cpp`):
  - dentro de `.rsrc`;
  - los 39 bytes comprobados;
  - safetyhook reubica exactamente la primera instrucción (`kNetLoopPumpPatch = 7`).

  El salto largo de 14 bytes ahora se rechaza (opción D apagada, con línea `ERR`). El lector de secciones pasa a
  ser común: `p8_exe_sections`.
- **M4** `pump_body` solo entrega si `server_accepts_guest()`, la misma prueba que hizo `inject` al encolar:
  sesión viva, o la construida en este mismo servidor esperando una época nueva. No se exige
  `g_epoch_session[época]`: el primer búfer de una época nueva es el que hace que el servidor la construya (C4r).
  Si el servidor se sustituye, las Req esperan; contador `turns without session 2's server`.
- **M5** `g_phase.on_req` después de un `push` con éxito, todavía bajo `g_q_mu`. El NetworkThread saca bajo ese
  candado, así que la fase avanza antes de que el búfer pueda llegar al servidor. `g_phase_mu` es un candado hoja.
- **M6** El gancho 2810 de `observer_stage` ya no anota como nativa la llamada anidada de nuestra copia
  (`observer_scene_copy_in_copy()`).
- **M7** `TrocLink` solo intercepta el opcode 2 si recibe lotes `ToHostServer`. Test en `optd`: hacia el invitado,
  el opcode 2 es del dueño.
- **M8** Sin cambio: `decode_message` reserva hasta 256 KB antes de validar; está acotado por el ritmo del
  transporte. Anotado en `p8-tunel-rendimiento.md` §7.
- **M9** `crash_uef` ya no asigna:
  - `crashpad_present()` se calcula una vez en `install_crash` (atómico);
  - la línea «ENDS NOW» se escribe con `log::emergency` (sin `std::format`; el filtro de detrás ya se nombra al
    instalar y en cada re-armado).
- **M10** `[session2] guest_goodbye_end` (invitado), **1 por defecto**. Es una excepción razonada a la regla:
  - con 0 se vuelve a un cierre del juego conocido (`exe+0x2045A6A`, sesiones 5 y 6), y un cierre del juego se
    envía a Pearl Abyss (`CLAUDE.md`);
  - solo actúa en modo remoto y con un campo del anfitrión cargado.

  0 = nunca `TerminateProcess` ni 2003/2002 local: el cliente sigue esperando su 2002 (el bloqueo antiguo; una
  línea `WRN`) y lo que llegue del anfitrión se sigue entregando. El detalle de `field` (`out_3093 != 0`,
  acumulado) no cambia.
- **M11** No hecho: solo afecta al diagnóstico S1 en el caso `Spill`.

### 6.7 Claves nuevas y valores por defecto

| Clave | PC | Defecto | Efecto |
|---|---|---|---|
| `notify_thread_rule` | anfitrión | 0 | 1 = regla de hilos de la noche (§6.3) |
| `guest_goodbye_end` | invitado | 1 | 0 = no termina el proceso tras el adiós con campo (§6.6 M10) |
| `c4_safe_reads` | anfitrión | 0 (sin cambio) | §6.2 |

`docs/research/ini/cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` llevan `notify_thread_rule=1` para medir la
regla como en la regresión de la noche. Para D-53 como antes, poner 0.

### 6.8 Qué mirar ahora (un PC y sesión 7)

- `Session2 child hold: notify_thread_rule=…` al instalar; `WRN … a due release has waited` y `longest wait of a
  due release` en `describe`.
- `UiPause: waiting for the local player in the world …` y luego `installing the hook` (ya no en el arranque);
  ningún `NO FRAMES` salvo con el juego minimizado.
- Ausencia de `GAME FAULTS passed on`, `GUARD PAGES touched` y de líneas `NOT handled by the mod`.
- `observer hold acks … holds ended by a 2005 with nothing held` (> 0 es normal) y `2042 bundles while holding`.
- Si el juego se queda sin fotogramas: las líneas `UiPause start-up`/`NO FRAMES` y `procdump -ma`.

### 6.9 Archivos

- **Nuevos:**
  - `src/mod/game/fault_line.h`, `game_fault.{h,cpp}`, `hook_traps.h`;
  - `third_party/safetyhook-patch/{os.windows.cpp, os.windows.cpp.patch, LICENSE}`;
  - `tests/optd_hook_trap_tests.cpp`.
- **Cambiados:**
  - `CMakeLists.txt`;
  - `src/mod/log.{h,cpp}`, `config.{h,cpp}`, `game/safe_memory.{h,cpp}`;
  - `src/mod/session2/`: `server.cpp`, `inject_logic.h`, `observer_scene_copy.{h,cpp}`,
    `observer_milestone.cpp`, `observer_stage.cpp`, `child_hold.{h,cpp}`, `observer_hold_acks{.h,.cpp,_logic.h}`,
    `ui_pause.{h,cpp}`, `guest.cpp`, `guest_s3.cpp`, `session2.cpp`, `p8_probes.{h,cpp}`, `offsets_s2.h`;
  - `src/common/troc_link.{h,cpp}`;
  - tests: `optd_c4`, `optd_character_hold`, `optd_inject`, `optd_scene_acks`, `optd_tests`.
