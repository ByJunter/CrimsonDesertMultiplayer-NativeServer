# P8, sesión 6 con Shadow: el 2322 del observador funciona; NPC, ESC y los dos cierres

10 de octubre de 2026, rama `opcion-d`, HEAD `016d2fd`. Build del juego 1.0.0.2976; direcciones en **RVA**
(base `0x140000000`). Mismo `cdmp.asi` en los dos PC (SHA `68A47585…C476`). Hora de Shadow = anfitrión − 1 h.

**[V]** = comprobado en log, captura, volcado o instrucciones (capstone sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, Python `-I`). **[E]** = inferencia o hipótesis pendiente.

Solo análisis: no se tocó código, no se lanzó el juego, no se escribió en `bin64` (las capturas del cliente del
anfitrión solo se leyeron) y no se hizo commit. No se propone enganchar `.xtls`.

## 0. Evidencia

| Ref. | Archivo |
|---|---|
| GA | `C:\Emulador CD\backups\p8-sesion6A-shadow\cdmp.log` (invitado, tramo A) |
| GAC | `…\p8-sesion6A-shadow\cdmp_packets_20261009-224011.bin/.log` |
| HA | `C:\Emulador CD\backups\logs\cdmp_20261009-2337_p8-6A-host.log` |
| GB | `C:\Emulador CD\backups\p8-sesion6B-shadow\cdmp.log` (invitado, tramo B) |
| GBC | `…\p8-sesion6B-shadow\cdmp_packets_20261009-224848.bin/.log` |
| GD | `…\p8-sesion6B-shadow\cdmp_crash_20261009-225257_17428_1.dmp` (volcado del invitado) |
| HB | `C:\Emulador CD\backups\logs\cdmp_20261009-2345_p8-6B-host.log` |
| HBC | `D:\SteamLibrary\…\bin64\cdmp_packets_20261009-234606.bin/.log` (cliente propio del anfitrión, tramo B) |
| HD | `%LOCALAPPDATA%\CrashDumps\CrimsonDesert.exe.18568.dmp` y `CrimsonDesert.exe(1).18568.dmp` (WER LocalDumps del anfitrión, 23:52:44/49, 250 MB cada uno). **Conviene copiarlos a `backups`**: Windows los rota. |
| WER | Visor de eventos, Application: `Application Error` 1000 a las 23:52:38 y 23:52:45, PID `0x4888`, `0xC0000005`, `exe+0x47EFE20` |

Herramientas (en el scratchpad de la sesión, no se conservan): lector CDPK según `capture_file.h`, lector de
minidump (streams 3/4/5/6/9, TEB y TLS), desensamblador con `.pdata` e IAT. `analyze_capture.py … trips` del repo.

## 1. Resumen

1. **S2 queda aceptado en el tramo B para la entrada probada [V].** Un único 2322 nuestro tras su 2144 y su 3066;
   el manejador nativo escribe `1→2` sobre el mismo M que espera `0x11`; `0xA97D60` devuelve 1 por sus
   condiciones reales; `0x11→0x12` (no `0x18`); estados posteriores completos; 2445 y 2762 normales. En A, con
   el mismo binario y sin emitir, la carga se queda en `0x11` con `M+0x231 = 1`. Faltan los casos de salida,
   reconexión y cambio de campo (criterio 6): la sesión acabó en el cierre del anfitrión.
2. **NPC: no faltan réplicas ni actúa ninguna guarda del mod [V].** De lo que el cliente del anfitrión manda
   para los NPC que ve el invitado, llega el 95 % de las acciones (2223→2991), el 100 % de los movimientos
   (2556→2697) y el 100 % de los cambios de IA (2734→2404); se instalan las 10 294 acciones que entran en el
   consumidor; guardas de update y de capas: 0. Los defectos vienen de tres causas:
   - **A-pose (15 NPC)**: restauración fallida (`eErrNoInvalidActionChart`) **y** el servidor no les reenvía
     nada (0 × 2991/2697/2404) aunque el cliente del anfitrión manda hasta 232 `StartActionReq` por clave. Son NPC
     de secuenciadores del dueño.
   - **Sin escenas para 235 NPC**: todos salen de la vf13 en `local_res8` (`0x9ED401`); en el invitado no se
     enlaza ninguno (binding 0), en el anfitrión 177.
   - **Pérdidas por orden en S3 [V]**: para los 465 NPC que libera `observer_hold`, el `StartSpawnActionAck`
     2723 y casi siempre 3 × `ChangeAIStateAck` 2404 llegan **antes** que su 2005 y se pierden (consumidor de 2723:
     0).
3. **ESC «ver un objeto» en el invitado [V]**: el servidor pone el secuenciador personal del usuario 2 (`0x1A4`) en
   el gestor del **dueño del campo**. El 2981, el 2661 y el 2524 llegan bien al invitado, pero el
   `UpdateStageChartEventAck` 2111 lleva la clave del Kliff del anfitrión y el `DeleteSequencerAck` 2810 (fin de
   la secuencia) se manda **al anfitrión** (`corr=2810:A0100001:0x1A4`). El invitado nunca cierra la secuencia y
   se queda sin control (solo cámara).
4. **Cierre del anfitrión [V + E]**: no fue una pausa que rechazara la Req. `in: … refused 0 … failed 13`: fueron
   **excepciones** dentro de `push_request` (asignador o cola del juego), tragadas por el `__except` del mod. El
   cierre `exe+0x47EFE20` es una lectura de un bloque **desmapeado** en la lista libre de una página de
   **mimalloc 2.0** (clase de 40 KiB, la de los búferes de paquete de `0x8030`), en el hilo principal mientras
   asignaba un paquete. **[E fuerte]**: el mod asigna sus búferes en el hilo de red con `0x47EFA78`, un
   `mi_heap` global con candado que comparte el `tld` (segmentos) del hilo principal. mimalloc no admite eso y
   corrompe los segmentos del hilo principal. El arreglo es asignar con el heap del propio hilo
   (`0x47EF9B0`) o en el hilo del servidor.
