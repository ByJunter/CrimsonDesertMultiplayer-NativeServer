# S44 · 3.1 Inventario con datos: las escenas del anfitrión en el cliente del invitado

Build 1.0.0.2976. Solo lectura de `C:\Emulador CD\backups` y del repositorio `C:\Emulador CD-escenas`: no se ha compilado,
desplegado ni abierto el juego. Etiquetas: **[V]** verificado en logs o capturas, **[E]** inferencia fuerte, **[H]** hipótesis.

Personajes: anfitrión `0xA0100001` (usuario `0x90100000`). Invitado `0xA0100066` en dos copias (`0xA0100067` en
`pose-anfitrion`), y una clave distinta en cada sesión de Shadow (`0xA0100377`, `0xA0100423`, …).

## Archivos de salida

Carpeta `scratchpad\s44\inventario\`:

- **`tabla_escenas_anfitrion.csv` / `.json`**: la tabla de §3, una fila por recurso, con todas las columnas;
- `out\occurrences.csv` / `.json`: una fila por 2661 recibido (sesión, secuenciador, enlaces completos, campos del play data,
  veredicto, creación, vetos de X, estados 2981, nº de 2524, marcas, línea del log);
- `out\by_resource.csv` / `.json`: lo mismo agregado por (clase de dueño, recurso), incluidos invitado y otros;
- `out\host_side.csv`: cada 2661 del anfitrión que vio su propio cliente (`SceneDiag`) y si `observer_seq_copy` lo envió
  al usuario 2, en las 89 ejecuciones del anfitrión;
- `out\path287.txt`: qué escenas creó el anfitrión por el camino de «stage element» `0x287C200` (`ShopProbe`);
- scripts: `extract_caps.py`, `analyze.py`, `aggregate.py`, `hostpass.py`, `path287.py`, `table.py`, `h3.py` (lookup3) y
  `mkdict.py`;
- `names.json` (173 MB): hash → nombre para los 2 052 523 archivos del índice del juego.

---

## 0. Resumen

1. **Volumen [V].** El invitado recibió **1 520 PrepareSequencerAck (2661) cuyo dueño es el anfitrión**:
   - 86 recursos distintos;
   - 50 ejecuciones del invitado con log, deduplicadas por cabecera (dos copias en un PC y Shadow, sesiones 6B–38);
   - 1 161 de ellos también en captura, con el payload completo.

   El mod los trató así:
   - 1 455 `ROUTED to the actor's own manager X` y 1 456 `created`;
   - **13 `DENIED`**: 12 de la revancha de Matthias en `s43-pose` y 1 de la cinemática de carga en la sesión 11;
   - 2 `REJECTED` por un NPC ausente.

   **No hay ningún 2661 cuyo dueño sea un NPC [V].** Los 1 635 2661 recibidos, del anfitrión o del invitado, tienen
   `actor == dueño`, y el dueño siempre es un personaje de jugador (0xA…).
2. **Los nombres se resuelven todos por datos [V]:**
   - recurso = lookup3 `hashlittle(nombre base sin extensión, 0xC5EDE)` (la misma función que `guest_sched_load_logic.h:77`);
   - comprobado con `cd_seq_quest_marnidragon_boss_7610 → 0x519CC9E7` (log del anfitrión) y con los 86 recursos, cada
     uno con un único nombre en `build\research\arrow\index.tsv` (`sequencer/*.paseq`).

   **Corrección de nombres:**
   - `0x9586FFA4` es **`cd_seq_boss_rematch_matthias`**. `'cd_seq_boss_rematch_matt'` es el nombre de etapa truncado que
     imprime `GuestXVeto`, y su hash es otro (`0xE4741173`);
   - `0x8CD771B1` es **`cd_seq_faction_bluemontwinehouse_loading`** (§4.2).
3. **`kind` y el tipo de enlace no distinguen nada [V]:**
   - `kind` = 4 en los 1 635;
   - tipo de enlace = 5 en los 4 051 enlaces.
4. **El id de rol tampoco [V]:**
   - `0x2000000N` es el índice de la ranura de actor dentro del secuenciador, no un rol semántico;
   - el anfitrión ocupa la ranura 1, la 2 o la 3 (la 5 en `funcnpc_dye`) según la escena, y los NPC van en las
     siguientes;
   - **muchas escenas del mundo enlazan al anfitrión en `0x20000002`**:
     - las 16 `cd_seq_funcnpc_*` (tiendas);
     - los minijuegos;
     - los `node_*`;
     - `contents_wanted`;
     - `weather_cloudy_0007`;
     - varias `quest_*`;
   - y solo 5 recursos no lo enlazan (§5).
   - **Ninguna escena del anfitrión enlaza al invitado [V].**
5. **Hay un campo nativo en el play data que nadie había separado [V en datos, E en significado].** Es el par de bytes
   `@17/@18` del payload del 2661 (justo antes de la etapa `@19`):
   - `1/0` en 1 080 de 1 161: tiempo, tiendas, minijuegos, misiones, recompensa, nodos… y la **cinemática de carga**;
   - **`2/2` solo en `cd_seq_boss_rematch_matthias`** (16 de 16). En ese caso:
     - la «etapa» `@19` es `clave_NPC << 32 | 0x135`;
     - `@55` repite esa clave del NPC;
     - `@31` y la posición están a 0;
   - **`6/3`** en `memory_*` y en `use_sealedartifact_0001`:
     - en `use_*`, `@55`/`@59` = la clave del jugador que usa el objeto;
     - el último byte `@90` es `0x04` o `0x00`, en vez de `0x0D`;
   - **`10/1`** en `spawn_auto_animal_bush_bird`: la «etapa» es `0x3E9/0x3EA << 32 | n`.

   Contradice la frase de `p8-impl-escenas-play.md` §15.4 («los únicos campos que cambian son la etapa y el recurso»). Es
   cierta para `0x213` frente a escenas de tipo 1, pero no en general.
6. **Correlación con lo visible:**
   - **pose congelada [V correlación]:** solo en las dos sesiones en las que `boss_rematch_matthias` se enrutó y se creó
     en X y seguía viva en la marca:
     - `pose-anfitrion`: `0x440`, marcas 2 y 3;
     - `s43-activo`: `0x464`, marca 1;
     - en `s43-pose` se denegó 12 veces de 12 y no hubo pose;
   - **recarga del campo [V]:** solo con `faction_bluemontwinehouse_loading` (tipo `1/0`), en la sesión 9 de Shadow;
   - **efectos no visibles pero medidos [V]:**
     - InputBlock/UIControl con las tiendas mientras el anfitrión las usa;
     - TimelineEvent con `reward_spear_base`;
     - SequencerCamera con `quest_progression_call_0100`;
     - UIShowMinimap con los `node_*`, `wanted` y `carrierpigeon`.

     Todos vetados o en «would».
7. **Para la pieza de tenderos [V]:**
   - en `tenderos-p2b`, el servidor también creó para el **invitado** sus propias `cd_seq_funcnpc_smithy` y `_butcher`:
     dueño `0xA0100066`, invitado en `0x20000002`, la misma etapa `0x41934310` y la misma posición que la copia del
     anfitrión;
   - dos de ellas salieron `REJECTED` porque el herrero no estaba aún en ese cliente.

   Las tiendas son instancias **por jugador** de un mismo «stage element» [E].

