# P8: implementación del permiso de rango y de los embudos por usuario (A, B, C)

10 de octubre de 2026, rama `opcion-d`. Build del juego 1.0.0.2976 (timestamp `0x6AB28F00`, base `0x140000000`);
direcciones en **RVA**. Diseño de partida: `p8-sistemas-juego.md` §11.1 (rango) y §11.7 (embudos), aprobado por el
usuario. Sin lanzar el juego, sin desplegar y sin commit: compila y pasan los 28 tests (`scripts/build.ps1`).

## 1. Qué se hizo

| Pieza | Clave `[session2]` | Dónde corre | Qué cambia con `1` | Con `probe` |
|---|---|---|---|---|
| **A** permiso de rango | `rank_permit` | cliente del anfitrión (hilos de IA) | `vf188(5)` del Kliff del usuario 2 responde 1 en `0x221E281` (antes de `GrantRankPermit 0x2220850`) y en `0x221F29F` (antes de quitar el permiso, `0x2220370`) | cuenta cuántas veces se llega con X = usuario 2 y qué devolvió el original |
| **sujeto** (común a B/C) | se instala si `quest_per_user` o `kill_loot_per_user` ≠ 0 | servidor del anfitrión | `thread_local` "sujeto = personaje del usuario 2" mientras dura su Req (`ExecuteTroc 0x2774B20`) | igual (el sujeto no cambia nada por sí mismo) |
| **B** misiones | `quest_per_user` | servidor | `ServerField::vf40 0x2B00C70` devuelve el `ServerQuestManager` **del usuario 2** | cuenta, compara el del dueño con el del usuario 2 |
| **C** muerte y botín | `kill_loot_per_user` | servidor | `0x2AFFF50` desde `0x29432DE` (DeadActor 0x62) da el **personaje del usuario 2**; `0x20DEE60` desde `0x2B83D24` (destinatario del botín) da el **usuario 2** | cuenta qué recibió el original y qué habría recibido el usuario 2 |
| sonda común | — | servidor | — | por Req del usuario 2: `vf40`, `vf48 0x2B00CE0`, `FindUser 0x2A82590`, `0x2AFFF50 (ret 0x29432E3)`, `0x20DEE60 (ret 0x2B83D29)`, por opcode y por llamador |

Ficheros:

| Fichero | Qué |
|---|---|
| `src/mod/session2/user2_funnels_logic.h` | lógica pura: modos, decisión de rango, veredicto del sujeto, decisión de embudo, cadena del gestor de misiones sobre memoria abstracta, contadores sin bloqueo |
| `src/mod/session2/user2_funnels.{h,cpp}` | ganchos, parche de llamadas, hilo propio (claves cada 100 ms, eventos cada 1 s, resumen cada 60 s) |
| `tests/optd_user2_funnels_tests.cpp` | test nuevo (`optd_user2_funnels`, el 28.º) |
| `src/mod/config.{h,cpp}` | `s2_rank_permit`, `s2_quest_per_user`, `s2_kill_loot_per_user` (texto crudo, por defecto `"0"`) |
| `src/mod/mod.cpp` | arranque en el anfitrión (`if (session2) user2_funnels_start(...)`, junto a `ui_pause_start`) y la línea `Config:` |
| `CMakeLists.txt` | el `.cpp` en `cdmp_mod` y el test |
| `docs/research/ini/cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini` | las tres claves a `1`, con comentario |
| `docs/research/ini/cdmp.p8-s7-shadow.ini` | las tres a `0` explícito (en el invitado no se instala nada aunque valgan 1) |

La F5 (`src/mod/net/aggro_test.cpp`, entrada de `0x22E0A30`) no se tocó. En opción D no se instala (es del modelo de
copias); sigue de reserva.

**Cuándo se instala:** solo en el anfitrión (`session2` con `guest=0`) y con `create_session=1`. Igual que
`ui_pause`: espera a que el código coincida byte a byte (hasta 2 min) y a que el jugador local lleve 3 s en el mundo;
nunca durante el arranque ni en el título. Orden: A; después los embudos (sin sujeto solo llaman al original); el
sujeto (`ExecuteTroc`) el último. Si el sujeto no entra, los embudos no cambian nada y el log lo dice.

## 2. Direcciones y bytes verificados

