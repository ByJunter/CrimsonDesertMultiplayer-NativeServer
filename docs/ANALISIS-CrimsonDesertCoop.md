# Análisis: blizz3010/CrimsonDesertCoop

Fuente: https://github.com/blizz3010/CrimsonDesertCoop (clonado en `reference/CrimsonDesertCoop`)
Fecha del análisis: 2026-10-03 · Último commit del repo: 2026-05-22 (`c0a8938`), versión 0.2.3

## Qué es

Un mod ASI (DLL que carga Ultimate ASI Loader) en C++20 para Crimson Desert que intenta
añadir cooperativo de 2 jugadores:

- **Transporte:** Steam P2P (`ISteamNetworkingSockets`) + lobby de amigos de Steam para invitar.
- **Modelo:** el host es la autoridad. El jugador 2 aparece en el mundo del host
  "secuestrando" a un compañero NPC (Oongka/Yann/Naira): le quita la IA y escribe
  su posición en memoria.
- **Hooks:** safetyhook (inline + mid-hooks) sobre firmas AOB sacadas de cheat tables
  y de otros mods de la comunidad.
- **UI:** overlay ImGui con hook a DX12 `Present`. F7 = hostear, F8 = mostrar/ocultar overlay.
- **Dependencias** (las descarga CMake): safetyhook 0.4.1, spdlog, nlohmann/json, imgui.
  El Steamworks SDK hay que ponerlo a mano.

Tamaño: unas 8.100 líneas, 38 archivos. No tiene tests (la opción `CDCOOP_BUILD_TESTS`
apunta a una carpeta `tests/` que no existe) ni CI.

## Origen y fiabilidad

- La mayoría de los commits los escribió un agente de IA ("Claude") y se fusionaron
  vía PR. No hay pruebas de que se haya ejecutado nunca dentro del juego: ni capturas,
  ni logs, ni issues de usuarios confirmando que funcione.
- Los offsets que el README marca como **"Verified"** vienen de cheat tables y mods
  de terceros (bbfox0703, Orcax, EquipHide, CDAnimCancel…). Son "verificados" por
  esos autores para *sus* usos, no probados dentro de este mod.
- **Licencia:** el repo no tiene archivo LICENSE; solo dice "for educational and
  personal use". Legalmente **no** debemos copiar su código en nuestro proyecto. Sí
  se puede usar como referencia y aprovechar los enlaces a investigación pública.

## Problemas críticos encontrados (leyendo el código)

### 1. Los hooks inline están puestos en mitad de funciones → crash muy probable
`hooks.h:44-57` siempre crea los hooks por firma con `safetyhook::create_inline`. Pero
las firmas `POSITION_*`, `DAMAGE_SLOT_PRIMARY`, `STAT_WRITE_*` y `CAMERA_ZOOM_FOV`
(`game_structures.h:656-680`) son puntos de inyección de Cheat Engine en **mitad de
una función**, no entradas de función. Además, las constantes `*_OFFSET` existen pero
nunca se usan.

Los detours tienen forma de función normal (`player_position_detour(void*, float, float, float)`,
`hooks.cpp:595`) y llaman a `hook.call<>()` como si llamaran a la función original.
En un punto de mitad de función eso rompe la pila y los registros. Como estos hooks
se instalan **por defecto**, lo más probable es que el juego crashee la primera vez
que pase por ese código (por ejemplo, al moverse). El propio comentario del código
reconoce que el sitio es "mid-function". Solo los hooks añadidos después
(dragón, teletransporte, montura) usan correctamente `create_mid_hook`.

### 2. La sesión nunca llega a conectarse
`Session::join_session` (`session.cpp:~100`) envía el `HANDSHAKE` justo después de
`ConnectP2P`. Pero `Session::send` exige que `t->is_connected()` sea verdadero, y
`SteamNetworkTransport::send` exige `impl_->connected`. Ese valor solo se pone a
`true` en el callback `Connected`, que llega más tarde, dentro de `RunCallbacks()`.
Resultado: el handshake se descarta sin avisar y no hay ningún reintento.

- El host se queda en `HOSTING` y el cliente en `CONNECTING`.
- `is_active()` solo devuelve verdadero en `CONNECTED` (`session.h:79`).
- Por tanto **ningún sistema de sincronización llega a ejecutarse**, el compañero
  nunca se secuestra y no se aplica el escalado de enemigos.

### 3. La sincronización es de un solo sentido
Solo el **host** secuestra un compañero, y lo hace al recibir el handshake. El cliente
recibe la posición del host pero `CompanionHijack` no está activo en su lado, así que
**el cliente nunca ve al host**. Cada jugador sigue en su propio mundo de un jugador.