5. **Cierre del invitado [V]**: la ruta B conocida (`0x2042A60 → .xtls 0xE014020 → 0x2045A20`), con
   `r8 = 0x9010031E`. El usuario que falta es **el propio invitado**, que el 2174 reescrito hace dueño del campo.
   Lo borra el cliente al procesar el 2002/2003 que el mod responde en local, 22 ms antes. En A, el logout lo
   contestó el servidor real (con `RemoveActorAck` y miles de `UnloadSyncSceneObjectAck` antes) y no hubo cierre.
   El «cuelgue» posterior de Alt+F4 es el filtro de excepciones del juego (`exe+0x4896E00`), al que encadenamos
   sin `crashpad_handler.exe`.

## 2. Pregunta 1: aceptación de S2 (tramo B frente a A)

### 2.1 Anfitrión, tramo B (HB)

```
4994 [23:50:07.361] ObserverMilestone [2144] … trip E1.T2: MoveFieldCompleteAck 2144 #1 to session 2 queued (seq 3)
4995 [23:50:07.361] ObserverMilestone [3066] … trip E1.T2: MoveFieldCompletePostAck 3066 to session 2 queued | after the trip's 2144 (seq 3) on the same thread …
4996 [23:50:07.361] ObserverMilestone [decision] … trip E1.T2 | observer entry of user 0x9010031E actor 0xA010031F into field 0x3DE28D17A10 (key 0x81020001) owned by 0x90100000 | EMIT NOW: 0x2A757A0 + 0x2999D80 on this thread | epoch 1 | session: live 0x3DDC40D00E0, the traveller's 0x3DDC40D00E0 | thread: server realm yes, send context yes, scope mode 1 open 0 current 16777215 | mode SEND
4997 [23:50:07.361] ObserverMilestone [our 2322] … OUR 2322 reached take_frame on this thread (actor 0xA010031F, the live session 2) -> queued | corr=2322:A010031F:0x0:0x0
4998 [23:50:07.361] ObserverMilestone [called] … its 2322 IS in user 2's queue (x1) (ToTarget session 0x3DDC40D00E0) | call 0.002 ms
5539 [23:50:23.365] ObserverMilestone [in] … user 2's LoadingCompleteReq 2445 #1 handed to the server (16.230 s after our 2322)
5540 [23:50:23.365] ObserverMilestone [2762] … LoadingCompleteAck 2762 to session 2 queued | 16.230 s after our 2322 | 0.707 ms after its 2445 | the load's normal exit seen from the host
8002 [23:52:36.399] ObserverMilestone [logout] … user 2's logout asked: trip E1.T2 was already decided
```

- **[V]** Viaje 1 (campo propio): `[native 2322] … (actor 0xA010031F) queued | ours will not be sent` (línea 4296)
  y `[trip end] … native 2322 x1 | ours x0` (4778). Convivencia correcta con la ruta nativa.
- **[V]** No aparecen `DUPLICATE`, `LOST`, `not seen`, `NOT CALLED`, `FAULT` ni ningún `[2042]`.

### 2.2 Invitado, tramo B (GB)

```
1300 [22:50:18.725] P8Probe [2322 entry 0xB87890] qpc 999.532422 … actor key 0xA010031F | … handler +0x21 0 (handled)
1301 [22:50:18.725] P8Probe [2322 write 0xB87A67] qpc 999.532469 … [M+0x231] = 2 (0xB87A67): M 0x3F2EC323600, byte before 1 | M at 0x779E48 0x3F2EC323600 (SAME manager) | 2322 key 0xA010031F | loading state 0x10
1562 [22:50:18.726] P8Probe [loading 0x779E77] qpc 1001.118175 … 0xA97D60(M, pos) -> 1 | first certain blocker: none (0xA97D60 true) | lists 0/0 | M 0x3F2EC323600 M+0x231 2 M+0x174 0 M+0x2D4 0 M+0x2D0 0.000 (threshold 4.000) | state 0x11 | pos (-10717.79, 620.17, -3914.56)
1577 [22:50:19.220] GuestDiag loading transition: [P+0x15C] 0x10 -> 0x11 after 1.639 s in 0x10 | qpc 1001.118171
1578 [22:50:19.220] GuestDiag loading transition: [P+0x15C] 0x11 -> 0x12 after 0.000 s in 0x11 | EXIT FROM 0x11 | qpc 1001.118176
1754 [22:50:19.707] GuestLoad [in 2144] … trip 2 | MoveFieldCompleteAck #1 | +7684.262 ms after the 2549
1755 [22:50:19.707] GuestLoad [in 2322] … trip 2 | PlayAllPreLoadSequencerAck #1 actor 0xA010031F | +55.279 ms after its 2144 | corr=2322:A010031F:0x0:0x0
1821 [22:50:24.730] GuestLoad [out 2445] … trip 2 | LoadingCompleteReq to the host | +8555.877 ms after the 2322 | last state seen 0x15
1822 [22:50:24.730] GuestLoad [in 2762] … trip 2 | LoadingCompleteAck | +37.040 ms after the 2445
1873 [22:50:29.737] GuestLoad trip 2 summary … 2144 +13149.830 ms (x1) | 3066 +13149.833 ms | 2322 +13205.109 ms (x1, actor 0xA010031F, to OnRecvPacket +8.740 ms) | 0x11 in +14815.979 ms out +14815.984 ms -> 0x12 | 2445 +21760.986 ms | 2762 +21798.026 ms | … | VERDICT: one 2322, after its 2144 | 0x11 -> 0x12 (+1610.875 ms after the 2322) | 2445 and 2762: normal load completion
```

- **[V] El mismo M.** La sonda de `0x779E48` solo escribe cuando cambia la firma, y la firma incluye M
  (`p8_probes.cpp:369-376`). En el viaje 2 su acierto (hits 5→6) no generó línea: su M, `+0x231 = 2`,
  `+0x174 = 0` y las listas coinciden con la última línea. `[loading 0x779E77]` (1562) imprime
  `M 0x3F2EC323600`, que es el M de la escritura (1301).
