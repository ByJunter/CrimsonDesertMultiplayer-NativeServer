# Progreso propio del invitado: cargar su partida y devolvérsela guardada

Noche del 11 al 12 de octubre de 2026, rama `opcion-d` (`43fc817`). Build del juego **1.0.0.2976** (PE `0x6AB28F00`,
base `0x140000000`; comprobado hoy en el exe del disco). Direcciones en RVA. **Solo investigación:** no se lanzó el
juego, no se tocó código del repo, no se paró el servidor de Ghidra. Herramientas: servidor de consultas de Ghidra
(`cdmp-query.ps1`), `C:\Emulador CD\build\research\{cdpe,xref,callers}.py`, capstone, scripts propios en el
scratchpad (`vf38scan.py`, `dis.py`, `s180.py`, `bytes.py`).

Marcas: **[V]** visto (log, traza, disco); **[E]** leído en `.rsrc` (decompilado/desensamblado); **[E-x]** leído en
`.xtls` (solo lectura); **[S]** inferencia o diseño; **[H]** a medir en vivo; **[R]** fuente externa.

Base previa que este informe amplía y corrige: `docs/research/opcion-d-p9.md` (P9), `pendientes/p8-sistemas-juego.md`
§5, `pendientes/p8-guardado-desviado.md` (caja), `pendientes/identidad.md` (C2), diario (A4, B2, B3, D-20, D-28).

---

## 0. Respuestas cortas

1. **Carga.** `3093 → 0x2C3E030` recarga el *lobby* del usuario 2 con `0x186F570(gestor, &res, N, usuario)` y luego,
   tras crear el personaje, `.xtls 0x1042AAF0` llama `0x186F570(gestor, &res, N, pj)`. Las dos acaban en
   `0x20688A0`, que busca `"slotN"` en el almacén `S = [gestor+0x50]` y hace leer del disco
   `saveRoot\slotN\{lobby,save}.save` (`saveRoot = %LOCALAPPDATA%\Pearl Abyss\CD\save\<accountID>`, la del
   **anfitrión**). N es el número que eligió el **cliente del invitado** en su título, pero los ficheros son los del
   anfitrión [E, V: Shadow manda siempre "save slot 1"]. Descifrado: `0x235C510` → VM `0x235C2E0` + LZ4.
   **La clave no depende de la cuenta ni del PC [V]:** una partida de otra cuenta, bajada de Nexus, carga como
   ranura 108 en el anfitrión, en un jugador (A4) y como usuario 2 (B3). La "clave" que se pasa a la VM es una
   cadena de `S` vacía al construirse (§1.5). **Una partida hecha en Shadow cargará en el PC del anfitrión.**
2. **Guardado.** Solo serializa `0x186E450(gestor, &res, ranura, pj, &opciones)`, llamado por el autoguardado
   (temporizador 2077, que piden el repetidor 2011 y 21 sitios del juego vía `0x28FC5F0`) y por el guardado manual
   (2910). La ranura la elige **el gestor, no el usuario** (rotación global 0-2). El cierre de sesión del personaje
   **no serializa nada**: solo confirma lo pendiente (`0x1871930`) [E]. Cifrado al escribir: `0x235C7A0` (LZ4 + VM)
   con la misma cadena; escritura `slotN_new → slotN` con copia `_backup` [V traza].
3. **Diseño recomendado** (§3): ranura reservada **207** solo en la **caja** del anfitrión. El invitado sube
   `lobby.save`+`save.save` de su ranura N por el túnel antes del 3093; el mod del anfitrión los deja en
   `cdmp_save_sandbox\<cuenta anfitrión>\slot207\`, **crea la entrada `slot207` en el almacén** (hace falta: §1.4),
   remapea N→207 en la entrada de `0x186F570` y de `0x186E450` para el usuario 2, comprueba tras la carga que se
   leyeron sus bytes, y tras cada guardado correcto del usuario 2 copia los dos ficheros a un buzón y se los manda al
   invitado, que los guarda fuera de su carpeta de partidas y los aplica a su ranura N con copia de seguridad.
4. **Un PC** basta para casi todo con `cdmp_peer` (replay + una opción nueva de subida): carga de una partida de otra
   cuenta como usuario 2 en la 207, guardado del usuario 2 en la caja, ida y vuelta, y que el fichero devuelto carga
   como partida normal. **Shadow** hace falta para el título real del invitado, su propia cuenta y carpeta, la
   aplicación en su disco con el juego de verdad, Steam Cloud del invitado y el ancho de banda real (§4).

---

## 1. Cómo carga el servidor el personaje de un usuario

### 1.1 Del 3093 al disco [E, E-x]

```
LoginCharacterToFieldServerReq 3093 (u8 | u64 characterNo @1 | u32 ranura @9)
  → 0x2C3E030 LoginCharacterToFieldServer_Process
       si g_SinglePlayerMode (siempre en este juego):
         ClearList del lobby + ClearCharacterLobbyDataForSinglePlayAck
         0x186F570(gestor, &res, N, usuario)        @0x2C3E1CE  → "lobby"
       SAM_CreateCharacter → SetupCharacter → 0x2940EE0 → .xtls 0x1042AAF0
         valida characterNo en el lobby (entradas 0xD0) → 0x186F570(gestor, &res, N, pj)  @0x1042ABF3 → "save"
  0x186F570 LoadSlot: acepta 0-2, 3, 100-108, 200-207; toma el byte ocupado [gestor+0x60];
            nombre = "slot%u"; 0x20688A0(actor, &res, S, "slotN", N); suelta +0x60.
            Solo si N == 206 (0xCE) borra la ranura tras cargar (0x1871040). No apunta "ranura actual" en el gestor.
  0x20688A0 LoadComponentsFromFile:
            S["slotN"] no existe → error DAT_146CF781C (no la crea)
            fichero "lobby" (si [[actor+0x88]+1]==0) o "save" en el mapa de la ranura (+8)
            si [ranura+0x69C]==0 → RegisterPendingFile(ranura, fichero, 0) → CommitSlot 0x2355700 → .xtls 0xEF51850
               → manejador Steam vf20 CommitLoad (.xtls 0xEF601B0): 0x235A800 (restaurar _new/_backup) +
                 0x2357DA0 (CreateFile2 + ReadFile, cabecera 0x80, 0x235C510 descifra, LZ4)
            → MemoryArchive → componente->vf50 por componente
```

Lo que se ve en disco [V, traza `traza-guardado-20261011-223220.log` #1-#28]: cada carga es `FindFirstFileW
save\<id>\*`, `GetFileAttributesW slotN_new` y `slotN_backup`, y `CreateFile2 READ slotN\<f>.save`, en el hilo del
servidor (tid 42836).

`UserLogin` (2490) también carga [E, `0x2C17520`]: con `+0x60` tomado llama al **refresco de ranuras**
`.xtls 0xCBD92D0` y después `0x186F570(…, ranura más reciente de 0-2/100-108 de la tabla del gestor, usuario)`.
**Si esa carga falla, `UserLogin` devuelve error** (el login del usuario 2 se cae). Las cinco lecturas de
22:32:44 de la traza son eso: el refresco lee lobby+save de `slot1` y `slot108` y luego el lobby de la más reciente.

### 1.2 Qué número usa hoy el usuario 2 [V]

- Shadow manda siempre `out LoginCharacterToFieldServerReq 3093: characterNo 0x7FFF…FFFF, save slot 1`
  (`p8-sesion1x-shadow`); su tabla local tiene 1 ranura (`title slot refresh: 1 done, last 1 slots`). El título del
  invitado sale de **su** disco (`p8-titulo.md`, `slot_refresh`), pero el 3093 va al servidor del anfitrión, que lee
  **su** `slot1`.
- En este PC `save\<id-cuenta>\slot1` y `slot108` son hoy la misma plantilla de capítulo 2 (736 731 B, 08-10 22:36):
  el anfitrión y el "clon" juegan la plantilla.

### 1.3 Cómo se llena el almacén `S` (nuevo) [E, E-x, V-inferencia]

- El mapa nombre→ranura (`S+0x18`) y el vector (`S+0x38`, n `S+0x40`) los construye **solo** `S::Init`:
  `.rsrc 0x23554C0` (salto) → `.xtls 0xEF510A0`: Init del manejador (vf1), **vacía el mapa** y llama a
  `manejador->vf7` = **`0x2358EF0`** (vtable `GamePlaySaveDataFileHandler_Steam 0x59840C0`, +0x38) y `vf8 0x2359FF0`.
  - `0x2358EF0` lista `saveRoot`, borra las carpetas `*_delete` (`0x2358D40`), trata `*_backup`, y **por cada carpeta
    que no esté en el mapa** crea la ranura (`0x2355BB0`), abre cada `*.save`, lee 0x80 B, valida la cabecera como el
    cargador y crea el objeto de fichero con **`0x235B100(ranura, nombre)`** copiando la cabecera a `+0x98`.
  - Único llamador de `vf7` en todo el exe (escaneo `FF 50 38` con `[rcx+0x10]`): `0xEF511C0`, dentro de `S::Init`.
- `S::Init` lo llama `0xA032470` (vía `0xAD3480`) desde la **inicialización del framework** `0xACD9E0` (llamada desde
  `0x616D40`, al arrancar el proceso) y desde una retrollamada de cambio de usuario de plataforma (`0xAD4140`, la
  registra `0xA03FAB0`). El `S` es `[[framework+0x108]+0x50]`, el mismo del gestor de guardado.
- El mod instala el guardián en el "late init" (WorldSystem + ventana visible + 5 s). En la traza no aparece
  ningún listado `slotN\*` ni lectura de cabeceras al estilo de `vf7` tras la instalación (22:32:31): **el mapa se
  construyó antes, contra la carpeta real** [V-inferencia]. Por eso **una carpeta `slot207` creada después en la caja
  no entra sola en el almacén.**
- Los guardados sí crean entradas nuevas: `0x2069640` hace `0x2355EC0(S, "slotN", 0)` (obtener o crear) y
  `SetSaveData` (`0x235AFE0` → `.xtls 0xEF69090`) crea el objeto de fichero con `0x235B100` si falta [E-x]. [V]: el
  autoguardado de las 23:16 creó `slot2` nuevo en la caja (rotación: la primera libre de 0-2 tras caducar la caché
  de 600 s de `slot0`).
- El refresco `0x1870C20` → `.xtls 0xCBD92D0` **no relee el directorio**: recorre el vector de `S`, admite 0-2,
  100-108, 200-207 y 3, **salta 3 y 201 (0xC9)**, y por cada una `0x1871D60` lee su lobby y su save y añade una
  entrada (0x68 B) a la tabla del gestor `+0x40` [E-x].

### 1.4 Consecuencia: la ranura del invitado hay que crearla, y hay una trampa [E]

- `0x20688A0` con una ranura **ausente** del mapa: error limpio (`DAT_146CF781C`).
- Con la ranura **presente pero sin objetos de fichero** "lobby"/"save": no registra nada pendiente, `CommitSlot` no
  hace nada (`+0x698 == 0`), el segundo bucle no encuentra fichero para ningún componente, **no llama a ningún
  `vf50` y devuelve 0 (éxito)**. El personaje quedaría **vacío o a medio inicializar**, y su primer guardado
  escribiría eso. Si ese fichero volviera al invitado, **borraría su progreso**. Hay que impedirlo por construcción y
  comprobarlo después de cada carga (§3.3).
- Cómo dejar una ranura cargable (lo mismo que hace `vf7` por carpeta, sin la copia de cabecera, que `0x2357DA0`
  rehace al leer):
  1. `0x2355EC0(S, "slot207", 0)` (salto a `.xtls 0xEF537A0`; bandera 0 = ranura de disco: `+0x69C = 0`);
  2. `0x235B100(ranura, "lobby")` y `0x235B100(ranura, "save")` (`.rsrc` normal, prólogo `40 53 55 56 57 41 54…`;
     reserva 0x140 B, cabecera por defecto "SAVE" v2, lo mete en el mapa `+8` y el vector `+0x688`). Sin cerrojo
     propio: hay que llamarlo **dentro de la sección ocupada del gestor y en el hilo del servidor** (§3.3).
  - Alternativa: llamar `manejador->vf7()` (`0x2358EF0`) tras dejar `slot207` en la caja. Es el código del arranque,
    pero también borra `*_delete` y trata `*_backup`: más efectos. Preferible la de arriba.
  - Alternativa sin llamadas: instalar el desvío **antes** de `S::Init` para que `vf7` vea la caja con un `slot207`
    de relleno. Depende de ganar una carrera con el arranque del juego: descartada.

### 1.5 La clave de cifrado [E, V, R]

- `S+0x78` no es una clave aparte: es el **búfer en línea (0x105 B) de una cadena** cuyo puntero de montón está en
  `S+0x180` (capacidad `+0x188 = 0x105`, longitud `+0x18C`). El ctor `0x2354FA0` la deja vacía. Por eso
  `0x186E450`/`0x23564F0` pasan "`[S+0x180]` si no es nulo, si no `S+0x78`": es el `c_str()` de la misma cadena.
  Corrige la lectura de P9 §1.5 (dos campos). No hay escritores de `[reg+0x180]` en el código del almacén, del
  gestor ni de CrossSave (escaneo `s180.py`); queda [H] leerla en vivo (G3a, barato).
- **[V]** A4 (diario 08-10 22:37): una partida de otra cuenta, bajada de internet (Nexus, en
  `C:\Emulador CD\backups\plantillas\chapter2-normal-nexus2863\slot108`), carga como ranura 108 en un jugador. B3
  (09-10 03:08): la misma carga como **usuario 2** (`0xA01002D5`) por el servidor del anfitrión. Ni cuenta de Steam ni
  PC entran en la clave; el accountID solo forma la ruta (`"%#/%#/save/%#"`, `0x5714110`).
- [R] `ben_save_decrypt.py` (ChaCha20 + HMAC-SHA256, clave por versión de cabecera) es coherente con esto.
- El número de ranura no impide cargar (la plantilla venía con otra ranura y cargó como 108) [V]. La entrada de la
  tabla usa el número de la **carpeta** (`0x1871D60`: `local_198 = param_2`) [E].

---

## 2. Cómo y cuándo guarda el servidor a un usuario

### 2.1 Quién pide guardar [E, E-x]

| Origen | Camino | Para el usuario 2 hoy |
|---|---|---|
| Repetidor 2011 (`GamePlaySaveDataRepeatTimer`, `.xtls 0x10859ED0`) | principal de cada usuario, cargado y con nivel que permite guardar → 2077 | sí, sin mirar quién es (`p8-sistemas-juego` §5.2) |
| 21 sitios del juego → `0x28FC5F0` (salto a `.xtls 0x103BC7A0`) | si el actor es personaje: encola el temporizador 2077 (`0x081D`, u8 motivo, u32 opciones) | sí (misión, objeto, viaje motivo 5 si es dueño del campo…) |
| `SaveAutoReq` 3037 (`0x2A21E60`) | payload u32; si el actor de la sesión es el principal del usuario → `0x28FC5F0(pj, &res, 7, &opc)` | **bloqueada** desde el túnel (filtro "Save*"); el cliente del invitado nunca la mandó en las sesiones 11-23 (`blocked 0`, `save Req seen 0`) [V] |
| `SaveToFileCurrentPlayerReq` 2910 → `.xtls 0x103BE8E0` | ranura que manda el cliente (100-108) → `0x186E450` | bloqueada desde el túnel |
| 2077 → `0x2C35EB0` (barrera TLS `+0x4460` en todos los hilos del servidor) → `0x28FC6F0` → `0x186E1A0` → `.xtls 0xCBD84D0` | ranura = `opciones&4 ? [gestor+0x5C] (o 0x1871850 si -1, y se apunta en +0x5C) : 0x1871850`; caché `+0x70/+0x78` de 600 s (`0x258`) | ranura del **anfitrión** |

`0x1871850` (`GetAutosaveSlot`) solo mira entradas con ranura `< 3`: devuelve la de los últimos 10 s, si no el primer
hueco de 0-2, si no la más antigua [E]. Una entrada 207 en la tabla no le afecta.

### 2.2 El guardado propiamente dicho [E, V]

```
0x186E450(gestor, &res, r8d=ranura, r9=pj, [rsp+0x28]=&opciones)
  "slotN"; si S["slotN"] existe: pone a 0 +0x6A8/+0x6B8/+0x6C8/+0x698; si no y N==3: GetOrCreate(S,"slot3",1)
  0x2069640(usuario=[pj+0xA0], &res, S, "slotN")  → "lobby"
  0x2069640(pj, &res, S, "slotN")                   → "save"
       cada una: GetOrCreate(S,"slotN",0) + vfD0 de cada componente + SetSaveData + CommitSlot (E/S inmediata)
  si N==3: cifra en memoria y UploadCrossSaveFileAsync 0x15AC100 (HTTP a Pearl Abyss)  ← nunca para el usuario 2
  si no y !(opciones&4): PostSave 0x1870DF0 (→ .xtls 0xCBDBA30): entrada de la tabla con el resumen del lobby del usuario
