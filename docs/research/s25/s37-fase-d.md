# S37 fase D: re-armado rápido de U-1 cuando el anfitrión se va (`user2_fast_rearm`)

Fecha: 15-10-2026. Rama `opcion-d-faseD` (worktree `C:\Emulador CD-faseD`), sobre `165c4aa`; primera versión `112df45`,
corregida tras la revisión independiente (`build/research/fase-d-revision.md`, sin hallazgos de tipo crash; §8). Build del
juego **1.0.0.2976**, timestamp **0x6AB28F00**, base **0x140000000**: todo RVA de este documento vale sólo para esa build.
**No se ha ejecutado el juego, ni el peer, ni Shadow para esta entrega**: sólo `scripts/build.ps1` con sus pruebas
(72/74; las dos que fallan, `launch_2inst_script` y `host_save_restore_script`, se niegan porque había dos copias del
juego abiertas: «the game is running»; no tienen relación con este cambio). Prueba nueva: `optd_fast_rearm`.

Implementa la fase D de [s35-relevo.md](s35-relevo.md) (§6, «Fase D — rearmado y 2043») con los cambios obligatorios de
[AUDITORIA.md](s34-investigaciones/AUDITORIA.md) §4 (parte de rearmado) y de
[INFORME-A4](s34-investigaciones/datos/auditoria-codex-20261014/a4-contencion/INFORME-A4.md) («Rearmado rápido»). Los datos
son de [4-contencion-y-relevo.md](s34-investigaciones/4-contencion-y-relevo.md) §2 y §4 #3/#6. La historia de U-1
(armado, vaciado, ruta de temporizadores, reinicio de la cadena, periodo en ms) está en
[u0-resultados-s28.md](u0-resultados-s28.md) §8.

Todo va detrás de **una clave propia**, `[session2] user2_fast_rearm = 0 | probe | 1` (la auditoría pide no activar varias
políticas nuevas a la vez). **`0` es exactamente el comportamiento anterior**: ninguno de los caminos nuevos se ejecuta
(los ganchos y el hilo del módulo comprueban la clave antes de hacer nada, el veredicto de restos devuelve «pedir» y el
resumen no cambia; sólo la línea `Config:` nombra la clave). `probe` no llama ni escribe nada en el juego.

## 1. El problema

S34, paso 1b, salida del anfitrión de Hernand: **21,402565 s** (recepción→recepción, AUDITORÍA §4) entre la última baja de
motivo 19 en el invitado y el primer 2005 de las stages del usuario 2:

| Tramo | s | Causa |
|---|---:|---|
| baja → `[ARMED]` | 10,807702 | histéresis de armado: ≥ 10 celdas durante 10 s, muestreo de 1 s |
| `[ARMED]` → 2043 | 10,052223 | la cadena 2043 del usuario 2 estaba muerta y el armado encoló un 2043 periódico con un periodo entero (`EnqueueTimer delay 100`) |
| 2043 → primera alta recibida | 0,542640 | evaluación, ~174 ms de creación de stages y el 2005 |

Las dos primeras partes son tiempo muerto. La fase D ataca las dos, y sólo en el caso de la salida del anfitrión.

## 2. Diseño

Código: [`fast_rearm_logic.h`](../../../src/mod/session2/fast_rearm_logic.h) (pura, probada) y `user2_stages.cpp`
(integración); la clave en `config.*`, `mod.cpp`, `session2.cpp` y `user2_stages.h`.

### 2.1 (a) Armar sin histéresis sólo ante una salida real del anfitrión

La auditoría marcó **[F]** «un salto de ≤ 7 a ≥ 10 celdas prueba un viaje rápido»: la distancia no dice quién se movió ni
descarta una corrección, una lectura vieja, un cambio de campo o una muestra perdida. Por eso el salto sólo da un
**candidato** si se cumplen **todas** estas condiciones (`frm::fast_verdict`, por este orden; la primera que falla da el
motivo):

1. **Salto**: la muestra anterior válida a ≤ 7 celdas y la actual a ≥ 10 (`frm::jump`). Si no, `NoJump` (el caso normal,
   no se registra).
