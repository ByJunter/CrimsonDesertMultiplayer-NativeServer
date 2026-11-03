# Política de misiones del invitado (`quest_policy`)

Build 1.0.0.2976. Solo en el anfitrión.
- Código: `user2_funnels_logic.h` (`decide_quest_policy`, `policy_drops_req`, con tests en
  `tests/optd_user2_funnels_tests.cpp`), `user2_funnels.cpp` (desvío de `ServerField::vf40 0x2B00C70` y la entrada del
  túnel) y `session2.cpp` (llama a `user2_funnels_policy_judge` / `_record` antes de mandar la Req al servidor; el sniffer alimenta `user2_funnels_sniffed`).
- El análisis previo está en `u0-resultados-s28.md` §8.15.

## S34 (2026-10-14): el invitado ayuda en las misiones del anfitrión, como un compañero

**Decisión del usuario:** el invitado **no** hace avanzar misiones propias en el mundo del anfitrión, y lo que hace
(matar enemigos de misión, entrar en zonas de misión, interactuar con objetos de misión…) **cuenta para las misiones
del anfitrión**, como lo haría un compañero.

### El compañero nativo (lo que hace el juego) [V]

- `GameEvent_Emit 0x1804360(actor, ev)` entrega cada suceso de juego (KillActor `0x61`, DeadActor `0x62`,
  interacción `0x3D`/`0x3E`, zona `0x11`/`0x12`…) al `ServerGameEventHandlerActorComponent` (`[[pj+0x68]+0xA0]`):
  - un personaje que no es el principal pero lo controla el jugador (`byte [A+0x94]`, `vf178`) lo entrega al handler
    del **principal** de su usuario `[[A+0xA0]+0xD0]` (`0x17EE020`), con `ev+0x68` = su clave;
  - con `+0x94 = 0` (un compañero con IA) el suceso se descarta;
  - `user+0xD0` = personaje principal (con el gestor de misiones y el handler), `user+0xD8` = el que se juega ahora
    (lo escribe `0x206AEC0` al terminar la carga de `ChangePlayerbleCharacter`, que es un viaje de recarga).
- El handler lo encola como `HandleGameEventOnceTimer` **2052** (`vf148 0x28FED10` → `0x2C33FE0` →
  `0x1ABAC90`). `ServerQuestActorComponent::vfB0 0x2908250` registra para unos 90 tipos de suceso un único callback,
  `0x290DBD0`, que **no usa el gestor de misiones propio**: pide el del campo con `ServerField::vf40` (retorno
  `0x290DC0B`) y le aplica el suceso (`0x292C450`, `0x292C8B0`, `0x292CA50`, `0x292CEC0`, `0x292D1F0`), con el
  personaje del suceso en `r9` para las condiciones.
- **Consecuencia:** en un campo cuyo dueño es el anfitrión, los sucesos del invitado ya hacen avanzar las misiones del
  anfitrión, digan lo que digan `quest_per_user` y `kill_loot_per_user`. Medido:
  - logs del anfitrión (`cdmp.5.log`, `cdmp_20261014-122206.log`): `exe+0x290DC0B` bajo Req 2052 del usuario 2,
    contestado por el original;
  - S9 en Shadow (`p8-s9-cinematica.md`): la muerte que acreditó al invitado completó el campamento del anfitrión
    (`CompleteQuestGaugeAck` 2738 a `A0100001`).
- El acreditado de una muerte (`0x29431A0`): el atacante si tiene `+0x94`; si no, la clave `arg6+8` del registro de
  ataque (sin verificar para compañeros) [E]. DeadActor no depende del asesino: va al dueño del campo.

### Lo que hace `quest_policy` desde S34

La política ya no inventa una ruta: deja la nativa (el gestor del dueño del campo) donde se hace progreso y solo da al
invitado su gestor propio donde su cliente cambia estado de interfaz por jugador y espera un Ack. Con `1`, dentro de
una Req del usuario 2 y en un campo del anfitrión:

| Retorno de vf40 | Qué es | Con `quest_policy=1` |
|---|---|---|
| `0x290DC0B` (`0x290DBD0`) | el progreso de **todos** los sucesos de juego | **El anfitrión** (el original, contado) |
| `0x2C4A36F` (`0x2C4A310`) | `MissionTriggerInOutReq` 2112: el suceso `0x11`/`0x12` va a `[gestor+0x10]` | **El anfitrión**: `quest_per_user` ya **no** la sustituye (el suceso va al personaje del anfitrión, como en un jugador) |
| `0x104C4B3D` (`.xtls 0x104C49F0`) | `ChallengeDiscoverActorReq` 2215 | El anfitrión (solo se compara la dirección; nada se engancha en `.xtls`) |
| `0x2C3409D` (`0x2C34020`) | `StartChallengeCompleteTimerOnceTimer` 2051 | El anfitrión |
| `0x2B03AA7` (`0x2B03A60`) | punto de reaparición o seguro | El anfitrión (como antes) |
| `0x2A4A32B` (`0x2A4A180`) | `ChangePlayerbleCharacterReq` 2369 | El anfitrión; la Req se descarta en la entrada |
| `0x2C4A0AD` (`0x2C4A050`) | `UpdateTraceMissionReq` 2372, Ack 2346 | Su **propio** gestor (como antes) |
| `0x2A49183` (`0x2A48F00`) | `UpdateMissionUIStateListReq` 2477, Ack 2203 | Su **propio** gestor (antes escribía el estado de interfaz de las misiones del anfitrión) |
| `0x2A4A03B` (`0x2A49F00`) | `SetMissionNewAlarmFalseReq` 2354, Ack 2212 | Su **propio** gestor (antes quitaba los avisos "nuevo" del anfitrión) |

- Los siete llamadores por vtable de `.rsrc` se comprueban al instalar (`FF 50 40` antes del retorno); si alguno no
  coincide, línea ERR.
- **La entrada del túnel**, con `1` y fuera de un campo propio del usuario 2:
  - 2372, 2477 y 2354 se descartan si no pueden llegar a su propio gestor (las condiciones de la 2372, más abajo);
  - 2369 se descarta siempre: el invitado no cambia de personaje (sería un viaje de recarga, y su vf40 es el gestor
    del anfitrión). El cliente no recibe respuesta; qué hace su interfaz está sin medir;
  - `CompleteQuestByCheatReq` 2627 y `ResetQuestByCheatReq` 2251 ya los bloquea el túnel (toda Req `*Cheat*`,
    `server.cpp`).
- **DeadActor:** con `quest_policy=1` el DeadActor del usuario 2 se queda en el dueño del campo, lo nativo, aunque
  `kill_loot_per_user=1`; el destinatario del botín sigue siendo por usuario. Con `kill_loot_per_user=1` el resultado
  en misiones era el mismo (el suceso acababa en el gestor del anfitrión por `0x290DC0B`), pero con el personaje del
  invitado en las condiciones.
- `probe` cuenta lo que haría `1` y no cambia nada (`quest_per_user` decide como antes).

### Qué está medido y qué decidido

| Punto | Estado |
|---|---|
| Los sucesos del invitado avanzan las misiones del anfitrión | **Medido** (ver arriba) y mantenido |
| Sus misiones propias no avanzan en el campo del anfitrión | **Medido** por la ruta: nada de su progreso usa su gestor allí |
| 2477 / 2354 a su propio gestor, 2369 descartada | **Decidido** (S34); falta verlo en Shadow |
| La 2112 en el mundo | **Sin medir**: 0 de 38 capturas la traen; las zonas van sobre todo por `GamePlayTriggerInOutReq` 3068 (gimmicks) y `SequencerTriggerVolumeInOutReq` 2767 (vf48, el secuenciador del anfitrión) |
| Aceptar o entregar misiones y elegir en diálogos (`SetQuestDialogReq` 2857, `QuestDialogEndReq` 2342, `StageBranchStateReq` 2436, `UpdateStageChartEventReq` 2591) | **Decisión abierta**: el invitado podría decidir diálogos por el anfitrión (un compañero nativo no habla). Solo se cuentan; no se descarta ninguna |
| Muertes de compañeros con IA (`arg6+8`) | Sin verificar [E] |