```

Disco [V, traza #29-#72]: son **dos commits seguidos** (lobby y save), 55 ms en total, en el hilo del servidor:
`CreateDirectoryW slotN_backup` o `MoveFileExW slotN → slotN_backup`, `CreateDirectoryW slotN_new`, `CopyFileW` de los
otros ficheros desde el backup, `CreateFileW GENERIC_WRITE CREATE_ALWAYS slotN_new\<f>.save`, `MoveFileExW slotN_new →
slotN`, `MoveFileExW slotN_backup → slotN_backup_<marca>_delete` y borrado. Cifrado: `0x235C7A0` (LZ4 + VM
`0x235C090`) con la cadena de §1.5. Antes de escribir, `vf48 Valid` mira la cuota de Steam Cloud **del anfitrión** y
< 10 000 ficheros (P9) [E]: con la caja no se sube nada, pero el chequeo existe.

### 2.3 Al salir el usuario 2 [E, V]

- Fase de cierre del personaje `0x28F26C0`: si es personaje y `[pj+0x96] != 0`, llama
  **`0x1871930 FlushPendingSaveData(gestor, &res, pj, 0, &0xC, &ranura)`**. Con `opciones & 8` no compara el contador;
  usa `[gestor+0x5C]` (la ranura pendiente que dejó un autoguardado diferido de **quien sea**), la pone a -1, hace
  `CommitSlot(S["slotX"])` y `PostSave(gestor, usuario del pj, X)`. **No serializa al personaje.**
- [V] Sesiones 8 y 9 (anfitrión, guardián en bloqueo): 12-17 ms después de encolar la 2001 del usuario 2,
  `refused create directory …\slot0_backup` en el hilo de guardado (T21248 / T8664), con
  `"[SaveData] Backup 생성 실패"` y un informe de Sentry retenido por el guardián de informes
  (`'[INFO] Savedata Save Fail Event', 1 file`). Era el commit pendiente de `slot0` que el bloqueo había rechazado
  minutos antes (10:44:36 en la sesión 8), no un guardado nuevo del personaje.
- Consecuencia: **lo que el invitado hizo desde su último autoguardado se pierde al salir**, igual que en un jugador
  si nadie pide guardar. El diseño pide un guardado explícito antes de la 2001 (§3.6).

### 2.4 Hoy, con `save_guard=redirect`

- El autoguardado o el commit de salida del usuario 2 va a la **caja** en una ranura del **anfitrión** (0-2): pisa
  el autoguardado del anfitrión **dentro de la caja** y deja en la tabla del gestor el resumen del usuario 2 para esa
  ranura. La carpeta real no se toca. Ese guardado del usuario 2 es una copia del personaje del anfitrión más lo que
  hizo el invitado: **no sirve** para devolverlo (no es su partida).
- Sin probar con usuario 2 presente: qué aparece en la caja tras su salida [H] (prueba L1, §4).

---

## 3. Diseño extremo a extremo

### 3.0 Principios

- **Solo con la caja activa y sana.** El modo "partida propia" exige `save_guard=redirect` instalado sin
  `falling back to block` y sin `FAILED`. En cualquier otro caso: modo actual (clon, progreso descartado), y se le dice
  al invitado.
- **Todo lo del almacén, en el hilo del servidor y dentro de la sección ocupada del gestor** (`+0x60`).
- **Falla cerrado:** un guardado del usuario 2 que no vaya a su ranura propia no se ejecuta.
- **Los bytes cifrados viajan tal cual**: el juego cifra, descifra y valida con su propio código; el mod solo valida la
  cabecera como lo hace el juego y calcula un hash.
- Nombres: `Ng` = ranura propia del invitado en el anfitrión; `N` = ranura del invitado en su PC.

### 3.1 Ranura `Ng`

- **207** para el primer invitado. Para el aforo de 4 (D-33): 200, 202, 203, 204, 205, 207 (6 libres).
- Prohibidas: 0-2 (rotación), 100-108 (manuales: entran en "más reciente" de `UserLogin`, en el predicado del título
  y en los adjuntos de Sentry `0x186D9A0`), 3 (CrossSave), 201 (el refresco la ignora), 206 (se borra tras cargar).
- 200-207 no las escribe nadie por su cuenta: `0x186E450` solo tiene dos llamadores (autoguardado y 2910) [E].
- Al arrancar la opción: si la caja ya tiene `slot207` (vendría de la carpeta real), el modo se apaga.
- **Sentry**: `0x186D9A0` (mal llamado *EnumerateSaveFiles*) adjunta los `*.save` de la ranura más reciente de
  0-2/100-108 a los informes; la 207 queda fuera [E]. El guardián de informes sigue siendo obligatorio (D-28).

### 3.2 Subida (invitado → anfitrión)

1. Al conectar el túnel (antes de la 2490, como exige P8), el mod del invitado elige la ranura que su "Continuar"
   cargaría (la de `save.save` más reciente de 0-2/100-108 en `save\<su accountID>\`), la valida y la ofrece.
2. Su gancho de `Send` (`guest.cpp`, ya ve el 3093 y su ranura en `+9`): si el 3093 pide otra N, **retiene el 3093**,
   sube esa N y lo suelta con el `SAVE_ACK` del anfitrión (tiempo máximo 30 s). Es la única Req que se retiene, en
   el título, con poco tráfico.
3. Se niega a ofrecer si hay `slotN_new` o `slotN_backup` (guardado interrumpido: que lo repare el juego en solitario),
   si N es 3 o 206, o si la cabecera no pasa.
4. Lectura con las API normales (el invitado no tiene guardián). Mientras juega en remoto su servidor local no ejecuta
   `UserLogin` ni guarda (`p8-titulo.md`), así que su carpeta no cambia durante la sesión [E; H: listar antes y después].

### 3.3 Anfitrión: servir la partida y cargarla

1. **Recepción** (hilo de red del mod): reensambla, valida, escribe en
   `…\CD\cdmp_save_sandbox\<cuenta anfitrión>\slot207.incoming\` (fuera de la vista del juego) y contesta
   `SAVE_ACK`. La ruta de la caja no está bajo `save`: los ganchos la dejan pasar sin desviar.
2. **Remapeo** en la **entrada de `0x186F570`** (`48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44 89 44 24 18`,
   prólogo normal, hook en línea permitido): si `r9` es el `ServerUserActor` del usuario 2 o `[r9+0xA0]` lo es, y
   hay partida del invitado lista → `r8d = 207`.
   - Llamador `UserLogin` (`0x2C17881`, se distingue por la dirección de vuelta `0x2C17886`): remapear solo si ya
     hay partida; si no, dejar pasar (hoy carga el lobby del anfitrión y el 3093 lo vuelve a cargar). Así el login
     del usuario 2 nunca falla por esto.
   - Llamadores del 3093 (`0x2C3E1D3`, `0x1042ABF8`): si no hay partida lista → devolver error sin llamar al
     original (`*rdx = [exe+0x6CF7CA4]`, el de "ocupado"), nunca cargar la del anfitrión en modo partida propia.
3. **Crear la ranura y colocar los ficheros** en el gancho de **entrada de `0x20688A0`** que ya comparten
   `identity_probe`/`identity_fix` (único llamador: `0x186F570`, con `+0x60` tomado, hilo del servidor), solo si el
   nombre es `"slot207"` y el actor es del usuario 2:
   - si hay `slot207.incoming` completo: renombrar `slot207` → `slot207.old` (de la caja) y `.incoming` → `slot207`;
   - si `S["slot207"]` no existe: `0x2355EC0(S, "slot207", 0)`; si le faltan "lobby"/"save": `0x235B100` por cada uno;
   - todo bajo SEH; si algo falla: `*res = InvalidSaveSlot`, no se llama al original y el modo se apaga en esta
     ejecución.
4. **Comprobación tras la carga** (salida del mismo gancho): `res == 0` y los objetos "lobby" y "save" de
   `S["slot207"]` tienen en `+0x98` la cabecera (magic, `+0x12`, `+0x16`, nonce `+0x1A`) de los ficheros subidos; y la
   cuenta de lecturas del guardián creció con `…\slot207\save.save`. Si no: **partida envenenada** → todo guardado
   del usuario 2 queda bloqueado hasta su salida (§3.4) y se avisa al invitado.
5. El arreglo C2 sigue igual: `characterNo` se sintetiza al cargar (`0x2B7F66C` y `.xtls 0x1042756B`), no viene del
   fichero (`identidad.md`), así que F002 se aplica sea cual sea la partida.

### 3.4 Guardado del usuario 2

1. Gancho en la **entrada de `0x186E450`** (`4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4C 24 08`): si
   `[r9+0xA0]` es el usuario 2:
   - modo partida propia, sana → `r8d = 207` (cubre autoguardado, 2910 y 3037; anula la rama de CrossSave, que
     decide con `r8d`);
   - modo clon o partida envenenada → **no llamar al original**, `*res =` error de guardado (el invitado ve el aviso
     nativo por la 3016). Es el mismo resultado que daba el bloqueo, sin escribir nada.
2. A la vuelta, si `res == 0`: en el mismo hilo (son ~1,2 MB; el guardado ya tarda ~55 ms) copiar
   `slot207\{lobby,save}.save` de la caja al **buzón** `%LOCALAPPDATA%\cdmp\guest_saves\<SteamID64 invitado>\gen<n>\`,
   validar las cabeceras y apuntar el hash. Solo cuando las dos llamadas a `0x2069640` acabaron: un fallo entre
   lobby y save deja la pareja desigual y no se exporta.
3. El buzón sobrevive a la caja (que se rota en cada arranque): guarda las últimas 5 generaciones.
4. Efectos que quedan y son cosméticos [E]: `CBD84D0` diferido apunta en `+0x5C` una ranura del anfitrión y el
   `FlushPendingSaveData` de la salida pone en la tabla de esa ranura el resumen del usuario 2. Los datos escritos son
   los del anfitrión. La tabla tendrá una entrada 207 (el predicado del título solo cuenta 0-2/100-108; si el menú
   "Cargar" del anfitrión la enseña es [H]).

### 3.5 Bajada (anfitrión → invitado) y aplicación

1. Tras cada exportación, `SAVE_RESULT` (generación, motivo, N, tamaños, hashes) y los trozos. Si hay una más nueva
   en cola, la vieja se descarta. Si el túnel cae, se reenvía la última no confirmada en la siguiente conexión.
2. El invitado guarda en `%LOCALAPPDATA%\cdmp\guest_progress\<SteamID64 anfitrión>\incoming\gen<n>\` (+ manifiesto)
   y confirma. Nunca dentro de `save\`.
3. **Aplicar** a `save\<su accountID>\slotN\`:
   - v1 (primeras pruebas): a mano con el juego cerrado, con un script (copia de seguridad, copiar, comparar hash);
   - v2 (automático): al recibir una generación completa, en modo remoto y sin carga local en curso:
     copia de seguridad de `slotN` a `%LOCALAPPDATA%\cdmp\guest_backups\<fecha>_slotN\` (últimas 10), escribir en
     `%LOCALAPPDATA%\cdmp\staging\slotN\` (mismo volumen), `MoveFileExW slotN → staging\slotN_old_<fecha>`,
     `MoveFileExW staging\slotN → slotN`, comprobar hashes; si falla, devolver `_old`.
   - **Nunca** crear nombres `*_new`, `*_backup`, `*_delete` dentro de `save\<id>\`: `0x235A800` y `vf7` los restauran
     o los borran.
4. Steam Auto-Cloud del **invitado** subirá esos ficheros al cerrar su juego: es su progreso en su nube, lo esperado.
5. Si un día el invitado usa el guardián en modo `redirect`, las escrituras del propio mod también se desviarían:
   aplicar desde fuera del proceso o por los trampolines.
6. **Generaciones y reconexión** (C4): la oferta del invitado lleva `base_gen` (la última que aplicó; 0 = partida
   local nativa). Si el anfitrión tiene en el buzón una generación más nueva **de la misma partida** (la misma línea,
   ver «Sesión 25: líneas» al final) que el invitado no aplicó (modo v1), la usa en vez de la ofrecida y se lo dice.
   Así una reconexión no retrocede el progreso. (Hasta la sesión 25 bastaba con ser del mismo SteamID: error grave.)

### 3.6 Guardado final al salir

- Salida pedida por el invitado (menú, "bye"): antes de su 2001, el mod del invitado manda un `SaveAutoReq` 3037
  (payload u32 0) y espera la `SaveToFileCurrentPlayerAck` 3016 y el `SAVE_RESULT` correspondiente (máx. 15 s); luego
  la 2001.
- Pérdida del enlace (`trigger=link_lost`): el mod del anfitrión inyecta ese mismo 3037 en la sesión 2 por la cola de
  `inject()` antes de su 2001 nativa (la secuencia de salida C4 ya ordena lo que entra) y deja el resultado en el
  buzón para la próxima conexión.

### 3.7 Política de Req del túnel (`server.cpp`, `load_channels`/`inject`)

| Req | Hoy | Modo partida propia |
|---|---|---|
| `SaveAutoReq` 3037, `SaveToFileCurrentPlayerReq` 2910 | bloqueadas ("Save*") | **permitidas** (remapeo en `0x186E450`) |
| `SaveDataSlotRefreshReq` 2579, `DeleteSave*`, `DeleteCharacter*`, trucos | bloqueadas | bloqueadas |
| `CreateCharacterReq` 2711, `NewGameStartReq` 2178 | **pasan** | **bloquear** (un personaje nuevo en el servidor del anfitrión se guardaría como "su" partida y pisaría su N) |
| 3093 con ranura 3 o 206 | rechazado | rechazado (también en la oferta) |

### 3.8 Protocolo

Dentro del túnel, como paquetes de control (`troc::kControlOpcode` = 1, ranura -1), nuevos estados. No cambia
`protocol.cpp`; los estados 3, 4 y 5 se añadieron igual sin subir `kVersion`. La compatibilidad la decide un bit de
capacidad: el invitado lo pone en su paquete `kCapsOpcode` (`kCapsGuestSave = 0x02`) y el anfitrión contesta con el
estado 10. Un extremo viejo ignora o descarta los estados nuevos. (Si se prefiere un `RelKind` propio con su flujo
para que 1,2 MB no retengan los paquetes del juego, entonces sí `kVersion = 9`; con la cadencia de abajo no hace falta.)

| Estado | Dir. | Formato (LE) |
|---|---|---|
| 6 `SAVE_OFFER` | G→A | u8 6, u8 ver, u32 xfer, u64 SteamID64 invitado, u32 N, u32 base_gen, u8 nfiles=2, 2×{u8 tipo (0 lobby, 1 save), u32 tamaño, 32 B SHA-256} |
| 7 `SAVE_CHUNK` | ambas | u8 7, u32 xfer, u8 tipo, u32 offset, u16 len, len B (≤ 28 672: el paquete Troc admite 0x8000-0x20) |
| 8 `SAVE_ACK` | ambas | u8 8, u32 xfer, u8 resultado (0 ok, 1 hash, 2 cabecera, 3 tamaño, 4 ocupado, 5 apagado, 6 ranura), u32 gen |
| 9 `SAVE_RESULT` | A→G | u8 9, u8 ver, u32 xfer, u32 gen, u32 N, u8 motivo (auto/manual/final/salida), u64 hora, 2×{tipo, tamaño, SHA-256} |
| 10 `SAVE_STATUS` | A→G | u8 10, u8 modo (0 apagado/clon, 1 listo, 2 activo, 3 envenenado), u32 Ng, u32 último error del juego |

- **Tamaños** [V]: `save.save` 0,74-1,18 MB (aquí 736 731 y 748 806 B; P9 vio hasta 1 182 846), `lobby.save`
  441-518 B. Tope de aceptación: 16 MB por fichero. ~27-43 trozos por transferencia. Cifrado: no se comprime
  (entropía 8,0); la compresión del túnel solo gasta CPU en esos lotes.
- **Cadencia**: la subida, en el título, sin límite (el túnel admite 16 MB de cola). La bajada, durante el juego, con
  un trozo nuevo solo si la cola del `TrocLink` está por debajo de 256 KB: 1,2 MB repartidos en ~1 s sin frenar los
  paquetes del juego (D-33 estima 8-12 Mbit/s de subida del anfitrión con 4 jugadores; 1,2 MB cada pocos minutos
  es poco al lado).
- **Integridad**: SHA-256 con `BCryptHash` (bcrypt.dll; el juego ya la importa) y la misma validación de cabecera
  que el juego (`"SAVE"`, versión 1..2, `+6 == 0x80`, `flags & ~2 == 0`, `+0x12 != 0`, `0 < +0x16 <= +0x12`,
  tamaño `== 0x80 + [+0x16]`).

### 3.9 Fallos

| Fallo | Se detecta | Respuesta |
|---|---|---|
| Caja no activa, o anfitrión/invitado sin la capacidad | estado 10 modo 0 / sin caps | modo clon; el invitado lo registra y avisa: "esta sesión no guarda tu progreso" |
| Ranura del invitado con `_new`/`_backup`, N = 3/206, cabecera mala | invitado | no se ofrece; según `guest_save_fallback`: clon o no entrar |
| Subida incompleta, hash o cabecera mal | `SAVE_ACK` ≠ 0 | un reintento; si no, el 3093 no sale (o sale en clon si se pidió) |
| Crear ranura o renombrar en la caja falla (SEH, E/S) | gancho `0x20688A0` | `res` = error, carga rechazada, modo apagado esta ejecución |
| Descifrado/HMAC del juego falla (`"[SaveData] 처리 실패"`) | `res ≠ 0` del 3093 | Nak nativo al invitado; ningún guardado |
| Carga "correcta" sin sus bytes (§1.4) | comprobación de `+0x98` | partida envenenada: guardados del usuario 2 bloqueados, aviso |
| Guardado del juego falla (cuota, E/S) | `res ≠ 0` en `0x186E450` | nada que exportar; aviso nativo 3016 |
| Túnel cae durante la bajada | sin `SAVE_ACK` | queda en el buzón; reenvío con su generación |
| El invitado no puede aplicar | invitado | se queda en `incoming`; se restaura la copia; aviso |
| El anfitrión se cuelga | — | vale la última generación confirmada (como un cuelgue en un jugador) |

### 3.10 Identidad y choques entre partidas de cuentas distintas

- **`characterNo`**: constante en las dos partidas (se sintetiza al cargar); C2 da F002 al usuario 2 [E, V].
- **Claves de actor** (`0xA01…`, hijos `0xB01…`): las reparte la SAM al entrar, no vienen del fichero [V: B3
  `0xA01002D5` con la plantilla].
- **`itemNo`** [S]: hoy el usuario 2 es un clon y tiene **todos** los `itemNo` del anfitrión repetidos, horas de juego
  en P8 sin fallos atribuidos a ello. Con su propia partida el solapamiento baja a coincidencias sueltas, pero no
  desaparece; y si el contador de objetos nuevos sale de la partida, los dos generarán números parecidos. Sigue
  abierto (R11, ID-2): no remapear nada a ciegas.
- **Globales que pisa la carga**: `_currentRestoreCodeVersionXXX` (`0x6C8CD98`, `identidad.md`); la hora del mundo
  (ya contenida por `clock_gate` [V línea de arranque]); eventos globales y precios (`GlobalGameEventSaveData`,
  `TradeItemPriceRate`, dentro de `FieldSaveData` de ContentsMisc, `vision-a3` §2.2) [S]. Con el clon eran los mismos
  valores; con otra partida pueden cambiar. Medir en L2.
- **Estado del mundo en la partida del invitado** [S, lo más delicado]: ContentsMisc guarda `FieldSaveData`
  (gimmicks, NPC, eventos). Con el campo compartido (E3b), si el productor lo toma del campo actual, el invitado se
  llevaría estado del mundo del anfitrión. No se cerró en esta noche (el productor `0x28B7360` tiene 0x419F bytes).
  Medirlo en L4 antes de aplicar automáticamente.
- **Posición**: se guarda en coordenadas del mismo mapa; si el invitado guarda dentro de una instancia o escena que
  en su mundo no existe, al cargar solo podría quedar mal colocado [S]. v1: exportar solo guardados hechos en el campo
  abierto (el mod ya sigue el campo del usuario 2).

### 3.11 Lo que no debe pasar

| Nunca | Por qué no pasa |
|---|---|
| Escribir una ranura del anfitrión con el usuario 2 | remapeo en `0x186E450` y, si no aplica, el guardado del usuario 2 no se ejecuta |
| Tocar la carpeta real del anfitrión o su Steam Cloud | la 207 y todo lo demás están en la caja, fuera de `save\` (Auto-Cloud no la ve) |
| Subir a CrossSave (ranura 3) | el remapeo cambia `r8d` antes de la rama de la ranura 3; 3 y 206 rechazadas en oferta y 3093 |
| Cargar o devolver una partida vacía | creación de los objetos de fichero antes de la carga + comprobación de `+0x98` + envenenado |
| Mezclar la partida de dos invitados | una `Ng` y un buzón por SteamID64 |
| Escribir la carpeta del invitado con el juego cargando esa ranura | solo en modo remoto y sin carga local; copia de seguridad fuera de `save\` |
| Mandar partidas a Pearl Abyss en un informe | `report_guard` activo; la 207 no entra en los adjuntos (`0x186D9A0`) |
| Que un invitado cree personaje en el servidor del anfitrión | 2711/2178 bloqueadas en este modo |

### 3.12 Dónde iría el código (nada tocado esta noche)

- `src/common/guest_save.{h,cpp}` (puro, con tests): validación de cabecera, codificación de los estados 6-10,
  reensamblado con topes, generaciones, decisión del remapeo y de la ranura, plan de aplicación sobre carpetas.
- `src/common/troc_frame.h`: estados 6-10 y `kCapsGuestSave`.
- Anfitrión `src/mod/session2/guest_save_host.cpp`: ganchos de `0x186F570` y `0x186E450`, fase en el gancho
  compartido de `0x20688A0`, buzón; `server.cpp`: tabla de §3.7; `session2.cpp`: control de estados.
- Invitado `guest_save_guest.cpp` (+ `guest.cpp`): oferta, retención del 3093, recepción, aplicación, 3037 final.
- `cdmp_peer`: `--guest-save <carpeta slotN>` (sube como el invitado y guarda lo que vuelve).
- `.ini`: `guest_own_save=0|1` (anfitrión), `guest_save_slot=207`, `guest_save_apply=off|incoming|auto`,
  `guest_save_fallback=clone|refuse` (invitado).

### 3.13 Secuencia

```
Invitado (Shadow)                     túnel                      Anfitrión (mod + servidor)
conecta, caps 0x02 ───────────────────────────────────────────▶  SAVE_STATUS modo 1, Ng=207
SAVE_OFFER N=1 + SAVE_CHUNK… ─────────────────────────────────▶  valida → caja\slot207.incoming ; SAVE_ACK
2490 ─────────────────────────────────────────────────────────▶  UserLogin: refresco + LoadSlot(→207 si listo)
(título) 3093 N=1 [retenido si N≠ofrecida] ───────────────────▶  0x186F570 r8d→207; 0x20688A0: renombra,
                                                                    crea S["slot207"]+ficheros, carga, comprueba +0x98
