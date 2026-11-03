# C1 (E3b): el cliente del anfitrión rechaza un actor del usuario 2 y luego se cierra

Build 1.0.0.2976, análisis estático de solo lectura (2026-10-09). No se ejecutó el juego.
Fuentes: `CrimsonDesert.exe` (capstone), exports de Ghidra, log
`backups/logs/cdmp_20261009-0332_e3b.log`, grabación del túnel `D:/cdmp-grabaciones/e3b.bin`
(solo la sesión 2) y una captura del anfitrión en un solo jugador (`cdmp_packets_20261003-125936.bin`).
Scripts en `%TEMP%/claude/c1/`: `fn.py`, `rec.py`, `alda.py`, `errtab.py`, `l3.py`, `hostorder.py`,
`d_8bd530.asm`, `d_8af580.asm`, `d_206b2d0.asm`.

Marcas: [E] probado en `.rsrc`; [E-x] leído en `.xtls`; [S] inferencia.

## 0. Resumen

- **Solo hubo un error del juego.** El texto registrado es `"Actor 생성에 실패했습니다."`
  (`0x55B6510`). `"비정상적인 공개정보입니다."` no salió en el log: es la **descripción del código**
  2781013502 [E] (§1.2). No se registró ningún otro error.
- **El actor rechazado no es el personaje 2: es su mercenario `0xB01002C1`.** Es un registro de
  tipo 6 con índice 1, dueño `0x901002BF` y nombre "Abyss Heuklang" [E] (§1.3). El personaje
  `0xA01002C0` sí se creó, porque su fallo habría dejado otra línea idéntica en el log [E].
- **Falló `0x8AF580` (`CreateOwnedActor`).** Esa función **no lee** `characterNo`, nombre,
  apariencia ni datos del registro. Solo usa tipo, clave, dueño e índice [E]. Por eso el `characterNo`
  repetido (C2) **no** es la causa de este error.
- **El cierre es otro problema [S fuerte]:** el mercenario que falla se destruye [E/E-x]. Lo que se
  cierra encaja con el personaje 2, un actor remoto (modo 2) que nunca recibe una acción. Es la misma
  firma que el cierre del caballo copia. Evitar el error del mercenario **no basta**.

## 1. Pregunta 1: la función de `0x8BDF7C` y la condición que falla

### 1.1 Qué función es [E]

- `0x8BDF7C` es la dirección de retorno de `call 0x35D8D0` (el registrador de errores), hecha en
  `0x8BDF77`, dentro de `0x8BD530 cdmp_ClientActorManager_CreateFromLoadRecord`.
- Cadena de llamadas: `ActorLoadDataAck` 2005 → `vf2 0xCB25A0` (`jmp .xtls 0xA79EA60`) →
  `0xA79EB3C: call 0x8BD530` [E-x], una vez por cada registro del paquete.
- El `call` de `0x8BDF77` lo comparten cinco ramas. El texto dice cuál fue:

| Rama | Texto (`[rsp+0x58]`) | Código (`[rsp+0x48]`) |
|---|---|---|
| `0x8BDC4A` (`0x8AF580` devolvió fallo: `cmp byte [rbp-0x48],0`) | `0x55B6510` "Actor 생성에 실패했습니다." | `[0x6CDB9F0]` |
| `0x8BDC94` (falla el unpack `0x17EA310`) | `0x55B6538` "Actor unpackInformation…" | `[0x6CDB9F0]` |
| `0x8BDE34` / `0x8BDEEC` (falla un componente, vf `0x68`/`0x70`) | `0x55B65A0` "Actor onProcessLoadData…" | el del componente |
| `0x8BDF3D` (transición de estado) | `0x55B63F0` "UserLogining처리에 실패했다." | `[0x6CDB928/92C/930/934]` |

- El log trae `0x55B6510` y la dirección `0x8BDF7C`. Por eso la rama es, sin ambigüedad,
  **`0x8BDC4A`**: falló `0x8AF580(mgr, out, tipo, clave, dueño, índice, u64, bool)`, llamada en
  `0x8BDC3E`.
- La otra rama con el mismo texto (`0x8BD676`) registra desde `0x8BD5B3`, así que habría dado
  "from `0x8BD5B8`".

