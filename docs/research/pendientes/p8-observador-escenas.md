# P8: el usuario 2 como observador de las escenas y secuenciadores ya existentes

9 de octubre de 2026, rama `opcion-d`. Crimson Desert PC **1.0.0.2976**, base **0x140000000**, direcciones como
**RVA exe+0x…**. Opcodes en decimal, offsets en hexadecimal.

**[V]** = comprobado en instrucciones del PE autorizado, capturas, registros conservados, código o compilación, con la
evidencia citada. **[E]** = inferencia o propuesta no probada en ejecución. No se arrancó el juego, no se tocó `bin64`
(las capturas de allí solo se leyeron), no se modificó ningún proyecto de Ghidra y no se hizo commit.

Responde al encargo de `p8-raiz-invitado.md` §6 filas 1–2 con la evidencia nueva de la sesión P8 5. Parte de
`p8-implementacion.md`, `p8-validacion.md`, `p8-impl-s1s2.md` y `opcion-d-campo-compartido.md` §3.3.

## 1. Resumen

- **La hipótesis de raíz común de S1 y S2 queda confirmada en lo esencial [V].**
  - El servidor manda los secuenciadores del campo **solo al cliente del dueño**: `PrepareSequencerAck` 2661,
    precedidos de `StageBranchStateAck` 2981.
  - Esos 2661 **llevan los enlaces nodo→actor de los NPC**.
  - Los 15 NPC que fallan en el invitado con `eErrNoInvalidActionChart` están **todos** enlazados en algún 2661 que
    recibió el anfitrión y que el usuario 2 no recibió nunca.
  - El hito 2322, que es lo que espera el estado 0x11, se emite al final de esa misma preparación y solo para el dueño.
  - Evidencia: §2.1–§2.4 y `observador/cruce-npc-fallidos.txt`.
- **Matiz [V]:** el enlace es necesario, no suficiente.
  - Diez NPC enlazados no fallan: los que se guardaron con una acción de su propio gráfico (por ejemplo `B0100079`,
    hash `0x92C685DE`, restaurado bien).
  - Falla el que llega con una acción del gráfico dinámico del secuenciador (`seq_bodyanim_action_*`).
- **El camino nativo de «instantánea para un observador» no existe en esta build y no se puede aislar sin
  reimplementar [V]** (§5):
  - El 2661 solo lo produce la preparación de secuenciadores `0x287C200`, que valida, resuelve actores y construye la
    lista de enlaces en el momento.
  - El 2981 solo se emite desde `.xtls` (`0x103FB3FD`).
  - `0x2910DE0` y `0x287EF60` son transiciones y preparación del mundo del dueño, no instantáneas.
- **Por eso no se envía nada [decisión].** Tal como pide el encargo, sin instantánea no se manda el 2322 solo, no se
  fuerza `M+0x231` y no se llama a `0x287EF60`.
- **Lo que se ha implementado:** la clave `[session2] observer_stage_sync`, a 0 por defecto.
  - Con **1**, solo mide, en modo lectura:
    - registra cada 2661/2810/2981/2322 que serializa el servidor (opcode, tamaño, destinatario y enlaces);
    - mantiene un libro de los secuenciadores vivos;
    - cuando el usuario 2 entra en un campo que no es suyo, escribe **exactamente lo que le falta** (secuenciadores,
      NPC enlazados y el 2322 de su personaje) como «PLAN, NOT SENT»;
    - escribe un seguimiento a los +10, +60 y +300 s.
  - Con **2 o más** (modo envío): se rechaza y funciona como 1.
- **Alternativa necesaria (§6):** extender el túnel con una réplica byte a byte de los 2661/2981 que recibió el dueño y
  un 2322 nativo para el observador. Antes hace falta una sonda en el invitado sobre el manejador de 2661 (`0xC05BB0`),
  porque no está demostrado en qué gestor colocaría el cliente del invitado un secuenciador cuyo dueño es el personaje
  del anfitrión.
- **Pruebas:** `.\scripts\build.ps1` compila y pasan **18/18 pruebas**, incluida la nueva `optd_observer_stage`, que
  reproduce los 23 paquetes 2661/2810/2322 reales de la captura del anfitrión.
- **Fase 2 (§13–§20, mismo día):** la réplica byte a byte de los 2661/2981 del dueño **no es coherente** y no se
  implementa. El cliente del invitado enruta todo Ack de escena al gestor del dueño de **su** campo (su propio
  personaje), mientras que los NPC buscan el secuenciador en el gestor del personaje del anfitrión. Se añade la sonda
  de solo lectura `guest_stage_diag=1` en Shadow y tokens `corr=` en las líneas del anfitrión.

## 2. Evidencia

### 2.1 Material

| Ref. | Archivo | Qué es |
|---|---|---|
| HC | `D:\SteamLibrary\…\bin64\cdmp_packets_20261009-203910.bin` (solo lectura) | Tráfico del **cliente propio del anfitrión** (dueño), sesión 5 |
| HL | `C:\Emulador CD\backups\logs\cdmp_20261009-2038_p8g-host.log` | Log del anfitrión, sesión 5 |
| GC | `C:\Emulador CD\backups\p8-sesion5-shadow\cdmp_packets_20261009-194117.bin` | Tráfico del cliente del usuario 2 (Shadow) |
| GL | `C:\Emulador CD\backups\p8-sesion5-shadow\cdmp.log` | Log del invitado (sondas `P8Probe` y `GuestDiag`) |
| PE | `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` | SHA-256 `57DA440D…E1E7`, recalculado hoy |
| Decomp | `…\p8-raiz-work\ghidra\exports\decomp\0x287ef60.c`, `0x2bc7670.c`, `0x9ed290.c` | Exportaciones existentes, solo lectura |

- **Scripts y salidas reproducibles:** `docs/research/pendientes/observador/`.
  - Python `-I` y Capstone 5.0.7, solo lectura.
  - El lector CDPK es `scripts/cap.py`, con el formato de `src/common/capture_file.h`.

### 2.2 Lo que recibe el dueño al entrar en su campo (HC) [V]

Segunda entrada del anfitrión, la de su campo de mundo: `2549` en t=49,555 y `2322` en t=50,926. Los recuentos
S>C por ventana están en `observador/histogramas.txt`.

| Ventana | Qué llega (S>C) | Lectura |
|---|---|---|
| 49,55–49,70 | `2144`, `3066`, 5 × `2724` (tipo 04, actualización), datos de usuario y misiones | cierre de la entrada |
| 49,70–50,40 | 98 × `2264` UpdateStageState, 17 × `2195` CreateFactionSpawnStage, 2 × `2724` | sincronización de escenas del dueño |
| 50,40–50,93 | **35 884 × `2264`**, 333 × `2195`, 147 × `2165`, 38 × `2335`, 18 × `2248`, **12 × `2981` + 12 × `2661`**, y **`2322`** | preparación (`0x287EF60` y temporizadores 2043) |
| 50,93–51,5 | 5 859 × `2665` SyncSceneObject… | objetos de escena |

- En total, **19 × 2661**: 18 entre t=50,42 y t=53,35, y uno más en t=477,5. También hay **3 × 2810**.
- Todos van dirigidos al personaje del dueño `A0100001`, con dueño de secuenciador `A0100001`
  (`observador/host-2661-2810.txt`).
- El tamaño de cada 2661 cumple **5 + 91 + 11·n**, con n = número de enlaces.
- La primera entrada del anfitrión, al campo de aparcamiento vacío, recibe `2144`, `3066` y `2322` en el mismo
  milisegundo, sin ningún 2661: allí no hay secuenciadores.

### 2.3 Lo que recibe el usuario 2 (GC) [V]

| Viaje | Qué llega | Falta |
|---|---|---|
| 1.º, a su propio campo (dueño): `2549` t=28,345 | `2144`, `3066`, **`2322`** (actor `A0100334`) en 28,844 | nada |
| 2.º, al campo del anfitrión (no dueño): `2549` t=55,260 | 5 461 × `2665` y 132 × `2005` mientras carga; `2144` en **67,556**, `3066` en 67,572 | **ningún** `2724` 04, `2264`, `2661` ni `2322` |

- En toda la captura del invitado hay **0 × 2661, 0 × 2810 y un único 2322**.
- Solo hay 6 × `2981`, 6 × `2524` y 12 × `2111`, a t=74,08 y t=95,68. Todos son de los secuenciadores `0x164`,
  `0x165` y `0x167` del anfitrión (`04 | A0100001 | seq`).
- Es decir, al invitado le llegan **cambios** de rama de secuenciadores que su cliente nunca preparó. Eso indica un
  buscador de proximidad [E]; su tipo se registrará con la nueva clave.

En el lado del invitado (GL), según la sonda `P8Probe`:

- **Viaje 1:** `[2322 write 0xB87A67] … byte before 1`, y `0x11 -> 0x12 after 0.209 s`.
- **Viaje 2:**
  - `0x10 -> 0x11` en t=74,054, **6,5 s después** del 2144.
  - `[loading 0x779E48] … M 0x3B07621E400 | M+0x231 1 (needs >= 2) … list M+0x1B0 n 0 … M+0x1C0 n 0`.
  - `0x11 -> 0x18 after 250.306 s` al final de la sesión, sin pasar nunca por 0x12.
