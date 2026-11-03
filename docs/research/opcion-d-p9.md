# Opción D, pieza P9: la partida del invitado en el servidor del anfitrión (diseño estático)

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Solo lectura del exe en disco (capstone, `build/research/cddis.py` y scripts propios en
`build/research/optd/p9/`). No se abrió el juego ni se ejecutó `deploy.ps1`. De la carpeta de
partidas solo se listaron nombres y tamaños y se leyeron los primeros 64 bytes de un `save.save`.

Leyenda: **[E]** leído en desensamblado normal (`.rsrc`); **[E-x]** leído en código mutado de
`.xtls` (legible); **[VM]** función virtualizada por Denuvo (no legible); **[S]** suposición
razonada; **[R]** fuente externa (sin contrastar con nuestro exe); **[V]** visto en disco.

---

## Resumen y recomendación

1. **La cadena de carga es entera legible salvo el descifrado**: `0x186F570` → `0x20688A0` →
   ranura `"slotN"` del almacén `S = [gestor+0x50]` → *commit load* del manejador Steam
   (`.xtls 0xEF601B0`) → `0x2357DA0` lee de disco `saveRoot/slotN/{lobby,save}.save` con el
   `FileSystem` global (`CreateFile2` + `ReadFile`) → valida la cabecera de 0x80 B → `0x235C510`
   → **`0x235C2E0` [VM]** (descifrado + HMAC) → LZ4 (`0x12C61D0`) → texto plano en
   `fileObj+0x120` → `0x235BF10` lo entrega a `0x20688A0`, que monta un `MemoryArchive` y llama a
   `componente->vf(0x50)`.
2. **La clave no depende de la cuenta de Steam** (pregunta 10):
   - [E] el SteamID solo aparece en la **ruta** (`saveRoot = <LocalAppData>/Pearl Abyss/CD/save/<accountID>`,
     formato `"%#/%#/save/%#"`). La única "clave" que llega al descifrado es la cadena `S+0x78`, la
     misma para todas las ranuras; no he encontrado quién la escribe.
   - [E] el juego tiene **CrossSave** (subir/bajar partidas a servidores de Pearl Abyss entre
     plataformas): los bytes bajados se descifran con el mismo `0x235C510` y la misma `S+0x78`.
     Una clave por SteamID rompería eso.
   - [R] el editor de partidas público (`ben_save_decrypt.py`) descifra con ChaCha20 + HMAC-SHA256,
     y la clave sale **solo de la versión de la cabecera** (constantes fijas), sin SteamID.
   - Conclusión: **los ficheros de Shadow deberían cargar tal cual en el anfitrión** [E + R].
     Confirmarlo en G3b es barato (§6).
3. **Recomendación: servir los bytes cifrados tal cual, desde un número de ranura reservado para
   el invitado (`Ng`), y redirigir esa ruta en los hooks de ficheros de P6.** Así el juego
   descifra, valida y reparte con su propio código, sin nada nuestro en medio:
   - hook en la **entrada de `0x186F570`** (y de `0x186E450` para guardar): si el actor es del
     usuario 2, cambiar la ranura por `Ng` (propuesta: 207 [S]);
   - en el hook de imports de P6 (`CreateFile2`, `CreateFileW`, `MoveFileExW`, `DeleteFileW`,
     `CreateDirectoryW`…), la ruta `…\save\<id>\slot<Ng>\` **no se bloquea sino que se redirige**
     a una carpeta de intercambio fuera de la carpeta de partidas (por ejemplo
     `%LOCALAPPDATA%\cdmp\guest\slot<Ng>\`), donde el mod deja los ficheros que llegan por el
     túnel;
   - antes del primer uso, crear la entrada `"slot<Ng>"` en el almacén con la función del propio
     juego `0x2355EC0(S, "slot<Ng>", 0)` (obtener o crear; cuerpo `.xtls` legible, no VM).
   - Esto es más robusto que inyectar el búfer ya descifrado: no depende de la clave, del formato
     del `MemoryArchive` ni de la propiedad de los búferes (que el juego libera con su propio
     asignador). La alternativa en memoria (hook en `0x2357DA0`) queda como plan B (§2.3).
4. **El progreso vuelve "gratis" por el mismo camino**: con `Ng` también en el guardado, el
   *commit save* del juego escribe `save.save`/`lobby.save` del usuario 2, ya cifrados por el
   juego, en la carpeta de intercambio. El mod los manda al invitado (§5).
   - Corrección a E3: `0x186E1A0` **no está virtualizado**. Es un salto a `.xtls 0xCBD84D0`
     (legible), que llama a `0x186E450` (`.rsrc`, prólogo normal): ese es el punto legible.
5. **Riesgos nuevos**:
   - la ranura del autoguardado es **global**: rota entre 0-2 con la tabla del gestor
     (`0x1871850`), así que hoy el usuario 2 escribiría **en una ranura de autoguardado del
     anfitrión**. La remapeo a `Ng` lo evita;
   - **la ranura 3 es la de CrossSave**: guardar en ella sube los ficheros por HTTP a Pearl Abyss
     (`0x15AC100`, `UploadCrossSaveFileAsync`). Nunca usar 3 (ni 206 = `0xCE`, que se borra tras
     cargar).

---

## 1. Cadena de carga [E salvo donde se indica]

### 1.1 De la Req a la ranura

```
LoginCharacterToFieldServerReq::vf2 0x298BA10 → 0x2C3E030
  → 0x186F570(gestor=[[exe+0x6D69698]+0x48], &res, ranura, ownerActor)        [E]
       comprueba ranura (0-2, 3, 100-108, 200-207) y guarda [gestor+0x60]
       nombre = sprintf("%#%#", "slot", ranura)  → "slot0", "slot207"…        (0x66EFB0, fmt 0x558EE38, "slot" 0x57A8080)
       0x20688A0(ownerActor, &res, S=[gestor+0x50], "slotN", ranura)          [E]
