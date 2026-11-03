# P8 sesión 14: el viaje rápido del invitado y el «cierre» del final

Build 1.0.0.2976. Investigación sin abrir el juego (11-10). Marcas: **[V]** verificado en logs, capturas o código;
**[E]** estimación fuerte; **[H]** hipótesis.

Datos:
- Invitado (Shadow), segunda partida: `backups\p8-sesion14-shadow\cdmp_20261011-011822.log` y
  `cdmp_packets_20261011-011835.{bin,log}`. La primera partida (`…011409`) terminó con un cierre normal desde el
  menú tras el atasco con el tendero (`Terminate 시작` a las 01:18:15) y solo sirve de comparación.
- Anfitrión: `backups\p8-sesion14-host\cdmp_20261011-021131.log` y `cdmp_packets_20261011-021141.log`.
- **Relojes:** anfitrión ≈ invitado **+1 h 00 min 00,45 s**, no +58 min [V]. Lo medí con la 2001 del invitado:
  sale a las 01:29:50.886 y el anfitrión la recibe a las 02:29:51.362. Lo confirma el 2437, que llega a
  02:29:51.231. En la captura del invitado, t = 0 corresponde a las 01:18:59.372.

## 0. Resumen

1. **No hubo crash ni cuelgue [V].** Lo que vio el usuario fue un diálogo del juego: «Se ha producido un error
   crítico. Se cerrará la sesión. Código de error (2384028426)».
   - **2384028426 = `0x8E19630A` = `eErrNoInvalidFieldInfoKey`** [V]. Los códigos son `hashlittle(nombre, 0xC5EDE)`
     (`optD-mundo.md`). Lo comprobé con los 1502 nombres `eErrNo*` del exe, con el par de control
     `eErrNoUserNotLogin` = `0xAE190FD8`.
   - El código viene **byte a byte** en el `LogoutCharacterToFieldServerAck` 2679 que mandó el servidor del anfitrión:
     `4C 0A 10 A0 | 0B | 0A 63 19 8E` (actor, motivo `0x0B`, código). Una salida normal trae
     `07 | C8 BB 0C A3` = `eErrNoParentsRequestToLogout` (las 13 salidas normales de las capturas de `backups`).

   Después el cliente hizo lo suyo: 2001 → título. El mod mandó la reconexión del título al servidor local, como está
   diseñado (`g_had_remote`). 3,4 s después el juego terminó por su camino normal de salida:
   - 2001 al servidor local;
   - `Terminate 시작`;
   - ningún `종료 이벤트 발생`, así que no fue Alt+F4 ni el cierre de la ventana;
   - es la misma firma que la salida normal de la primera partida y la del anfitrión al final.

   No hay excepción, ni volcado, ni `the game ENDS NOW` del mod (`GuestS3 … crash on`, `dumps 0`). Por eso no llegó
   ningún `cdmp_crash_*.dmp`. Lo más probable [E] es que el `Terminate` sea la salida tras aceptar el diálogo, o la
   salida desde el título.
2. **La pila de FreezeWatch es la del apagado normal [V].** El hilo principal está en `WaitForSingleObjectEx`
   (IAT `0x51EB2F8`), dentro de `Terminate` (`exe+0xA0C75C` → `0xA0DCD2` → `0xA8B2A3`). Está esperando a que acabe
   otro hilo. La misma pila aparece en el cierre del anfitrión (02:30:38) y en el de la primera partida del
   invitado (01:18:15). No es un bloqueo ni código del mod (`not in a mod hook`).
