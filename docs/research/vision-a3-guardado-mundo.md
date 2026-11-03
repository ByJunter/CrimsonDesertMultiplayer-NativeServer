# Visión A3: guardado del mundo y de sus jugadores

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`; direcciones RVA. Investigación estática y documental: no se ejecuta el juego ni se leen partidas completas. **Informe y verificación independiente terminados; diez contrastes en §8**.

Leyenda: [V] captura/observación histórica; [E] `.rsrc`; [E-x] `.xtls` (incluido cuerpo legible); [S] hipótesis o diseño; [R] fuente externa no contrastada. Los componentes alojados en un usuario no son automáticamente estado privado: su semántica puede describir el mundo.

**Actualización transversal de las pruebas:** [S, D-28] antes de la parte de E1 que provoca un guardado rechazado y de cualquier ensayo deliberado de blob inválido, disco lleno, corte de guardado o crash, debe completarse **RPT-0** de [A7 §4.3](vision-a7-transversal.md). P6 bloquea escrituras, pero no aísla lecturas ni adjuntos de reportes. [E/E-x] La ruta `0x2355700 → 0xEF51850 → 0x2356270 → 0x1317D40` puede solicitar adjuntos de la ranura fallida al SDK de Sentry, bajo condiciones; no se observó transmisión. Esta precondición completa todas las secuencias de pruebas que aparecen después, sin ejecutar ni habilitar ninguna.

## Veredicto

[S] Es razonable conservar el serializador nativo y sus blobs cifrados para un prototipo, pero separar de verdad mundo/jugador exige además una política por componente y una transacción global; redirigir archivos por SteamID no basta. [E] El almacén compartido indexa por nombre `slotN` (`0x20688A0`, `0x2069640`, `0x2354FA0`), y el autoguardado selecciona ranura con estado global [E-x] (`0xCBD84D0`, `0xCBDE990`). Dos usuarios sobre una misma ranura pueden intercambiar datos en memoria incluso si P6 impide escribir en disco. Fuentes: `opcion-d-p9.md` §§1,5; `opcion-d-e3-riesgos.md` §§1,3–4.

[S] Propuesta prudente para A4: una ranura candidata arrendada por sesión activa, remapeo íntegro a raíz propia y directorio externo por SteamID; más usuarios registrados permanecen en disco, sin reservar actores. Reutilizar una ranura exige demostrar invalidación completa de su caché. La serialización nativa conserva la barrera TLS `+0x4460`; actualmente no existe base para prometer guardados sin pausa. Prohibidas 3 (CrossSave) y 206 (ruta condicional de borrado tras cargar); no se toca el byte un jugador `0x6CEEDE8`.

## Fuentes de partida

`CLAUDE.md`; `OPCION-D-EMULADOR.md`; `opcion-d-diario.md`; D0/login/E3/campo compartido/P8/P9; `segunda-sesion.md`; `hora-del-dia.md`; `pausa-copia-congelada.md`. Volcados históricos: `C:\Emulador CD\build\research\optd\e3\` y `optd\p9\`; resultados nuevos bajo `build\research\vision\a3\`.

## 1. Formato, criptografía y posibilidad de editar fuera del juego

### 1.1 Lo comprobado y lo que permanece externo

[E] `0x2357DA0` lee una cabecera de **0x80 bytes**; valida `SAVE`, versión 1–2, tamaño de cabecera, flags, y longitudes. La copia a `fileObj+0x98`. `0x235C510` llama al descifrador/verificador `0x235C2E0`, y solo si vuelve sin error descomprime mediante `0x12C61D0` cuando está activa la **máscara `0x2` (bit 1, numerado desde cero)**. Las instrucciones `0x235C576`, `0x235C583`, `0x235C602` y `0x235C60F` corresponden a flags, tamaño plano, tamaño comprimido y descompresión. El plano termina en `fileObj+0x120`. Volcados nuevos `vision\a3\0x2357da0.asm`, `0x235c510.asm`; P9 §1.2.

| Offset de fichero | Significado y evidencia | Confianza |
|---|---|---|
| `+0x00`, 4 B | magic `SAVE`; constante `0x5547B6C`, validador `0x2357DA0` | [E] |
| `+0x04`, u16 | versión 1–2 aceptada en esta build | [E] |
| `+0x06`, u16 | tamaño de cabecera `0x80` | [E] |
| `+0x08`, u32; `+0x10`, u16 | campos no identificados; cero en cabecera histórica de P9 | [V histórica], semántica desconocida |
| `+0x0C`, u32 | flags; solo se acepta la máscara `0x2` (bit 1), usada para compresión | [E] |
| `+0x12`, u32 | tamaño descomprimido, no nulo | [E] |
| `+0x16`, u32 | tamaño de carga cifrada/comprimida, no nulo y no mayor que el plano | [E] |
| `+0x1A`, 16 B | nonce según editor público | [R] |
| `+0x2A`, 32 B | autenticador HMAC-SHA256 según editor público | [R] |
| `+0x4A..0x7F` | resto reservado según editor; no asumir que siempre sea cero | [R/S] |

[V histórica] P9 §1.2 leyó **solo los primeros 64 B** de `slot0/save.save`: versión 2, plano de 4.203.220 B y carga de 1.182.718 B; con cabecera, 1.182.846 B. No se ha vuelto a leer ni descifrar ninguna partida en esta investigación. Es un ejemplo de tamaño, no un presupuesto máximo.

[E] `0x235C2E0` salta a `.xtls 0xEF713F0`; `0x235C090` salta a `.xtls 0xEF6EA10`. Esos núcleos siguen opacos: **ChaCha20 y HMAC no quedan demostrados por este desensamblado**. [R] El [editor público, módulo ben_save_decrypt.py](https://github.com/NattKh/CRIMSON-DESERT-SAVE-EDITOR-AND-GAME-MODS/blob/main/CrimsonSaveEditor/ben_save_decrypt.py), consultado el 2026-10-08, describe ChaCha20, HMAC-SHA256 sobre el comprimido, LZ4 y claves según versión. No se ha copiado, instalado ni ejecutado ese módulo; tampoco se reproducen sus constantes. El orden externo coincide con la envoltura legible, pero falta una prueba independiente de ida y vuelta en 2976.

[E] La escritura legible `0x235C7A0` llama a compresión `0x12C6150`, al núcleo de cifrado y forma cabecera+carga. El nombre LZ4 procede del análisis previo P9; las llamadas y límites están reextraídos aquí. Riesgo: aceptar una cabecera válida sin verificar el autenticador no demuestra integridad. Prueba futura: en copias aisladas y con autorización de pruebas, comparar semántica tras guardar/cargar y rechazar versiones/tamaños no conocidos.

### 1.2 `lobby.save` y `save.save` no equivalen a mundo y jugador

[E] `0x206897C` elige `lobby` para el tipo de actor cuyo descriptor tiene `+1==0`, y `save` para el personaje. `0x186E450` llama dos veces a `0x2069640`: en **`0x186E606` con `[pj+0xA0]`**, y en **`0x186E678` con `pj`**. El primero conserva estado de usuario/lobby, el segundo el conjunto de componentes del personaje. Fuentes: volcados de `0x20688A0`/`0x186E450`; P9 §§1 y 5.

[S] Por tanto, renombrar `lobby.save` a «mundo» y `save.save` a «jugador» sería incorrecto. Los datos de campo, vivienda, misiones y hora están mezclados con progreso personal dentro de los componentes del personaje (§2). Debe conservarse la pareja como unidad de publicación.

[E] Cada `0x2069640` llama por separado a `0x2355700` en `0x2069CE3`. **No equivale a dos commits efectivos:** `0x2355700` es un thunk hacia `.xtls 0xEF51850` [E-x]; su cuerpo comprueba `ranura+0x69C/+0x699/+0x698` en `0xEF5185A..81` y puede devolver éxito en `0xEF51B2A..39` sin recorrer las operaciones virtuales de escritura. `_new/_backup/_delete` de `0x23571D0` protegen operaciones de fichero; **no se ha demostrado atomicidad de la pareja lobby+save**, menos aún de mundo+10 jugadores. [S] Si ambas llamadas publican sucesivamente, un corte entre ellas puede dejar generaciones distintas. La transacción debe observar pendientes y generación completa, sin interpretar dos retornos de éxito como durabilidad. Prueba futura: interrumpir en una carpeta de laboratorio tras cada fase y verificar recuperación del último manifiesto completo. Contraste independiente: §8, afirmación 6.

### 1.3 Vía propia para lectura/escritura externa

[S] Primera vía, de menor dependencia criptográfica: conservar blobs completos y hacer que el motor legítimamente instalado los cargue/guarde bajo una raíz exclusiva del servidor; ninguna reimplementación del cifrado es necesaria para esta fase. La separación semántica sigue pendiente: la ruta propia no la aporta por sí sola. Fuente: `0x186F570`, `0x186E450`, §2–4.

[S] Segunda vía, si la separación exige manipular el archivo: especificación propia a partir de observación autorizada y del esquema de reflexión del exe; implementación original del archivo, conservación de campos desconocidos, bibliotecas con licencia adecuada para primitivas estándar y corpus de pruebas generado por el propietario. No derivar una clave fija de [R] ni asumirla universal sin contrastarla; no copiar código sin licencia ni distribuir blobs, claves o recursos del juego. Esta es una propuesta técnica, **no una conclusión jurídica de que el contrato o la legislación permitan todo el flujo**; la evaluación externa corresponde a A7.

[E/S] La ruta visible entrega `S+0x78` al núcleo, pero no demuestra que no haya otras dependencias dentro de la VM. CrossSave tampoco descarta por lógica una clave ligada a otra cuenta. Se matiza P9 §1.3: independencia de SteamID es **hipótesis apoyada**, no hecho cerrado. G3a/G3b deben comparar carga de blobs de dos cuentas en directorios aislados, además de observar `S+0x78`.

## 2. Inventario de componentes y separación semántica

### 2.1 Recorrido real y alcance de la tabla

[E] El recorrido es **por actor**, no exclusivamente por usuario: `0x2069018` obtiene `[actor+0x78]`, `0x206901C` el recuento de entradas `[actor+0x80]`, y avanza entradas de 16 B. `vf(+0xD8)` da el nombre; el cargador llama `vf(+0x50)` en `0x20693E8`. Al guardar, `0x20696A6..B8` usa el mismo vector y **`vf(+0xD0)` en `0x20697E1`** produce objetos reflejados que se agregan y serializan. No existe aquí un `if componente==mundo`.

[E] `inventario.py` extrajo las vtables completas de RTTI `Server*ActorComponent` y `Common*ActorComponent`: **47 de servidor y 53 comunes**. Hay **18 componentes de servidor con carga diferente del stub `0x35EA90`**; `Navigation` tiene nombre pero carga y guardado stub. Catálogo completo con las 100 entradas, vtable, tres vf y nombre: [vision-a3-componentes.csv](data/vision-a3-componentes.csv). El recorrido de referencias directas hasta profundidad 2 y sus límites `.xtls` está en [vision-a3-recorrido.json](data/vision-a3-recorrido.json).

**Límite de exhaustividad:** [E/S] este es el catálogo estático completo para ese patrón RTTI, no una medición de cuáles de los 100 objetos están instalados en el vector de cada actor vivo. No se puede afirmar la membresía concreta del usuario técnico, del jugador o de cada hijo sin observar su descriptor/vector. Tampoco están identificados todos los campos serializados de cada componente mixto. El siguiente cuadro distingue los 18 puntos de entrada persistentes y explicita la política propuesta; las lagunas son trabajo de investigación, no campos que puedan omitirse al guardar.

La columna «partición» es **[S] diseño**; nombres/vtables/vf son **[E] lectura de tablas**. Una flecha a `.xtls` significa que el cuerpo debe tratarse [E-x] si llega a leerse, **no** que esté permitido enganchar el thunk.

| Componente / nombre | Vtable RVA | Carga `vf50` | Guardado `vfD0` | Partición propuesta y evidencia de contenido; riesgo/prueba |
|---|---|---|---|---|
| Faction / `faction` | `0x5B16BE0` | `0x2869790 → .xtls 0x10290F10` | `0x28698E0 → .xtls 0x10293250` | **Mixto**: reputación personal frente a dominio/operaciones del mundo. Cuerpos no certificados aquí. Riesgo: conquista o recompensa se repite; probar cambio por A y recarga de B. |
| Friendly / `friendly` | `0x5B16D90` | `0x286A990` | `0x286B040` | **Jugador** con referencias al NPC del mundo: `Friendly*SaveData`, `ExperienceLevelSaveData`, `ShareKnowledgeSaveData` en recorrido. Riesgo: contador diario y afinidad ajenos; comparar dos relaciones distintas. |
| ContentsMisc / `contentsetc` | `0x5B1D318` | `0x28B7360` | `0x28BB540` | **Mixto esencial**, véase §2.2. Riesgo máximo de sobrescribir mundo; matriz de gimmick/vivienda/evento/historial. |
| GameEventHandler / `gameevent` | `0x5B1EBD0` | `0x28FE9C0 → .xtls 0x103C78C0` | `0x28FEBE0` | **Mixto**: `GameEventSaveData` en salida. Distinguir eventos personales y efectos globales caso a caso; registrar evento disparado por un solo jugador. |
| QuestDialog / `questdialog` | `0x5B1F730` | `0x29042F0` | `0x2904670` | **Jugador**, con efectos de mundo restringidos: `ExecutedQuestDialogSaveData`, `QuestDialogInterrogationSaveData`. Probar diálogo completado A, pendiente B. |
| Quest / `quest` | `0x5B1F8F8` | `0x2908090 → 0x290DFA0` | `0x29080D0 → 0x290F1F0` | **Mixto**: RTTI de `QuestStateData`, `MissionStateData`, `StageStateData`, `QuestGaugeStateData_FactionOperation`; strings `_stageList` y `_factionOperationList`. Una misión personal puede mutar el mundo. Probar misión que cambia NPC/stage. |
| Status / `characterstatus` | `0x5B203F0` | `0x293FE70 → .xtls 0x10427500` | `0x1791DA0 → .xtls 0xC73A550` | **Jugador** para stats/identidad aparente; detalle del cuerpo pendiente. No asumir qué subcampo aloja apariencia. Probar stats distintos tras logout/reinicio. |
| Wanted / `wanted` | `0x5B20750` | `0x25D6B10 → .xtls 0xFAFC600` | `0x25D6C40` | **Mixto**: búsqueda/reacción del mundo y delito personal. Semántica interna pendiente. Cometer delito con A y observar B/recarga. |
| EquipSlot / `equipment` | `0x5B29178` | `0x20C24E0 → .xtls 0xE23DAD0` | `0x20C27C0` | **Jugador**: `EquipmentSaveData`, `ItemSaveData`, `ItemDyeSaveData`, sockets. Riesgo de referencias itemNo; cambiar equipo y validar ambas partidas. |
| Inventory / `inventory` | `0x5B2B760` | `0x212A8D0` | `0x212CDB0` | **Jugador**, inventario/banco/housing portátil; RTTI `InventoryHousingGimmickSaveData` no convierte la vivienda colocada en inventario. Riesgo de duplicación al intercambiar; checkpoint conjunto y reinicio. |
| Knowledge / `knowledge` | `0x5B2BA70` | `0x2B73CC0` | `0x213DB20` | **Jugador**: `KnowledgeElementSaveData`, `SkillPointSaveData`, `SkillLearnElementSaveData`. Riesgo de desbloqueo que presupone mundo antiguo; probar conocimientos A/B diferentes. |
| Lobby / `lobby` | `0x5B2BE58` | `0x2B7F4D0` | `0x2B7F850` | **Jugador/usuario**: `LobbySaveData`; selección de personaje. Riesgo de título/caché ajena; salir y reconectar A/B con identidad distinta. No es el archivo del mundo. |
| MercenaryClan / `mercenaryclan` | `0x5B2C6C8` | `0x2146F30` | `0x2148CF0` | **Mixto**: `MercenarySaveData`, `ShipStationSaveData`, cooldowns, equipo, personalización. Campamento compartido exige política específica; probar mercenario/estación y recarga de no dueño. |
| TransformSync / `transform` | `0x5B2CF60` | `0x2BC2970` | `0x2BC2280` | **Mixto**: posición de jugador y tabla de hora por nivel `+0x5D0`; `TransformSaveData/TransformFieldSaveData`. Hora autoritativa del mundo, ubicación por jugador. Probar login de partida antigua sin salto de hora. |
| Sequencer / `sequencer` | `0x5B2D7C0` | `0x2BE7890 → .xtls 0x107835C0` | `0x2BE7790 → .xtls 0x10782780` | **Mixto**: stages/eventos comunes y participación individual; consultar A1/A5 y `pendiente-jefes.md`. No guardar ciegamente un combate a medias; reinicio a estado seguro. |
| Skill / `skill` | `0x5B2E5A8` | `0x2BED910` | `0x2BEDDB0` | **Jugador**: `ActiveSkillSaveData`, `SkillUnlockMarkSaveData`, `RemainTimeBuffSaveData`. Riesgo de buff con reloj de otra época; logout y expiración durante desconexión. |
| Store / `store` | `0x5B2ECA8` | `0x2BFE260` | `0x2BFD8D0` | **Mixto**: `StoreStockHistorySaveData`, `StoreSoldGimmickSaveData`, `_itemSoldFieldTimeRawList`, recompra. Definir stock mundial y recompra personal; dos compras concurrentes y reinicio. |
| SubLevel / `sublevel` | `0x5B2F378` | `0x2C0F650` | `0x2C0F190` | **Mixto por resolver**: `SubLevelElementSaveData`, experiencia/donaciones, `_experienceUpdatedSubLevelKeyList`. No confundir automáticamente con streaming físico; probar donación/desbloqueo A/B. |

[E] Los **29 componentes Server restantes** tienen carga y guardado `0x35EA90` en esta vía: Ai, Attack, Catch, CharacterControl, Docking, ForceField, FrameEvent, GamePlayContents, GamePlayState, Gimmick, Guild, Hierarchy, Interaction, ItemExchange, Journal, LootDrop, Mercenary, Navigation, Party, Projectile, QuickTimeEvent, RemoteCatch, RepairItem, SequencerPlayData, SpecialMode, Target, TradeMarket, UserLogin y Vehicle. Sus vtables individuales están en el CSV. **La lectura de los punteros es [E]; la semántica de stub es [E-x]:** `0x35EA90 → .xtls 0x86F0570..80`, cinco instrucciones que calculan un código, lo escriben en `[rdx]` y devuelven `rdx`, sin recorrer el componente ni producir datos. No se propone enganchar ese thunk. [S] Esto significa «sin sección propia cargada/guardada por estas vf», **no** «sin estado persistente»: `Gimmick` es un ejemplo cuyo estado aparece bajo ContentsMisc. Riesgo/prueba común: comparar altas/bajas en campo contra las estructuras reflejadas del checkpoint.

### 2.2 ContentsMisc: el mundo está dentro de un componente mixto

[E] El nuevo recorrido identifica desde `0x28B7360`/`0x28BB540` tipos de mundo: `FieldSaveData`, `FieldNPCSaveData`, `FieldGimmickSaveData`, `FieldLevelModifiedSaveData`, `ContentsNPCScheduleSaveData`, `CustomHouseSaveData` y objetos de vivienda. A la vez incluye `UserGameSaveData`, historial/posición, tutoriales (`PlayGuideSaveData`), derechos (`EntitlementSaveData`), marcadores/filtros y minijuegos. Fuente reproducible: `profundidad.py` y entrada ContentsMisc en `vision-a3-recorrido.json`.

[E] Evidencia concreta: `0x28B8CDB` construye `FieldSaveData` con `0x18CE210`; el constructor referencia vtable `0x58AF0E0` en `0x18CE266` y nombra `_fieldGimmickSaveDataList` (`0x57DE190`), su lista de perdidos (`0x57DE150`) y `_globalGameEventDataList` (`0x57DE110`). Son **miembros de ese objeto de datos**: la palabra «global» no demuestra escritura en un singleton global. [E] El productor de guardado menciona `_moldingHousingObjectSaveDataList` en `0x28BF989`; las otras clases/listas de vivienda salen de `0x1957C70`/`0x194F230`.

[S] Política mínima para el mundo: una sola copia autoritativa de gimmicks/NPC colocados, modificación de niveles, vivienda compartida, eventos globales, hora y estados de stages que alteran el campo. Mantener por jugador inventario/stats/equipo/habilidades/conocimiento/diálogos/historial personal. **No copiar ContentsMisc entero** desde el propietario a todos: borraría marcadores, tutoriales o derechos personales; tampoco permitir que cualquiera lo sustituya en el mundo con su blob antiguo.

[E/S] No está cerrado el detalle de `GlobalGameEventSaveData`, precios `TradeItemPriceRate`, facciones ni todos los caminos de horario NPC: el recorrido contiene saltos `.xtls` y llamadas virtuales. Se corrige la lectura demasiado fuerte de E3 §1.2: allí solo se inspeccionaron escrituras directas y una profundidad limitada; ausencia de `mov [rip]` no excluye escrituras a objetos globales obtenidos por puntero. Esta laguna **bloquea afirmar aislamiento completo**, aunque no impide diseñar el siguiente experimento.

[S] Prueba discriminante: dos blobs con hora, quest, vivienda y gimmick distintos; cargar B sin aplicar mundo; observar antes/después los contenedores del propietario y del campo. Guardar B, reiniciar y comprobar que ni su inventario cambió al de A ni el mundo retrocedió. Registrar vtable, sección y actor propietario de cada llamada `vf50/vfD0`; solo entonces cerrar el mapa exacto de componentes instalados.

## 3. Gestor único, banderas y autoguardado

[E] Las rutas visibles resuelven el gestor por `[[0x6D69698]+0x48]`, `[[0x6D696A0]+0x30]` y `[[0x6D696A8]+0x10]`; **la igualdad de los tres punteros sigue siendo [S] hasta leerla en vivo**, aunque usan el mismo layout. E3 §1.5. No convertir similitud de acceso en prueba de identidad de objeto.

| Estado | Evidencia y efecto | Riesgo / prueba |
|---|---|---|
| `+0x60` | [E] `0x186F570` toma el lock del gestor y comprueba el byte ocupado en `0x186F6BF`; si ya estaba activo, libera el lock y devuelve el error almacenado en `0x6CF7CA4` (`0x186F6DF`). Si estaba libre, lo marca y lo limpia al salir. [E-x] `.xtls 0xCBD8609..30` hace lo mismo para guardar. No es un fallo de adquisición del lock. | No es cola de usuarios ni espera justa. Dos operaciones pueden rechazar una; probar carga durante guardado y reintento limitado externo. |
| `+0x61/+0x64/+0x88` | [E] `ReviveLoadingReq → 0x2947E00` deja recarga pendiente; `LoadingComplete → 0x2BCAF00` limpia en `0x2BCB1B0/1C9/1E4` sin etiqueta de usuario visible. E3 §1.5, `savemgr_fields.txt`. | Un usuario consume/borra la recarga de otro. Probar A revive mientras B viaja/termina login; requerir dueño y época de transición. |
| `+0x80/+0x90` | [E] `0x2BC7670` pasa `+0x90` a `pj->vf(0x458)`, lo vacía con `0x186DD70` y consume `+0x80` mediante `0x1871680/0x1873140`. | Punteros temporales compartidos no se clonan con `memcpy`. Serializar la transición completa o demostrar un contexto por usuario. |
| `+0x40/+0x48`, `+0x58/+0x5C`, `+0x70/+0x78` | [E/E-x] tabla de ranuras, contadores/caché y selección global; `0x1871850 → .xtls 0xCBDE990` rota 0–2/reutiliza guardado reciente, P9 §5.1. | ID de ranura no identifica cuenta. Remapear por transacción antes de `0x186E450`; no permitir commits pendientes de otra cuenta. |
| `0x6CEEDE8` | [E] modo un jugador usado por login `0x2C3E030`; el camino alternativo espera infraestructura de base de datos. `segunda-sesion.md` §1.5; A0. | Mantenerlo. Cambiarlo no crea un servidor dedicado y puede abandonar todas estas rutas. |

### 3.1 Temporizadores: qué conservar

- [E-x] **2011**, `GamePlaySaveDataRepeatTimer`, `.xtls 0x10859D60/0x10859ED0`: contador de 10 ticks y recorrido de actores; selecciona principal del usuario, `pj+0xC3` cargado y nivel que permite guardar. No limita la selección al anfitrión. Fuente: `optd\e3\x_10859d60_2011.asm`, `x_10859ed0.asm`, E3 §3.2. **10 ticks no se convierte aquí en 10 segundos** sin conocer su cadencia.
- [E] **2077**, `SaveGameDataAutoTimer`, `0x2C35EB0`: pide pausa a cada hilo registrado en `ServerNet+0xF8` (`0x2C35F31..47`), espera a que los que están procesando indiquen pausa (`0x2C35F70..7E`, espera de 10 ms), llama `0x28FC6F0` (`0x2C35F94`) y limpia petición (`0x2C35FC4`). El registro usa TLS `+0x4460` y la red lo consulta; E3 §4. Es barrera de consistencia durante guardado, no prueba de pausa por menú.
- [E/E-x] **2070**, `FlushPendingSaveDataOnceTimer`, `.xtls 0x10839370 → 0x1871930`: completa pendientes del gestor con la guarda `+0x60`, depende de `+0x58/+0x5C`. Un fallo llega por Ack3016/Nak1013 a la sesión correspondiente; no hay rearmado propio visible. E3 §3.3.
- [E] `0x28FC6F0` envía `SaveToFileCurrentPlayerAck`3016 mediante destinatario del personaje al fallar. **No suprimir temporizadores o fingir éxito**: hay que conservar resultado, orden y gestión de pendientes. [S] Una cola externa puede agrupar solicitudes repetidas por cuenta, pero solo una vez demostrada la equivalencia del ciclo nativo completo.

### 3.2 Cómo guardar sin pisarse y qué significa «sin congelar a todos»

[S] Diseño inicial: un coordinador de persistencia con `transaccion{SteamID, sesionEpoca, slotArrendado, generacion, motivo}`. Solo permite una operación nativa de carga/guardado y una transición que use `+0x61/+0x80/+0x90` a la vez. No se bloquea un hilo esperando una Req que necesita ese mismo hilo: se retiene la admisión en la capa de transporte/planificación antes del despacho. No hacer esa retención indiscriminadamente dentro de un hook tardío.

[S] El contexto vive hasta completar los pendientes de 2070 y el par lobby/save, aunque el jugador se desconecte; los punteros vivos se validan por época y vida útil. Desconexión no autoriza a reasignar inmediatamente su ranura. Comprobar que el consumidor de `MoveFieldComplete/LoadingComplete` corresponde al dueño de la transición; si el motor no lo permite sin cambiar demasiadas rutas, **limitar temporalmente el viaje/respawn simultáneo** es más prudente que intercambiar los globales a ciegas.

[E/S] La barrera es global a los hilos registrados: con la ruta nativa **hay una pausa compartida**. No puede prometerse «cero congelación» para 2, 4, 8 o 10. Medir duración de serialización, cifrado, disco y espera por separado. Primero mantener la barrera; después, si hace falta, estudiar copia inmutable coherente bajo barrera y escritura diferida de esa copia. Esto requiere demostrar vida útil y ausencia de punteros al estado mutable, y separar serialización/cifrado/commit; **no basta con mover `0x186E450` a un hilo propio**. El realm TLS y los asignadores también importan (D0 §5–6).

| Sesiones totales U, incluido propietario técnico | Pares nativos por ronda si todos guardan | Ficheros mínimos de esos pares | Pausa acumulada de ruta secuencial | Capacidad humana si uno está solo aparcado |
|---:|---:|---:|---|---:|
| 2 | 2 | 4 | `t1+t2`, sin medición | 1 invitado; o 2 humanos si el propietario juega |
| 4 | 4 | 8 | `Σti`, sin medición | 3 invitados; o 4 humanos si el propietario juega |
| 8 | 8 | 16 | `Σti`, sin medición | 7 invitados; o 8 humanos si el propietario juega |
| 10 | 10 | 20 | `Σti`, sin medición | 9 invitados; o 10 humanos si el propietario juega |

[S] Es aritmética de este diseño, no capacidad observada. El manifiesto del mundo es una entidad lógica independiente; **no se añade un undécimo usuario técnico**. Si mundo y jugador anfitrión comparten temporalmente un contenedor nativo, su separación lógica requiere la política de §2, no dos motores. Para una persistencia propia futura se podrían mantener un snapshot mundial y U snapshots privados sin que cada archivo sea un actor, pero no está implementado.

[S] Presupuesto de referencia —usando solo el ejemplo histórico de 4,2 MB planos y 1,2 MB cifrados de P9—: 2/4/8/10 snapshots serían aproximadamente 8,4/16,8/33,6/42 MB planos y 2,4/4,8/9,6/12 MB cifrados, antes de copias de trabajo, mundo, metadatos y copias de seguridad. No son límites de memoria ni tiempos de guardado. Treinta registrados no necesitan treinta sesiones; requieren archivos en reposo y un catálogo de identidad (A4).

## 4. Ruta de escritura y aislamiento de archivos

[E/E-x] Ruta contrastada: `2077 → 0x2C35EB0 → 0x28FC6F0 → 0x186E1A0 → .xtls 0xCBD84D0 → 0x186E450 → 0x2069640 → 0x235AFE0/0x2355700 → 0x23571D0 → 0x2358750 → 0x235C7A0`. El salto `.xtls` es mutado legible en la selección de ranura, no un lugar para enganchar. La ruta ejecuta código nativo con su sincronización.

[S] Puntos de observación/redirección posibles: entradas `.rsrc` **`0x186F570`** para carga y **`0x186E450`** para guardado. El actor identifica una sesión/cuenta autenticada en la tabla del servidor; no confiar en un SteamID dentro de una Req arbitraria. Cambiar la ranura solamente dentro de una transacción cuyo arrendamiento ya existe. `0x20688A0`/`0x2069640` sirven primero para observar componentes y verificar la separación. No se propone hook nuevo sobre ninguna vf que sea salto `.xtls`.

| Entrada .rsrc | Primeros 16 bytes | AOB exacta única para 2976, medida en todo el exe |
|---|---|---|
| `0x186F570` carga | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 44 89 44 24 18 55 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC` |
| `0x186E450` guardado | `4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48` | `4C 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 78 FE FF FF` |