```

- `S` es el **almacén de partidas** (ctor `0x2354FA0`, 0x198 B; lo crea el ctor del gestor
  `.xtls 0xCBD2920`):

  | Campo de `S` | Contenido |
  |---|---|
  | `+0x10` | manejador `GamePlaySaveDataFileHandler_Steam` (vt `0x59840C0`) |
  | `+0x18` | mapa nombre→ranura (`GamePlaySaveData`, vt `0x5984168`) |
  | `+0x78` | cadena fija (0x105 B) que se pasa como "clave" al cifrado y al descifrado |
  | `+0x190` | objeto de trabajo con lock |

- `0x20688A0` (`d_20688a0.asm` de E3) [E]:
  - busca la ranura por nombre en `S+0x18`. **Si no existe, falla** (no la crea);
  - elige el fichero: `"lobby"` si `byte [[owner+0x88]+1] == 0` (el `ServerUserActor`) y `"save"`
    si no (el personaje). Cadenas `0x590E5EC`/`0x590E5F4`;
  - modo de fichero combinado (`byte [exe+0x6CEEED8]`, el de este PC: solo hay `lobby.save` y
    `save.save`) [V + E]:
    - si `[ranura+0x69C] == 0`, registra una carga pendiente (`0x235B620` → `.xtls 0xEF695C0`,
      lista `ranura+0x6A0`);
    - **`0x2355700(S, ranura)`** (→ `.xtls 0xEF51850`) hace el *commit*: `vf20` Commit Load,
      `vf48` Valid (cuota de Steam Cloud), `vf28` Commit Save y `vf30` Commit Delete. Los mensajes
      de error son `"[SaveData] Commit Load 실패"`…;
    - `0x235BF10` (→ `.xtls 0xEF6DFC0`) saca el texto plano de `fileObj+0x120` (lo **mueve**, o
      lo copia si `fileObj+0x138`);
    - `MemoryArchive` (`0x1874850`, `0x1294A70`, `0x1296C80`) → por componente
      `vf(0x50)(res, versión=[ranura+0xA8], archivo, ranura)`.

### 1.2 Lectura y descifrado (manejador Steam)

| vf | Dirección | Qué es |
|---|---|---|
| `+0x08` | `0x235AB00` → `.xtls 0xEF62160` [E-x] | Init(usuarioPlataforma): `saveRoot = sprintf("%#/%#/save/%#", <LocalAppData…>, <"CD">, usuario->vf20())` (fmt `0x5714110`, log `"saveRootPath: %#"`); `+0x148` = interfaz de Steam (`GetQuota`/`GetFileCount`) |
| `+0x20` | `0x2356F40` → `.xtls 0xEF601B0` [E-x] | **Commit Load**: ruta `"%#/%#"` = `saveRoot/slotN`; `0x235A800` intenta restaurar copias (`_new`/`_backup`, `"[SaveData] 복구 실패!"`); **`0x2357DA0`** lee |
| `+0x28` | `0x23571D0` [E] | **Commit Save**: escribe con temporal + copia de seguridad + renombrado (`_new`, `_backup`, `_delete`); por fichero `0x2358750` → `0x235C7A0` |
| `+0x30` | `0x2357C60` [E] | Commit Delete |
| `+0x48` | `0x235AE00` → `.xtls 0xEF652C0` [E-x] | Valid: cuota de Steam Cloud y menos de 10.000 ficheros |

`0x2357DA0(handler, &listaFicheros, rutaRanura)` [E], por cada fichero:
1. ruta = `rutaRanura/<nombre>.save` (`0x235CAC0`);
2. abre con el `FileSystem` global `exe+0x6A85460` (`0x1326620`; por debajo `CreateFile2` en
   `0x13274B0` y `ReadFile` en `0x13277F9`/`0x1327A9D`) [E];
3. lee 0x80 B y **valida la cabecera**:
   - `"SAVE"` (`exe+0x5547B6C`);
   - versión 1..2;
   - `u16 +6 == 0x80`;
   - `flags +0xC & ~2 == 0`;
   - `+0x12` (tamaño descomprimido) ≠ 0;
   - `+0x16` (tamaño cifrado) ≠ 0 y ≤ `+0x12`;
4. copia la cabecera a `fileObj+0x98..+0x118`;
5. lee el resto y llama a **`0x235C510(fileObj, S+0x78, &búfer)`**:
   - `0x235C2E0` → **[VM]** `.xtls 0xEF713F0` (descifra y comprueba; `"[SaveData] 처리 실패 [%#]"`);
   - si `flags & 2`, LZ4 con `0x12C61D0` (`"압축 해제 실패"`);
   - deja el texto plano en `fileObj+0x120` (`{ptr, u32 tamaño, u32 cap, u8}`).

Cabecera vista en `slot0/save.save` de este PC (64 B) [V]:

```
53 41 56 45 | 02 00 | 80 00 | 00000000 | 02000000 | 0000 | d4224000 | fe0b1200 | a5c86e3f…
"SAVE"       ver 2   hdr 0x80  +8 = 0   flags=2 (LZ4)  +0x10  desc=4.203.220  cif=1.182.718 (= 1.182.846 − 0x80)
```

[R] Según `ben_save_decrypt.py`:
- `+0x1A` es el nonce de 16 B y `+0x2A` el HMAC-SHA256 de 32 B (sobre el texto comprimido);
- el cifrado es ChaCha20;
- la clave = constante XOR (prefijo de la versión + `"PRIVATE_HMAC_SECRET_CHECK"`).

Esas constantes **no están en claro en el exe** (se buscaron; viven dentro de la VM).

### 1.3 ¿Depende la clave del SteamID o de la cuenta? No [E + R]

- La carpeta `<id-cuenta>` es el `accountID` de Steam (la parte baja del SteamID64). Sale de
  `usuarioPlataforma->vf20()` en Init y **solo forma la ruta**.
- El cifrado y el descifrado reciben la cadena `S+0x78`. La inicializa vacía `0x2354FA0`; no
  encontré quién la escribe (ni en el gestor ni en el almacén). Es la misma para todas las ranuras
  y para CrossSave.
- **CrossSave**: `0x23564F0` (llamado desde `setCrossSaveData` `0x15AE0A0`) instala bytes
  **cifrados bajados de internet**:
  - solo en la ranura 3;
  - valida la misma cabecera;
  - llama al mismo `0x235C510(fileObj, S+0x78, …)`.

  La subida (`0x186E450`, ranura 3) cifra con `0x235C7A0(fileObj, S+0x78, …)` y llama a
  `UploadCrossSaveFileAsync` `0x15AC100`. Un fichero hecho en otra plataforma se descifra aquí,
  así que la clave no puede ser el SteamID.
- Imports: `steam_api64` solo trae `SteamInternal_*`/`RunCallbacks`. `bcrypt` solo trae hash
  y aleatorio (no hay `BCryptDecrypt`). El cifrado es propio y está en la VM.
- **Queda una duda razonable** [S]: que `S+0x78` sea un secreto por cuenta de **Pearl Abyss**
  (CrossSave exige vincular cuenta). Lo contradice [R], que descifra cualquier partida sin datos
  de cuenta. Prueba barata en §6 (G3a).

---

## 2. Punto de inyección para el usuario 2

### 2.1 Opciones evaluadas

| Opción | Dónde | Pros | Contras |
|---|---|---|---|
| **A (recomendada)** Ranura reservada `Ng` + redirección de ruta | entrada `0x186F570` (cambiar `r8d`) + hooks de import de P6 | Cero llamadas a internos para leer. El juego valida, descifra y reparte. Mismo mecanismo para guardar (§5). La redirección es por ruta, así que da igual el hilo que haga la E/S | Hay que crear `"slot<Ng>"` en `S` (una llamada a `0x2355EC0`). Hay que elegir un `Ng` que no exista en el disco del anfitrión |
| B: servir los bytes cifrados en memoria | entrada `0x2357DA0` (si `rutaRanura` es la de `Ng`) | Sin disco | Hay que replicar ~40 instrucciones del original: búferes del asignador del juego (`0x1326400`), copia de la cabecera y llamada a `0x235C510`, que contiene la VM. Es frágil |
| C: texto plano | entrada `0x20688A0` o cambiar `fileObj+0x120` | Ninguno real | El búfer lo libera el juego (asignador propio, `0x1447F02BC`). Hay que tocar `+0x69C/+0x698/+0x138` de la ranura y no pisar la ranura del anfitrión con hilos concurrentes. **Descartada** |
| D: copiar a una ranura libre del disco del anfitrión | — | — | Escribe en la carpeta de partidas y Steam Auto-Cloud lo sube. **Descartada** (ya lo era en la guía) |

### 2.2 Diseño de la opción A [E + S]

1. **Hook en la entrada de `0x186F570`** (`48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44…`):
   - `rcx` = gestor, `rdx` = &res, `r8d` = ranura, `r9` = ownerActor;
   - si `r9 == usuario2` o `[r9+0xA0] == usuario2` (personaje del usuario 2):
     - si el blob del invitado no está listo, devolver error (`*rdx = [exe+0x6CF7CA4]`, el mismo
       de "ocupado") sin llamar al original;
     - si está listo: si `"slot<Ng>"` no está en `S+0x18`, llamar `0x2355EC0(S, "slot<Ng>", 0)`;
       después llamar al original con `r8d = Ng`;
   - Se ejecuta en el hilo del servidor y con la guarda `+0x60` aún sin tomar (la toma el
     original). La llamada a `0x2355EC0` va antes, en el mismo hilo. Riesgo bajo de carrera con un
     guardado del anfitrión: también usa la guarda, pero la creación de ranura no [S]. Para
     cubrirlo, crear la ranura en otro momento tranquilo: al recibir el blob, encolando una tarea al
     hilo del servidor.
2. **Hooks de import de P6** (ya previstos): `CreateFile2`, `CreateFileW`, `GetFileAttributes*`,
   `FindFirstFile*`, `MoveFileExW`, `DeleteFileW`, `CreateDirectoryW`, `RemoveDirectoryW`,
   `CopyFileW`, `SetFileAttributesW`.
   - Una ruta que empiece por `saveRoot\slot<Ng>` (con `/` o `\`, sin distinguir mayúsculas) se
     **reescribe** a `%LOCALAPPDATA%\cdmp\guest\slot<Ng>\…`;
   - el resto de `…\CD\save` sigue con `ACCESS_DENIED` mientras haya usuario 2;
   - el commit save usa `_new`/`_backup`/`_delete` y renombra: hay que redirigir los dos lados de
     `MoveFileExW`.
3. **Elección de `Ng`**:
   - válidas: 0-2, 3, 100-108, 200-207;
   - **prohibidas**: 0-2 (rotación del autoguardado, §5.1), 3 (CrossSave → HTTP), 206 = `0xCE`
     (se borra tras cargar, `0x1871040`), y cualquier `slotN` que exista en el disco del
     anfitrión (este PC: `slot0`, `slot1`, `slot2`, `slot100`);
   - propuesta: **207** [S] (rango "instancia", `_useInstanceSaveSlot`; no he leído su
     semántica). El mod comprueba al arrancar que no existe `slot207` en el disco.
4. **El blob**: `lobby.save` y `save.save` de la ranura que elige el invitado, sin tocar. Se
   escriben en la carpeta de intercambio antes de pasar la Req al servidor.
5. **`UserLogin` también carga** (E3 §1.1): `LoginUserToFieldServerReq` → `0x2C17520` llama a
   `0x186F570` con la ranura más reciente **del anfitrión** (t = 0,06 s, antes de la pantalla de
   título).
   - Con el hook, esa carga también va a `Ng`, así que el blob tiene que estar ya.
   - Propuesta: el mod del invitado manda la ranura más reciente al conectar el túnel (P8 ya exige
     el túnel antes de la `1001`). Si en el título elige otra, manda esa antes de su
     `LoginCharacterToFieldServerReq` (§4).

### 2.3 Plan B (si la redirección de ruta diera problemas)

Hook en la entrada de **`0x2357DA0`** (`4C 89 44 24 18 48 89 4C 24 08 55 53 56 57 41 54…`):
- si `r8` (ruta) termina en `/slot<Ng>`, recorrer la lista `[rdx]` (n = `[rdx+8]`, punteros a
  `fileObj`; nombre en `fileObj+0x28`, o `+0xB0` si no es nulo);
- por cada fichero, repetir lo que hace el original desde `0x2357F77`:
  - validar la cabecera y copiarla a `+0x98`;
  - `+0x118 = (versión == 1)`;
  - pedir un búfer del juego con `0x1326400(&buf, tamaño, "")`;
  - `memcpy` de la carga útil;
  - `0x235C510(fileObj, S+0x78, &buf)`;
- devolver `al = 1` sin llamar al original.

Solo lo llama el commit load (`.xtls 0xEF601B0`, una referencia).

---

## 3. Qué escribe la carga en estado global (partida distinta a la del anfitrión)

Lo ya establecido en E3 §1.2-§1.5 sigue valiendo: la carga escribe en los **componentes del
usuario 2**, y el mundo de la partida se aplica a **su propio `ServerField`** (`field+0x180` =
dueño). Con una partida **distinta**:

| Qué | Dónde se aplica | Efecto con campos separados (hoy) | Efecto si un día se mete al usuario 2 en el campo del anfitrión (`0x2B07790`) |
|---|---|---|---|
| Hora del mundo (`TransformSync+0x5D0`) | `FieldTime` global `exe+0x6A6D238` en `0x2BC0A30` (si es dueño) y `0x219D7F0` (sin mirar dueño) | Como en R1, pero ahora salta a **la hora del invitado**, distinta | Las ramas "solo dueño" ya no se aplican; `0x219D7F0` sí (mitigar con `LoadingCompleteReq` `00`) |
| Misiones, diálogos, conocimiento, facciones, inventario, habilidades, estado, secuenciador, SubLevel | componentes del usuario 2 | Aisladas [E, profundidad 1] | **Incoherencia de mundo**: las misiones del invitado esperan stages/gimmicks/NPC de *su* mundo, y el campo del anfitrión tendrá los del anfitrión. Riesgo de misiones bloqueadas [S] |
| Gimmicks y NPC del campo (`_fieldGimmickSaveDataList`, `_fieldSaveDataList`, `FieldNPCSaveData`, `LevelGimmickSceneObject*`, `Fog`) | se guardan en componentes del usuario y se aplican al campo del que es dueño [S] | En su campo propio | No se aplican (no es dueño). **Al guardar**, ¿qué se serializa? Si los toma del campo actual, el invitado se lleva a su partida el estado de gimmicks del anfitrión [S]. Es lo más delicado del progreso |
| Eventos globales (`GlobalGameEventSaveData`, `_globalGameEventDataList`, `TradeItemPriceRate`; cargadores cerca de `0x18B6xxx`/`0x18CDxxx`) | sin leer [S] | Si el cargador escribe en un gestor global, **los precios o eventos del anfitrión pasan a ser los del invitado** | Igual |
| Gestor de guardado (`+0x40` tabla de ranuras y tiempos, `+0x58` contador, `+0x5C`, `+0x70/+0x78` caché de la ranura del autoguardado) | global | La carga no lo toca [E]; el **guardado** sí (§5.1) | Igual |
| `characterNo`/`itemNo` | E3 §5 | Con otra partida pueden coincidir o no; las tablas son por usuario [E parcial] | Igual |

Pendiente estático corto [S]: leer los cargadores `vf(0x50)` a profundidad 2 buscando escrituras
en gestores globales (`GlobalGameEvent*`, `FactionNode*`, `NPCSchedule*`) con `globscan.py` de
E3. Para la etapa con campos separados no bloquea.

---

## 4. Lado del invitado: leer su ranura y mandarla

**Carpeta de partidas de este PC** (solo nombres y tamaños) [V]:

| Fichero | Tamaño |
|---|---|
| `save\<id-cuenta>\slot0\save.save` | 1.182.846 |
| `save\<id-cuenta>\slot0\lobby.save` | 506 |
| `save\<id-cuenta>\slot1\save.save` | 1.168.495 |
| `save\<id-cuenta>\slot1\lobby.save` | 506 |
| `save\<id-cuenta>\slot2\save.save` | 1.168.815 |
| `save\<id-cuenta>\slot2\lobby.save` | 506 |
| `save\<id-cuenta>\slot100\save.save` | 1.154.572 |
| `save\<id-cuenta>\slot100\lobby.save` | 441 |
| `save\<id-cuenta>\steam_autocloud.vdf` | 53 |
| `save\initialize_input.save` | 242 |
| `save\user_engine_option_save.xml` | 3.586 |

En Shadow será `%LOCALAPPDATA%\Pearl Abyss\CD\save\<accountID del invitado>\slotN\`, con **dos
ficheros por ranura**: `save.save` (~1,15-1,2 MB, cifrado y LZ4; ~4,2 MB en claro) y
`lobby.save` (~0,5 KB). Puede haber también `_new`/`_backup` si un guardado se interrumpió:
`0x235A800` los restaura [E].

Diseño [S]:
1. **Al conectar el túnel** (antes de la `1001`, requisito de P8):
   - el mod del invitado localiza su carpeta (la única subcarpeta numérica, o el `accountID` de
     `SteamUser()->GetSteamID()`);
   - elige la ranura con `save.save` más reciente (`mtime`; parecido a lo que hace `UserLogin`);
   - manda `SaveBlob{ranura, "lobby", bytes}` y `SaveBlob{ranura, "save", bytes}`, con SHA-256.
2. **Con el hook de `Send` de P8** (`0x14380B0`):
   - si la trama es `LoginCharacterToFieldServerReq` 3093, leer la ranura (`u32` en `+9`);
   - si no es la enviada, mandar sus dos ficheros y **retener** la Req hasta el `SaveBlobAck` del
     anfitrión, con un tiempo límite;
   - P8 §2.4 recomienda no retener Req. Aquí la espera es corta (1,2 MB por el túnel) y el cliente
     está en el título.
3. **Volumen**: `ReliableChannel` admite mensajes de hasta 256 KB, así que `save.save` va en ~5
   trozos con índice y total. Subir `kVersion` por el mensaje nuevo.
4. **Anfitrión**:
   - comprueba la cabecera (magic, versión, `+0x16 + 0x80 == tamaño`);
   - escribe los dos ficheros en `%LOCALAPPDATA%\cdmp\guest\slot<Ng>\` (fuera de la carpeta de
     partidas);
   - contesta `SaveBlobAck`.
5. **Sin escritura en el disco del invitado** en este paso: solo lectura.
6. Bloquear en el invitado **"Nueva partida"** (`NewGame`/`CreateCharacter`, `.xtls 0x1069F640`
   → `0x186F920`): en el servidor del anfitrión crearía y guardaría personaje. Detectar su Req en
   el hook de `Send` y no enviarla [S].

---

## 5. Progreso: devolver al invitado su partida actualizada

### 5.1 El camino del guardado, ahora legible [E / E-x]

```
SaveGameDataAutoTimer 2077 → 0x2C35EB0 → 0x28FC6F0(pj) → 0x186E1A0  (salto, no VM)
  → .xtls 0xCBD84D0(gestor, &res, pj, opciones, …)                         [E-x]
       ranura = (opciones & 4) ? gestor+0x5C (caché) : 0x1871850(gestor)
       0x1871850 → .xtls 0xCBDE990: rotación del autoguardado entre 0-2 con la tabla
         del gestor (+0x40, entradas de 0x68 B {ranura, hora}); reutiliza la de los últimos 10 s
       → 0x186E450(gestor, &res, ranura, pj, opciones)                      [E]
            0x2069640(usuario=[pj+0xA0], &res, S, "slotN")   → "lobby"
            0x2069640(pj, &res, S, "slotN")                   → "save"
               ├ 0x2355EC0(S, "slotN", 0)  obtener o crear la ranura en memoria
               ├ serializa componentes → 0x235AFE0 (plano a fileObj, pendiente de guardar)
               └ 0x2355700 commit → vf28 Commit Save 0x23571D0 → 0x2358750 →
                    0x235C7A0(fileObj, S+0x78, &out) = LZ4 + 0x235C090 [VM] + cabecera
                    → escribe saveRoot/slotN/<f>.save con _new/_backup/rename
            si ranura == 3: además cifra en memoria y UploadCrossSaveFileAsync (0x15AC100) ← NO
            si no y !(opciones & 4): 0x1870DF0 (posprocesado / registro en la tabla)