- Es el **mismo M** que recibió el 2 en el viaje 1. Conclusión: el viaje lo devuelve a 1 y no llega ningún 2322 que
  lo suba otra vez.

En el lado del anfitrión (HL, `[owner 0x2BC8470]`):

- `capture t=168.250`: el usuario `0x90100333` entra en su campo, **es el dueño** y la preparación se ejecuta, con su
  2322 a `A0100334`.
- `capture t=195.206`: el usuario `0x90100333` entra en el campo `0x48F30D19D10`, cuyo dueño es `0x90100000`. Toma la
  rama **NOT the owner** y no hay preparación ni 2322.

### 2.4 Cruce de S1 con los enlaces del dueño [V]

Detalle en `observador/cruce-npc-fallidos.txt`. Los 15 fallos de GL (`RestartLayerAction 0x863880 FAILED`)
coinciden **uno a uno** con nodos de 2661 que solo recibió el anfitrión:

| Secuenciador | Hora HC | NPC enlazados (nodo) que fallan en el invitado |
|---|---:|---|
| `0x13` | 50,420 | `B010006D` (0x2000000C), `B0100071`, `72`, `73`, `74`, `75`, `76` |
| `0x19` | 50,437 | `B0100077`, `B0100078` |
| `0x16C` | 50,505 | `B010007E`, `B0100080` (también enlaza `B010007F`) |
| `0x190` | 50,859 | `B0100082` |
| `0x193` | 51,550 | `B010015A` (también `B0100159`) |
| `0x197` | 53,348 | `B0100213`, `B0100214` |

- **Diez enlazados no fallan:** `79`, `7A`, `7B`, `7C`, `7F`, `158`, `159`, `15B`, `15C` y `15D`.
  - Nueve llegaron al invitado en un 2005; `7C` no aparece en GC.
  - `B0100079` y `B010007A` se restauraron **bien** con hashes de su propio gráfico (`0x92C685DE`, `0xB30D4776`;
    GL 19:42:45.951).
  - Por tanto, **el fallo exige además** que la acción guardada sea `seq_bodyanim_action_*`, del gráfico dinámico.
    Encaja con `p8-raiz-invitado.md` §3.3.
- **Cadena causal [E fuerte]:**
  1. Sin 2661 no hay instancia de secuenciador `(A0100001, seq)` en el cliente del invitado.
  2. Sin instancia, el componente `ClientSequencerPlayDataActorComponent` del NPC no se enlaza. Su vf13 `0x9ED290`
     busca el actor `comp+0x2D8` y la instancia en `[[[actor+0x68]+0xA8]+0x18]+0x148`.
  3. Sin enlace no se ejecuta el evento `GameBodyAnimationEventData` → `0xA79770`, que instala `CC+0x70/+0x78`.
  4. La restauración no encuentra `seq_bodyanim_action_*` y devuelve `eErrNoInvalidActionChart`.
- **Lo que lo apoya en la sesión 5:**
  - En el anfitrión, la sonda de enlace (`binding 0x9ED431`) registra `comp+0x2D8 0xA0100001` para los NPC de sus
    secuenciadores, y su primer acierto es en t=50,869, en plena ráfaga de 2661.
  - En el invitado, `0x9ED431` y `0x9ED47F` se ejecutan **0 veces**, y `0xA79770` no se ejecuta para ninguno de los 15.
- **Lo que falta medir:** que la vf13 del invitado salga antes por falta de instancia. Es una inferencia; no hay sonda
  en ese punto.

### 2.5 Código nativo del servidor [V]

Desensamblado en `observador/desensamblado-extractos.txt` y llamadas en `observador/emisores-xrefs.txt`.

**`MoveFieldCompleteCore 0x2BC7670`.** R13 = campo (`0x2BC769E`) y R14 = actor viajero (`0x2BC76AA`). Tiene dos
comprobaciones de dueño:

| Punto | Qué salta el que no es dueño | Gestor que se usaría |
|---|---|---|
| `0x2BC7885 cmp eax,[r13+0x180]` y `0x2BC788C jne` | `0x2BC78A7 call 0x2910DE0(stageMgr=[rbp+0x30], field, …)` | `[rbp+0x30] = 0x2B00C70(field)`: usuario por la clave del **dueño** `[field+0x180]` (`0x2A82590`) → `[[[user+0xD0]+0x68]+0x138]+0x18` |
| `0x2BC8470 cmp eax,r8d` y `0x2BC8473 jne 0x2BC84D9` | `0x2BC8483 call 0x2A82590(SAM, &att, ownerKey)` → `rbx = [[[[att+8]+0xD0]+0x68]+0xA8]+0x18`; `0x2BC84D4 call 0x287EF60(rbx, field, actor, …)` | gestor de secuenciadores del personaje del dueño, el de `+0xA8`, homólogo del `ClientSequencerStageManager` del cliente |

**`0x2910DE0`** (sincronización de escenas al cambiar de nivel):
- Compara el índice de nivel del campo (`[field+0x2D0]+4`) con el del estado global `+0xC4`. Si coincide, no hace nada.
- Si no coincide, ejecuta:
  - `0x2913F60(stageMgr)`, una función con un marco de 0x41A8 bytes que recorre las escenas del gestor (`+0x48`/`+0x4C`);
    no se ha leído entera, así que «rehace el registro» es [E];
  - recorre `stageMgr+0x68` (tipos 2/5) y el campo `+0x28`;
  - llama a `0x29153D0`, `0x29168B0` y `0x29171B0`;
  - si el nivel coincide con `+0xC0` y el gestor de guardado (`[[exe+0x6D696A0]+0x30]`) cumple `+0x64 == -1`,
    `+0x61 == 0` y `+0x88 == 0`, llama además a `0x29150F0` y `0x2917510`.
- Es **transición y mutación** del gestor de escenas del dueño.

**`0x287EF60`** (`0x287ef60.c`):
- Pone `mgr+0x689 = 1`.
- Comprueba si está listo con `0x287FDD0`. Si no lo está, pone `mgr+0x68A = 1` y vuelve a programar el temporizador
  2043 a 10 ms.
- Si está listo, encadena `0x2CA7F0` (thunk a `.xtls`), `0x2896C60`, `0x2CB550` (thunk a `.xtls`), `0x287FA30` y
  `0x288B7A0`. El último llega a `0x2885BD0` → `0x2A56BC0`, el **emisor de 2264**.
- Después programa el temporizador periódico 2043 (`UpdateStageOnceTimer`) y crea un `ToTargetSendTargetFinder` para
  `param_3->vfE8()`.
- Por último llama a `0x2999D80` (el **2322**) y a `0x28431A0`.
- Es **preparación y simulación**: si se repitiera, duplicaría la actualización de escenas y el temporizador del dueño;
  si se ejecutara con el gestor del usuario 2, arrancaría sus misiones dentro del mundo del anfitrión.

**Emisores**, encontrados por la escritura del opcode `mov word [reg],imm16` dentro de `.rsrc` y por sus llamadores,
usando `.pdata`:

| Opcode | Serializador | Llamadores | ¿Sirve para una instantánea? |
|---|---|---|---|
| 2661 PrepareSequencerAck | `0x2A4DEF0(actor, finder, playData, bindings)` | **solo** `0x287D03C`, dentro de `0x287C200` | No: §5 |
| 2981 StageBranchStateAck | `0x2A4F2D0(actor, finder, &seq, &u32, …)` | **solo** `.xtls 0x103FB3FD` | No: está en código protegido |
| 2810 DeleteSequencerAck | `0x2A4E4C0(actor, finder, &seq, &u32, …)` | `0x2879819`, en `0x2879440` | Es un borrado: no forma parte de la instantánea |
| 2524 StageOnBranchEventAck | `0x2A4F920` | `0x2A4FBA4`, en `0x2A4FB00` | No estudiado |
| 2111 UpdateStageChartEventAck | `0x2A50710` | `0x2936C33`, en `0x2936060` | No estudiado |
| 2264 UpdateStageStateAck | `0x2A56BC0` | `0x2887035`, en `0x2885BD0`, que viene de `0x288B7A0` | Mutación: se emite al actualizar escenas |
| 2724 UpdateStageListAck | `0x2A40B30` | `0x2910A64`, `0x291B1CA` | Es el estado de **misiones del propio personaje**; el invitado ya recibe el suyo al entrar (79 × 2724) |
| 2322 PlayAllPreLoadSequencerAck | `0x2999D80(actor, finder)` | **solo** `0x287F1D4`, dentro de `0x287EF60` | **Sí: emisor puro** (escribe `actor+0x60`, opcode `0x912` y envía por el buscador) |

### 2.6 Lado del cliente [V/E]

- **Manejador del 2661, `0xC05BB0`** [V]:
  1. Lee la clase (`bl == 4`).
  2. Resuelve el actor del paquete con `0x8A67E0`.
  3. Obtiene el contexto de campo de ese actor con `0x17FA6C0` y el gestor con su `vf+0x48`.
  4. Crea la instancia con `0xA9CA40`, que reserva 0xC0 bytes y construye con `0xA90AF0`.