- **[V] Estados posteriores** (1579–1785): `0x12→0x13` 0,033 s, `0x13→0x14`, `0x14→0x15` 0,977 s, `0x15→0x16`
  5,929 s, `→0x17`, `→0x18` a qpc 1008.126. La línea base local era `0x14` 14,1 s y `0x15` 2,3 s.
- **[V] Captura (`analyze_capture.py GBC trips`)**:
  `trip 2: #9118 2144 → #9128 3066 → #9380 2322 actor 0xA010031F → #17247 2445 → #17271 2762 -> one 2322, 47.950 ms after its 2144, after its 3066; 2445 yes, 2762 yes`.
- **[V] Ruta del cliente**: `GuestStage [2322 field 0xB87A32] … owner user in this client's view 0x9010031E; Q … same` (1738).
- **[V] Viaje 1 (campo propio, nativo)**: `0x779E77 -> 0` con `first certain blocker: M+0x174 != 0` (176) y
  después `-> 1` (178); `0x11→0x12` en 0,072 s. Es el contraejemplo del dictamen §3.2, otra vez: hay que mantener
  `0xA97D60`.
- **[V]** `2322 entry` se ejecuta 2 veces en el viaje 1 (qpc 964.2479 y 964.3185) con una sola escritura: el
  reintento nativo del manejador, igual en A. No es un 2322 duplicado: la captura tiene uno solo.

### 2.3 Tramo A (bloqueado), mismo binario

- HA:4498: `ObserverMilestone [decision] … trip E1.T2 | observer entry of user 0x901002F3 actor 0xA01002F4 … owned by 0x90100000 | WOULD EMIT (dry run: observer_milestone_send=0, nothing called)`. **[V]**
- GA:1382: `P8Probe [loading 0x779E48] … state 0x11 … M 0x552838E4E00 | M+0x231 1 (needs >= 2) | M+0x174 0 | … list M+0x1C0 n 0 (0x55269098538)`. GA:1398 `0x10 -> 0x11` a qpc 489.2028. Sin `0x11 ->` hasta el logout (22:44:13, unos 149 s). **[V]**
- `analyze_capture.py GAC trips`: `trip 2: #8892 2144 → #8902 3066 -> NO 2322; 2445 no, 2762 no`. **[V]**

### 2.4 Tabla de criterios

| # | Resultado |
|---|---|
| 1 | **Cumple [V]**: `[decision] EMIT NOW` + `[called] … (x1)` tras `[2144]`/`[3066]`; `GuestLoad [in 2322] … #1 … +55 ms after its 2144`; `trips` = `one 2322 … after its 2144, after its 3066` |
| 2 | **Cumple [V]**: `1→2` sobre `M 0x3F2EC323600`, el mismo que `0x779E48`/`0x779E77` |
| 3 | **Cumple [V]**: `0xA97D60 -> 1 … (0xA97D60 true)` con `+0x174 0`, listas 0/0 y `+0x2D4 0` |
| 4 | **Cumple [V]**: `0x11→0x12`; estados hasta `0x18`; `[out 2445]`/`[in 2762]`; en el anfitrión `[2762] … normal exit` |
| 5 | **Parcial**: durante la carga y los 2,5 min siguientes, sin regresión [V]: rtt 36–45 ms; `Net:` del invitado sin vueltas lentas (`slow 0 (0 in all)`, máx. 37 ms); S3 como en A (A: 364 retenidos / 364 liberados; B: 472 / 465 + 7 retenidos al final). Después, el cierre del anfitrión (§5), **[E] no atribuible al 2322**: 2 min 29 s después y en otra ruta (asignador) |
| 6 | **No probado**: no hubo salida/reentrada, reconexión ni cambio de campo. Lo único que se vio: `[logout] … trip E1.T2 was already decided` y ningún 2322 tras el logout [V] |

### 2.5 Observación: latencia de bajada en la carga

**[V]** El anfitrión encola el 2144 a las 23:50:06.578 y el invitado lo recibe a las 22:50:14.826 (hora de Shadow).
Con el desfase de reloj estimado a partir de 2174 y 2549 (≈ 0,6 s) **[E]**, son unos **7,6 s en la bajada**. En HB, a
las 23:50:11, `reliable pending 549 … pending 106317 B queue 100 ms est 105600 B / limit 104857 B` y después
`out 1015.1 KB/s`: el 2144 y nuestro 2322 esperan detrás de la ráfaga de la carga (2665/2005). No impide la
aceptación, pero explica los 16,2 s entre «nuestro 2322» y el 2445 vistos desde el anfitrión.

## 3. Pregunta 2: animaciones de los NPC

### 3.1 Lo que se descarta [V]

| Hipótesis | Evidencia |
|---|---|
| La guarda de update congela actores sin acción | GB, línea final de GuestS3: `null action at 0x232D5C0 0 … guard skips 0 keys 0 recovered 0 stuck 0`; estado: `guard on: held actors 0 (skips 0…)` |
| La guarda de capas omite animaciones | `layer guards on \| animation skipped 0 … empty layer guarded 0 \| not controlled by 0x9010031E: skipped 0` |
| 2991 descartados por la bandera de `0x48AD40` (`[[CC+0x60]+0x10]&1`) o por una sesión de acción vieja | `2991 consumer 10294 (discarded 0) \| StartAction … from 2991 10294/0/0`; `first Update 679 (chart without action 0)` |
| El controlador manda solo parte de los paquetes a los observadores | Mismos NPC, ventana del juego (anfitrión t 246–374, invitado desplazado 170,57 s): **2223→2991**: 10 546 enviados, 10 154 recibidos, mediana por clave 0,95 (p10 0,89); **2556→2697**: 17 504 → 17 327, mediana 1,00; **2734→2404**: 5 665 → 5 658, mediana 1,00. Los bytes coinciden (p. ej. `B0100238`, acción `0x80E1E165` cada ~1,3 s en los dos) |

### 3.2 Clase A: A-pose, 15 NPC [V]