```

Consecuencias:
- **La ranura del autoguardado no es del usuario sino del gestor**: hoy el usuario 2 escribiría
  en `slot0..2` del anfitrión. P6 lo bloquea en disco, pero **la memoria del almacén (`fileObj` de
  esa ranura) queda con los datos del usuario 2** hasta el siguiente guardado del anfitrión [E].
  Si se levanta P6 con un commit pendiente, se escribiría.
- El *Commit Save* hace la E/S **dentro** del hilo del servidor que guarda, con la barrera de
  2077 (E3 §4).

### 5.2 Diseño recomendado [S]

1. **Hook en la entrada de `0x186E450`** (`4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48…`):
   - `rcx` = gestor, `rdx` = &res, `r8d` = ranura, `r9` = pj, `[rsp+0x28]` = &opciones;
   - si `[r9+0xA0] == usuario2`, llamar al original con **`r8d = Ng`**. Cubre el autoguardado,
     `SaveToFileCurrentPlayerReq` (`.xtls 0x103BE8E0`) y cualquier otro llamador;
   - el juego serializa y cifra con su clave, y escribe en `saveRoot\slot<Ng>`. La redirección de
     §2.2 lo manda a `…\cdmp\guest\slot<Ng>\`;
   - al volver con `res == 0`, el mod lee `save.save` y `lobby.save` de la carpeta de intercambio
     (con la secuencia `_new`→rename ya hecha) y los manda al invitado (`SaveBlob` en sentido
     inverso, con número de versión).
2. **Invitado**: recibe y guarda en `%LOCALAPPDATA%\cdmp\incoming\slotN\` (fuera de su carpeta de
   partidas). La aplicación a su carpeta real va en un paso aparte:
   - con el juego cerrado, o al cerrar con el `cdmp_peer`;
   - antes, copia de seguridad (regla 7);
   - y escritura atómica (temporal + rename).

   No escribir con su juego abierto: Steam Auto-Cloud y el propio almacén del invitado tienen la
   lista de ranuras en caché [S].
3. Alternativa si no se quiere el disco de intercambio en el guardado:
   - en el hook de `0x186E450` llamar al original con `opciones & 4` (no hace `0x1870DF0`);
   - a la vuelta, leer de memoria `fileObj+0x98` (cabecera) y `+0x120` (plano) de
     `S["slot<Ng>"]`.

   Pero el commit save **se ejecuta igual** dentro de `0x2069640` (§5.1), así que la redirección
   de ruta sigue haciendo falta, o habría que aceptar `ACCESS_DENIED` y su "restaurar" (`복구`).
   **La opción de ruta es la más simple.**
4. Hasta que esto esté probado: **el guardado del usuario 2 sigue bloqueado** (P6 y salto de 2077
   para el usuario 2, E3 §3.3) y el progreso se pierde al salir (regla actual).
5. **Abierto** [S]: si `lobby.save`/`save.save` llevan dentro el número de ranura, devolver la
   ranura `Ng` a la ranura `N` del invitado podría no cuadrar. Comprobarlo en G3d con el editor
   público (descifrar en Shadow, solo lectura, y comparar).

---

## 6. Riesgos y etapas de prueba

### 6.1 Riesgos

| # | Riesgo | Gravedad | Mitigación |
|---|---|---|---|
| P9-1 | La clave sí depende de algo de la cuenta (`S+0x78`) y el blob de Shadow no descifra | media (solo falla la carga: `"[SaveData] 처리 실패"`, logout del pj 2) | G3a: registrar el contenido de `S+0x78` en los dos PC. G3b falla limpio |
| P9-2 | El usuario 2 escribe en la ranura de autoguardado del anfitrión (0-2) | alta sin P6/remapeo | Remapeo `Ng` en `0x186E450` + P6 |
| P9-3 | Ranura 3 → subida CrossSave a servidores de Pearl Abyss | alta (sale del PC y usa la cuenta del anfitrión) | Lista negra de 3 y 206 en los dos hooks. Además, no dejar pasar ninguna `SaveToFileCurrentPlayerReq` del invitado con ranura 3 |
| P9-4 | `"slot<Ng>"` en el almacén del anfitrión aparece en su menú de cargar partida | baja | P8 dice que el título lee el disco; la carpeta real no existe. Verificar en G3c |
| P9-5 | Carrera: la creación de la ranura o la carga del usuario 2 coincide con un guardado del anfitrión (guarda `+0x60`) | baja | La guarda no bloquea: falla con Nak. Reintentar |
| P9-6 | Incoherencia de mundo (§3) al guardar o volver | media | Campos separados en esta etapa. Comparar partidas en G3d |
| P9-7 | Hooks de import que reescriben rutas: un fallo deja ficheros a medias en la carpeta de intercambio, no en la de partidas | baja | Comprobar el `SHA` y la cabecera antes de mandar |
| P9-8 | Tamaño: 1,2 MB por el túnel en el login | baja | Troceo a 256 KB |

### 6.2 Etapas (después de E3 / G3 del plan)

- **G3a (solo lectura, ningún cambio de flujo)**:
  - registrar en el anfitrión `S = [[exe+0x6D69698]+0x48]+0x50`, `S+0x78` (cadena) y `S+0x10`
    (vtable `0x59840C0`);
  - registrar la tabla de ranuras del gestor (`+0x40`, n `+0x48`) y `+0x5C/+0x70/+0x78`;
  - lo mismo en Shadow.

  Si `S+0x78` es igual o vacío en los dos, P9-1 queda descartado.
- **G3b (carga del blob, sin guardar)**:
  - P6 activo; el invitado manda su ranura; el anfitrión carga con `Ng`;
  - en el log: `"End Load SaveSlot"` sin `"[SaveData] … 실패"`;
  - en el servidor: inventario o nivel del pj 2 distintos de los del anfitrión (con los Ack
    `MainPlayerData…` que van a la sesión 2).

  Prueba previa sin Shadow: copiar a mano a la carpeta de intercambio una ranura **de este PC**
  (no la cargada) y cargarla como usuario 2.
- **G3c (aislamiento)**: comprobar los 32 B de `exe+0x6A6D238` antes y después, que el menú del
  anfitrión sigue igual y que no hay escrituras en `…\CD\save` (log del hook de P6).
- **G3d (guardado del usuario 2 a la carpeta de intercambio)**:
  - activar el remapeo en `0x186E450`;
  - forzar un autoguardado;
  - comprobar que aparecen `save.save`/`lobby.save` en `…\cdmp\guest\slot<Ng>\`, con la cabecera
    válida y una cuenta de bytes plausible;
  - nada en la carpeta real.
- **G3e (vuelta)**:
  - mandar los ficheros a Shadow, a `incoming` (sin aplicar);
  - con el juego de Shadow cerrado y copia de seguridad, aplicarlos a una **ranura libre** del
    invitado y cargarla en partida local normal (sin mod de sesión 2);
  - si carga, se aplica a la ranura original.

---

## 7. Direcciones (build 2976) y primeros bytes

### 7.1 Funciones propuestas para enganchar (entrada, `.rsrc`, prólogo normal)

| RVA | Qué es | Primeros bytes | Uso |
|---|---|---|---|
| `0x186F570` | cargar ranura (gestor, &res, ranura, ownerActor) | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44 89 44 24 18` | **P9**: `r8d → Ng` para el usuario 2 |
| `0x186E450` | guardar ranura (gestor, &res, ranura, pj, &opciones) | `4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4C 24 08` | **P5**: `r8d → Ng` para el usuario 2 |
| `0x2357DA0` | leer ficheros de ranura del disco (handler, &lista, ruta) | `4C 89 44 24 18 48 89 4C 24 08 55 53 56 57 41 54` | Plan B (§2.3) |
| `0x20688A0` | `processLoadDataFromFile` por componente | `48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54` | Solo para registrar (no recomendado modificar) |
| `0x2069640` | `processSaveDataToFile` por componente | `48 89 5C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08` | Solo para registrar |