Leídos con capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (Python `-I`), y comparados **otra vez en
el juego** antes de escribir (`.rsrc` según la tabla de secciones en memoria + bytes exactos; si algo no coincide:
`User2Funnels: ... REFUSED` y esa pieza queda apagada). Nada en `.xtls`. Un barrido de todos los `rel32`
(`E8`/`E9`/`0F 8x`) de `.rsrc`/`.xtls`/`.text`/`.xtext` y de los `rel8` cercanos no encontró ningún salto real que
caiga dentro de los bytes parcheados (los dos aciertos eran mitades de instrucción).

| Punto | Bytes comprobados | Parche | Notas |
|---|---|---|---|
| A1 `0x221E281` | contexto `0x221E279`, 17 B: `48 8B 03 B2 05 48 8B CB FF 90 88 01 00 00 0F B6 D8` | `FF 90 88 01 00 00` → `E8 rel32 90` | `rcx = rbx = [r14+8]` = X; se llama con X bloqueado (`0x393DF0` … `call r8`) |
| A2 `0x221F29F` | contexto `0x221F297`, 17 B: `48 8B 06 B2 05 48 8B CE FF 90 88 01 00 00 0F B6 D8` | igual | `rcx = rsi`; después usa `rsi` (no volátil: el thunk lo conserva) |
| sujeto `0x2774B20` | la función entera, 0x38 B (`48 89 4C 24 08 53 48 83 EC 20 4C 8B CA …`) | gancho en línea, 5 B (fronteras admitidas 5/6/10/13/16/22) | `ExecuteTroc(res, troc, params)`: solo dos llamadores (`0x27742A9`, `0x2774A08`), `params = rbx` del llamador; `try/catch` interno (continúa en `0x2774B4A`) |
| B `0x2B00C70` vf40 | la función entera, 0x67 B (`40 53 48 83 EC 40 44 8B 81 80 01 00 00 …`) | 6 B (6/13/18) | lee el campo **una sola vez** (`[rcx+0x180]`) |
| `0x2B00CE0` vf48 | la función entera, 0x67 B | 6 B | solo cuenta |
| C `0x2AFFF50` | la función entera, 0x164 B (`48 89 5C 24 08 48 89 74 24 18 48 89 54 24 10 55 57 …`) | 5 B (5/10/15/…) | el campo solo se lee en `0x2AFFF78` y no se guarda |
| llamador DeadActor | `0x29432D2`, 22 B: `48 8D 54 24 78 48 8B 8D 98 00 00 00 E8 6D CC 1B 00 90 80 7D 88 00` | — | retorno `0x29432E3` |
| C `0x20DEE60` | `E9 EB 09 1E 0C` (`jmp .xtls 0xE2BF850`) | exactamente 5 B (el stub de `.rsrc`) | se rechaza cualquier otro tamaño |
| llamador botín | `0x2B83D1C`, 18 B: `48 8D 55 40 48 8B 4D F0 E8 37 B1 55 FF 90 40 38 7D 50` | — | retorno `0x2B83D29` |
| `FindUser 0x2A82590` | 29 B: `44 89 44 24 18 48 89 54 24 10 53 55 56 57 41 56 48 83 EC 30 …` | 5 B, **solo con algún `probe`** | lo llama la sustitución del botín |
| global | `[[exe+0x6D696B8]]` = primer argumento de FindUser | — | sale del `mov rcx,[rip+…]` de vf40 (`0x2B00C82`) y de `0x2AFFF83`, ya verificados |
| RTTI | `ServerNormalInGameActor` vt `0x5B1DE20`, `ServerQuestActorComponent` `0x5B1F8F8`, `ServerQuestManager` `0x5B1FA48`, `ScopeAttacherBase` `0x5725E48` | — | comprobados por COL |

**Cómo se escribe A.** Ambos contextos verificados antes de tocar nada; un stub de 16 B a menos de 2 GB
(`safetyhook::Allocator::allocate_near`, `FF 25 00 00 00 00` + dirección del thunk); cada sitio con
`safetyhook::trap_threads` (la página deja de ser ejecutable mientras cambian los 6 B; un hilo que llegue espera) y
releído después. Si el segundo no se puede escribir, se restaura el primero (un permiso concedido que nunca se
quitaría sería peor que nada). El retorno de la nueva `call` cae en el `nop` (`0x221E286` / `0x221F2A4`) y sirve para
saber qué sitio llamó.