### 1.2 El código 2781013502 [E]

- `0xA5C2E5FE` (no `…E7FE`) es el valor de la global `[0x6CDB9F0]`. Se calcula al arrancar en
  `0x1428140` (`0x1428EF6…0x1428F19`):
  `0x1427790(tabla, nombre "eErrNoInvalidPackPublicInformation" 0x5722D58, descripción
  "비정상적인 공개정보입니다." 0x5722D80, severidad 2, &0x6CDB9F0)`.
- El valor es `0x1364780` = **lookup3 `hashlittle(nombre, 0xC5EDE)`**. El código del
  `ebx = len − 0x2145E233` equivale a `0xDEADBEEF + len + 0xC5EDE`. Comprobado:
  `hashlittle("eErrNoInvalidPackPublicInformation", 0xC5EDE) = 0xA5C2E5FE` (`l3.py`).
- `0x8BD530` usa este código como **etiqueta genérica** para "no se pudo crear el actor" y "no se pudo
  desempaquetar" (`0x8BDC5E`, `0x8BDC7D`). Ahí, "pack public information" es el **registro de
  `ActorLoadDataAck`** (los datos públicos de un actor; `PrivateInformation` es la parte solo para el
  dueño). No existe un objeto "información pública" aparte que se valide.
- Otros códigos útiles (globales → nombre → valor), sacados de la misma tabla (`errtab.py`,
  1.559 entradas):

| Global | Nombre | Valor |
|---|---|---|
| `0x6CDB9EC` | `eErrNoCantAttachToActor` | `0x2A3A7013` |
| `0x6CF7190` | `eErrNoInvalidChildSlotIndex` | `0x6E90EEBE` |
| `0x6CF7188` | `eErrNoInvalidUserActor` | `0x978DCCEF` |
| `0x6CF78A8` | `eErrNoDuplicateCreateActor` | `0xA1B3B447` |
| `0x6CDB928` | `eErrNoUserNotLogining` | `0x8B2EE38F` |
| `0x6CD9448` | `eErrNoMemoryIsLack` | `0xE4DA8DFE` |

### 1.3 Qué registro falló [E]

- El hook del mod copia los argumentos 13–16 desde `[rsp_entrada+0x68…]`, es decir,
  `[rsp+0x60…0x78]` del llamador. Esa llamada solo pasa 12 argumentos, así que esos "valores" son
  variables del marco de `0x8BD530`.
- `values[2] = [rsp+0x70] = 0x901002BF_B01002C1`:
  - `u32 [rsp+0x70]` es la **clave del registro** (la lee `0x8BD5C2`);
  - `u32 [rsp+0x74]` es el **dueño** (la lee `0x8BD994`).
- **Es el registro de clave `0xB01002C1` y dueño `0x901002BF`.**
  - `0x13FFB90`, `0x13FFB01` y `0x31FAF879B10` son restos de la pila. No significan nada.
- En la grabación de la sesión 2, ese actor llega en el paquete n.º 7247 (t = 1,407 s, 9.419 B)
  (`alda.py`): `[u16 9][tipo 0, usuario 0x901002BF]` + `[u16 0x24B9][tipo 6, clave 0xB01002C1,
  dueño 0x901002BF, índice 1, u64 −1, +0x1F = 0x901002BF, CharacterInfo 0x14C09, "Abyss Heuklang"]`.
- Le preceden `RefreshMercenaryDataAck` 2861 (`+0` = `0xA01002C0`, `+4` = `0xB01002C1`, n.º
  `0xF42BB`) y `JoinPartyAck` 2425 [E].
- El personaje del usuario 2 llegó a la sesión 2 a t = 1,372 s:
  - n.º 940: usuario + personaje, 11.713 B; `payload` = 11.708 B, el mismo tamaño que la "user
    template" que vio llegar el cliente del anfitrión;
  - n.º 1067: solo el personaje.
  - El mercenario llegó **35 ms después**.

### 1.4 La condición que falla dentro de `0x8AF580`

`0x8AF580` (`d_8af580.asm`) falla, es decir `out+0x10 = 0`, si [E/E-x]:

| # | Comprobación | Dónde | ¿Deja rastro? |
|---|---|---|---|
| a | El dueño no es una clave de usuario (`& 0x30000000 != 0x10000000`) | `0x8AF5D7` | no (aquí `0x9…`: pasa) |
| b | `0x8ADFE0(dueño)`: el usuario existe y **`u16 [usuario+0x5E] & 4`** (estado 4 exacto: los estados son 1→2→4→8→0x10→0x20, `vf60…vf80` = `0x1436A20…0x1436A60`) | `0x8AE0D4–0x8AE0EB` | no |
| c | Fábrica `0x4634C0` → `.xtls 0x898D890` devuelve nulo: | | |
| c1 | `vf8` init `0x4F8FA0` → `.xtls 0x8C08690` → `0x2067F40` → `.xtls 0xE0BFAC0` → componentes `0xE0EB950` | | sí: `0x35D8D0` (texto `0x590E768`/`0x590E830`): **no salió** |
| c2 | `TryAddRef` del nuevo | `0x898DA57` | sí (`0x557C620`): **no salió** |
| c3 | `[[usuario+0x68]+0x18]` (`ClientChildContainerActorComponent`) nulo | `0x898DAF7` | no |
| c4 | Ranura `índice` ocupada (`byte +8`) → `eErrNoInvalidChildSlotIndex` | `0x898DB03/0x898DB15` | no |
| c5 | `0x206B2D0` (confirmar ranura) ≠ 0: usuario no en estado 4 → `eErrNoCantAttachToActor` (`0x206B345/0x206B394`); `vf40 0x211A230` → `.xtls 0xE3D6C70` (índice ≥ tamaño → `eErrNoInvalidUserActor`, ocupada → `eErrNoInvalidChildSlotIndex`); **enganche al personaje principal** `[usuario+0xD0]`: si `TryAddRef(pj, 0x10)` vale, exige **`[pj+0x5E] & 0x10`** o da `eErrNoCantAttachToActor` (`0x206B535–0x206B585`); y después `vf41 0x211A2A0` | `0x206B2D0` | no |
| d | Inserción en el mapa del gestor `0x463320` → `.xtls 0x898D400`: clave ya existente → `eErrNoDuplicateCreateActor` | `0x898D45C–0x898D4E7` | solo por otro registrador (`0x464860` → `0x1419070`, "ActorKey[%#]가 중복 로그인되었습니다"), que el mod no engancha |

- **Qué se descarta** [E]:
  - c1 y c2: dejarían otra línea en el log, y no hay ninguna.
  - b, si se toma literal: el personaje, con el mismo dueño y en la misma tanda, pasó `0x8ADFE0`.
    `0x8BD530` deja al usuario en estado 4 en `0x8BD88E–0x8BD952` (`vf68`). No he visto nada que lo
    mueva de 4 entre los dos paquetes (búsqueda no exhaustiva).
- **Lo más probable [S fuerte]: c5, el enganche al personaje principal.** El personaje
  `0xA01002C0` se creó 35 ms antes y aún está en estado 8 (cargando recursos: `vf70` en `0x8BDF84`
  y la tarea asíncrona `0x4F9C60`), no en 0x10.
  - Encaja con el precedente del caballo (`INFORME-CABALLO-CRASH.md` §4):
    - prueba **A**: un hijo con dueño en la misma tanda que el personaje copia **no se crea**;
    - prueba **C**: el mismo, **3 s después**, sí.
  - Lo único que cambia con el tiempo es el estado del personaje principal. Las ranuras, la clave y
    el estado del usuario no cambian.
- **Pega [S]:** en un solo jugador, los hijos locales (`0xB0100002/3/4`, tipos 4/5/6, índices 1/2/3)
  llegan 70–80 ms después del personaje y se crean bien (`hostorder.py` sobre 125936, n.º
  1451/1472/1519). O el personaje local ya está en 0x10 a esas alturas, o el `TryAddRef(0x10)`
  (`0x1436A90` → `0x1436910`, depende del contexto del hilo, TLS `+0x1D2/+0x1D4`) da falso y se salta
  el enganche. Sin una prueba en vivo no se puede cerrar.