**Aviso: los objetos de misión.** Lo que recoge el invitado va a **su** inventario: `PushTargetToInventoryReq` 2198 y
el saqueo `0x2B81410` no comprueban dueño. Una misión del anfitrión de "conseguir X" se bloquea si el objeto lo recoge
el invitado. Hasta que haya una solución, que los recoja el anfitrión.

**Otros:** si los dos clientes mandan 2112 de la misma zona, el gestor del anfitrión recibe entradas y salidas dobles
[E]. No hay doble crédito por muertes: KillActor va a un único acreditado y DeadActor se emite una vez [V]. La
interfaz del invitado sigue mostrando su diario (congelado) y no ve el progreso del anfitrión: copiarle las Acks de
misión del anfitrión es un paso posterior.

### La sonda (`probe` y `1`, solo lectura)

- Gancho de entrada en `0x290DBD0` (59 bytes comprobados hasta su `call [rax+0x40]`): para cada suceso del personaje
  del usuario 2 (`[rcx+8]`), el tipo `byte [rdx+8]`, la clave `ev+0x68` (del usuario 2 / del anfitrión / otra / 0) y si
  corre dentro de una Req del usuario 2. Cuenta también los sucesos del personaje del anfitrión.
- Contadores por llamador de vf40 y resultado.
- La entrada: 2342/2857/2436/2591 del usuario 2 (en el campo del anfitrión o fuera; entregadas o no), 2369, y 2372 /
  2477 / 2354 con sus fallos de ruta.
- El cliente del anfitrión: 3009 `UpdateMissionListAck`, 3088 `UpdateQuestGaugeAck`, 2738 `CompleteQuestGaugeAck`,
  2346 `UpdateTraceMissionAck` (por el sniffer; necesita `[sniffer] enabled=1`).
- Líneas: `User2Funnels: quest_policy … | vf40: the host's at … | DeadActor … | probe 0x290DBD0 hooked …` al instalar;
  `… | quest_policy N: vf40 the host's N (would N), its own N …` en `User2Funnels summary`; y cada 60 s
  `User2Funnels quest_policy N summary | vf40 callers under user 2: … | own-manager Reqs: … | 2369 … | measured
  only: … | 2112 … | DeadActor kept native … | events of user 2's character at 0x290DBD0 (hooked): types … | ev+0x68
  … | the host client got 3009 N / 3088 N / 2738 N / 2346 N`.

**En un PC:** `cdmp_peer` con `build\e2e\u2_shop.troc` (2671/2720) da sucesos de interacción del usuario 2: deben salir
`game events 0x290DC0B: the host's N` (o `would be the host's` con `probe`) y tipos `0x3D`/`0x3E` en la sonda, sin
ERR. Ningún guion trae 2112, 2342, 2369, 2477 ni 2354.

**Con Shadow:** el invitado mata miembros de un campamento (3088/2738 en el cliente del anfitrión, tipos `0x61`/`0x62`),
recoge un objeto de misión (el aviso de arriba), abre el diario y el mapa (2477/2354 contestadas por su gestor),
intenta cambiar de personaje (2369 descartada) y habla con quien da o recibe una misión (se cuentan 2857/2342/2436/2591;
anotar qué cambia en el diario de cada uno).

## La regla (el usuario, 2026-10-13)