2. **Muestras consecutivas**: las dos válidas (posiciones leídas, mismo campo, personaje del usuario 2 conocido) y a
   ≤ 2,5 s una de otra (`NotConsecutive`). Una muestra inválida entre medias rompe la pareja; una muestra que el hilo no
   llega a tomar (personaje del usuario 2 a 0 durante < 10 s) no entra en el historial, y si el hueco sigue ≤ 2,5 s las
   dos de alrededor cuentan como consecutivas (las dos son válidas y del mismo personaje).
3. **Mismo campo** (`OtherField`) y **mismo personaje del usuario 2** (época, `OtherEpoch`) en las dos muestras.
4. **Quién se movió**: la celda del usuario 2 cambió como mucho 1 celda (`GuestMoved`: él viajó, o los dos) y la del
   anfitrión al menos `on − off` = 3 celdas (`HostStill`: el salto es un artefacto de lectura).
5. **Señal real de carga del anfitrión, y que sea la de su salida**: su paso de carga **L** (`0x287DA50` sobre su gestor,
   el paso del dueño en `SendMoveFieldAck`, el mismo gancho que ya marca `g_last_host_load_us` para M2) en los **últimos
   30 s** y no posterior a la muestra (`NoHostLoad`), **y posterior al inicio de la racha cercana** que termina en el salto
   (`LoadBeforeNear`, revisión 3a). El inicio de la racha es `near_since` de la histéresis leído **antes** del `step()` de
   esa muestra (que lo pone a 0 al pasar a ≥ 10). La L de la **llegada** del anfitrión siempre es anterior a esa racha
   (cargó cuando aún estaba lejos), así que una lectura vieja o corregida en los 30 s siguientes a su llegada no acelera
   nada. Una carga de salida larga (la posición cambia tarde) sí pasa: su L es posterior al momento en que ya estaba cerca.
   Sin racha conocida (máquina reiniciada en esa muestra), no.
6. **Nada en curso**: U-1 no abortado ni armado, la máquina de histéresis en OFF, ninguna petición pendiente (armado o
   vaciado), ningún vaciado retenido para la preparación del anfitrión (`g_hold_since`), `g_p_draining` libre (ojo: este
   indicador lo toman tanto el vaciado en la preparación del anfitrión como la limpieza de `npc_activation`; los motivos
   lo dicen), el usuario 2 sin cargar (`child_hold_user2_loading`) ni despidiéndose (`server_logout_requested`).
7. **No dos veces seguidas**: ningún otro candidato en los últimos **60 s** (`TooSoon`) y como mucho 64 por partida
   (`Capped`). Un candidato descartado cuenta igual.

**Confirmación (revisión 3b, «el invitado sigue al anfitrión»).** El candidato se confirma tras **3 s** más
(`frm::confirm_verdict`, una vez por muestra). Se descarta limpiamente, sin pedir nada, si en ese tiempo:

- el usuario 2 empieza una carga (la entrada de su Troc 2283/2362/2437, o `child_hold_user2_loading`): `GuestLoad`;
- aparece cualquier bloqueo del punto 6: `Blocked`;
- una muestra es inválida, o de otro campo o personaje: `Invalid`;
- pasan más de 5,5 s sin muestra: `Stale`;
- el anfitrión vuelve por debajo de 10 celdas: `NotFar`;
- el usuario 2 se mueve más de 1 celda: `GuestMoved`.

Así un invitado que viaja detrás del anfitrión en esos 3 s no provoca ningún armado. Si viaja más tarde, después del
armado, la carga del usuario 2 vacía su gestor como siempre. Esa carga se cuenta como `followed by a load of user 2
within 30 s`, en `probe` y en `1`, para medir cuánto ocurre.

Cualquier otro caso sigue con la histéresis de siempre: alejarse andando o a caballo pasa por la banda 8–9 y nunca es un
salto; un salto sin la L de la salida no acelera.

Con `1` (y `user2_stages=1`), al confirmar el candidato:

- La máquina pasa a ON y `control()` pide el armado **en el acto** (sin la espera de 10 s entre peticiones), marcado como
  rápido.