- **Diagnóstico mínimo (solo lectura, mid-hooks en `.rsrc`):**
  - `0x8AF61B`: `byte [rbp-0x11]` (b);
  - `0x8AF696`: `rax` (c, fábrica nula);
  - `0x8AF6D7`: `[rax]` (d, compararlo con `0xA1B3B447`);
  - `0x206B39E`: `eax` (c5, ranura);
  - `0x206B5DA`: `r13d` (c5, enganche, `0x2A3A7013`), junto con `u16 [[r14+8]+0x5E]`.

### 1.5 ¿Qué dato del usuario 2 lo hace "anómalo"?

- `characterNo`, nombre, apariencia y una "tabla de info pública por usuario" **no intervienen**.
  `0x8AF580` solo recibe tipo/clave/dueño/índice/u64/bool, y la fábrica ni siquiera usa el u64.
  El registro se desempaqueta **después** (`0x17EA310` en `0x8BDC74`) [E].
- Las claves son distintas de las del anfitrión: usuario `0x901002BF` ≠ `0x90100000`, personaje
  `0xA01002C0` ≠ `0xA0100001` [E].
- Lo que importa es el **orden y el momento**: un hijo con dueño (tipo ≠ 1) de un usuario remoto
  llega mientras su personaje principal todavía carga [S].

## 2. Pregunta 2: qué mensaje S→C lo lleva

- **`ActorLoadDataAck` 2005** (`troc_messages_2976.csv`: `vtable 0x5647900`, `OnReceive 0xCB23C0`
  (solo encola), `vf2 0xCB25A0`).
- Recorrido: `vf2 0xCB25A0` → `.xtls 0xA79EA60` → `0xA79EB3C call 0x8BD530` → `0x8BDC3E call
  0x8AF580` → fallo → `0x8BDC4A` → `0x8BDF77 call 0x35D8D0` [E/E-x].
- No hay ningún mensaje "de información pública" aparte que haga falta mandar antes.
- Lo que el servidor manda **solo al dueño** antes del mercenario (`RefreshMercenaryDataAck` 2861,
  `JoinPartyAck` 2425…) no lo usa `0x8AF580`. El componente de mercenario solo exige
  `RefreshMercenaryData` si el dueño es el jugador local (`caballo-coop.md` §8.3). [E para
  `0x8AF580`; el resto, por documentación previa.]
- No hay grabación de la sesión 1. Que el anfitrión recibió el par usuario + personaje lo prueba la
  "user template" del mod: el hook está en la recepción del cliente, `0x35CA00`. Que recibió el
  mercenario lo prueban los valores del error.

## 3. Pregunta 3: qué queda tras el fallo y por qué se cierra

- **No queda ningún mercenario a medio crear** [E/E-x]:
  - el fallo de b no construye nada;
  - en c4 y c5 la fábrica libera la ranura (`0x898DB3B–0x898DB44`) y destruye el actor
    (`0x898D9BB–0x898D9C6`, `vf0` con `edx = 1`);
  - `0x8BD530` solo suelta los handles (`0x8BDF9B–0x8BE023`) y el bucle del Ack sigue con el
    siguiente registro.
  - **Excepción (d):** `0x8AF580` borra el actor (`0x8AF6EE`), pero la fábrica ya lo había metido en
    la ranura del usuario y en el contenedor del personaje. Eso dejaría punteros colgantes [E-x], y el
    cierre sería distinto.
- **El cierre** (`0x1F63FE6`: `mov rax,[rcx]` nulo y luego `[rax+0x10]`) es el de una **capa de
  acción con gráfico y sin reproducción** (`[capa+0x18] = 0`). `0x2329700` no lo comprueba.
  - Es exactamente la firma del caballo copia (`caballo-coop.md` §9.1, `INFORME-CABALLO-CRASH.md`
    §5.2 y §11), que solo pasa con actores en **modo 2**, sin IA local (`+0x90` ≠ usuario local).