3. **Causa [V en la cadena estática y en las capturas; E fuerte en el último eslabón]:** el viaje rápido nativo
   manda `ReloadStageStartReq` 2283.
   - En el servidor, esa Req pasa por el embudo de dueño único `ServerField::vf48`. El destino, con su FieldInfoKey,
     se escribe en `TransformSync+0x550` del personaje **dueño** de ese gestor (el Kliff del anfitrión), no en el del
     usuario 2.
   - Cuando llega el `GameLoadingStartReq` 2437 del invitado, el manejador (`0x29C59D0`) llama a `0x2BC9200` sobre el
     TransformSync **del usuario 2**. Allí `cmp [rdi+0x550], 0` da 0 y devuelve **`eErrNoInvalidFieldInfoKey`**
     (`0x2BC925F`).
   - El servidor saca al personaje con ese código: 2679 → diálogo → 2001 → título.
   - Es el mismo patrón que en la sesión 9 (`p8-s9-cinematica.md` §2, motivo `0x0B`).
   - No hay ningún 2174, 2549 ni 2144 de ese viaje.
4. **No es culpa de ningún módulo nuevo de la sesión 14 [V]:**
   - `observer_load_defer` nunca vio una carga: no hubo 2549, `loads 4` siguió igual, `STUCK 0` y `withheld now 0`.
   - `guest_sched_route` hizo lo que debía: 17 `stage teardown` con `X GONE` desviados al gestor vacío del mod y la
     línea `[X gone]`, y el proceso siguió vivo 20 s más.
   - `guest_seq_route`/`seq_play` no intervinieron.

   Lo que falta es soporte: el viaje rápido del usuario 2 en el campo compartido no existe. Además, el ciclo de vida
   del invitado no permite volver a entrar sin reiniciar (P8-4).
5. **Nunca se había probado el viaje rápido del invitado en la opción D [V].** Solo se probó en la arquitectura de
   espejo anterior, donde cada jugador tenía su propio servidor (`optD-mundo.md` §4.2, pruebas del 10-03 y 10-05:
   «fast travel survive»). En la opción D estaba previsto como riesgo:
   - `opcion-d-campo-compartido.md` C5: «el usuario 2 no recibe … la recarga pendiente (solo dueño)»;
   - `opcion-d-accion-usuario2.md` R3: viaje rápido sin comprobar [S].

   En ninguna captura anterior aparece un 2283: lo he buscado en los 77 directorios de `backups`.

## 1. Secuencia de los últimos ~60 s

