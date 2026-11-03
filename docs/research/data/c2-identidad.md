# C2: identidad repetida del usuario 2 (`characterNo`, nombre, cuenta)

Build 1.0.0.2976, análisis estático de solo lectura (2026-10-09). No se ejecutó el juego, no se
tocó la carpeta del juego, el código fuente ni el proyecto de Ghidra. Fuentes: servidor de
consultas Ghidra (`127.0.0.1:18490`, `.ps1`) y capstone sobre el exe (solo lectura, `rpt0/pe.py`).
Scripts propios en el scratchpad de la sesión: `tr.py` (traza de `.xtls` siguiendo `jmp`),
`scan70.py`/`scan70r.py`/`scancn.py`/`scancn2.py` (lectores/escritores de `Status+0x70`),
`scan7f.py` (constantes `0x7FFFFFFFFFFFFFFF`), `errtab2.py` (nombres de códigos de error).

Marcas: [E] probado en `.rsrc`; [E-x] leído en `.xtls` (legible, mutado); [S] inferencia.

## 0. Resumen

- **`characterNo` no está en la partida.** Lo inventa el servidor al cargar el componente de lobby:
  `ServerLobbyActorComponent::LoadSaveData` `0x2B7F4D0` escribe la constante en
  `0x2B7F66C: mov rax,0x7FFFFFFFFFFFFFFF` → `[rsp+0x70]` y la pasa como `&characterNo` al alta de
  la entrada del lobby `0x2143E70` (→ `.xtls 0xE4A20F0`) [E/E-x]. Por eso **cambiar de ranura no
  cambia el `characterNo`**: cualquier usuario en un jugador recibe `0x7FFF…`.
- **El servidor no deja entrar con otro `characterNo` "a pelo".** El 3093 se valida contra la lista
  del lobby **de ese usuario**: si no está → `eErrNoInvalidCharacterNo` (`[0x6CF6C04]`), Nak 1013 y
  logout del personaje 2 (§2.2) [E-x].
- **Hay dos tablas globales indexadas por `characterNo` que devuelven el primero que coincide:**
  cliente `0x8AEB30` (caché de equipo) y **servidor `0x2A83F80`** (respuesta a
  `ClientCacheReq "cacheEquipSlotList"` vía `EquipSlotLIstCacheServer` `0x27779F0`) [E]. Con dos
  personajes `0x7FFF…`, ambos lados resuelven al personaje del anfitrión. Es el fallo de equipo ya
  visto [V histórico].
- **"Personaje local" no depende de `characterNo`** (puntero `ClientActorManager+0x50`, marca
  `actor+0x96`) [E]. El nombre ("Kliff") sale de los datos estáticos del personaje, no de la ranura.
- **Recomendación:** gancho `.rsrc` en `0x2B7F4D0` (post-llamada) o mid-hook en `0x2B7F676` que,
  **solo si el dueño del componente es el usuario 2**, ponga otro valor (p. ej.
  `0x7FFFFFFFFFFFF002`). Además, que el 3093 del usuario 2 lleve ese mismo valor (el guion lo toma
  de 2869 @5, o el túnel lo reescribe). Detalle y riesgos en §4.

## 1. Cliente (el juego del anfitrión): qué se indexa por `characterNo`

Dónde vive: `Status = [[actor+0x68]+0x20]`; `Status+0x70` es un **puntero** a un u64 en el heap,
creado con `-1` en el constructor `0x178F1D0` (`param_1[0xe]`). `Status+0x78` es el nombre
(wstring, por defecto `"$NotLoginCharacter$"`) [E].