---

## 1. Fuentes y método

- **Capturas del invitado:** 40 archivos `.bin`, los de dos copias y todos los de Shadow (sesiones 3–38).
  - Se extrajeron los S>C 2661/2810/2981/2524/2322 completos con `extract_caps.py`, más 6 capturas sin log (sesiones 3–6A
    y la primera de la 10).
  - Formato: `src/common/capture_file.h`.
- **Logs del invitado:** 59 ejecuciones distintas tras deduplicar por los primeros 400 bytes, quedándose con el archivo
  más largo. Por ejemplo, `pose-anfitrion\guest.log` es una copia truncada de `cdmp.guest_20261015-083511.log`.
  - Líneas que se leen:
    - `GuestStage [2661 in]`: kind, dueño, enlaces (el log corta a 6 con «+N more»; en 28 casos sin captura faltan
      enlaces);
    - `GuestSeqRoute [2661 …]`: veredicto y `resource`;
    - `GuestStage [2661 result]` y `[2661 missing actor]`;
    - `GuestStage [2810 in]` y `GuestSeqRoute [2981 …]`;
    - `GuestXVeto [...] … stage '<nombre>' (key <seq>)`;
    - `=== MARK n (Ctrl+F12) ===`;
    - la lista activa `guest_seq_route_deny: … : 0x…`.
- **Enlace captura ↔ log:** misma carpeta y mayor solape de (dueño, secuenciador).
  - La hora de pared de los registros de captura sale de la mediana de `(~hh:mm:ss) − capture t` del log.
- **«Viva en la marca»:** el 2661 llegó antes de la marca y su 2810 no había llegado.
  - Las tiendas, los minijuegos y el tiempo viven toda la sesión, así que este dato solo discrimina en escenas cortas.
- **Lado del anfitrión:** 89 ejecuciones (`hostpass.py`) con `SceneDiag [session 1 (sniffer)] … 2661 … resource` y
  `ObserverSeqCopy [copy] … SENT to user 2's queue`.
  - Incluye las sesiones 23–33, que solo tienen log del anfitrión, y las pruebas locales con `cdmp_peer`
    (`u1-local-*`, `u33-local-*`, `f4-*`), en las que el «usuario 2» no es un juego.

---

## 2. Formato del 2661 visto en las capturas [V]

Payload: `u8 kind | u32 actor @1 | play data de 86 bytes @5 | n × 11 bytes de enlace @91 (u32 ranura, u32 actor, u8 tipo, 2 × u8)`.

| Offset | Tipo `1/0` (`weather_winddegree_0006`, `s43-activo` seq 0x115, captura t=53.713) | Tipo `2/2` (`boss_rematch_matthias`, `s43-activo` seq 0x464, t=142.378) |
|---|---|---|
| @0 kind | `04` | `04` |
| @1 actor / @5 dueño | `A0100001` / `A0100001` | `A0100001` / `A0100001` |
| @9 secuenciador | `0x115` | `0x464` |
| **@17 / @18** | **`01 00`** | **`02 02`** |
| @19 etapa (u64) | `0x000F496C` (id) | **`0xB0101214_00000135`** (clave NPC « 32 \| 0x135) |
| @27 recurso | `0x1DC703E8` | `0x9586FFA4` |
| @31 | `0x000F496C` (= etapa) | `0` |
| @35 pos xyz + yaw | (-6726.45, 477.77, -3046.45, …) | ceros |
| @55 / @59 | 0 / 0 | **`B0101214`** / 0 |
| @68 | `ff ff ff 7f` + contador `@72` (1–4; hasta 0x17 en spawn_auto) | igual (contador 1–11) |
| @90 | `0d` | `0d` |

Valores de `@17/@18` en las 1 161 escenas del anfitrión con captura:

| `@17/@18` | Escenas |
|---|---|
| `1/0` | 1 080 |
| `6/3` | 39 |
| `10/1` | 26 |
| `2/2` | 16 |

Las escenas del invitado tienen la misma forma:
- `funcnpc` `1/0`;
- `use_sealedartifact` `6/3`, con `@55` = la clave del invitado;
- `spawn_auto` `10/1`.

Payloads completos: `out\occurrences.json`, campo `tail`, y `peek.py`.

---

## 3. Tabla completa: escenas con dueño `0xA0100001` que llegaron al cliente del invitado (deduplicadas por recurso)

Columnas:
- **b17/18:** el par `@17/@18`.
- **Enlaces:** el número de enlaces.
- **Ranura anf.:** la ranura del anfitrión (…N = `0x2000000N`).
- **Firma:** la primera firma, en formato `ranura:actor/tipo`, con H = anfitrión y N = NPC.
- **2981:** los estados vistos.
- **287C200:** si el anfitrión la preparó por el camino de «stage element» (`NOT` = preparada sin ese camino; `after-cap`
  = el log de `ShopProbe` ya se había cortado).
- **Vetos de control:** vetos de X distintos de UIStageGuide, SetInteraction y SetQuestDialogAICategory.
- **Ocur./ses.:** ocurrencias y sesiones del invitado.
- **Viva en marca:** si la escena seguía viva en alguna marca Ctrl+F12.

Los enlaces completos, el nombre de las sesiones y la primera línea de log de cada escena están en el CSV.