Además: los imports de P6 (`CreateFile2` IAT `0x51EB158`, usado en `0x13274B0`; `CreateFileW`
`0x51EB428`; `ReadFile` `0x51EB430`; `WriteFile` `0x51EB448`; `MoveFileExW` `0x51EBE00`;
`DeleteFileW` `0x51EB198`; `CreateDirectoryW` `0x51EB1C0`; `FindFirstFileW` `0x51EB178`).

### 7.2 Funciones a llamar (no enganchar)

| RVA | Qué es | Nota |
|---|---|---|
| `0x2355EC0` | obtener o crear ranura `(S, nombre, flag)` → `.xtls 0xEF537A0` [E-x] → crea con `0x23559F0`/`0x2355BB0` | entrada `E9 DB D8 BF 0C` (salto). Llamarla, no engancharla |

### 7.3 Referencia (no tocar)

| RVA | Qué es |
|---|---|
| `0x2354FA0` | ctor del almacén `S` (0x198 B) |
| `.xtls 0xCBD2920` | ctor del gestor de guardado (`+0x50 = S`) |
| `0x59840C0` / `0x5983FE0` / `0x5984050` / `0x5984168` | vtables `GamePlaySaveDataFileHandler_Steam` / `…FileHandler` / `IGamePlaySaveDataHandler` / `GamePlaySaveData` |
| `.xtls 0xEF62160` | handler Init: `saveRoot` (`"%#/%#/save/%#"` `0x5714110`) |
| `.xtls 0xEF601B0` | Commit Load |
| `0x23571D0` | Commit Save (`_new`/`_backup`/`_delete`) |
| `0x2358750` | escribir un fichero (Get/Open/writeData) |
| `0x2355700` → `.xtls 0xEF51850` | commit de una ranura (load/valid/save/delete) |
| `0x235B620` → `.xtls 0xEF695C0` | registrar fichero pendiente (`ranura+0x6A0 + 0x10·tipo`) |
| `0x235BF10` → `.xtls 0xEF6DFC0` | sacar el texto plano de `fileObj+0x120` |
| `0x235AFE0` → `.xtls 0xEF69090` | poner datos a guardar en un fichero |
| `0x235C510` | descifrar + LZ4 (`fileObj`, clave `S+0x78`, &búfer) |
| `0x235C2E0` → `.xtls 0xEF713F0` | **[VM]** descifrado y verificación |
| `0x235C7A0` | LZ4 + cifrado + cabecera (sale `0x80 + carga útil`) |
| `0x235C090` → `.xtls 0xEF6EA10` | **[VM]** cifrado |
| `0x12C61D0` / `0x12C6150` | LZ4 descomprimir / comprimir |
| `exe+0x6A85460` | `FileSystem` global (`0x1326620` abrir; `CreateFile2`/`ReadFile`) |
| `exe+0x5547B6C` | magic `"SAVE"` (la estructura de alrededor tiene constantes de xxHash32) |
| `0x186E1A0` → `.xtls 0xCBD84D0` | guardar el pj (autoguardado); **no es VM** (corrige E3) |
| `0x1871850` → `.xtls 0xCBDE990` | ranura del autoguardado: rotación 0-2 global del gestor |
| `0x1870DF0` | posprocesado tras guardar (si no `opciones & 4`) |
| `0x23564F0` | CrossSave: instalar bytes cifrados en la ranura 3 (modelo del plan B) |
| `0x15AC100` / `0x15AE0A0` | `UploadCrossSaveFileAsync` / `setCrossSaveData` (HTTP a Pearl Abyss) |
| `0x186D9A0` | enumerar `*.save` de las ranuras |
| `0x186F920` | carga de `"save"` en `UserLogin`/`NewGame` |
| `0x1871040` | borrar ranura (`0xCE`) |

