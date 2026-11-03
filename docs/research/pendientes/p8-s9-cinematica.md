# P8 · Sesión 9, prueba 1: la cinemática del campamento liberado saca al invitado al título

Build 1.0.0.2976, rama `opcion-d` (`c4995c1`). Solo lectura: no se tocó `src`, no se lanzó el juego. Marcas: **[V]**
visto en log o captura, **[E]** inferencia fuerte, **[H]** hipótesis sin comprobar.

Fuentes:
- Invitado: `C:\Emulador CD\backups\p8-sesion9-shadow\cdmp_packets_20261010-151331\cdmp.log` y la captura
  `cdmp_packets_20261010-151331.log` (t = s desde el primer paquete).
- Anfitrión: `C:\Emulador CD\backups\p8-sesion9-host\cdmp_fase1.log` y `cdmp_packets_20261010-161029.log`.
- Relojes: hora del invitado = anfitrión − 1 h (más ~1,4 s de desfase). En las capturas, el mismo 2981 da
  **t(anfitrión) ≈ t(invitado) + 194,58 s**.

## 0. Resumen

1. **Causa del título [V cadena, E en el último eslabón]:** la escena del campamento liberado es una
   **recarga del campo en el sitio**. Solo el dueño (anfitrión) la tiene preparada en el servidor. La opción A
   la copió al invitado:
   - el anfitrión copia el 2661 (`observer_seq_copy`);
   - el invitado lo crea en X, el gestor del Kliff del anfitrión (`guest_seq_route`);
   - el 2981 nativo (ToNear) de la rama 2 llega a X.

   El cliente del invitado ejecuta la rama como si fuera suya: arranca **su** máquina de cambio de campo y,
   2 s después, manda **su** `GameLoadingStartReq` 2437. El servidor no tiene ningún viaje pendiente para el
   usuario 2. Le responde con `LogoutCharacterToFieldServerAck` 2679, que saca su personaje del campo. 37 ms
   después el cliente manda la 2001 y vuelve al título.
2. **No es un cierre ni un "Game error" [V]:** en el log del invitado no hay ninguna línea `Game error` entre
   15:18:57 y el Alt+F4 (15:28:16, `종료 이벤트 발생`). El error que vio el usuario es seguramente un aviso de la
   interfaz que no pasa por el gancho de errores [H]. En el anfitrión, los únicos `Game error` son
   `[SaveData] Backup 생성 실패` (16:25:53 y 16:26:03): el guardián de guardado rechazó el autoguardado de la
   misión, como está previsto. No tienen relación.
3. **`quest_per_user` y `kill_loot_per_user` no son la causa [V]:**
   - la última muerte se acreditó al usuario 2 (`DeadActor goes to character 0xA0100377`, t=907,924);
   - pero el indicador del campamento se completó en el **anfitrión**: `CompleteQuestGaugeAck` 2738 a `A0100001`
     (t=907,925), y la escena 0x213 se preparó para su Kliff;
   - lo que llevó la escena al invitado fue la copia de escenas (opción A), no los embudos.
4. **Reentrada [V]:** es la política actual del invitado, `g_had_remote` (`guest.cpp:285`, `:657-664`):
   - tras la 2001, el cliente abre una conexión nueva; el mod la corta ("the client reconnected") y la manda al
     **servidor local**;
   - la partida local recibe su 2174 local y se queda en el estado de carga `0x0C` sin mandar nunca la 2549
     (97 s, hasta el Alt+F4);
   - el anfitrión **no** se quedó con el usuario 2 viejo: barrera limpia a las 16:26:14.243,
     `reconnect epoch=2 ready` a las 16:26:14.760 y después `session 2 ready | user 2 no`.

## 1. Secuencia exacta

| t invitado | t anfitrión | Lado | Hecho | Marca |
|---|---|---|---|---|
| 713,2 | 907,77 | ambos | 2524/2981 periódicos de 0x164/0x212 (sin relación) | V |
| — | 907,924 | servidor | Req 2380 del usuario 2 → DeadActor al Kliff del usuario 2 (`kill_loot_per_user`) | V |
| 713,337 | 907,925 | ambos | `CompleteQuestGaugeAck` 2738 a `A0100001` (solo anfitrión); `UpdateStageListAck` 2724: etapa `0xF4568` en estado 02 (también le llega al invitado) | V |
| — | 907,939 | anfitrión | `SaveToFileCurrentPlayerAck`; el guardián rechaza la copia de seguridad (`Game error` de SaveData) | V |
| 722,169 | 916,757 | ambos | 2981 `0x213` estado 0 y **2661 `0x213`** (recurso `0x8CD771B1`, etapa `0xF4568`, un enlace: `0x20000002→A0100001`, tipo 5) | V |
| 722,174 | 916,758 | invitado | `GuestSeqRoute`: 2661 `0x213` **ROUTED** a X `0x37E00FF7500` → `0xA9CA40` "created" | V |
| 723,360 | 917,946 | ambos | 2981 `0x213` estado 1 → X | V |
| 725,8–726,0 | 920,36 | ambos | 2661 `0x214` (10 NPC) y `0x215` copiados; 2335 de spawns del campamento | V |
| — | 922,954 | anfitrión | su cliente manda `StageBranchStateDelayEndReq` 2105 de `0x213` (vida nativa de M) | V |
| **728,380** | **922,961** | ambos | **2981 `0x213` estado 2** (ToNear, "via finder"); en el invitado se enruta a X (qpc 1209,2335) | V |
| **728,392** | — | invitado | **`[P+0x15C]` 0x18 → 0x01 → 0x02**, 1,1 ms después y en el mismo hilo T15980 | V |
| 730,1 | 924,6 | ambos | cámara de la cinemática: fov 40 y cámara a 20–30 m del jugador, en los dos clientes | V |
| 730,405–730,436 | — | invitado | 0x02 → 0x04 → 0x05 → 0x06; manda `ResetImportantGimmickReq` 2749 ×13 y **`GameLoadingStartReq` 2437** | V |
| — | 925,088–925,090 | servidor | quita al usuario 2 del campo: `RemoveActorAck` de su Kliff y su mercenario, con motivo `0x0B` | V |
| — | 925,102 | anfitrión | **su** 2749 + 2437, 2,14 s después de su 2981 (el invitado tardó 2,06 s) | V |
| — | 925,107–925,129 | anfitrión | recarga del dueño: `SaveToFile`, `LoadingFieldDataAck` 2643, 2810 ×6, `MoveFieldCompletePostAck` 3066, `ChangeGameLevelStateAck` 2344, secuenciadores nuevos (`0x23B`, `0x376–0x379`) y 2322 | V |
| 730,562 | — | invitado | **`LogoutCharacterToFieldServerAck` 2679** `77 03 10 A0 0B …` (su Kliff, motivo `0x0B`), 2810 ×6 y 5 700 `UnloadSyncSceneObjectAck` | V |
| **730,599** | — | invitado | **`LogoutSessionToServerReq` 2001** (15:26:09.827); 0x06 → 0x18 en 730,600 | V |
| — | 16:26:11.227 | anfitrión | C4 `logout begin … trigger=bye`; "user 2 left after its GameLoadingStartReq … waiting up to 3000 ms for the server's MoveFieldAck 2174" | V |
| — | 16:26:14.216 | anfitrión | no llega ningún 2174 en 3 s; la 2001 pasa al servidor | V |
| — | 16:26:14.243 / .760 | anfitrión | barrera a/b/c/d **pass**, `logout complete epoch=1`, `reconnect epoch=2 ready` | V |
| 733,648 | — | invitado | 2003/2002/1004; 0x18 → 0x01 → 0x02 → 0x03 → **0x13** (título) | V |
| 746,57 | — | invitado | conexión nueva → mod: `ERR … the client reconnected; restart the game…`, `connection (epoch 0x3) -> local` | V |
| 749,0–750,7 | — | invitado (local) | 3093 → 2468 → 2174 local (dueño `0x90100000`, actor `A0100001`) → `SetMoveFieldAck`; estado 0x04 → **0x0C**; ninguna 2549; `Local player: loading (actor parked at 0,1000,0)` | V |
| 15:28:16 | — | invitado | Alt+F4 (`종료 이벤트 발생`) | V |