[E] Entradas por `.pdata`, bytes y unicidad constan en [vision-firmas-2976.json](data/vision-firmas-2976.json), SHA del exe `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`. Allí constan también las firmas de observación `0x20688A0`, `0x2069640` y lectura `0x2357DA0`. Una AOB única hoy no garantiza estabilidad ante parche ni seguridad semántica.

[S] Árbol propuesto fuera de toda carpeta personal/Steam Cloud: `servidor/world/<worldId>/generacion-N/`, `servidor/players/<SteamID64>/generacion-N/`, `servidor/staging/<epoca>/<slot>/` y un manifiesto que referencia únicamente generaciones completas. Cada registro lleva build, versión de esquema propia, versión de SAVE, hashes de ambos archivos, SteamID, worldId, época y secuencia. La raíz concreta se elige al implementar, no se crea ahora.

[S] Redirigir **todas** las operaciones nativas de esa raíz: leer, escribir, enumerar, temporales, copia, renombrado en ambos extremos, atributos y borrado. Las rutas personales siguen protegidas por P6. No usar en disco el `slot100` existente del anfitrión ni cualquier otra ranura real. Mantener 3/206 prohibidas antes de la carga y del guardado, no solo al escribir un fichero. [E] 3 dispara CrossSave (`0x186E6CD`, llamadas a `0x15AC100`), 206 tiene una ruta condicional de borrado después de cargar (`0x186F850–867 → 0x1871040`); la llamada no prueba eliminación consumada, pues también se comprueban tipo/estado del actor y ocupación/existencia en el gestor. Fuentes: P9 §§2,5 y revisión independiente de A4 §9.