| Familia | Recurso | Nombre | b17/18 | Enlaces | Ranura anf. | Firma | 2981 | 287C200 | Vetos de control | Ocur./ses. | Veredicto | Creación | Viva en marca |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| boss_rematch | `9586FFA4` | cd_seq_boss_rematch_matthias | 2/2 | 3 | …2 | 4:N/5 2:H/5 6:N/5 | 0 | NOT-287C200: 13, after-cap: 3 | WOULD VETO BlockInteraction off WOULD VETO BlockInteraction on | 16/3 | DENIED 12 ROUTED 4 | created 4 | sí (3) |
| memory | `54CD80FE` | cd_seq_memory_alustin_journal_0037 | 6/3 | 3 | …3 | 3:H/5 5:N/5 6:N/5 | 2 | - | - | 1/1 | ROUTED 1 | created 1 | - |
| memory | `72A00680` | cd_seq_memory_bluemontmanoralley_0001 | 6/3 | 3 | …3 | 3:H/5 5:N/5 6:N/5 | 2 3 | - | - | 26/13 | ROUTED 25 | created 25 | sí (1) |
| memory | `F2A314F0` | cd_seq_memory_hernandgibbet_0001 | 6/3 | 13 | …3 | 3:H/5 5:N/5 7:N/5 9:N/5 A:N/5 B:N/5 C... | 2 | after-cap: 1 | - | 1/1 | ROUTED 1 | created 1 | - |
| use | `70AE382E` | cd_seq_use_sealedartifact_0001 | 6/3 | 1 | …2 | 2:H/5 | 0 | - | - | 11/11 | ROUTED 10 | created 10 | - |
| faction | `8CD771B1` | cd_seq_faction_bluemontwinehouse_loading | 1/0 | 1 | …2 | 2:H/5 | 0 1 2 3 4 5 | - | - | 3/3 | DENIED 1 ROUTED 1 | created 1 | sí (1) |
| reward | `8250A569` | cd_seq_reward_spear_base | 1/0 | 2 | …1 | 1:H/5 3:N/5 | 0 | 287C200: 10 | VETOED TimelineEvent VETOED UIShowMinimap | 33/30 | ROUTED 34 | created 34 | sí (21) |
| minigame | `94FEA6AE` | cd_minigame_armwrest_base | 1/0 | 8 | …2 | 2:H/5 11:N/5 4:N/5 8:N/5 9:N/5 B:N/5 ... | 0 | 287C200: 12, after-cap: 4 | - | 35/24 | ROUTED 33 | created 32 REJECTED 1 | sí (18) |
| minigame | `AF1A2C86` | cd_minigame_duel_unarmed | 1/0 | 10 | …2 | 2:H/5 4:N/5 8:N/5 9:N/5 A:N/5 B:N/5 D... | 0 | - | - | 6/3 | ROUTED 4 | created 4 | sí (2) |
| minigame | `6FC2AAA5` | cd_minigame_rockpapersci_base | 1/0 | 3 | …2 | 2:H/5 4:N/5 6:N/5 | 0 | 287C200: 12, after-cap: 3 | - | 34/24 | ROUTED 33 | created 33 | sí (21) |
| minigame | `1A1786F0` | cd_minigame_seotda_base_hernand | 1/0 | 1 | - | 1:N/5 | 0 | 287C200: 12, after-cap: 4 | - | 35/24 | ROUTED 33 | created 33 | sí (21) |
| minigame | `E6D0C64E` | cd_minigame_timeattack_base | 1/0 | 24 | …2 | 2:H/5 1C:N/5 1D:N/5 1E:N/5 1F:N/5 20:... | 0 | 287C200: 1 | - | 1/1 | ROUTED 1 | created 1 | - |
| funcnpc | `AA015527` | cd_seq_funcnpc_bank | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 15 | - | 35/24 | ROUTED 33 | created 33 | sí (21) |
| funcnpc | `12AB3626` | cd_seq_funcnpc_bond | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 1 | - | 1/1 | ROUTED 1 | created 1 | - |
| funcnpc | `978345CA` | cd_seq_funcnpc_butcher | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 1 2 3 7 | 287C200: 16 | VETOED InputBlock VETOED UIControl WOULD VETO BlockInteraction WOULD VETO ChangePhasePanelTag | 42/30 | ROUTED 41 | created 41 | sí (22) |
| funcnpc | `3BB54CDB` | cd_seq_funcnpc_charlatanstore_demeniss_west | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | - | - | 6/3 | ROUTED 4 | created 4 | sí (2) |
| funcnpc | `D0D90BA8` | cd_seq_funcnpc_church | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 11 | - | 33/30 | ROUTED 34 | created 34 | sí (21) |
| funcnpc | `591B85B` | cd_seq_funcnpc_clothes | 1/0 | 3 | …2 | 2:H/5 12:N/5 6:N/5 | 0 1 2 7 | 287C200: 15 | - | 32/23 | ROUTED 31 | created 31 | sí (20) |
| funcnpc | `28C6B5C2` | cd_seq_funcnpc_contribution | 1/0 | 2 | …2 | 2:H/5 9:N/5 | 0 | 287C200: 16 | - | 36/24 | ROUTED 34 | created 34 | sí (21) |
| funcnpc | `A2304D5D` | cd_seq_funcnpc_craftsmith | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 1 2 14 | 287C200: 14 | - | 30/22 | ROUTED 29 | created 29 | sí (19) |
| funcnpc | `2CFAD71A` | cd_seq_funcnpc_general | 1/0 | 2 | …2 | 2:H/5 6:N/5 | 0 | 287C200: 16 | - | 44/30 | ROUTED 43 | created 43 | sí (22) |
| funcnpc | `25C5EC9B` | cd_seq_funcnpc_goblinbroker | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 15 | - | 21/13 | ROUTED 21 | created 21 | sí (15) |
| funcnpc | `8FA1605E` | cd_seq_funcnpc_grocery | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 1 2 7 | 287C200: 17 | VETOED InputBlock VETOED UIControl WOULD VETO BlockInteraction WOULD VETO ChangePhasePanelTag | 46/30 | ROUTED 45 | created 45 | sí (22) |
| funcnpc | `6EA4BC4E` | cd_seq_funcnpc_leather | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 17 | - | 37/24 | ROUTED 36 | created 36 | sí (21) |
| funcnpc | `EA60A5DF` | cd_seq_funcnpc_pub | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 17 | - | 37/24 | ROUTED 35 | created 35 | sí (21) |
| funcnpc | `6F42C85E` | cd_seq_funcnpc_seatdealer | 1/0 | 3 | …2 | 2:H/5 4:N/5 8:N/5 | 0 | - | - | 24/20 | ROUTED 23 | created 23 | sí (5) |
| funcnpc | `A7132561` | cd_seq_funcnpc_smithy | 1/0 | 2 | …2 | 2:H/5 8:N/5 | 0 1 2 7 | 287C200: 17 | VETOED InputBlock VETOED UIControl WOULD VETO BlockInteraction off WOULD VETO BlockInteraction on WOULD VETO ChangePhasePanelTag | 46/30 | ROUTED 45 | created 45 | sí (22) |
| funcnpc | `F2FD9646` | cd_seq_funcnpc_wantedfence | 1/0 | 4 | …2 | 2:H/5 6:N/5 8:N/5 9:N/5 | 0 | 287C200: 1 | - | 1/1 | ROUTED 1 | created 1 | - |
| quest | `8DBF8E16` | cd_seq_quest_bloodcoronation_1500_01 | 1/0 | 10 | …1 | 1:H/5 3:N/5 4:N/5 5:N/5 6:N/5 7:N/5 | 0 | NOT-287C200: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `55116B0A` | cd_seq_quest_caliburn_boss_0499_05 | 1/0 | 3 | …1 | 1:H/5 5:N/5 3:N/5 | 0 | 287C200: 11 | - | 35/30 | ROUTED 36 | created 36 | sí (21) |
| quest | `57A22A43` | cd_seq_quest_carrierpigeon_abyssteleport_0100 | 1/0 | 2 | …1 | 1:H/5 3:N/5 | 0 1 | - | VETOED UIShowMinimap | 7/6 | ROUTED 7 | created 7 | sí (3) |
| quest | `C542144F` | cd_seq_quest_crime_police_hernand_0300 | 1/0 | 6 | …1 | 1:H/5 3:N/5 4:N/5 5:N/5 6:N/5 7:N/5 | 0 | 287C200: 1 | - | 1/1 | ROUTED 1 | created 1 | - |
| quest | `E5B490F1` | cd_seq_quest_crime_police_wakeup_prison_hernand_0100 | 1/0 | 4 | …2 | 2:H/5 7:N/5 4:N/5 5:N/5 | 0 | 287C200: 1 | WOULD VETO UIStageIcon | 1/1 | ROUTED 1 | created 1 | - |
| quest | `AC7128DA` | cd_seq_quest_demeniss_0200 | 1/0 | 6 | …2 | 2:H/5 A:N/5 4:N/5 5:N/5 7:N/5 8:N/5 | 0 | after-cap: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `3E9BFA1D` | cd_seq_quest_demeniss_circus_0100 | 1/0 | 1 | …1 | 1:H/5 | 0 | NOT-287C200: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `8D8BB4C8` | cd_seq_quest_demeniss_circus_0500 | 1/0 | 8 | …1 | 1:H/5 3:N/5 4:N/5 5:N/5 6:N/5 7:N/5 | 0 | NOT-287C200: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `B4117165` | cd_seq_quest_demeniss_south_2100 | 1/0 | 3 | …2 | 2:H/5 4:N/5 5:N/5 | 0 | NOT-287C200: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `C97A68F6` | cd_seq_quest_demeniss_south_2200 | 1/0 | 3 | …2 | 2:H/5 4:N/5 5:N/5 | 0 | NOT-287C200: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `1FC48224` | cd_seq_quest_hernand_1200 | 1/0 | 6 | …2 | 2:H/5 A:N/5 4:N/5 5:N/5 7:N/5 8:N/5 | 0 | - | - | 6/6 | ROUTED 5 | created 5 | sí (2) |
| quest | `FDA00E61` | cd_seq_quest_hernand_1300 | 1/0 | 6 | …2 | 2:H/5 A:N/5 4:N/5 5:N/5 7:N/5 8:N/5 | 0 | - | - | 6/6 | ROUTED 5 | created 5 | sí (2) |
| quest | `1B34BB42` | cd_seq_quest_hernand_2400 | 1/0 | 3 | …2 | 2:H/5 4:N/5 5:N/5 | 0 | 287C200: 16, after-cap: 9, NOT-287C200: 2 | - | 36/16 | ROUTED 36 | created 36 | sí (4) |
| quest | `1616DA5E` | cd_seq_quest_hernand_3800 | 1/0 | 6 | …1 | 1:H/5 5:N/5 6:N/5 7:N/5 8:N/5 9:N/5 | 0 | - | - | 1/1 | ROUTED 1 | created 1 | - |
| quest | `59A69304` | cd_seq_quest_hernand_4900 | 1/0 | 2 | …1 | 1:H/5 3:N/5 | 0 | NOT-287C200: 1 | - | 2/2 | ROUTED 2 | created 2 | - |
| quest | `AD854E2E` | cd_seq_quest_hernand_safezone_2500 | 1/0 | 0 | - | (none) | 0 | 287C200: 10, after-cap: 4, NOT-287C200: 2 | - | 21/13 | ROUTED 21 | created 21 | sí (15) |
| quest | `519CC9E7` | cd_seq_quest_marnidragon_boss_7610 | 1/0 | 19 | …1 | 1:H/5 3:N/5 4:N/5 5:N/5 6:N/5 7:N/5 | 0 | 287C200: 11 | - | 35/30 | ROUTED 36 | created 36 | sí (21) |
| quest | `E61D7F9` | cd_seq_quest_progression_call_0100 | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | 287C200: 2 | WOULD VETO SequencerCamera | 30/22 | ROUTED 28 | created 28 | sí (5) |
| quest | `DBF74FA` | cd_seq_quest_propagation_information_0100 | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | - | VETOED UIShowMinimap | 3/2 | ROUTED 3 | created 3 | - |
| quest | `DECD890C` | cd_seq_quest_spawnmainroad_treasurebox_0100 | - | 2 | …1 | 1:H/5 3:N/5 | 0 1 | - | - | 1/1 | ROUTED 1 | created 1 | - |
| quest | `C4E5A00C` | cd_seq_quest_splithorn_boss_0100 | 1/0 | 8 | …3 | 3:H/5 C:N/5 5:N/5 7:N/5 8:N/5 9:N/5 B... | 0 | - | - | 23/17 | ROUTED 22 | created 21 REJECTED 1 | sí (7) |
| quest | `BD40FBCF` | cd_seq_quest_splithorn_boss_0100_01_dlc | 1/0 | 3 | …1 | 1:H/5 3:N/5 4:N/5 | 0 | - | - | 26/19 | ROUTED 25 | created 25 | sí (11) |
| onetimequest | `DC722956` | cd_seq_onetimequest_hernand_03020 | 1/0 | 4 | …1 | 1:H/5 3:N/5 4:N/5 6:N/5 | 0 | - | - | 26/19 | ROUTED 25 | created 25 | sí (11) |
| node | `75B077C` | cd_seq_node_her_bluemontmanor_0300 | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | - | VETOED UIShowMinimap | 23/17 | ROUTED 22 | created 22 | sí (7) |
| node | `28F597B5` | cd_seq_node_her_bluemontmanor_0400 | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | - | VETOED UIShowMinimap | 5/3 | ROUTED 2 | created 2 | - |
| node | `94AD94C1` | cd_seq_node_her_hernandcastle_0200_closefilter | 1/0 | 0 | - | (none) | 0 | - | - | 14/11 | ROUTED 13 | created 13 | sí (6) |
| node | `3ED6A56` | cd_seq_node_her_hernandcastle_nature_call_0100 | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | - | VETOED UIShowMinimap | 25/19 | ROUTED 23 | created 23 | sí (5) |
| contents | `563BF30D` | cd_seq_contents_wanted_her_0010 | 1/0 | 2 | …2 | 2:H/5 4:N/5 | 0 | - | VETOED UIShowMinimap | 15/11 | ROUTED 13 | created 13 | sí (6) |
| contents | `212CFE3E` | cd_seq_contents_wanted_her_0010_02 | 1/0 | 0 | - | (none) | 0 | - | - | 15/11 | ROUTED 13 | created 13 | sí (6) |
| contents_weather | `?` | cd_seq_contents_weather_ | - | 1 | …1 | 1:H/5 |  | - | - | 3/1 | - | created 3 | - |
| contents_weather | `1CD9E76E` | cd_seq_contents_weather_cloudy_0000 | 1/0 | 1 | …1 | 1:H/5 | 0 1 | after-cap: 1 | - | 8/4 | ROUTED 8 | created 8 | sí (4) |
| contents_weather | `6B74F552` | cd_seq_contents_weather_cloudy_0002 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | after-cap: 1, 287C200: 1 | - | 9/9 | ROUTED 10 | created 10 | sí (5) |
| contents_weather | `4272500D` | cd_seq_contents_weather_cloudy_0003 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 5, NOT-287C200: 2, after-cap: 1 | - | 22/16 | ROUTED 22 | created 22 | sí (12) |
| contents_weather | `EE8E8B20` | cd_seq_contents_weather_cloudy_0004 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | after-cap: 1, 287C200: 6 | - | 27/17 | ROUTED 25 | created 25 | sí (19) |
| contents_weather | `B2DEA65` | cd_seq_contents_weather_cloudy_0005 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | - | - | 7/5 | ROUTED 7 | created 7 | sí (5) |
| contents_weather | `1051D443` | cd_seq_contents_weather_cloudy_0006 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 7, NOT-287C200: 2, after-cap: 1 | - | 13/7 | ROUTED 13 | created 13 | sí (5) |
| contents_weather | `CD952BCB` | cd_seq_contents_weather_cloudy_0007 | 1/0 | 1 | …2 | 2:H/5 | 0 1 2 | after-cap: 2 | - | 7/6 | ROUTED 6 | created 6 | - |
| contents_weather | `DDF27ABB` | cd_seq_contents_weather_rainy_0002 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | - | - | 6/4 | ROUTED 6 | created 6 | - |
| contents_weather | `18B03717` | cd_seq_contents_weather_rainy_0003 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | - | - | 3/3 | ROUTED 3 | created 3 | - |
| contents_weather | `9CA42F38` | cd_seq_contents_weather_sunny_001 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 9, after-cap: 2, NOT-287C200: 1 | - | 37/26 | ROUTED 36 | created 36 | sí (24) |
| contents_weather | `55CCB243` | cd_seq_contents_weather_wind_0001 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | after-cap: 2, 287C200: 1 | - | 8/8 | ROUTED 7 | created 7 | sí (2) |
| contents_weather | `A6FA186E` | cd_seq_contents_weather_wind_0002 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 7, after-cap: 2, NOT-287C200: 1 | - | 33/26 | ROUTED 32 | created 32 | sí (18) |
| contents_weather | `C60EAD52` | cd_seq_contents_weather_wind_0003 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 10, NOT-287C200: 1, after-cap: 4 | - | 36/24 | ROUTED 36 | created 36 | sí (19) |
| contents_weather | `21885337` | cd_seq_contents_weather_wind_0004 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | after-cap: 4, 287C200: 1 | - | 22/18 | ROUTED 21 | created 21 | sí (3) |
| contents_weather | `43CEB070` | cd_seq_contents_weather_wind_0005 | 1/0 | 1 | …1 | 1:H/5 | 1 2 | - | - | 4/4 | ROUTED 4 | created 4 | sí (2) |
| contents_weather | `C8C2F356` | cd_seq_contents_weather_wind_0006 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 3, NOT-287C200: 1 | - | 17/11 | ROUTED 17 | created 17 | sí (6) |
| contents_weather | `13A55C51` | cd_seq_contents_weather_winddegree_0001 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | 287C200: 5, after-cap: 2, NOT-287C200: 1 | - | 26/20 | ROUTED 27 | created 27 | sí (14) |
| contents_weather | `CDD50893` | cd_seq_contents_weather_winddegree_0002 | 1/0 | 1 | …1 | 1:H/5 | 0 1 | 287C200: 4, NOT-287C200: 1 | - | 13/11 | ROUTED 12 | created 12 | sí (10) |
| contents_weather | `5C07DBC4` | cd_seq_contents_weather_winddegree_0003 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | after-cap: 1, 287C200: 4, NOT-287C200: 1 | - | 13/8 | ROUTED 13 | created 13 | sí (9) |
| contents_weather | `4BDBA293` | cd_seq_contents_weather_winddegree_0004 | 1/0 | 1 | …1 | 1:H/5 | 0 1 2 | - | - | 6/4 | ROUTED 6 | created 6 | - |
| contents_weather | `E5A60CD3` | cd_seq_contents_weather_winddegree_0005 | 1/0 | 1 | …1 | 1:H/5 | 0 1 | after-cap: 1, 287C200: 1 | - | 5/5 | ROUTED 5 | created 5 | sí (7) |
| contents_weather | `1DC703E8` | cd_seq_contents_weather_winddegree_0006 | 1/0 | 1 | …1 | 1:H/5 | 0 1 | 287C200: 6 | - | 11/6 | ROUTED 11 | created 11 | sí (10) |
| contents_weather | `502415B6` | cd_seq_contents_weather_winddegree_0007 | 1/0 | 1 | …1 | 1:H/5 | 1 | - | - | 2/2 | ROUTED 2 | created 2 | - |
| organization | `42B3F7EB` | cd_seq_organization_miseenscene_0001 | 1/0 | 2 | …1 | 1:H/5 3:N/5 | 0 1 | - | - | 28/20 | ROUTED 25 | created 25 | sí (5) |
| entrance | `1F2716A7` | cd_seq_entrance_her_castle_room | 1/0 | 2 | …1 | 1:H/5 3:N/5 | 0 | - | - | 3/2 | ROUTED 3 | created 3 | - |
| spawn | `2BC74C06` | cd_seq_spawn_auto_animal_bush_bird | 10/1 | 4 | …1 | 1:H/5 3:N/5 4:N/5 5:N/5 | 0 1 | after-cap: 2, NOT-287C200: 8 | - | 41/24 | ROUTED 39 | created 39 | sí (9) |
| cd | `FA99EF6B` | cd_m05_m04_monster_puzzle_00_0002_01 | 1/0 | 2 | …1 | 1:H/5 3:N/5 | 0 | 287C200: 10 | - | 33/30 | ROUTED 34 | created 34 | sí (21) |
| cd | `D207FE50` | cd_m07_m04_npc_etc_00_0007 | 1/0 | 1 | - | 3:N/5 | 0 | 287C200: 10, NOT-287C200: 1 | - | 35/30 | ROUTED 36 | created 36 | sí (21) |