La divergencia de 8,57 m (15:26:09.266) es un síntoma, no la causa [V]. La cinemática teletransportó al Kliff del
anfitrión a (−10694,67, −4200,68): esa es la posición del canal y la `Local:` del anfitrión a las 16:26:10.949.
El invitado ya estaba cargando y no lo movió.

## 2. Por qué la rama 2 recarga el cliente del invitado

- **En un jugador, la liberación es una recarga en el sitio [V, captura del anfitrión]:**
  1. rama 2;
  2. ~2 s de fundido;
  3. `ResetImportantGimmickReq` + `GameLoadingStartReq`;
  4. el servidor contesta al dueño con `LoadingFieldDataAck` + `ChangeGameLevelStateAck`: el campamento cambia
     de estado de nivel.
- **El disparador es el 2981 de la rama 2, no el tick de X [E fuerte]:**
  - El bucle principal `0xAD2380` procesa en orden los Ack (`0xAD246A`), la máquina de campo `0x7797F0`
    (`0xAD2748`) y el tick `0x8B6060` (`0xAD277F`), donde está `guest_seq_play` (`p8-impl-escenas-play.md` §2.2).
  - La transición se registró 1,1 ms después del enrutado del 2981, en el mismo hilo y por tanto en el mismo
    fotograma, antes de que corriera el tick.
  - Ninguno de los otros Ack de ese lote (2586, 2506) tiene que ver con cargas.
- **Sin la opción A esto no pasaría [E]:**
  - el 2981 (ToNear) ya llegaba antes, pero el cliente lo daba a M (vf48), donde no existe `0x213`, y no hacía
    nada;
  - ahora `0x213` existe en X, porque el 2661 se copió (anfitrión) y se enrutó (invitado);
  - la rama ejecuta su acción, y esa acción no mira de quién es X: arranca la máquina global
    `P = [g_WorldSystem+0x60]`, que es la del usuario local.
- **Por qué el servidor saca al usuario 2 [V comportamiento, H mecanismo]:**
  - no tiene ningún viaje ni recarga preparados para él, porque el `LoadingFieldData` y el `ChangeGameLevelState`
    son solo para el dueño;
  - su 2437 lo saca del campo (2679 y `RemoveActorAck` con motivo `0x0B`; el logout normal del anfitrión lleva
    `0x07`) y no le sigue ningún 2174 en 3 s;
  - el cliente, con su personaje fuera del campo, vuelve al título (2001 a los 37 ms) [E].
- **La cámara del invitado también entró en la cinemática** (fov 40) [V]. El tick de la fase 1 reproduce las
  instancias de X con su cámara. Ver el riesgo R3.

## 3. Por qué la reentrada se quedó en "conectándose"

- **Por diseño [V]** (`p8-preparacion.md` §1.1, P8-4; `guest.cpp` `decide`/`guest_send`): tras una conexión remota,
  cualquier conexión nueva del mismo proceso es **local** (`g_had_remote`). El procedimiento P8-4 dice: "cierra desde
  el título sin cargar ninguna partida".
- **El anfitrión estaba listo [V]:** `reconnect epoch=2 ready` y `user 2 no`. La época, la barrera y la sesión 2
  estaban bien. Bloqueó el invitado, no el anfitrión. Aunque el invitado lo permitiera, haría falta un enlace de
  transporte más nuevo que el del READY (`p8-preparacion.md`, trampa `ready_link_`).
- **El bloqueo en `0x0C` de la partida local [V síntoma, H causa]:**
  - el servidor local contestó (2174 y 2277), pero el cliente nunca salió de `0x0C`;
  - candidatos [H]:
    - los ganchos del invitado siguen puestos en modo local (`guest_schedule_load`, `seq_actor_restore`,
      `remote_interp`, `ui_pause` y la tabla de `guest_seq_play`, que aún guarda el X viejo `0x37E00FF7500` bajo la
      clave `0xA0100001`, la misma que ahora tiene el Kliff local);
    - restos del estado de cliente de la sesión remota;
  - esta partida no permite comprobarlo.

## 4. Arreglo propuesto