- `GuestDiag login FAILED … error 0x349EFD61 (eErrNoInvalidActionChart)` y `restore … CHART FAILED 15` para
  `B010006D, 71–78, 7E, 80, 82, 1F5, 1F6, 1F9` (tipos 3, 4 y 5). Son los NPC de secuenciador de las sesiones
  anteriores, con otras claves en `0x1F5`, `0x1F6` y `0x1F9`.
- **Ninguno recibe nada después de su 2005**: 0 × 2991, 0 × 2697 y 0 × 2404 en toda GBC. En HBC, el cliente del
  anfitrión sí manda por ellos 2–232 `StartActionReq`, 1 `MoveActorReq` y 4–5 `ChangeAIStateReq` por clave.
- De los 707 NPC con 2005 en el invitado y `StartActionReq` del anfitrión en la ventana, 684 reciben 2991 y
  **23 no reciben ninguno**: 10 de los 15 fallidos (por ejemplo `B0100077`: 96 enviados, 0 recibidos; los otros
  5 no tuvieron 2223 dentro de la ventana), `B010007F` (enlazado al mismo secuenciador `0x16C`, restaurado bien) y
  12 claves de tipo 3/6 aparecidas tarde (t 111–195 s) con un solo 2223 en la ventana.
- **Causa**: sin acción restaurada (S1: la acción guardada es `seq_bodyanim_*`, del gráfico dinámico que solo crea
  el enlace con el secuenciador) y sin ningún 2991 que instale otra, el actor se queda en la pose de enlace.
  **[E]** Que el servidor no reenvíe a un no participante las réplicas de los NPC de escenas del dueño parece un
  filtro de visibilidad de escena personal; no está localizado en el código.

### 3.3 Clase B: congelados en la acción restaurada [V/E]

NPC de secuenciador que se restauraron bien pero no reciben réplicas: `B010007F` seguro; las 12 claves tardías con
un solo 2223, sin confirmar que sean de escena. Se quedan en la acción del 2005, en bucle. **[E]**: el usuario lo
vería como «repite una animación».

### 3.4 Clase C: la mayoría (repetida o a medias) [V/E]

- **[V] 235 NPC con clave de secuenciador; ninguno se enlaza.** GB:3556 `GuestStage summary`: `NPC side (0x9ED290 …): 235 NPCs, owners: 0xA0100001 x235 | passed local_res8 0, exited there 235, owner actor found 0 … instance found 0`. Probe: `binding 0x9ED431 hits 0` en el invitado frente a 177 en el anfitrión (HB). Así se cierra la laguna del dictamen §7.1: **la búsqueda de X no llega a ejecutarse**, porque el nodo de escena propio del NPC (`local_res8`) es 0 en el invitado. **[E fuerte]**: ese nodo lo pone el 2661 (sus enlaces llevan `nodo→actor`, p. ej. `0x2000000C→B010006D`), así que crear la instancia en X tampoco bastaría para enlazar.
- **[V] Los 2981 del anfitrión cambian el M del invitado.** `GuestStage [M+0x231 change] … manager 0x3F2EC323600 (actor 0xA010031F): 2 -> 3 (seen at 2981 route 0xC07749)` (GB:1945). Cada `2981 route` dice `sequencer owner 0xA0100001 -> ANOTHER character's manager`: es B1 confirmado en vivo.
- **[V] S3 pierde la acción de aparición y el estado de IA inicial.** `observer_hold` liberó 465 NPC (HB) y el invitado recibió 465 × `StartSpawnActionAck` 2723 de 465 claves, pero `2723 consumer 0`. En GBC, 377 de esos 2723 llegan **antes** de su 2005 (mediana −1 µs, en la misma ráfaga) y ninguno después con el actor ya creado. Además, 429 claves reciben su 2404 inicial antes del 2005 (normalmente 3, con una mediana de 64 ms antes). El motivo es el diseño: la liberación ocurre **después** de `StartActionReq::vf2 0x296CAA0` (`child_hold.h:32`), que ya ha emitido los Ack a los observadores. **[E]**: el NPC aparece sin su acción de aparición y en el estado de IA por defecto hasta el siguiente cambio.
- **[E] Por qué se ven repetidas o a medias**, con la evidencia anterior. Las acciones de 2991 se instalan, pero la
  parte de escena (BodyAnimation, eventos de rama 2524/2111 de las escenas del dueño, nodo de escena) no existe en
  el invitado. Además falta el estado de IA inicial en los NPC liberados por S3. La asignación «bucle» frente a «a
  medias» por NPC necesita medida (§7.3).

### 3.5 Lo que recibe el cliente del anfitrión [V]

Es el **controlador**: envía 24 252 × 2223, 39 442 × 2556 y 12 007 × 2734 por 1 063–2 198 claves, y solo recibe
2991/2697 de los actores que controla el usuario 2 (2 claves). Tiene los enlaces: `binding 0x9ED431 hits 177`,
`instance 0x9ED47F hits 177`, `BodyAnim 0xA79770 hits 21`.

### 3.6 Nota aparte: el invitado envía solicitudes de mundo [V]

GBC C>S: `MoveActorReq` para 1 086 claves de NPC, casi todas **una sola vez** y unos 0,18 s después de su 2005
(963 claves), y 28 objetos de tipo 7 (controlados por el anfitrión) movidos de 19 a 144 veces. También 4 060 ×
`RotateActorReq` para 274 claves de tipo 7, que el cliente del anfitrión no rota nunca, y 3 028 ×
`SceneLevelOnDemandGimmickSpawnReq`, con 2 480 `SpawnOnDemandGimmickNak` en respuesta (el anfitrión: 326 y 67).
Esos movimientos no se reenvían al anfitrión: solo recibe 2697 de 2 claves. **[E]** Falta saber si el servidor
los aplica a su estado. Merece un filtro o una medida aparte; no explica las animaciones.

## 4. Pregunta 3: ESC «ver un objeto» en el invitado

### 4.1 Secuencia [V]