Notas a la tabla:

- `contents_weather` con recurso `?`: 3 ocurrencias, de logs sin captura y sin la línea de ruta. Por el nombre de etapa
  de `GuestXVeto` es una escena del tiempo.
- `quest_spawnmainroad_treasurebox` sin `b17/18`: solo log.
- Los veredictos pueden sumar más que las ocurrencias: hay un 2661 repetido (instantánea + en vivo).
- Las líneas de log de cada caso están en `out\occurrences.csv` (`log_line` y `route_line`).

---

## 4. Lo que hizo el mod y lo que se vio

### 4.1 `0x9586FFA4` `cd_seq_boss_rematch_matthias`: pose congelada [V correlación, E causa]

| Sesión | Secuenciador | Llegada → 2810 | Enlaces (`@55` = ranura 4) | Mod | Marca |
|---|---|---|---|---|---|
| `pose-anfitrion` (`cdmp.guest_20261015-083511.log` L4448 / L4980) | 0x2B1 | 08:37:50.9 → 08:38:21.5 | `4→B0100662`, `2→A0100001`, `6→B010069A` | ROUTED, created; XVeto UIStageGuide VETOED, BlockInteraction on/off WOULD | — |
| ídem (L16094 / L16338) | **0x440** | 08:38:34.5 → **sin 2810** | `4→B01010A3`, `2→A0100001`, `6→B01010C9` | ROUTED, created | **viva en M2 (08:51:50) y M3 (08:54:13)**: pose |
| `s43-activo\guest.log` (L17869 / L17994) | 0x2F1 | 20:08:25.2 → 20:08:36.4 | `4→B0100C07`, `2→A0100001`, `6→B0100CA1` | ROUTED, created; XVeto L18003-18004 | — |
| ídem (L27073 / L27214) | **0x464** | 20:08:54.5 → **sin 2810** | `4→B0101214`, `2→A0100001`, `6→B0101313` | ROUTED, created | **viva en M1 (20:10:24)**: pose |
| `s43-pose\guest.log` (L18025 / L18088 … L45027 / L45116) | 0x301, 0x303–0x30C, 0x48D (12) | 20:43:23 – 20:49:58 | la misma forma | **DENIED** ×12 (`kind set to 0, nothing created in M`) | sin pose (`s43-resultado-vivo.md`) |