| # | Dónde | Qué | Marca |
|---|---|---|---|
| A0 | ini, inmediato | Mientras no esté A1: no liberar campamentos ni avanzar misiones con cinemática con el invitado dentro, o bien `guest_seq_route=0` (invitado) / `observer_seq_copy=0` (anfitrión). Se pierden los tenderos y escenas de NPC de la opción A | E |
| **A1** | invitado, raíz | **X nunca toca el estado global del cliente.** `guest_seq_route` pone un `thread_local` "contexto X" alrededor de los manejadores 2661/2981/2524/2810/2322 que enruta a X; `guest_seq_play`, alrededor de su tick de X. Se localiza la función que pide la recarga (la que escribe `[P+0x15C]=1` desde la acción de la rama; candidata [H] `0x63F200`, que el bloque `_chapterTransition` llama, `p8-impl-escenas-play.md` §13.1) y se engancha su entrada en `.rsrc`: con contexto X, retorno sin efecto y línea WRN. Lo mismo, como segunda fase, para la toma de cámara | **Resuelto en §7:** el escritor es `0x778AB0`, y el punto de veto es V1 `0x5F2A3A` (función de etapa `Loading`); no es `0x63F200` |
| A1-RE | 1 PC | Para localizar el escritor sin Shadow: en el anfitrión, liberar un campamento o repetir la etapa `0xF4568` con una sonda de escritura en `P+0x15C` (gancho en `0x7798D0`, ampliado con la pila de llamada) o leer en Ghidra los llamadores de `0x7797F0`/`0x63F200` | — |
| A2 | anfitrión, red de seguridad | **Sonda** (solo log): un `GameLoadingStartReq` 2437 del usuario 2 sin viaje ni reaparición pendiente (sin 2174/2643/2993 suyos) es la firma de este fallo; WRN con el contexto. **No** se descarta: el cliente del invitado espera en `0x06` una respuesta, y sin ella se quedaría en la carga [E] | E |
| A3 | invitado | Plan B de A1 si no se encuentra el escritor: no enrutar a X los 2981/2524/2111 de secuenciadores cuya etapa (u32 del 2661, p.ej. `0xF4568`) acabe de cambiar de estado en un 2724 del anfitrión; esos irían a M, sin efecto, como antes. Es poco preciso: `0x165`, `0x166` y `0x1C9` también tienen etapas que aparecen en el 2724 | H |
| B1 | invitado, reentrada | G4 de `p8-preparacion.md`: aviso en el HUD tras `end_remote`. Además, si después de `g_had_remote` el cliente manda su 3093 por la conexión local, terminar con un ERR claro, igual que `EndProcess` en `p8-salida-invitado.md`, en vez de dejar una carga local que no termina | E |
| B2 | ambos, después | G5: reconectar sin reiniciar (control "época nueva" y un enlace de transporte nuevo). El lado del anfitrión ya está listo (C4r) | E |

## 5. Riesgos

- **R1 (desincronización del estado de nivel):** aunque A1 impida la recarga del invitado, el servidor cambió el
  estado de nivel del campamento solo para el dueño (`ChangeGameLevelStateAck` 2344). El invitado se quedaría con el
  campamento "viejo" hasta su próximo viaje [H]. Hay que medir qué recibe el usuario 2 tras la recarga del anfitrión.
- **R2 (el anfitrión también recarga):** sus secuenciadores se rehacen (`0x23B`, `0x376–0x379`, 2322 nuevo). Con
  A1, `observer_seq_copy` tiene que copiar ese segundo lote y su 2322 en el mismo viaje [E]. Su `[clear]` actual
  solo se dispara al salir el usuario 2.
- **R3 (cámara):** la fase 1 ya cambia la cámara del invitado a la cinemática del anfitrión [V]. Es justo el
  criterio de aborto de `p8-impl-escenas-play.md` §10.
- **R4:** A1 anula una acción del juego por contexto. Si el `thread_local` se queda puesto fuera del manejador,
  se perdería una recarga legítima del propio invitado (muerte o viaje). Hay que usar RAII y contarlo.
- **R5:** no queda probado que otras escenas del anfitrión (diálogos de misión, cambios de capítulo) no tengan
  acciones globales distintas de la recarga: UI, tiempo, teletransporte del jugador local [H]. A1 debería cubrir
  la familia entera, no solo `P+0x15C`.

## 6. Qué medir en la próxima prueba

- Invitado: `GuestDiag loading transition` junto a una línea nueva con la pila o el llamador de la escritura de
  `P+0x15C`. Además: el contador de A1 (`suppressed in X context`), cero 2437 sin 2174, cero 2679.
- Anfitrión: la sonda A2 a cero, `ObserverSeqCopy` del lote posterior a la recarga y `ChangeGameLevelStateAck` hacia
  el usuario 2.

## 7. A1-RE estático: quién pide la recarga y dónde vetarla (10-10, sin juego)

Herramientas:
- Ghidra: servidor de consultas de solo lectura (`D:\RE\CrimsonDesert_2976\scripts`).
- capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (1.0.0.2976).
- Scripts en el scratchpad de la sesión: `re\scan15c.py`, `rel32.py`, `cg.py`, `scfuncs.py`, `vt.py`, `rtf.py`.

Nada escrito en `src`. Marca nueva: **[V est]** = leído en el binario.

### 7.1 El escritor de la petición es `0x778AB0` [V est]

- **Barrido de escrituras de `+0x15C`:**
  - Formas buscadas: `mov byte [r+0x15C], imm8` y `mov [r+0x15C], r8`.
  - Secciones: `.rsrc`, `.xtls`, `.xtext` y `.text`.
  - El único `= 1` de la clase P está en **`0x778B99`** (`41 C6 8x 5C 01 00 00 01`), dentro de **`0x778AB0`**
    (RUNTIME_FUNCTION `0x778AB0–0x779164`).
  - El resto de escrituras de P son los pasos internos de la máquina `0x7797F0` (2–0x18) y `0x77C644`/`0x77D494`.
- **La máquina no sale sola de 0x18:** su `switch` de `0x7797F0` solo trata los estados 1..0x17; en `0x18` no hace
  nada. La transición 0x18 → 0x01 solo puede venir de fuera, es decir, de `0x778AB0`.
- **Qué hace `0x778AB0(P, req)`:** arranca una carga. Devuelve `void`.
  1. Copia a P la petición `req` (la monta `0x778320` y la libera `0x49E480`):
     - tipo: `+0x1DD = req+0x18`;
     - `+0x1DE = req+0x19`;
     - `+0x15D = (req+0xC6 == 0)`;
     - `+0x16D = req+0xC3`, …
  2. Escribe **`[P+0x15C] = 1`**.
  3. P es `[[exe+0x6D691E0]+0x10]`.