[S] Los números 100–108 y 207 ofrecen diez candidatos de trabajo, **no diez ranuras certificadas seguras**: 207 tiene semántica de instancia aún sin cerrar. Se puede reservar una ranura por sesión activa, con el mundo en el contenedor del propietario técnico. El registro de 30 cuentas permanece independiente del número de ranuras. Reutilizar un número para otra cuenta exige demostrar que `S+0x18`, `ranura+0x69C`, listas `+0x6A0…`, `fileObj+0x120/+0x138` y todos los pendientes se vacían/recargan. Hasta entonces, **rechazar o esperar nuevas admisiones al agotarse arrendamientos**, no cambiar solo la ruta por SteamID.

[S] Publicación: tras el éxito de ambos archivos, comprobar hashes y versión, cerrar la generación de staging y cambiar un pequeño manifiesto de forma atómica. El puntero publicado solo avanza a un checkpoint válido. No se borra el anterior hasta haber validado recuperación. Intercambio de objetos, botín y cambios que abarcan mundo+jugador requieren una generación común o diario de operaciones idempotentes; recuperar «el archivo más nuevo de cada uno» puede duplicar o perder bienes.

[S] Un fallo de disco no puede producir Ack de persistencia propio de éxito; conservar la última generación y marcar el servidor como no apto para admitir más cambios durables. Si se difiere el commit, la interfaz distingue «estado en memoria» de «checkpoint confirmado». Riesgo/prueba: pérdida de conexión, proceso interrumpido entre lobby/save y falta de espacio en staging, siempre sobre copias de laboratorio autorizadas posteriormente.