juega …                                                          2011/2077 → 0x186E450 r8d→207 → caja\slot207
                                                                    → buzón gen n
         ◀──────────────────────────────── SAVE_RESULT gen n + SAVE_CHUNK… (a ritmo)
guarda en incoming\gen n ; SAVE_ACK ─────────────────────────▶
(v2) aplica a save\<id>\slot1 con copia
sale: 3037 ───────────────────────────────────────────────────▶  guardado → gen n+1 ──▶ … ; luego 2001
```

---

## 4. Qué se comprueba en un PC y qué necesita Shadow

Todas las pruebas con juego: `save_guard=redirect`, `report_guard` activo, copia de `save` en `backups/` antes, y la
lista de la carpeta real (nombres, tamaños, fechas) antes y después (debe quedar idéntica).

| # | Dónde | Qué | Éxito |
|---|---|---|---|
| U0 | sin juego | tests puros de `guest_save` (cabecera con los ficheros reales de este PC, trozos, generaciones, plan de aplicación sobre carpetas en `%TEMP%`, decisiones de remapeo), `optd_hooks_redirect` ampliado con una `slot207` en la caja | `.\scripts\build.ps1` en verde |
| L1 | un PC, solo lectura | sonda de registro en las entradas de `0x186E450`, `0x186F570` y `0x1871930` (ranura, claves de pj/usuario, opciones, dirección de vuelta) con `cdmp_peer` (replay `login_e3lc00bye`, `SAVE_SLOT=108`), esperar un autoguardado, despedida | saber cuándo y con qué ranura guarda el servidor al usuario 2 hoy, y qué deja su salida en la caja (punto 6 de `p8-guardado-desviado.md`) |
| L1b | un PC | leer en vivo la cadena `S+0x78`/`[S+0x180]` (G3a) | vacía o igual en los dos PC |
| L2 | un PC | `cdmp_peer --guest-save <copia de otra partida>` (p. ej. la plantilla de Nexus si el anfitrión juega otra, o una partida propia de `backups\` con permiso) | redirección `READ …cdmp_save_sandbox\…\slot207\save.save`, comprobación `+0x98` correcta, datos de personaje a la sesión 2 distintos de los del anfitrión, nada en la carpeta real |
| L3 | un PC | el peer manda `SaveAutoReq` 3037 tras entrar | `MoveFileExW …slot207_new → slot207` en la caja, gen 1 en el buzón y en el `incoming` del peer, cabeceras válidas, `slot0-2` de la caja sin escrituras del usuario 2 |
| L4 | un PC | segunda vuelta: el peer sube la gen 1 devuelta; movimiento antes de guardar | carga; la posición y lo cambiado coinciden. Comparar el estado del mundo (§3.10) |
| L5 | un PC | opción de depuración "superponer en la caja" (`slot108` ← gen 1) y cargar en **un jugador** | el fichero escrito para el usuario 2 (cuenta 1) carga como partida normal |
| L6 | un PC | negativos: trozo corrupto, falta `save.save`, `slot207` sin objetos (forzar), N=3 | rechazo limpio, nunca carga vacía ni guardado |
| S1 | Shadow | título real: "Continuar" con su partida (otra cuenta), jugar, autoguardado, guardar en ESC (2910), salir | gen en `incoming` de Shadow; carpeta de Shadow sin cambios durante la sesión |
| S2 | Shadow | aplicar (v1 a mano con el juego cerrado; luego v2) y cargar **en solitario** | xp, objetos, misiones y posición del invitado presentes; nada del anfitrión en su inventario |
| S3 | Shadow | Steam Cloud del invitado tras S2; reconexión C4 a mitad (generaciones); enlace cortado (guardado de salida desde el anfitrión) | sin conflicto de nube; sin retroceso de progreso |
| S4 | Shadow | ancho de banda: subida en el título y bajadas durante combate | sin tirones medibles en el túnel (`p8-tunel-rendimiento`) |

Lo único que exige Shadow: el título y el cliente reales del invitado, **otra cuenta de Steam con su carpeta y su
nube**, la aplicación en su disco y el enlace real. La mecánica del servidor (carga, guardado, cifrado entre cuentas,
ida y vuelta) se puede cerrar en este PC.

---

## 5. Riesgos y preguntas abiertas

| Riesgo | Grav. | Estado |
|---|---|---|
| Llamar a `0x2355EC0`/`0x235B100` desde el mod (primera vez que se crea una ranura a mano) | media | [E] mismo uso que `vf7`; hilo y sección del gestor; SEH; L2/L6 |
| Estado del mundo del anfitrión dentro de la partida del invitado (campo compartido) | alta | [S] abierto; medir en L4 antes de aplicar en automático |
| Globales pisados por otra partida (precios, eventos, `restoreCodeVersion`) | media | [S]; L2 |
| `itemNo` repetidos | media | [S] R11; ID-2 |
| La entrada 207 visible en el menú "Cargar" del anfitrión | baja | [H] mirar tras L2 (está en la caja: inocuo) |
| El cliente del invitado ante un Nak de 3093 (¿vuelve al título?) | baja | [H] L6 con el peer, S1 |
| Cuota de Steam Cloud del anfitrión en `vf48 Valid` al guardar la 207 | baja | [E] solo comprobación |
| Partida guardada en instancia o escena | media | [S] v1 exporta solo en campo abierto |
| Guardián del invitado en `redirect` desviando la aplicación | baja | regla en §3.5 |

---

## 6. Direcciones (1.0.0.2976)

| RVA | Qué | Uso |
|---|---|---|
| `0x186F570` | `LoadSlot(gestor, &res, r8d ranura, r9 actor)` | **gancho**: remapeo |
| `0x186E450` | `SavePlayerToSlot(gestor, &res, r8d, r9 pj, &opc)` | **gancho**: remapeo + exportación |
| `0x20688A0` | `LoadComponentsFromFile(actor, &res, S, nombre, ranura)` (gancho compartido existente) | crear ranura, comprobar `+0x98` |
| `0x2355EC0` → `.xtls 0xEF537A0` | `GetOrCreateSlot(S, nombre, enMemoria)` | llamar (bandera 0) |
| `0x235B100` | `AddFile(ranura, nombre)` (objeto 0x140 B) | llamar ×2 |
| `0x23554C0` → `.xtls 0xEF510A0` | `S::Init` (vacía el mapa, `vf7`, `vf8`) | no tocar |
| `0x2358EF0` | manejador `vf7`: enumerar carpetas, crear ranuras y ficheros | referencia (alternativa) |
| `0x2358D40` | borra `*_delete` | referencia |
| `0xACD9E0`, `0xA032470`, `0xAD4140` | init del framework / llamada a `S::Init` / cambio de usuario | referencia |
| `0x1870C20` → `.xtls 0xCBD92D0`; `0x1871D60` | refresco de la tabla (salta 3 y 201) | referencia |
| `0x2C17520` (`+0x361` LoadSlot) | `UserLogin` | dirección de vuelta `0x2C17886` |
| `0x2C3E030` (`0x2C3E1CE`), `.xtls 0x1042AAF0` (`0x1042ABF3`) | 3093: lobby y personaje | direcciones de vuelta `0x2C3E1D3`, `0x1042ABF8` |
| `0x2069640` | `processSaveDataToFile` (GetOrCreate + SetSaveData + Commit) | referencia |
| `0x235AFE0` → `.xtls 0xEF69090` | `SetSaveData` (crea el fichero si falta) | referencia |
| `0x2355700` → `.xtls 0xEF51850` | `CommitSlot` (salta si `+0x69C` o nada pendiente) | referencia |
| `.xtls 0xCBD84D0` (vía `0x186E1A0`) | guardado del pj: ranura, `+0x5C`, caché 600 s | referencia |
| `0x1871850` → `.xtls 0xCBDE990` | `GetAutosaveSlot` (solo `< 3`) | referencia |
| `0x1871930` | `FlushPendingSaveData` (salida: commit sin serializar) | referencia |
| `0x28F26C0` | fase de salida del personaje | referencia |
| `0x28FC5F0` → `.xtls 0x103BC7A0` | pedir autoguardado (2077) | referencia |
| `0x2A21E60` | `SaveAutoReq` 3037 | permitir en modo propio |
| `0x186D9A0` | adjuntos de Sentry (no enumera ranuras) | referencia |
| `0x2354FA0` | ctor de `S`: cadena `+0x78`/`+0x180` | referencia |

Bytes de entrada comprobados hoy en el exe del disco (`bytes.py`): `0x186F570 48 89 5C 24 08 48 89 74 24 10…`,
`0x186E450 4C 89 4C 24 20 44 89 44 24 18…`, `0x20688A0 48 89 54 24 10 48 89 4C 24 08 55 53…`,
`0x235B100 40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 88 00 00 00`, `0x2355EC0 E9 DB D8 BF 0C`,
`0x28FC5F0 E9 AB 01 AC 0D` (salto a `.xtls 0x103BC7A0`), `0x23554C0 E9 DB BB BF 0C`.

Evidencia de logs: `C:\Emulador CD\backups\p8-sesion8-host\cdmp.log` (líneas 16601, 20538, 20547),
`p8-sesion9-host\cdmp_fase1.log` y `cdmp_fase2.log` (18619, 18626), `p8-sesion1x-shadow\*.log` (3093 ranura 1),
`traza-guardado-20261011-223220.log` (#1-#72), diario A4/B3, listado de hoy de `save` y `cdmp_save_sandbox`.

---

## Implementado (fase A: solo el anfitrión)

12 de octubre de 2026, rama de `opcion-d` (`214ecbc`). Build del juego **1.0.0.2976**. Sin juego: compilado y
probado con `.\scripts\build.ps1` (48/48 tests). No se lanzó el juego, no se desplegó nada.

### Corrección importante: la ranura del invitado es la **204**, no la 207 [E-x]

`GetOrCreateSlot` (`.xtls 0xEF537A0`, por el thunk `0x2355EC0`) pone **`ranura+0x699 = 1`** a toda `"slotN"` cuyo N
no sea un número, sea 201 (constante ofuscada: `[.xtls 0xEC0FE84] ^ 0x48D9F781 = 0xC9`) o esté en **205-207**
(`N - 0xCD < 3`), también cuando la ranura ya existía. Y `CommitSlot` (`.xtls 0xEF51850`) **no hace ninguna E/S**
mientras `+0x699` (o `+0x69C`) esté puesto. Es decir: una 207 creada (o solo tocada) por `GetOrCreateSlot` es una
ranura **solo de memoria**: su carga no lee el disco y devuelve 0 (la trampa del personaje vacío de §1.4) y su
guardado no escribe nada. Las ranuras de disco que `LoadSlot` acepta en 200-207 son **200, 202, 203 y 204**; el
invitado usa la 204 (el "aforo de 4" de §3.1 pasa a 200, 202, 203, 204). Todo lo demás del diseño sigue igual
cambiando 207 por 204. Además el mod comprueba en vivo, tras `GetOrCreateSlot`, que `+0x699` y `+0x69C` valen 0;
si no, rechaza la carga (nunca carga vacía).

### Qué hay

| Pieza | Dónde | Qué hace |
|---|---|---|
| Lógica pura | `src/mod/session2/guest_save_logic.h` | modos, reglas de ranura (204; 201/205-207 solo memoria), cabecera como el juego (`"SAVE"`, versión 1-2, `+6 == 0x80`, `flags & ~2 == 0`, `+0x12 != 0`, `0 < +0x16 <= +0x12`) más tamaño `== 0x80 + [+0x16]` y ≤ 16 MB, decisiones de `LoadSlot`/`SavePlayerToSlot` por llamador y dueño, plan de `AddFile`, generaciones del buzón, manifiesto |
| Tests | `tests/optd_guest_save_tests.cpp` (`optd_guest_save`) | lo anterior, con propiedades de "falla cerrado" sobre los 64 estados; la cabecera también con los ficheros reales de la plantilla de Nexus si están en el PC; `optd_hooks_redirect` ampliado con el contador de lecturas |
| Anfitrión | `src/mod/session2/guest_save_host.{h,cpp}` | ganchos, preparación, registro de la ranura, comprobación, exportación, sondas |
| Gancho compartido | `identity_probe.{h,cpp}` (`identity_load_hook_share`) | la entrada de `0x20688A0` (la misma de C2) llama antes y después a la fase de la partida propia; antes puede rechazar sin llamar al original |
| Caja | `save_hooks.{h,cpp}` | `save_hooks_sandbox_dir`, `save_hooks_redirect_failed`, y un contador de lecturas correctas (`CreateFile*` de lectura desviadas) de `slot204\lobby.save` y `slot204\save.save` |
| Ini | `[session2] guest_own_save = 0 \| probe \| 1` (por defecto 0); `guest_own_save_debug_dir = <carpeta>` | `cdmp.p8-noche-local.ini` y `cdmp.p8-s7-host.ini` llevan `probe`; los ini del invitado, nada |

Requisitos: `create_session=1` y `save_guard=redirect` **instalado y sano** (sin `falling back to block`, sin
`FAILED`); si no, una línea `Session2 guest save: guest_own_save=... is forced OFF: ...` y nada más. También se
apaga si la caja no tiene exactamente una carpeta de cuenta (todo dígitos), si `slot204` ya existe en la caja
(vendría de la carpeta real), si no se pueden borrar los `slot204.incoming`/`.old` de la ejecución anterior en la
carpeta de preparación, o, en modo 1, si falta cualquier comprobación de código (abajo).

**De quién es una carga o un guardado** (solo hechos positivos; revisión, puntos 1, 6 y 7): el usuario es el propio
actor si es un `ServerUserActor`, si no `[actor+0xA0]`. Es del **usuario 2** si es el usuario 2 validado
(`server_user2`), si su puntero de vuelta a la sesión (`+0x1F8`) es la sesión 2 de cualquier época, o si la clave del
personaje es la del personaje del usuario 2 de esta época. Es del **anfitrión** si es el usuario validado del
anfitrión, si su `+0x1F8` es la sesión del propio juego (`[[exe+0x6D69680]+0x18]`, la única de `ServerNet`), o si en
esta ejecución no se construyó ninguna sesión 2 (nadie más puede estar dentro). Si no, **sin identificar** = "quizá
el usuario 2". Antes, un fallo de `server_user2` hacía "no es el usuario 2" a cualquiera.

- [E] `[pj+0xA0]` ya está puesto antes de `LoadSlot` en la carga del personaje del 3093: `.xtls 0x1042AB12` toma el
  pj de `[rcx+8]`, `0x1042AB16` lee `[pj+0xA0]` y `0x1042AB1D` lo desreferencia **sin comprobar** (`[user+0x68]`, la
  tabla de componentes, para el lobby) antes de la llamada de `0x1042ABF3`, que recibe ese mismo pj
  (`r9 = [rbp+8]`, `0x1042ABDC`); si fuera nulo el juego fallaría ahí. [V] El arreglo C2 lo usa en la vuelta de
  `0x20688A0` (`backups\cdmp_20261011-214457.log`: `user 2 status of character 0xA01004B8 … (written)`). El guardado lo lee en
  `0x186E5DD`. Así que el 3093 del propio anfitrión es "anfitrión" (su usuario está en la sesión del juego) y no se
  rechaza nunca; las cargas y guardados del anfitrión solo se rechazan si piden la 204.

### Cómo funciona (modo 1)

1. **Preparación** (`guest_save_incoming_dir()` + `guest_save_incoming_ready(origen, why)`): los dos ficheros van a
   `…\Pearl Abyss\CD\cdmp_guest_save_staging\<cuenta anfitrión>\slot204.incoming\`, hermana de `save` y de la caja:
   ninguna ruta de partidas del juego la nombra y el juego **no la lista** (sí lista la carpeta de la cuenta en cada
   carga y guardado; por eso ya no se prepara dentro de la caja: revisión, punto 5). `ready` los valida como el juego
   (cabecera, tamaños), calcula SHA-256 (BCrypt) y marca la ranura `staged`. En la fase A los copia en la instalación
   desde `guest_own_save_debug_dir`; la fase B solo tiene que llenar la misma carpeta y llamar a `ready`.
2. **`LoadSlot 0x186F570`** (gancho de entrada). Llamador por dirección de vuelta: `0x2C17886` UserLogin, `0x2C3E1D3`
   3093 lobby, `0x1042ABF8` 3093 personaje (son los tres únicos).
   - usuario 2 y ranura lista (`staged` o `live`, sin `disabled` ni `poisoned`) → `r8d = 204`, sea cual sea el llamador
     (también la 3 de CrossSave: nunca llega);
   - usuario 2 sin ranura lista → **no se llama al original** en ningún camino, **también su UserLogin** (revisión,
     punto 2): `*res = [exe+0x6CF7CA4]` ("ocupado"); el login del usuario 2 falla de forma nativa y el invitado
     vuelve a su título. Nunca recibe la ranura del anfitrión;
   - el anfitrión: pasa siempre, salvo si pide la 204 (`[exe+0x6CF7900]`, ranura no válida);
   - sin identificar: rechazado, salvo en UserLogin (su actor es el usuario que se está conectando, vivo: su sesión se
     lee, y si no es ni la del juego ni una sesión 2 tampoco es la del usuario 2).
   - En cada entrada del usuario 2 se borran las comprobaciones anteriores: una carga de lobby borra las dos, una de
     personaje la del personaje; una carga rechazada o con resultado ≠ 0 borra las dos (revisión, punto 4).
3. **Fase en `0x20688A0`**: solo para la 204, para el usuario 2, y **solo dentro de una llamada a `LoadSlot` que este
   gancho remapeó para ese mismo actor** (marca `thread_local` alrededor de la llamada al original; `0x186F570` es el
   único llamador de `0x20688A0` y solo lo llama tras tomar el byte ocupado `+0x60`, `0x186F6BF..6C5`; revisión, punto
   3); además comprueba el hilo del servidor. En una carga de **lobby** (un login empieza siempre por su lobby) y con
   `staged`: `<caja>\<cuenta>\slot204` → `preparación\slot204.old` (el `.old` anterior se borra) y
   `preparación\slot204.incoming` → `<caja>\<cuenta>\slot204` (renombrados: mismo volumen); estado `live`. Luego lee
   la cabecera del fichero que el juego va a cargar (`[[actor+0x88]+1] == 0` → lobby, si no save) y la valida;
   registra la ranura con las funciones del juego: `0x2355EC0(S, "slot204", 0)` y, si faltan, `0x235B100(ranura,
   "lobby")` / `(ranura, "save")` (nunca dos veces el mismo nombre: `AddFile` no busca antes y duplicaría la entrada
   del vector). Las dos llamadas van dentro de `__try` con el patrón de `game_fault_continue`: una excepción del
   juego apaga el modo y **se pasa** (no se traga). Cualquier fallo (renombrado, cabecera, ranura no de disco,
   hilo que no es del servidor, caja no sana, fuera de un `LoadSlot` remapeado) → `*res = [exe+0x6CF781C]` (el error
   nativo de "ranura ausente") y el original no se llama; los fallos de E/S o del juego apagan el modo para el resto
   de la ejecución.
4. **Comprobación** (vuelta del mismo gancho): `res == 0`, la cabecera del objeto de fichero (`+0x98`, 0x80 bytes
   que copia el cargador `0x2357DA0`) es **byte a byte** la del fichero, y el contador de la caja vio la lectura de
   `slot204\<fichero>.save`. El lobby verificado fija el usuario de ese login; el personaje solo se da por bueno si su
   `[pj+0xA0]` es ese usuario. Si sí: `lobby-ok` / `save-ok`. Si la cabecera o la lectura no cuadran: **POISONED**, y
   desde ahí ninguna carga ni guardado más del usuario 2 en esta ejecución (también una reconexión se rechaza).
5. **`SavePlayerToSlot 0x186E450`** (gancho de entrada), dueño por `[pj+0xA0]` y la clave del pj: usuario 2 con
   `live` + `lobby-ok` + `save-ok` **y** cuyo usuario es el del login verificado y sigue siendo el usuario 2 de la
   sesión 2 → `r8d = 204` (autoguardado `0xCBD86D3`, 2910 `0x103BE99F`); en cualquier otro caso de usuario 2 (clon,
   envenenada, apagada, sin verificar, otro login, o un autoguardado encolado tras su salida) → **no se llama al
   original**, `*res = [exe+0x6CF7CA0]` (el error de guardado del propio `0x186E450`). Sin identificar: rechazado.
   Los guardados del anfitrión pasan siempre (salvo a la 204): no vuelve el bucle de "Failed to save data".
6. **Exportación** tras un guardado correcto de la 204 (mismo hilo): lee `slot204\{lobby,save}.save` de la caja,
   valida cabeceras, SHA-256, y escribe `%LOCALAPPDATA%\cdmp\guest_saves\<invitado>\gen<n>.partial\` (+
   `manifest.txt`: generación, invitado, motivo con dirección de vuelta, ranura 204 y la pedida, hora UTC, por
   fichero tamaño, sha256, versión, raw y data) que se renombra a `gen<n>`; se quedan las 5 últimas. Luego llama al
   manejador de resultado (`guest_save_set_result_handler`) si lo hay: el gancho para la fase B. `<invitado>` =
   `[steam] peer_steamid` del anfitrión si está puesto, si no `local-debug` con la carpeta de depuración (la fase B lo
   fija con `guest_save_set_guest_id`).
7. **Sondas** (modo `probe` y modo 1): una línea por llamada (con tope) a `LoadSlot`, `SavePlayerToSlot` y
   `FlushPendingSaveData 0x1871930` (llamadores `0x28F273F` fase de salida y `0x108393CD` temporizador): llamador,
   actor/pj, usuario y de quién es, ranura pedida → acción y ranura, opciones, `+0x5C` pendiente, ranura confirmada,
   resultado, hilo (`server realm` o no). En `probe` además dice lo que **haría** el modo 1 (`mode 1 WOULD: ...`) y
   la carpeta de depuración se prepara y valida igual (`STAGED`), pero no se mueve ni se registra nada.

Código comprobado contra el exe del disco (capstone sobre `CrimsonDesert.exe`, PE `0x6AB28F00`) y comprobado otra vez
por el mod antes de instalar: entradas `0x186F570` (47 B, `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44 89 44 24
18 55 41 54 …`), `0x186E450` (47 B, `4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 …`),
`0x1871930` (43 B, `48 89 5C 24 10 48 89 74 24 18 55 57 41 55 …`), `0x2355EC0 E9 DB D8 BF 0C`, `0x235B100` (26 B,
`40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 88 00 00 00 48 8B EA 4C 8B F9`); las siete llamadas `E8` a esas
funciones (`0x2C17881`, `0x2C3E1CE`, `.xtls 0x1042ABF3`, `.xtls 0xCBD86CE`, `.xtls 0x103BE99A`, `0x28F273A`, `.xtls
0x108393C8`); y los cuatro códigos de error se leen de sus globales solo si la instrucción `mov eax,[rip+…]` que los
usa es la esperada (`0x186F6DF`, `0x186F62C`, `0x2069615`, `0x186E780`) y no valen 0. En modo 1 falta cualquiera →
apagado; en `probe` cada sonda va por su cuenta.

### Prueba en UN PC (L1 + L2 + L3 con la plantilla de otra cuenta)

Antes: copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` y la lista de la carpeta real
(`Get-ChildItem -Recurse "$env:LOCALAPPDATA\Pearl Abyss\CD\save" | Select-Object FullName, Length, LastWriteTime`);
debe quedar idéntica al final. Copia de trabajo de la plantilla (nunca la original):
`Copy-Item -Recurse "C:\Emulador CD\backups\plantillas\chapter2-normal-nexus2863\slot108" "C:\Emulador CD\backups\plantillas\invitado-fase-a"`.