- Lado del anfitrión: 12, 2 y 2 `SceneDiag` en las tres sesiones (`s43-pose\host.log`, `s43-activo\host.log`,
  `cdmp.host_20261015-083454.log`), todas `SENT` a `user 2`. Más 2 en `u1-local-host-travel.log`, una prueba local con peer.
- `s43-resultado-vivo.md` dice «15 veces». **Los datos dan 12 2661 [V]**: 12 líneas `DENIED` y 12 `SceneDiag` en el
  anfitrión.
- Rasgos únicos de esta escena entre las 86:
  - `@17/@18 = 2/2`;
  - etapa = `clave del NPC de la ranura 4 << 32 | 0x135`;
  - `@55` = ese NPC;
  - sin posición;
  - **no la creó el camino de «stage element» `0x287C200` en el anfitrión**: 13 de 13 medibles `NOT-287C200` (§5.3).

  El NPC de la ranura 4 cambia en cada instancia, porque el servidor lo genera de nuevo en cada una. En `s43-activo` es
  el «Game Mechanic» `B0101214` (`s43-datos/pose-informe.md` §1.1), y la ranura 6 es Matthias.
- Que la causa de la pose sea esta escena es [E]: correlación 2/2 frente a 0/1 con la escena denegada. El mecanismo
  sigue sin medir (`pose-informe.md` §3, afirmación 6).