| Sitio | Qué hace | Con `characterNo` repetido |
|---|---|---|
| `0x1790FA0` (Status vf `0x58`, unpack de `ActorLoadDataAck`): `0x1791192 mov rsi,[rdi+0x70]` … `0x17911B5 mov [rsi],rax` | escribe el `characterNo` del registro en `*Status+0x70`; en `0x17910F0` el nombre en `+0x78` [E] | el Kliff remoto queda con `0x7FFF…`, igual que el local |
| `0xB742B0` (`CharacterStatAck` 2358, `0xB73D70`) | reescribe `*Status+0x70`, el nombre (`+0x78`), varios stats, y además la **global `0x6C8CD98`** con su último u32, sin mirar de qué actor es [E] | si el servidor la manda a la sesión 1 para el pj 2 (no comprobado [S]), pisa esa global |
| `0x8AEB30` `ClientActorManager::FindByCharacterNo` (bucle `0x8AEC56–73`) | **primer** actor de la lista del gestor con ese número. Si ese primero no está en estado `0x10`, devuelve vacío (`0x8AEC8B`) y no sigue buscando [E] | resuelve al anfitrión o al pj 2 según el orden de la lista |
| `0xAA7F20` `EquipSlotListCacheData::Apply` → `0x8AEB30` | sobrescribe el componente de equipo del actor encontrado [E] | puede aplicar la lista de uno sobre el otro |
| `0xAA8020` (vf de `EquipSlotListCacheData`, `0x55C9F28`) → `0x8AEB30` | instantánea de la caché (serie `+0x18`) sacada del primer actor coincidente [E] | se guarda en la caché la serie del actor equivocado |
| `0xAA90D0` `EquipSlotListCache_Lookup` (desde `0x97FE40`, deserialización de EquipSlot, `0x980062`) + `0xAA8E40` | entrada `(characterNo, serie)` (`0xAA8E88/8E`). Si la serie es vieja: invalida, **borra el fichero de caché en disco** (nombre hecho con la entrada, `0xAA95E0`) y manda `ClientCacheReq` [E] | el pj 2 y el anfitrión comparten la entrada y el fichero |
| `0x632110` (desde `ReviveLoadingAck` `0xB6FA60`) | copia el `characterNo` del jugador local en `[[WorldSystem]+0xD0]+0xC90` [E] | solo el local; sin choque |
| `0xFA9C80` | compara el `characterNo` del jugador local (`0x8B44E0`) con `+0xC90` (UI) [E] | solo el local |
| `0x8B44E0` GetLocalPlayer | devuelve el puntero `ClientActorManager+0x50`; no usa `characterNo` [E]. Marca `actor+0x96`, puesta solo en `0x46B271` (`optD-mundo.md`) | sin choque |

Alcance de la búsqueda: tres patrones de bytes sobre todo `.rsrc` (`[r+0x68]→[r+0x20]→[r+0x70]→[r]`;
escrituras `mov [ptr(+0x70)],x`). Salen 30 lectores; **solo 3 comparan** el número: `0x8AEC70`
(cliente), `0x2A840A9` y `0x2C3E0DA` (servidor) [E]. La búsqueda no es exhaustiva: quedan fuera
otros patrones y `.xtls`.

**¿Se espera que un personaje remoto tenga un `characterNo` distinto?** Sí [S fuerte]. En la ruta
MMO, las entradas de lobby salen de una consulta a la base de datos (`0x27863C0`, `0x2B7EFA0`;
clase `LobbyCharacterFetchResultSet`), con un número por personaje. La caché por `characterNo` (en
memoria y en disco) y las búsquedas del "primero que coincide" solo funcionan si el número es
único en todo el mundo. El `0x7FFF…` constante es lo propio de un solo jugador.

## 2. Servidor: dónde se asigna y valida (`LoginCharacterToFieldServerReq` 3093)

### 2.1 Asignación [E/E-x]

- `0x2B7F4D0` (vf `0x50` de `ServerLobbyActorComponent`, vtable `0x5B2BE58`; slot `0x5B2BEA8`):
  - para cada `LobbySaveData`: el nombre sale de los datos estáticos del personaje (`0x389570` →
    vf8 → `cdmp_Utf8ToUtf16`);
  - `0x2B7F66C` constante `0x7FFF…` en `[rsp+0x70]`;
  - `0x2B7F6DB call 0x2143E70(comp, &res, &characterNo, &charKey, nombre, …)`.
- `.xtls 0xE4A20F0` (alta en el lobby):
  - recorre `[comp+0x18]` (entradas de `0xD0` B, u64 `characterNo` en `+0`) y rechaza si el número
    ya está **en esa misma lista** (`0xE4A214D`);
  - después monta la entrada (`+0` número, `+8` u16 clave, `+0xA` nombre `wchar[0x1F]` por
    `wcsncpy_s`) y la añade (`0xE4A22A2`).
- La partida **no tiene** campo `characterNo`. `LobbySaveData` = `_generateNo` (int64, `0x1878B20`),
  `_gameSaveVersion` (`0x1879050`) y `_slotSaveData` (`0x18795E0`). En la tabla de nombres
  reflejados de `0x57DBE00–0x57DC200` no aparece `_characterNo` [E].
