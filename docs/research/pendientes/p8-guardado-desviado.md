# P8 sesión 24: guardado desviado (`save_guard=redirect`)

11 de octubre de 2026, desde `opcion-d` `7c7b651`. Build del juego **1.0.0.2976** (base `0x140000000`). No se lanzó el
juego ni se desplegó nada. **[V]** = comprobado (traza, código, tests); **[H]** = a medir en vivo.

Pendiente del ROADMAP (punto 1, "Antes de seguir"): con `save_guard=1` el guardián rechaza el autoguardado, el juego
lo reintenta cada 10 s y sale el aviso **"Failed to save data"** en bucle (tras ESC y a los ~16 min). La solución: no
rechazar, **desviar** la carpeta de partidas entera a una copia.

## 1. Lo que hace el juego al guardar (traza del 11-10) [V]

Log: `C:\Emulador CD\backups\traza-guardado-20261011-223220.log` (`save_guard=trace`, `cdmp.p8-save-trace.ini`),
líneas `Session2 save trace #N`: 73 operaciones, todas en un hilo (tid 42836). Por API: `GetFileAttributesW` 33,
`FindFirstFileW` 13, `CreateFile2` 7, `MoveFileExW` 5, `CreateDirectoryW` 3, `CreateFileW` 2, `RemoveDirectoryW` 2,
`SetFileAttributesA`/`W` 2+2, `CopyFileW`/`CopyFileExW` 1+1, `DeleteFileW` 1. Nada de `ReplaceFileW`, `CreateFileA`
ni `CopyFile2`.

- **Carga:** `FindFirstFileW save\<id>\*`; `GetFileAttributesW slotN_new` y `slotN_backup` (no existen); `CreateFile2`
  `GENERIC_READ`, `OPEN_EXISTING`, `FILE_FLAG_OVERLAPPED|READONLY` de `slotN\lobby.save` y luego `slotN\save.save`.
- **Autoguardado** (ranura 0, ya existente):
  1. `FindFirstFileW save\<id>\*`; `GetFileAttributesW slot0_new`, `slot0_backup`, `slot0`;
  2. `MoveFileExW slot0 -> slot0_backup` (`REPLACE_EXISTING|COPY_ALLOWED`); `FindFirstFileW slot0_backup\*.*`;
  3. `CreateDirectoryW slot0_new`; `CopyFileW` (que llama a `CopyFileExW`) de los otros ficheros desde el backup:
     **el origen va entero en minúsculas** (`c:\users\...\pearl abyss\cd\save\...`);
  4. `SetFileAttributesA` (→ `W`) `ARCHIVE`; `CreateFileW GENERIC_WRITE CREATE_ALWAYS slot0_new\save.save`;
  5. `MoveFileExW slot0_new -> slot0`; `MoveFileExW slot0_backup -> slot0_backup_<marca>_delete`;
  6. `FindFirstFileW`, `SetFileAttributesA`, `DeleteFileW` de su contenido y `RemoveDirectoryW` de esa carpeta.
- La primera vez (sin `slot0`) crea un `slot0_backup` vacío en vez de moverlo. El servidor del anfitrión lee (y, al
  salir el usuario 2, intenta escribir) las ranuras del usuario 2 **por las mismas API**.
- La carpeta real también tiene `user_engine_option_save.xml`, `initialize_input.save` y
  `save\<id>\steam_autocloud.vdf` (Steam Auto-Cloud sincroniza `save`).

## 2. Diseño [V]

**Toda la carpeta, copia nueva en cada arranque.** `src/mod/session2/save_hooks.cpp`; la lógica pura en
`src/common/save_guard.{h,cpp}` (`Mode::Redirect`, `redirect_path`, `Redirector`).

### 2.1 Al instalar (antes de crear ningún gancho, con las API normales)

- Caja: `%LOCALAPPDATA%\Pearl Abyss\CD\cdmp_save_sandbox`, **hermana** de `save` (fuera de la carpeta de Auto-Cloud).
- Si ya existe, pasa a `cdmp_save_sandbox_prev`; antes se borra el `_prev` anterior (solo ese árbol: el borrado se
  niega a tocar nada dentro o alrededor de `save`, y un enlace se quita sin seguirlo).
- Se copia **todo** `save` a una caja nueva: `CopyFileW` por fichero y las tres fechas (creación, acceso, escritura)
  de ficheros y carpetas con `SetFileTime`.