### 4.2 `0x8CD771B1` `cd_seq_faction_bluemontwinehouse_loading`: qué es y por qué se denegó [V]

- **Nombre:** `cd_seq_faction_bluemontwinehouse_loading` (`sequencer/…paseq|.paseqc|.pastage`). Es la cinemática de
  «carga» de la facción de la bodega de Bluemont, la liberación del campamento.
- **Payload:** tipo **`1/0`**, como las escenas inofensivas:
  - etapa `0xF4568`, un id normal;
  - un solo enlace, `0x20000002→A0100001`, igual que `weather_cloudy_0007` o `use_sealedartifact`;
  - sin posición.
- **Sesión 9 de Shadow** (`p8-sesion9-shadow\cdmp_packets_20261010-151331\cdmp.log`, sin lista de denegación):
  - L7536: 2661 de `0x213`, a las 15:26:01.39;
  - L7581: ROUTED a X;
  - L7534, L7545 y L7640: 2981 de estados 0, 1 y 2 enrutados a X;
  - **el estado 2 llega a las 15:26:07.608 y en el mismo instante `GuestDiag loading transition: [P+0x15C] 0x18 → 0x01`
    (15:26:07.609)**: el cliente del invitado empieza a recargar el campo;
  - según `p8-s9-cinematica.md`, después vinieron 2749/2437, el servidor sacó al usuario 2 y el juego volvió al título;
  - marca 3 a las 15:28:08 (la escena seguía «viva» porque nunca llegó su 2810).
- **Por qué se denegó** (`p8-impl-escenas-play.md` §15.4):
  - no se encontró ninguna bandera que la distinguiera, así que se añadió una lista de recursos
    (`guest_seq_route_deny=8CD771B1`, en todos los inis del invitado desde la sesión 11);
  - el veto de raíz del escritor de la recarga (A1 de `p8-s9-cinematica.md`) quedó pendiente.
- **Sesión 11 con la lista** (`cdmp_20261010-191512.log` L8520 / L8542):
  - `0x231` DENIED a las 19:27:23;
  - le llegan 2981 de estados 0–5 (captura), que se quedan en M;
  - 2810 a las 19:27:47;
  - **ninguna `loading transition` entre 19:20 y 19:32 [V]**.
- **Sesión 10** (primera captura, `cdmp_packets_20261010-173057.bin`, sin log):
  - `0x245`, estados 2981 de 0 a 5;
  - no se sabe qué hizo el mod.
- **Lo que distingue a esta escena** de las 1 080 de tipo `1/0`, en los datos del 2661: **nada**.
  - Su `@17/@18`, el kind, el tipo de enlace y la ranura son los mismos.
  - Solo cambian el recurso y la etapa: confirma el §15.4 *para este caso*.
  - Lo único propio, y solo después de crearse, es que su 2981 sube hasta el estado 5 y el estado 2 arranca la recarga.
  - Ninguna otra escena del anfitrión pasó del estado 3, salvo las tiendas en uso (7 y 14).

### 4.3 Tiendas en uso por el anfitrión [V]

- `funcnpc_smithy`, `_butcher` y `_grocery`: cuando el anfitrión las usa, su secuenciador persistente recibe 2981 de
  estado 1, 2, 3 y 7 (14 en `craftsmith`), y en X se disparan:
  - `InputBlock` y `UIControl`: vetados;
  - `BlockInteraction` y `ChangePhasePanelTag`: «would».
- Ejemplos: `mov2-D1\guest.log` a las 07:07:25.640 (smithy, key 0x2A7) y `tenderos-p1b\guest.log` L12720 (14:38:41.5,
  smithy 0x293).
- Fuera de ese uso, la tienda solo dispara `SetQuestDialogAICategory` (would), `SetInteraction add/remove` (permitido) y
  UIStageGuide.
- **Consecuencia [E]:** la misma escena (el mismo recurso y secuenciador) es «del mundo» mientras está en reposo y
  «personal» en la rama de uso. Clasificarla solo por el 2661 no basta: hacen falta también los 2981 y los 2524.

### 4.4 Otras escenas con efecto en X (ninguna reportada como visible) [V]

- `reward_spear_base` (`1/0`, ranuras `1:H 3:N`): **TimelineEvent VETOED** en las 33 ocurrencias (por ejemplo,
  `s43-probe\guest.log` L2624), más UIShowMinimap en 12. Es la recompensa de la lanza, una escena personal por nombre.
- `quest_progression_call_0100` (`1/0`, `2:H 4:N`): **SequencerCamera WOULD** en 13 de 30 ocurrencias
  (`p8-sesion17-shadow\cdmp_20261011-140408.log`).
- UIShowMinimap vetado:
  - `node_her_bluemontmanor_0300/0400`;
  - `node_her_hernandcastle_nature_call_0100`;
  - `contents_wanted_her_0010`;
  - `quest_carrierpigeon_abyssteleport_0100`;
  - `quest_propagation_information_0100`.
- `quest_crime_police_wakeup_prison_hernand_0100`: UIStageIcon WOULD.
- Nada de esto aparece en ningún informe del usuario. Los vetos hacen su trabajo, o el efecto pasó desapercibido.

### 4.5 Marcas Ctrl+F12

| Sesión del invitado | Marcas | Escenas cortas vivas en la marca | Lo reportado |
|---|---|---|---|
| `pose-anfitrion` | M1 08:37:19.9 (invitado aún cargando), M2 08:51:50.4, M3 08:54:13.9 | **`boss_rematch_matthias` 0x440** | pose junto al mendigo / Matthias |
| `s43-activo` | M1 20:10:24.2 | **`boss_rematch_matthias` 0x464** | pose |
| `s9-shadow` 151331 | M3 15:28:09 | `faction_bluemontwinehouse_loading` 0x213 (sin 2810) | recarga / título |
| el resto | `s42-probe` M1–M8, `s43-probe` M1–M4, `tenderos-p1b` M1–M5, `tenderos-p2b` M1–M2, `enemigos` M1–M5, `carros*`, `s8`/`s11`/`s13`/`s17`/`s34`/`s38` | solo las persistentes (tiendas, minijuegos, tiempo, misiones de zona); `memory_bluemontmanoralley` en `s13` M1 | nada relacionado con escenas del anfitrión (no busqué todos los informes de esas marcas) |