## 3. Cómo se reconoce al usuario 2

- **Claves** (las publica el hilo del módulo cada 100 ms en atómicos; los ganchos solo leen copias):
  `server_user2_user_key()` (su `MainUserDataStartAck`, `0x9…`), `server_user2_player_key()` (su
  `MainPlayerDataStartAck`, `0xA…`), la del anfitrión (`[user de la sesión 1 + 0x60]`) y "vivo" (`server_user2_present()`).
  Sin usuario 2 vivo o con alguna clave sin conocer, todo pasa al original sin leer nada.
- **A, cliente del anfitrión:** X es "el Kliff del usuario 2" si `u32 [X+0x60]` = su clave de personaje **y**
  `u32 [X+0x90]` (controlador) = su clave de usuario (lecturas `fast_read`, protegidas por SEH; la del controlador solo
  si la clave coincide). Ni el Kliff del anfitrión, ni NPC, ni el caballo del usuario 2 cumplen las dos. Con `1`,
  además, `[[X+0x68]+0x90]` (su componente de objetivo `C_X`) no puede ser nulo: `0x221E2B4` se lo pasa a
  `GrantRankPermit` sin comprobar.
- **B/C, servidor:** en `ExecuteTroc`, `[params]` es el personaje de la sesión; es el sujeto si su vtable es
  `ServerNormalInGameActor` y tiene las dos claves de arriba. El "campo compartido" se decide **en cada embudo** con el
  campo que se le pide: `u32 [field+0x180]` = clave del anfitrión. Si el dueño es el usuario 2 (su propio campo, tras un
  viaje), el original ya es correcto; si es otro, original.
- **Anidamiento:** el gancho guarda el sujeto anterior y lo restaura al volver (RAII), así que un `ExecuteTroc` dentro de
  otro decide el suyo.

## 4. Decisiones y desviaciones del diseño (leído el código)

1. **`0x2AFFF50` no devuelve un personaje: rellena un "handle" con bloqueo** (`out+8` personaje, `out+0x10` bloqueado,
   el personaje bloqueado con su `vfC0(4, 0x10)`, liberado por el llamador con `0x1434880`). Construirlo a mano
   desajustaría el bloqueo. Solución: se llama **al original con un campo del mod** (memoria estática del DLL, todo
   ceros salvo `+0x180` = clave del usuario 2). Como la función solo lee `[rcx+0x180]` (verificado en sus 0x164 B), el
   propio juego busca al usuario 2 y bloquea a su personaje, igual que hace con el anfitrión en una partida normal.
2. **`0x20DEE60` (cuerpo en `.xtls`) tampoco devuelve un usuario: rellena el mismo tipo de handle.** No se le pasa un
   campo falso (no se puede comprobar qué más lee el cuerpo ofuscado). Con sujeto se llama en su lugar a
   **`FindUser 0x2A82590([[exe+0x6D696B8]], out, clave del usuario 2)`**, la misma búsqueda y el mismo handle que usa
   vf40 (el llamador del botín lee `+8`/`+0x10` y lo libera con `0x1434880` sobre `ScopeAttacherBase 0x5725E48`, igual
   que vf40). El cuerpo de `.xtls` no se ejecuta en ese caso.
3. **vf40 según el diseño:** `[[[sujeto+0x68]+0x138]+0x18]`, con las dos vtables comprobadas; si algo falla, el
   original (y el motivo en el resumen).
4. **"Su campo es el compartido"** se evalúa en el embudo (`[field+0x180]`), no al entrar en la Req (no hay que llamar
   a `0x17FA6C0` ni leer el campo del actor).
5. **La cuenta de vf48** se instala siempre que B o C estén activos (`probe` o `1`), porque las ini de la próxima prueba
   van a `1` y es barata (un `thread_local` por llamada); **la de FindUser** solo con algún `probe` (es una función muy
   llamada en el servidor).
6. Lo que no se sostiene tal cual del §11.7: nada bloqueante. Ojo con el **riesgo nuevo R18** (§5): vf40 también lo
   llaman `0x2BC7670` (fin de viaje, el camino del 2322) y el revivir (`0x2947F76`, `0x2B03AA2`); dentro de una Req del
   usuario 2 usarán su gestor de misiones.