1. **L1, solo sondas.** `cdmp.p8-noche-local.ini` tal cual (`guest_own_save=probe`, sin carpeta de depuración).
   Juego, cargar la 108 (o la que toque) y, ya en el mundo:
   `.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_112627.troc --var SAVE_SLOT=108 --linger 1200000`
   (20 min sin despedida para que llegue un autoguardado del usuario 2; luego se cierra el peer y el servidor hace
   la salida a los 15 s). En el log:
   - `Session2 guest save (P8 S25, phase A): guest_own_save=probe | PROBE: log only ... | slot 204 (...) | hooks on:
     LoadSlot 0x186F570, SavePlayerToSlot 0x186E450, FlushPendingSaveData 0x1871930 | callers checked | errors: busy
     0x… invalid slot 0x… absent slot 0x… save 0x…`
   - `Session2 guest save probe: LoadSlot 0x186F570 from exe+0x2C17886 (UserLogin 2490) | actor 0x… (user actor) of
     user 0x… = user 2 | slot <la del anfitrión> -> pass … (probe) | mode 1 WOULD: REFUSE … (fail closed: no guest slot ready …)`;
     lo mismo con `exe+0x2C3E1D3 (3093 lobby)` y `exe+0x1042ABF8 (3093 character)` (también `WOULD: REFUSE`); las del anfitrión, `= the host … -> pass`.
   - El autoguardado del usuario 2: `SavePlayerToSlot 0x186E450 from exe+0xCBD86D3 (autosave) | character 0x… of user
     0x… = user 2 | slot N options 0x… -> pass N (probe) | mode 1 WOULD: REFUSE N (user 2 plays a clone …)` y
     `returned 0x0`; con su `Session2 save redirect #… slotN…` en la caja (ranura del anfitrión: lo de hoy).
   - La salida: `FlushPendingSaveData 0x1871930 from exe+0x28F273F (logout phase) | character … = user 2 | … pending
     slot +0x5C before … | committed slot … | returned 0x…` (responde al punto 6 de `p8-guardado-desviado.md`).