| Hora | Dónde | Paquete |
|---|---|---|
| GBC t=153.548 (22:51:46.3) | C>S | `UseItemReq 2855` (objeto `0x0F42A5`) |
| HB 23:51:45.724 | servidor (T4572) | `save guard: refused create directory …\slot0_backup` + `report guard: … 'Savedata Save Fail Event'` (el guardado que provoca el objeto) |
| t=153.653 | S>C | `RemoveItemToInventoryAck`, `UpdateMissionListAck` (del Kliff del anfitrión), `SaveToFileCurrentPlayerAck 3016`, **`2981 A010031F 0x1A4`**, **`2661 A010031F 0x1A4`** (1 enlace `0x20000002→A010031F`) |
| GB:2992-2994 | cliente | `2661 route … sequencer OWNER's manager (native case)`, `0xA9CA40 result 0x0 = created … instances 0 -> 1` |
| t=153.849–153.952 | C>S | 2 × `UpdateSequencerGamePlayDataEventReq 2173` y 12 × `UpdateStageChartEventReq 2591` de `0x1A4` (el cliente conduce su secuencia) |
| t=153.961 | S>C | 2 × `UpdateStageChartEventAck 2111` con `04 01 00 10 A0 A4 01 …`: **actor `A0100001`** (el anfitrión) |
| t=159.900 / 159.983 | C>S / S>C | `StageBranchStateReq 2436 (0x1A4)` → `StageOnBranchEventAck 2524` con `A010031F` |
| HB 23:51:52.055 | servidor | `ObserverStage [2810 DeleteSequencerAck] … -> actor 0xA0100001 user 0x90100000 via ToTarget \| sequencer 0x1A4 … corr=2810:A0100001:0x1A4:0x0` |
| invitado | — | `2810 in 0xC06330: hits 0` en toda la sesión: **nunca recibe el fin de la secuencia** |

Ni la secuencia emite 2322 ni el invitado lo necesita aquí: no es un viaje de carga. Tampoco interviene 2223:
del invitado solo salen 2223 de su personaje y de su acompañante (`B0100320`). HBC muestra el mismo patrón para el
objeto del anfitrión (`0x1A5`, 23:52:32).

### 4.2 Por qué [V]

`0x2879440` emite el 2810 en `0x2879819` con `rcx = [r12+0x18]` y un `ToTarget` construido con
`[r12+0x18]->vfE8()` (`0x28797BB–0x28797F4`). Es decir, va al **actor dueño del gestor** en el que el servidor
guardó el secuenciador, no al dueño del secuenciador (`play data +0`). El servidor guardó `0x1A4` en el gestor del
dueño del campo, el anfitrión (`0x2B00C70(field)` por `[field+0x180]`, como en `p8-observador-escenas.md`
§2.5). El 2111 sigue la misma regla. El 2981, el 2661 y el 2524 se dirigen por el actor de la secuencia y llegan
bien. **[E]** En el cliente, el personaje queda en el estado de secuencia (cámara libre, sin control) hasta un
2810 que no llega.

### 4.3 Relación con el problema de escenas del observador [V/E]

Es el mismo desacople que S1/S2 (dictamen §3.1), en sentido contrario. El servidor es un gestor por **dueño del
campo**: las escenas del anfitrión no le llegan al invitado (S1), y lo personal del invitado vive en el gestor del
anfitrión, de modo que su cierre se dirige al anfitrión (ESC). En el invitado el enrutado del cliente es
indiferente al actor del paquete (B1): un 2810/2111 que llegara al invitado caería en su M, que es justo donde
está `0x1A4`.

## 5. Pregunta 4: el ESC del anfitrión y el cierre `exe+0x47EFE20`

### 5.1 Cronología del anfitrión [V]

| Hora | Evento |
|---|---|
| 23:52:32.224 (HBC t≈370.70) | `UseItemReq` del anfitrión → `2661 0x1A5` para A0100001 |
| t≈374.265 (23:52:35.79) | última ráfaga S>C a su cliente; después solo un `GlobalGameEventUpdateAck` (t=374.962) |
| 23:52:35.850 | `ERR … the Troc tunnel stops for epoch 1: the server could not take Req 2556` → `C4: logout begin … trigger=link_lost` |
| 23:52:35.850–38.689 | 39 × `LogoutSessionToServerReq 2001 COULD NOT be handed to the server; will retry` (una por vuelta de red; el contador `failed` ya iba en 13 a las 23:52:36.598) |
| 23:52:36.051 | barrera: `packet buffers 25591 handed/25590 seen`: el **último búfer que sí entró en la cola no lo tomó el servidor** |
| 23:52:36.598 | `in: injected 25591 refused 0 blocked 0 failed 13 seen by the server 25590` (a las 23:52:31: `failed 0`) |
| t=374.356–374.711 | el cliente del anfitrión sigue enviando (`LeavePartySelfReq` ×5) **sin respuesta** (a las 372.4/372.5 la respuesta llegaba en 9 ms); sus C>S siguen hasta **t=375.314** (≈23:52:36.84) |
| 23:52:38 | WER `Application Error`, `exe+0x47EFE20`, PID `0x4888` |
| 23:52:38.668 | `Net: one loop took 711.7 ms (mostly host tunnel)` **[E]**: coincide con el volcado de WER/crashpad, que congela el proceso |
| 23:52:44.089 | última línea del log; volcados a las 23:52:44 y 23:52:49 |

### 5.2 ¿Pausa y Req rechazada? No [V]

- `failed` cuenta los `push_request` que devuelven false (`server.cpp:1060-1063`): `guarded_alloc`
  (`g_alloc = 0x47EFA78`) o el `memset`/`memcpy`/`g_push` bajo `__try`. Una pausa no hace fallar un `push` a la
  cola ni una asignación. Los motivos de rechazo de verdad (`server_accepts_guest`, época, canal) cuentan como
  `refused`/`blocked`, que quedaron en 0.
- El cliente del anfitrión **no estaba en pausa**: siguió enviando 1 s más. Lo que se detuvo hacia t≈374.27–374.36
  es el **servidor**: no respondió a sus `LeavePartySelfReq` y no tomó el último búfer inyectado. **[E]** Con el
  ESC puede que el juego detenga el servidor (en el volcado, T4572 espera en `0x2774A9F`, `call [rax+8]`, su espera
  de bucle). Pero el fallo del mod fue una **excepción**, no un rechazo.