- `scan7f.py`: en `0x2000000–0x2D00000` el único `mov r64,0x7FFFFFFFFFFFFFFF` ligado a esto es
  `0x2B7F66C` [E, solo esa forma de instrucción].
- Otros llamadores de `0x2143E70`: `0x9D9400` (cliente, `CreateCharacterAck`), `0x27863C0` y
  `0x2B7EFA0` (servidor, rutas MMO desde la base de datos).

### 2.2 Validación en el 3093 [E/E-x]

1. `0x2C3E030`: recorre los hijos del usuario (`0x2C3E0DA cmp rsi,[rax]`). Si encuentra uno y
   **no** es un jugador: `eErrNoCharacterAlreadyLogined` (`[0x6CF6B80]`). En un jugador sigue igual.
2. Un jugador (`0x2C3E0FF`): vacía la lista de lobby del usuario (`0x9D9B30`) y recarga la ranura
   (`0x2C3E1CE call 0x186F570`). El lobby **se vuelve a llenar con `0x7FFF…`** (§2.1).
3. `0x2C3E38A call 0x2A82F90` → `.xtls 0x1052D2B0`. **No usa el `characterNo`**: `r9` se pierde y
   solo llama a `0x2A7D9F0` (`ReuseFromUserPool`) con `(usuario, out, [SAM+0x50]+0x18)` [E-x].
4. `0x2C3E4AD call 0x2940EE0` → `.xtls 0x1042AAF0(Status, &res, &characterNo, ranura)`:
   - toma el lobby del usuario: `[[[Status+8]+0xA0]+0x68]+0x10`, lista `+0x18`;
   - busca el número (`0x1042AB60–AB76`, paso `0xD0`, `cmp [rcx],r9`);
   - **si no está: `eErrNoInvalidCharacterNo` `[0x6CF6C04]`** (`0x1042AB7F`);
   - si está: vuelve a cargar la ranura en el personaje (`0x1042ABF3 call 0x186F570`, gestor
     `[[0x6D696A0]+0x30]`) y luego llama a `0x2941020` [E-x].
5. Si falla: en `0x2C3E4B2…` se pone `pj+0x98` = error y `+0x9C = 0xB`, y se llaman vf80/vf110
   (logout del pj 2), `MainPlayerDataAck` con error y Nak `0x3F5` (si el error es de tipo 1) [E].

**Respuesta a la pregunta 2:** un 3093 inyectado con otro u64 **falla** con
`eErrNoInvalidCharacterNo`, porque el servidor busca el número en la lista de lobby que acaba de
cargar de la partida. Para que valga, el lobby del usuario 2 tiene que contener ese número.

### 2.3 Tablas del servidor indexadas por `characterNo`

- **`0x2A83F80`**: lista `[[[exe+0x6D696A8]]+0xB8]`; devuelve el **primero** con
  `*Status+0x70 == n`, sin distinguir usuario [E].
  - Único llamador: `0x27779F0`, vf1 de `pa::EquipSlotLIstCacheServer` (vtable `0x5B048D0`). Saca
    la serie y la lista de equipo con `0x20C42A0`.
  - Así que el `ClientCacheReq "cacheEquipSlotList"` del pj 2 recibe el equipo del anfitrión, o al
    revés. Encaja con lo observado ("el servidor contestó con la lista del jugador local",
    `optD-mundo.md`) [E + V].
- **Marcas de dueño en objetos:** `0x2AE1010` devuelve `(tipo 2, characterNo)`; lo usan la creación
  de objetos `0x2B6D620` y `0x2AE1650 → 0x20C4B20` [E]. Los objetos nuevos del usuario 2 llevan
  el mismo dueño que los del anfitrión. Efecto real desconocido [S]; `ItemSaveData` no tiene un
  campo de dueño visible.
- El resto de lectores (`0x27ADA50`, `0x27B2420`, `0x27B2FA0`, `0x2871B70`, `0x28B7040`,
  `0x28D57B0`, `0x2940B10`, `0x2AE26D0`, `0x2AE3EF0`, `0x2AE5970`, `0x2AEA410`, `0x2B87610`,
  `0x2BAFF30`, `0x2BE4C10`, `0x2BED1C0`, `0x2BF4540`, `0x2C0EF90`, `0x2C11A30`, `0x2C12670`) solo
  copian el número. Casi todos con `_time64` + `HashLittle` + `0x2776D70`: registros o telemetría
  [S]. No comparan.