## 5. Riesgos

| Id | Riesgo | Mitigación |
|---|---|---|
| R1′ | A da rango a los enemigos sobre un actor remoto: `BattleRank`, `0x170D900`, `0x1EB2D10`, `0x221A940` ven ahora la lista llena para él. Lo esperado es "atacan como al jugador local", pero ningún camino nativo lo había hecho nunca con un remoto | `rank_permit=probe` primero si algo va raro; el resumen dice cuántas veces se respondió 1 |
| R1″ | Permisos huérfanos si el actor desaparece con permisos concedidos | se quitan por el mismo camino (`0x221F29F` también responde 1 para él); los vectores son del motor y se destruyen con el componente |
| R3′ | Las muertes del usuario 2 dejan de contar para el anfitrión (campamentos, contadores) y pasan a sus misiones | es lo buscado; probar con **partida desechable** |
| R17′ | DeadActor/botín solo se redirigen si la muerte se procesa **dentro** de la Req del usuario 2 (golpe → daño → muerte síncrona). Muertes por efectos periódicos o temporizadores siguen yendo al anfitrión | el resumen separa "bajo Req del usuario 2" y "en el llamador filtrado"; si `DeadActor` sale a 0 tras matar, la muerte no es síncrona |
| R18 | vf40 dentro de las Req del usuario 2 también afecta a `0x2BC7670` (fin de viaje; lee `+0x17C/+0x186/+0x188` del gestor y, solo si el viajero es el dueño, `0x2910DE0`) y al punto de reaparición. El 2322 del observador (`observer_milestone`) usa vf48/`0x2BC8470`, que no cambian | mirar en el resumen `vf40 … callers` si aparece `exe+0x2BC76C1`; si un viaje del usuario 2 se atasca, `quest_per_user=probe` |
| R19 | Si el bloqueo del usuario 2 (o de su personaje) falla en ese instante, el juego no da botín ni DeadActor (igual que haría con el dueño) | el evento lo muestra (`locked 0`) |
| R20 | Coste: `ExecuteTroc` se llama en **todas** las Req; con usuario 2 vivo, dos lecturas protegidas y unos atómicos por Req | despreciable frente a la Req; sin usuario 2, solo un atómico |
| R21 | Misiones del usuario 2 que esperan stages de **su** mundo (P9 §3) | fuera de alcance; sin vf48 por usuario las escenas siguen en el gestor del anfitrión |

## 6. Qué mirar en el log del anfitrión (`bin64\cdmp.log`)

Arranque (líneas exactas, valores de ejemplo):

```
Config: session2 (option D) ON | ... | P8 gameplay: rank permit '1' quest per user '1' kill loot per user '1'
User2Funnels: waiting for the local player in the world before hooking (never at start-up or on the title screen)
User2Funnels: rank_permit 1 ON: exe+0x221E281 and exe+0x221F29F call the thunk through the stub at 0x... (FF 90 88 01 00 00 -> E8 rel32 90); vf188(5) of user 2's main character answers 1
User2Funnels: quest_per_user 1 (vf40 hooked), kill_loot_per_user 1 (DeadActor 0x2AFFF50 hooked, loot 0x20DEE60 hooked) | subject at ExecuteTroc hooked | counters: vf48 hooked, FindUser off | patches 6 / 6 / 0 / 5 / 5 / 5 bytes
User2Funnels: user 2 LIVE (user 0x9010031E, character 0xA010031F; host user 0x90100000)
```

Cualquier `User2Funnels: ... REFUSED` = esa pieza no está (bytes distintos, fuera de `.rsrc`, parche de tamaño
inesperado). `... - a key is still unknown: nothing changes until it is` al entrar es normal si llega antes el
`LIVE` que el `MainPlayerDataStartAck`.

Durante la prueba (primeros 20 por sitio de A y 40 por embudo; después solo contadores):

```
User2Funnels rank [1] qpc ... : vf188(5) of user 2's character 0xA010031F at exe+0x221E281: original 0 -> 1 (answered 1)
User2Funnels vf40 qpc ...: Req 2112 of user 2 (0xA010031F) | field owner 0x90100000 | user 2's | returned user 2's ServerQuestManager 0x... | from exe+0x2C4A36F
User2Funnels dead qpc ...: Req 2380 of user 2 (0xA010031F) | field owner 0x90100000 | user 2's | DeadActor goes to character 0xA010031F (locked 1) | from exe+0x29432E3
User2Funnels loot qpc ...: Req 2380 of user 2 (0xA010031F) | field owner 0x90100000 | user 2's | the loot recipient is user 0x9010031E (locked 1) | from exe+0x2B83D29
```