### 5.3 El código del mod [V]

- `session2.cpp:161`: `if (!server_inject(p)) kill_tunnel("the server could not take Req …")`. Cualquier `false`
  (rechazo, bloqueo, excepción) se trata igual. Con `g_reconnect` se convierte en `begin_logout(LinkLost)`
  (`session2.cpp:97-102, 173-186`), es decir, en «enlace perdido» aunque el enlace de Steam siguiera sano (rtt
  36 ms en la misma línea `Net:`).
- `hand_logout` (`server.cpp:1145-1175`) se llama en **cada vuelta** del hilo de red (`server_logout_tick`,
  `session2.cpp:330`) sin espera entre intentos. Cada intento vuelve a entrar en `server_inject` → `push_request` →
  asignador → excepción, y escribe un `ERR` (39 en 2,8 s). Solo hay límite cuando el usuario 2 no existe
  (`kLogoutPendingMs`).
- `guarded_alloc` y `push_request` tragan con `EXCEPTION_EXECUTE_HANDLER` una excepción **dentro del asignador del
  juego**. `0x47EFA78` entra en un `CRITICAL_SECTION` (`0x47EFA8F`, `EnterCriticalSection`) antes de asignar y
  sale en `0x47EFAC5`. Una excepción entre los dos deja la sección tomada por el hilo de red y el heap a medias.
  `g_push` (`0x35DF50`) toma el candado de la cola con `vf8` y lo suelta con `vf10`, y además asigna bloques de cola
  (`0x35FB00`): el mismo riesgo. El `__except` no registra ni el código ni la dirección: hoy no se sabe cuál de las
  dos falló.

### 5.4 `exe+0x47EFE20` en el volcado (HD) [V]

- Hilo **16880** (`0x41F0`). Su heap por defecto `[TLS+0x90] = exe+0x6A44178`, en `.data`: el heap estático del
  hilo principal de mimalloc (`_mi_heap_main`). Es el mismo hilo que en HB escribe `P8Probe [binding 0x9ED431]`,
  lógica del cliente.
- Pila (por direcciones de retorno, con marcos comprobados): `0xB54B90 → 0x35C670` (el `PostPacket` del cliente:
  `mov ecx,0x8030` en `0x35C6A8`) `→ 0x47EF9B0` (**sin candado**: el heap del propio hilo; el retorno `0x35C6BF`
  está justo encima del marco de `0x47EF648`, sin `0x47EFABB`) `→ 0x47EF648 → 0x47EF690/0x47EF6F4 → 0x47EFDF4`.
- `0x47EFDF4` es `_mi_page_malloc` de **mimalloc 2.0**: `rbx = page->free [rdx+0x10]`; si no es nulo,
  `inc page->used [rdx+0x18]`, y en **`0x47EFE20 mov rax,[rbx]`** lee `block->next`. El AV es una lectura de
  `0x3DDC43FA000` con `rbx = 0x3DDC43FA000`, página `rdx = 0x3DDC40014B8` y tamaño pedido `0x803F` (`0x8030`
  alineado a 16).
- El diseño encaja con mimalloc 2.0: segmentos de 32 MiB (`and …,0xFFFFFFFFFE000000`), rebanadas de 64 KiB
  (`>>16`), páginas de `0x50` B en `segment+0x108`, `segment->thread_id` en `+0x100` comparado con `gs:[0x30]`
  (`0x47F02BC`), `local_free +0x20`. La página es la 63 del segmento `0x3DDC4000000`. `0x3DDC43FA000` = rebanada 63
  + `0xA000`: el **bloque 1** de una página de la clase de **40 KiB**. Es una dirección válida, pero **no está
  mapeada**: la lista libre apunta a memoria ya purgada o desconfirmada. Es una **corrupción del heap**, no un
  puntero nulo.
- `0x35C670` elige el asignador según `TLS+0x1FD`: si vale 1, `0x47EFA78` con candado; si vale 0, `0x47EF9B0` sin
  candado. El bucle del servidor (`0x2774A49`) hace lo mismo al liberar (`0x47F03BC` / `0x47F02BC`). En el
  volcado, **ningún hilo** tiene `TLS+0x1FD ≠ 0` (leído en cada TEB/TLS): **el juego no usa en la práctica el heap
  global con candado**.
- `0x47EFA78` usa `[exe+0x6C0E668] = 0x3DDC40C0000`, creado con `0x47F19A8 → 0x47F19BC`. Ese es
  **`mi_heap_new_in_arena`**: toma el heap de respaldo del hilo que lo crea, reserva `0xBF8` B, copia el vacío,
  `heap->tld = backing->tld` (`mov rax,[r14]; mov [rbx],rax`), `thread_id` en `+0xB20` y `arena_id` en `+0xB28`.
  El objeto `0x3DDC40C0000` vive en el segmento `0x3DDC4000000`, el mismo de las páginas del heap principal y de la
  página corrupta (la cola de `exe+0x6A44178` apunta a `0x3DDC40006A8`…). **[E fuerte]**: el heap global lo creó
  el hilo principal y **comparte su `tld`** (sus segmentos, colas de rebanadas y purgas).

### 5.5 Causa probable [E fuerte]

1. Para cada Req del invitado, el mod asigna un búfer de `0x8030` en **su hilo de red**, con `0x47EFA78`.
2. Ese heap comparte el `tld` del hilo principal. Las páginas nuevas, las retiradas y las purgas que hace nuestro
   hilo (bajo el candado del heap global) tocan las estructuras de segmentos del hilo principal **sin
   sincronizarse con él**, que asigna y libera en su propio heap sin candado.
3. mimalloc exige que un heap solo asigne desde el hilo que lo creó. Su `tld` no es seguro entre hilos.
4. Resultado: una página de 40 KiB del hilo principal se purga mientras sigue en uso. Primero falla nuestra
   asignación en el hilo de red (23:52:35.850, tragada, 13 veces). Después falla el hilo principal al enviar su
   propio paquete (`0x35C670`), sin manejar: cierre.