| Hora invitado | t captura inv. | Hora anfitrión | Lado | Hecho | Marca |
|---|---|---|---|---|---|
| 01:28:04–01:29:33 | — | — | inv. | Juego normal en Hernand: 2 tiempos de espera de `GuestSchedLoad`, vetos `UIStageGuide`, `GuestXInter` (C1) redirigido y aceptado. Hay 4 `responseTrChangeAIStateAck에 실패` (aparecen en toda la sesión; no tienen relación) | V |
| 01:29:45.722 | — | — | inv. | Abre el menú principal (`UIGamePlayControl_Root_MainMenu`); `ui_pause_block` deja correr el mundo | V |
| 01:29:46.245 | — | — | inv. | Mapa del mundo (`UIGamePlayControlRootWorldMap`) | V |
| 01:29:48.771 | **649,399** | ~02:29:49.2 | inv. | **C>S `ReloadStageStartReq` 2283**, 53 B: `u32 0, u32 1, u32 0`, pos **(−10626,4; 621,2; −3813,3)** (a unos 263 m del invitado, en (−10808,5; 609,7; −4002,8)), cuaternión yaw ≈ 12°, escala, flag. En ese mismo instante: `[P+0x15C]` 0x18 → 0x01 → 0x02 → **0x04** (T9160). El mapa se cierra (UI 0 → 1 a las 48.810) | V |
| 01:29:48.8–50.7 | 649,4–651,3 | — | inv. | 1,95 s en 0x04 (fundido). El servidor del anfitrión sigue mandando lo normal: 2516 de NPC con motivo 0x19, 2953, 2318 | V |
| 01:29:50.725 | **651,353** | 02:29:51.231 | ambos | 0x04 → 0x05 → 0x06; **C>S `GameLoadingStartReq` 2437**. En el anfitrión, `User2Funnels vf40 … Req 2437 of user 2 … returned user 2's ServerQuestManager … from exe+0x2B03AA7`: es `0x2B03A60`, el cálculo del punto de reaparición (`p8-sistemas-juego.md` §11) | V |
| — | — | 02:29:51.212 | anf. | El guardián rechaza `…\slot2_backup` (`[SaveData] Backup 생성 실패`): el servidor intenta guardar al salir; está previsto | V (E: que sea el guardado del usuario 2 al salir) |
| 01:29:50.856 | **651,436–651,484** | — | inv. | S>C: 2861 ×2 (los mercenarios se disuelven), 2516 de `0xB0100A4D` con motivo **0x07**, buffs y **`LogoutCharacterToFieldServerAck` 2679 `4C 0A 10 A0 0B 0A 63 19 8E`**: su Kliff, motivo **0x0B** (el mismo que en la sesión 9), código **`0x8E19630A` `eErrNoInvalidFieldInfoKey`**. Es el **diálogo de error crítico** que vio el usuario. El overlay sigue en «conectado a Host» y «no controlled actor» porque el personaje ya no está en el mundo | V |
| 01:29:50.874–.880 | 651,50–651,51 | — | inv. | 17 × `GuestSchedRoute [stage teardown 0x43B0B6] … X GONE` → al gestor vacío del mod; después, `[X gone] … held 32 stage(s)`. Sin fallos | V |
| 01:29:50.886 | **651,514** | 02:29:51.362 | ambos | **C>S `LogoutSessionToServerReq` 2001**; 0x06 → 0x18. En el anfitrión: `C4: logout begin epoch=2 … trigger=bye`, «user 2 left after its GameLoadingStartReq (MOVE_ASKED …): waiting up to 3000 ms for the server's MoveFieldAck 2174» | V |
| — | — | 02:29:54.359 | anf. | «no MoveFieldAck 2174 in 3000 ms … the 2001 as before»; la 2001 llega al servidor | V |
| 01:29:53.933 | 654,569 | — | inv. | 2003/2002 (`index 4 vs own 4: equal`) y 1004; 0x18 → 0x01 → 0x02 → 0x03 → **0x13 (título)** → 0x14…0x18. Las respuestas son del **anfitrión**, no locales | V |
| — | — | 02:29:54.4–55.0 | anf. | `User2Funnels: user 2 gone`, barrera a/b/c/d **pass**, `logout complete epoch=2`, `reconnect epoch=3 ready` | V |
| 01:29:55 | — | — | inv. | `host ready no`: por diseño, el anfitrión no anuncia «ready» hasta que haya un enlace de transporte más nuevo (`min_link = ready_link_ + 1`, `session2.cpp:524-528`; la «trampa `ready_link_`» de `p8-s9-cinematica.md` §3) | V |
| 01:30:06.934 | **667,562** | — | inv. | El título se reconecta: 1001, 2319 (667,621), 2490. Mod: `ERR … the client reconnected; restart the game…` y `connection (epoch 0x3) -> local: this game run already had a remote connection`. El servidor local responde 2646/2869/2716 (vestíbulo) | V |
| 01:30:07.026 | — | — | inv. | `UiPause: P8 session over` | V |
| 01:30:10.327 | **670,955** | — | inv. | C>S 2001 al servidor **local**; 2003/2002/1004 en 5 ms | V |
| 01:30:10.551 | — | — | inv. | `Game error: "Terminate 시작."` (desde `exe+0xA0C715`, valor `0x140A0DCD2`) | V |
| 01:30:10.69–.84 | — | — | inv. | FreezeWatch #1.1–#1.3 (+328…+484 ms): el apagado espera a un hilo (§2). Fin del log | V |
| — | — | 02:30:38.663 | anf. | El anfitrión sale con normalidad (`Terminate 시작` y la misma pila) | V |

**Por qué `connection local BROKEN` [V, código]:**
- `BROKEN` es la bandera `g_broken` que puso `end_remote` (`guest.cpp:251`). No se borra hasta una conexión remota
  nueva.