## 5. Hora, clima y día/noche bajo autoridad del servidor

[E] `FieldTime` tiene copia de servidor en **`0x6A6D238`** y de cliente en `0x6A6D218`; la selección usa el realm TLS `+0x1EC`. `0x20E55A0` calcula la hora actual a partir del ancla/escala salvo campo pausado `+0x17C` o nivel de hora fija. `hora-del-dia.md` §§1–4; `0x219D7F0` y `0x2BC0A30` reextraídos en A3.

[E] R1 tiene dos rutas diferentes: `0x2BC0A30` instala hora al viajar si el actor es dueño del campo; `0x219D7F0` copia la hora guardada desde el componente al recibir `LoadingCompleteReq` con byte no cero **sin control de dueño visible**. Por tanto, compartir campo suprime una condición del primer camino para el invitado pero no arregla el segundo. `LoadingCompleteReq=00` evita esa copia, **no resuelve por sí solo toda la hora**, y su compatibilidad debe probarse (D-15).

[S] Persistir una hora mundial normalizada, su escala y una secuencia; restaurarla una vez al iniciar mundo, reanclar con el reloj del proceso y distribuirla a todos al completar carga. No guardar como autoridad un ancla monotónica del proceso anterior. El mundo puede avanzar mientras el servidor está encendido; política prudente al reiniciar: **reanudar desde el último checkpoint sin simular días offline**, para no expirar eventos o saltar fases sin jugadores. Hora semanal real de A6 es otro reloj, independiente del día del juego.