## 8. Scripts y volcados (`C:\Emulador CD\build\research\optd\p9\`)

| Fichero | Qué es |
|---|---|
| `rd.py` | bytes, ASCII y UTF-16 en una RVA |
| `rdsearch.py` | cadenas en `.xdata`/`.rdata` con sus referencias |
| `immscan.py` | inmediato de 32 bits como operando real |
| `vts.py` | vtables RTTI por subcadena, con sus vf y si son saltos a `.xtls` |
| `rangegrep.py`, `seqscan.py`, `strmap.py`, `modcallers.py`, `precall.py` | búsquedas por rango de funciones |
| `iat.py` | nombre de cada entrada IAT |
| `d_*.asm`, `h_*.asm`, `x_*.asm` | volcados: `186e450`, `2069640`, `23564f0`, `2354fa0`, `23559f0`, `2355bb0`, `235c7a0`, `h_2357da0`, `h_235c510`, `h_235c2e0`, `h_ef601b0`, `h_ef62160`, `h_23571d0`, `x_cbd84d0`, `x_cbd2920`, `x_ef713f0`, `x_ef537a0`… |

Fuente externa [R]: `CrimsonSaveEditor/ben_save_decrypt.py` del repositorio
[NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS)
(ChaCha20 + HMAC-SHA256 + LZ4, con la clave según la versión de la cabecera).