- **Manejador del 2322, `0xB87890`** [V]: sigue el mismo camino de contexto y gestor, y termina en `[M+0x231] = 2`.
- **Gestor que espera el estado 0x11** [V]: el invitado obtiene `M = 0x9A5BE0(Q)` a partir del dueño `Q+0x180`, que el
  2174 reescrito anuncia como el **propio usuario 2**. Por tanto, M es el gestor del personaje del usuario 2.
- **Gestor donde busca la instancia el componente del NPC** [V]: el de `comp+0x2D8 = A0100001`, es decir, el del
  Kliff del anfitrión visto en el invitado.
- **Sin resolver [E]:** si un 2661 cuyo dueño es `A0100001` acabaría en el invitado en ese gestor o en el M del usuario
  2, porque el contexto de campo del actor es el campo actual del invitado. Es el **primer riesgo** de cualquier
  réplica (§6) y requiere una sonda en `0xC05BB0`/`0xA9CA40` antes de enviar nada.

### 2.7 Clasificación: instantánea o mutación

| Paquete o paso | Instantánea segura para un observador | Mutación del mundo |
|---|---|---|
| 2661 de un secuenciador **ya preparado** | **Sí** en contenido: describe el secuenciador y sus enlaces. No hay forma nativa de volver a emitirlo | `0x287C200` (lo prepara) sí muta |
| 2981 con el estado actual de la rama | **Sí** en contenido; su emisor solo es accesible desde `.xtls` | La transición de rama que lo emite |
| 2322 para el personaje del observador | **Sí**: `0x2999D80` no toca ningún estado del servidor | `0x287EF60`, que lo envuelve, sí muta |
| 2264 / 2195 / 2335 / 2165 / 2724 de la entrada del dueño | No hacen falta para S1/S2 [E]: son el estado de escenas de **ese** personaje (creación de escenas de facción, horarios, misiones) | Sí: se generan al crearlas |
| `0x2910DE0`, `0x287EF60`, temporizadores 2043, recompensas | Nunca | Sí |

## 3. Veredicto sobre la hipótesis

| Afirmación | Estado |
|---|---|
| El cliente del observador no recibe los secuenciadores ni el hito que el servidor prepara solo para el dueño | **[V]** (§2.2–§2.3) |
| `0x2BC7885/8C` salta `0x2910DE0` y `0x2BC8470/73` salta `0x287EF60` y su 2322 para el que no es dueño | **[V]** estático y en vivo (HL t=195,206) |
| S2: lo que bloquea el estado 0x11 del segundo viaje es la falta de 2322 (`M+0x231 = 1`, listas vacías) | **[V]** con las sondas de la sesión 5 |
| S1: los NPC que fallan son exactamente los enlazados en 2661 del dueño, con acción `seq_bodyanim` guardada | **[V]** en correlación (15/15) |
| S1: el mecanismo es «sin 2661 no hay instancia → no hay enlace → no hay BodyAnimation» | **[E fuerte]** (§2.4) |
| Recibir esos 2661 (y 2981) en el invitado y después el 2322 arreglaría S1 y S2 sin efectos secundarios | **[E]**, con el riesgo de §2.6 sin resolver |

## 4. Diseño del arreglo de raíz (objetivo)

La incorporación del usuario 2 como observador, una vez por entrada en un campo ajeno:

1. **Disparador.** `MoveFieldCompleteCore` termina con el viajero ≠ dueño.
   - Punto `0x2BC84E2`, ya enganchado en modo medida. Hay que exigir además que la entrada se completara con éxito:
     resultado 0 y `FM+0x10 == field`.
2. **Instantánea, en el hilo lógico del servidor** (el de `MoveFieldCompleteCore`, el mismo que emite los paquetes
   nativos). Por cada secuenciador **vivo** del gestor del dueño:
   1. su 2981 con el estado actual de la rama;
   2. su 2661 con los enlaces actuales;
   3. **solo hacia la sesión del usuario 2** (`ToTargetSendTargetFinder` con `session = user2Actor->vfE8()`).

   Sin escrituras en los gestores del dueño, sin temporizadores y sin recompensas.
3. **Hito.** Después de la instantánea, `0x2999D80(user2Actor, &finderToTarget)`, que **es** el emisor nativo, para
   el personaje del usuario 2.
   - Solo después del 2144 de esa entrada, porque el cliente devuelve `M+0x231` a 1 al empezar el viaje (GL viaje 1:
     «byte before 1»).
   - Una vez por (época, entrada), y cancelado si hay salida, otro viaje o desconexión.
4. **Después.** Mientras el usuario 2 siga en el campo, los 2661/2810/2981 nuevos del dueño también deberían llegarle.
   Hoy no le llega ninguno (§2.3), así que el problema no es solo de entrada.

Prohibido: enviar solo el 2322, forzar `M+0x231`, saltar el estado 0x11, llamar a `0x287EF60` o `0x2910DE0` para el
observador, cambiar `field+0x180` o reutilizar el gestor del usuario 2 en el campo del anfitrión.

## 5. Por qué se detiene aquí: la instantánea nativa no se puede aislar [V]

1. **2661.** El serializador `0x2A4DEF0` en sí es puro: escribe `u8 4 | actor+0x60 | 0x244BA90(playData) | n × (nodo,
   actor, 3 × u8)` y envía. **Sus entradas no existen fuera de `0x287C200`:**
   - `playData` (R8) es el parámetro de preparación.
   - Los enlaces (R9) son un vector temporal en `[rsp+0x60]` que `0x287C200` construye recorriendo entradas de 0x28
     bytes y resolviendo cada actor (`0x2A82150`, bucle `0x287CDE0–0x287CFD2`). Por el camino tiene una decena de
     salidas de error (`vf+0x148`).
   - `0x287C200` empieza con `0x287D1D0(rcx, &res, playData)`, que devuelve un error si no procede (validación o
     alta [E]; no se ha leído entera), y emite el 2661 dentro de la misma preparación. Llamarla otra vez prepararía de
     nuevo un secuenciador que ya existe.
   - Construir R8/R9 a partir del estado persistente del gestor sería reimplementar ese bucle, con una disposición
     interna (lista de 0x28 bytes, objeto de secuenciador del servidor) que todavía no está cartografiada.
2. **2981.** El único llamador del serializador `0x2A4F2D0` es `.xtls 0x103FB3FD` (código virtualizado). Las reglas
   prohíben enganchar `.xtls` o sus thunks, y llamar a `0x2A4F2D0` desde fuera exigiría reconstruir argumentos cuyo
   origen solo se ve en código protegido.
3. **No hay ruta nativa de reenvío.** Ninguna rama de `0x2BC7670`, `0x287EF60`, `0x287DBB0` ni `0x2895EF0` recorre
   observadores. Los reintentos usan `manager+0x18`, el actor del gestor. El diseño de esta build asume que los
   secuenciadores del gestor de un personaje solo los ve ese personaje.
4. **Sin la instantánea, el 2322 solo sería la solución rápida prohibida.** Quitaría el bloqueo de S2 sin dar al
   cliente los secuenciadores que S1 necesita, y dejaría al invitado con un `M+0x231 = 2` cuyos NPC no tienen
   contexto.

Por eso `observer_stage_sync` **solo mide**: deja escrito qué habría que enviar y comprueba en vivo que no llega por
ninguna otra vía.

## 6. Alternativa necesaria: extensión del túnel (no implementada)

**A. Lado invitado, antes que nada: sonda de solo lectura en `0xC05BB0`, `0xA9CA40` y `0x9ED290`.**
- Registrar en qué gestor (M del usuario 2 o el de `A0100001`) se crea la instancia cuando llega un 2661 de
  `A0100001`, y si después la vf13 del NPC la encuentra.
- Mientras no llegue ningún 2661, basta con comprobar que la vf13 sale antes de `0x9ED431` por falta de instancia
  (`local_res8`).
- Sin esta respuesta, una réplica podría crear instancias en el gestor equivocado.

**B. Lado anfitrión: réplica fiel en lugar de reconstrucción.**
1. Guardar los **bytes exactos** de cada 2661/2981/2810 que el servidor manda al dueño.
   - Dónde: el `OnRecvPacket` del cliente del anfitrión, que ya ve el sniffer (`net/troc_sniffer.*`). Otra opción es
     una copia en la entrada de los serializadores, aunque esos paquetes no pasan por el `vf9` de la sesión 2.
   - Cómo: un libro (secuenciador → último 2661 y último 2981; un 2810 lo borra). Es exactamente el modelo de
     `obs::Ledger`, ya probado.
2. En la entrada del observador, encolar esos bytes en la cola de salida de la **sesión 2** (`server.cpp` P3) en orden:
   todos los 2981 y 2661 vivos.
3. A continuación, el 2322 **nativo** con `0x2999D80(user2Actor, &finder)`.
   - El buscador va en la pila, con la vtable `exe+0x5B25438`, el byte `+8 = 0` y `+0x10 = user2Actor->vfE8()`,
     exactamente como `0x287F194–0x287F1C7` antes de `0x287F1D4` [V].
   - Llamado **en el hilo lógico del servidor**: por ejemplo, desde el gancho de `0x2BC84E2`, o diferido a un gancho
     del mismo hilo como hace `child_hold` con `NotifyAppear`, que se llama desde el hilo del manejador.
   - Comprobar antes que el actor sigue vivo, que es de la época actual y que el usuario 2 tiene sesión.