> Lo que sigue es la versión de 2026-10-13 (antes de S34). Siguen vigentes la regla, la clave y la 2372 (sus
> condiciones de entrada valen ahora también para 2477 y 2354). Cambiaron, según la sección S34 de arriba: la tabla
> de llamadores (ahora nueve), la 2112 (ya no solo se mide: con `1` va al anfitrión aunque `quest_per_user=1`) y el
> formato del resumen (los contadores por llamador están en la línea `User2Funnels quest_policy N summary`).

- El invitado entra para hacer las cosas **del anfitrión**: no hay misiones propias del invitado.
- Lo que dependa del progreso de misiones o del mundo sigue el **progreso del anfitrión**.
- Lo que el juego ya tiene por usuario sigue igual: inventario, dinero, tiendas, habilidades y la partida propia del
  personaje invitado.

## La clave

`[session2] quest_policy = 0 | probe | 1`, en el anfitrión, por defecto `0`.
- Es **independiente** de `quest_per_user`, y el kit 31 no cambia.
- `probe` cuenta lo que haría `1` y no cambia nada.

Con `1`, dentro de una Req del usuario 2 y en un campo cuyo dueño es el anfitrión:

| Llamador de vf40 | Qué es | Con `quest_policy=1` |
|---|---|---|
| `0x2B03AA7` (`0x2B03A60`) | La posición de reaparición o segura según la región de misión. La usan la muerte, el revivir, `ChangePlayerbleCharacter` y el guardado de `TransformSync` (13 llamadas en 11 funciones: `0x287B0AA`, `0x291AE32`, `0x28E4C15`, `0x2B97EFE`, `0x29189EE`, `0x294D1CD`, `0x2953444`, `0x2B5186F`, `0x2BC16FC`, `0x2BC17D3`, `0x2BD112D`, `0x29485BC`, `0x2949517`) | El gestor de misiones **del anfitrión** (el original), diga lo que diga `quest_per_user` |
| `0x2C4A0AD` (`0x2C4A050`) | `UpdateTraceMissionReq` 2372: qué misión sigue el jugador | El gestor **propio** del usuario 2 la contesta |
| `0x2C4A36F` (`0x2C4A310`) | `MissionTriggerInOutReq` 2112: el cliente entra o sale de la zona de una misión | **Solo se mide**; decide `quest_per_user` |
| `0x2896347` (`0x2896120`) | El paso de misión al aplicar un viaje rápido | Sin cambios: los dos clientes mandan 0 y no se ejecuta |

### 2372: por qué contestarla con su propio gestor y no descartarla

- **Hay respuesta:** existe `UpdateTraceMissionAck` 2346. Si se descarta la Req, el cliente del invitado no recibe la
  Ack, y no se sabe qué hace su interfaz (puede revertir el seguimiento o reintentar).
- **Con su propio gestor**, el manejador nativo corre entero sobre el gestor del usuario 2:
  - el invitado recibe su Ack como siempre;
  - el seguimiento del anfitrión **no se toca**: el manejador no ve otro gestor que el que le devuelve vf40.
- **En la entrada del túnel** (antes del servidor) se comprueba que la 2372 **solo** pueda llegar a su propio gestor. Si
  no, con `1` se **descarta**, salvo en un campo propio del usuario 2, donde el dueño ya es él (revisión de `7da4dfa`,
  M1). Las condiciones, en este orden:
  1. los ganchos del sujeto y de vf40 están puestos (`user2_funnels_vf40_live`; las vtables que usa la comprobación se
     publican antes, así que no se leen sin inicializar: B2);
  2. las claves están vivas (se refrescan cada 100 ms tras una entrada o recarga);
  3. el personaje principal de la sesión es el que nombran las claves y pasa la prueba del sujeto de `ExecuteTroc`
     (clave, controlador `+0x90`, clase);
  4. su propio gestor (`[[[pj+0x68]+0x138]+0x18]`, con las vtables) se puede leer.
