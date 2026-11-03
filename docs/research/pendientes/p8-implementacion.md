# P8: implementación de S1/S2/S3 (invitado real)

Rama `opcion-d`. Commits: `05897a3` (S3), `4ace55b` (S1/S2), `ec33cd6` (fusión), `6276700` (arreglos tras
la prueba en el juego) y `6b0ae94` (arreglos de la revisión independiente). Build 1.0.0.2976.

Punto de partida: [p8-raiz-invitado.md](p8-raiz-invitado.md), con sus etiquetas [V] y [E]. Detalle técnico:
- [p8-impl-s3.md](p8-impl-s3.md): tabla de enganches, invariantes, costes y §12 "Correcciones tras la
  revisión".
- [p8-impl-s1s2.md](p8-impl-s1s2.md): tabla de enganches, líneas esperadas y §10.

Validación: [p8-validacion.md](p8-validacion.md).

## 1. Qué es cada pieza

Todas las claves van en `[session2]` y están **a 0 por defecto**. Con todas a 0, el anfitrión se comporta
igual que antes: la revisión lo comprobó en el código y la regresión de un PC lo confirma.

| Clave | Lado | Tipo | Qué hace |
|---|---|---|---|
| `observer_hold` | anfitrión | **arreglo (S3, publicación)** | Un NPC sin dueño con controlador de usuario (`+0x90`) y restauración 0 llega en el acto solo a su controlador. Al otro usuario se le anuncia con un 2005 nativo recién serializado cuando el servidor acepta su primera acción (restauración 1). Pasados 10 s se anuncia como haría el motor. |
| `guest_s3_diag` | invitado | diagnóstico | Historial por actor: 2005, restauración, primer Update, decisión 2723 (vf188(5)), 2991, acción instalada. |
| `guest_crash_dump` | invitado | diagnóstico | Ante un cierre: minidump local (como mucho 2) y los últimos registros en `cdmp.log`. El VEH solo apunta. |
| `guest_update_guard` | invitado | **CONTENCIÓN** | En `0x232D5C0` sale enseguida si el actor no se controla en local y su acción actual es nula. |
| `guest_layer_guard` | invitado | **CONTENCIÓN** | Guarda de capas existente (`0x1F63F70`/`0x2329700`) para actores no controlados en local. |
| `guest_actor_diag` | invitado | diagnóstico | Restauración (capas antes y **después**), lista de espera **entera** y **transiciones** de `[P+0x15C]`. |
| `load_diag` | ambos | diagnóstico | S2: dos predicados de `0x11`, 2322, rama de dueño `0x2BC8470`, emisión `0x287F1CC` y UI ExtraReward `0xE919C0`. |
| `s1_diag` | ambos | diagnóstico | S1: búsqueda de acciones `0x24314C0` (en línea, prefiltro por los dos hashes), `0x86392A`, BodyAnimation `0xA79770`/`0xA79BAC`/`0xA79F4C` y binding `0x9ED431`/`0x9ED47F`. Solo para sesiones cortas. |

Todos los enganches están en `.rsrc`, con los bytes completos verificados contra el PE
(SHA-256 `57DA440D…E1E7`) y con fronteras de instrucción y destinos de salto comprobados. Si algo no
cuadra, se rechazan y se conserva la ruta original. **Nada en `.xtls`.**

## 2. Decisiones y su evidencia

- **S3, criterio de liberación.** La `StartActionReq` que acepta el servidor es el disparador, igual que
  en D-53r. [V]
  - En la captura del cliente del anfitrión, los 956 NPC con restauración 0 reciben su 2223.
  - En vivo hoy, con `cdmp_peer`: 730 retenidos y 730 liberados con restauración 1, en una media de
    214 ms (máximo 375 ms) y sin vencimientos.
- **Corrección de una conclusión mía de esta tarde.**
  - La primera pasada sugería que algunas criaturas de tipo 6 no llegaban nunca a restauración 1.
  - Con el barrido dirigido de la revisión, ninguna venció. Aquel resultado salió del barrido defectuoso
    (fallos B1 e I1), no del motor. [V]
  - El vencimiento con anuncio nativo se queda como red de seguridad.