2. **L2+L3, partida propia.** En el ini: `guest_own_save=1` y, en otra línea sin comentario,
   `guest_own_save_debug_dir=C:\Emulador CD\backups\plantillas\invitado-fase-a`. Mismo arranque y el mismo
   `cdmp_peer` (20 min). En el log, por orden:
   - `guest_own_save=1 | user 2 loads and saves ITS OWN slot in the sandbox; fail closed (never the host's) | …`
   - `Session2 guest save: STAGED from guest_own_save_debug_dir '…invitado-fase-a': lobby 518 B v2 sha256 … | save
     736731 B v2 raw … sha256 … | user 2's next load moves it in as slot204`
   - UserLogin: `LoadSlot … from exe+0x2C17886 (UserLogin 2490) | … = user 2 | slot N -> REMAP 204 (the guest's slot
     is ready) | state staged`, `the staged guest slot moved in: '…\slot204.incoming' -> '…\slot204'`, `slot204
     registered in the game's store: slot 0x… (adds lobby and save; 2 file object(s))`, `user 2's lobby loads from
     slot204 …`, `user 2's lobby VERIFIED: the game read '…\slot204\lobby.save' (redirected reads 0 -> 1) …` y
     `LoadSlot from exe+0x2C17886 … slot 204 returned 0x0`; luego la línea de C2 `user 2 lobby … (written)`.
   - 3093: `from exe+0x2C3E1D3 (3093 lobby) … REMAP 204`, `lobby VERIFIED (redirected reads a -> a+1)`; `from
     exe+0x1042ABF8 (3093 character) … REMAP 204`, `user 2's save VERIFIED: the game read '…\slot204\save.save'
     (redirected reads b -> b+1) … | state live lobby-ok save-ok`; y C2 `status of character … (written)`,
     `invariant ok`. (Los contadores también suben con los refrescos de la tabla, que leen la 204 una vez
     registrada; lo que cuenta es que cada carga los haga crecer.)
   - El usuario 2 entra con el personaje de la plantilla (en el peer, los datos de personaje que manda el servidor;
     comparar con los de L1: deben ser distintos de los del anfitrión).
   - Autoguardado: `SavePlayerToSlot … (autosave) … = user 2 | slot N … -> REMAP 204 (user 2's own slot, both files
     verified)`, las líneas `Session2 save redirect #… slot204_new …` / `slot204_backup …`, y `Session2 guest save:
     EXPORTED gen1 to '…\AppData\Local\cdmp\guest_saves\local-debug\gen1' (autosave (ret exe+0xCBD86D3); lobby … B
     sha256 …, save … B sha256 …)`.
   - En la línea de cada minuto: `guest save 1: slot 204 live lobby-ok save-ok | LoadSlot calls … (user 2 …:
     remapped 3, refused 0) | saves … (user 2 1: remapped 1, refused 0) | exported 1 …`.
   - Nunca: `REFUSED`, `POISONED`, `forced OFF`, `GAME FAULTS passed on`.