- **Si algo falla** (un fichero que no se puede leer, un enlace dentro de `save`, el renombrado): línea `ERR`, se
  borra la caja a medio hacer (el `_prev` sigue) y el guardián arranca en **bloqueo** (`save_guard=1`). Nunca se
  desvía a una copia parcial.

### 2.2 Mientras dura

- Cada API enganchada (`CreateFileW/A/2`, `DeleteFileW/A`, `MoveFileExW`, `MoveFileWithProgressW`,
  `CopyFileW/ExW/2`, `SetFileAttributesW/A`, `RemoveDirectoryW`, `CreateDirectoryW/A/ExW`, `ReplaceFileW` y las
  consultas `FindFirstFileW/ExW`, `GetFileAttributesW/ExW`) toma cada ruta, la resuelve (`GetFullPathNameW`) y, si
  está bajo `save`, llama al original con **la misma ruta bajo la caja**. Las demás pasan intactas.
- La caja no está bajo `save`: las llamadas internas (`CopyFileW → CopyFileExW`, `SetFileAttributesA → W`) ya ven la
  caja y pasan sin contarse dos veces. Las `A` con ruta bajo `save` se ensanchan y van al trampolín de la `W`.
- Coste fuera de `save`: el filtro barato de la traza (`maybe_save`: solo una ruta absoluta que nombre
  "pearl abyss", o una relativa, pasa a `GetFullPathNameW` en un búfer de pila); sin reservas de memoria.
- El `GetLastError` que recibe el juego es exactamente el del original (y el original ve el del juego a la entrada).
- Una ruta bajo `save` que no se pudiera reescribir (sin memoria) hace **fallar** la llamada
  (`ERROR_NOT_ENOUGH_MEMORY`): nunca llega a la carpeta real.
- Las consultas son obligatorias en este modo: sin ellas el juego vería la carpeta real y escribiría en la caja.

### 2.3 Contrato de `redirect_path(full_path, save_dir, sandbox_dir)`