- `local` sale de `decide()` (`guest.cpp:303`): esta ejecución ya tuvo una conexión remota.
- `local packets 3` son las tramas que pasaron al servidor local: 2319, 2490 y la 2001 final.
- Es el comportamiento previsto de P8-4, no un fallo nuevo.

**No hubo carga en el anfitrión [V]:**
- `observer load defer (on): user 2 loading now no (loads 4, STUCK past 60000 ms 0) … withheld now 0`, igual antes y
  después.
- `2322 hold`: `trips held 2, ran out … 0`.
- `ObserverMilestone [logout] … trip E2.T4 was already decided`.
- La fase de movimiento pasó de `STABLE (3093 1, 2437 1, 2174 2, 2549 2, …)` a
  `MOVE_ASKED (… 2437 2, 2174 2, 2549 2 …)`: el 2437 del viaje nunca tuvo su 2174.
- `observer_seq_copy` no descartó el 2283: `C>S dropped: host sequencer 0`. Su `u64 @0` es `0x100000000`, que no es
  ningún secuenciador del anfitrión.

## 2. ¿Crash o cuelgue? La pila de FreezeWatch

Las direcciones de retorno, en build 1.0.0.2976 (capstone sobre el exe):

| Retorno | Llamada anterior | Lectura |
|---|---|---|
| `0x13656D3` | `call [0x51EB2F8]` = **`KERNEL32!WaitForSingleObjectEx`** (rip `ntdll+0xA0154`, la espera del kernel) | espera a un objeto (un hilo o un evento) |
| `0xB5AB56D` (`.xtls`) | `call 0x13655C0` | envoltorio de la espera |
| `0x128B12E` / `0x128B0DC` | `0x128B140` / `0x128B0B0` | apagado de un subsistema |
| `0x47EF687`, `0x6126E3`, `0x1420429`, `0xB652423` | destructores / liberación | — |
| `0xA0C75C` | `call 0x128B0B0` después de `0x128ABA0` | dentro de la función que escribe `Terminate 시작` (`0xA0C715`) |
| `0xA0DCD2`, `0xA0DD45`, `0xA8B2A3` | llamadores de `Terminate` (`0xA0DCD2` es el valor que trae la propia línea `Game error`) | camino de salida del juego |
| `0x1371B40` ×2, `0x9470B7` (`call [rax+0x40]`), `0x1408A94` | despachador del bucle principal | — |

- La pila es idéntica a la de la salida normal del anfitrión y a la de la primera partida del invitado (las dos
  desde el menú) [V].
- Ninguna línea del invitado indica una excepción. No hay `GuestS3 … dumps` > 0, ni `report guard … not sent` > 0,
  ni `Sentry`.
- El mod tampoco terminó el proceso: no hay `ENDS NOW` ni ninguno de los códigos 0x2001/0x2003.
- Conclusión: **ni crash ni cuelgue.** Fue la salida normal del juego, pedida desde el título 3,4 s después de que el
  título se reconectara [E: que la pidiera el usuario; el juego no muestra ningún error antes].

## 3. Por qué el servidor saca al invitado

**El manejador del 2283 en el servidor, `0x2A5F580` [V estático]:**
1. Deserializa `u32, u32, u32, float3 (+0x24), float4 (+0x14), float3 (+8), u8 (+0x90)`. Coincide con los 53 B de
   la captura y con lo que describe World Builder (`optD-referencias.md` §3.3).
2. Toma el remitente `rsi = [params]` (el Kliff del usuario 2) solo para encontrar su campo (`0x2A5F7B7 call
   0x17FA6C0`).
3. Llama a **`[field]->vf48`** (`0x2A5F7F9 call [rax+0x48]`), que es `0x2B00CE0`, el gestor de secuenciadores **del
   dueño** (`p8-sistemas-juego.md` §2).
4. Llama a `0x2896120(gestor, &res, …)` **sin el actor**. `0x2896120` trabaja sobre `r13 = [gestor+0x18]`, el
   personaje dueño de ese gestor (`0x2896155`).