---

## 5. Familias y qué campos las separan

### 5.1 Por familia [V]

| Familia (recursos) | `@17/@18` | Ranura del anfitrión | NPC enlazados | Etapa `@19` | Camino del anfitrión | 2981 | Clase por el contenido (nombre, solo como referencia) |
|---|---|---|---|---|---|---|---|
| `contents_weather_*` (24) | 1/0 | `0x20000001` (`cloudy_0007`: `0x20000002`) | 0 | id 0xF48xx–0xF4Cxx / 0x13106xx | 287C200 | 0–2 | mundo (efecto sobre el jugador) |
| `funcnpc_*` (16) | 1/0 | **`0x20000002`** | 1–3 | id (0xF4…, 0x4193xxxx, 0x5DA6xxx) | 287C200 | 0; en uso 1, 2, 3, 7, 14 | mundo en reposo / personal en uso |
| `minigame_*` (5) | 1/0 | `0x20000002` (`seotda`: no) | 0–23 | id | 287C200 | 0 | estación del mundo; personal si se juega [H] |
| `quest_*` (22) + `onetimequest` | 1/0 | 1, 2 o 3 | 0–18 | id 0xF4…–0xFB…, 0x5A9Bxxx, 0x170xxxx | 287C200 casi siempre; **NOT** en `demeniss_*`, `hernand_4900`, `bloodcoronation` | 0 (1 en `carrierpigeon` y `treasurebox`) | puesta en escena de misión en el mundo |
| `node_*` (4) | 1/0 | `0x20000002` o ninguna | 0–1 | id | (sin medida) | 0 | mundo |
| `contents_wanted_*` (2), `organization`, `entrance`, `m05`/`m07` | 1/0 | 1, 2 o ninguna | 0–1 | id | 287C200 | 0–1 | mundo |
| `reward_spear_base` | 1/0 | `0x20000001` | 1 | id 0xF442B | 287C200 | 0 | **personal** (TimelineEvent) |
| `faction_bluemontwinehouse_loading` | 1/0 | `0x20000002` | 0 | id 0xF4568 | (sin medida) | **0–5** | **personal** (recarga del campo) |
| `memory_*` (3) | **6/3** | `0x20000003` | 2–12 | id 0xF5E91–0xF6611 | (sin medida) | 2–3 | personal (recuerdo) |
| `use_sealedartifact_0001` | **6/3** | `0x20000002` | 0 | id 0xF675F; **`@55` = jugador** | (sin medida) | 0 | **personal**, y también existe para el invitado con su propia clave |
| `boss_rematch_matthias` | **2/2** | `0x20000002` | 2 | **clave NPC « 32 \| 0x135** | **NOT 287C200** | 0 | **personal** (pose) |
| `spawn_auto_animal_bush_bird` | **10/1** | `0x20000001` | 3 (fauna) | `0x3E9/0x3EA « 32 \| n` | NOT 287C200 | 0–1 | ambiente; existe para el invitado con su clave |
| `spawn_docu_*` | — | — | — | — | — | — | **no son 2661 en el invitado**: son horarios 2335 (`GuestSchedLoad … cd_seq_spawn_docu_npc_small_0001.paschedule`, `s43-activo\guest.log`). Tres `spawn_docu_animal_*` 2661 solo en sesiones de anfitrión sin log del invitado (§6) |

No apareció ninguna familia `cutscene` ni `cd_seq_reward_*` aparte de la lanza. La «cinemática» es la `faction_*_loading`.

### 5.2 Respuesta directa: ¿hay escenas del mundo que enlazan al anfitrión con `0x20000002`?

**Sí, la mayoría [V].** Llevan al anfitrión en `0x20000002`, o en la ranura 1 o 3, y ningún dato del 2661 las distingue
de la revancha salvo `@17/@18`, la forma de la etapa y `@55`:

- las 16 tiendas;
- 4 minijuegos;
- 3 `node_*`;
- `contents_wanted_her_0010`;
- `weather_cloudy_0007`;
- `quest_hernand_1200/1300/2400`;
- `quest_progression_call_0100`;
- `quest_propagation_information_0100`;
- `quest_demeniss_0200/south_2100/2200`;
- `quest_crime_police_wakeup_prison_hernand_0100`.

**Ranura del anfitrión en las 86** [V]:

| Ranura | Recursos |
|---|---|
| ninguna | 5 |
| `0x20000001` | 40 |
| `0x20000002` | 37 |
| `0x20000003` | 4 |

### 5.3 Tabla de verdad candidata

Los datos se pueden leer en el cliente del invitado, en el payload del 2661, antes de crear la escena:

- **A** = `@17/@18 ≠ 1/0` (y ≠ `10/1`);
- **B** = la etapa `@19` lleva una clave de actor en su dword alto (`(stage >> 60) == 0xB`);
- **C** = `@55 ≠ 0`.

Otros datos, que no están en el 2661 o solo se conocen después:

- **D** = la preparó el camino de «stage element» `0x287C200` en el anfitrión (solo medible en el anfitrión con
  `ShopProbe`);
- **E** = un 2981 de estado ≥ 3 llega a X;
- **F** = X dispara control o cámara: InputBlock, UIControl, SequencerCamera, TimelineEvent o BlockInteraction.

| Escena | A | B | C | D | E | F | Efecto visto | Personal (contenido) |
|---|---|---|---|---|---|---|---|---|
| `boss_rematch_matthias` | **1** | **1** | **1** (NPC) | **0** (13/13) | 0 | 1 (BlockInteraction) | **pose** | sí |
| `use_sealedartifact_0001` | **1** | 0 | **1** (jugador) | ? | 0 | 0 | ninguno | sí |
| `memory_*` | **1** | 0 | 0 (`@59` = NPC) | ? | 0 | 0 | ninguno | sí |
| `spawn_auto_animal_bush_bird` | 0 (`10/1`) | 0 (hi = 0x3E9/0x3EA) | 0 | **0** (8/8) | 0 | 0 | ninguno | no (ambiente por jugador) |
| `faction_bluemontwinehouse_loading` | 0 | 0 | 0 | ? | **1** | 0 | **recarga del campo** | sí |
| `reward_spear_base` | 0 | 0 | 0 | 1 | 0 | **1** (TimelineEvent) | ninguno | sí |
| `quest_progression_call_0100` | 0 | 0 | 0 | 1 | 0 | **1** (SequencerCamera, would) | ninguno | ¿? |
| `funcnpc_*` en reposo | 0 | 0 | 0 | 1 | 0 | 0 | ninguno (los NPC se ven) | no |
| `funcnpc_*` en uso por el anfitrión | 0 | 0 | 0 | 1 | **1** (7, 14) | **1** (InputBlock, UIControl) | ninguno | sí (la rama) |
| tiempo, minijuegos, misiones de zona, `node`, `wanted`… | 0 | 0 | 0 | 1 (o sin medida) | 0 | 0 o UIShowMinimap | ninguno | no |
| `quest_demeniss_*`, `hernand_4900`, `bloodcoronation` | 0 | 0 | 0 | **0** | 0 | 0 | ninguno | ¿? (misión) |