4. Durante la estancia, reenviar a la sesión 2 cada 2661/2981/2810 nuevo del dueño. Son contenido de solo lectura para
   el observador.

**Riesgos de B:**
- §2.6: en qué gestor del invitado acaba el 2661.
- Que el invitado ya tenga algún secuenciador con el mismo identificador. Ese caso no existe hoy: su campo propio se
  libera al viajar.
- El orden respecto a los 2005 de los NPC: en el anfitrión, el 2661 llega después de los 2005 y el enlace es tardío
  (t=50,87).
- El tamaño: en la entrada de la sesión 5 son 17 × 2661 (2 083 bytes) más sus 2981 (34–42 bytes cada uno).
- Ninguno es una mutación del servidor.

**C. Descartado:** reconstruir 2661/2981 a partir de la memoria del servidor (§5.1–5.2) o hacer «dueño» al observador.

## 7. Implementación: `observer_stage_sync` (solo medida)

### 7.1 Archivos

| Archivo | Contenido |
|---|---|
| `src/mod/session2/observer_stage_logic.h` | Lógica pura: separación de paquetes, analizadores de 2661/2810/2322, `Ledger` (vivos por (actor, seq); `need()` = lo que le falta a un observador), `EntryWatch` y seguimientos, `choose_mode` (2 o más se rechaza) |
| `src/mod/session2/observer_stage.{h,cpp}` | Ganchos, anillo, hilo propio, instalación |
| `tests/optd_observer_stage_tests.cpp` | 9 grupos de pruebas con los paquetes reales de HC |
| `docs/research/pendientes/observador/` | Scripts y salidas de la evidencia |

### 7.2 Tabla de ganchos (todos son ganchos intermedios de safetyhook, en `.rsrc`)

Comprobaciones [V] (`observador/verificacion-enganches.txt` y `observador/constantes-vs-pe.txt`):
- **Parche:** dentro de `.rsrc`, con instrucciones enteras desde el inicio de la función según `.pdata`, ningún salto
  de la función ni ningún rel32 (`E8`/`E9`/`0F 8x`) de `.rsrc`, `.xtls` o `.text` que caiga dentro, y ningún operando
  relativo a RIP.
- **Bytes:** los 15 pares (RVA, bytes) del código coinciden con el PE.
- **En ejecución** (`p8_install_mid`): bytes y contexto iguales a los del PE, gancho creado deshabilitado, bytes
  reubicados idénticos a los verificados y, solo entonces, se habilita. Si algo falla: `ObserverStage: … REFUSED`, sin
  escribir nada.

| Punto | Parche (bytes) | Contexto verificado | Registros leídos |
|---|---|---|---|
| `0x2A4DEF0` (2661) | `48 89 5C 24 08` (5) + `4C 89 4C 24 20 4C 89 44 24 18 55` | `0x2A4E024` 16 B (bucle de enlaces), `0x2A4E09F` `66 41 C7 07 65 0A` | RCX actor (`+0x60`, `[+0xA0]+0x60`), RDX buscador (`[0]` vtable, `+0x10` sesión), R8 `u32 +0`, `u64 +8`, R9 `{ptr, u32 n @+8}`, entradas de 12 B (≤ 64) |
| `0x2A4E4C0` (2810) | `48 89 5C 24 08` (5) + `4C 89 4C 24 20 55 56 57 41 54 41` | `0x2A4E5C2` `49 8B 04 24`, `0x2A4E60C` `66 41 C7 06 FA 0A` | RCX, RDX, `[R8]` u64 seq, `[R9]` u32 |
| `0x2A4F2D0` (2981) | `48 89 5C 24 08` (5) + `4C 89 4C 24 20 4C 89 44 24 18 55` | `0x2A4F3F8` `48 8B 4D 67 48 8B 01`, `0x2A4F486` `66 41 C7 07 A5 0B` | RCX, RDX, `[R8]` u64 seq, `[R9]` u32 |
| `0x2999D80` (2322) | `48 89 5C 24 08` (5) + `55 56 57 41 54 41 55 41 56 41 57` | `0x2999E77` `66 41 C7 06 12 09` | RCX actor, RDX buscador |
| `0x2BC84E2` (entrada) | `49 8B 85 D0 02 00 00` (7: `mov rax,[r13+0x2D0]`) + `48 85 C0 75 07 B8 FF FF 00` | `0x2BC84D9` `48 8B 4E 18 E8 1E 98 C7 FF`, `0x2BC769E` `4D 8B E8`, `0x2BC76AA` `4C 8B 71 08` | R13 campo (`+0x180` dueño, `+0x2C8` clave), R14 viajero (`+0x60`, `[+0xA0]+0x60`) |

**Coexistencia [V]:**
- Ninguna constante de otro módulo cae en estos rangos (búsqueda en `src`).
- `0x2BC84E2` queda fuera de lo que parchean o comprueban las sondas P8 (`0x2BC8452–0x2BC8479`, `0x2BC84D4–0x2BC84D8`) y
  de `0x287F1CC–0x287F1DF`.
- Se pasa por `0x2BC84E2` tanto en la rama de dueño como en la de no dueño; el gancho decide comparando las claves.

**Callbacks:**
- Solo hacen cargas protegidas por SEH y empujan a un anillo Vyukov de 4096 entradas. Sin reservas de memoria, sin
  CRT y sin llamadas al juego.
- Comprobado en el `cdmp.asi` compilado (`dumpbin /disasm`): `on_prepare`, `on_delete`, `on_branch`, `on_milestone`,
  `on_entry`, `emit` y `common` solo llaman a `p8_now_us`, `GetCurrentThreadId`, a los auxiliares atómicos y de
  lectura y a `__security_check_cookie`. Ni `memcpy`/`memset` ni VEX.
- **Hilo:** el de los emisores y el de `MoveFieldCompleteCore`, es decir, el hilo lógico del servidor. El libro vive
  solo en el hilo propio, que vacía el anillo cada segundo.

**Coste [E]:**
- Puntos esporádicos: 19 × 2661, 3 × 2810, unos 160 × 2981 y 2 × 2322 en 8 minutos de HC, más un acierto por entrada
  de campo.
- Límite: 512 eventos por punto cada 5 s y 4000 líneas por punto.

### 7.3 Configuración y cableado

- `[session2] observer_stage_sync=0|1|2+`. Necesita `create_session=1`; si no, avisa y queda apagado.
- No toca `mod.cpp`. La clave la lee `config.cpp` y `session2::install` la recoge con `loaded_config()`.

## 8. Diagnóstico para la próxima sesión: líneas esperadas

En el anfitrión, con `[session2] observer_stage_sync=1`. Conviene acompañarlo de `load_diag=1` y `s1_diag=1`, y en
Shadow de `load_diag=1`, `guest_actor_diag=1` y `s1_diag=1`.

| Línea (anfitrión) | Qué dice |
|---|---|
| `ObserverStage ON (observer_stage_sync, MEASURE ONLY, read only): hooked: 2661 PrepareSequencerAck (exe+0x2A4DEF0) … entry 0x2BC84E2 (exe+0x2BC84E2) \| REFUSED: none \| nothing is sent …` | instalación |
| `ObserverStage: observer_stage_sync=2 asks to SEND …; that mode is NOT implemented … Running as 1` | solo si se pide envío |
| `ObserverStage [2661 PrepareSequencerAck] qpc … capture t=… T… \| 2661 size 184 (5 + 91 + 11 x 8) -> actor 0xA0100001 user 0x90100000 via ToTarget (session 0x…) \| owner actor 0xA0100001 sequencer 0x13 \| bindings 8: 0x20000003->0xA0100001 0x2000000C->0xB010006D … \| live sequencers N` | cada 2661 |
| `ObserverStage [2981 StageBranchStateAck] … 2981 (size: see the capture) -> actor 0xA0100001 user 0x90100000 via <ToTarget \| finder vt exe+0x…> \| sequencer 0x… u32 … \| a live sequencer` | cada 2981; el tipo de buscador dirá si es de proximidad |
| `ObserverStage [2810 DeleteSequencerAck] … 2810 size 22 -> actor 0xA0100001 … sequencer 0x163 … \| removed from the ledger \| live sequencers N` | cada 2810 |
| `ObserverStage [2322 PlayAllPreLoadSequencerAck] … 2322 size 9 -> actor 0x… user 0x… via ToTarget …` | cada 2322 nativo |
| `ObserverStage [entry 0x2BC84E2] … user 0x90100000 actor 0xA0100001 completed its entry into field 0x… (key 0x…) it OWNS: …` | entrada del dueño |
| `ObserverStage [entry 0x2BC84E2] … user 0x90100333 actor 0xA0100334 … owned by user 0x90100000 -> NOT the owner (observer): … the owner's live sequencers …: 17, also prepared for this user: 0 \| MISSING for the observer: 17 sequencer(s): 0x13(8 bindings, branch …) … \| NPCs they bind (…): 0xB010006D … \| PLAN, NOT SENT …: 17 x 2661 (+ its 2981 state), then 2322 for actor 0xA0100334` | **la línea clave** |
| `ObserverStage [since entry +10 s] user 0x90100333 … addressed to it since: 2661 x0, 2810 x0, 2981 x0, 2322 x0 \| 2981 of the owner's sequencers through a non-ToTarget finder xK \| … \| NO 2322 reached it: …` | también a +60 y +300 s |
| `ObserverStage (measure only) \| hooked 5/5 \| ring dropped 0 \| ledger: live … \| …: hits … queued … limited … dropped … lines …` | contadores cada 60 s |