- **Sin cerrar:** no encontré quién escribe `*Status+0x70` del personaje en el servidor. En `.rsrc`
  solo están `0x293FB30`/`0x293FE48`, que lo ponen a `-1`. Estará en `.xtls` [S]. Como el número
  tiene que coincidir con una entrada del lobby (paso 4), lo esperable es que el pj 2 acabe con el
  valor del lobby/petición.

## 3. Otras identidades que coinciden con la misma ranura

| Campo | Origen | ¿Choca? |
|---|---|---|
| Nombre del personaje | datos estáticos por clave de personaje (`0x2B7F4D0`, `0x389570`) → lobby `+0xA` → `Status+0x78` (`0x17910F0`) [E] | sí ("Kliff"), solo visual. Solo se comprueba que no se repita al crear o renombrar (`eErrNoDuplicateCharacterName`, `0x21FA720`/`0x2209C20`), no en el login [E] |
| `_customDisplayName` | `SlotSaveData` (`0x57DBFD8`) [E] | nombre de la ranura, solo en el menú |
| wstring del 3093 @17 (`0x4242`) y u32 @13 | `0x2135420` → `[[pj+0x68]+0xB8]`, `+0x124` [E] | iguales; significado desconocido |
| Cuenta u64 (2490 @0) | `0x2A838E0(SAM,&cuenta)`: si ya existe → **relogin** `0x2C17AB0` (`opcion-d-login.md`) [E] | sí, si no se reescribe. El mod ya pone la cuenta 1 |
| Nombre de usuario (2490 @13) | `0x2C187A0` → componente de usuario `+0x88` [E] | vacío en los dos |
| Nombre de familia | solo la cadena `"FamilyName"` `0x5223FC0`, sin código que la use [E] | no se usa |
| Claves de actor | SAM, índice libre: `0x901002xx`/`0xA01002xx` [V] | distintas |
| "Es mío" | cliente: `ClientActorManager+0x50`, `actor+0x96`, `actor+0x90` (usuario que controla). Servidor: `field+0x180`, actor de `TrocParams` por sesión [E] | ninguno usa `characterNo` |
| `itemNo` | igual en las dos partidas (`apariencia-estado-coop.md` §5.3) | pendiente (E3 §5) |

## 4. Recomendación para el mod

**Opción elegida: gancho de servidor en la carga del lobby, solo para el usuario 2 (`.rsrc`).**

- **Dónde:** detour de entrada en `0x2B7F4D0` (`.rsrc`; se llega por la vtable, no hace falta tocar
  `.xtls`). Después de llamar al original:
  - si `owner = [comp+8]` no es el usuario del anfitrión (clave `[owner+0x60]` ≠ la del
    anfitrión, o = la del usuario 2 que ya conoce `session2`), recorrer `[comp+0x18]`
    (`{ptr, u32 n @+8}`, paso `0xD0`);
  - cambiar `u64 @+0` de `0x7FFFFFFFFFFFFFFF` a `0x7FFFFFFFFFFFF000 | n`, empezando en `n = 2`.
    `…F001` ya lo usa la marioneta del cooperativo.
  - Alternativa más pequeña: mid-hook en `0x2B7F676` (justo después de `mov rax,imm64`) que cambie
    `rax` con la misma condición (`r15` = componente).
- **Coherencia del 3093:** el servidor envía al usuario 2 su `CharacterLobbyDataAck` 2869 con el
  número nuevo en @5. El guion (`login_e3*`) debe copiar ese valor al 3093 @1 en vez de la
  plantilla (`0x7FFF…`). Si no, que el túnel reescriba el 3093 de la sesión 2. Un cliente real (P8)
  lo hará solo.
- **Momento:** el 3093 de un jugador vacía y recarga el lobby antes de validar (§2.2, pasos 2 y 4).
  Por eso basta con tener el gancho activo antes del 3093. Para que el 2869 de E2 ya lleve el valor
  nuevo, hay que activarlo antes de `LoginUserToFieldServerReq` (carga en `0x2C17881`).
- **Qué arregla:**
  - el cliente del anfitrión recibe el pj 2 con otro número (`ActorLoadDataAck` +0x17 [S],
    `0x1790FA0`);
  - `0x8AEB30` y `0x2A83F80` resuelven a personajes distintos;
  - la caché de equipo y su fichero en disco quedan separados.