[E/S] `SetFieldTimeAck`2816 existente se emite por `ToNear` (`0x2BC8970 → 0x299AED0`), de modo que no alcanza por definición a usuarios lejanos. Para autoridad mundial haría falta entregar el mismo cambio a cada sesión cargada y resincronizar el que termina un viaje. No es suficiente sincronizar solo el cliente del anfitrión. No dejar que cada cliente haga dormir al mundo por su cuenta; arbitrar solicitudes y eventos de historia. Riesgo/prueba: B entra con guardado de otro día, C viaja lejos y A duerme; comprobar monotonía/consistencia de servidor y todos los clientes.

[E] El clima no queda reducido al reloj: `SyncWeatherStateReq`2560 llega a `0x29C5DF0 → 0x20E6FE0` y actualiza `field+0x178` con seis valores; existe simulación de cliente según `hora-del-dia.md` §3. La igualdad del clima visual con igual hora/región **no está demostrada**. [S] Para un mismo campo debe existir una fuente autorizada de clima, con época y filtrado por sesión; a distancia puede variar por región dentro del mismo mundo. Si el cliente técnico no mantiene las regiones remotas, la autoridad climática requiere traspaso o simulación propia: no atribuirle trabajo que no hace.

[S] Prueba de clima: dos clientes quietos, misma región/hora, capturar sus seis valores y comparar; repetir con propietario técnico lejos y tras reconexión. Si divergen, aún falta el mecanismo que alinea efectos visuales y estado servidor. Los primeros clientes de cada región no deben competir escribiendo un único campo climático sin política.