5. El desencadenante de ese momento no está probado. Puede ser una purga o recogida de memoria al abrir el menú, que
   explicaría la coincidencia con el ESC, o simplemente la carrera, más probable cuanto más tráfico. Hay casi 100
   búferes por segundo durante toda la sesión.

**Lo que falta comprobar**: (a) en el arranque, leer `[[exe+0x6C0E668]+0xB20]` (`thread_id` del heap global) y
compararlo con el TEB del hilo principal; (b) registrar en el `__except` de `guarded_alloc`/`push_request` el
código y la dirección de la excepción (con `GetExceptionInformation`), para saber si falló el asignador o la cola;
(c) repetir el ESC del anfitrión con el invitado en el mundo, con el cambio de §7.2.1 y sin él.

### 5.6 Arreglos de raíz propuestos

1. **No usar el heap global compartido desde un hilo del mod.** Asignar el búfer con `0x47EF9B0`
   (`mi_malloc_aligned` del heap **del propio hilo**: nuestro hilo de red ya tiene uno propio,
   `TLS+0x90 = 0x31FC0000` en el volcado). Las liberaciones desde el servidor son entonces liberaciones entre hilos,
   que mimalloc sí admite (atómicas). Mejor aún: encolar los bytes y hacer la asignación y el `push` en el hilo del
   servidor, desde el gancho que ya tiene `child_hold`, en el «reino» del servidor. Lo mismo para
   `g_alloc(kSessionSize)` (`server.cpp:226`).
2. **Clasificar la causa** del `false` de `server_inject`: *rechazo* (no acepta, época, canal), *excepción en el
   código del juego* (asignador o cola) y *servidor parado o en pausa*. Solo el enlace caído (Steam o
   `ClientSession`) debe llevar a `LinkLost`.
   - Excepción: parar toda llamada al juego desde los hilos del mod para esa época, registrar código y dirección, y
     **no** intentar un logout por inyección. El heap ya está dañado, y reintentar solo vuelve a entrar en él.
   - Pausa: retener las Req del invitado en una cola acotada (como `g_early`) y no hacer logout.
3. **2001 con ritmo**: un intento cada ≥ 250 ms, sin repetir el `ERR` (contador y una línea cada 5 s), y nunca
   después de un fallo con excepción.
4. **Pausa**: medir si el ESC detiene `ServerRunThread`. Por ejemplo, el hueco entre «último búfer tomado» y
   «último entregado», ya disponible por `seen by the server`, más una línea al cambiar de estado. Si se confirma,
   avisar al invitado de la pausa en lugar de seguir inyectando.

## 6. Pregunta 5: el cierre del invitado `0x2045A6A`

### 6.1 Volcado (GD) [V]

- `exception tid 8700 0xC0000005 at exe+0x2045A6A`, lectura de `0x0`; `rax = rbx = rdx = rdi = 0`,
  `r8 = 0x9010031E`. T8700 es el hilo lógico del cliente (el de `P8Probe`).
- Pila: `rsp+0x48 → exe+0xE014040` (`.xtls`, cuerpo de `0xE014020`) y `rsp+0x98 → exe+0x2042C65` (en
  `0x2042A60`). **Es la ruta B** de `p8-hilo-red.md` §4.2 (`StageChartProcessor vf1 0x5DA1C0 → 0x2042A60 → 0x2045B20 (jmp .xtls) → 0x2045A20`). La duda [E] de aquel informe queda resuelta.
- `r8 = 0x9010031E` es la clave del **propio usuario 2** de esta sesión. Por el 2174 reescrito, es el dueño del
  campo en el cliente (`ClientField::vf40 0x9A5B70` busca `[campo+0x180]`).

### 6.2 Secuencia (GB) [V]

```
3570 [22:52:44.333] Session: peer timed out after 5000 ms; reconnecting      (el anfitrión ya se había cerrado)
3589 [22:52:55.539] Game error: "종료 이벤트 발생" …                          (Alt+F4)
3592 [22:52:57.518] Session2 guest: out LogoutSessionToServerReq 2001: D1 07 FF
3593 [22:52:57.534] … answered locally with 2003 D3 07 FF 04 00 00 00
3594 [22:52:57.534] … answered locally with 2002 D2 07 FF 04 00 00 00
3595 [22:52:57.556] GuestS3 crash capture (unhandled, top-level filter): exception 0xC0000005 at exe+0x2045A6A …
3661 [22:52:58.412] … minidump … cdmp_crash_20261009-225257_17428_1.dmp written (local only)
```

### 6.3 Causa [V/E]

- **[V]** No falta el usuario del anfitrión: falta el **local**. `answer_goodbye` (`guest.cpp:702-722`) inyecta
  solo 2003 + 2002. El cliente desmonta su usuario y, 22 ms después, el procesador de escenas del campo, que sigue
  cargado, busca a su dueño (ese mismo usuario) y desreferencia el nulo.
- **[V]** En A, el servidor real respondió al 2001 con `RemoveActorAck`, `ClearAllyGroupDataAck`,
  `RefreshMercenaryDataAck` y **≥ 5 285 `UnloadSyncSceneObjectAck`** antes de sus 2003/2002 (GAC desde t=213.298), y
  **no** hubo cierre (GA: 0 líneas `crash capture (unhandled`).
- **[E]** El orden nativo vacía el campo antes de borrar el usuario. La respuesta local no puede reproducirlo
  (no conoce los objetos), así que siempre dejará el campo vivo sin su dueño.
- **[V] El cuelgue de Alt+F4.** Nuestro filtro encadena al del juego, `CrimsonDesert.exe+0x4896E00` (GB:34,
  `replaced by …; ours is in front again and chains to it`). El proceso sigue vivo **5 min 40 s** después del volcado
  (las líneas `Net:` y `Local:` siguen hasta las 22:58:37). **[E]**: el informador del juego espera al
  `crashpad_handler.exe`, que no está (K2).