- **S2, rama de dueño confirmada en vivo.** [V]
  - El viaje del usuario 2 al campo del anfitrión toma la rama de no dueño en `0x2BC8470` y no emite 2322.
  - En su campo propio sí lo emite.
  - En una partida de un jugador (invitado local, modo observe), cada viaje recibe su 2322, `M+0x231`
    pasa a 2 y `0x11` sale en 0–0,5 s. Es la línea base nativa.
- **S2 sin arreglo todavía, a propósito.** No se envía 2322 ni se fuerza `M+0x231`: el informe y el
  encargo lo descartan.
  - Falta ver en el invitado real qué predicado bloquea: `M+0x231 < 2` o una dependencia de
    `0xA97D60`. Ahora se registra en el momento y con la dependencia concreta.
- **S1 sin arreglo todavía, a propósito.** Falta la primera divergencia entre el NPC del anfitrión y el
  del invitado. `s1_diag` en los dos lados la mide: búsqueda en el gráfico dinámico de 2 entradas,
  instalación en `CC+0x70/+0x78` y binding.
- **Contenciones.** `guest_update_guard` y `guest_layer_guard` solo evitan el acceso inválido conocido;
  no instalan acciones. Su registro distingue las claves que luego reciben acción (recuperadas) de las
  atascadas.

## 3. Archivos

- **Anfitrión:** `src/mod/session2/child_hold.{h,cpp}` (D-53 + `observer_hold`) y
  `src/mod/game/safe_memory.{h,cpp}` (`fast_read`).
- **Invitado:** `src/mod/session2/guest_s3.{h,cpp}`, `guest_s3_logic.h`,
  `guest_actor_diag.{h,cpp}`, `guest_actor_diag_logic.h`, `guest.{h,cpp}` y
  `src/mod/net/remote_puppet.{h,cpp}`.
- **Ambos lados:** `src/mod/session2/p8_probes.{h,cpp}` y `p8_probe_logic.h`.
- **Configuración y cableado:** `src/mod/config.{h,cpp}`, `src/mod/mod.cpp`, `session2.*`,
  `guest_lifecycle.*`, `troc_sniffer.*` y `CMakeLists.txt`.
- **Pruebas:** `tests/optd_s3_tests.cpp`, `optd_p8_probe_tests.cpp` y `optd_guest_actor_tests.cpp`, con
  datos en `tests/data/p8s4_2005_*.bin`.
- **Ini:**
  - `cdmp.p8-host-full.ini`: anfitrión, todo.
  - `cdmp.p8-shadow-full.ini`: Shadow, todo.
  - `cdmp.p8-local.ini`: un PC, anfitrión con peer.
  - `cdmp.p8-guest-local.ini`: un PC, lado invitado sin red.
  - `cdmp.p8-shadow-s3.ini` y `cdmp.p8-shadow-s12.ini`: parciales.

## 4. Activar, desactivar y revertir

- **Activar:** copiar el ini correspondiente como `bin64\cdmp.ini`.
- **Desactivar:** poner la clave a 0 o usar el ini ligero `cdmp.a2.ini`.
- **Revertir:** reconstruir desde `6f5de7f`, el commit anterior a esta serie. Cada clave es
  independiente; no hay datos persistentes que limpiar.
- **Cierres capturados:** los `.dmp` quedan junto a `cdmp.asi` (se conservan los 2 más nuevos) y se
  pueden borrar.

## 5. Artefacto

`build\bin\RelWithDebInfo\cdmp.asi`, SHA-256
`27975C23E164BEDA174B724347A31074405B54C390381BEA742CF8AD1B160FDD`. Es el mismo en `bin64` y en el kit
`dist\cdmp-p8-shadow.zip`. Anfitrión e invitado usan el mismo `cdmp.asi`.

## 6. Límites

- **S1 y S2 no están arreglados:** esta versión solo los mide.
- **S3:** `observer_hold` cierra la ventana de publicación, y en el invitado se mide si la primera acción
  llega a instalarse.
- **Las contenciones siguen activas en el kit de Shadow.**
- **Lo que la contención no cubre:** otros consumidores de una acción nula (`0x1831598`, `0x17F328F`,
  `0x88F832`). Si cierran, la captura local lo dirá.
- **Coste:** medido en el anfitrión, 10 µs de media y 276 µs de máximo por pasada, y 581 µs como máximo
  por reparto. En el invitado real no está medido.