## 6. Pruebas en vivo propuestas y criterios de avance

Todas son **propuestas al usuario para después**: ninguna se ejecutó. Precondición común: E0/E1/E2/E3/E3b/G2 según A0, backups, raíz de laboratorio y P6 protegiendo archivos reales. No levantar P6 globalmente para probar persistencia.

| Orden | Experimento | Qué confirma o refuta | Decisión |
|---|---|---|---|
| PERS-1 | G3a/G3b: dos cuentas, dos blobs distintos; observar gestor/S/cachés y cargas en directorio aislado | Igualdad real de contextos, compatibilidad de cifrado y lobby; sin escribir en save personal | Si falla, resolver formato/cuenta antes de persistir |
| PERS-2 | Traza `actor+0x78`, vtable, vf50/vfD0 por actor; entrada B con mundo diferente | Catálogo realmente instalado, escrituras globales/campo, hora y componentes mixtos | Autorizar solo secciones cuyo dueño esté demostrado |
| PERS-3 | G3d/G3e ampliada: A y B modifican objetos distintos; guardar/reiniciar; intercambiar orden | Par lobby/save coherente, inventarios aislados y mundo que no retrocede | Persistencia básica de 2 usuarios |
| PERS-4 | Reutilizar ranura A→B→A tras logout, siempre desde sus propias generaciones | Ausencia de plano/pendientes viejos y de título ajeno | Si no queda demostrado, no reutilizar y bloquear admisión al agotar slots |
| PERS-5 | A revive, B viaja, otro login y autoguardado coinciden | Propiedad de `+61/+64/+88/+80/+90`, ausencia de interbloqueo o Nak sin recuperación | Generalizar coordinador antes de 4 jugadores |
| PERS-6 | Corte entre cada fase de guardado, disco lleno y recuperación | Manifiesto consistente; no pareja parcial, duplicación o pérdida de inventario | Persistencia durable; pruebas en directorio aislado |
| PERS-7 | Repetir con 4→8→10 sesiones totales; registrar p50/p95/p99 y máximo de barrera, cola y E/S | Cuánto dura realmente la pausa y si la cola drena; no basta promedio | Detener crecimiento si hay pérdida de fluidez/cola creciente; presupuesto se acuerda tras medir 2 |
| PERS-8 | Reinicio, login antiguo, viaje lejano, sueño y clima; 24–72 h con ciclos de reconexión | Hora autoritativa, aislamiento duradero, acumulación de cachés y backups | Mundo doméstico persistente; de otro modo solo sesiones temporales |