### 6.4 Cómo debería tratar el invitado la pérdida del anfitrión [E]

1. Con el anfitrión perdido y un campo del anfitrión cargado, **no** sintetizar 2002/2003 mientras el juego
   desmonta. Hay dos opciones:
   - en la salida (Alt+F4 o el evento de cierre), cerrar el proceso de forma limpia en cuanto el log esté
     escrito (`TerminateProcess` tras vaciar), sin pasar por el desmontaje del usuario;
   - o bien, al perder el anfitrión, volver al título por el camino nativo de «desconexión del servidor» (si
     existe en el cliente; está por localizar).
2. Contención alternativa (necesita decisión, porque cambia el comportamiento): el gancho en `0x2045A5D`
   propuesto en `p8-hilo-red.md` §4.3 (si `rax == 0`, la rama de «falso»).
3. Kit de pruebas: tras escribir el volcado, si no hay `crashpad_handler.exe`, terminar el proceso en vez de
   encadenar al filtro del juego. Es fácil y evita el cuelgue.

## 7. Pregunta 6: lista de arreglos por prioridad

### 7.1 Hecho y validado

| Pieza | Evidencia |
|---|---|
| S2: 2322 nativo para el observador (`observer_milestone_send=1`) | §2: los criterios 1–4 se cumplen en vivo; el criterio 5 sin regresión mientras duró; criterio 6 pendiente |
| Hilo de red del invitado (diagnóstico en su propio hilo, `fast_read`, búfer de 16 MB) | `Steam: RecvBufferSize = 16777216`; `net thread … max 37.3 ms … slow (>250 ms) 0 (0 in all)`; ningún corte de conexión en la carga (rtt 36–91 ms) |
| Captura de cierres del invitado con volcado | GD escrito y legible |
| S3 `observer_hold` (sin acciones nulas) | `null action 0`, `guard skips 0`; 465 liberados |

### 7.2 Arreglar ahora (riesgo bajo o medio)

| # | Arreglo | Riesgo |
|---|---|---|
| 1 | **Asignación de los búferes de inyección** con `0x47EF9B0` (heap del hilo) o en el hilo del servidor, en vez de `0x47EFA78`. Comprobar antes `[[exe+0x6C0E668]+0xB20]` (§5.5 a) | Medio: hay que verificar los bytes de `0x47EF9B0` y que nuestro hilo inicializa su heap (`0x48953C4`). Es la causa probable del cierre del anfitrión |
| 2 | `guarded_alloc`/`push_request`: registrar código y dirección de la excepción. Tras una excepción, **apagar la inyección** (sin logout por inyección ni reintentos) | Bajo |
| 3 | `inject_or_kill`: separar rechazo, excepción y enlace caído. `LinkLost` solo por el enlace | Bajo |
| 4 | `hand_logout`: ritmo (≥ 250 ms) y `ERR` agregado | Bajo |
| 5 | **S3: orden de la liberación**. Retener en `take_frame` los 2723/2991/2697/2404 de las claves retenidas hasta que salga su 2005 y luego mandarlos en orden. O liberar el 2005 antes de que `vf2` emita los Ack | Medio: toca la cola de salida; probar con `cdmp_peer` (contar en la grabación «Ack antes de 2005» = 0) |
| 6 | **Fin de las secuencias personales del usuario 2**. En `take_frame`, cuando un 2810/2111 de un secuenciador cuyo dueño es el personaje del usuario 2 (lo sabe el libro de `observer_stage`) va al anfitrión, encolar también una copia para la sesión 2. El cliente lo enruta a su M (B1), donde está la secuencia | Bajo o medio; cambia el enrutado, así que conviene el visto bueno |
| 7 | Kit de pruebas del invitado: tras el volcado, terminar el proceso si no hay `crashpad_handler.exe` | Bajo |

### 7.3 Necesita diseño o decisión

| Tema | Por qué |
|---|---|
| **S1/escenas del dueño en el invitado** (235 NPC salen en `local_res8`; 15 en A-pose) | El nodo de escena del NPC lo da el 2661. Hacen falta la réplica en X **con** sus nodos, el momento de la carga y la autoridad (dictamen §7). Antes, medir dónde se escribe `local_res8` |
| **Réplicas suprimidas por el servidor** para los NPC de escenas del dueño (0 × 2991/2697/2404) | Hay que localizar el filtro del servidor (visibilidad de escena personal) antes de decidir si se replica |
| **Medida por NPC** («bucle» o «a medias») | Una sonda que, para N claves elegidas, registre en los dos PC cada segundo la acción actual de las capas 0/1 (hash y tiempo de reproducción) |
| **Pausa del anfitrión** | Confirmar si el ESC detiene el servidor; si es así, avisar al invitado y retener sus Req sin logout |
| **Salida del invitado sin anfitrión** | Cerrar el proceso o volver al título por el camino nativo, en vez del 2002/2003 local; o la contención de `0x2045A5D` |
| **Solicitudes de mundo del invitado** (2556 de NPC, 2828 de objetos tipo 7, 3028 rechazados) | Medir si el servidor las aplica; si lo hace, filtrar las de actores que no controla el usuario 2 |
| **Latencia de bajada en la carga** (~7,6 s detrás de la ráfaga) | Priorizar en el túnel los mensajes de control de carga (2144/3066/2322/2762) frente a 2665/2005, o subir el ritmo |
| Criterio 6 de S2 | Salida y reentrada, reconexión y cambio de campo con Shadow, tras los puntos 1–4 de §7.2 |

## 8. Notas para la próxima sesión

- Copiar `%LOCALAPPDATA%\CrashDumps\CrimsonDesert.exe*.18568.dmp` a `backups` antes de que se roten.
- Con el punto 2 de §7.2 en el anfitrión, cualquier repetición del fallo dirá si es el asignador o la cola.
- Para S1, la sonda de `local_res8` del invitado ya existe (`guest_stage_diag`). Falta saber **quién escribe** el
  nodo del NPC (`[[npc+0x68]+0x58]+0x90`) en el anfitrión cuando se procesa el 2661.