**Correlación con el invitado.** Las sondas de Shadow ya existen:
- `P8Probe [2322 entry 0xB87890]`, `[2322 write 0xB87A67]`;
- `[loading 0x779E48] … M+0x231 …`, `[loading 0x779E77]`;
- `GuestDiag loading transition … EXIT FROM 0x11`;
- `P8Probe [binding 0x9ED431]`, `[Restart 0x86392A]`.

Para alinear los relojes se usa el 2144 de la entrada del observador:
- La línea `[entry 0x2BC84E2]` y la `[owner 0x2BC8470] … NOT the owner` se escriben en el mismo procesamiento de
  `MoveFieldComplete` que emite ese 2144.
- La captura del invitado registra cuándo lo recibe.
- En la sesión 5: anfitrión t=195,206 (20:42:42.419) frente a invitado t=67,556. El estado 0x11 llegó 6,5 s después
  (t=74,054).

**Criterio de la próxima sesión (medida):**
- La línea `NOT the owner (observer)` lista de 15 a 20 secuenciadores, cuyos NPC enlazados incluyen los que luego
  fallen en Shadow con `eErrNoInvalidActionChart`.
- Los seguimientos confirman 0 × 2661 y 0 × 2322 hacia el usuario 2.
- El tipo de buscador de los 2981 que sí le llegan queda identificado.

## 9. Pruebas y compilación

- **`tests/optd_observer_stage_tests.cpp`**, solo lógica sin juego, con los 23 paquetes 2661/2810/2322 de HC copiados
  byte a byte:
  - separación de paquetes y longitudes erróneas;
  - el 2661 del secuenciador `0x13` (8 enlaces exactos);
  - los 19 cumplen `5 + 91 + 11n` con actor y dueño `A0100001`;
  - el 2810 de `0x163`;
  - réplica del libro en la entrada del observador (t=195,206): 18 preparados, 1 borrado, **17 vivos**, 17 que faltan;
  - **los 15 NPC fallidos del invitado están entre los enlazados**;
  - final: 19 preparados, 3 borrados, 16 vivos;
  - entrega al observador y reglas de rama;
  - límites (libro lleno, más de 64 enlaces);
  - calendario de seguimientos;
  - modo de envío rechazado.
- **`.\scripts\build.ps1` (RelWithDebInfo):** compila sin avisos nuevos y **18/18 pruebas OK**.
  - Se compiló dos veces; la segunda, tras corregir que R9 de `0x2A4F2D0` es un puntero.
  - Se hizo con el árbol que en ese momento contenía también los cambios en curso del otro agente.

## 10. Riesgos

- **Lectura en el hilo del servidor:** son cargas protegidas en código esporádico. Si la disposición de R8/R9 no fuera
  la deducida, solo saldrían datos erróneos (`UNREADABLE`), nunca escrituras.
- **Orden del anillo:** los eventos de un mismo hilo salen en orden. Un 2661 con más de 6 enlaces se reparte en eventos
  de continuación que el hilo vuelve a juntar. Si se pierde alguno por el límite, la entrada se cierra incompleta y se
  marca con `(+N not kept)`.
- **El libro solo conoce lo emitido después de la instalación.** El hilo instala en la pantalla de título, antes de
  cargar la partida, igual que las sondas P8. Si se instalara tarde, «MISSING» se quedaría corto. Para ver si el libro
  arranca vacío, compárese «live» con el `prepared` del contador.
- **Modo envío:** no existe. El valor 2 o más queda registrado y se rechaza.

## 11. Qué sigue sin demostrar

- **[E]** Que, al recibir los 2661 del dueño, el cliente del invitado cree la instancia **donde la busca** el
  componente del NPC (§2.6).
- **[E]** Que los 2981 sean necesarios antes del 2661 o solo convenientes: en el anfitrión siempre llegan en pareja y
  delante.
- **[E]** El tipo de buscador de los 2981/2524/2111 que sí llegan al invitado (lo medirá la nueva clave).
- **[E]** Que no haga falta más estado que los 2661/2981 y el 2322 para que `0xA97D60` dé verdadero en el invitado.
  En la sesión 5 las listas del gestor estaban vacías, así que solo bloqueaba `M+0x231`.
- **[E]** Si existen secuenciadores del dueño anteriores a la entrada del anfitrión (preparados por guardado o
  recarga) que esta captura no muestra.
- **[V] Alcance:** solo análisis estático, capturas y logs conservados, compilación y pruebas. Ninguna sesión en el
  juego.

## 12. Líneas compartidas tocadas

| Archivo | Cambio exacto |
|---|---|
| `src/mod/config.h` | campo `uint32_t s2_observer_stage_sync = 0;` (con su comentario) al final de `Config`, y declaración `const Config& loaded_config();` tras `load_config` |
| `src/mod/config.cpp` | `Config g_loaded;` en el espacio de nombres anónimo; `c.s2_observer_stage_sync = read_uint(ini_path, L"session2", L"observer_stage_sync", 0);` tras `s1_diag`; `g_loaded = c;` antes del `return c;`; definición de `loaded_config()` |
| `src/mod/session2/session2.cpp` | `#include "mod/config.h"` y `#include "mod/session2/observer_stage.h"`; en `install()`, tras el aviso de `child_hold=0`, el bloque que llama a `observer_stage_install(loaded_config().s2_observer_stage_sync)` (o avisa sin `create_session`); en `describe()`, `if (observer_stage_active()) s += " \| " + observer_stage_describe();` |
| `CMakeLists.txt` | `src/mod/session2/observer_stage.cpp` en `cdmp_mod` (tras `p8_probes.cpp`); el ejecutable y la prueba `optd_observer_stage` (tras `optd_p8_probe`) |

No se tocaron `mod.cpp`, `guest_actor_diag.*`, `guest_s3.*`, `steam_transport.*`, `send_gate.*` ni `p8_probes.*`. Se
reutiliza `p8_install_mid`, `p8_now_us` y `p8_when`, que ya estaban exportados.

---

# Fase 2: ¿se puede replicar al observador lo que recibió el dueño?

9 de octubre de 2026, rama `opcion-d` (HEAD `b8c4dfa` más estos cambios, sin commit). Mismas reglas y notación que
arriba. No se arrancó el juego ni se tocó `bin64` (solo se leyeron sus capturas), no se cambió ningún proyecto de
Ghidra y no se enganchó ni parcheó `.xtls`.

## 13. Resumen de la fase 2

- **Veredicto: replicar al usuario 2 los bytes exactos de los 2661/2981 del dueño NO es coherente [V estático +
  capturas]. No se implementa `observer_stage_sync=2`**, que sigue rechazado (corre como 1) con el motivo nuevo en el
  log.
- **Bloqueo decisivo (B1) [V]:** el cliente **no elige el gestor por el contenido del paquete**.
  - Los manejadores de 2661, 2981, 2810, 2264 y 2322 obtienen el contexto de campo con `0x17FA6C0`.
  - `0x17FA6C0` llama a `ClientTransformSyncActorComponent::vf140` (`0x9F8560`), que devuelve el **único campo
    actual** del cliente, `[[[exe+0x6D69190]+0xC8]]`, sea cual sea el actor.
  - Su `vf48` es `ClientField::vf48 = 0x9A5BE0`: el gestor del personaje del usuario dueño **según ese cliente**
    (`field+0x180`).
  - En Shadow, el 2174 reescrito hace dueño al propio usuario 2. Por tanto, cualquier 2661/2981 replicado acabaría en
    el gestor de `A0100334`, que es el mismo M del estado 0x11.
  - En cambio, los NPC (`0x9ED290`) buscan la instancia en el gestor del actor `comp+0x2D8`. Ese valor viene en su 2005
    y es `A0100001`, el Kliff del anfitrión visto en el invitado: **otro objeto**.
  - Reescribir el payload no cambia nada, porque el enrutado no lo lee.
- **Otros bloqueos [V]:**
  - **B2.** Todos los enlaces de los 19 × 2661 son de **tipo 5**. `0xA9CA40` exige que cada actor enlazado (también
    `A0100001`) exista ya en el cliente. Si falta uno, descarta el secuenciador entero (`0xA9CE81`).
  - **B3.** El orden nativo es `2005` del NPC → `2981` → `2661` en el mismo milisegundo. Una réplica tras el 2144 del
    observador llega cuando sus NPC ya se cargaron; que la vf13 no vuelva a ejecutarse es [E].
  - **B4.** El cliente dueño **conduce** los secuenciadores con C>S: 431 × 2767, 316 × 2591 y 143 × 2436 en la sesión
    5, que solo llevan el u64 del secuenciador. Un segundo cliente que creyera tenerlos los mandaría para su propio
    usuario en el servidor.
  - **B5.** Un id ya presente en el gestor se rechaza (`0xAA6E90`, error `[exe+0x6CF76A4]`).