- Se ejecuta, como siempre, a la salida de la siguiente Req del usuario 2 en el hilo del servidor con el cerrojo del
  módulo, y **se vuelve a evaluar allí**:
  - **Distancia recalculada** desde los dos personajes con `actor_cell` y el campo de cada uno, como hace `prep_drain`,
    no con el `g_dist` de la muestra que lo disparó (revisión 3a). Si es desconocida, son campos distintos o hay menos de
    10 celdas, el armado se rechaza (`[arm refused] … a FAST arm whose distance … (N cells)`) y la máquina se reinicia:
    era un falso disparo.
  - **Carga, despedida o `g_p_draining`** (`frm::fast_still_valid`): si el usuario 2 empezó a cargar, pidió su despedida
    o `g_p_draining` está tomado, el armado se rechaza **dejando la máquina en ON** (revisión 3c). `control()` lo vuelve a
    pedir como armado normal pasados sus 10 s, que es lo que habría hecho la histéresis sola. Ya no se pierden los 1–2 s
    de un reinicio.
- El resto de comprobaciones de `arm_locked` (estado 0/1, listas vacías, campo del anfitrión, distancia ≥ 10 de
  `g_dist`, ranuras) no cambian.

Un vaciado ya no borra el indicador de armado rápido de una petición pendiente (revisión 3e): `arm_locked` decide con él.

### 2.2 (b) Un 2043 de un solo disparo en el armado rápido

En el armado rápido, después de `0x287F450` y de la reparación de la cadena de siempre, se encola **un** 2043 de un solo
disparo. No hay RVA nuevo: es la misma `ServerNet::EnqueueTimer 0x2774430`, la misma ranura de Troc `[exe+0x6FA3ED8]` y la
misma celda cero `[exe+0x5545F50]` que usa `queue_chain_2043`, en el mismo hilo y con el mismo cerrojo.