3. Después (juego cerrado): la carpeta real igual que antes; en la caja `…\<cuenta>\slot204\` con los ficheros del
   autoguardado; en `%LOCALAPPDATA%\cdmp\guest_saves\local-debug\gen1\` `lobby.save`, `save.save` y `manifest.txt`
   con los mismos SHA-256 del log. L5 (que el fichero devuelto carga en un jugador) sigue pendiente: copiarlo a mano a
   una ranura de **otra** copia de la carpeta de partidas, nunca a la real.
4. Negativo rápido (L6 parcial): `guest_own_save=1` **sin** `guest_own_save_debug_dir` → ya el UserLogin del usuario 2
   se rechaza (`LoadSlot … (UserLogin 2490) … = user 2 | slot N -> REFUSE N (fail closed: no guest slot ready …)`), su
   login falla de forma nativa y el usuario 2 no entra; el anfitrión sigue jugando y guardando normal (sus líneas
   dicen `= the host … -> pass`).

### Lo que añade la fase B

- La subida del invitado por el túnel (§3.2, estados 6-8 de §3.8) escribiendo en `guest_save_incoming_dir()` y
  llamando a `guest_save_incoming_ready()`; `guest_save_set_guest_id(<SteamID64>)`; retención del 3093 hasta el
  `SAVE_ACK`.
- La bajada: desde `guest_save_set_result_handler` encolar `SAVE_RESULT` + trozos (§3.5) y el `SAVE_STATUS` con
  `guest_save_state()` (incluido avisar al invitado de `POISONED` o de "esta sesión no guarda tu progreso").
- La tabla de Req de §3.7 en `server.cpp`: permitir 3037/2910 en modo propio, bloquear 2711/2178.
- El guardado final antes de la 2001 (§3.6) y la aplicación en el PC del invitado (v1 a mano, v2 automática).

### Riesgos que quedan [H]

- Primera vez que el mod llama a `GetOrCreateSlot` / `AddFile` (mismo hilo y sección que el guardado nativo, que
  también llama a `GetOrCreateSlot`; `GetOrCreateSlot` empieza con `S->vf3(-1)` como `0x186E450`). L2 lo dice.
- En modo 1, sin ranura lista o si la fase falla, el login del usuario 2 falla (falla cerrado: vuelve al título; el
  anfitrión sigue). Con la ranura `POISONED` también se rechaza una reconexión en esa ejecución. Cómo reacciona el
  cliente del invitado a ese fallo del UserLogin es [H] (L6, S1).
- La tabla del gestor tendrá una entrada 204 tras el primer refresco: si el menú "Cargar" del anfitrión la enseña y
  la elige, el mod la rechaza (ranura no válida).
- "Sin identificar" solo sale si el usuario del pj no se puede leer (o su sesión no es ninguna conocida) mientras
  existe una sesión 2; el anfitrión se reconoce por su sesión aunque `server_host_user` fallara.
- La exportación (~1,2 MB + SHA-256) va en el hilo del servidor dentro del guardado (el diseño lo admite).
- El estado del mundo dentro de la partida del invitado (§3.10) no cambia con esta fase: medir en L4 antes de aplicar.

### Prueba en vivo del 12-10 (00:36) y corrección del registro de la ranura

Log `bin64\cdmp_20261012-003108.log` (ini `cdmp.p8-noche-prueba1pc.ini`, `guest_own_save=1`, plantilla de Nexus
preparada), `cdmp_peer` en replay como usuario 2: el UserLogin se remapeó a la 204, la ranura preparada entró en la
caja y la carga se **rechazó** con `after AddFile the files are lobby 0 save 0 other 2`; el login del usuario 2 falló
de forma nativa, sin cierre del juego (falla cerrado como se esperaba).

Causa [E]: el mod leía el vector `ranura+0x688` como **objetos de fichero** (nombre en `+0x88`/`+0`). Ese vector
guarda **punteros a los caracteres del nombre** (`const char*`): `RemoveFile 0x235B450` hace `strcmp` de cada entrada
con el nombre (`0x235B4D4..`), y `AddFile` mete los caracteres de la clave insertada (`0x235B3ED..42F`). Las dos
llamadas a `AddFile` habían funcionado. El cargador no usa ese vector: busca en el **mapa nombre → objeto** de
`ranura+0x08` (`0x20688A0`: `HashLittle 0x1364780` + `HashMap_Find 0x612620(ranura+8, hash, &clave, &índice)`, el
objeto en `entrada+0x10`, `0x20689D9..9FF`). Estructura del mapa (de `HashMap_Find`): `+0x00` u32 número de cubos,
`+0x10` cubos de 0x100 B (`u32 n` y luego n × `{u32 hash, u32 índice}` desde `+8`), `+0x18` vector de entradas; una
entrada: `+0x08` clave = cabecera de cadena `{char*; u32 longitud (0xFFFFFFFF = sin contar)}`, `+0x10` el objeto de
fichero (cabecera del fichero en `+0x98`). Los nombres van en minúsculas y se comparan tal cual.

Corrección (`gsv::read_slot_files`, solo lecturas protegidas, plantilla probada con una imagen falsa de memoria con
esta estructura en `optd_guest_save`): lee los nombres del vector (byte a byte, hasta 8) y recorre el mapa entero
(todos los cubos, sin calcular el hash); `plan_slot_files(nombres, mapa)` exige que las dos estructuras coincidan
(si no: `Inconsistent`, rechazo) y no llama a `AddFile` para un nombre que ya está en alguna. Los objetos para la
comprobación de `+0x98` salen del mapa (los mismos que usa el cargador). Si algo falla, la línea `ERR` lleva ahora
todo lo leído: `names 2 [0x…:'lobby' 0x…:'save'] (lobby 1 save 1 other 0) | map 2 [0x…:'lobby' hash 0x… len 5 ->
0x… …] (lobby 1 save 1 other 0)`, con los bytes en hexadecimal si no son imprimibles y `READ FAILED: …` si una
lectura no se pudo hacer. La línea `slot204 registered in the game's store` también lo lleva. Siguiente prueba: la
misma; se espera `registered … (adds lobby and save): names 2 […] | map 2 […]` y luego `user 2's lobby VERIFIED`.

**Resultado (12-10, 01:03, `bin64\cdmp_20261012-010005.log`)** [V]: con la plantilla de Nexus preparada, el usuario 2
(`cdmp_peer` en replay) entró con SU partida desde la ranura 204 de la caja: lobby VERIFIED, save VERIFIED, invariante
C2 bien, ancla del mundo puesta, sin errores. No se vio ningún guardado del usuario 2: en 20 min no hubo autoguardado
suyo, y el `FlushPendingSaveData` de su salida confirmó la ranura 0 pendiente del anfitrión (§2.3: la salida no
serializa). Por eso la fase B añade el guardado final (§3.6).

---

## Implementado (fase B: el túnel, el invitado y `cdmp_peer`)

12 de octubre de 2026, rama `opcion-d` (sobre `5b53e71`). Build del juego **1.0.0.2976**. Sin juego: compilado y probado
con `.\scripts\build.ps1` (52/52 tests). No se lanzó el juego ni se desplegó nada. Revisado el mismo día (commit
siguiente a `b930b13`): el guardado final pasa de 3037 a **2910** tras la prueba en vivo de las 04:09 (abajo) y se
corrigen los puntos 1-9 de la revisión de código de `b930b13` (citados donde aplican).

### Protocolo (estados de control del túnel)

Paquetes de control del túnel (`troc::kControlOpcode` = 1, ranura -1), LE. **No cambia `protocol.cpp`** (`kVersion`
sigue igual): la compatibilidad la decide el bit de capacidad **`kCapsGuestSave = 0x02`** del paquete de capacidades del
túnel (`troc_frame.h`, `kCapsOpcode`), que el invitado manda junto a `kCapsCompressed` antes de su primer paquete de
cada conexión (`TrocLink::set_announce_flags`, nuevo; `TrocLink::peer_caps()`, nuevo). El anfitrión solo manda los
estados 9 y 10 a un invitado que lo dijo en la conexión actual. Un extremo viejo ignora (invitado) o descarta
(anfitrión: no es una Req del servidor) estos estados.

| Estado | Dir. | Formato | Tamaño |
|---|---|---|---|
| 6 `SAVE_OFFER` | I→A | u8 6, u8 ver 1, u32 xfer, u64 SteamID64 invitado, u32 N, u32 base_gen, u8 nfiles 2, 2×{u8 tipo (0 lobby, 1 save), u32 tamaño, 32 B SHA-256} | 97 B |
| 7 `SAVE_CHUNK` | ambas | u8 7, u32 xfer, u8 tipo, u32 offset, u16 len, len B (1..28 672) | 12 B + len |
| 8 `SAVE_ACK` | A→I | respuesta a una oferta: u8 8, u32 xfer, u8 resultado, u32 gen (la generación del buzón que el anfitrión preparó **en lugar** de lo subido; 0 = lo subido) | 10 B |
| 8 `SAVE_ACK` | I→A | mismo formato, resultado 8 (*cancelled*): el invitado dejó de esperar (sin partida propia en esta sesión) | 10 B |
| 9 `SAVE_RESULT` | A→I | u8 9, u8 ver 1, u32 xfer, u32 gen, u32 N, u8 motivo (1 auto, 2 manual 2910, 3 final del invitado, 4 enlace perdido), u64 hora Unix, u8 nfiles 2, 2×{tipo, tamaño, SHA-256}; luego sus `SAVE_CHUNK` | 98 B |
| 10 `SAVE_STATUS` | A→I | u8 10, u8 ver 1, u8 modo (0 apagado/clon, 1 lista, 2 **activa**, 3 envenenada, 4 probe), u32 Ng (204), u32 último error del juego, u64 SteamID64 anfitrión, u32 última generación | 23 B |
| 11 `SAVE_RESULT_ACK` | I→A | u8 11, u32 xfer, u8 resultado, u32 gen | 10 B |

Resultados (`gsw::AckResult`): 0 ok, 1 hash, 2 cabecera, 3 tamaño, 4 ocupado, 5 apagado, 6 ranura, 7 identidad (el
SteamID64 de la oferta no es el par de la sesión de Steam), 8 cancelado, 9 protocolo, 10 E/S. Desviaciones del §3.8:
el acuse del invitado a un resultado es un estado propio (**11**) en vez del 8; 10 lleva versión, SteamID64 del
anfitrión y última generación, y un modo 4 (probe); hay resultados 7-10. Trozos de 28 672 B como mucho (una partida de
736 731 B son 26 trozos + 1 del lobby). Integridad: SHA-256 en C++ puro (`src/common/sha256.h`, comprobado con los
vectores del NIST y contra BCrypt) y la cabecera como el juego (`gsv::check_header`) en los dos extremos.

### Qué hay