Cada 60 s:

```
User2Funnels summary: rank_permit 1 (on): user 2 reached N, answered 1 N, would 0 | quest_per_user 1 / kill_loot_per_user 1 (subject on): user 2 Reqs N, vf40 N (user 2's N), DeadActor N (user 2's N), loot N (user 2's N) | user 2 LIVE | events lost 0
User2Funnels rank exe+0x221E281: calls N | user 2's character N (original 1: 0, original 0: N) | answered 1 N | would (probe) 0 | no target component 0 | not user 2 N | other dl 0
User2Funnels rank exe+0x221F29F: ...
User2Funnels subject: Reqs while user 2 is live N | of user 2 N (not user 2 N, user 2 N) | top Req opcodes of user 2: 2556xN 2223xN ...
User2Funnels vf40 0x2B00C70 (quest manager): under user 2's Reqs N, at the filtered caller N | user 2's N | ... | Req opcodes ... | callers exe+0x...xN
User2Funnels vf48 0x2B00CE0 (sequencer manager, count only): ...
User2Funnels 0x2AFFF50 from 0x29432DE (DeadActor 0x62): ... | other callers under user 2's Reqs exe+0x...xN
User2Funnels 0x20DEE60 from 0x2B83D24 (loot recipient): ...
```

Lecturas clave: en A, `original 0` alto y `answered 1` igual (si `no target component` crece, el Kliff remoto no
tiene `C`); en `subject`, `user 2 N` > 0 (si sale `user 2's keys on another class` o `unreadable`, el sujeto no se
reconoce); en DeadActor/botín, `at the filtered caller` > 0 tras matar algo.

## 7. Plan de prueba (con Shadow; kit de la sesión 7 + estas tres claves a `1`)

Antes: copia de la carpeta de partidas del anfitrión; para G9, **partida desechable** del anfitrión. Sniffer con
`record=1` en los dos PC.

1. **G1 combate (rango).** Invitado junto a un grupo de enemigos débiles; el anfitrión **no** ataca y se aparta unos
   metros. Esperado: los enemigos atacan al invitado con ataques normales (antes: rodear sin atacar). Log: `rank …
   answered 1` y en el resumen `answered 1` creciendo; HBC: `DoFrameEventReq` de enemigos con objetivo `0xA010031F`
   (comparar ataques por minuto con el 0 de la marioneta). Si algo se ve raro: repetir con `rank_permit=probe`.
2. **G1b muerte del enemigo por el invitado.** El invitado mata a uno. Esperado: líneas `dead … DeadActor goes to
   character 0xA010031F (locked 1)` y `loot … the loot recipient is user 0x9010031E (locked 1)`; botín en el suelo;
   GBC `AddSubLevelExperienceAck` 2381 como antes; si es campamento, `UpdateQuestGaugeAck` al **invitado** (GBC), no al
   anfitrión (HBC). Recoger el botín (G2).
3. **G1c muerte por el anfitrión.** El anfitrión mata otro. Esperado: nada en `dead`/`loot` (no hay sujeto): todo al
   anfitrión como siempre.
4. **G9 misiones (partida desechable).** El invitado entra en una zona de misión **suya** y luego en una del
   anfitrión; cambia el seguimiento. Esperado: `vf40 … Req 2112 …` / `2372` con `user 2's`; la misión del anfitrión ya
   **no** avanza por el invitado; la suya, si su mundo lo permite (R21). **No guardar.**
5. **G6 muerte y reaparición del invitado.** Mirar `vf40 … callers` (`exe+0x2947F7B`, `exe+0x2B03AA7`) y que el viaje
   termine (`ObserverMilestone`); si se atasca, `quest_per_user=probe` y repetir.
6. Al terminar: el último resumen de 60 s, el `cdmp.log` del anfitrión y las dos capturas.

Fallo grave (cierre, atasco): poner las tres claves a `0` (nada se instala) y avisar con el log.