- **Encaja con la secuencia del invitado:** con `+0x15D=1` y `+0x16D=0` sale exactamente lo que se vio:
  1 → 2 (0 s), 2 s en 2 (fundido, `0x77A220`) → 4 → 5 → 6.
- **Llamadores directos:** 20, sacados con un barrido `E8`/`E9` rel32 de todo el PE, no solo de lo que analizó
  Ghidra.
  - En `.rsrc` (18): `0x49A0B0`, `0x49D5F0`, `0x4FA8A0`, `0x5CA8A0`, `0x5CDDE0` (`ChangePlayerbleCharacter`),
    **`0x5F2A00`**, `0x631020`, `0x6315C0`, `0x655110`, `0x8C8890`, `0x8C91D0`, `0x8F7F80`, `0x8F8090`, `0x96BE50`,
    `0x9C8290`, `0xA38B00` (`ApplyFieldTime`), **`0xA99AD0`** (vf7 del gestor, "Reload Stage - %#") y `0xA9A860`.
  - En `.xtls` (2): `0x9928D30` (por el thunk `0x8C9000`) y `0x993FFE0` (por el thunk `0x8CB370`).

### 7.2 El camino desde la rama 2 pasa por la función de etapa `Loading` [V est; E en el disparo]

**Cadena de llamadas:**
1. `StageBranchStateAck::vf2 0xC073A0`.
2. `vf48` en `0xC07746`; aquí `guest_seq_route` cambia RAX a X.
3. `0xA9E100(X, ctx, 4, seq, estado, …)`.
4. `0x5E1BB0` = `jmp .xtls 0x8F1FAA0`.
5. `0x2044390`: el secuenciador cambia de rama.

**Cómo se ejecutan las funciones de la rama:**
- Al entrar en la rama se ejecuta su *stage chart*.
- Cada `StageChart_Function_*` tiene su `Execute` en la ranura 8: un trampolín de 3 instrucciones,
  `rax=[rdx]; rcx=[rax+0x58]; jmp [vt(rcx)+off]`.
- Ese `rcx` es el procesador de la etapa:
  - en el cliente, `pa::ClientSequencerStage_StageChartProcessor` (vtable `0x558C080`);
  - en el servidor, vtable `0x5B1FE10`;
  - la clase común (`0x590DF78`) deja esas ranuras vacías.

**`StageChart_Function_Loading` llama a `0x778AB0`:**
- vtable `0x5985C18`; `Execute` `0x236A120` → `+0x218`, que en el procesador del cliente es **`0x5F2A00`**
  ("StageChart Loading Func - %#").
- Según la rama, llega a `0x778AB0` de tres formas:
  - directamente (`0x5F3377`);
  - por la vf7 del gestor: `[[this+8]]+0x38` = `0xA99AD0` → `0x778AB0`;
  - por `0x631020` → `0x778AB0`.

**Disposición del procesador [V est]:**
- `this+8` = **el gestor G** (`ClientSequencerStageManager`). Pruebas:
  - `0x5F2A00` pasa `[this+8]` a `0x1F6C540`, igual que hace la vf7 con su `this`;
  - `0x5F2A00` llama a `[[this+8]]+0x38`, que es la vf7.
- `this+0x10` = la etapa o nodo:
  - `+0x48` → info de la etapa, con el nombre en `+0x1B8`;
  - `+0x30` = clave;
  - `+0x280` = usuario.

**El juego ya prevé "etapa de otro usuario → no recargar", pero solo en un camino:** en la rama del registro de
guardado pendiente igual, si `[etapa+0x280]` ≠ 0 y ≠ usuario local, no hay carga (`this+0x29B = 1`). Nuestro veto
lo generaliza.

**Descartados:**
- `0xA9A860`/`0x655110`: mandan `ReloadStageStartReq` 2283 (`0xC04D20` → `PostPacket` op `0x8EB`). No hay ningún
  2283 en las capturas.
- La vf7 por su único llamador directo, `0x2140 StageBranchStateLoadingAck` (`0xC09F90` → `0xA9F560`):
  - no hay ningún 2140 en las capturas;
  - además, ese camino usa el jugador local, no X.
- **`0x63F200` no es el escritor:**
  - solo lo llaman la rama de capítulo de la vf7 (`param_7/8 ≠ −1`) y el bloque `_chapterTransition`;
  - es un efecto de fundido o de capítulo, no la petición de carga;
  - **se corrige la candidata [H] de §4**.

**Por qué es `Loading` [E]:**
- No hay ninguna cadena de llamadas directas de `0x8F1FAA0` a un llamador de `0x778AB0` (búsqueda en anchura sobre
  1 355 funciones): el disparo es virtual, por el procesador de etapa. Eso encaja con la ranura `Loading`.
- El 1,1 ms entre el 2981 y la transición apunta a `Loading` al entrar en la rama 2.
- El anfitrión hace lo mismo con su propio 2981 (§1).

### 7.3 Funciones que toman la cámara, la pantalla o la interfaz, en el mismo procesador [V est]

Todos los manejadores reciben `rcx` = el procesador (`[rcx+8]` = G).

| Función de etapa | Ranura | Manejador del cliente | Bytes de entrada | Efecto |
|---|---|---|---|---|
| **Loading** | `+0x218` | **`0x5F2A00`** | `48 89 5C 24 08 48 89 74 24 10 …` | petición de carga (`0x778AB0`) |
| FadeInOut | `+0x210` | `0x5F52E0` | `E9 EB 8D 95 08` (thunk de 5 B → `.xtls 0x8F4E0D0`) | fundido de pantalla |
| LetterBox | `+0x208` | `0x5F2970` | `C4 C1 7A 10 50 20 B2 01 E9 …` (13 B) | bandas de cine |
| SequencerCamera | `+0x1C8` | `0x5EE250` | `E9 6B A3 93 08` (thunk → `.xtls 0x8F285C0`) | **cámara de secuenciador**: usa el gestor de cámara `[0x6D691E0]+0x120` y escribe `[[0x6D691E0]]+0xDE8 = 0` |
| DeactiveSequencerCamera | `+0x1B8` | `0x5EE210` | `4C 8D 0D … 45 33 C0 33 D2 E9 …` (RIP-relativo, 17 B) | suelta la cámara |
| DeactivateSequencerCameraFadeIn | `+0x1D0` / `+0x1D8` | `0x5EE0C0` / `0x5EE160` | `40 53 48 83 EC 20 48 8B 41 10` / `E9 4B 76 93 08` | suelta la cámara con fundido |
| CameraPreset | `+0x1E0` | `0x5EDD30` | `48 89 5C 24 08 48 89 74 24 10` | preajuste de cámara |
| ForceLockOn | `+0x1F0` | `0x5EAE90` | `48 89 5C 24 08 4C 89 44 24 18` | fija el objetivo |
| LookAt | `+0x220` | `0x5F3600` | `48 89 5C 24 18 55 56 57` | mira a un punto |
| PostProcess | `+0x260` | `0x5F4440` | `48 89 5C 24 10 48 89 74 24 18` | posproceso |
| InputBlock | `+0x150` | `0x5E4E60` | `48 83 EC 28 41 0F B6 40 21` | bloquea la entrada local |
| UIControl, UIStageIcon/Guide/BackgroundGuide, UIHint, UIShowMinimap, UIFindPath | `+0x158`, `+0x1F8`, `+0x238`, `+0x240`, `+0x248`, `+0x168`, `+0x170` | `0x5E4EB0`, `0x5F2980`, `0x5F3A90`, `0x5F4050`, `0x5F4110`, `0x5ECD90`, `0x5ED9A0` | — | interfaz |