5. Si `res ≠ 0`, manda un Nak 1013 (`0x3F5`) a la sesión del remitente. El invitado no recibió ningún Nak: el
   anfitrión cuenta `Nak 1013 0`.

**El 2437 y el error [V estático]:**
- `0x2BC4950` es la función que aplica el destino. Escribe en `TransformSync+0x550` (`0x2BC4BA9 lea rcx,[r14+0x550]`
  → `0x2BBF510`), y World Builder la describe como «destino en `TransformSync+0x550`».
- El manejador del 2437 (`0x29C59D0`) llama a `0x2BC9200` (`0x29C5A90`).
- `0x2BC9200` hace `cmp dword [rdi+0x550], 0 ; jne` y, si es 0, escribe el global `0x6CE51D8`
  (`0x2BC925F mov eax,[rip+…]`). Ese global lo inicializan `0x21FB67F`/`0x220A606` con la cadena
  `eErrNoInvalidFieldInfoKey` (`.rdata 0x5932260`).
- Otros lectores del mismo error en `.rsrc`: `0x2B07811` (dentro de `AcquireField 0x2B07790`, el gancho E3b H1, cuando
  la FieldInfoKey no está en la tabla de campos), `0x2BC97BD`, `0x21A15BF` y `0x9FA732`. Los de `.xtls` no se han
  buscado.

**Consecuencias:**
- La recarga quedó en el personaje del anfitrión [E fuerte]. El `+0x550` del usuario 2 sigue a 0, así que no tiene
  ningún viaje pendiente.
- Con el 2437 sin viaje pendiente, el servidor sigue el camino de salida con `eErrNoInvalidFieldInfoKey`:
  1. `0x2B03A60`, el punto de reaparición, que llama a vf40; con `quest_per_user=1`, el mod le da el gestor del
     propio usuario 2;
  2. el guardado al salir, que el guardián rechaza;
  3. 2861, 2516 con motivo 0x07 y 2679 con motivo 0x0B.

  Es el mismo patrón que en la sesión 9 [V].
- `quest_per_user` no es la causa [E]: la misma llamada (`0x2B03AA7`) ocurrió en las dos entradas buenas, bajo la
  Req 2027 (02:15:44 y 02:19:41), y la sesión 9 terminó igual.

**Riesgo para el anfitrión [E/H]:** el destino del invitado probablemente quedó en `TransformSync+0x550` **del Kliff
del anfitrión**. Su próximo 2437 (muerte, viaje rápido o recarga) podría llevarlo al punto que eligió el invitado. En
World Builder, un 2283 que el cliente no completa aparca y mata al personaje a los ~10 s. Aquí no se vio nada en los
47 s siguientes:
- ni `MoveForceAck`, ni muerte, ni carga en la captura del anfitrión;
- `Local` siguió en (−10804,7; 609,5; −4005,7) con 300 de vida.

Puede que `0x2896120` saliera pronto sin dejar estado. No se puede comprobar con estos datos (§5, P1).

**Viaje rápido del cliente [V estático]:** los dos remitentes del 2283 que se conocen, `0x655110` y `0xA9A860`
(`p8-s9-cinematica.md` §7.2), llaman a **`0x778AB0`** (el que escribe `[P+0x15C]=1` y arranca la máquina de cambio de
campo) y después a `0xC04D20` (`PostPacket` del 2283). Por eso la transición 0x18 → 0x01 y el 2283 caen en el mismo
instante (t = 649,399 / 649,4018).

## 4. Qué módulo «lo causó» (V/E/H)