| Pieza | Dónde | Qué hace |
|---|---|---|
| Lógica pura | `src/mod/session2/guest_save_wire.h`, `src/common/sha256.h` | codificación de los estados 6-11, filtro exacto de las tramas, trozos (`Outgoing`) y reensamblado (`Assembler`: en orden por fichero, topes, transferencia vieja ignorada, hash y cabecera), elección de ranura (`pick_continue_slot`, `parse_slot_dir`) y de cuenta (`pick_account`), libro de resultados del anfitrión (`ResultSender`), retenciones del invitado (`GuestHold`), guardado final del anfitrión (`FinalSave`), política de Req (`req_policy`), modo de `SAVE_STATUS`, manifiestos |
| Tests | `tests/optd_guest_save_b_tests.cpp` (`optd_guest_save_b`), `tests/optd_replay_tests.cpp` (`optd_replay`) | lo anterior; con los ficheros reales de la plantilla de Nexus si están en el PC (518 + 736 731 B, 27 trozos); y `cdmp_peer` de punta a punta contra un anfitrión falso por UDP: subida antes de la primera Req, espera del `SAVE_ACK`, 3037 antes de la despedida, resultado comprobado y guardado byte a byte, despedida después del acuse |
| Anfitrión, túnel | `src/mod/session2/guest_save_tunnel.{h,cpp}` | subida → `guest_save_incoming_dir()` + `guest_save_incoming_ready()`; buzón y generaciones; `SAVE_RESULT` a ritmo; `SAVE_STATUS`; política; guardado final |
| Anfitrión, fase A | `guest_save_host.{h,cpp}` | nuevos: `guest_save_incoming_begin/_write` (bajo el cerrojo de las carpetas: quita la marca `staged` antes de tocar nada), `guest_save_set_guest_slot`, `guest_save_mailbox_folder`, `guest_save_exports`, `guest_save_last_error`; el manifiesto del buzón lleva `guest_slot=N` y `time_unix=` |
| Anfitrión, servidor | `server.cpp` | el canal real de las Req bloqueadas (`g_blocked_channel`) para dejar pasar 2910 (ranura 100-108) y 3037 en modo propio; `inject()` con tipo `FinalSave`; el 2910 final antes de la 2001 (`final_save_step`, `server_logout_user2(why, final_save)`) |
| Anfitrión, sesión | `session2.cpp` | las tramas de guardado se sacan **antes** de la admisión C4 (una subida no ata la conversación); `offer()` aplica la política; enlace perdido → guardado final |
| Invitado | `src/mod/session2/guest_save_guest.{h,cpp}`, `guest.cpp`, `mod.cpp` | elección y lectura de la ranura en su propio hilo al arrancar; subida en cuanto el anfitrión habla; retenciones en `guest_send` / `guest_pump`; recepción y escritura en su hilo |
| `cdmp_peer` | `tools/peer_sim/save_sim.{h,cpp}`, `replay.cpp`, `main.cpp` | `--guest-save`, `--guest-slot`, `--guest-id`, `--base-gen`, `--save-at`, `--save-before-bye`, `--save-results` |
| Aplicar | `tools/scripts/aplicar-progreso-invitado.ps1` | en el PC del invitado, con el juego cerrado (abajo); elige la generación válida más nueva por la **hora** de su manifiesto (`time_unix`), no por el número (revisión, punto 7); se niega con una ranura que tenga subcarpetas (punto 8) y comprueba el tope de `raw` |
| Ini | `[session2] guest_own_save` | invitado: `cdmp.p8-s7-shadow.ini` y `-fase2.ini` = `probe`; plan B `cdmp.p8-s12-shadow-seguro.ini` = 0; anfitrión `cdmp.p8-s7-host.ini` sigue en `probe` |

### Claves

`[session2] guest_own_save = 0 | probe | 1` — la misma clave en los dos PC:

- **Anfitrión** (como en la fase A, más el túnel): `0` = nada (una oferta se contesta `SAVE_ACK` 5 *apagado* al momento);
  `probe` = acepta, comprueba y deja preparada la subida, pero el usuario 2 juega el clon y no se devuelve nada
  (`SAVE_STATUS` 4); `1` = carga y guarda la partida del invitado y se la devuelve.
- **Invitado** (`guest=1`, `guest_mode=remote`): `0` = nada (sin el bit 0x02); `probe` = elige, comprueba y sube su
  ranura y guarda lo que vuelva, **sin retener ningún paquete del cliente ni añadir ninguna Req**; `1` = además las
  retenciones (abajo). Sin `guest_mode=remote` se queda apagado (una línea).

### Cómo funciona