- La tabla completa (ranura → manejador del cliente) sale de `scfuncs.py`.
- `SetPhase` (`0x204D7E0`) y `SetHologramActor` (`0x204DEB0`) son lógica de etapa: no se tocan.
- La cámara de la cinemática en el invitado (fov 40 a las 15:26:09, §1) cuadra con `SequencerCamera` [E].
- **No cubierto [H]:** las pistas de cámara de la línea de tiempo del secuenciador, si las reproduce el tick de X
  (`0xA953C0`), no pasan por estas funciones. Ver G-cam en §7.6.

### 7.4 Punto de veto propuesto (invitado)

Solo actúa con `guest_mode=remote`, conexión remota y `guest_seq_route=1`.

**Predicado, sin `thread_local`:** sea `G = [proc+8]` (lectura con SEH). Se veta si se cumplen las tres:
1. `[G]` = vtable `exe+0x55C9720` (`ClientSequencerStageManager`);
2. G ≠ el M propio (el gestor del Kliff local, que ya calcula `guest_seq_play`);
3. una de dos:
   - G está en la `XTable` de `guest_seq_play` de esta conexión;
   - la clave de `[G+0x18]` (`u32 +0x60`) es de clase personaje y distinta de la del jugador local.

Es más exacto que un contexto de hilo: identifica al gestor dueño de la etapa sea cual sea el camino (manejador
enrutado, tick de X, 2524/2111, vida de la fase 2).

| # | Punto | Tipo | Bytes (verificar todos antes de instalar) | Registros | Acción del veto | Retorno nativo |
|---|---|---|---|---|---|---|
| **V1** | **`0x5F2A3A`**, en `Loading` justo tras su compuerta `0x204F730` | `safetyhook::create_mid` | `84 C0 0F 84 95 0B 00 00` (`test al,al; je 0x5F35D7`) en `0x5F2A3A`; contexto `49 8B F0 4C 8B F1 33 FF 89 7D B0 49 8B D0 E8 F6 CC A5 01` desde `0x5F2A27` | `R14` = `RCX` = procesador; `RSI` = datos de la función | poner `RAX = 0` | No hay nada que emular. Con `al=0` el `je` nativo va al epílogo `0x5F35D7`, el mismo camino que cuando la compuerta rechaza; antes solo corrió la compuerta, de solo lectura. La función devuelve `rax = 0` |
| V2 | entradas de `0x5F52E0`, `0x5EE250` y `0x5EE160` (thunks `E9` de 5 B) | gancho en línea de 5 B, como el de `0x20DEE60` de los embudos (se rechaza cualquier otro tamaño) | los 5 B de la tabla de §7.3 | `RCX` = procesador | `xor eax,eax; ret` sin llamar | la ranura no usa el valor [E] |
| V3 | entradas de `0x5F2970`, `0x5EE210`, `0x5EE0C0`, `0x5EDD30`, `0x5EAE90`, `0x5F3600`, `0x5F4440` y `0x5E4E60` | gancho en línea en la entrada | los de la tabla de §7.3 | `RCX` = procesador | `xor eax,eax; ret` | igual |
| V0 (red de seguridad) | entrada de `0x778AB0` | en línea, 5 B: `48 89 5C 24 10` | `RCX` = P, `RDX` = req | solo con el `thread_local` "contexto X" puesto: `ret`, contarlo y escribir una línea WRN | `void` [V est]; el llamador libera su `req` con `0x49E480` de forma nativa |

**El `thread_local`, solo para V0** (lo que pidió el coordinador):
- se pone en `0xC07749` (el gancho de `guest_seq_route`, cuando enruta a X);
- se quita en **`0xC07776`**, el `nop` tras `call 0xA9E100` (mid-hook; bytes `90 48 8D 05 E2 B6 D0 04`);
- en el tick, se pone alrededor de nuestras llamadas a `0xA953C0(X, …)` y `0xAA3E70(X)`;
- cubre los caminos hacia `0x778AB0` que no pasan por funciones de etapa, por ejemplo `ApplyFieldTime 0xA38B00`
  desde un evento `SetTimeOfDay` de la línea de tiempo de X [H].

**Instalación, como en `user2_funnels`:**
- comparar los bytes en el juego antes de escribir; si algo no coincide, `REFUSED` y esa pieza queda apagada;
- clave nueva **`guest_x_stage_veto = 0 | probe | 1`**; `probe` cuenta "WOULD veto (Loading/cámara/…)" con la clave
  de G y el nombre de la etapa (`[[proc+0x10]+0x48]+0x1B8`);
- primera prueba: V1 y V0 a `1`; V2 y V3 a `probe`.

**Qué hace el invitado en vez de recargar: nada.**
- X sigue en `+0x231 = 3`.
- Tras su recarga, el anfitrión borra los secuenciadores viejos (2810 ×6, copiados al invitado) y prepara otros
  nuevos (`0x23B`, `0x376–0x379`). `observer_seq_copy` copia sus 2661, y X los crea y los reproduce con el tick, como
  cualquier 2661 en vivo [E].
- El 2322 del anfitrión tras su recarga **no** hace falta:
  - el resumen de la sesión 9 lo marca como no copiado (`2322 1 (not this trip)`);
  - si llegara, la lógica actual ya lo admite ("re-armed").