- Si aun así llega al vf40 sin poder leer el suyo (una carrera entre hilos), contesta el original y queda una línea
  **ERR** (`… the ORIGINAL answered - the host's tracked mission may have changed`).
- **Un Nak es lo esperado** si el invitado sigue una misión que no está activa en su propio gestor: lo contesta su
  gestor, no el del anfitrión.
- Con `quest_per_user=1` el resultado ya era el mismo para la 2372. La política lo garantiza también con
  `quest_per_user=0`, y deja explícito por qué.
- (Hasta S34) **Había que dejar `quest_per_user=1` junto con `quest_policy=1`:** con `quest_per_user=0`, la 2112
  llegaría al gestor de misiones del anfitrión. Desde S34 eso es lo que se quiere, y `quest_per_user` ya no la cambia.

### 2112: solo medir

Con `quest_per_user=0`, las entradas y salidas de zona del invitado llegarían al gestor del anfitrión. Habría dos
personajes dando entradas y salidas de la misma zona, y si el invitado sale mientras el anfitrión sigue dentro se podría
romper el estado de su misión. Con `1` hacen progresar las misiones propias del invitado, que la regla no quiere.
- Antes de decidir, se mide:
  - cuántas manda el invitado;
  - el primer u32 (aún sin leer qué es: probablemente la zona o la misión);
  - los primeros 16 bytes de las 12 primeras;
  - a qué gestor llegan en su vf40 (`user 2's N / the host's N`).

## Qué se ve en el log

- Las Req se juzgan en cada ofrecimiento (una Req retenida se ofrece otra vez), pero se **cuentan una vez**, en su
  disposición final: entregada al servidor o no (B1).
- `User2Funnels quest_policy: user 2's MissionTriggerInOutReq 2112 #n in the host's field (… bytes): … | handed …`, las
  12 primeras en el campo del anfitrión.
- `User2Funnels quest_policy: user 2's UpdateTraceMissionReq 2372 dropped before the server: <motivo> …` (WRN, las 4
  primeras).
- `User2Funnels quest_policy …: … its own quest manager could not be read at the vf40 …` (ERR), si pasa la carrera.
- En `User2Funnels summary`:
  `| quest_policy 1: respawn point the host's N (would N) | 2372 from user 2 in the host's field N / elsewhere N: its
  own N (would N), unreadable at the vf40 N, dropped at the ingress N (would N) | route failures: … | 2112 from user 2
  in the host's field N / elsewhere N (first u32: …): at its vf40 user 2's N / the host's N`.
- Las 2372 que contesta su propio gestor también se suman en `vf40 … (user 2's N)` del resumen.
- La línea de configuración: `Config: P8 quest_policy '…'`.

## Pruebas

**En un PC** solo se puede comprobar que se instala y que no rompe nada: el peer no muere y ningún guion manda 2372 ni
2112.
- Con `quest_policy=1` añadido a `cdmp.p8-u1-local-1.ini` (una copia local, sin cambiar el kit), el comando de siempre
  (`u0-resultados-s28.md` §8.11).
- **Se da por bueno si:**
  - `User2Funnels: quest_per_user … (vf40 hooked)`;
  - `Config: P8 quest_policy '1'`;
  - en el resumen, `quest_policy 1: respawn point the host's 0 … 2372 from user 2 0 … 2112 from user 2 0`;
  - el resto como en §8.11–8.14, sin ERR.

**Con Shadow**, primero `quest_policy=probe` y luego `1`:
- el invitado muere y revive en el campo del anfitrión: `respawn point the host's N` (con `1`) o `would N` (con
  `probe`), y reaparece en un sitio válido;
- el invitado cambia la misión que sigue: `2372 from user 2 N: its own N`, su interfaz lo refleja, y la misión seguida
  del anfitrión **no cambia** (lo mira el anfitrión en su mapa);
- el invitado cruza zonas de misión: el resumen de 2112 da sus ids y a qué gestor llegaron. Con eso se decide la
  política de la 2112.