| Candidato | Veredicto | Evidencia |
|---|---|---|
| `observer_load_defer` (retener NPC durante la carga) | **No** [V] | No hubo 2549 ni carga del usuario 2 (`loads 4` sin cambios, `STUCK 0`, `withheld now 0`); solo se movió la fase MOVE_ASKED del 2437 |
| Retención del 2322 | **No** [V] | `trips held 2, ran out 0`; el viaje E2.T4 ya estaba decidido |
| `guest_sched_route` / `guest_sched_bind` con X muerto | **No; la guarda funcionó** [V] | 17 `stage teardown … X GONE` desviados (`0x43B0B6`), `[X gone] … 32 stage(s)`, `census … routed X managers gone 1`, y 20 s más de proceso vivo. Es justo el fallo que la guarda de la sesión 12 evita |
| `guest_seq_route` / `guest_seq_play` | **No** [V] | Nada en la ventana; después, `X 0x0 unreadable (unchanged)` |
| `guest_schedule_load` | **No** [V] | Solo tiempos de espera; `LATE 0`, `failed 0` |
| `quest_per_user` (vf40 del 2437) | **No es la causa** [E] | La misma llamada (`0x2B03AA7`, punto de reaparición) en las entradas buenas; mismo resultado en la sesión 9 |
| Ciclo de vida del invitado (`g_had_remote`, `end_remote`) | **Es lo que impide volver a entrar**, por diseño [V] | `ERR … the client reconnected` y `-> local`; `host ready no` por `min_link` |
| Fase de movimiento del anfitrión (espera de 3 s al 2174 antes de la 2001) | **No** [V] | Solo retrasó la 2001 3 s; el 2679 con el error ya se había mandado (651,484, antes de la 2001) |
| «answering locally» del ciclo de vida del invitado | **No ocurrió** [V] | 2003/2002 llegaron del servidor del anfitrión (01:29:53.933); no hay ninguna línea `answered locally` |
| Reescritura del 2174 (`MoveFieldAck owner -> user 2`) / viaje de observador a un campo que no es el del anfitrión | **No ocurrió** [V] | No hubo 2174: `MoveFieldAck owner -> user 2: 2`, sin cambios desde la entrada; `E3b H1` no se llamó (no hay línea `user 2 travels`) |
| **Campo compartido (E3b) + embudo vf48 sin redirigir para el usuario 2** | **Condición necesaria, del diseño del mod** [E fuerte] | Con `share_field` el dueño es el anfitrión, y vf48 da su gestor. En un jugador, el remitente del 2283 es el dueño y funciona. `User2Funnels` redirige vf40 y la muerte/botín, pero vf48 está en `count only` |
| **Falta de soporte del viaje rápido del usuario 2 en el campo compartido** | **Causa raíz** [E fuerte] | §3: el 2283 escribe el destino en el `+0x550` del Kliff del anfitrión; el 2437 del usuario 2 encuentra su `+0x550` a 0 → `eErrNoInvalidFieldInfoKey` → 2679 → diálogo |

## 5. Arreglo propuesto

**F0. Inmediato, sin código.** En `docs/PRUEBA-DOS-PC.md`, avisar de que el invitado **no** debe usar el viaje rápido
ni el teletransporte del mapa. Si ocurre, volverá al título: hay que cerrar el juego y entrar de nuevo, sin pulsar
«Continuar», porque una partida local tras una sesión remota se queda en la carga (sesión 9).

**P1. Medida en 1 PC, sin Shadow, antes de programar nada:**
1. El anfitrión, solo y con el sniffer, hace un viaje rápido desde el mapa.
2. Se captura la secuencia nativa. Se espera 2283 → ? → 2437 → `SetMoveFieldAck` 2277 / `MoveFieldAck` 2174 → 2549 →
   2144 → 2445 (00) → 2762 (`optD-mundo.md` §4.1, «Fast travel: SetMoveFieldAck, MoveFieldAck, MoveFieldComplete»).
3. Con una sonda en la entrada de `0x655110` y `0xA9A860`, se mira cuál usa el mapa, su firma y su valor de retorno.
4. Con una lectura del gestor de secuenciadores del anfitrión antes y después, se mira dónde deja `0x2896120` la
   recarga pendiente: `[gestor+0x18]` o `TransformSync+0x550`.

**F1. Invitado: veto del viaje rápido mientras juega en remoto (recomendado; corto y seguro).** Clave nueva
`guest_fast_travel_veto = 0 | probe | 1`:
- **Dónde:** gancho inline en la **entrada** de `0x655110` y de `0xA9A860`. Las dos llaman a `0x778AB0` y luego
  mandan el 2283, y una entrada no está a mitad de función, así que cumple la regla.