- Queda R1: el `ChangeGameLevelStateAck` 2344 solo va al dueño, así que el invitado ve el campamento sin el estado
  nuevo hasta su próximo viaje. Copiar el 2344 sin recarga es [H] y queda fuera.

### 7.5 B1 exacto: partida local cargada después de una sesión remota

**Detección:**
- Dónde: en `guest_send` (`src/mod/session2/guest.cpp`), en la rama `if (g_mode.load() != Mode::Remote)`, líneas
  681–687 (donde hoy se mira el 2490).
- Condición: `g_had_remote` y opcode `kOpLoginCharacterToFieldServerReq` (3093, ya definido en la línea 55).
- Por qué el 3093 y no el 2490:
  - el título manda el 2490 al reconectar, sin cargar nada (t=746,634);
  - el 3093 es el "Jugar" (t=749,012, conexión local, §1) [V].

**Acción:**
1. No reenviar la trama: no llamar al `Send` original, para que no llegue al servidor local.
2. Llamar a una función nueva `end_local_after_remote(epoch)`, gemela de `end_without_host` (`guest.cpp:832-865`):
   - se ejecuta una sola vez (`g_goodbye_ending` o un atómico propio);
   - lee `[P+0x15C]` para el log;
   - escribe `log::error("Session2 guest: a game was loaded on this PC's own server after a session on the host's server in this run (3093 on local connection epoch 0x{:X}). That is not supported (session 9: the load stayed in loading state 0x0C forever), so the game ENDS NOW (TerminateProcess, exit code 0x2003). Restart the game to play on the host or alone | pendientes/p8-s9-cinematica.md")`;
   - llama a `log::flush(log::kTerminateFlushMs)` y después a `TerminateProcess(GetCurrentProcess(), 0x2003)`.
3. Si `TerminateProcess` falla: una línea ERR y la trama pasa como hoy.

**Detalles:**
- **Hilo:** `guest_send` corre en el NetworkThread (`check_send_thread`), el mismo que `end_without_host`.
- **Clave:** `guest_local_after_remote = end | allow`. Por defecto `end`; `allow` = el comportamiento actual.
- **Prueba:** unidad sobre la decisión (`g_had_remote`, modo y opcode) con las tramas reales de la captura: el 2490
  no hace nada; el 3093 termina.
- **Antes, en la reconexión:** la línea ERR actual de `end_remote` ("client reconnected") debe avisar también: "do not
  load a game: it will close" (G4).

### 7.6 Riesgos de A1

- **G-cam [H]:** si la cámara viene de pistas de la línea de tiempo reproducidas por el tick de X, y no de
  `SequencerCamera`, V2 y V3 no bastan. Medirlo en `probe`: ¿salió "WOULD veto SequencerCamera" al pasar a fov 40?
- **Firma de los manejadores [V est parcial]:**
  - solo `0x5F2A00` y `0x5EE250` se leyeron enteros;
  - en el resto, que `rcx` sea el procesador se deduce del trampolín común de la ranura 8;
  - antes de V3, confirmar manejador por manejador que no hace nada que necesite el estado de X (p.ej. contadores de
    la etapa en `[proc+0x10]`).
- **V1 cambia una decisión de la etapa de X, no del servidor.** El servidor no espera nada del invitado para esa
  carga: el 2437 del invitado era justo lo que sobraba.
- **V0 con `thread_local`:** los mismos riesgos que R4 (§5): usar RAII y quitarlo también en el filtro de fallos.

## 8. Implementación (10-10): `guest_x_stage_veto`, `guest_x_camera_veto`, `guest_local_after_remote` (V0, B1 y el predicado cambiados en §9)

No se lanzó el juego. Compilado con `scripts\build.ps1`: **38/38 pruebas**, incluida la nueva `optd_x_veto`.

### 8.1 Ficheros

| Fichero | Qué |
|---|---|
| `src/mod/session2/guest_x_veto_logic.h` | Lógica pura: claves; `gxv::judge` (de quién es el gestor de la etapa); `gxv::act` según el modo; `gxv::inside` (regla de pila del contexto X); `gxv::end_local_load` (B1) |
| `src/mod/session2/guest_x_veto.{h,cpp}` | Ganchos V1, V0, el punto de limpieza y las cámaras (sondas); líneas y resumen |
| `tests/optd_x_veto_tests.cpp` | Prueba nueva (`optd_x_veto`) con los valores de la sesión 9 |
| `guest_seq_route.{h,cpp}` | `guest_seq_route_own_key()`; en el 2981 **enrutado** a X pone el contexto X (`guest_x_context_routed_2981(X, rsp)`) |
| `guest_seq_play.cpp` | `GuestXContextScope` alrededor de `0xA953C0(X)` y `0xAA3E70(X)`; `call_fault` también limpia el contexto |
| `guest.cpp` | B1 en la rama local de `guest_send`, `end_local_after_remote` (gemela de `end_without_host`), texto ampliado del ERR "client reconnected" y arranque de `guest_x_veto_start` |
| `config.{h,cpp}`, `mod.cpp` | Claves nuevas y su línea `Config: P8 X veto: …` |
| `guest_diag_worker.cpp` | `guest_x_veto_tick()` cada 5 s |
| inis `cdmp.p8-s7-shadow.ini` y `-fase2.ini` | `guest_x_stage_veto=1`, `guest_x_camera_veto=probe`, `guest_local_after_remote=end`. Siguen siendo iguales salvo `guest_seq_play_live` (comprobado con `diff`) |

### 8.2 Puntos y bytes (verificados al instalar; si algo no coincide, `GuestXVeto: … REFUSED` y esa pieza queda apagada)