[S] Hasta PERS-3 no hay fundamento para llamar «persistente» al prototipo; hasta PERS-5 no hay aislamiento de transiciones; hasta PERS-7/8 no hay evidencia para 8–10 ni operación continua. Ningún número de sesiones elimina los riesgos de estado compartido.

## 7. Diez afirmaciones prioritarias para verificación independiente

| # | Afirmación a refutar | Contraste exacto |
|---:|---|---|
| 1 | Cabecera 0x80, `SAVE`, tamaños y flags | `0x2357DA0`, `0x235C576..60F`; comparar validación frente a tabla §1 |
| 2 | Descifrador y cifrador son thunks a `.xtls`; ChaCha no está demostrado en `.rsrc` | primeros bytes `0x235C2E0`, `0x235C090`; destinos `0xEF713F0/0xEF6EA10` |
| 3 | Recorrido por actor de vector +78/cuenta +80/stride16; carga vf50 | `0x2069018..27`, `0x20693E8` |
| 4 | Guardado usa vfD0 y nombre vfD8 | `0x20696A6..B8`, `0x20696F6`, `0x20697E1` |
| 5 | 18 vf50 no stub entre 47 Server*ActorComponent; Navigation stub | regenerar `inventario.py`, verificar tablas RTTI del CSV; comprobar límite de membresía vivo |
| 6 | Se guardan usuario/lobby y personaje/save en dos llamadas; contrastar si eso demuestra commits efectivos separados | `0x186E5DD`, `0x186E606`, `0x186E678`, `0x2069CE3`; resultado corregido en §8 |
| 7 | ContentsMisc realmente refiere FieldSaveData, gimmicks, globalGameEvent y vivienda | `0x28B8CDB → 0x18CE210`, `0x18CE266/292/2FA`, `0x28BF989` |
| 8 | 2077 pone/espera/limpia la barrera alrededor de guardar | `0x2C35F31`, `0x2C35F70..94`, `0x2C35FC4`; `0x27733F0` para TLS |
| 9 | Guardar elige ranura global y llega a 186E450; +60 rechaza ocupado | `.xtls 0xCBD84D0` (en especial `0xCBD8609..38`), `.xtls 0xCBDE990`, `0x186F570` |
| 10 | Hora del login puede escribir global sin comprobar dueño; la del viaje sí lo mira | `0x219D7F0`, `0x2BC0A30`, global `0x6A6D238`; probar contra mitigación00 |