**Invitado, al arrancar** (`guest_save_guest_configure`, su hilo): `%LOCALAPPDATA%\Pearl Abyss\CD\save\<cuenta>` (la
carpeta con el número de cuenta = los 32 bits bajos de su SteamID64, o la única de dígitos; si hay varias espera hasta
60 s a conocer su SteamID64); la ranura que cargaría "Continuar": la de `save.save` más reciente entre 0-2 y 100-108
con `lobby.save` y `save.save` (empate: el número menor); **se niega** si esa ranura tiene `slotN_new` o `slotN_backup`
(un guardado interrumpido: que la cargue una vez en solitario) o si una cabecera no pasa. Lee los dos ficheros (lectura
compartida, nunca escribe en `save\`) y calcula los SHA-256. Lee también los `applied.txt` de
`%LOCALAPPDATA%\cdmp\guest_progress\*\` (lo que el script aplicó: su `base_gen`).

**Subida.** En cuanto llega el primer paquete de control del anfitrión en una conexión del túnel (su "listo" o su
personaje; **antes** de que `guest.cpp` lo dé por listo, así que antes del primer paquete del cliente que pueda ir al
anfitrión, su 2319/2490) se encola la oferta, y `guest_take_outgoing` la saca **antes** que los paquetes del cliente;
los trozos salen **a ritmo**, como la bajada (mientras la cola propia del túnel esté por debajo de 256 KB), y mientras
quede alguno los paquetes del cliente esperan detrás en su cola (revisión, punto 1: antes salían todos de golpe y un
`push` rechazado por el tope de 16 MB del túnel se ignoraba; ahora se cuenta y se avisa, el enlace se reinicia y la
subida se repite). Una nueva conexión (nueva época de la sesión) la repite mientras el cliente no haya jugado en
remoto en esta ejecución. El anfitrión procesa las tramas en el orden del túnel, en su hilo de red: reensambla, comprueba
(SHA-256 y cabecera), escribe en `cdmp_guest_save_staging\<cuenta>\slot204.incoming\` y la prepara
(`guest_save_incoming_ready`) **antes** de mirar la 2490 que viene detrás. Contesta `SAVE_ACK`.

**Generaciones y reconexión** (§3.5.6, corregido en la sesión 25): si el buzón de ese invitado
(`%LOCALAPPDATA%\cdmp\guest_saves\<SteamID64>\`) tiene una generación más nueva **de la línea de lo subido** (lo subido
es su raíz o una de sus generaciones: `gsw::find_continuation`), se prepara **esa** (comprobada con su manifiesto) en
vez de lo subido y el `SAVE_ACK` lo dice (`gen`); otra partida se prepara tal cual, como línea nueva. Tras aceptar una oferta, la generación más nueva que el invitado no acusó (`delivered.txt`
del buzón) ni aplicó se le vuelve a mandar. Identidad: con Steam, el SteamID64 de la oferta tiene que ser el del par de
la sesión (si no, `SAVE_ACK` 7); por UDP (`cdmp_peer`) vale el de la oferta, o `local-peer` si es 0. La numeración
(revisión, punto 6) queda siempre por encima del `base_gen` del invitado y de lo que acusó
(`guest_save_set_generation_floor`, `gsv::next_generation_above`), también si se vació el buzón; una exportación que aun
así no sea más nueva que lo que el invitado tiene se registra (`gen… is NOT sent`), nunca se tira en silencio.

**Bajada.** Cada exportación de la fase A (hilo del servidor) solo se encola; en el hilo de red se manda
`SAVE_RESULT` + trozos, uno a uno mientras la cola propia del túnel esté por debajo de 256 KB. Solo vale la generación
más nueva: una nueva aborta la que se estaba mandando. El invitado la reensambla, la comprueba y la escribe **en su
hilo** (el disco de Shadow llegó a tardar segundos) en `%LOCALAPPDATA%\cdmp\guest_progress\<SteamID64 anfitrión>\incoming\gen<n>\`
(`lobby.save`, `save.save` y `manifest.txt` al final, por `.partial` + renombrado; se quedan las 10 últimas) y acusa con
`SAVE_RESULT_ACK`. Sin acuse en 60 s, o con error, se reintenta (3 veces por conexión; luego espera a la siguiente). El
motivo: 4 si el anfitrión inyectó el 2910 final, 3 si el invitado mandó su 2910 final (nombre `Co-op`) en los últimos
20 s, si no el del llamador (autoguardado / 2910). Revisión: una generación con ese número ya guardada solo vale si sus
dos ficheros (no solo el manifiesto) tienen el tamaño y el SHA-256 anunciados (punto 2); si no, se sustituye (no se
guardan copias aparte sin límite, punto 3); y solo espera una escritura a la vez: otra que llegue mientras tanto se
contesta *ocupado* y el anfitrión la reintenta, con los bytes movidos, no copiados (punto 4).

**Retenciones del invitado (solo modo 1, `gsw::GuestHold`)**:
- *De arranque*: mientras la oferta se prepara, los paquetes del cliente que van al anfitrión esperan (en orden) a que
  la subida esté en la cola delante de ellos; si no hay nada que ofrecer, salen al momento.
- *El 3093* (y todo lo que venga detrás) espera el `SAVE_ACK` de la oferta actual, **30 s como mucho**; un rechazo de
  hash/cabecera/tamaño/E-S se reintenta una vez; *apagado*, *ranura* e *identidad* no. Si se agota: se suelta, el
  invitado manda `SAVE_ACK` 8 (*cancelled*) y avisa "this session does NOT keep your progress".
- *3093 de otra ranura*: si el 3093 pide una ranura M distinta de la ofrecida (válida), se ofrece M primero (una vez);
  si M no se puede ofrecer, se queda la ofrecida (`SlotKept`, aviso: el progreso vuelve a la ranura ofrecida). La
  ranura ofrecida cuenta como pedida (revisión, punto 9): un 3093 que vuelva a la primera la vuelve a ofrecer.
- *Despedida*: si el anfitrión dice `SAVE_STATUS` 2 (*activa*), el 3093 ya salió y la subida fue aceptada, la 2001 del
  cliente espera detrás de un **`SaveToFileCurrentPlayerReq` 2910** (ranura 100, nombre `Co-op`, abajo) hasta que llega
  y se guarda una generación nueva, **15 s como mucho**, o hasta que la `SaveToFileCurrentPlayerAck` 3016 del servidor
  dice que no se guardó (entonces sale al momento). El reloj de la despedida (`guest_goodbye_logic.h`) empieza cuando la
  2001 sale de verdad.
- Una conexión rota tira lo retenido (una 2001 retenida pasa a la lógica de despedida como siempre).

**Política de Req del túnel** (§3.7, `gsw::req_policy`, mira también la carga útil): con `guest_own_save=1` y la ranura
del invitado `live` (sin `disabled` ni `poisoned`), `SaveToFileCurrentPlayerReq` 2910 **para una ranura manual 100-108 y
bien formado**, y `SaveAutoReq` 3037 (inocuo: no guarda, abajo), llegan al servidor (por su canal real;
`SavePlayerToSlot` los remapea a 204 o los rechaza, nunca la ranura del anfitrión); un 2910 para la ranura 3 (CrossSave)
o mal formado, `CreateCharacterReq` 2711 y `NewGameStartReq` 2178 **nunca** (contados, `own-save Req` en el informe). En
`probe` y `0`, como antes. 2579, `DeleteSave*`, trucos: bloqueados siempre.

**Guardado final al perder el enlace** (§3.6, `gsw::FinalSave`): si el enlace se pierde con la partida propia activa
(modo 1, `live lobby-ok save-ok`), `hand_logout` inyecta primero el mismo 2910 (ranura 100, `Co-op`, por la cola de
`inject` con el permiso `FinalSave`, después de cerrar un movimiento abierto) y la 2001 espera a la siguiente exportación,
15 s como mucho. La generación queda en el buzón y se manda en la próxima conexión.

### Prueba en vivo del 12-10 (04:09) y cambio del guardado final a 2910

Log `bin64\cdmp_20261012-040922.log`, consola `C:\Emulador CD\backups\peer-noche-1pc-d.txt`, ini
`cdmp.p8-noche-prueba1pc-b.ini` [V]: subida (27 trozos), `SAVE_ACK ok`, `STAGED`, `REMAP 204`, lobby y save `VERIFIED`,
`SAVE_STATUS` 2: todo bien. Pero el guardado final **no se hizo**: 04:12:45 `user 2's SaveAutoReq 3037 handed to the
server`, después ninguna llamada a `SavePlayerToSlot 0x186E450`, ningún 3016 en el peer, `the goodbye goes (no
SAVE_RESULT in 15 s)`, y en la salida `FlushPendingSaveData` confirmó la ranura 0 pendiente del anfitrión, como antes.

Por qué [E]: el manejador del 3037 (`0x2A21E60`) llama a `SaveGameRequest(pj, &res, motivo 7, &opc = el u32 de la carga)`
(`0x28FC5F0` → `.xtls 0x103BC7A0`), que encola el 2077 interno con motivo 7 → `0x28FC6F0` → `SavePlayer` (`.xtls
0xCBD84D0`, thunk `0x186E1A0`). Este, con su bandera `[exe+0x6CE4A28]` puesta y `opc & 1 == 0`, convierte el motivo en
una clase = motivo − 1 (la constante de `.xtls 0xD7D6BB0` vale −0x2DDEB700): las clases 0, 1 y 3 guardan, la 2 solo 900 s
después del último guardado y **cualquier otra (motivo 7 → clase 6) devuelve 0 sin guardar y sin 3016**. Es justo lo que
se vio.

El camino inmediato es `SaveToFileCurrentPlayerReq` 2910 [E]: manejador `0x2A21B80` → `.xtls 0x104B93B0`, carga útil
**`u32 ranura | u32 n (≤ 30) | n caracteres UTF-16 | u16 0`** (el escritor del cliente `.xtls 0xA4624C0`, llamado desde
`.xtls 0x992CCD0`, manda exactamente eso, con el byte de ranura del paquete de su sesión); `0x28FD6E0` exige ranura
100-108 (o 3), que el actor de la sesión sea el personaje principal de su usuario y que el filtro de palabras
`0x12C35A0` acepte el nombre; guarda el nombre como título de la partida (`0x51A510` en el componente del lobby) y
llama a `.xtls 0x103BE8E0`: `CanSave 0x186F000` (opciones 0: campo cargado, `[pj+0xC3]`, no en ciertos estados), el byte
ocupado del gestor, `SavePlayerToSlot` (llamada `0x103BE99A`, la `Manual2910` de la fase A: se remapea a 204) y
`SaveToFileCurrentPlayerAck` 3016 (escritor `0x2A1FD40`): **`u32 clave del pj | u32 ranura | u8 ok | u8 (opc & 3) == 0 |
u32 resultado`**.

Cambio (`gsw::make_final_save_req`, `parse_save_req`, `parse_save_ack`): el guardado final del invitado, el del anfitrión
al perder el enlace y el de `cdmp_peer` (`--save-before-bye`, `--save-at`) son un **2910 para la ranura 100 con nombre
`Co-op`** (byte de ranura 0). El remapeo de la fase A lo lleva a 204 (el 3016 del cliente dice 100). El 3016 se registra
(`SAVED` / `NOT saved (result 0x…)`) y, si dice que falló, la despedida no espera los 15 s. El 3037 sigue permitido en
modo 1 (no hace nada) pero el mod ya no lo usa. Efecto visible: el título de esa partida del invitado pasa a ser `Co-op`.

### Prueba en UN PC (ida y vuelta completa con `cdmp_peer`)

Antes: copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\` y la lista de la carpeta real (debe quedar
idéntica). Ini del anfitrión: `cdmp.p8-noche-prueba1pc.ini` (`guest_own_save=1`) **con la línea
`guest_own_save_debug_dir=` comentada** (la fuente es ahora la subida; con las dos, una subida rota dejaría preparada la
de depuración). Buzón limpio: renombrar `%LOCALAPPDATA%\cdmp\guest_saves\local-peer` si existe (si no, §3.5.6 prepararía
su última generación en vez de la subida, y lo diría). Juego, cargar la partida del anfitrión y, ya en el mundo:

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00bye_112627.troc --var SAVE_SLOT=108 --guest-save "C:\Emulador CD\backups\plantillas\invitado-fase-a" --guest-slot 108 --save-before-bye --save-results "C:\Emulador CD\backups\plantillas\invitado-fase-b-resultados"
```

(20 s en el campo del anfitrión, el 2910 final, y la despedida tras el resultado). Para un guardado a mitad: el replay sin
despedida con `--save-at 60 --linger 600000`.

En la consola de `cdmp_peer`, por orden: `Guest save: …invitado-fase-a ready as slot 108`, `SAVE_OFFER xfer … + 27
chunks -> the host`, `SAVE_ACK xfer …: ok`, `Playing the Req now`, `SAVE_STATUS mode 1 = ready` y luego `mode 2 = active`,
`SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op') -> the host's server (before the goodbye…)`,
`SaveToFileCurrentPlayerAck 3016 from the server: character 0x… slot 100 -> SAVED (result 0x0)`,
`SAVE_RESULT gen1 (…, final (the guest's 2910 before its goodbye), slot 108; …)`, `gen1 checked (SHA-256, header) and
stored in …\gen1; SAVE_RESULT_ACK ok`, `the goodbye goes (the final save came back)`.

En `bin64\cdmp.log`, por orden: `Session2 guest save (P8 S25, phase B): the tunnel speaks states 6-11 …`; `Session2
guest save link: SAVE_OFFER xfer … from guest 0 (connected peer unknown (UDP)) | its slot108 | base gen 0 | lobby 518 B
… | save 736731 B …`; `Session2 guest save: STAGED from the guest's upload (xfer …, its slot108, guest local-peer) …`;
`upload xfer … VERIFIED and STAGED for guest local-peer (its slot108); SAVE_ACK ok | user 2's next lobby load moves it in
as slot204`; `the guest announced caps 0x03 …`; `SAVE_STATUS to the guest: mode 1 = ready …`; las de la fase A (`REMAP
204`, `moved in`, `lobby VERIFIED`, `save VERIFIED`); `SAVE_STATUS … mode 2 = active`; `user 2's SaveToFileCurrentPlayerReq 2910
(slot 100, the guest's final save) handed to the server (own-save mode: it saves now …)`; `SavePlayerToSlot … (manual
2910) … REMAP 204`; `EXPORTED gen1 to '…\guest_saves\local-peer\gen1'`;
`gen1 (final (the guest's 2910 before its goodbye)) queued for the guest`; `SAVE_RESULT gen1 -> the guest (xfer …, its
slot108, …; lobby 518 B, save … B in 27 chunks, paced under 256 KB …)`; `all 27 chunks of gen1 are in the tunnel …`;
`the guest STORED gen1 (SAVE_RESULT_ACK ok …)`; luego la salida C4. Nunca: `REFUSED`, `POISONED`, `forced OFF`, `GAME
FAULTS passed on`, `BROKE`.

Si el servidor no deja guardar [H] (`CanSave 0x186F000`: campo no cargado, `[pj+0xC3]` a 0, ciertos estados), el 3016
dice `NOT saved (result 0x…)`, la consola `the goodbye goes (the server's 3016 says the final save failed)` al momento y
el log no tiene `EXPORTED`: anotar el código.

Segunda vuelta (L4 y §3.5.6): el mismo comando otra vez (con `--base-gen 0`): el anfitrión dice `the upload is the
root of line … and the mailbox … has its gen1: that one is staged INSTEAD of the upload` y el peer `SAVE_ACK … ok (the
host continues this save from its mailbox gen1)`; el usuario 2 entra con lo guardado en la primera vuelta. Con otra
partida, `no mailbox generation continues the upload's save …: it is staged as a new line`. Con `--guest-save …\gen1 --base-gen 1`
se sube la devuelta.

L5 (que lo devuelto se aplica bien) sobre una **copia**: `.\tools\scripts\aplicar-progreso-invitado.ps1 -Progreso
"C:\Emulador CD\backups\plantillas\invitado-fase-b-resultados" -CarpetaPartidas <copia de save> -Ranura 108
-SoloComprobar`, y después sin `-SoloComprobar`. Nunca sobre la carpeta real del anfitrión.

### Prueba en dos PC (Shadow invitado, este PC anfitrión)

1. **Ronda 1, solo transferencia** (los dos en `probe`: `cdmp.p8-s7-host.ini` y `cdmp.p8-s7-shadow.ini`). En Shadow,
   antes de abrir el juego: lista de `%LOCALAPPDATA%\Pearl Abyss\CD\save` (nombres, tamaños, fechas). Sesión normal. Se
   espera en Shadow `the offer is ready: slotN: the most recent save.save …`, `the start offer -> the host: SAVE_OFFER
   …`, `the host ACCEPTED the upload …`, `the host says: probe (…)`; en el anfitrión `SAVE_OFFER … from guest <SteamID64
   de Shadow> (connected peer <el mismo>)` y `VERIFIED and STAGED … probe: nothing moves in`. El usuario 2 juega el clon
   como siempre. Mide el tiempo de la subida en el título (líneas `Net:` / `host tunnel` sin tics lentos).
2. **Ronda 2, partida propia**: `guest_own_save=1` en los dos inis (el resto igual). Shadow pulsa "Continuar": entra con
   SU personaje. Jugar, esperar un autoguardado (o guardar en ESC: 2910) → anfitrión `EXPORTED genN`, `SAVE_RESULT genN ->
   the guest`, `the guest STORED genN`; Shadow `genN of the host STORED in '…\guest_progress\<anfitrión>\incoming\genN'`.
   Salir por el menú: Shadow `a SaveToFileCurrentPlayerReq 2910 (slot 100, 'Co-op') goes first …`, el 3016 `SAVED` y
   `the final save came back` (o `the server refused the final save` con su código, o el aviso de 15 s).
3. **Aplicar en Shadow**, con el juego CERRADO: `.\tools\scripts\aplicar-progreso-invitado.ps1 -SoloComprobar` y, si
   dice que la generación es válida, sin `-SoloComprobar`. El script hace copia de seguridad en
   `%LOCALAPPDATA%\cdmp\guest_backups\<fecha>_slotN\`, prepara la ranura en el mismo volumen, cambia las carpetas con dos
   renombrados, comprueba los SHA-256 y deja `applied.txt` (su siguiente oferta lleva ese `base_gen`). Cargar en
   solitario: experiencia, objetos, misiones y posición del invitado; nada del anfitrión en su inventario (S2).
4. Shadow devuelve por la carpeta compartida (`cdmp-shadow\logs\`) su `cdmp.log`, la lista de `save` de antes y después de la sesión
   (debe ser idéntica antes de aplicar), la carpeta `guest_progress\<anfitrión>\` (manifiestos) y la salida del script.

**Criterios para abortar** (volver a `probe` o al plan B):
- `POISONED`, `forced OFF`, `GAME FAULTS passed on`, `REFUSED` de una carga del usuario 2 en modo 1, o el usuario 2 con
  el personaje del anfitrión en modo 1;
- `the export FAILED`, `could NOT be stored` repetido, `cannot be sent`, o `NOTHING is offered` (arreglar la ranura en
  solitario antes);
- la carpeta `save` de Shadow cambia durante la sesión, o la del anfitrión en cualquier momento;
- tics lentos del túnel (`host tunnel: one tick took`) o desconexiones que coinciden con la subida o una bajada;
- cualquier error del script de aplicar (nunca aplicar a mano; la copia de seguridad está en `guest_backups`).

### Riesgos que quedan [H]

- Si `CanSave 0x186F000` deja guardar al usuario 2 justo al despedirse (campo cargado, `[pj+0xC3]`, sin combate…): si
  no, el 3016 lo dice y se pierde lo hecho desde el último autoguardado, como en la fase A.
- El byte de ranura del 2910 que mandan el invitado y el anfitrión es 0 (el escritor del cliente pone el de su sesión;
  las Req de las mismas banderas, 2549 y 2445, llevan 0 en las capturas); ninguna captura tiene un 2910 de un cliente.
- El guardado final cambia el título de esa partida del invitado a `Co-op` (lo escribe `0x28FD6E0`), y su cliente recibe
  un 3016 que no pidió justo al despedirse.
- La cabecera ahora también acota el tamaño `raw` (64 MB y 255 veces los datos: revisión, punto 5) en los dos PC y en
  el script; el juego solo comprueba `0 < datos <= raw`.
- El hilo de red del anfitrión escribe ~1,2 MB y calcula dos SHA-256 al final de una subida, y lee ~1,2 MB al empezar
  cada bajada (milisegundos en su SSD).
- §3.5.6 (desde la sesión 25): si el invitado recibió una generación y no la aplicó, y luego jugó esa partida en
  solitario, su fichero ya no es de la línea: la siguiente sesión carga su progreso local como línea nueva (lo jugado
  en solitario gana; la generación recibida sigue en `incoming` y el script ya no la aplica sobre esa ranura).
- Un anfitrión sin la fase B y un invitado en modo 1: el 3093 espera 30 s y sigue sin partida propia.
- El estado del mundo dentro de la partida del invitado (§3.10) sigue sin medir: por eso la aplicación es manual (v1).
- Steam Cloud del invitado sube la ranura aplicada al cerrar el juego (lo esperado, §3.5.4).

## Sesión 25: líneas (12-10, tras la prueba con Shadow)

**El fallo.** En la segunda entrada del paso 2 Shadow subió su ranura 0 (otra partida, mucho más avanzada) y el anfitrión
preparó EN SU LUGAR la gen2 de la partida de pruebas que tenía en el buzón: la regla de §3.5.6 solo miraba el SteamID y
el `base_gen`. A partir de ahí cualquier ranura acababa en la de pruebas, y las generaciones volvieron marcadas para las
ranuras 0 y 1 (aplicarlas habría pisado las partidas avanzadas). Los ficheros reales de Shadow no se tocaron.

**Líneas.** Cada generación lleva su **raíz** (`lineage_root`: el SHA-256 del save.save que subió la primera sesión de
la línea) y su **base** (`lineage_base`: el save.save que el invitado subió en la sesión que la hizo).
- Anfitrión (`gsw::find_continuation`): una generación del buzón sustituye a lo subido solo si lo subido es la raíz de
  su línea o una de sus generaciones y la línea tiene una más nueva. Las generaciones sin raíz (builds anteriores) no
  casan nunca. La etiqueta (invitado, ranura, raíz, base) se prepara con los ficheros y pasa a viva al entrar en
  `slot204`; las exportaciones llevan la viva, y se descartan si otro par entró mientras se leían los ficheros.
- `SAVE_RESULT` versión 2 (162 B) lleva raíz y base; el invitado contesta `SAVE_RESULT_ACK 9` y avisa una vez si le llega
  otra versión. El invitado guarda `lineage_root` y `lineage_base` en su `manifest.txt`.
- Script de aplicar: una generación solo va a una ranura sin partida o con una partida de su línea (la raíz, una
  generación, la base de una generación o lo último aplicado según `applied.txt`), nunca hacia atrás dentro de la
  línea, y nunca una generación sin raíz. Escoge la más nueva que cabe.

**Pruebas en un PC** (juego como anfitrión, `cdmp_peer` como usuario 2): la de pruebas con el buzón lleno de
generaciones sin raíz (línea nueva), la misma otra vez (continúa desde gen3; gen4 mantiene la raíz), otra partida con
el buzón lleno de la primera línea (línea nueva: el caso de S25 corregido); el script sobre los resultados reales (escoge
gen5 de la línea de la ranura y rechaza gen6) y 12 casos sintéticos (incluidos los de la revisión: generación aplicada
ya podada, y vuelta atrás). Carpeta real de partidas intacta.