| Punto | Tipo | Bytes parcheados | Contexto verificado | Qué hace |
|---|---|---|---|---|
| **V1 `0x5F2A3A`** | mid, 8 B | `84 C0 0F 84 95 0B 00 00` | `0x5F2A27` (19 B: `mov rsi,r8 ; mov r14,rcx ; … ; call 0x204F730`) y el epílogo `0x5F35D7` (`4C 8D 9C 24 E0 03 00 00`) | si la compuerta pasó y `gxv::judge(R14)` = Veto: `AL := 0`, y el `je` nativo va al epílogo |
| **V0 `0x778AB0`** | en línea, 5 B | `48 89 5C 24 10` (16 B verificados) | — | solo **dentro del contexto X** (marco más profundo, mismo hilo, conexión remota viva): no llama y devuelve (`void`) |
| limpieza `0xC07776` | mid, 8 B | `90 48 8D 05 E2 B6 D0 04` (el `lea` relativo a RIP lo reubica safetyhook) | `0xC07771` `E8 8A 69 E9 FF` (`call 0xA9E100`) | quita el contexto que puso el 2981 en el mismo marco (`rsp` igual en `0xC07749` y aquí) |
| cámaras (sonda) | mid, en la entrada de cada manejador | §7.3: `0x5F52E0` 5 B, `0x5F2970` 6 B, `0x5EE250` 5 B, `0x5EE210` 7 B, `0x5EE0C0` 6 B, `0x5EE160` 5 B, `0x5EDD30`/`0x5EAE90`/`0x5F3600`/`0x5F4440` 5 B, `0x5E4E60` 9 B | — | solo cuentan "would veto" por manejador |

**Comprobado en el PE (capstone):**
- Ningún salto cae dentro de los tramos parcheados:
  - barrido lineal completo de `0x5F2A00–0x5F35F8` y de `0xC073A0–0xC0793E`;
  - barrido `rel32` de todo el PE para los bytes interiores de los parches de más de una instrucción.
- Todo está en `.rsrc`, `[0x1000, 0x51EB000)`. Nada en `.xtls`.

### 8.3 Predicado (`gxv::judge`, en este orden)

1. Conexión no remota → nada.
2. `[proc+8]` nulo → nada.
3. `[G]` ≠ `exe+0x55C9720` → nada.
4. Dueño de G = personaje propio de esta conexión (`guest_seq_route_own_key`, su 2756) → nada.
5. G en la `XTable` de `guest_seq_play` con su clave → **veto**.
6. Personaje propio aún no conocido → nada (falla abierto).
7. Dueño de G que no es un personaje (clase ≠ `0xA`) o ilegible → nada.
8. En otro caso → **veto**.

Con `probe` cuenta "WOULD VETO" y escribe la línea, sin tocar nada.

**Contexto X (V0):**
- `{X, sp}` por hilo. V0 solo veta si la dirección de su propio marco es menor que `sp`.
- Así, un contexto que no se haya limpiado nunca atrapa una llamada desde un marco menos profundo.
- El 2981 solo pone el contexto si el punto de limpieza `0xC07776` está instalado.
- Los ámbitos guardan y restauran el contexto anterior.

### 8.4 Qué mirar en el log del invitado

- **Arranque:**
  - `Config: P8 X veto: guest_x_stage_veto '1' guest_x_camera_veto 'probe' guest_local_after_remote 'end'`;
  - `GuestXVeto (guest_x_stage_veto=1, …): V1 0x5F2A3A hooked, V0 0x778AB0 hooked, X context clear point 0xC07776 hooked | camera: 11 of 11 handlers hooked (count only)`.
- **Al liberar un campamento o ver una cinemática del anfitrión con el invitado cerca:**
  - `GuestXVeto [Loading] … VETOED the stage chart function Loading (0x5F2A00) of manager 0x… (character 0xA0100001; own 0xA0100377; in the XTable yes): another character's manager (X)`;
  - **ninguna** transición `GuestDiag loading transition: [P+0x15C] 0x18 -> 0x01`;
  - ningún `GameLoadingStartReq` 2437 del invitado en su captura;
  - ningún 2679 ni 2001.
- **Cámara:** `GuestXVeto [camera] … WOULD VETO (probe) SequencerCamera …`, para saber si la cámara de la cinemática pasa por las funciones de etapa (G-cam, §7.6).
- **Resumen cada 60 s:** `GuestXVeto summary (…) | V1 Loading in | calls past its gate N, VETOED N … | V0 0x778AB0 in: calls N, inside an X context N, SKIPPED N … | X context: routed 2981 N (clear point in), cleared N, clear of another frame N | camera handlers 11/11 hooked, would veto / calls: …`.
  - Con todo bien: "routed 2981" ≈ "cleared", y "clear of another frame" = 0.
- **B1, tras volver al título y pulsar Jugar:** `ERR … a game was loaded on this PC's own server after a session on the host's server …`, y el proceso termina con el código `0x2003`.

### 8.5 Criterios de aborto

- **Un VETOED con `character` igual al propio:** no debería pasar, porque el predicado lo excluye; sería un error del predicado. Poner `guest_x_stage_veto=probe`.
- **El invitado se cuelga en una carga tras una cinemática del anfitrión** (estado `[P+0x15C]` fijo, sin vuelta a `0x18`): `guest_x_stage_veto=probe`. Si sigue mal, `0`.
- **`SKIPPED` de V0 sin un `[Loading] VETOED` justo antes:** otro camino de recarga dentro del contexto X (p.ej. `ApplyFieldTime`). Anotar el `called from exe+0x…` de la línea.
- **Cierre del juego** en o cerca de `0x5F2A3A`, `0x778AB0` o `0xC07776`: todo a `0` y volcado.
- **Cámara:** con `probe` no se toca nada, así que no hay criterio de aborto propio. Para pasar a `1` hay que leer antes cada manejador entero (§7.6); hoy `1` actúa como `probe` y lo dice en el log.

## 9. Revisión de `6e67d0c` / `f1f2991` (10-10) y cambios

Compilado con `scripts\build.ps1` sin avisos nuevos: **38/38 pruebas**. `optd_x_veto` amplía sus casos con el
contexto X y con B1, y `optd_seq_copy` con la lista de denegación. No se lanzó el juego.

