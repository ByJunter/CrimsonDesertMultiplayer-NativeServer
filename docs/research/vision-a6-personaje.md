# Visión A6 — Personaje y personalización semanal

> **Alcance revisado (D-36, 2026-10-08): fuera la personalización semanal.** No hay cambio de
> aspecto una vez por semana ni reloj semanal del servidor; T18 queda retirada. Sigue la apariencia
> al entrar por primera vez (alta inicial). Detalle en el [diario](opcion-d-diario.md), D-36.

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`. Todas las direcciones son RVA. Investigación estática; no se ha ejecutado el juego, alterado el mod ni leído partidas completas. [V] capturas o observación previa documentada; [E] desensamblado de `.rsrc`; [E-x] `.xtls`, con alcance parcial; [S] diseño o inferencia; [R] fuente externa sin verificar contra esta build.

## 1. Estado inicial y criterio

La personalización semanal debe tratarse como una operación estética sobre la identidad persistente del miembro; no como borrar y volver a crear su progreso. Esta es una recomendación de diseño [S], dependiente del catálogo autenticado de A4 y de las generaciones de guardado de A3. El reloj acelerado del mundo no sirve para contar semanas civiles (`hora-del-dia.md`, `vision-a3-guardado-mundo.md` §5).

La existencia de `CreateCharacterReq` 2711, `SaveCharacterCustomizationReq` 2676 y `CharacterCustomizationAck` 2622 en `data/troc_messages_2976.csv` no demuestra un creador libre accesible ni el funcionamiento de un flujo MMO. Se contrastan sus cuerpos antes de proponer un uso. Kliff observado en las capturas no demuestra que todos los modelos de personaje soporten las mismas acciones, cámaras o cinemáticas (`apariencia-estado-coop.md` §§2.7–3; `companeros-mercenarios.md`).

**Veredicto: cumplimiento parcial y condicionado del objetivo de creación semanal.** Conservar progreso mientras se reedita la apariencia es una propuesta prudente [S], no una equivalencia demostrada con crear un personaje completo. Se separan tres operaciones:

| Operación solicitada o posible | Viabilidad y alcance | Dependencias y prueba |
|---|---|---|
| Alta inicial de un miembro | Diseñable [S]: identidad propia, plantilla A5 y primera elección estética sobre un protagonista existente. No existe aún una alta persistente de varios miembros validada. | A3/A4/A5 y AP-1/2: la nueva identidad no hereda progreso o propiedad de otro miembro. |
| Reedición estética semanal | Condicionada [S]: barbería y protocolo presentes [E], pero apertura desde el mod, validación de campos, difusión y commit durable siguen pendientes. | AP-1/2/6; controles de permiso y persistencia descritos abajo. |
| Recreación completa, cuerpo libre o cambio semanal de protagonista | **Pendiente, no acreditada.** Req 2711 no demuestra un creador libre; tampoco se demuestra que una sustitución conserve habilidades, misiones y cinemáticas compatibles. | Investigación adicional del creador y AP-5 para cada protagonista/cuerpo permitido. Un reinicio de progreso requeriría una política expresa y transacción propia. |

La recomendación inicial es conservar Kliff y su progreso, distinguiéndolo por alias y marcador [S]. Damiane/Oongka y sustituciones de cuerpo son ampliaciones con matriz de compatibilidad propia. Esto deja explícita la parte del objetivo que todavía no puede prometerse.

Scripts y volcados propios: `C:\Emulador CD\build\research\vision\a6\`. El procedimiento `a6_estatico.py` reúne también fragmentos de `.pdata` con unwind encadenado; un volcado que sólo tome el primer fragmento recorta, por ejemplo, `0xF97B10` o `.xtls 0x10890B30`. Las cifras y firmas conservadas en `data/vision-a6-estatico.json` son de esta build. Las cadenas en datos del PE se distinguen de las instrucciones que las usan: por sí solas no prueban que una opción sea accesible.

## 2. Personalización que tiene respaldo

| Posibilidad | Respuesta y evidencia | Riesgo y prueba futura |
|---|---|---|
| Pelo, barba, cejas, tatuajes | Hay una pantalla de barbería, no sólo una cadena sobrante: RTTI/vtable `UIGamePlayControlRootBarberShop` `0x56C5E80`, inicialización `0xF97B10`, activación del estado de la vista `0xF980A0`, actualización `0xF98380` [E]. El recurso ya extraído `C:\Emulador CD\build\research\arrow\out\ui\ui__barbershopview.html` contiene cinco pestañas pelo/barba/cejas/tatuaje facial/corporal, subpestañas tipo/color/deslizador y tres controles de longitud [R: recurso previo cuya build de extracción no se ha confirmado; las cadenas del exe lo corroboran parcialmente]. | No se han contado estilos habilitados ni condiciones de desbloqueo. En AP-1 inventariar controles disponibles y cambiar uno por vez. |
| Color y longitud | Referencias del PE: `hairBaseColor` `0x56C4E00`, `hairLength1/2/3` `0x56C4D98/88/78`, `faceTattooColor` `0x56C4D38`, `BodyTattooColor` `0x56C4D28`, `faceTattooOpacity` `0x56C4F38`, `BodyTattooOpacity` `0x56C4F20`; inicializadores y consumidor de la vista documentados por `a6_estatico.py` y `a6_strings.py` [E para instrucciones; cadenas como datos]. | La presencia de huesos/mallas/paletas en `CharacterCustomizationDataManager` no prueba acceso a todos los parámetros desde barbería. AP-1 debe relacionar cada control con un campo del mensaje. |
| Tinte de ropa y armas | Existen rutas separadas: Req 2545/2800, Ack 2282/3026, según tabla Troc y `apariencia-estado-coop.md` §§1.2/1.5. Los escritores conocidos de tinte usan `ToTarget`; `OtherEquipItemAck` sólo transporta itemKey y ranura [E heredada del informe, escritores `0x29F6320/0x29F67D0`, equipo `0x29ED1F0`]. | Ni el mismo itemKey ni el relevo de equipo garantizan color remoto. AP-3 exige cambiar tinte con observador presente y comprobar entrada posterior. |
| Equipo visible | El cliente dispone de aplicación a actor por clave `0xBBF8D0 → 0x99F500`, y el servidor de relevo `0x2AD685A → 0x29ED1F0` [E, `apariencia-estado-coop.md` §1]. Es equipamiento normal, con sus estadísticas; no demuestra transfiguración cosmética independiente. | No congelar cambios de equipo durante siete días: impediría jugar. El límite semanal se propone para el perfil estético; equipar/quitar sigue las reglas normales [S]. Validar con P5 y AP-3. |
| Nombre | Las ocho capturas contienen `Kliff` en el lobby [V, `a6_capturas.py`]. Hay Ack 2484 y lector `0xB70DB0`, pero eso no demuestra una pantalla nativa de renombrado o una etiqueta sobre jugadores. `apariencia-estado-coop.md` §2.7 localiza también Kliff en ActorLoadData. | Usar alias propio por SteamID con límites de longitud, normalización y escape en la UI [S]; no sustituir cadenas narrativas ni usar alias como identificador. AP-4 comprueba asociación de alias y actor tras reconectar. |
| Damiane y Oongka | Las notas oficiales de 1.13.00 describen restricciones y mejoras distintas para ambos, incluida ropa y acceso a contenidos [R, fuente F1]. El camino de barbería distingue personaje controlado de mercenario (`0xF98197–0xF981DE`) [E]; existe además Ack 2388 de personalización de mercenario, lector `0xBEA250 → 0x9E43A0` [E]. | Esto prueba categorías y protagonistas disponibles en el producto, no tres cuerpos intercambiables manteniendo todas las habilidades de Kliff en cooperativo. AP-5 debe comprobar combate, armas, montura, cámara, cinemáticas y guardado para cada protagonista antes de habilitarlo. |
| Otra raza, sexo, altura o cara libre | El motor tiene controlador de personalización y metadatos de mallas/huesos, pero no se ha establecido una lista blanca segura de cuerpos jugables ni un flujo que replique esas sustituciones [S; RTTI `0x559DD98`, cadenas `0x559E270–0x559E340`]. No se ha usado código de mods externos ni descargado modelos. | Tratarlo como investigación separada. Una malla que se dibuja puede tener otro esqueleto, colisiones, sockets, ropa, voz o cinemáticas. No se promete en el objetivo inicial. |

[R] El fabricante confirma la barbería y la previsualización de estilos en sus notas 1.08.00 (F2), y tintes/equipamiento diferenciado en 1.13.00 (F1). **No se equipara esa numeración pública con 1.0.0.2976**: para la build investigada mandan el exe, sus datos y las capturas. El catálogo exacto desbloqueado sigue pendiente de AP-1.

## 3. Almacenamiento y trayecto de la apariencia

### 3.1 Req 2676 y Ack 2622 no son un creador de personajes

`SaveCharacterCustomizationReq::Process` en `0x298DC30` verifica longitud de trama, deserializa el bloque, exige consumir exactamente el payload y usa el **actor ya resuelto en TrocParams**: `[params] → [actor+0x68]+0x20`, pasando ese componente a `0x294AA40` (`0x298DDE9–0x298DE11`) [E]. No deserializa una cuenta u64 ni usa un SteamID del payload para resolver ese actor; esto no descifra la semántica de todos los bytes del bloque estético. El transporte y la selección de slot deben vincularlo al actor autorizado por A4; que el handler use el actor de sesión no valida por sí mismo cada valor de apariencia.

| Mensaje | Payload de esta build | Evidencia |
|---|---|---|
| Req 2676 | **271 B** = `u32 A + u8 B + 16 bytes C + 250 bytes D`. Total Troc **276 B** incluida cabecera de 5 B. A/B todavía no tienen semántica de UI completa; no llamar a A actorKey. | `0x298DCE4` lee 4 B; `0x298DD11` lee 1 B; bucles hasta `0x10` en `0x298DD74` y `0xFA` en `0x298DDB4`; comprobación `0x298DDBC–0x298DDC4` [E]. |
| Ack 2622 | **279 B** = `u32 K1 + u32 K2 + bloque de 271 B`; total **284 B**. Los ocho Ack de capturas tienen K1=K2=`0xA0100001`; la igualdad observada no autoriza suponer que siempre sean equivalentes. | Lector `0xB71530`: K1 en `0xB715F7`, K2 en `0xB7168F`, bloque en `0xB716BC–0xB71777`; consumido exacto `0xB71779` [E]; `data/vision-a6-capturas.json` [V]. |

El escritor cliente `0xB6E260` serializa el bloque mediante `0x24450D0`, obtiene slot de `actor->vf(0x168)` en `0xB6E2E8`, requiere slot distinto de `0xFF`, y pone opcode `0xA74` en `0xB6E2F2`; sólo envía por `PostPacket 0x35C670` cuando `ClientNet+0x112` indica conexión [E]. Los llamadores directos hallados son `0x6299E0` y `0x629A70` (xrefs comprobables); **no se recomienda llamar al escritor desde un hilo arbitrario**.

En el receptor del Ack, K1 pasa por la comprobación `0x8B53C0` cuando lo permiten sus condiciones de tipo/valor (`0xB71637–0xB71659`) y por la búsqueda `0x8A67E0`; el bloque termina aplicado al actor resuelto con **K2**, mediante `0x8AE940` en `0xB717FF`, su componente `+0x20` y `0x1795B40` en `0xB7184B` [E]. Este último es un thunk a `.xtls 0xC759040` [E-x para la aplicación]. La semántica dual de claves debe conservarse y probarse, especialmente para otros protagonistas o actores vinculados.

### 3.2 Representación en componentes y archivo

La conversión legible `0x178EA70` transforma el bloque de 271 B en dos vectores de **16** y **250** bytes más el u32 en `+0x20` (`0x178EACF`, `0x178EB1D`, `0x178EB30`) [E]. El byte B no se conserva como un tercer campo en esa copia observada; no interpretarlo como licencia, época o contador semanal.

`0x1795A60` mantiene una estructura de **0x28 B** apuntada por **StatusComponent+0x388**; copia los dos vectores y el u32 (`0x1795A77`, `0x1795A9F`, `0x1795B09–0x1795B24`) [E]. La ruta protegida `0x1795B40 → .xtls 0xC759040` llama a esa copia; también puede notificar al actor mediante vf298/vf2A0 y actualizar estado de mercenario [E-x]. Por tanto apariencia de mercenario y personaje controlado no son necesariamente rutas idénticas.

| Persistencia | Carga | Producción de datos | Alcance y límite |
|---|---|---|---|
| `characterstatus`, vtable servidor `0x5B203F0` | Servidor `0x293FE70 → .xtls 0x10427500`; base `0x1791980` distingue objetos de reflexión. En `0x1791A46` consulta metadatos `CustomizationSaveData` por `0x179E720`; la copia hacia `+0x388` está en `0x1791D19` [E/E-x]. | vfD0 `0x1791DA0 → .xtls 0xC73A550`. Si `+0x388` no es nulo (`0xC73A75B`), crea objeto de metadatos `0x179E720`, copia con `0x178EC60` (`0xC73A797`) y añade al archivo de componentes [E-x]. | Apariencia vive en estado privado del personaje, además de stats y otros campos. No separar el componente entero como sólo estética. Tabla base de A3 y estos volcados. |
| Objeto reflejado `CustomizationSaveData` | Metadatos `0x179E720`, vtable de metadatos `0x57C78C8`; RTTI de objeto `0x58AEBA0` [E/datos RTTI]. | `0x178EC60` copia las listas hacia el objeto de guardado [E]. | Conservar formato nativo; no escribir posiciones fijas dentro del fichero cifrado. |
| Lobby | `ServerLobbyActorComponent` `0x2B7F4D0`; Ack 2869 lector `0xBDCE50` [E]. | `0x2B7F850`; A3/A4 detallan su relación con la ranura y el personaje [E]. | El lobby contiene identidad/resumen del personaje; **no se ha demostrado que él sea el archivo autoritativo del bloque de 271 B**. Cambiar sólo el lobby no demuestra que cambie el modelo en mundo. |

Una escritura de apariencia en RAM, un Ack de éxito y la generación durable son tres hitos distintos [S, diseño sustentado por A3 §4 y ruta `0x294B1BC–0x294B259`]. El guardado puede fallar después de que el dueño haya visto el nuevo aspecto. El derecho semanal sólo se consume mediante el recibo durable de §5.

### 3.3 Difusión: corrección de una expectativa importante

Tras Req 2676, la rama observada de `0x294AA40` realiza varias comprobaciones/transacciones (`0x27B0540` y familia), convierte y aplica el bloque, y construye **ToTarget** en `0x294B21B` (vtable `0x5B25438`); toma la sesión por vfE8 y llama a `CharacterCustomizationAck` writer `0x298A3A0` en `0x294B24F` [E]. **Ese envío explícito sólo confirma al destinatario seleccionado.** Las llamadas virtuales/protegidas impiden demostrar por este cuerpo una ausencia global de otros efectos, pero tampoco autorizan prometer difusión a vecinos.

Existe otro emisor `.xtls 0x10446580`, alcanzado por thunk `0x2945B40`. Aplica primero la apariencia y **sólo si la aplicación devuelve código cero y su argumento r9b inicial es distinto de cero** construye ToNear (comprobación de resultado en `0x104465AE–0x104465B8`; condición y construcción en `0x104465BA–0x104465F8`) y llama al mismo writer en `0x10446614` [E-x]. Además, `0x28F31F0` llama al writer en `0x28F3614` dentro del arranque/carga del personaje [E]. La etiqueta del mensaje no identifica el destinatario: **no deducir que la edición en vivo usa ToNear porque un envío de carga sí lo hace**.

[V] El lector propio `a6_capturas.py` recorrió las ocho capturas `20261003-{105149,112627,125936,131004,131648,134008,160139,200328}`. Cada una tiene **un Ack 2622 y un Ack 2869**, todos los Ack 2622 de payload 279. No encontró Req 2676, Req 2711, Ack 2651, Ack 2484 ni las cuatro Req/Ack de tinte enumeradas. Son capturas de entrada/juego que **no documentan una sesión de barbería**. La captura 105149 es versión 1 y conserva truncados sus 3.995 ActorLoadDataAck; las otras siete son versión 2 y tienen 14.334 cargas completas. Se leyeron 18.329 cargas en total, sin registros rotos. No se interpreta ausencia como imposibilidad del flujo.

`ActorLoadDataAck` contiene estado compuesto de usuario/cuerpo y equipo, con longitudes variables (`apariencia-estado-coop.md` §§3/5). En esta investigación **no se ha localizado un offset universal de apariencia en ActorLoadData**, ni una copia plana exacta de los 271 bytes de los Ack 2622 completos dentro de ninguno de los payload ActorLoadDataAck disponibles en las ocho capturas (`a6_capturas.py`, sin filtrar por una supuesta clase). La captura versión 1 limita esa búsqueda a los bytes conservados; el JSON cuenta expresamente sus cargas truncadas. El resultado no demuestra que falte la apariencia: su representación interna usa vectores y su aplicación puede completarse con Ack 2622. La prueba de un jugador que entra tarde debe decidir si basta el arranque nativo o necesita una instantánea estética adicional. No parchear a ciegas el paquete de carga.

[S] Diseño prudente: el servidor conserva por miembro `{revisionApariencia, bloqueNativoValidado, generación}`; tras commit envía la revisión aprobada a observadores y al propio cliente mediante un canal de control del mod, y entrega también la última revisión cuando aparece el actor. Si se aprovecha el Ack nativo, construirlo con las dos claves correctas y a través de la cola/hilo ya establecidos; nunca inventar un hook en `.xtls`. Evitar dobles aplicaciones y descartar revisiones/épocas antiguas. AP-2 decide si hace falta esta capa o el flujo nativo ya cubre todos los observadores.

## 4. Abrir el editor y qué significa «crear»

La pantalla nativa de barbería tiene selección, previsualización, aplicar, guardar y cancelar en el recurso HTML ya extraído. `0xF980A0` restablece listas de la vista, identifica el personaje/mercenario y prepara previsualización por `0x5F75B0` [E]. **No se ha demostrado una llamada pública y segura «abrir barbería desde cualquier lugar»** con todos sus prerrequisitos: actor, cámara, escena, menú, coste y control. Las funciones de inicialización/actualización de UI no son por sí mismas una orden de apertura; llamar su vtable manualmente no es un diseño aceptado.

[S] Camino inicial propuesto: usar el servicio nativo de barbería con autorización de edición. **Primero debe comprobarse que ese servicio está disponible justo en la frontera de exploración libre certificada por A5.** Si requiere progreso posterior, no adelantar la plantilla ni desbloquear misiones silenciosamente para hacerlo accesible. En tal caso, la primera elección estética queda pendiente, o exige investigar presets validados que respeten ese estado inicial. Sólo si la compatibilidad se demuestra, el mod puede guiar al jugador a la interacción real de barbería antes de entrar en acción. Después de AP-1 puede añadirse un panel propio que invoque el mismo evento de apertura ya identificado mediante una captura/observación legítima. Mientras no se haya resuelto ese evento, el editor universal desde el mod queda **pendiente**, sin RVA de enganche inventada. Una alternativa propia sería elegir presets previamente validados, conservando el bloque nativo y dejando el renderizado/aplicación al cliente; tampoco está implementada.

`CreateCharacterReq` **sí tiene handler**, pero es otro flujo: `0x2A1D380` lee `u8`, un u32, longitud u32 del nombre y nombre UTF-16 limitado a **30** unidades más terminador (`0x2A1D4B0`), y otro u32 antes de llamar al thunk `0x2C48120 → .xtls 0x10890B30` [E/E-x]. El cuerpo protegido llega a `0x2B7FDC0 → .xtls 0x1069F640`; esta ruta toca la lista de lobby, el byte de un jugador `0x6CEEDE8` y, para parámetro de ranura distinto de −1, el gestor global con bandera `+0x60` y `0x186F920` [E-x]. No equivale a «abrir el editor», no es puramente cosmética y no se probó en capturas. **No usar CreateCharacterReq cada semana.**

[S] Se propone implementar primero el alta mediante SteamID, plantilla A5 con identidad A4 propia y primera elección estética. El mundo ya existe; esa operación no volvería a ejecutar el prólogo ni reiniciaría facciones/stages. La primera confirmación estética iniciaría el intervalo semanal. Esta propuesta cubre alta y reedición; no resuelve por sí sola la recreación completa o el creador libre del objetivo, que permanece pendiente según §1.

**Dependencia de código existente [S sobre implementación, fuente propia]:** `src/mod/session2/server.cpp:451` rechaza nombres que empiezan por `Save`. Por ello Req 2676 hoy queda fuera del túnel. La función semanal exige una excepción específica, después de validar sesión/SteamID, permiso, slot/actor, tamaño exacto y lista de valores permitidos. Mantener bloqueadas las demás Req de guardado, CrossSave y desarrollo. No se modificó ese código.

## 5. Una personalización cada siete días del servidor

Todo este apartado es **[S], diseño nuevo**, no una función semanal encontrada en el exe. Se apoya en la identidad autenticada A4, la publicación de generaciones A3/D-24 y el formato/efectos de §3. El coste de almacenamiento es una fila por registrado; treinta registros no requieren treinta actores o ranuras abiertas.

### 5.1 Regla inequívoca

Propuesta para la decisión del coordinador: **intervalo móvil de 604.800 segundos (7×24 h)** desde la última confirmación durable, en UTC del servidor. No se reinicia cada lunes, al cambiar de zona horaria, al reconectar, al cambiar de ranura o al reinstalar el cliente. La creación inicial autoriza una primera elección; después se aplica el mismo intervalo. Previsualizar y cancelar son ilimitados. Un envío repetido o un fallo no gastan otra semana.

El perfil incluye los parámetros estéticos admitidos y el alias. Los tintes persistentes sólo se incluyen cuando sus rutas separadas de §2 estén auditadas y puedan formar una misma transacción. Permitir varias Req como pasos de **una sola sesión de edición autorizada**, con un único commit final; contar cada clic o cada Req 2676 como una personalización frustraría el editor. Cambiar armas/armaduras por el inventario conserva su comportamiento normal y no reinicia el periodo. Modelos, raza y cuerpo no validados quedan fuera de la lista blanca.

Registro mínimo propio por `(worldId, SteamID64)`: `{revisionPerfil, hashPerfil, ultimaConfirmacionUTC, proximaFechaUTC, idOperacionConfirmada, generacion, estadoOperacionPendiente}`. El derecho se mantiene en el catálogo del servidor, fuera del blob que puede aportar/restaurar un cliente. `characterNo`, slot y actorKey son referencias de ejecución, no la clave del derecho. A4 identifica colisión de caché por characterNo; no reutilizar el valor compartido `0x7FFFFFFFFFFFFFFF` como identidad de diez miembros.

### 5.2 Reloj y cambios de hora del PC

`FieldTime` es el reloj del mundo con escalado y restauración de partida (`hora-del-dia.md` §§1–3; A3 §5); **no interviene en esta regla**. El cliente sólo muestra una cuenta atrás recibida del servidor y no envía su propia fecha como autoridad.

Durante un arranque, fijar una base UTC validada y avanzar con reloj monotónico. Persistir periódicamente la última UTC aceptada y la fecha de cada commit. Un cambio del reloj civil hacia atrás nunca reduce ese máximo ni adelanta la siguiente autorización. Un salto brusco hacia delante tampoco concede automáticamente siete días: congelar nuevos derechos hasta reconciliar la hora con una fuente confiable o una decisión administrativa registrada. Reiniciar no se trata como semana nueva.

El máximo histórico por sí solo **no evita** un salto fraudulento hacia delante; un reloj monotónico por proceso **no mide** lo ocurrido entre reinicios. Para contar tiempo offline con garantías, se necesita validar UTC al arrancar mediante una fuente externa confiable. Si falta o es incoherente, política prudente: dejar jugar y suspender sólo nuevas confirmaciones estéticas hasta resolverlo, mostrando la fecha pendiente. En un servidor doméstico administrado por alguien que también puede cambiar reloj, ficheros y ejecutable, no existe garantía antifraude contra ese administrador sin una autoridad externa. No se promete esa garantía.

### 5.3 Confirmación recuperable

1. Autenticar SteamID; resolver la sesión, época, personaje y slot; comprobar que el mundo está estable y que el intervalo venció. Reservar una operación exclusiva para ese miembro, con nonce/ID de operación y plazo de edición. La reserva no consume la semana.
2. Previsualizar localmente. Al confirmar, validar tamaño de 271 B, semántica/rangos de campos ya conocidos y compatibilidad del protagonista; el servidor decide los objetos/colores admitidos, sin aceptar rutas, modelos arbitrarios, estadísticas o identidades del cliente.
3. Registrar durablemente una intención con hashes de estado anterior/propuesto. Aplicar en el hilo correcto mediante ruta nativa aprobada, bajo la coordinación de carga/guardado A3; no interpretar un retorno 0 del handler como éxito durable. Los fallos nativos deben resolver la UI, no dejarla esperando indefinidamente.
4. Producir y verificar nueva generación. Si hay gasto nativo de recursos o modifica otro estado, incluir esos archivos en el mismo grupo coherente. **Publicar perfil, generación y recibo del derecho como una única decisión recuperable**. Sólo aquí avanza `ultimaConfirmacionUTC` y se fija `proximaFechaUTC`.
5. Entregar recibo al cliente y difundir la revisión. Reintentos con el mismo ID devuelven el recibo anterior; no vuelven a consumir recursos ni el derecho. Tras fallo antes de publicar, recargar la generación anterior y cancelar la intención. Tras fallo después de publicar y antes de responder, recuperar la operación por el manifiesto y reenviar el recibo.

El ledger de derechos no debe retroceder por restaurar una generación antigua del mundo. Al restaurar, resolver explícitamente el perfil publicado más reciente frente al blob anterior; no otorgar otra semana por la discrepancia. Conservar copia estética/revisión y registro administrativo para una restauración deliberada. Ningún protocolo garantiza esto sólo con «última fecha» dentro de la partida; AP-6 lo comprueba mediante fallos y recuperación.

**Riesgos:** identidad no autenticada, Req alternativas que eluden la puerta, doble clic, edición simultánea desde dos clientes, gasto nativo duplicado, crash entre aplicación y commit, reloj incoherente. **Pruebas:** AP-1/2/6; repetir también con el anfitrión si juega: su loopback no puede quedar exento de la regla por no pasar por el túnel.

## 6. Distinguir hasta diez protagonistas iguales

### 6.1 Identidad visual propuesta [S]

Mantener una asociación aprobada por el servidor `SteamID → alias, número corto, color/forma` y una asociación efímera `{época, actorKey → SteamID}`. Mostrar alias y número cerca del actor, y el mismo número/forma en minimapa. No depender sólo del color: diez colores próximos son difíciles de diferenciar y no cubren daltonismo. Aplicar distancia/oclusiones y agrupar etiquetas superpuestas; no confundir el alias con el nombre narrativo Kliff.

La necesidad de etiquetas nuevas está respaldada por el informe de apariencia §2.7: no se ha demostrado un nameplate nativo para otros jugadores. El Ack 2484 no prueba que dibuje uno. La opción de menor dependencia es el overlay propio; debe tener una proyección/cámara válida y hacerse visible sólo para actores observados, salvo marcadores globales voluntarios acordados por el mundo. Un jugador lejano no tiene por qué tener un actor cargado en cada cliente.

`src/mod/net/puppet_minimap.cpp` es **código propio existente**, no evidencia [V] de ocho jugadores: filtra exactamente `kPuppetPlayer=0xA010F001`, usa un único `g_offset` y responde con icono/subicono/flags. Por tanto no basta con activarlo para diez identidades. Habría que sustituir conceptualmente ese filtro único por la tabla de actores nativos autorizados; no se ha implementado.

En el exe, el punto existente de decisión de icono es entrada `.rsrc 0x9E89C0`, y tiene firma única de esta build (§7). El código actual busca iconos en `[[[[exe+0x6D69528]]+8]+0x270]` y alternativa, según `offsets.h`/`puppet_minimap.cpp`; no asumir que esa tabla tenga diez variantes cromáticas útiles ni que escribir un icono resuelva visibilidad en todos los casos. AP-4 compara icono nativo, overlay y ausencia por distancia.

### 6.2 Escala de la función estética

| Personas que juegan | Perfiles distintos | Destinatarios remotos por una edición, si todos se observan | Tráfico mínimo de un Ack 2622 a esos observadores | Criterio de UI |
|---:|---:|---:|---:|---|
| 2 | 2 | 1 | 284 B | Alias y dos marcas distinguibles. |
| 4 | 4 | 3 | 852 B | Cruce de etiquetas y salida/reentrada. |
| 8 | 8 | 7 | 1.988 B | Ocultación/agrupación, ocho números y formas. |
| 10 | 10 | 9 | 2.556 B | Diez estilos de marca verificables; no diez colores solos. |

Las cifras son **[S] aritmética** `284×(H−1)` y sólo incluyen un paquete Troc, sin transporte, fiabilidad, instantáneas de equipo ni copias usuario/cuerpo; no son consumo total medido. La personalización semanal no parece el límite dominante de aforo. Un anfitrión técnico que observe también suma un destinatario y consume sesión, conforme A0/D-22. Con diez sesiones totales y ancla separada sólo se diseñan nueve personas externas; no se presupone una undécima sesión para esta tabla.

El catálogo puede contener treinta o más registrados aunque sólo haya hasta diez sesiones activas. Procesar confirmaciones estéticas una por una al principio, porque la persistencia usa gestor global A3; no fingir diez commits paralelos porque existan diez perfiles. Estando dispersos, entregar sólo a observadores y guardar la revisión para el próximo aparecido reduce tráfico, sin cambiar el derecho semanal.

## 7. Puntos legibles y límites de intervención

No se propone enganchar ninguna dirección `.xtls` ni thunk cuyo cuerpo sólo salta allí. No se modifica el exe. Los siguientes son **candidatos futuros**, no hooks implementados ni ABI validada en vivo; todas sus firmas exactas deben fallar de forma cerrada al cambiar la build.

| Punto | Uso propuesto y limitación | Entrada/bytes/firma |
|---|---|---|
| `0x298DC30` | Puerta servidor de Req 2676, antes de aplicar. Permite observar TrocParams y cubrir anfitrión e invitados si se instala en el despacho común; necesita respuesta de rechazo coherente y autorización A4. | Entrada `.rsrc`, `.pdata` `0x298DC30–0x298DFE8`; primeros 16 B `48 89 5C 24 08 48 89 74 24 10 55 57 41 54 41 55`. AOB de 40 B única en todo exe en `data/vision-a6-estatico.json`. |
| `0xB6E260` | Observar la confirmación nativa del cliente y relacionarla con la operación estética. No es la orden para abrir el editor, ni puede decidir el permiso por su cuenta. | Entrada `.rsrc`, `.pdata` `0xB6E260–0xB6E3B6`; primeros 16 B: `48 89 5C 24 08 48 89 74 24 18 89 54 24 10 55 57`; AOB de 40 B única en el mismo JSON. |
| `0x9E89C0` | Generalización futura del icono por actor; conservar lógica nativa cuando no es miembro conocido. | Entrada `.rsrc`, `.pdata` `0x9E89C0–0x9E8D01`; primeros 16 B: `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55`; AOB de 40 B única en el mismo JSON. |

Las firmas del JSON son exactas, con rel32 incluidos cuando aparece alguno; prueban unicidad en el exe 2976, no portabilidad a otro parche. No se propone un hook nuevo en `0x298A3A0`: sus primeros 40 bytes tienen **15 coincidencias**; un prólogo aparentemente válido no basta como AOB única. Tampoco se propone un hook en la inicialización de la barbería para forzar su apertura sin resolver los prerrequisitos.

La entrega conserva metadatos de 40 funciones, 333 cadenas relevantes, SHA-256 del exe y hashes de los tres lectores propios. Los intervalos de funciones sin `.pdata` en esos metadatos son provisionales de un máximo de 16 B; no demuestran límites reales. Esa limitación no afecta a las tres entradas propuestas, que sí tienen `.pdata` propio. Los volcados completos consultados permanecen en el área A6, identificados por RVA.

## 8. Pruebas futuras, ordenadas y con criterio de cierre

Estas pruebas **no se ejecutaron**. RPT-0/D-28 de `vision-a7-transversal.md` debe cerrarse antes de E1 y de provocar errores o cierres deliberados; esta remisión adopta la dependencia transversal y no constituye una auditoría adicional de los reportes de fallo. E0–E3/E3b, cliente remoto estable e identidad/guardado aislados de A3/A4 son prerrequisitos para las que modifican personajes persistentes. No pedir al usuario activar una opción todavía inexistente. Toda futura escritura se hará en copias de prueba bajo la raíz aislada del servidor; P6 conserva protegidas las partidas reales y CrossSave permanece bloqueado. AP-6 requiere expresamente esa puerta antes de cualquier fallo provocado.

| Prueba | Acción futura | Éxito y decisión |
|---|---|---|
| **AP-1: editor y campo por campo** | Con un personaje de prueba y captura autorizada, abrir barbería normal, cambiar un control, cancelar; luego confirmar. Repetir pelo, color, cejas, tatuaje y tintes por separado. Registrar 2676, 2545/2800, Ack, errores y cambios del componente. | Determinar controles realmente disponibles, mapear A/B/C/D, coste nativo, si la UI envía múltiples Req y distinguir preview/aplicación/guardado. No habilitar datos arbitrarios antes de esto. |
| **AP-2: réplica y persistencia** | Dos personas en el mismo campo; B permanece mirando mientras A edita. B sale del radio y vuelve; A reconecta; después entra un observador C si ya se superó el hito 4. Comparar hash/revisión/imagen, no sólo retorno del handler. | Todos ven la misma revisión sin alterar a otro personaje. Decide si los Ack nativos bastan o se necesita la instantánea/difusión propia. Con dos PC, el tercer cliente sintético sólo comprueba bytes; no sustituye esa tercera observación visual. |
| **AP-3: equipo y tinte** | Cambiar prenda, teñir, quitar/poner, montar/desmontar y reaparecer con observador, y repetir tras reinicio. | Mismo aspecto y UI local coherente; sin colisión characterNo/itemNo ni pérdida de tintes. Si falla tinte, no prometerlo en la personalización compartida inicial. |
| **AP-4: identidad visual** | Cruces, oclusión, mapa/minimapa, viaje, reconexión y cambios de actorKey en 2/4/8/10 según etapa habilitada. | Alias, número y forma siempre pertenecen al SteamID correcto; no marcas huérfanas ni sólo color. Aforo UI no demuestra aforo de simulación. |
| **AP-5: otros protagonistas** | Sólo después de Kliff estable: cada personaje candidato con ropa, armas, combate, montura, death/respawn, escena y retorno al lobby. | Cada combinación compatible y durable, sin trasladar progreso/misiones al personaje equivocado. Un modelo que carga no cierra la prueba. |
| **AP-6: siete días y recuperación** | Reloj de prueba controlado en entorno aislado: −1 d, +8 d, reinicio, cambio de zona horaria; confirmar dos veces; duplicar login; fallos antes/después del commit y rollback de mundo. | Cero derechos extra por manipular cliente/reloj/slot; cancelación o fallo no consume; commit publicado recupera un único recibo; ni gasto nativo ni semana se duplican. Con hora no confiable sólo se bloquea la confirmación estética. |

## 9. Diez afirmaciones prioritarias para revisión independiente

| ID | Afirmación falsable y alcance | Direcciones/script |
|---|---|---|
| A6-C1 | Req 2676 consume 271 B y despacha el componente +20 del actor de TrocParams; no lee una cuenta u64 para resolverlo [E]. | `0x298DC30`, bucles 0x10/0xFA, `0x298DDE9–0x298DE11`. |
| A6-C2 | El writer 2676 obtiene slot mediante vf168, requiere que sea distinto de FF y envía sólo conectado [E]. | `0xB6E260`, `0xB6E2E8–0xB6E338`. |
| A6-C3 | El envío explícito al terminar la edición es ToTarget, no ToNear [E; sin afirmar ausencia de efectos indirectos]. | `0x294B21B`, `0x294B230`, `0x294B24F`. |
| A6-C4 | La ruta alternativa protegida emite ToNear sólo con resultado previo cero y r9b inicial no nulo [E-x]. | `.xtls 0x10446580`, `0x104465BA–0x10446614`. |
| A6-C5 | Ack 2622 consume 279 B, comprueba K1 y aplica mediante búsqueda K2 [E/E-x]. | `0xB71530`, `0xB717B4`, `0xB717FF`, `0xB7184B`; thunk `0x1795B40`. |
| A6-C6 | El bloque se representa como dos vectores de 16 y 250 bytes más un u32, y se almacena en Status+388 [E]. | `0x178EA70`, `0x1795A60`. |
| A6-C7 | Producción de CustomizationSaveData desde Status+388 ocurre dentro de `.xtls`, no en cuerpo legible de 0x1791DA0 [E/E-x]. | `0x1791DA0 → 0xC73A550`, `0xC73A75B–0xC73A7B7`, `0x179E720` RTTI. |
| A6-C8 | CreateCharacterReq contiene nombre limitado a 30 unidades UTF-16 y es ruta distinta de Req 2676 [E/E-x]. | `0x2A1D380`, `0x2A1D4B0`, `0x2A1D5BC → 0x2C48120 → 0x10890B30`. |
| A6-C9 | La ruta CreateCharacter llega a lista de lobby/gestor global +60 y llamada a 0x186F920 condicionada por ranura, por lo que no es sólo edición estética [E-x]. | `.xtls 0x1069F640`, `0x1069F6DF–0x1069F756`, `0x1069F789–0x1069F866`. |
| A6-C10 | BarberShop tiene vtable con inicialización en 0xF97B10, preparación en 0xF980A0 y actualización en 0xF98380; los tres candidatos propuestos son entradas .rsrc con AOB únicas [E]. | vtable `0x56C5E80`; `a6_estatico.py`; `data/vision-a6-estatico.json`. |

Comprobaciones complementarias recomendadas: volver a contar las ocho capturas, refutar la supuesta difusión de una edición, y revisar el filtro `Save*` del mod. Las afirmaciones sobre semana, atomicidad del recibo, modelos no probados y aforo son diseños [S]; no se convierten en [E] por aparecer en esta tabla.

## 10. Fuentes externas y límites

- **F1 [R]:** Pearl Abyss, [notas oficiales 1.13.00, 3 de julio de 2026](https://crimsondesert.pearlabyss.com/en-US/News/Notice/Detail?_boardNo=105). Apoyan que los protagonistas tienen equipamiento/restricciones propias y que hay tintes. Consultadas en esta investigación; no identifican las RVAs del exe local.
- **F2 [R]:** Pearl Abyss, [notas oficiales 1.08.00, 22 de mayo de 2026](https://crimsondesert.pearlabyss.com/en-US/News/Notice/Detail?_boardNo=93). Describen previsualización de estilos en barbería. No se toma de ahí el formato de red ni la disponibilidad exacta en 2976.
- Fuentes internas principales: `apariencia-estado-coop.md`, `companeros-mercenarios.md`, `opcion-d-p9.md`, `vision-a3-guardado-mundo.md`, `vision-a4-identidad.md`, tabla Troc y `src/mod/net/puppet_minimap.cpp`. Los niveles [V] de informes antiguos que describen desensamblado se han reclasificado aquí como [E]/[E-x].

Revisión independiente de §9 cerrada en §11. No hay evidencia nueva en vivo.

## 11. Revisión independiente de las diez afirmaciones

**Resultado: 10 confirmadas en su alcance local; ninguna prueba en vivo.** Se intentaron refutar las afirmaciones leyendo nuevamente el ejecutable, sin reutilizar los volcados del autor. El hash SHA-256 obtenido es `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`, PE `0x6AB28F00`. Procedimientos propios: `C:\Emulador CD\build\research\vision\verificacion\a6\verifica_estatico.py` y `verifica_capturas.py`; resultados y hashes en `data/vision-verifica-a6.json`. El primero recorre `.pdata` con sus fragmentos encadenados, lee RTTI directamente del PE y busca las firmas en el archivo completo; no emplea la caché RTTI del autor. Los thunks sin `.pdata` sólo se interpretan hasta su salto.

| ID | Resultado | Contraste independiente y límite conservado |
|---|---|---|
| A6-C1 | Confirmada [E] | Lecturas de 4+1+16+250 bytes en `0x298DCE4–0x298DDBA`, consumo exacto en `0x298DDBC–0x298DDC4` y actor de TrocParams en `0x298DDE9–0x298DE11`. Se precisa «no lee una cuenta u64 para resolver el actor»; no se atribuye semántica exhaustiva a los campos opacos. |
| A6-C2 | Confirmada [E] | `0xB6E2E8` llama vf168, `0xB6E2EE` compara con FF; `0xB6E30D–0xB6E338` exige `ClientNet+112 != 0` para PostPacket. `0x24450D0` vuelve a demostrar 4+1+16+250 bytes. Esto acredita la rama de envío, no autenticación ni entrega. |
| A6-C3 | Confirmada [E] | En `0x294B21B–0x294B24F` se coloca vtable `0x5B25438`, cuyo RTTI es `ToTargetSendTargetFinder`, y se toma la sesión mediante vfE8. No se afirma ausencia universal de difusión indirecta ni commit durable. |
| A6-C4 | Confirmada [E-x] | `0x10446597` conserva r9b; `0x104465AE–0x104465BC` exige resultado cero y flag no nulo. La vtable `0x5B25418` leída directamente es `ToNearSendTargetFinder`. Se añadió explícitamente la condición de éxito que precede al flag. |
| A6-C5 | Confirmada [E/E-x] | Dos lecturas u32 distintas (`0xB715F7`, `0xB7168F`); K1 alimenta la comprobación/búsqueda inicial y K2 la búsqueda `0x8AE940` en `0xB717FF`. Se aplica por el componente +20 y thunk `0x1795B40 → 0xC759040`. K1 sólo llama a la comprobación `0x8B53C0` bajo sus condiciones de tipo/valor; no es una puerta incondicional para toda clave. |
| A6-C6 | Confirmada [E] | `0x178EACF` fija 16, `0x178EB1D` fija 250 y `0x178EB30` copia u32. `0x1795A77–0x1795B24` aloja/copía la estructura apuntada por +388. No fija offsets del archivo cifrado ni demuestra que el byte B carezca de efectos en otras rutas. |
| A6-C7 | Confirmada [E/E-x] | La vtable `ServerStatusActorComponent` `0x5B203F0`, slot D0, apunta a `0x1791DA0`, cuyo salto llega a `.xtls 0xC73A550`. `0xC73A75B–0xC73A7B7` lee +388, obtiene metadatos `0x179E720` y copia con `0x178EC60`. RTTI de `0x57C78C8` confirma `CustomizationSaveData`. Producir el objeto no confirma escritura durable. |
| A6-C8 | Confirmada [E/E-x] | `0x2A1D494` lee la longitud u32; `0x2A1D4B0` rechaza valores >30 y `0x2A1D514–0x2A1D51B` exige terminador UTF-16. Llama a `0x2C48120 → 0x10890B30`, ruta distinta a Req 2676. Treinta unidades UTF-16 no equivalen necesariamente a treinta caracteres visibles. |
| A6-C9 | Confirmada [E-x] | `0x10890C0A → 0x2B7FDC0 → 0x1069F640`: rama del byte `0x6CEEDE8`, trabajo sobre +18 del lobby, comprobación de ranura !=−1 y toma de +60 antes de `0x186F920` (`0x1069F789–0x1069F866`). Si +60 está ocupado devuelve error; no se observa una cola. Acredita efectos ajenos a la estética, no un alta multijugador validada. |
| A6-C10 | Confirmada [E] | RTTI de `0x56C5E80` identifica BarberShop; slots +08/+80/+118 apuntan a `0xF97B10/0xF980A0/0xF98380`. Los tres candidatos `0x298DC30`, `0xB6E260` y `0x9E89C0` coinciden con entradas `.pdata` en `.rsrc` y sus AOB de 40 bytes tienen una coincidencia cada una en todo el exe. Esto no identifica una orden segura de apertura del editor. |

**Capturas [V]:** el lector independiente recorrió las ocho, reproduciendo un Ack 2622 de 279 bytes de payload y un Ack 2869 por archivo. En todos, K1=K2=`0xA0100001`; los ocho bloques de 271 bytes también tienen el mismo hash. No encontró Req 2676 ni los demás mensajes de edición enumerados en §3.3. Se reprodujeron 18.329 cargas, 3.995 truncadas, cero registros rotos y cero coincidencias planas del bloque estético completo en sus payloads disponibles. Por tanto son ocho muestras del mismo aspecto, sin evidencia de edición o réplica entre jugadores.

**Código propio leído:** `src/mod/session2/server.cpp:451` clasifica como bloqueado cualquier nombre que empiece por `Save`; `SaveCharacterCustomizationReq` satisface el filtro. `src/mod/net/puppet_minimap.cpp` mantiene el único actor `0xA010F001`. Estas comprobaciones de código no se convierten en [V] de funcionamiento. Ningún fichero del mod se modificó.

Los recursos de UI extraídos conservan [R] mientras no se establezca su build. No se revalidaron aquí las fuentes web ni se usan para acreditar disponibilidad exacta en 2976. Siguen pendientes AP-1/2/6, la apertura segura desde el mod y cualquier recreación completa de personaje.