Lectura:

- **A ∨ B ∨ C** separa sin falsos positivos las escenas «disparadas por un jugador o un NPC» (revancha, objeto, recuerdo)
  de las que salen de una etapa (stage element) [V en las 86].
  - Pero **no** atrapa la cinemática de carga ni la recompensa, que son de tipo `1/0` por el camino normal.
- Ningún campo del 2661 separa `faction_*_loading` de `weather_cloudy_0007` [V]: misma forma y el mismo enlace único
  `2:H`.
  - Para esa clase hace falta un dato del recurso (`.paseq`/`.pastage` o `stageinfo`) o del comportamiento (2981 / vetos),
    a medir en 3.2.
- **D** (el camino de creación en el servidor) apunta en la misma dirección que A para la revancha y `spawn_auto`.
  - Pero también marca `NOT` algunas misiones (`demeniss_*`, `hernand_4900`) que no hicieron nada visible.
  - Y la cobertura de `ShopProbe` está cortada a 24 líneas `[stage]` por sesión: medida parcial, [E].

---

## 6. Escenas que el anfitrión envió sin log del invitado (sesiones 23–33 y locales) [V en el envío, E en la llegada]

`out\host_side.csv` (`ObserverSeqCopy [copy] … SENT`). Las que no aparecen en ningún log ni captura del invitado:

- **Tiempo:** `contents_weather_cloudy_0001` (`0x7DC42C04`).
- **Tiendas:**
  - `funcnpc_furniture` (`0xF3164FCD`, ×7, sesiones 25–29);
  - `funcnpc_stonestore_stallholder` (`0x3ED641E4`);
  - `funcnpc_witchstore_modesty` (`0xDBBE72AE`).

  Todas con `2:H 4:N`.
- **Nodos:**
  - `node_her_hernandcastle_daily_0100` (`0x8D7A852F`, ×12, `2:H` + 5 NPC);
  - `node_her_hernandcastle_daily_1000` (`0xA6EEB941`).
- **Misión:** `quest_hernand_1100` (`0xA25AAD45`).
- **Facción:** `faction_bleed_ruffians_pervin_0320_closefilter` (`0x54E15059`, sin enlaces).
- **Animales** (`1:H 3:N`, sesiones 25 y 28):
  - `spawn_doc_animal_fish_jump_00` (`0xC75F8232`);
  - `spawn_docu_animal_eagle_0001` (`0xF2F97639`);
  - `spawn_docu_animal_sparrow_0001` (`0xA239059B`).

Preparadas por el anfitrión y **nunca enviadas** (sin copia): `cd_quest_documentary_pailoon_*` (7),
`funcnpc_dye/fishing/smuggler`, `memory_alustin_journal_0003/0014`, `node_pailune*_closefilter`, `quest_black_wall_3000`.

---

## 7. Contraste: escenas cuyo dueño es el invitado (y NPC)

- **Dueño NPC: ninguna [V]** (0 de 1 635). Las escenas de los NPC son 2661 con dueño = un personaje de jugador y los NPC
  en las ranuras.
- **Dueño invitado:** 109 2661 en 29 ejecuciones (más 6 de capturas sin log). Las con nombre:
  - `spawn_auto_animal_bush_bird` (`10/1`, `1:G 3:N 4:N 5:N`):
    - 52 en 10 sesiones;
    - **las 52 `REJECTED`**: la fauna no estaba en el cliente (`GuestStage [2661 missing actor]`; por ejemplo,
      `s42-probe\guest.log` L38518);
  - `use_sealedartifact_0001` (`6/3`, `2:G`, `@55` = la clave del invitado): 15, todas `created`. Por ejemplo,
    `p8-sesion12-shadow\cdmp_20261010-215406.log` L3430;
  - **`funcnpc_smithy` ×2 y `funcnpc_butcher` ×1** en `tenderos-p2b\guest.log` (L9560, L9566, L19676):
    - `2:G` + el tendero;
    - **la misma etapa `0x41934310` y la misma posición que la copia del anfitrión**;
    - 2 `REJECTED` (el tendero `B01002EC`/`B01002EB` aún no estaba) y 1 `created`.
  - Las demás (39) son de logs sin captura y sin línea de ruta (el mod no enruta las propias), así que no tienen recurso.
    Sus firmas son `2:G 4:N` (×12), `1:G 3:N 4:N 5:N` (×7), `1:G` (×4)… y su creación: 21 `created`, 18 `REJECTED`.
- **El mod** no toca las propias: van nativas a M, sin `GuestSeqRoute`.
- **Lo que enseñan [E]:**
  - el servidor da a **cada jugador** su propia instancia de las escenas de etapa (tienda, objeto, fauna), con ese
    jugador en la ranura de jugador;
  - la instancia del anfitrión que el mod reproduce en el invitado es la que el juego nativo nunca enviaría a un tercero;
  - el equivalente nativo para el invitado es su propia instancia, que hoy U-1/U-f del anfitrión suprime casi siempre
    (`User2Stages [element] … SKIPPED`, `tenderos-p2b\host.log`).

---

## 8. Lo que no está demostrado y siguientes medidas

- **El significado de `@17/@18`** (¿origen de la etapa: etapa / función de NPC / objeto o recuerdo / spawn?) es [E].
  - Está correlacionado con el nombre en 86 de 86 recursos, pero no se ha leído el código que lo escribe ni el que lo lee.
  - Para 3.2: buscar en el serializador del 2661 en el servidor (el escritor del play data de 86 bytes) y en
    `0xA9CA40` qué hace el cliente con esos bytes.
- **Si `0x135` es un id de función o interacción del NPC [H].**
- **El camino `0x287C200` como discriminador [E]:**
  - el log `ShopProbe [stage]` se corta a 24 líneas por sesión;
  - algunos `NOT` del tiempo pueden ser pérdidas del log;
  - hace falta una sonda que cuente por secuenciador, sin tope.
- **La cinemática de carga y la recompensa no se distinguen por el 2661 [V].**
  - Candidatos nativos para 3.2: la entrada de `gamedata/stageinfo.staticinfobody` (`0.paz`, 28 MB; índice en
    `build\research\arrow\index.tsv`) para la etapa `@19` (`0xF4568`, `0xF442B`…), o las pistas del `.paseq` de la ranura
    del jugador.
  - No se ha extraído ninguno.
- **Qué pista pone la pose** en la revancha: sigue sin medir (`pose-informe.md` §4.2).
- **Las marcas de otras sesiones:** no he cruzado los informes del usuario de todas las marcas, solo las de pose y
  recarga.
- **La llegada al invitado** en las sesiones 23–33: solo consta que el anfitrión las envió.
- **Las tiendas** son instancias por jugador: el formato es idéntico [V], pero el significado es [E].