- **El candidato es el personaje 2 `0xA01002C0`** en el cliente del anfitrión [S fuerte]:
  - es el único actor remoto que sobrevive;
  - `cdmp_peer` en modo replay **no manda nunca `StartActionReq`**: en toda la grabación solo hay 9
    Req (2319, 2490, 3093, 2549, 2445, 2601, 2437, 2549, 2445);
  - en la grabación no hay ningún `StartActionAck`/`StartSpawnActionAck`/`ChangeAIStateAck` con
    `0xA01002C0`;
  - el servidor solo reenvía acciones si el cliente controlador las pide;
  - el cierre llega unos 2 s después, cuando el CC asigna el gráfico a la capa 0.
- **Conclusión:** arreglar el mercenario no evita el cierre. Además hace falta contener a los
  actores remotos sin acción o darles una.
  - La contención del mod (`horse_diag.cpp`, salto de `0x85FBA0`/`0x860D30`) solo cubre claves de
    copia (`is_copy`), y en E3b no estaba instalada (`co-op off`).

## 4. Pregunta 4: opciones para el mod, ordenadas

1. **(c) Filtrar en el cliente del anfitrión lo del usuario 2 (para la próxima prueba).**
   - **Qué:** en la recepción de la sesión 1 (`0x35CA00`, `.rsrc`; hoy es un mid-hook del sniffer
     que habría que convertir en filtro con detour), quitar de `ActorLoadDataAck` los registros
     cuyo dueño (`+0x12`) o clave (`+0x0E`) sean del usuario 2 (usuario `0x9…`, personaje `0xA…` y
     sus hijos), o descartar el paquete entero si no queda ninguno.
   - **Efecto:** no hay mercenario fallido ni personaje 2 sin acción, así que no hay cierre, y H1/H2
     se pueden seguir validando.
   - **Riesgos:**
     - los Ack posteriores con esas claves (`MoveActorAck`, `RemoveActorAck`…) apuntan a actores
       que no existen; en los experimentos previos el cliente los ignora [S];
     - el anfitrión no ve al usuario 2;
     - el servidor cree que la sesión 1 sí lo conoce.
2. **(d) Dejar que se vean, pero bien hecho.** Tres cambios:
   - **Contención:** generalizar `horse_diag` (`0x85FBA0`/`0x860D30` y la guarda de
     `0x1F63F70`/`0x2329700`, todo en `.rsrc`) a cualquier actor con `+0x90` = usuario no local,
     mientras la capa 0 tenga gráfico sin acción.
   - **Darle una acción:** que `cdmp_peer` (y luego P8) mande un `StartActionReq` de espera de su
     personaje tras `LoadingCompleteReq`, para que el servidor reenvíe `StartActionAck` a la
     sesión 1. Un cliente real lo hará solo; el replay no.
   - **Mercenario:** retener en el cliente del anfitrión los registros de tipo ≠ 1 con dueño = usuario
     2 hasta que su personaje esté en `0x10` (o unos 3 s, como en la prueba C del caballo), y
     reinyectarlos con `guarded_inject`.
   - **Riesgos:** la contención congela la animación hasta la primera acción. Si la causa real es d
     y no c5 (§1.4), habría que confirmarlo antes con el diagnóstico.
3. **(a) Identidad propia del usuario 2** (`characterNo`, nombre) antes de entrar.
   - **No arregla C1** (§1.5).
   - Sigue siendo necesaria para C2: la caché de equipo `0x8AEB30` y `EquipSlotListCacheData`
     indexan por `characterNo` (`optD-mundo.md` §2.5), y el `0x7FFF…` repetido acabará estropeando
     el equipo del anfitrión cuando el cliente pida `ClientCacheReq`.
   - **Dónde:** en la carga del usuario 2 (`0x2C3E030`, servidor) o en la partida que se le da. Las
     claves ya son distintas.
   - **Riesgo:** el guardado/guardián, porque el `characterNo` entra en el guardado.
4. **(b) "Mandar antes la info pública que falta":** **no aplica**. No existe ese mensaje (§2). El
   equivalente útil es el reordenamiento o la retención de (d).

**Nunca** enganchar `.xtls`. Todos los puntos propuestos (`0x35CA00`, `0x85FBA0`, `0x860D30`,
`0x1F63F70`, `0x2329700`, `0x8AF580`, `0x206B2D0`) están en `.rsrc`.