Comprobado en los exports de Ghidra (`D:\RE\CrimsonDesert_2976\exports\decomp\`):

- **La forma es nativa** (salvo la estática de Troc). `0x287EF60` (`0x287ef60.c:44-88`), cuando la preparación no está
  lista, encola un 2043 con `local_2fd = 0` (`+0xB..+0xE` = 0, línea 75) y `delay 10` (línea 85). Usa la estática
  `[exe+0x6FA3EC8]`; la cadena usa `[exe+0x6FA3ED8]`, y las dos salen de `Troc__Find` del 2043. El periódico de la misma
  función pone `local_2fd = 1` (línea 129) y `delay = raw·1000/100` (línea 142). El nuestro es ese payload con
  `+0xB = 0` (`frm::oneshot_2043_payload`), pero **sin** el `+0x68A = 1` de esa rama (línea 50), que haría que el tick
  volviera a preparar (`0x287dbb0.c:148`). El nuestro sólo evalúa.
- **Nunca crea una cadena.** El manejador `.xtls 0x1082F900` pasa `+0xB..+0xF` como `r8b`, `r9b` y tres bytes de pila
  (`0x1082f900.c`). `0x287DBB0` sólo reprograma con `param_3` (`+0xB`) ≠ 0 (`0x287dbb0.c:118-140`). Con estado 2, campo y
  sin preparación pendiente evalúa sus celdas y termina.
- **Se distingue del 2043 inmediato del vaciado.** `0x102C2A30` (el destino de `0x287DA50`) escribe `u16 0x100` en `+0xB`
  (`0x102c2a30.c:72`), así que su `+0xC` (`r9b`) vale 1. El nuestro tiene `+0xB = 0` y `+0xC = 0` (`frm::oneshot_shape`).
  El gancho T (un `create_mid` en la entrada de `0x287DBB0`) lee ahora también `r9b`.
- **Unidad del retraso**: el argumento `delay` cuenta 100 ms (periódico: `raw = 10` → `delay 100` → ~10,1 s medidos; la
  rueda `0xb8a1310.c` no trata de forma especial los retrasos pequeños). Se usa `delay 1` = **100 ms** (la rama nativa
  usa 10 = 1 s, el ritmo de reintento de una preparación «no lista»). El fallo S33/`53d70df` (tomar el argumento por
  milisegundos) no puede volver: toda comparación de tiempo usa `timer_2043_period_ms` = raw × 1000 (prueba
  `test_payload_and_delay`).

Contrato con la cadena periódica (los cambios obligatorios de A4):

- **El puntual no sustituye a la cadena.** `arm_locked` sigue encolando la periódica si está muerta o terminada, y
  `chain_kick_due` sigue reiniciándola una vez por armado.
- **No enmascara una cadena muerta.** El tick del puntual (o uno tardío) **no** actualiza `g_last_mgr2_tick_us` ni
  `g_chain_last_us`. El vigilante `NoTicks` y el reinicio ven sólo ticks de la cadena (prueba `test_chain_untouched`: si
  contara, el reinicio de una cadena muerta llegaría tarde).
- **Uno pendiente por armado** (`frm::OneShotSlot`, una palabra atómica: estado | motivo | generación). La generación
  sube con cada armado. Si ya hay uno pendiente, o ya se encoló uno para ese armado, no se encola otro (`NOT queued`).
- **Resultado de encolado comprobado**: sólo `EnqueueTimer` con resultado 0 deja el puntual pendiente. Con un error
  devuelto se gasta la generación y no queda nada pendiente. **Una excepción dentro de `EnqueueTimer` no se maneja**
  (revisión 2d): `call_fault` devuelve `EXCEPTION_CONTINUE_SEARCH` (`game_fault.cpp`), así que es un cierre real del
  juego, exactamente como en la llamada de la cadena, que es la misma función con los mismos argumentos y ya está medida
  en vivo. No hay rama «falló por SEH».
- **Consumo y cancelación trazables**:
  - `consumed`: el primer tick con esa forma en el gestor del usuario 2;
  - `cancelled`: con cualquier vaciado (motivo `Drain` o `Logout`), cambio de personaje o un armado nuevo;
  - `lost`: 5 s pendiente sin tick;
  - `late`: el primer tick con esa forma en los **5 s** siguientes a una cancelación o pérdida. Se cuenta y nunca vale como
    tick de cadena.

  Cada armado (también los de histéresis) devuelve a reposo un estado `cancelled`/`lost`/`late` anterior (`settle`,
  revisión 2a). Así un puntual nativo con la misma forma minutos después (el de la rama «no lista» de una preparación del
  usuario 2) es un tick normal. La hora del puntual y la de su cancelación o pérdida se guardan **antes** de cambiar el
  estado (revisión 2b), así que `expire()` nunca ve la hora de uno anterior.
- **La invalidación es lógica** (revisión 2e). El temporizador del juego no se puede sacar de su cola: si llega tras un
  vaciado (como mucho 100 ms después del armado), `0x287DBB0` hace lo que diga el estado del gestor (estado 1: descarga;
  `+0x68A`: preparación), igual que un tick de cadena. La exigencia de la auditoría de que «ningún temporizador pendiente
  sobreviva» se cumple, pues, sólo en la contabilidad, con ese residuo de ≤ 100 ms.
- **Nada viejo afecta a un armado nuevo**: los armados rápidos están separados ≥ 60 s, un puntual se da por perdido a los
  5 s y su retraso es de 100 ms. Así, un puntual tardío de la generación N no puede encontrarse con uno pendiente de la N+1
  (prueba).
- **Sólo en el armado rápido.** Los armados por histéresis no encolan puntual. En `probe` (y para todos los armados con la
  clave ≠ 0) se mide cuánto tarda el primer 2043 tras el armado (§3).

### 2.3 (c) El WRN espurio de restos (4-contencion §3, arreglo #6)

`[DRAIN requested] stages left in its manager after a drain` aparecía a mitad de un vaciado en la preparación del
anfitrión: el hilo del módulo había leído las listas antes de que terminara. Ahora, con la clave distinta de 0, el
veredicto `frm::leftover_verdict` es **por operación**:

- usa el estado real `g_p_draining` (tomado por el vaciado en la preparación **o** por la limpieza de NPC), o que un
  vaciado haya terminado después de la lectura (`g_last_drain_us ≥` la hora de la lectura);
- no usa una espera fija de 1 s;
- con `1` no se pide nada (`[leftover check skipped]`, como mucho una línea cada 10 s; se cuentan todas) y se vuelve a
  mirar en la siguiente muestra;
- con `probe` se registra `[leftover check would be skipped]` y se pide como antes;
- con `0` se pide como antes.

## 3. Modos y líneas

| Clave | Requisitos | Qué hace |
|---|---|---|
| `0` | — | nada nuevo (comportamiento anterior) |
| `probe` | `user2_stages` ≠ 0 | evalúa cada salto y su confirmación y registra `[would FAST REARM]`; mide lo que habría ahorrado; el WRN de restos sólo se marca |
| `1` | `user2_stages=1` (con `probe` queda en probe; sin `user2_stages`, apagado); el armado ya exige `user2_timer_route=1` | arma sin histéresis ante la salida confirmada del anfitrión y encola el puntual |

Líneas (prefijo `User2Stages`; en `probe` la etiqueta es `[would FAST REARM]`, en `1` `[FAST REARM]`):

- `… | candidate: the host's own departure | distance 1 -> 61 cells in 1.0 s (host moved 60 cells, user 2 0) | the host's
  load step (L) 0.8 s ago, its preparation (P) …, the near streak since 75.0 s ago (L after it) | history: … | user 2's
  chain: dead (…), chains alive on its manager N | host chains N (…) | confirming for 3 s …`.
- `… | candidate DROPPED after 2.0 s: user 2 started a load (it follows the host) | nothing asked …` (u otro motivo).
- `… | CONFIRMED after 3000 ms … | the arm is asked now (~7000 ms of hysteresis saved, estimated) and queues a one-shot`
  (en `1`, seguida de `[ARM requested] … | FAST …` y `[ARMED] … | FAST (user2_fast_rearm=1)`), o
  `… | CONFIRMED after 3000 ms | probe: nothing asked …`.
- `… | jump NOT taken: <motivo> | …`: un salto que no cumplía todo, con el contexto (p. ej. `LoadBeforeNear` para la L
  de la llegada).
- `… | user 2 started a load X s after the fast arm / would-fast: it followed the host …` (WRN).
- `[would FAST REARM] … | the hysteresis armed X ms after the would-fast: a fast arm would have saved X ms of hysteresis`.
- `[would FAST REARM] … | the first 2043 reached user 2's manager Y ms after the arm G: a one-shot (100 ms) would have saved
  ~Y−100 ms more (with the hysteresis ~… ms in all)`.
- `[would FAST REARM] … | NO hysteresis arm followed in 30 s …: a fast arm would have armed for nothing` (WRN: el anfitrión
  volvió o cambió de campo; es el coste de un falso positivo).
- `[FAST REARM] … | one-shot 2043 for arm G on user 2's manager …: QUEUED (result 0) | … | the chain's next tick would have
  come in N ms (chain queued anew / left as it was at the arm; period 10000 ms)`.
- `[FAST REARM] … | the one-shot 2043 of arm G reached user 2's manager … (state 2) D ms after it was queued: it evaluates
  its cells now | ~S ms before the chain's next tick | not counted as a chain tick`.
- `[FAST REARM] … | the first 2043 reached user 2's manager D ms after the fast arm G`.
- `[FAST REARM] … | the pending one-shot 2043 of arm G cancelled: …`, `… LOST …` (WRN) y `… late …`.
- `[arm refused] … | a FAST arm no longer valid at the Req …` / `… a FAST arm whose distance, recomputed … (N cells)`.
- `[leftover check skipped]` / `[leftover check would be skipped]`.

Resumen (cada 60 s, al final de `U-1 (...)`, sólo con la clave ≠ 0): `fast re-arm (user2_fast_rearm=…): jumps <= 7 -> >= 10
N, verdicts: […] | candidates N, dropped while confirming: […] | followed by a load of user 2 within 30 s N | fast arms N
(refused at the Req N, of them by the distance recomputed there N), would-fast followed by the hysteresis N (saved mean/max
ms), not followed N | one-shots queued/consumed (delay max, saved mean/max)/cancelled/lost/late/failed | first 2043 after
an arm: N (mean/max ms) | leftover checks skipped N (would N) | user 2's chains alive N`. El recuento de cadenas del
usuario 2 usa sus ticks que reprograman, con el mismo estimador que las del anfitrión (`count_chains`).

## 4. Cambios obligatorios de la auditoría

| Exigencia (AUDITORÍA §4 / INFORME-A4 / s35-relevo) | Cómo se cumple |
|---|---|
| El salto no prueba un viaje: hace falta carga/viaje **del anfitrión**, mismo campo y época, muestras válidas consecutivas, invitado disponible, sin prepare/drain; reevaluar antes de actuar; el resto con histéresis | §2.1: condiciones 1–7 (la L posterior al inicio de la racha cercana), la confirmación de 3 s y la reevaluación en `arm_locked` (distancia recalculada) |
| Puntual separado de `queue_chain_2043` (que pone `+0xB=1` y toca `g_chain_last_us`) | `queue_oneshot`, payload propio, no toca la cadena |
| Uno pendiente por sujeto/época/armado; resultado de encolado comprobado; consumo/cancelación trazables; sólo en hilo del servidor con el cerrojo | `OneShotSlot` (generación por armado, se cancela con un personaje nuevo, `settle` en cada armado); `res == 0`; líneas y contadores; dentro de `arm_locked` |
| Un puntual no sustituye la cadena periódica: mantener su reparación | la reparación y `chain_kick_due` siguen igual |
| No confundir puntual con cadena en el vigilante y las estadísticas | el tick puntual o tardío (ventana de 5 s) no actualiza `g_last_mgr2_tick_us` ni `g_chain_last_us`; contadores separados |
| Medir el retraso de cadenas viejas | `chains alive on its manager` del usuario 2 (sus ticks que reprograman) y del anfitrión en cada línea y en el resumen (sólo medida; el criterio de 1,5 periodos no cambia) |
| Invalidar trabajo viejo al vaciar, salir o rearmar; ningún temporizador pendiente sobrevive | **en parte**: cancelación lógica en `drain_now` (todos los vaciados, también el de su logout), en un armado nuevo y con un personaje nuevo. El temporizador del juego sigue en cola ≤ 100 ms y equivale a un tick de cadena (§2.2) |
| Política si `EnqueueTimer` falla | error devuelto: gasta la generación, nada pendiente; excepción: el cierre del juego, igual que la cadena (§2.2) |
| WRN de drenaje con el estado real, no con 1 s fijo | §2.3 |
| Hueco < 2 s **[NV]** hasta medirlo | §5 |
| Conservar stages del usuario 2 al llegar el anfitrión | **fuera** (cambia U-1; no se toca) |

## 5. Qué hay que medir en vivo

Primero con dos copias en un PC (`docs/research/dos-instancias.md`, `scripts\launch-2inst.ps1`). Inis:

- anfitrión `docs/research/ini/cdmp.2inst-host-faseD.ini` (= `cdmp.2inst-host.ini` + `user2_fast_rearm=probe`);
- anfitrión `cdmp.2inst-host-faseD-1.ini` (lo mismo con `user2_fast_rearm=1`);
- invitado `cdmp.2inst-guest-faseD.ini` (= `cdmp.2inst-guest.ini`; la clave es sólo del anfitrión).

En la primera prueba, comprobar que la línea `Config: … user2_fast_rearm 'probe'` sale con el valor esperado. Después, lo
mismo con Shadow (invitado) por Steam.

**Guion** (cada paso con `probe` primero y luego con `1`, reiniciando los dos procesos):

1. El invitado se queda quieto en una ciudad (p. ej. Hernand). El anfitrión llega con viaje rápido, espera a que se vacíen
   las stages del usuario 2 y a que aparezcan las suyas (≥ 30 s), y **sale con viaje rápido** a otra ciudad (≥ 11 celdas).
   Allí espera 60–90 s. Repetirlo 3–4 veces, con ≥ 60 s entre salidas.
2. Control negativo: el anfitrión se aleja **a caballo** más de 10 celdas. No debe salir ningún `FAST`; el armado llega
   por histéresis (`would ARM` a los 10 s).
3. Control negativo: el **invitado** viaja rápido lejos del anfitrión. Debe salir `jump NOT taken: user 2 moved`, o
   ningún salto si cambia de campo.
4. **Control negativo: el invitado sigue al anfitrión** (revisión 3b). El anfitrión sale con viaje rápido y el invitado
   viaja tras él, (a) a los 1–2 s y (b) a los 5–10 s. En (a) debe salir `candidate DROPPED …: user 2 started a load`
   y ningún armado. En (b) habrá un armado rápido seguido de `… it followed the host` y del vaciado por su carga: es el
   coste aceptado; contarlo.
5. **Llegada y lectura corregida** (revisión 3a): tras cada llegada del anfitrión, ningún candidato. Si hay un salto en los
   30 s siguientes, su motivo debe ser `LoadBeforeNear`. En cada candidato, la línea dice si la L fue `after` el inicio de
   la racha cercana; tiene que ser siempre `after`.

**Medidas** (horas por `qpc`, como en S34):

- **El hueco**: desde la carga del anfitrión (su L: la línea `[owner load]` con `(host)`), o desde la última baja de
  motivo 19 en el invitado, hasta el **primer 2005 de las stages del usuario 2 recibido por el invitado**. Con `probe`
  debería salir como en S34 (~21 s), y las líneas `[would FAST REARM]` deberían predecir unos 7 s (10 s de histéresis
  menos los 3 s de confirmación) + 10 s de ahorro. Con `1`, objetivo < 5 s **[NV]**: `[ARMED]` − L ≈ 3–4 s (confirmación
  + la siguiente Req) y `[create] (user 2)` − `[ARMED]` < 0,5 s.
- **`[ARMED]` respecto a la P del anfitrión**: con el armado rápido, la primera evaluación del usuario 2 puede caer
  durante la pantalla de carga del anfitrión (entre su L y su P), cosa que antes casi no pasaba. Hay que comprobar que
  llega `reached … (state 2)` y que no hay ningún `ABORT` (en particular `HostTouched`) ni líneas raras de evaluación o
  de U-a2 en esa ventana.
- **El puntual**: `QUEUED (result 0)` y `reached … (state 2) ~100 ms after`, ningún `LOST` ni `late` y `failed 0`. Si el
  estado no es 2 al llegar, el puntual no sirve y hay que saber por qué.
- **Las cadenas**: el número de cadenas vivas del usuario 2 no sube con el puntual (1 tras el armado), las del anfitrión
  siguen su patrón nativo (`host chains`), no hay más de un `[chain restarted]` por armado ni ningún `[ABORT]` (sobre todo
  `NoTicks` y `NotRunning`), y `[host cadence]` no cambia.
- **Falsos positivos y armados inútiles**:
  - `not followed` (sin armado por histéresis en 30 s) = 0 en el guion 1;
  - los controles 2, 3 y 4a no dan `FAST`;
  - `followed by a load of user 2` se cuenta en todo el guion y sobre todo en el 4b.
- **La llegada** sigue igual: `[DRAIN held]` / `[drain at host prepare]` y ningún `[leftover check skipped]` que se coma
  un resto real. Con la clave a `1` no aparece el WRN de restos de S34; con `probe` aparece junto a su `would be skipped`.
- En el invitado: los NPC del usuario 2 vuelven (con U-e a 1, controlados por el invitado; esta fase no lo cambia).

**Criterios para no pasar a `1` con Shadow** (medidos con `probe`):

- cualquier `ABORT`;
- un `not followed` en el guion 1;
- un candidato confirmado en los controles 2, 3 o 4a;
- un candidato con la L anterior a la racha cercana;
- un puntual que no llegue en estado 2;
- más cadenas del usuario 2 que antes;
- en una sesión de juego normal (no el guion 4), `not followed` + `followed by a load of user 2` por encima de **1 de
  cada 4** confirmaciones: con más, el armado rápido crea y vacía stages inútilmente con demasiada frecuencia y hay que
  alargar la confirmación (`kConfirmUs`).

## 6. Límites y riesgos

- **La señal de carga sigue siendo indirecta [P]**: la L del anfitrión en los últimos 30 s y después del inicio de la racha
  cercana, que se moviera él y no el invitado, y 3 s de confirmación. Si el anfitrión carga y vuelve en poco tiempo, el
  armado rápido puede ser innecesario: el vaciado por distancia (≤ 7 celdas durante 5 s) lo deshace como hoy. `probe` lo
  cuenta en `not followed`.
- **El invitado que sigue al anfitrión más de 3 s después** provoca un armado y su vaciado (stages creadas para nada,
  limpieza de U-e). Se cuenta (`followed …`) y tiene umbral (§5).
- **El puntual cambia el momento de la primera evaluación, no lo que hace.** Evalúa con la celda cero, igual que el primer
  tick de la cadena. Si entre el armado y su tick (100 ms) el gestor pasa a estado 1, descarga como haría la cadena; si
  pasara a `+0x68A ≠ 0` (una preparación del usuario 2), ese tick llamaría a la preparación (`0x287dbb0.c:148`), igual
  que lo haría el periódico. La ventana es de 100 ms y el armado exige `+0x689/+0x68A = 0`.
- **Un puntual nativo con la misma forma**: el de la rama «no lista» de la preparación del usuario 2 (sus cargas). Sólo se
  confundiría con el nuestro si coincidiera dentro de esos 100 ms, o como «tardío» en los 5 s siguientes a una cancelación;
  `settle` en cada armado limpia el resto.
- **`g_p_draining`** lo comparten el vaciado en la preparación del anfitrión y la limpieza de `npc_activation`: sus
  veredictos (`Draining`, `Blocked`, el rechazo en la Req, los restos) son más amplios que «vaciado en la preparación».
- **No cambia la llegada del anfitrión** ni la decisión estructural (conservar stages), ni U-e, ni la contención.
- Los parámetros (2,5 s, 30 s, 3 s, 60 s, 5 s, 64, `delay 1`) son experimentales y no leyes del motor.

## 7. Pruebas

`tests/optd_fast_rearm_tests.cpp` (`optd_fast_rearm`):

- la salida de S34 1b (1 → 61 celdas, L 0,2 s después de la muestra cercana, racha cercana desde un minuto antes) da
  `FAST`, también con L 29 s antes dentro de la racha;
- **la llegada** (L 20 s antes, racha cercana desde 15 s antes, una lectura a 12 celdas) da `LoadBeforeNear`, igual que
  L en el mismo instante que el inicio de la racha o sin racha conocida;
- **la confirmación**: `Go` a los 3 s y `Wait` antes; una carga del usuario 2 después del salto (sigue al anfitrión) da
  `GuestLoad` (una anterior no cuenta); bloqueo, muestra inválida u otro campo, hueco, vuelta por debajo de 10 celdas y
  movimiento del usuario 2;
- alejarse andando (7→8→9→10) no salta; un salto sin L, o con L viejo (31 s) o futuro, no acelera;
- quién se movió (el invitado, los dos, 1 celda de margen), muestras no consecutivas o inválidas, otro campo u otro
  personaje;
- cada estado pendiente, los 60 s y el tope, y la reevaluación en el armado;
- payload y unidad del retraso (con la relación S33 periodo/retraso);
- la ranura: uno por generación, consumo único, cancelación, pérdida, tardío con su ventana de 5 s, `settle` en cada
  armado (nunca sobre uno pendiente), encolado fallido y armado → vaciado → armado;
- que contar el puntual como tick enmascararía `chain_kick_due`;
- el tiempo ahorrado, el veredicto de restos y el historial.

## 8. Revisión de `112df45` (`build/research/fase-d-revision.md`)

| # | Sev. | Corrección |
|---|---|---|
| 3a | wrong | La L tiene que ser posterior al inicio de la racha cercana (`LoadBeforeNear`); la distancia del armado rápido se recalcula en `arm_locked` desde los dos personajes |
| 3b | wrong | Confirmación de 3 s (se descarta si el usuario 2 empieza a cargar); contador `followed by a load of user 2`; control negativo 4 y umbral en §5 |
| 2a | minor | `settle` en cada armado y ventana de 5 s para `late` |
| 2b | nit | hora guardada antes del cambio de estado (`queued`, `cancel`, `expire`) |
| 2c | nit | «encolado por el juego pero rechazado por la ranura» se registra aparte |
| 2d | minor | quitada la rama inalcanzable «falló por SEH»; el texto dice que es un cierre real, como en la cadena |
| 2e | minor | §4: la invalidación es lógica, con un residuo de ≤ 100 ms |
| 3c | minor | el rechazo por carga, despedida o `g_p_draining` deja la máquina en ON; los motivos dicen qué cubre `g_p_draining` |
| 3d | nit | texto del punto 2 de §2.1 |
| 3e | nit | un vaciado ya no borra el indicador de armado rápido |
| §4 nit | nit | `[leftover check skipped]` limitado a una línea cada 10 s |
| §8 nit | nit | los tres `.ini` en CRLF como los demás |