- **2264 [V]:** se emite por usuario (ToTarget al actor del gestor, `0x2886FC3–0x2887035`). El usuario 2 no recibe
  ninguno en el campo del anfitrión, y si los recibiera también caerían en su propio gestor.
- **Lo que se ha implementado (todo apagado por defecto):**
  - **Invitado: `[session2] guest_stage_diag=1`.** Sonda de solo lectura con 15 puntos `.rsrc` verificados. Registra en
    qué gestor cae cada 2661/2981/2810/2322, el resultado de `0xA9CA40` (creado, actor ausente o duplicado), el lado
    del NPC en `0x9ED290` (local_res8, actor dueño, gestor que busca, instancia) y las escrituras de `M+0x231`. Formatea
    en el hilo de diagnóstico del invitado.
  - **Anfitrión (`observer_stage_sync=1`):**
    - tokens `corr=op:actor:seq:valor` idénticos a los del invitado;
    - el libro descarta lo preparado para el dueño antes de que entre en un campo nuevo;
    - la línea PLAN muestra el **orden nativo** (2981 → 2661 por secuenciador y, al final, 2322) y el motivo por el que
      no se envía.
- **Pruebas:** `.\scripts\build.ps1` compila y pasan **19/19**, con la nueva `optd_guest_stage` sobre paquetes reales
  de las dos capturas.
- **Siguiente paso (§16):** una sesión de Shadow con `guest_stage_diag=1` decide la alternativa. Primero hay que medir
  si existe el gestor de `A0100001` en el invitado, si la vf13 de los NPC sale en local_res8 y cuándo se ejecuta.

## 14. Análisis estático del cliente (tarea A)

Desensamblado comentado en `observador/fase2-cliente-enrutado.txt` (script `observador/scripts/fase2ev.py`). Capturas
en `observador/fase2-capturas.txt`. La tabla RTTI se rehízo con `tools/scripts/rtti_dump.py`; con ella trabajan
`vt.py`, `vslots.py` y `slotref.py` (necesitan `rtti.json`/`vtmap.json` en el directorio de trabajo).

### 14.1 Qué lee el manejador del 2661 (`0xC05BB0`) [V]

| Paso | Instrucciones | Qué hace |
|---|---|---|
| Formato | `0xC05BDB–0xC05C04`, lectores en `0xC05CC6–0xC05FB9` | `u8 kind` → `[rbp+0xF0]`; `u32 actor` → `[rbp+0xF8]`; play data con `0x244BC50` → `[rbp]`; `(len−91)/11` enlaces de 12 B en un vector `[rbp−0x80]` (u32 nodo, u32 actor, 3 × u8). Debe consumir el paquete exacto |
| Listo | `0xC05D49–0xC05D8C` | si la clave no es de usuario, `0x8B53C0` exige que el actor esté «logueado»; si no, devuelve **reintentar** (`[exe+0x6CDB9D0]`) |
| Actor | `0xC0608D` | `0x8A67E0(actores, clave)`; si existe, `[r12] = actor` |
| Campo | `0xC060EC` → `0x17FA6C0` | `[[actor+0x68]+0x1A0]->vf140`: el **único** implementador que construye un `FieldAttacherForActor` es `ClientTransformSyncActorComponent::vf140 = 0x9F8560` (búsqueda en todas las vtables RTTI, ranura 40). Si `actor+0xC0` o `+0xC2`, el campo es `[[[exe+0x6D69190]+0xC8]]`: **el campo actual del cliente, no uno del actor** |
| Gestor | `0xC06120–0xC06127` | `field->vf48()`; la vtable de `pa::ClientField` (`exe+0x55BCEA0`) tiene en la ranura 9 **`0x9A5BE0`**: gestor `[[[pj+0x68]+0xA8]+0x18]` del personaje del usuario `field+0x180` |
| Alta | `0xC0612A`, `0xC06188` | solo `kind == 4`: `0xA9CA40(gestor, &res, &play, &enlaces)`; si no, solo un error en el log. El handler devuelve 0 en ambos casos |

- **Confirmación en vivo [V]:** el 2322 sigue el mismo camino (`0xB87A2C` → `0xB87A64` → `[M+0x231] = 2`). En la
  sesión 5, la sonda `P8Probe` vio que su M era **el mismo** que el `0x9A5BE0(Q)` del estado 0x11 (§2.3).
- **Consecuencia:** el gestor depende solo de quién es el dueño del campo en ese cliente. El actor del paquete solo
  tiene que existir y estar listo.

### 14.2 Los demás Ack de escena: el mismo enrutado [V]

| Opcode | Manejador | Llamada al gestor | Aplica con |
|---|---|---|---|
| 2981 StageBranchStateAck | `0xC073A0` | `0xC07746 call [rax+0x48]` | `0xA9E100` (búsqueda por id con `0x1F6C190`) |
| 2810 DeleteSequencerAck | `0xC06330` | `0xC0659B` | `0xA9D950` |
| 2264 UpdateStageStateAck | `0xC0DF10` | `0xC0E236` | `0xAA1E60` |
| 2322 PlayAllPreLoadSequencerAck | `0xB87890` | `0xB87A64` | `[M+0x231] = 2` |

Los cinco pasan por `0x8A67E0` → `0x17FA6C0` → `vf48`. Ninguno usa un gestor ligado al dueño del secuenciador.

### 14.3 Qué crea `0xA9CA40` y qué exige [V]

- Reserva 0xC0 B y construye con `0xA90AF0(instancia, play, gestor)`. Después recorre los enlaces
  (`0xA9CB60–0xA9CDEE`).
- **Enlaces de tipo 4 o 5** (`0xA9CB87–0xA9CB92`): el actor (`[r14+4]`) se busca con `0x8AF170`. Si no está
  (`0xA9CC13`), salta a `0xA9CE81`: resultado `[exe+0x6CDB9E4]`, instancia destruida (`0xA9CF25`) y fin.
  - En HC **todos** los enlaces son `05 00 00`, incluido el del Kliff del dueño (`observador/fase2-capturas.txt`).
- **Alta** (`0xA9D25D–0xA9D2BE`): `inc [gestor+0x17C]`, hash de 8 B (`0x1364780`) del **u64 play data +8**
  (secuenciador) e inserción con `0xAA6E90` en el mapa `gestor+0x170`. Si ya existía: resultado `[exe+0x6CF76A4]` y
  fin.
- **Vaciado:** la ranura 1 de `pa::ClientSequencerStageManager` (`0xA94660`) limpia los mapas `+0x170…+0x188` y
  `+0x190…+0x1A8` y las listas `+0x1B0`, `+0x1C0` y `+0x1D0`. Las dos primeras listas son las que vigila
  `0xA97D60`.
- **[E]** No está cartografiado cómo se llena el contenedor `[gestor+0x148]` en el que busca el NPC. En el dueño
  funciona de forma nativa (la sonda de enlace del anfitrión acierta), así que el 2661 lo alimenta **en el gestor en
  el que cae**.

### 14.4 El lado del NPC (`0x9ED290`) [V]

`ClientSequencerPlayDataActorComponent::vf13` (vtable `exe+0x55BEE78`, ranura 13):

1. Sale si `comp+0x2E0 & 0x1FFFFFFF == 0`.
2. Calcula `local_res8`, el nodo de escena **del propio NPC** (`[[npc+0x68]+0x58]+0x90…` o, para el tipo 7,
   `[[npc+0x68]+0x30]+0x50`). Sale en `0x9ED401` si vale 0 o su parte alta es −1.
3. Busca el actor `comp+0x2D8` con `0x8AE1A0` (solo personajes): `A0100001` para los 15 NPC (su 2005, §3.4 de
   `p8-raiz-invitado.md`).
4. Busca `X = [[[A+0x68]+0xA8]+0x18]` y luego `[X+0x148]->vf8(&comp+0x2E0)`.

- **X es el gestor del Kliff del anfitrión en el invitado. El enrutado de §14.1 lleva los Ack al gestor de
  `A0100334`.** Son dos objetos distintos: en el dueño coinciden y en el observador no.
- **[E]** En la sesión 5, `0x9ED431` (justo después de `0x8AE1A0`) se ejecutó 0 veces en el invitado. La vf13 salió
  antes (paso 2) o no se llamó. No se sabe si la ranura 13 vuelve a llamarse cuando ya existe la instancia. La sonda lo
  medirá.

### 14.5 Orden nativo y tiempos [V]

En HC, cada NPC llega en un 2005 **inmediatamente antes** de su par 2981/2661:

- `B010007E`: 2005 en t=50,504, 2981 en 50,505 y 2661 en 50,505.
- `B0100082`: 2005, 2981 y 2661, los tres en 50,859.
- `B0100213`: 2005 en 53,346, 2981 y 2661 en 53,348.

El enlace se produce después (primer acierto a t=50,869). La preparación del servidor crea el NPC y lo enlaza en el
mismo paso. En el invitado, los 2005 de esos NPC llegan **durante la carga** (55–67 s) y el 2144 en 67,556.

### 14.6 2264, 2981 que sí llegan y C>S [V]