- **Riesgos:**
  - **R-a:** no sé dónde copia el servidor el número a `Status+0x70` (`.xtls`, §2.3). Antes de
    seguir, comprobarlo en vivo: leer `*[[[pj2+0x68]+0x20]+0x70]` y el u64 +0x17 del registro en el
    túnel.
  - **R-b:** código que espere la constante de un jugador. No encontré comparaciones con
    `0x7FFF…` en el servidor (`scan7f.py`, solo esa forma). La copia por `ReviveLoading` en el
    cliente del usuario 2 (`0x632110`) y la UI `0xFA9C80` comparan contra el suyo propio [S: bajo].
  - **R-c:** la partida no guarda el número (§2.1), así que no se persiste nada. El
    `ClientCacheReq` nuevo del anfitrión para el número del pj 2 debería recibir la lista correcta
    del pj 2 por `0x2A83F80` [S]. Deja un fichero de caché nuevo en la carpeta de caché del
    anfitrión.
  - **R-d:** como en el resto de ganchos `.rsrc`, la integridad de Denuvo. Nada en `.xtls` ni en
    *thunks*.
- **Descartado:**
  - **Solo reescribir el 3093 en el túnel:** falla con `eErrNoInvalidCharacterNo` y saca al pj 2
    (§2.2).
  - **Otra ranura para el usuario 2:** no cambia el `characterNo` (constante al cargar). Solo
    separa el aspecto, el equipo y el progreso; sigue siendo útil para P9, pero no para C2.
  - **Enganchar `.xtls 0x1042AAF0`/`0xE4A20F0`:** prohibido.
- **Plan B (solo cliente, sin gancho en el servidor):** en la recepción de la sesión 1 (`0x35CA00`),
  reescribir el u64 de `ActorLoadDataAck` +0x17 (y sus copias en el cuerpo) de los registros del
  usuario 2. Es lo que ya hizo la marioneta con `…F001` (`apariencia-estado-coop.md` §5.4,
  `optD-mundo.md` RM 4e).
  - Separa la caché del cliente.
  - Pero si el anfitrión pide la caché para ese número, `0x2A83F80` no encuentra a nadie con él en
    el servidor (respuesta vacía [S]).
  - Hay que reescribir también los Ack posteriores que lleven el número.

## 5. Direcciones (build 2976)

| RVA | Qué es |
|---|---|
| `0x2B7F4D0` / `0x2B7F66C` / `0x2B7F6DB` | carga del lobby / constante `0x7FFF…` / alta de la entrada (punto de gancho propuesto) |
| `0x2143E70` → `.xtls 0xE4A20F0` | alta en el lobby (rechaza duplicados solo en la misma lista; entrada de `0xD0` B) |
| `0x2C3E030` (`0x2C3E0DA`, `0x2C3E38A`, `0x2C3E4AD`) | 3093: búsqueda entre los hijos, creación y preparación |
| `0x2A82F90` → `.xtls 0x1052D2B0` → `0x2A7D9F0` | creación del pj (no usa el `characterNo`) |
| `0x2940EE0` → `.xtls 0x1042AAF0` (`0x1042AB60–AB7F`) | valida el `characterNo` contra el lobby; `eErrNoInvalidCharacterNo` |
| `0x6CF6C04` / `0x6CF6B80` / `0x6CF71A0` | `eErrNoInvalidCharacterNo` / `eErrNoCharacterAlreadyLogined` / `eErrNoInvalidCharacterInfo` |
| `0x2A83F80`, `0x27779F0` (vtable `0x5B048D0`) | servidor: primer actor por `characterNo`; respuesta de la caché de equipo |
| `0x2AE1010` | servidor: identidad del dueño `(2, characterNo)` para objetos |
| `0x178F1D0` | constructor de `CommonStatus` (`+0x70` = puntero al número, inicial `-1`; `+0x78` nombre) |
| `0x1790FA0` (`0x17911B5`) | unpack de Status: escribe el `characterNo` del registro |
| `0xB73D70` (`0xB742B8`) | `CharacterStatAck`: escribe el número, el nombre y la global `0x6C8CD98` |
| `0x8AEB30`, `0xAA7F20`, `0xAA8020`, `0xAA90D0`, `0xAA8E40` | cliente: búsqueda por número y caché de equipo |
| `0x8B44E0` | cliente: jugador local = `ClientActorManager+0x50` |