Si `full_path` es `save_dir` o está dentro (sin mayúsculas, `/` o `\`, separadores repetidos, en un límite de
componente: `savegame` no es `save`), devuelve `[prefijo \\?\ o \\.\ de la entrada] + sandbox_dir (sin prefijo ni
separador final) + el resto de full_path tal cual` (mayúsculas, separadores, comodines). `save` mismo → raíz de la
caja. Si no, `nullopt`: otra carpeta, la propia caja, carpetas vacías o una caja dentro de `save` (o que la
contenga). Tests en `tests/optd_tests.cpp` (`test_save_redirect_path`) con las rutas de la traza, incluida la de
minúsculas.

### 2.4 Log

- Al arrancar: `Session2 save guard REDIRECT: everything under '<save>' goes to '<caja>', a copy made now (N files
  in M folders, B bytes; ...)`. Si falla: `Session2 save redirect: cannot ...` y `... falling back to block`.
- Una línea por escritura desviada, las 200 primeras: `Session2 save redirect #N <API> '<real>' -> '<caja>' -> <resultado>`
  (las lecturas y consultas solo se cuentan).
- En la línea periódica: `Session2: save guard REDIRECT (to '...cdmp_save_sandbox', writes W, reads R, queries Q) | ...`
  (`FAILED n` si alguna llamada falló por no poder reescribirse).

### 2.5 Tests sin juego

`tests/optd_hook_redirect_tests.cpp` (`optd_hooks_redirect`): los ganchos reales en modo redirect con
`%LOCALAPPDATA%` en `%TEMP%`. Reproduce el autoguardado de la traza con sus formas de ruta (minúsculas, `A`,
`CreateFile2`, `\\?\`, relativas, copias hacia y desde fuera), comprueba los códigos de error, la rotación
caja → `_prev`, las fechas copiadas, el tope de 200 líneas, y que la carpeta falsa queda **idéntica** (nombres,
tamaños, atributos, fechas de creación y escritura, contenido) leyéndola desde un proceso hijo sin ganchos. Otro
hijo comprueba la vuelta a bloqueo con un fichero bloqueado.

## 3. Qué mirar en la próxima prueba [H]

Antes de arrancar, apuntar las fechas de la carpeta real:
`Get-ChildItem -Recurse "$env:LOCALAPPDATA\Pearl Abyss\CD\save" | Select-Object FullName, Length, LastWriteTime`.

- Al arrancar: la línea `save guard REDIRECT ... a copy made now` y **ningún** `falling back to block`.
- **Ningún "Failed to save data"**: tras ESC (menú) y tras más de 16 min de juego.
- Líneas `Session2 save redirect #N MoveFileExW ... slot0 ... -> TRUE` en cada autoguardado; en la línea periódica
  `writes` crece y no aparece `FAILED`.
- Al cerrar el juego, la misma lista de la carpeta real: **mismas fechas y tamaños** que antes.
- `cdmp_save_sandbox\<id>\` tiene `slot0` (o la ranura que toque) con fecha de la sesión.
- Si entra y sale el usuario 2: líneas de su ranura (lectura al entrar, escritura al salir) con rutas de la caja.

## 4. Plan B

`save_guard=1` (el bloqueo de siempre: protege la carpeta, vuelve el aviso). `cdmp.p8-s12-shadow-seguro.ini` se queda
en 1. El mod también vuelve solo a 1 si la copia falla.

## 5. Límites conocidos

- **El progreso del anfitrión en una sesión P8 se queda en la caja.** El siguiente arranque copia otra vez la carpeta
  real (que no cambió) y la caja anterior solo se conserva una vez más, como `_prev`. Lo mismo para las opciones
  (`user_engine_option_save.xml`). Para quedarse algo, copiarlo a mano con el juego cerrado, sabiendo que la
  ranura de autoguardado la elige el gestor global y puede llevar datos del usuario 2 (`p8-sistemas-juego.md` §5.2).
  **S35 fase E:** con `[session2] host_save=export|probe` la caja y su `_prev` se exportan verificadas a
  `<bin64>\cdmp_save_exports\<generación>\` antes de rehacerla, y se restauran a mano con
  `scripts/host_save_restore.ps1` (`../s25/s35-fase-e-guardado.md`). Publicar en la carpeta real sigue bloqueado.
- Un fichero abierto antes de instalar no se desvía (no se vio ninguno: el mod instala antes de cargar partida).
- Como el bloqueo: solo las API de KernelBase de la lista; nombres cortos 8.3 (`PEARLA~1`), unidades `SUBST`,
  rutas `\??\`, enlaces hacia `save`, las variantes A de las consultas (`FindFirstFileA`, `GetFileAttributesA/ExA`),
  `CreateHardLink*`/`CreateSymbolicLink*`, las `*Transacted*` y `NtCreateFile` directo no se ven. La traza no mostró
  ninguna de ellas.
- **Revisión (sesión 24):** en `redirect`, toda API de la lista que KernelBase exporta es obligatoria (si una no se
  engancha, la opción D no arranca: un `CopyFileW` sin desviar escribiría la carpeta real). Una ruta relativa fuera
  de `save` se pasa ya resuelta, para que un `SetCurrentDirectory` de otro hilo no la convierta en una ruta de `save`
  entre la comprobación y la llamada. El test de los ganchos ya no compara la hora de escritura de las carpetas (NTFS
  la sella tarde y daba fallos falsos); compara entradas, horas de los ficheros y contenido.
- **Comprobado en un PC (11-10, 23:00):** un autoguardado de 16 operaciones fue entero a la caja (`slot0` nuevo en
  `cdmp_save_sandbox`), la carpeta real quedó como estaba y no salió ningún "Failed to save data". El autoguardado periódico
  (23:16:06, 16 min después de cargar) también fue a la caja (ranura rotada `slot2`), sin rechazos ni avisos.
- **En el invitado (Shadow) el guardián no se instala** (solo arranca con las etapas del anfitrión que escriben en el
  juego): la línea `save_guard=redirect` de sus `.ini` aún no hace nada y su juego guarda en su carpeta como hasta
  ahora.

## 6. Uso futuro: el progreso propio del invitado (ROADMAP punto 5)

Lo que el servidor del anfitrión escribe para el usuario 2 (al salir, o su autoguardado) cae en la caja en vez de
perderse con un error: es la base para guardar el progreso del invitado. Siguiente paso: localizar su ranura en
`cdmp_save_sandbox` tras una sesión con salida limpia, comprobar que es suya (y no la del anfitrión) y darle una
ranura propia (`p8-sistemas-juego.md` §5.3).