- **2264:** `0x2885BD0` lo envía con `ToTargetSendTargetFinder` al actor del gestor (`[r15+0x18]`,
  `0x2886FC3–0x2887035`). Es **por usuario**: 36 325 al dueño en HC y 0 al usuario 2 en GC.
- **2981/2524/2111 de `0x164`, `0x165` y `0x167`** (solo enlazan `A0100001`): llegan **byte a byte iguales** a los dos
  clientes en el mismo momento (anfitrión t=210,834 e invitado t=74,080). En el invitado caen en su M, que no tiene
  esos ids; que la búsqueda falle sin efecto es [E].
- **C>S del dueño:** 431 × 2767 SequencerTriggerVolumeInOutReq, 316 × 2591 UpdateStageChartEventReq, 143 × 2436
  StageBranchStateReq y 1 × 2547. Sus payloads empiezan por el **u64 del secuenciador**, sin actor. El invitado no
  envía ninguno (solo 7 × 2173).

### 14.7 Respuesta a la pregunta de la tarea A

| Pregunta | Respuesta |
|---|---|
| ¿Dónde caería un 2661/2981 replicado? | En el gestor del personaje del **usuario 2** (M de `0x9A5BE0`, el del estado 0x11) **[V]** |
| ¿Enlazaría los 15 NPC? | **No:** buscan en el gestor de `A0100001` (`0x9ED45B`) **[V estático]**; confirmarlo en vivo es tarea de la sonda |
| ¿Qué claves exige el cliente? | El actor del paquete existente y listo; **todos** los actores enlazados (tipo 5) presentes; un id libre en ese gestor **[V]** |
| ¿Hay que reescribir el payload? | No serviría: el enrutado no lo lee. Cambiar `A0100001` por `A0100334` en los enlaces haría del Kliff del usuario 2 el «jugador» de las cinemáticas del anfitrión **[V/E]** |
| ¿Colisión de ids con secuenciadores propios del invitado? | Posible y silenciosa (`0xAA6E90`) si M conserva ids iguales. En la sesión 5 su campo propio no tenía ninguno **[V/E]** |
| ¿Fallo o cierre? | No se ve en el camino estático del 2661. El riesgo real es el de **B4** (C>S de un segundo conductor) **[E]** |
| ¿2264 por usuario o del campo? | **Por usuario** **[V]**; el usuario 2 no recibe ninguno en el campo ajeno |

## 15. Por qué no se implementa la réplica

Las condiciones del encargo eran implementarla solo si el análisis demostraba que es coherente. No lo es:

1. **B1:** el destino no se puede elegir desde el anfitrión.
2. **B2/B3:** el momento correcto es durante la carga y pegado al 2005 de cada NPC, no tras el 2144.
3. **B4:** el cliente que tiene el secuenciador en su gestor actúa como su conductor.

Enviar los bytes solo convertiría al usuario 2 en un segundo dueño parcial, sin arreglar S1. Tampoco se emite el 2322
de `0x2999D80` sin la instantánea, porque es la solución rápida prohibida.

## 16. Siguiente alternativa (propuestas, sin implementar)

**A. Medir primero (implementado: `guest_stage_diag=1`).** En la próxima sesión de Shadow hay que decidir:

- si existe X, el gestor del Kliff del anfitrión en el invitado (`GuestStage [NPC owner 0x9ED453]`: puntero, vtable
  `ClientSequencerStageManager`, `+0x231`, instancias);
- si la vf13 de los NPC sale en `local_res8` (`[NPC res8 0x9ED401] … EXITS`) y cuántas veces se llama por NPC (si es
  solo al cargar, B3 obliga a entregar durante la carga);
- en el viaje 1 (campo propio), la ruta nativa como control: `route … the sequencer OWNER's manager` si llega algún
  2661 propio.

**B. Colocación en el gestor del dueño, en el invitado (solo diseño).** Si la medida confirma que X existe y que el
cliente no conduce los secuenciadores de un gestor ajeno, la vía coherente sería:

- crear la instancia **en X** y no en M, para que la encuentre la búsqueda nativa del NPC;
- entregar el par 2981/2661 en el **orden nativo**: en el anfitrión, en `take_frame`, cuando sale hacia la sesión 2
  el 2005 del último actor enlazado de un secuenciador vivo del dueño;
- filtrar en el anfitrión los C>S de secuenciador de la sesión 2 cuyo id no sea suyo.

Exige cambiar una decisión del cliente (el gestor de `0xC0612A`), así que necesita aprobación expresa y una medida
previa. No es una réplica de bytes.

**C. S2 por separado (decisión del usuario).** B1 desacopla S1 y S2:

- El M del usuario 2 **nunca** debería contener los secuenciadores del anfitrión. Para el campo ajeno, el estado
  nativo coherente de M es «vacío».
- El caso nativo de un M vacío ya existe: la primera entrada del anfitrión a su campo vacío recibe `2144`, `3066` y
  `2322` en el mismo milisegundo (§2.2).
- Un 2322 nativo (`0x2999D80`, buscador ToTarget como en `0x287F194–0x287F1C7`) para el personaje del usuario 2 tras
  su 2144 reproduciría ese caso.
- Las reglas actuales lo prohíben como «2322 suelto», así que **no se implementa**. Se deja escrito para que el
  usuario decida si este argumento cambia la regla.

## 17. Implementación de la fase 2

### 17.1 Archivos

| Archivo | Contenido |
|---|---|
| `src/mod/session2/guest_stage_diag_logic.h` | Lógica pura: `parse_prepare` (con el tipo de cada enlace), `parse_branch` (2981), `corr_token`, `fnv1a64`, `classify_route`, `classify_result`, `ManagerBook` (gestores vistos y cambios de `+0x231`), `NpcTracker` (lado del NPC) |
| `src/mod/session2/guest_stage_diag.{h,cpp}` | 15 ganchos de solo lectura, anillo, hilo de instalación y `guest_stage_diag_tick()` (líneas y resumen) |
| `src/mod/session2/observer_stage_logic.h` | `Ledger::drop_user_before`, `invalidated()`, `PlanStep` y `native_order()`; comentario de `choose_mode` |
| `src/mod/session2/observer_stage.cpp` | Tokens `corr=` en las líneas de 2661/2810/2981/2322; descarte al entrar el dueño en otro campo; orden nativo y motivo en la línea PLAN; texto del rechazo de `=2` |
| `tests/optd_guest_stage_tests.cpp` | 8 grupos de pruebas con paquetes reales de HC y GC |
| `observador/fase2-*.txt`, `observador/scripts/*.py` | Evidencia y scripts reproducibles |

### 17.2 Puntos de la sonda del invitado

Todos están en `.rsrc` y son instrucciones enteras. Ninguna rama de la función ni ningún rel32 de
`.rsrc/.xtls/.text` cae dentro, y no hay operandos relativos a RIP (`observador/fase2-verificacion-enganches.txt`).
Los 36 pares (RVA, bytes) del código coinciden con el PE (`observador/fase2-constantes-vs-pe.txt`).

- **Sin solapes:** ninguno coincide con un punto de `P8Probe` ni cae en los bytes que verifica. Se comprobó cruzando
  todas las constantes de `src/mod` con un script.
- **Descartado:** `0xC0619C`, porque un rel32 de `.rsrc 0x8543CD` cae en `0xC0619E`; se usa `0xC061C1`.

| Punto | Parche | Contexto verificado | Qué lee |
|---|---|---|---|
| `0xC05BB0` 2661 entrada | 5 | — | R8: raw `[R8+0x18]`; kind, actor, dueño, secuenciador, enlaces y tipos, FNV-1a del payload; `[RCX+0x21]` |
| `0xC0612A` 2661 enrutado | 5 (`cmp bl,4 ; je`, el je corto se reubica) | `0xC05BC0`, `0xC060C6`, `0xC060C2`, `0xC06120`, `0xC06179` | RAX = gestor; `[gestor+0x18]` → actor; `+0x231`; `+0x174`; campo `[rbp−0x40]` y `+0x180`; actor `[R12]` y **su** gestor; Q del estado de carga |
| `0xC061C1` 2661 resultado | 6 | ídem | `[rbp−0x50]` (si se enrutó y kind 4), instancias antes y después |
| `0xA9CE87` actor ausente | 7 | `0xA9CA40`, `0xA9CB87`, `0xA9CB98`, `0xA9CC0E` | R14 = enlace (nodo, actor, tipo), `[rbp+0x410]` gestor, `[rbp+0x420]` play data |
| `0xC073A0` / `0xC07749` 2981 | 5 / 9 | `0xC073BC`, `0xC076E6`, `0xC0773F` | raw (48 B) / RAX = gestor, campo `[rbp−0x38]`, actor `[R15]` |
| `0xC06330` / `0xC0659E` 2810 | 5 / 8 | `0xC06594` | raw / RAX = gestor, campo `[rbp−0x48]` |
| `0xB87A32` 2322 | 6 (`cmp ; jne` corto) | `0xB87A28`, `0xB879B7` | campo `[rbp−0x58]`, válido `[rbp−0x50]`, actor `[R14+5]`, dueño del campo |
| `0x9ED290` NPC vf13 | 5 | — | `comp+0x2D8`, `comp+0x2E0`, clave del NPC; sin clave de secuenciador: solo se cuenta |
| `0x9ED401` local_res8 | 10 (`cmp ; je rel32` reubicado) | `0x9ED2A8`, `0x9ED2B5` | `[rbp+0x30]` |
| `0x9ED453` dueño hallado | 8 | ídem | A = `[rbp−0x20]`, X = su gestor, vtable, `+0x231`, `+0x174` |
| `0x9ED48C` instancia hallada | 8 | ídem | RDI = instancia |
| `0x77AA8B` / `0x77B0CE` | 7 / 8 | `0x77AA5C`, `0x77AA6C` / `0x77AE9F`, `0x77AE8A` | M (RAX / R14), valor previo de `+0x231`, P y su estado |