### 4. El "secuestro de compañero" probablemente apunta a la entidad equivocada
Las ranuras `+0xD0..+0x108` del actor del jugador ("body slots") vienen de EquipHide,
un mod que oculta **equipamiento**, y se resuelven como `ChildActor`. Lo más probable
es que contengan armas, armadura o accesorios, no a los compañeros. "Desactivar la IA"
consiste en poner a 0 un puntero en `+0x48` (`companion_hijack.cpp`), lo cual
seguramente rompe o crashea la entidad en vez de desactivar su IA de forma limpia.

### 5. La sincronización de enemigos no puede funcionar
- `EnemySync` recorre `actor_manager_ptr + 0xD0..0x108` (`enemy_sync.cpp`). Son los
  offsets de las ranuras del **actor del jugador**, aplicados al **ActorManager**: no
  enumeran enemigos, y como mucho serían 8.
- El ID de cada enemigo es `puntero & 0xFFFFFFFF`. Las direcciones son distintas en
  cada PC, así que el cliente nunca encontrará al mismo enemigo. Los mensajes de daño
  y de muerte nunca coinciden con nada.

### 6. Otros
- `on_remote_full_state` hace `memcpy` del paquete entero (cabecera incluida) en el
  buffer. Funciona porque el buffer tiene el mismo tipo de paquete, pero la cabecera
  queda mezclada en el estado interpolado.
- `read_mem`/`write_mem` solo comprueban el rango del puntero, no que la memoria sea
  legible. Si un puntero queda obsoleto (tras una pantalla de carga) → access violation.
- La sincronización de animaciones, misiones, cinemáticas, interacción con el mundo y
  el viaje rápido en el receptor son stubs o solo escriben en el log (el README lo admite).
- La firma `ANIM_EVALUATOR` usa `create_hook` inline con un detour `(void* evaluator)`.
  Si ese AOB tampoco es una entrada de función, tiene el mismo riesgo que el punto 1.
- Es posible que el overlay de Steam y el `SetRichPresence` del mod choquen con la
  presencia propia del juego.

## Qué se puede aprovechar

1. **Mapa de investigación pública:** las listas de proyectos relacionados del README
   son lo más valioso: Orcax player-status-modifier, CDGuardCancel (formato `.paac`
   de animaciones), CrimsonDesertTools/EquipHide (cadena WorldSystem → ActorManager),
   bbfox0703 CT, CD Companion (teletransporte por delta de física, XYZ estático,
   heading de cámara), desempaquetadores PAZ y pycrimson.
2. **Cadenas de punteros documentadas** (para comprobarlas nosotros con Cheat Engine/ReClass):
   - Jugador: `WorldSystem → +0x30 ActorManager → +0x28 UserActor`
   - Posición: `actor → +0x40 → +0x08 → core → +0x248 → +0x90` (float XYZ), cuaternión en `+0xA0`
   - Stats: componente en `actor+0x58`, `StatEntry` int64 ×1000
3. **Ideas de arquitectura correctas en lo conceptual:** host autoritativo, Steam P2P
   con lobby de amigos, posición por canal no fiable a 30 Hz y eventos por canal fiable,
   un archivo de marcadores de arranque (`cdcoop_loaded.txt`) para diagnosticar, y un
   hilo de input independiente del overlay.
4. `tools/offset_scanner.py` y `docs/REVERSE_ENGINEERING.md` como guía de flujo de trabajo.

## Conclusión

Es un **andamiaje con buena documentación, no un mod funcional**. Tal como está, lo más
probable es que crashee al instalar los hooks por defecto (punto 1), y aunque no
crashee, nunca llega a completar una conexión (punto 2). Lo que realmente aporta es la
recopilación de offsets e investigación de la comunidad.

### Recomendación para nuestro proyecto
- **No** hacer fork como base de código (licencia y defectos de diseño). Empezar de cero
  usando este repo y sus fuentes como referencia.
- Primer hito realista, en este orden:
  1. Un ASI mínimo que cargue y escriba un log.
  2. Leer la posición del jugador local con la cadena documentada, verificándola en el juego.
  3. Una conexión P2P real entre dos PCs con handshake y reintentos.
  4. Mostrar al otro jugador como un marcador del overlay (sin tocar entidades del juego).

  Solo después investigar cómo spawnear o poseer una entidad de forma segura,
  usando mid-hooks y verificándolo todo con un depurador.