Los datos derivados y scripts no ejecutan código del juego. `inventario.py` corta los thunks al primer salto y no interpreta su relleno como instrucciones; `profundidad.py` normaliza límites de función, sigue solo llamadas directas `.rsrc` hasta profundidad 2 y registra saltos `.xtls`. No demuestra ausencia de efectos en llamadas indirectas. La revisión independiente y sus correcciones constan a continuación.

## 8. Verificación independiente de diez afirmaciones

Revisión del 2026-10-08 por un agente distinto del autor, leyendo de nuevo el exe en disco. SHA-256 obtenido de nuevo: `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`. No se ejecutó juego, código del mod, pruebas ni acceso a partidas. Scripts y volcados propios: `C:\Emulador CD\build\research\vision\verificacion\a3\contraste.py`, `complemento.py` y `verificacion_final.py`; datos resumidos: [vision-verifica-a3.json](data/vision-verifica-a3.json). El catálogo se regeneró buscando RTTI/COL/vtables en el exe, **sin reutilizar la caché RTTI ni el CSV del autor**.

| # | Resultado | Evidencia independiente y tentativa de refutación |
|---:|---|---|
| 1 | **Confirmada [E]**, precisión editorial aplicada | `0x2357F69..CB` valida 0x80, magic en `0x5547B6C` (`SAVE`), versión 1–2, máscara permitida 0x2 y longitudes no nulas con carga ≤ plano. `0x235C531..3F` exige retorno cero del núcleo antes de `0x235C576`; `0x235C60F` descomprime y `0x235C616..2A` exige longitud esperada. Se reemplaza «bit 2» por máscara 0x2. No valida aquí ChaCha/HMAC. |
| 2 | **Confirmada [E/E-x]** | Bytes de entrada: `0x235C2E0` es salto a `0xEF713F0`, y `0x235C090` a `0xEF6EA10`; destinos en `.xtls` según secciones del PE recién leído. Las envolturas no identifican la primitiva criptográfica ni demuestran independencia de SteamID. |
| 3 | **Confirmada [E]** | `0x2069018/1C/23/27` toma vector, recuento, desplaza 4 y calcula fin; `0x20693E8` llama `vf50`. Es por el actor recibido, sin prueba de que cada uno de los 100 componentes esté instalado en cada usuario. «Cuenta +80» se aclara como recuento, no identidad. |
| 4 | **Confirmada [E]** | `0x20696A6..B8` repite vector/recuento; `0x20696F6` y `0x2069714` llaman `vfD8`; `0x20697E1` llama `vfD0` con contenedor de salida. No se encontró partición explícita mundo/jugador en ese recorrido. |
| 5 | **Confirmada [E/E-x]**, confianza corregida | Escaneo propio produce 100 vtables completas: 47 Server y 53 Common; 18 Server tienen tanto `vf50` como `vfD0` diferentes de `0x35EA90`. Navigation `0x5B1F2A8` usa ese destino en ambas. El supuesto stub es thunk `.rsrc → .xtls 0x86F0570..80`; sólo su cuerpo protegido demuestra el retorno sin sección. La membresía viva y las dependencias indirectas siguen sin probarse. |
| 6 | **Dudosa en su formulación fuerte; corregida [E/E-x]** | Confirmadas dos llamadas a `0x2069640`: `0x186E606` con `[pj+0xA0]`, `0x186E678` con `pj`; ambas alcanzan `0x2069CE3 → 0x2355700`. Pero éste salta a `.xtls 0xEF51850`: `+69C/+699/+698` pueden llevar a éxito sin operaciones de escritura. No queda demostrado «dos commits efectivos» ni atomicidad/durabilidad de la pareja. §1.2 ya no los da por hechos. |
| 7 | **Confirmada [E]**, semántica limitada | `0x28B8CDB → 0x18CE210`, constructor con vtable `0x58AF0E0`, COL `0x5EAE688`, RTTI `0x6B4CB68` = `FieldSaveData`. Strings recién leídas: `_fieldGimmickSaveDataList` en `0x57DE190`, perdidos en `0x57DE150`, `_globalGameEventDataList` en `0x57DE110`; `0x28BF989` referencia vivienda en `0x57E1D28`. El nombre «global» no demuestra singleton ni aislamiento del componente. |
| 8 | **Confirmada [E]** | `0x2C35F35` activa petición; `0x2C35F74..7E` espera confirmación de los activos; `0x2C35F94` llama guardado; `0x2C35FC4` limpia petición. El vínculo TLS está en `0x277343A` (0x4460), `0x2773500..24` (activo/espera/confirmación) y `0x27735E9` (fin). No demuestra cuánto dura la barrera ni qué hace un menú. |
| 9 | **Confirmada [E/E-x]**, motivo de rechazo corregido | `0x186F6BF..E7` rechaza byte ocupado después del lock; no es try-lock. `.xtls 0xCBD8609..30` reproduce guarda; `0xCBD866A/8683 → 0x1871850 → 0xCBDE990` elige según `+70/+78` o tabla `+40/+48`, con candidatos 0–2; `0xCBD86CE` llama `0x186E450`. Es estado del gestor recibido; igualdad de gestores para todos los contextos continúa [S]. |
| 10 | **Confirmada [E]**, alcance local | `0x2BCB0B3..CA` pasa el byte recibido a `0x219D7F0`; `0x219D82B/82F` permite saltar la copia si es cero. Si no, copia 32 B a `0x6A6D238` en `0x219D8EC`, sin comparación de dueño en este cuerpo. Viaje sí compara `field+0x180` con `[actor+0xA0]+0x60` en `0x2BC0A78` antes de escribir en `0x2BC0C50`. No se certifica ausencia de controles en todos los llamadores ni que el byte cero resuelva toda la sincronización. |

Resultado: **9 confirmadas con límites explícitos y 1 dudosa en la lectura fuerte, corregida**. El contraste no certifica separación semántica, reutilización segura de ranuras, cifrado independiente de cuenta, commit durable ni operación con varios jugadores. Las pruebas PERS-1..8 siguen siendo necesarias; para A4 el cambio relevante es esperar pendientes y confirmar la pareja publicada, no sólo dos retornos de éxito.