**Límites y coste:**
- 256 eventos por punto cada 5 s.
- Los puntos del NPC, solo las 3 primeras llamadas de cada NPC; el resto se cuenta.
- Como mucho 3000 líneas por punto.
- Las vf13 sin clave de secuenciador solo suman un contador atómico.
- Los callbacks solo hacen cargas protegidas por SEH y empujan al anillo.
- Comprobado en el `cdmp.asi` compilado (`dumpbin /disasm`): solo llaman a `rd/rdv/rdp`, `raw_of`, `manager_of`,
  `emit`, `npc_budget`, `p8_now_us`, `GetCurrentThreadId`, los auxiliares atómicos y `__security_check_cookie`. Ni
  `memcpy`/`memset`, ni `rep stos`, ni VEX.

### 17.3 Configuración y cableado

- **`[session2] guest_stage_diag=0|1`**, en `config.{h,cpp}` y leído con `loaded_config()`. No toca `mod.cpp`.
- `guest_start` (`guest.cpp`) lo arranca. `guest_diag_worker.cpp` llama a `guest_stage_diag_tick()` cada 5 s, en el
  hilo de diagnóstico y nunca en el de red. `guest_describe()` añade `| stage diag on hooked N/15 …`.
- En el anfitrión no hay clave nueva: los tokens y el descarte van con `observer_stage_sync=1`.

### 17.4 Líneas esperadas

| Línea (invitado) | Qué dice |
|---|---|
| `GuestStage ON (guest_stage_diag, READ ONLY): hooked: … \| REFUSED: none \| … error codes missing actor 0x… duplicate 0x…` | instalación |
| `GuestStage [2661 in 0xC05BB0] … 2661 payload 179 bytes (91 + 11 x 8) kind 4 \| corr=2661:A0100001:0x13:0x8 \| … bindings 8: 0x20000003->0xA0100001(type 5) …` | cada 2661 que llega |
| `GuestStage [2661 route 0xC0612A] … manager 0x… (ClientSequencerStageManager) of actor 0xA0100334 … \| addressed actor … (0xA0100001), its own manager 0x… \| sequencer owner 0xA0100001 -> ANOTHER character's manager …` | **dónde cae** y el veredicto |
| `GuestStage [2661 result 0xC061C1] … 0xA9CA40 result 0x0 = created … \| instances 0 -> 1` | resultado de `0xA9CA40` |
| `GuestStage [2981 in 0xC073A0] … corr=2981:A0100001:0x164:0x1 …` y `[2981 route 0xC07749] …` | se cruza con la línea `ObserverStage [2981 …] … corr=…` del anfitrión |
| `GuestStage [2322 field 0xB87A32] … corr=2322:A0100334:0x0:0x0 \| field … owner user … 0x90100333` | el 2322 y su campo |
| `GuestStage [NPC vf13 0x9ED290] NPC 0xB010006D … comp+0x2D8 0xA0100001 …` | el NPC entra en la vf13 |
| `GuestStage [NPC res8 0x9ED401] … -> EXITS here …` / `passes …` | salida temprana |
| `GuestStage [NPC owner 0x9ED453] … searches manager 0x… … NOT the manager the stage Acks are routed to …` | **el dato decisivo de B1 en vivo** |
| `GuestStage [NPC instance 0x9ED48C] … FOUND: the binding happens` | enlace |
| `GuestStage [M+0x231=0 0x77AA8B]` / `[M+0x231=1 0x77B0CE]` / `[M+0x231 change]` | transiciones de M |
| `GuestStage summary … managers seen: … NPC side …: passed local_res8 …, owner actor found …, instance found …` | cada 60 s |

| Línea (anfitrión, `observer_stage_sync=1`) | Cambio |
|---|---|
| `ObserverStage [2661 …]`, `[2981 …]`, `[2810 …]`, `[2322 …]` | añaden `corr=op:actor:seq:valor` |
| `ObserverStage [entry 0x2BC84E2] … it OWNS … ledger: N sequencer(s) of its previous field dropped` | descarte al cambiar de campo |
| `… NOT the owner (observer) … PLAN, NOT SENT …; native order: 2981(0x13) 2661(0x13) … 2322 \| NOT SENT BECAUSE (… Fase 2) …` | orden nativo y motivo |

**Correlación:**
- Los 2981 compartidos llevan el mismo `corr=` en los dos PC: el del anfitrión al serializar y el del invitado al
  manejarlo.
- La captura del invitado conserva los bytes; para los 2661, el FNV-1a de la línea `[2661 in]` es el de su payload.

## 18. Próxima sesión de Shadow: claves

- **Shadow** (`cdmp.p8-shadow-full.ini`): añadir **`guest_stage_diag=1`** en `[session2]`. Conviene mantener
  `load_diag=1`, que aporta `P8Probe [2322 write 0xB87A67]` con M, y `s1_diag=1`, que aporta
  `P8Probe [binding 0x9ED431]`.
  - Si `s1_diag` pesa demasiado, `guest_stage_diag` funciona solo: no depende de él ni comparte puntos.
- **Anfitrión** (`cdmp.p8-host-full.ini`): **`observer_stage_sync=1`** (no 2), más `load_diag=1` y `s1_diag=1`.
- **No se tocaron los `.ini` de `docs/research/ini/`** (había cambios ajenos en curso en dos de ellos).

**Criterio de lectura:**
- `[NPC owner 0x9ED453] … NOT the manager the stage Acks are routed to` confirma B1 en vivo, y el puntero y la vtable
  de X dicen si la alternativa B es viable.
- `[NPC res8 …] EXITS` en los 15 NPC con `comp+0x2D8 0xA0100001` explica que `0x9ED431` no se ejecute.
- Los 2981 compartidos deben dar `route … ANOTHER character's manager` y ningún resultado visible.

## 19. Pruebas, compilación y riesgos

- **`.\scripts\build.ps1` (RelWithDebInfo):** compila y pasan **19/19 pruebas**.
  - El único aviso es el preexistente de `guest.cpp` (`try_refresh`, parámetro `net` sin usar).
  - Nueva: `optd_guest_stage`, con los 2661 `0x13` (8 enlaces de tipo 5, FNV-1a), `0x164` y `0x197`; los 2981 `0x13`,
    `0x164` y `0x197` y el compartido (anfitrión #139918 = invitado #17684, mismo `corr=`); el 2322 #794; los
    veredictos de enrutado y de resultado; `ManagerBook`; `NpcTracker`; y el descarte y orden nativo del libro, también
    con la secuencia real de entradas del dueño.
- **Riesgos de la sonda [E]:**
  - Lecturas protegidas en el hilo que maneja los Ack.
  - Si una disposición deducida fuera otra, solo saldrían valores erróneos (`unreadable`, `NOT routed`), nunca
    escrituras.
  - `0x9ED290` puede llamarse a menudo: se cuenta todo y solo se encolan 3 llamadas por NPC.
  - Los ganchos con salto corto o `je rel32` reubicado (`0xC0612A`, `0xB87A32`, `0x9ED401`) siguen el mismo
    procedimiento que `P8Probe 0x779E77`/`0x2BC8470`. Si safetyhook propusiera otros bytes, el punto se rechaza.

## 20. Líneas compartidas tocadas (fase 2)

| Archivo | Cambio exacto |
|---|---|
| `src/mod/config.h` | campo `bool s2_guest_stage_diag = false;` (con su comentario) al final de `Config` |
| `src/mod/config.cpp` | `c.s2_guest_stage_diag = read_uint(ini_path, L"session2", L"guest_stage_diag", 0) != 0;` tras `observer_stage_sync` |
| `src/mod/session2/guest.cpp` | includes de `guest_stage_diag.h` y `mod/config.h`; en `guest_start`, `guest_stage_diag_start(loaded_config().s2_guest_stage_diag);` antes de `guest_diag_worker_start()`; en `guest_describe()`, `+ guest_stage_diag_describe()` |
| `src/mod/session2/guest_diag_worker.cpp` | include y `guest_stage_diag_tick();` tras `guest_s3_tick()` |
| `CMakeLists.txt` | `src/mod/session2/guest_stage_diag.cpp` en `cdmp_mod` (tras `guest_s3.cpp`); ejecutable y prueba `optd_guest_stage` (tras `optd_observer_stage`) |

No se tocaron `mod.cpp`, `server.cpp`, `p8_probes.*`, `guest_actor_diag.*` ni `guest_s3.*`.