| # | Punto de la revisión | Veredicto y evidencia | Cambio |
|---|---|---|---|
| 1 | V1 solo borraba AL | **Bastaba, pero se cambia.** La compuerta `0x204F730` devuelve un booleano: `0x204F7D7 xor al,al` / `0x204F7F5 mov al,1`, con los bits altos de RAX tal como los dejó `0x1434880`. El camino nativo de "rechazo" solo garantiza AL=0. RAX=0 es uno de los valores posibles de ese camino | `ctx.rax = 0` |
| 2 | V0 veta demasiado (no mira P ni el dueño) | **De acuerdo.** P es un objeto global (`[[0x6D691E0]+0x10]`) y la petición (`0x778320`) no lleva gestor ni etapa: no hay forma barata de saber de quién es la carga. El veto queda en V1, que sí conoce G | V0 **solo cuenta** siempre (`gxv::v0_effective`). En el log aparece como "would skip" |
| 3 | El contexto podía quedarse puesto | **De acuerdo.** Ahora tiene cuatro salidas | `gxv::ContextSlot`: en `0xC07776` se borra si `rsp >= sp`; se borra a la entrada de **cada** gancho de `guest_seq_route`; caduca a los 500 ms (se borra al comprobarlo y se cuenta); los ámbitos RAII guardan y restauran el anterior; el filtro SEH lo borra. Probados: poner, borrar, marco de encima o de debajo, entrada de gancho, caducidad, reloj hacia atrás, anidado y SEH |
| 4 | El dueño propio fijado en el login | **De acuerdo.** | `gxv::Facts.m/m_key`: el M actual (resultado del vf48 del campo) y su dueño vivo. Lo publican el gancho de fotograma de `guest_seq_play` (cada fotograma) y cada manejador de `guest_seq_route` (`guest_x_note_m`). Se usa si es de esta conexión y tiene menos de 2 s. Orden en `judge`: G == M → propio; dueño(G) == dueño vivo de M → propio; dueño(G) == clave del login → propio; después, la tabla y la clave |
| 5 | B1 terminaba demasiado pronto | **De acuerdo.** | `guest_local_after_remote = allow \| stuck`, por defecto `allow`. Con `stuck`, `guest_send` solo marca el 3093 y el 2549 de la conexión local (después de una remota). El hilo de diagnóstico (`guest_local_load_watch`, cada 5 s) termina **solo** si no llegó el 2549 en 45 s desde el 3093 y el estado de carga lleva 20 s igual (`gxv::LocalLoadWatch`; una carga normal manda su 2549 ~15 s después de su 2174). La trama ya no se retiene. Inis: `stuck` |
| 6 | `DenyList::parse` callaba el 0 y el desborde | **De acuerdo.** | Los dos van a `bad` ("(zero: no resource)", "(over the limit of the list)"); la línea WRN lo dice. Prueba ampliada |
| 7 | Hilo de los ganchos 2661/2981/2524 | **Todos corren en el hilo principal [V est + V log].** Cadena: bucle principal `0xAD2380` → `0xAD246A call 0x35D0C0` → `0x35D1B6 call 0x35D2A0` → `vf2` de cada mensaje Troc (2661 `0xC05BB0`, 2981 `0xC073A0`, 2524 `0xC08xxx`, 2322 `0xB87890`; `p8-impl-escenas-play.md` §2.2). Sesión 9: 907/907 líneas `GuestStage [… in/route/result]` en T15980 y "routing hooks on another thread 0" en los 15 resúmenes de `GuestSeqPlay` | Además: el primer hilo que entra es el dueño de `g_seq_res`; un gancho en otro hilo ni lee ni escribe (se cuenta como "resource table: hooks on another thread" en el resumen). `g_deny` es de solo lectura después del arranque |
| 8 | Qué vuelve a armar `X+0x2B0 >= 0`, y lectores de `+0x2A8` | **Escritores de `[G+0x2B0]` en el código del gestor (`0xA90000–0xAB8000`):** solo la vf1 `0xA94A7B` (`mov qword [rbx+0x2B0], r14` con `r14 = 0`, que pone `+0x2B0 = 0.0` y `+0x2B4 = 0` en el reinicio; también limpia `+0x2A8` en `0xA94A02`) y `0xAA5040` (`0xAA50EB`, la resta de dt mientras `>= 0`). El inicio de una mezcla (`+0x2A8 =` asa, en `0xA9C6B0`) lo llama código ofuscado de `.xtls` (`0x11997D12 jmp 0xA9C6B0`, dentro de un tramo con máquina virtual): **quién pone un `+0x2B0` positivo no se ve en estático [H]**. **Lectores de `[G+0x2A8]`:** solo `0xAA5040` (`0xAA5069`, con la guarda `+0x2A8 != 0 && +0x2B0 >= 0`). `0xA939B1` es la ranura `+0x2A8` de una vtable, no este campo. **Un `+0x2B0` negativo** es justo el estado en el que `0xAA5040` deja una mezcla terminada (resta hasta pasar de 0): nadie lo lee mal. Con `+0x2B0 = 0.0` tras la vf1, `+0x2A8` ya está a 0 | ninguno (la compuerta de `f1f2991` es correcta) |
| 9 | ¿El M nativo, con un 2981 de estado ≥ 2 de un secuenciador desconocido, puede recargar? | **No [V est].** 2981: `0xA9E100`, tipo 4 → `0xA9E329 call 0x1F6C190` (búsqueda del nodo) → `0xA9E346 mov rcx,[rbp-0x49] ; test rcx,rcx ; je 0xA9E44F` (suelta y vuelve): no hay rama ni stage chart. Tipo ≠ 4: solo una línea de log (`0x35DE50`). 2524: `0xA9E480` → `0xA9E50F call 0x1F6C190` → `rdi` nulo → `0xA9E564 jne` no se toma → suelta el cerrojo → `0xA9ECBC` (salida). Así que un 2661 denegado (nunca creado) no deja ningún camino de `Loading` en M | ninguno |
| 10 | ¿Quién más lee `P+0x15D/+0x1DD/+0x1DE/+0x16D`? | **Barrido en `0x770000–0x790000`:** los lectores son `0x7797F0` y sus estados (`0x77A3B0` del 4, `0x77B2F0` del 0x0A, `0x77D1F0` del 0x15, `0x77DE20` desde `0x77F9D0`, que llama la máquina al final de cada vuelta) y `0x77FD60` (sin llamador directo: una retrollamada). Todos son parte del ciclo de carga, que sin `[P+0x15C] = 1` no arranca. Con V0 en solo cuenta (punto 2), V0 ya no deja nada sin escribir | ninguno |

**Qué mirar ahora en el log** (cambia respecto a §8.4):
- `GuestXVeto summary … V1 Loading in | … VETOED N, would N, own M N | … V0 0x778AB0 in (count only): … would skip N, stale contexts dropped N | X context: routed 2981 N (clear point in), cleared N, kept (a frame above) N, cleared at a routing hook's entry N`;
- la línea `[Loading]` incluye `current M 0x… of 0x…`;
- B1:
  - `a save is loading on this PC's own server … watched`;
  - después, `not stuck` (con su 2549), o bien `ERR … is STUCK: no MoveFieldCompleteReq 2549 N s after its 3093 …` y el fin con el código `0x2003`.