- **Qué hace:** con la conexión REMOTE, devolver sin efecto y con el valor que diga P1, antes de que arranque la
  máquina de carga; además, una línea WRN y un aviso en el HUD: «el viaje rápido no está disponible en cooperativo».
- **Por qué en la entrada:** vetar solo el 2283 (`0xC04D20`) no basta. El cliente ya estaría en 0x04, mandaría su
  2437 igualmente y el servidor lo sacaría.
- **Riesgo [H]:** el mapa podría quedarse en «teletransportando». Se comprueba en P1 o en la prueba en 1 PC.
- **Abortar:** `guest_fast_travel_veto=0`.

**F2. Anfitrión: red de seguridad.** Descartar y contar **todo** 2283 del usuario 2 en el campo compartido, con una
línea WRN, para que nunca toque el gestor del anfitrión (§3, riesgo [H]):
- Corregir el supuesto de `observer_seq_copy_logic.h:74` («u64 seq @0 [E: not in a capture]»): ya hay captura, y son
  53 B sin id de secuenciador.
- Solo con F2 el invitado sigue saliendo al título. F1 es el arreglo principal; F2 protege al anfitrión.

**F3. Soporte real (después, [H]).** Redirigir, solo para el 2283, la vf48 del manejador hacia el gestor de
secuenciadores **del propio usuario 2**:
- **Mecanismo:** el sujeto de `User2Funnels` en ExecuteTroc ya existe. Hay que filtrar por la dirección de retorno
  `0x2A5F7FC`, igual que vf40 / `kill_loot_per_user`. Así `0x2896120` → `0x2BC4950` escriben el destino en el
  `TransformSync+0x550` de **su** Kliff, y el 2437 pasa la comprobación de `0x2BC9200`.
- **Qué debería pasar después:** el 2437 del invitado recibiría su 2174. `E3b H1` lo mantendría en el campo del
  anfitrión si el destino es del mismo nivel, y el viaje de observador (con `load_defer`, la retención del 2322 y la
  instantánea) haría el resto, como en la entrada.
- **Necesita:** P1 y una lectura estática de dónde lee el manejador del 2437 el viaje pendiente.
- **Prueba:** en Shadow, un viaje rápido corto dentro de la misma ciudad.

**F4. Ciclo de vida (UX).** Si llega un 2679 por la conexión remota sin que el usuario haya pedido salir:
- una línea ERR clara y un aviso en el HUD («el anfitrión te sacó del campo; reinicia el juego para volver»);
- a largo plazo, B2/G5: volver a entrar sin reiniciar (el lado del anfitrión ya llega a `reconnect epoch=3 ready`).

## 6. Preguntas para el usuario

- Respondido por el usuario: tras el viaje salió el diálogo «error crítico … se cerrará la sesión … (2384028426)»,
  con el overlay en «Red: conectado a Host» y «Tú: no controlled actor». Encaja con el 2679 de §1.
- Respondido (11-10): al aceptar el diálogo **el juego se cerró directamente**; el usuario no vio el título. El estado
  0x13 de las 01:29:53.9, la reconexión de las 01:30:06.9 y la 2001 local son el camino automático del cliente antes
  de `Terminate` (01:30:10.5): la salida la provoca el diálogo de error crítico, no el usuario desde el título (corrige
  la lectura [E] de §0.1 y §2; la conclusión «ni crash ni cuelgue» no cambia).
- Respondido (11-10): el destino era **Hernand**, el único punto de viaje rápido desbloqueado en esa partida, en el
  mismo campo (unos 263 m). El caso que hay que soportar primero es una recarga dentro del mismo campo:
  `p8-impl-viaje-rapido.md` (F1/F2/F3).
- Al anfitrión: si en una sesión futura muere o hace un viaje rápido después de un viaje rápido del invitado, ¿aparece
  en un sitio inesperado? (riesgo de §3)
