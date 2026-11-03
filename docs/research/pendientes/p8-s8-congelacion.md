# P8 sesión 8: congelación mutua y bloqueo de 8,8 s del invitado

10 de octubre de 2026, rama `opcion-d` (HEAD `16bf71f`). Build **1.0.0.2976**, base `0x140000000`, RVA. Solo lectura:
logs y capturas de `C:\Emulador CD\backups\p8-sesion8-host\` y `…\p8-sesion8-shadow\`, código de `src/`. No se tocó
`src`, no se lanzó el juego.

- **[V]** comprobado (log, captura, código). **[E]** deducción fuerte. **[H]** hipótesis.

## 0. Reloj común

- `t` de las capturas: el log da `qpc … (~hora) capture t=…`. Anfitrión: `t0 = 10:32:48,3`; invitado: `t0 = 09:35:43,5`
  (no la hora del nombre del fichero). **[V]**
- `ts` de los Move: `t_cap·1000 − ts` es constante en cada PC (anfitrión 8 700–8 770 ms, invitado −166 060…−165 940
  ms) para sus Req **y** para los Ack del otro: los dos relojes de juego van en la misma base, con ~30 ms de latencia.
  `t_anfitrión − t_invitado = 175,2 s` → hora del invitado = hora del anfitrión − 1 h exacta. **[V]**
- No hay muestras "futuras" ni "viejas"; durante los diálogos el reloj del anfitrión tampoco se para (lag de sus Req
  8 699–8 750 ms en t = 784–886). **[V]**

## 1. Congelación mutua

### 1.1 La cadena de movimiento funcionó en los dos sentidos [V]

| Minuto (log) | Anfitrión: Ack 2697 de 0xA0100333 / `set pos` 0xA / `update pos` 0xA | Invitado: Ack de 0xA0100001 / `set pos` 0xA |
|---|---|---|
| …10:44:04 | 581 / 655 / 3 107 | |
| …10:45:04 (09:45:51 inv.) | 922 / 978 / 1 116 | 251 / 292 |
| …10:46:04 (09:46:51) | 26 / 62 / 1 100 | 171 / 219 |
| …10:47:04 (09:47:51) | 950 / 981 / 2 274 | 587 / 651 |
| …10:48:04 (09:48:51) | 833 / 896 / 1 802 | 536 / 612 |

- Cada Ack del otro llegó a `setInterpolatePositionData` (`set pos` ≈ Ack + StartAction). `update pos` siguió
  corriendo (cuenta ≈ segundos con interpolación activa). 0xA nunca se sustituye (por diseño): `remote_interp` no
  toca al personaje del otro. `older` 0xA: +11 y +7 por minuto (normal, StartAction).
- El último Move del invitado que recibe el anfitrión (t ≈ 900, 10:47:3x) no es una pérdida: el invitado **dejó de
  moverse** a las 09:47:29 (sus Req acaban ahí, posición (−10640,54, 613,48, −3854,53) = la del HUD). El último Ack
  del anfitrión llega al invitado a las 09:48:31,8 (ts 934309), dentro del bloqueo (§2).
- **Sonda del invitado #1 (09:48:15,7–21,8)**: posición en memoria (`TS+0xB4`) del personaje del anfitrión =
  la del HUD ((−10639,56, 613,96, −3836,84)), con acciones que cambian. En ese momento no estaba congelado.
- **Sonda del anfitrión #3 (10:44:12–18)**: el invitado sube (método 0→5→0) y sigue moviéndose después. El wall-up
  **no** deja al remoto congelado.

### 1.2 Las cuatro marcas caen dentro de los diálogos del anfitrión [V]

Interacciones del anfitrión (`InteractionPreemptionStepReq`/`InteractionDoStepReq` tipo `0x0F4321`, con
`SetQuestDialogAck`, cámara a 1,9–2,2 m "behind", `ChangeDrowningState` 0/1 al entrar/salir,
`SaveToFileCurrentPlayerAck` cada 10 s):

| # | Inicio–fin (anfitrión) | NPC | Secuenciador (`SetAvoidByCutScene`) | Marcas dentro |
|---|---|---|---|---|
| 1 | 10:45:52,9–10:46:22,5 (29,7 s) | 0xB010013F | 0x197 | invitado 09:46:06,7 (+13,8 s); anfitrión 10:46:20,3 (+27,4 s) |
| 2 | 10:47:03,7–10:47:33,5 (29,8 s) | 0xB0100143 | 0x19A | anfitrión 10:47:06,0 (+2,3 s); invitado 09:47:13,5 (+9,8 s) |
| 3–5 | 10:47:49–56, 10:48:04–10, 10:48:17–24 | 0xB0100143, 0xB0101108, 0xB010110A | 0x19A, 0x1CF, 0x1D0 | — |

4 de 4 marcas dentro de 60 s de diálogos en ~12 min de sesión. Ninguna marca coincide con: el wall-up (10:44:12 /
09:48:16), el autoguardado rechazado (10:44:36, 90 s antes), montar (no hubo: `HorseFollow made 0`), cambios de
método del remoto, ni el 2322/`X+0x231` (en el invitado `X+0x231 = 2` fijo toda la sesión).

### 1.3 Qué ve cada lado durante un diálogo

- **Invitado mirando al anfitrión [V datos / E visual]:** el anfitrión está **quieto de verdad** (0 Move). Recibe
  `StartActionAck` (acción de capa 0 `0x3E005FA1`), `ChangeAIStateAck`, `SetAvoidByCutScene(0x197,1→0)` y los 2981
  del secuenciador del diálogo. `guest_seq_route` los enruta al gestor X del Kliff del anfitrión, pero X tiene
  `instances (+0x174) 0` (los 0x197/0x19A se crearon en la instantánea de 09:36:37 y ya no existen): el escenario
  del diálogo no se reproduce. Resultado esperado: el anfitrión plantado en la acción de diálogo sin su escena,
  "congelado". El HUD también estaba quieto (el anfitrión no se movía).
- **Anfitrión mirando al invitado [V datos / H mecanismo]:** el invitado sí se movió durante el diálogo #2 (13 m entre
  10:47:13 y 10:47:33) y sus Ack entraron en el búfer del cliente del anfitrión. La congelación está **aguas abajo de
  la interpolación** (render/animación del remoto con el cliente local en modo diálogo) o es percepción desde la
  cámara de diálogo. No hay muestra de la sonda del anfitrión en esa ventana (solo registra episodios de
  montar/escalar) para decidirlo.

### 1.4 Conclusión

- **Causa raíz [E, confianza media]:** no es red, ni túnel, ni reloj, ni `remote_interp`/`remote_motion`: todo eso
  está medido sano. La "congelación mutua" son los diálogos del anfitrión con NPC (tipo `0x0F4321`, ~30 s). El
  invitado ve al anfitrión plantado porque su escena de diálogo (0x197/0x19A) no existe en su cliente. El anfitrión ve
  quieto al invitado mientras él está en diálogo, aunque las posiciones le llegan.
- **Módulos relacionados:** `observer_seq_copy` (anfitrión) y `guest_seq_route` (invitado), que mandan y enrutan
  los 2981/2810 de esos secuenciadores al X del Kliff. Ninguna clave del movimiento.
- **Sin cerrar [H]:** por qué el cliente del anfitrión en diálogo no muestra el movimiento del invitado, y si los dos
  diálogos de 29,7–29,8 s (fin pedido por el servidor: ráfaga `UpdateStageState`/`AddStageRandomRate` 0,6 s antes
  del `ChangeDrowningState 1`) son un tiempo límite.

## 2. Bloqueo de 8,8 s del invitado y caída del túnel

### 2.1 Qué pasó [V]

- **09:48:28,53** el hilo de red (T2660) formatea `Net: connected …` y entra en `log::write`. La siguiente línea suya
  (`Local: pos …`, que solo formatea, `mod.cpp:177`) sale a las **09:48:37,365**: `5 s report 8833,2 ms`.
- **09:48:29,03** el hilo de diagnóstico (T4840) formatea una línea `GuestS3` y no escribe más hasta 37,365: `S3 8338,0
  ms` (37,37 − 29,03 = 8,34 s exactos).
- **09:48:32,02** el hilo de **presentación** (T6796, `hud.cpp:171`, `Overlay diag`) entra en el log. La siguiente
  `Overlay diag` (37,383) cuenta **1 fotograma** desde la anterior (46 655 → 46 656): el juego no pinta durante 5,3 s.
- Todo se libera en el **mismo milisegundo** (37,365): cientos de líneas en cola.
- Lo mismo, más corto, a las 09:49:52 (5,0 s, `status line 4521,9 ms`, 1 fotograma) y 09:50:37 (4,9 s, 3 fotogramas).
  El anfitrión no tiene ninguno (máximo, una vuelta de 560 ms a las 10:35:30).

### 2.2 El candado compartido [V código]

`src/mod/log.cpp:70–73`: `std::lock_guard lock(g_mutex); … WriteFile(...)`. Es lo único que se hace con el candado.
El "5 s report" (`mod.cpp:416–451`) y el grupo "S3" (`guest_diag_worker.cpp:32–37`: `guest_s3_tick`,
`guest_stage_diag_tick`, `guest_scene_diag_tick`, `guest_seq_route_tick`, `load_trip_tick`) solo formatean y escriben
log. Un `WriteFile` de `cdmp.log` que tardó ~8,8 s retuvo `g_mutex`: hilo de red, hilo de diagnóstico y **hilo de
presentación** esperaron detrás. **[H]** por qué tardó: E/S del disco de Shadow (el sniffer escribe ~0,3 MB/s en
`cdmp_packets_*.bin/.log`, 310 MB en 16 min; posible estrangulamiento de escritura de la caché o un análisis del
antivirus sobre el log).

### 2.3 Consecuencia [V]

Hilo de red parado 8,8 s → sin recepción ni latidos del túnel → el anfitrión ve el túnel `not connected` a las
10:48:35,5 (backlog 347 KB, 1 MB a las 10:48:40) → 09:48:42 el invitado declara el túnel caído → 10:48:41 el
anfitrión: "the guest's connection restarted". El bloqueo **no** causó la congelación de §1 (va 2 min después).

## 3. Arreglos propuestos

1. **Logger asíncrono (prioridad alta, cierra §2).** `log::write` formatea y encola en un anillo acotado (candado
   solo para el `push`, sin E/S); un hilo escritor propio hace `WriteFile` por lotes. Si la cola está llena, descarta
   y cuenta (`log lines lost`), nunca bloquea. `emergency()` sin cambios. Prueba unitaria: un escritor bloqueado 10 s no
   retiene a los productores más de 1 ms.
2. **Ningún log síncrono en hilos del juego:** sacar `Overlay diag` del hilo de presentación (pasarlo al hilo de red
   como el resto del HUD). Con el punto 1 basta, pero conviene igual.
3. **Instrumentar la congelación (antes de tocar la lógica).** En `dismount_probe`, que ya muestrea cada 30 ms, un
   detector de divergencia: si `|pos TS − pos del canal del mod| > 1,5 m` durante más de 1 s, una línea por segundo
   con método, acciones de capa, modo de control (`0x88E8E0`), `count`, `ts[0]`, `ts[último]`, `now − delay` del búfer
   `mc+0x3D0` y si `update pos` pasó por ese `mc` en el último segundo. En los dos PCs.
4. **Diálogo del anfitrión en el invitado:** que los 2981 de un secuenciador sin instancia en X no se enruten en
   silencio (contar `routed, no instance`). Decidir si se recopia el 2661 cuando falta la instancia o si se acepta
   el anfitrión "plantado" en diálogo (comportamiento de juego).

## 4. Qué apagar si hay que probar ya

- **Contra el bloqueo (invitado, Shadow):** `record` del sniffer a 0 (mantener contadores) y bajar el volumen de log:
  `guest_scene_diag=0`, `guest_s3_diag=0`, sondas `P8Probe` y `s1_diag` a 0. No toca el juego; reduce la E/S que
  retiene `g_mutex`.
- **Para aislar la congelación:** prueba A/B corta. (a) El anfitrión **no** habla con NPC 3 min mientras los dos se
  mueven y marcan. (b) Diálogo de 30 s del anfitrión con el invitado caminando delante, primero como está y luego con
  `observer_seq_copy=0` (anfitrión) y `guest_seq_route=0` (invitado). `remote_interp` y `remote_motion` pueden quedarse
  a 1: los datos no los implican. Si se quiere descartarlos del todo, `remote_interp=probe`.

## 5. Respuestas puntuales

- **Resúmenes `RemoteInterp`.**
  - **Anfitrión:** hilos `SEVERAL` (`set` en el hilo principal 41456; `update` en ≥ 8 hilos de trabajo); solo 2
    controladores en toda la sesión (0xA0100333 y 0xB0100334). 0xB vaciados 119 frente a 309 con el umbral de 1000;
    retardos 0xB de 1 150 a 1 800 ms.
  - **Invitado:** 1 121 controladores; 0xB vaciados 13 201 frente a 80 438 con el de 1000; retardo aplicado sobre
    todo entre 1 000 y 1 600 ms.
  - 0xA: siempre el retardo del juego (200 ms); `unresolved` y `no slot` a 0 en `set`/`update pos`.
- **Wall-up (2851/2893).** Solo 2 `UpdateWallUpDataAck` por subida (estados 1 y 2). No hay relación con la
  congelación.
- **2322 / `X+0x231`.** Sin cambios durante las marcas.
- **Autoguardado 10:44:36.** Rechazado por el guardián, sin efecto medible en el movimiento.
- **Montar.** No hubo.
- **Cambio de método.** Solo el wall-up de 10:44:12.

Scripts de análisis en el scratchpad de la sesión (no se conservan): `mv.py`, `allkey.py`, `bins.py`, `perwin.py`,
`ops.py`.
