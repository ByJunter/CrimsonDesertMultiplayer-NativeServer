# Crimson Desert Multiplayer — guía para Claude

Mod cooperativo para Crimson Desert (C++20, MSVC x64). Lee `README.md` y
`docs/ROADMAP.md` para saber el estado actual.

## Contexto que no está en el código
- Documentación e investigación en español.
- **Pruebas con dos jugadores:**
  - Lo normal es desarrollar y hacer la primera prueba en un PC contra `cdmp_peer.exe` por loopback
    (rápido, un solo log con los dos lados).
  - Las pruebas de dos jugadores reales usan un segundo PC (con otra cuenta de Steam que tenga el juego)
    como **invitado** y el PC principal como **anfitrión**, conectados por Steam (`docs/PRUEBA-DOS-PC.md`).
    El invitado recibe el mod y devuelve sus logs por una carpeta compartida.
  - En un mismo PC **sí** se pueden ejecutar dos copias del juego (comprobado con la build 2976, hasta la
    pantalla de título, con la misma cuenta de Steam). El `.exe` lanzado a mano se relanza a través de Steam,
    que rechaza una segunda copia; la segunda se lanza como lo hace Steam: variables de entorno
    `SteamAppId=3321460` y `SteamGameId=3321460`, y el argumento `PlatformServiceType=Steam`. Steam tiene que
    estar abierto (`scripts/launch-2inst.ps1`).
  - Para usarlo con el mod, cada copia necesita su propio papel, ini y log. Las dos comparten
    `%LOCALAPPDATA%\Pearl Abyss\CD\save`: separa las partidas del invitado y haz copia de seguridad antes de
    cargar ninguna partida. La prueba entre dos PC sigue siendo la final (red real, túnel por Steam, cargas
    a distinta velocidad).
- Juego: `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`,
  versión 1.0.0.2976, PE timestamp `0x6AB28F00`, base fija `0x140000000`. Cualquier
  offset o firma vale solo para esta build; anota la build junto a cada hallazgo.
- Las partidas están en `%LOCALAPPDATA%\Pearl Abyss\CD\save`. Haz una copia en
  `backups/` antes de cualquier prueba que **escriba** en la memoria del juego.
- `reference/` (fuera del repositorio) puede contener proyectos de terceros sin licencia, como
  CrimsonDesertCoop: úsalos solo como referencia y no copies su código. Su análisis está en
  `docs/ANALISIS-CrimsonDesertCoop.md`.

## Comandos
- Compilar y pasar tests: `.\scripts\build.ps1` (busca solo las VS Build Tools; cmake no está en el PATH).
- Instalar en el juego: `.\scripts\deploy.ps1`; desinstalar con `-Remove`. Falla si el juego está abierto.
- Prueba completa sin juego: arranca `cdmp_peer.exe` y luego `cdmp_host.exe <ruta\cdmp.asi> <segundos>`.
  `LoadLibrary` falla con rutas largas (error 206), así que usa `build\e2e\`.
- Prueba en el juego: arranca `cdmp_peer`, lanza `steam://rungameid/3321460` y lee `bin64\cdmp.log`.
  El juego ignora WM_CLOSE: si está en la pantalla de título, ciérralo con `Stop-Process`.

## Reglas de ingeniería
- Nada de inline hooks en direcciones que estén a mitad de una función. Para puntos
  de inyección de Cheat Engine usa `safetyhook::create_mid`.
- Cada lectura de memoria del juego tiene que estar protegida (`VirtualQuery`/SEH).
  Un crash del juego se envía a Pearl Abyss (sentry/crashpad).
- Nunca identifiques entidades entre PCs por su dirección de memoria.
- No hagas trabajo dentro de `DllMain`: todo va en el hilo worker.
- El loader (`winmm.dll`) también carga en `crashpad_handler.exe`; `dllmain.cpp` filtra por el exe.
- Overlay: solo **hooks inline** (safetyhook) sobre funciones, nunca parches de
  vtable, porque Streamline llama a Present directamente. Instálalo **diferido**
  (tras `WorldSystem` y con la ventana visible): instalarlo al arrancar colgó el juego.
  Antes de probar en el juego, prueba con `cdmp_dx12_test.exe`.
- Para verificar visualmente, haz una captura con `System.Drawing CopyFromScreen`
  (pantalla 2560×1440) y recorta la zona de interés.
- El protocolo se serializa campo a campo (`protocol.cpp`); si cambia el formato, sube `kVersion`.
