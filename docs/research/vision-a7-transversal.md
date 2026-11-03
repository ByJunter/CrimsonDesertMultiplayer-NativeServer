# Visión A7: parches, pruebas y riesgos externos

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`. Todas las direcciones son RVA para esta build. Investigación de solo lectura del 2026-10-08: no se ejecutó el juego ni el mod, no se compiló, desplegó o alteró ningún guardado. Estado: **investigación cerrada y verificada independientemente; correcciones en §8**.

Convenciones: [V] captura/observación histórica identificada; [E] desensamblado `.rsrc`; [E-x] `.xtls`; [S] propuesta o inferencia; [R] fuente externa no contrastada con el exe. La lectura de fuentes del mod se identifica como estado del repositorio, sin convertirla en validación en vivo. Scripts y volcados: `C:\Emulador CD\build\research\vision\a7\`.

## 1. Resultado y alcance

[S] La continuidad de un servidor requiere una puerta de compatibilidad por build, pruebas que diferencien protocolo de simulación real y una distribución controlada. Encontrar una AOB única no demuestra que la función conserve contrato, y diez sesiones de replay no representan diez clientes que simulen IA/física. Fuentes técnicas: [A0](vision-a0-base.md), [A1](vision-a1-mundo.md), [A2](vision-a2-servidor.md), [A3](vision-a3-guardado-mundo.md), [A4](vision-a4-identidad.md).

[E/E-x] El hallazgo que cambia el orden de pruebas es una **ruta de reporte de fallos de guardado con archivos `*.save` adjuntos**, separada de CrossSave. P6 bloquea escrituras, pero permite leer partidas: por ello no basta para contener estos reportes. Antes de repetir la parte de E1 que induce un guardado fallido, hace falta resolver **RPT-0** (§4.3). No se observó ningún envío durante esta investigación. Evidencia: `.xtls 0xEF51A63 → .rsrc 0x2356270 → 0x1317D40`; [datos A7](data/vision-a7-estatico.json).

[R/S] La distribución pública añade riesgos contractuales y de seguridad distintos de la viabilidad técnica. Las fuentes primarias específicas están en §5. Este informe no publica, distribuye ni contacta con terceros, ni convierte un diseño técnico en autorización para operar el servicio.

## 2. Compatibilidad entre parches

### 2.1 Detección de build y comportamiento ante una actualización

El script `vision/a7/estatico.py` releyó el exe y obtuvo **384.521.624 bytes**, SHA-256 **`57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`**, PE timestamp `0x6AB28F00` y base preferida `0x140000000`. Coinciden con [el manifiesto común](data/vision-manifiesto.json). Son lecturas del archivo, no una ejecución [evidencia estática de datos PE]. La etiqueta 1.0.0.2976 viene del contexto/build registrada; no se deduce del timestamp.

**Estado del repositorio:** `src/mod/mod.cpp:98` registra versión, base, tamaño de imagen y timestamp. `session2/server.cpp:490` y `guest.cpp:265` rechazan base/timestamp distintos y comprueban cabeceras concretas. `server.cpp:509–538` comprueba también slots de las vtables de los buscadores. No se encontró verificación del SHA-256 del exe en esas puertas. El logger auxiliar `src/mod/game/game_errors.cpp:83` comprueba base y el primer byte del punto antes del resto de su instalación; esa condición aislada no certifica compatibilidad del juego. El reproductor de capturas valida timestamp 2976 (`replay.cpp:178`), no todo el perfil del payload.

[S] Perfil de compatibilidad propuesto, **sin implementarlo**:

1. Identidad del archivo: versión de recurso, timestamp, tamaño, hash completo, secciones y flags PE; comparar contra un manifiesto aprobado. Una fecha igual o nombre de archivo igual no basta. Registrar también Steam build ID si está disponible, como metadato, no como sustituto del hash.
2. Antes de activar cualquier pieza que lea/escriba el motor, exigir perfil exacto compatible y que las verificaciones locales de bytes/vtable/estructura coincidan. Si no, dejar la opción D sin activar, explicar qué comprobación falló y preservar las partidas. No «probar a ver» el RVA viejo.
3. En el saludo entre peers, acordar hash/perfil de juego, versión de protocolo del mod, versión de esquema Troc y esquema del guardado. `protocol.h:31` contiene actualmente versión 8; añadir campos o semántica al túnel exige su revisión/subida (D-21). Rechazar mezcla de builds antes del login nativo.
4. Un parche inicia **reanálisis**, no reparación automática: regenerar candidatos de firmas/RTTI, comparar contratos y layouts, revisar cambios del guardado y revalidar E0→E1→E2 con las puertas de privacidad de §4. Mantener generaciones anteriores según A3/A4 y migrar una copia de laboratorio; no degradar una partida nueva abriéndola con el motor anterior.

Riesgo: un prólogo conservado puede ocultar un nuevo contrato, un offset de objeto cambiado o un handler de igual nombre con otro payload. Prueba futura **PAT-1**: un manifiesto de prueba con un solo hash/timestamp/byte alterado debe rechazar activación/admisión antes de escribir memoria. Para una build real nueva, el usuario debe repetir las sondas de estructuras y un round-trip aislado de guardado; hasta entonces esa build figura como no soportada. Es el mismo requisito con 2/4/8/10 personas.

### 2.2 Generación de AOB y mapa de mensajes por nombre

[E] La instancia estática de `MoveActorReq` está en `0x6A6FBA0`; su inicializador **`0x221D45–4C`** escribe la vtable derivada `0x5B24CF8`. En el archivo tiene inicialmente la vtable base `0x5726410`. Leer únicamente la vtable que hay en disco no permite nombrar ese handler. [E] `Troc::Find` **`0x143B351`** aplica `test [handler+8], máscara`, no igualdad del tipo: esto explica por qué el interno 2319 también se encuentra con máscara servidor 4. Fuentes: `00221d20.asm`, `0143b2c0.asm`; `troc-rtti.md`; D-16.

El contraste nuevo de `estatico.py` comprueba las **1.115 filas**, **1.115 nombres distintos** y **1.115 opcodes distintos** del CSV 2976: en todas coinciden `kind`, opcode de la instancia, `vf2` de la vtable y nombre RTTI; las 1.115 instancias llevan la vtable base en disco. El recuento es evidencia estática de datos y RTTI, apoyada por inicializadores [E]; **no verifica los 1.115 formatos de payload ni sus permisos**. Resultados resumidos en [vision-a7-estatico.json](data/vision-a7-estatico.json); detalle local `tabla_comprobada.json`.

[E] La recepción del servidor sí consulta canal y límites de tamaño: `0x2773D49` lee `handler+0x14`, y `0x2773DB4–BF` compara mínimo/máximo `+0x18/+0x1A`; `0x277402C–33` decide la ruta diferida con `+0x1C`. Estos metadatos tienen que viajar en el catálogo de la build, además del nombre/opcode. No implican autorización de dueño ni validación de significado (contraejemplo 2181 de A2).

[S] Automatización propuesta:

- Reconstruir `TypeDescriptor→COL→vtable→vf1/vf2` desde el PE actual; enlazar cada vtable a su instancia mediante el inicializador que la escribe, y contrastar con funciones de registro. Extraer tipo, canal, tamaños y flags desde esa instancia. Conservar origen/RVA y hash de los bloques que justifican cada fila.
- Diferenciar nombre completo y lado; detectar duplicados, desapariciones y cambios de tipo antes de generar el mapa. El rango fijo `0x6A51AE0..0x6A82C80` del escáner actual también cambia potencialmente con un parche: no tratarlo como descubrimiento universal.
- Comparar por **nombre**, pero exigir revisión de lectores/escritores de payload para los mensajes usados. No reasignar solamente los números en guiones viejos: el significado o longitud de campos puede variar. Los mensajes nuevos no se admiten automáticamente por tener sufijo Req.
- Para una función candidata: comprobar sección ejecutable `.rsrc`, entrada real mediante `.pdata` **siguiendo unwind encadenado**, bytes iniciales y referencias/RTTI/llamadores. Generar AOB en límites de instrucción; enmascarar desplazamientos rel/RIP sólo de forma controlada; contar coincidencias en **todo el exe** y conservar la cardinalidad. Una cardinalidad de 0 o mayor que 1 bloquea el candidato.
- [E] Caso concreto de falsa confianza: `0x2A75880` y `0x2A759B0` tienen el mismo prólogo y estructura; enmascarar los rel32 de una firma puede hacerla coincidir en ambas. Diferenciarlas por `ToNearExceptOneSelf/Control` y sus vtables, más semántica local. El [catálogo común](data/vision-firmas-2976.json) conserva AOB exactas únicas para 2976, sin promesa de portabilidad.
- [E] `0x35BAD4` pertenece a la función `0x35B800` y es interior: sigue siendo un punto **mid** de P8, nunca una entrada para inline. Un `.pdata` partido tampoco crea nuevas entradas válidas: `Troc::Find` empieza en `0x143B2C0` aunque tenga un fragmento posterior en `0x143B2DF`.

No se propone ningún hook nuevo en este informe. Los candidatos existentes de A0–A4 conservan sus AOB/bytes publicados; ningún salto desde `.rsrc` a `.xtls` convierte el cuerpo protegido en un lugar admisible para enganchar. Prueba futura **PAT-2**: regenerar el mapa de 2976 sin usar como autoridad sus RVAs guardados, reproducir las 1.115 correspondencias y rechazar deliberadamente la firma ambigua de los buscadores. Con un parche, toda fila cambiada usada por el mod requiere revisión humana y prueba del efecto, no sólo Ack de éxito.

### 2.3 Inventario de dependencias fijas del mod

El [inventario CSV](data/vision-a7-rvas-fuente.csv) registra **58 declaraciones/lecturas**, de ellas dos VA base y **53 RVAs distintas**, con archivo/línea/símbolo/sección. Se obtuvieron los `constexpr uintptr_t`, los cuatro `Site` de `horse_diag` y la lectura literal de `FieldTime`; excluye los límites genéricos de punteros. Es una enumeración de esas construcciones, **no prueba de ausencia de otras dependencias semánticas**. La búsqueda amplia guardó 146 líneas con hexadecimales largos en 22 archivos; muchas son claves, máscaras o hashes, no direcciones. Scripts `estatico.py` y `exportar.py`.

| Superficie del código actual | Dependencias principales | Qué puede romper un parche; verificación futura |
|---|---|---|
| P1–P5, `session2/offsets_s2.h` y `server.cpp` | Globals `0x6D69680/0x6D69170/0x6DA67D8`; sesión/vtables; ctor `0x1436EE0`; cola `0x35DF50`; asignador `0x47EFA78`; recepción `0x2773C20`; buscadores; layouts +0x90/+0x1F8/+0xC8 y búfer 0x8030 | Resolver puntos y volver a demostrar vida útil/refcounts/locks/layout con E0–E4. Los offsets de campos, tamaños y número de slots de vtable también son dependencias de build. |
| P8, `guest.cpp` | Send `0x14380B0`, mid `0x35BAD4`, recepción `0x35CA00`, época +0xB0, Q3 +0x28 | Registros vivos del mid, entrega por hilo y formato de trama; repetir G0→G2. |
| Nombres/canales, `troc_sniffer.cpp`, `server.cpp` | Rango estático fijo y campos +8/+C/+14/+16; opcodes de movimiento constantes; filtros por nombre | Regenerar catálogo, comparar payloads y política. El comentario «por RTTI» no elimina el rango fijo. |
| Lectores y HUD, `game/offsets.h` | WorldSystem `0x6D69190`, cámara `0x6C8CF30`, posición/origen, vtables y cadenas de componentes | Sondas de identidad/tamaño y posición coherente; no seguir punteros con un perfil fallido. |
| Apariencia/minimapa/caballo | `0x29ED1F0`, `0x2969BC0`, `0x9E89C0`, `0x691400`, `0x1F63F70`, `0x2329700`, `0x681C80`, `0x682C20`; cuatro puntos de `horse_diag.cpp` | Lectores/escritores, layout de actor y estado de animación. A6 decide qué reutilizar para actores nativos; no activar todo lo histórico. |
| Herramientas de daño/aggro y copias | `aggro_test.cpp`: `0x22141E0/0x2217290/0x22E0A30`; claves sintéticas, hashes de acciones, offsets de payload en módulos de copias | Dependen también de datos y opcodes; muchas piezas no pertenecen al flujo final D (A0). Estar apagadas reduce superficie, no certifica las restantes. |
| Errores/sondas | logger `0x35D8D0`; `server.cpp:664` lee FieldTime `0x6A6D238` literalmente | Incluir incluso constantes fuera de headers en perfil y revisión. |
| P6, Steam, DX12 | P6 resuelve exportaciones de KernelBase; Steam resuelve interfaz/API; DX12 obtiene funciones de render | No dependen de esas RVAs del exe, pero sí de ABI/API, versiones y ciclo de vida. P6 no cubre todo el sistema de archivos ni otro proceso por existir su hook. |

Fuentes: archivos de la tabla; `opcion-d-d0.md`, `opcion-d-p8.md`; A0 y catálogo común. Riesgo transversal: habilitar una pieza auxiliar antigua puede saltarse el nivel de validación de P1–P8. Criterio futuro: una única puerta de compatibilidad y lista explícita de piezas activas, con cero acceso al motor desde piezas no certificadas [S].

## 3. Diseño del banco de pruebas de hasta diez sesiones

### 3.1 Qué existe y qué no

**Estado del repositorio:** `tools/peer_sim/replay.cpp` tiene un `ClientSession`, un `TrocLink` y un `Source` por ejecución; el servidor del mod sólo tiene `g_session2`. El replay por guion espera Ack; por captura reproduce C→S a ritmo temporal y **no espera respuestas** (`CaptureSource::on_server_packet` está vacío). Reescribe la cuenta en 2490, excluye 1001 y una lista de guardados, y omite registros truncados. **No remapea de forma general las claves/IDs del mundo**. El grabador usa `session=2` y cabecera de captura con timestamp 0 (`Recorder::open/write`), por lo que no identifica por sí solo múltiples usuarios ni una captura reutilizable como 2976. No se ejecutaron estos programas.

[S] Lanzar nueve copias de ese programa no transforma automáticamente el mod en multisesión. Hace falta diseño e implementación en ambos lados, con revisión de P1–P8 y A1/A2. El simulador podrá modelar hasta 10 usuarios frente a un anfitrión falso sin juego. En una prueba integrada, un anfitrión real ya consume una sesión: **máximo 9 usuarios sintéticos adicionales**, para U=10. Una décima sesión sintética sobre ese anfitrión produciría U=11 y queda fuera del alcance.

### 3.2 Contrato del futuro `cdmp_peer` multisesión

[S] Arquitectura propuesta, sin cambios en código:

1. Un coordinador mantiene de 1 a 10 contextos independientes: `{idLaboratorio, conexión, época, guion, mapaDeClaves, contadores, estadoDeCarga}`. Una conexión real del transporte selecciona el contexto; **un identificador libre del payload nunca elige una sesión ajena**. Las identidades sintéticas sólo existen en modo laboratorio por loopback, separado del ingreso público y de Steam.
2. Cada contexto usa `ClientSession/TrocLink` propio y buffers/ACK/retransmisiones propios; el anfitrión tiene tabla simétrica de sesiones, con equidad de bombeo, cuota por peer y total, y una sola admisión de carga inicialmente (D-26). Se conservan orden y época al devolver búferes. Desconectar uno no corta ni mezcla los demás.
3. Máquina de estados: `conectado→crypto→usuario→lobby→personaje→campo1→carga1→campo2→carga2→jugando→cerrando`. Avanzar por la respuesta y el efecto esperados, con timeout explícito, no sólo por el reloj grabado. Los dos ciclos proceden de `opcion-d-login.md`; un Ack con retorno 0 no acredita por sí solo un efecto de `DoFrameEvent` (A2 verificada).
4. Aprender claves y series de los Ack de **esa** sesión. Transformar campos tipados por nombre/esquema de mensaje: usuario, jugador, actor de mundo, sesión de movimiento/acción, characterNo y referencias de inventario. Nunca reemplazar patrones de 4/8 bytes en todo un paquete: pueden ser floats, hashes u otro campo. El inventario de ID de A4 es prerrequisito del replay que equipa/adquiere objetos.
5. Replays de movimiento reproducibles y desfasados entre usuarios; escenarios juntos/dispersos, viaje de uno, entrada/salida, dueño de IA que desaparece, paquetes de época anterior, login duplicado y destinatario incorrecto. La IA no se «reproduce» correctamente copiando un cliente de captura sin estado: esos escenarios sólo inspeccionan reacciones de protocolo hasta probar P10 real.
6. Inyección de latencia/pérdida/desorden/duplicación **en el transporte de laboratorio propio**, sin alterar Steam ni los protocolos de terceros. Presupuestos separados de 10/30/100 Mbit/s y RTT 20/50/100/200 ms son perfiles [S], no medidas de la casa. Semilla guardada para reproducir el fallo; no enviar fuzz arbitrario al juego vivo. Casos malformados primero contra parsers/modelos fuera del juego.
7. Captura v3 o sidecar propuesto: hash de build, versión de protocolo y esquema, id de ensayo, peer, dirección, época, hora monotónica, opcode/nombre, tamaño original/copied, descarte/razón y fase. No llamar 2976 a una captura con timestamp 0. No registrar tokens, credenciales ni adjuntar partidas completas al informe público.

Fuentes que motivan el contrato: `replay.cpp`, `capture_file.h`, `troc_link.h`, `client_session.h`, D-13/D-14/D-18, A0 §§2–3, A1 IA y A4 identidad. Riesgo: un guion puede completar «login» mientras los actores estén aislados en campos distintos o el cliente no tenga escena; se exigen las sondas de efecto de A0.

### 3.3 Matriz de carga y evidencia que permite cada equipo

| Personas objetivo H | Sesiones U si anfitrión juega | U con ancla aparte | Ensayo sintético integrado correspondiente | Qué NO demuestra |
|---:|---:|---:|---|---|
| 2 | 2 | 3 | Anfitrión+1 peer; repetir anfitrión+2 para el modo con ancla | Dos clientes reales, IA/mundo lejano, privacidad o persistencia durable |
| 4 | 4 | 5 | Anfitrión+3, después+4 peers | Cuatro escenas/físicas y cuatro identidades de Steam |
| 8 | 8 | 9 | Anfitrión+7, después+8 peers | Ocho clientes de IA ni capacidad doméstica por sí sola |
| 10 | 10 | 11, excluido | Anfitrión+9 peers; U≤10 | Diez humanos externos con ancla adicional |

[S] Con U≤10, un ancla no jugada deja como máximo 9 externos; es la política de alcance D-22, no un límite nativo demostrado. Para el objetivo 8 externos debe medirse también U=9 aunque los puntos base de escalado sean U=2/4/8/10. Cada prueba conserva el contador real de usuarios creados y claves consumidas, no sólo el número de conexiones.

- **Un PC sin juego:** prueba aislada de codificación, colas, reenvío simulado, recuperación y límites; no mide CPU del motor. El anfitrión falso sólo certifica lo que su modelo comprueba.
- **Un PC con un juego, en pruebas futuras autorizadas:** hasta U=10 mediante peers locales, para alta/dispatch/visibilidad al anfitrión, ocupación SAM, colas y memoria del motor. La carga gráfica y buena parte de IA siguen siendo de un único cliente.
- **Dos PC con dos juegos y dos cuentas legítimas:** base para G2/G3/B5/P10, latencia real/Steam y una distribución de dueño. Se pueden añadir peers locales de laboratorio hasta U=10 para presión mixta; sigue sin representar 8/10 clientes reales. No lanzar dos juegos por máquina ni eludir las restricciones descritas en `CLAUDE.md`.
- **Para certificar 8/10 humanos:** hacen falta esos clientes reales y cuentas/licencias válidas, en ensayos posteriores; los dos PC actuales alcanzan para reducir riesgos, no para expedir esa certificación. No usar cuentas artificiales o la automatización de Steam como sustituto.

### 3.4 Métricas y criterios de avance

Todos son requisitos de diseño [S], aún sin instrumentación multisesión. Reutilizar los contadores/formatos existentes donde corresponda, sin atribuirlos ya al mod:

| Grupo | Medir por sesión y global | Criterio de aceptación propuesto; fuente del riesgo |
|---|---|---|
| Identidad/aislamiento | SteamID autenticado o ID de laboratorio, época, userKey/playerKey/characterNo, campo y destinatario de cada Ack sensible | Cero cruce de partidas/respuestas y cero sesión de una cuenta sobre otra; ID-1/ID-2 de A4. |
| Login/cargas | Tiempo de cada fase p50/p95/p99, Nak y motivos, dos ciclos, tamaño/edad de colas | Ningún timeout silencioso; un login nuevo no congela los activos; A0, ráfagas de hasta 6,56 MB/1 s en A2. |
| Claves y vida útil | SAM libres/ocupadas, actores visibles por cliente, usuarios del campo, actores sin sesión/controlador, altas/bajas por época | Regreso al nivel estable tras salir y margen real medido. No equiparar visible con ocupado; A1. |
| Red | Bytes/paquetes S→C/C→S, fan-out, RTT/pérdida, retransmisiones, profundidad/edad de cola, bloqueo por ventana | Mantener orden; cualquier límite produce rechazo/cierre explícito, nunca descarte oculto de estado; A2/D-19/D-26. |
| CPU/memoria | Tiempo de hilo servidor/red/walk/timer, paso más lento, RAM privada, reservas/liberaciones, tasa de crecimiento | Sin cola o memoria creciendo sostenidamente tras calentamiento y estabilización de la carga; un umbral en ms exige primero medir U=2. El rendimiento con ocho personas sigue sin medirse. |
| IA/contenido | Dueño/época por NPC, transición, acciones rechazadas, distancia, daño/aggro/recompensa observados | Cero doble dueño aceptado; progreso según política, no sólo paquetes recibidos. P10/B5/A1. |
| Guardado | Duración de barrera TLS +0x4460, operaciones pendientes, último checkpoint publicado, hashes/coherencia del par, recuperación | Todos recuperan una generación coherente; no detener la barrera para aprobar latencia. A3/A4 y RPT-0 antes de inducir fallos. |
| Larga duración | Horas activas, rotación A→B→A, reconexiones y paquetes viejos, cola de espera, errores acumulados | Al menos 2 h (U=2), 8 h (U=4), 24 h (U=8/9) y 48 h (U=10) como objetivos iniciales de ensayo, no demostración de semanas. Detener escalado por fuga reproducible. |

**Orden futuro:** PAT-1/PAT-2 fuera del juego; RPT-0; E0/E0b/E1/E2; E3 y E3b tras implementarlo; G2/G3+B5/P10 con dos PC; ID/PERS; luego U=4→8/9→10 con los mismos criterios. Una cola de espera al estar lleno no crea usuario nativo ni arrienda slot hasta admitirlo (A2/A4). El número de registrados en catálogo se prueba separadamente de las sesiones activas: 30 perfiles rotando no significan 30 clientes vivos.

## 4. Reportes de cierre y CrossSave

### 4.1 Reportería nativa: configuración y alcance demostrable

[E] `0x1315CD0` configura Sentry: en `0x1315E87–91` pasa un DSN con host **`pentry-relay.pearlabyss.io`** (cadena `0x5711280`) a `sentry_options_set_dsn`, import IAT `0x51ECA08`. No se reproduce su identificador en este informe. Activa auto-session tracking y symbolization con argumento 1 (`0x1315E9F`, `0x1315EAD`), define base local `DumpCache` y construye `crashpad_handler.exe` (`0x1315FB1`, cadena `0x57111F0`). La selección/instalación del backend tiene condiciones; la llamada a `sentry_init` está en `0x1316226`. Añade etiquetas BuildKey/CodeRev/Publisher en `0x131624C/267/287`. Son llamadas e imports del exe, no sólo nombres de cadenas.

[E] La ruta `0x1321F90` contiene `sentry_capture_minidump` en `0x1323493` y, si devuelve UUID no nulo, `sentry_flush(0x1388)` en `0x1323501`. La rutina `0x1318ED0` dispone de ramas que configuran usuario mediante `sentry_value_new_user/sentry_set_user` (`0x131933A/343`, `0x1319398/3A1`). **No se ha identificado aquí el contenido exacto de ese identificador ni se afirma que sea el SteamID.** Fuente: volcados A7 y tabla de imports propia.

[S] Existe una vía configurada para reportar fallos a infraestructura de Pearl Abyss; la formulación histórica «todo cierre se envía» de `CLAUDE.md` es más fuerte que lo que prueba este contraste. Puede haber condiciones de activación, fallo de red, consentimiento, retorno nulo o terminación sin reporte. **No se ha observado transmisión ni recepción por Pearl Abyss en esta tarea**, ni se ha inspeccionado la implementación de `sentry.dll` o de `crashpad_handler.exe`. No se infiere que cada volcado contenga todos los archivos o todas las credenciales.

[R] La EULA, art. 13, contempla recopilación de identificadores, estadísticas, registros de errores/cierres y diagnósticos; no especifica el payload de este ejecutable. [Fuente primaria](https://store.steampowered.com/eula/3321460_eula_0). La [política de privacidad](https://crimsondesert.pearlabyss.com/en-US/Policy?_policyNo=128), efectiva 8-07-2026, describe categorías de datos y proveedores/transferencias, pero no se encontró mención literal a Sentry o Crashpad en el texto consultado. No usarla como prueba de qué bytes se enviaron.

### 4.2 Fallos de guardado: adjuntos distintos de CrossSave

**Cadena demostrada** [E-x/E], sin provocar ningún fallo:

| Paso | Evidencia | Condiciones y alcance |
|---|---|---|
| Commit de ranura | `0x2355700 → .xtls 0xEF51850` | Sale con éxito sin procesar si `slot+0x69C` o `+0x699` están activos, o si `+0x698==0` (A3). Por tanto no todos los intentos llegan a E/S ni a reporte. |
| Fallo nativo | `.xtls 0xEF51913`, `0xEF519B8`, `0xEF51A63`, `0xEF51B07` llaman a `0x2356270` | Tras retorno falso de los métodos `vf+0x20` (carga), `vf+0x48` (validación), `vf+0x28` (guardado), `vf+0x30` (borrado), respectivamente. Las cadenas dicen Savedata Load/Valid/Save/Delete Fail Event. El motivo guardado está en `0x5983908`. |
| Ruta de los archivos | `.rsrc 0x235629E`, `0x23562D8–0x235634D` | La función recibe **RCX=M** (objeto propietario del handler), **RDX=motivo** y **R8=S** (ranura). Lee `H=[M+0x10]` y retorna si H es nulo; compone la raíz de `H+0x20` + nombre de ranura `S+0x28`, patrón `%#/%#`; enumera `*.save` con `0x1328100`. Es la **ranura que falló**, no una enumeración global de todos los SteamID. La versión inicial confundía M con S: no se debe buscar H en `S+0x10`. |
| Origen de la raíz | `.xtls 0xEF62216`, `0xEF622CE–E3` | El inicializador de FileHandler escribe `H+0x20` con el patrón `%#/%#/save/%#`, que luego registra como `saveRootPath`. La dirección absoluta efectiva depende de la instancia en vivo; sin redirección es la raíz nativa descrita en P9. |
| Límite de reportes | `.rsrc 0x2356291`, `0x2356439` | Retorna si el byte global `0x6D9CCE8 >=3`; incrementa tras llamar al reportero, **también si éste retorna por estar inactivo o no crear scope**. Cuenta invocaciones, no entregas confirmadas. No es un control por jugador ni promesa de máximo 3 envíos globales bajo cualquier carrera/reinicio. |
| Entrega a Sentry | `.rsrc 0x2356434 → 0x1317D40` | El reportero retorna si su byte de activación es 0 o falla crear scope. Recorre lista de rutas: `sentry_scope_attach_filew` en `0x1317E24`; entrega evento mediante `sentry_capture_event_with_scope` en `0x1317E91`. Esto demuestra adjuntos solicitados, no subida consumada. |

[E] Además, `0x2356270` puede añadir una ruta devuelta por `0x13003D0` a la lista (`0x2356362..0x2356406`); **no se determina aquí qué archivo es**. El alcance de los posibles adjuntos no se debe reducir sólo a `save.save` y `lobby.save`.

[E] La escritura normal `0x2358750` devuelve falso si falla abrir/escribir (`0x23588BE–C5`, `0x23588E2–E9`, `0x235892B`); `0x23571D0` detecta ese fallo en `0x23578AE`, intenta restaurar estado y termina falso (`0x2357BFC`, retorno `0x2357C3A`). Así puede llegar al Save Fail anterior. El guardián P6 devuelve errores de acceso a escrituras y deja lecturas (`src/common/save_guard.cpp:90`, `src/mod/session2/save_hooks.cpp:76`); **es razonable [S] que la prueba de E1 de guardar con P6 alcance este reporte**, aunque no es un resultado observado y puede fallar antes por otras condiciones.

[S] Si la raíz sigue siendo real y la ranura contiene partidas anteriores legibles, el reporte puede solicitar adjuntar esos archivos aunque P6 haya impedido sobrescribirlos. Si sólo se remapea `CreateFileW` dentro del juego, pero el reportero conserva rutas nativas o otro proceso abre los adjuntos, tampoco queda demostrado aislamiento. Un remapeo de ficheros correcto debe incluir **enumeración, nombres de adjuntos y lecturas del reportero**, además de escrituras. La puerta de DLL actual omite activar el mod dentro de Crashpad en el arranque normal (`src/mod/dllmain.cpp:14–24`; admite expresamente otros procesos con `CDMP_TEST_HOST`); por ello no se puede atribuir a P6 protección de ese otro proceso.

### 4.3 Puerta RPT-0 antes de inducir errores

[S] **RPT-0** debe preceder a E1 si se intenta el guardado rechazado, y a pruebas de blob inválido, disco lleno, corte de guardado o crash. No requiere ni propone enganchar `.xtls`, falsear reportes o sortear DRM. Faltan diseño/implementación y revisión propios; no existe hoy un interruptor que certifique esta contención.

1. Preparar una instalación de prueba con partidas de laboratorio y raíz aislada, sin datos reales accesibles por el juego y sus procesos de reporte. Una copia de seguridad protege recuperación, **no evita que el original pueda leerse y adjuntarse**. No realizar ahora esa preparación: esta tarea sigue siendo sólo lectura.
2. Auditar en la prueba futura las rutas que produce `H+0x20/slot+0x28`, resultados de enumeración, archivo adicional y dónde materializa adjuntos el backend. Resolver de forma explícita el tratamiento de reportes mediante opciones oficiales y controles del entorno que el usuario autorice, manteniendo las protecciones y requisitos de acceso del juego. No afirmar que desconectar Internet o bloquear un único dominio evita envíos posteriores: puede haber cola local y otros destinos.
3. Validar primero el guardián con archivos ficticios fuera del juego (prueba histórica del repositorio); cuando el entorno de reportes esté revisado, inducir **un** error en una ranura de laboratorio y observar sólo rutas/estado sin subir deliberadamente partidas del usuario. Confirmar que ni juego, backend ni cola de reporte acceden a partidas reales. La ausencia de paquetes durante un intervalo no demuestra que no haya reporte pendiente.
4. Resultado exigido antes de continuar: evidencia de aislamiento de archivos y decisión explícita sobre reportes pendientes/salientes, documentada con el usuario. Si no puede comprobarse, **posponer las pruebas deliberadas de error** y continuar las lecturas estáticas/sondas permitidas. No desactivar telemetría como sustituto de autorización contractual o de validación del mod.

Riesgo para 2/4/8/10: el mismo proceso y reportero pueden ver el estado de todas las sesiones; más usuarios no crean compartimentos de privacidad. Treinta partidas registradas accesibles al servidor aumentan el conjunto sensible, aunque esta ruta concreta enumera una ranura. La política de minimización/retención se diseña para el catálogo completo.

### 4.4 CrossSave

[E] La rama de ranura 3 de `0x186E450` (`0x186E6CD`) alcanza `0x15AC100` en `0x186EE49`, y esa rutina comprueba cuenta/plataforma, credenciales y archivos antes de preparar una petición de subida. Hay diagnóstico explícito `UploadCrossSaveFileAsync`, y la llamada asíncrona `0x15AD425 → 0x1365850` comprueba su resultado antes de registrar fallo (`0x15AD44A`) o petición cursada (`0x15AD49E`). Una llamada no demuestra servicio disponible ni subida completada. Fuentes: A3/A4 verificados, `0186e450.asm`, `015ac100.asm`, `015ab6c0.asm` (cabecera Authorization).

[S] Mantener **3 prohibida al cargar/guardar/admitir** y 206 prohibida por su ruta de borrado (A4); no basta bloquear escrituras en disco para impedir peticiones generadas desde memoria. CrossSave, reportes Sentry y Steam Auto-Cloud son canales distintos. La raíz canónica del servidor y staging de clientes deben quedar fuera de la raíz real sincronizada; A3/A4 definen arrendamientos y generaciones. Prueba futura **EXT-1**: las rutas de 3/206 se rechazan antes de ejecutarlas y un guardado aislado no llama a la subida CrossSave; no probarlo enviando datos a Pearl Abyss.

## 5. Términos y distribución

### 5.1 Fuentes primarias y consecuencias limitadas

Consulta de fuentes: **8 de octubre de 2026**. [R] significa fuente externa; no implica que sea un rumor. Se eligieron documentos oficiales del juego concreto y de Valve, sin trasplantar condiciones de Black Desert.

| Fuente | Lo que dice la fuente, resumido | Implicación para esta visión [S] |
|---|---|---|
| [EULA de Crimson Desert App 3321460](https://store.steampowered.com/eula/3321460_eula_0), efectiva 19-03-2026 | [R] Art. 4 concede uso personal/no comercial. Art. 5 restringe ingeniería inversa salvo excepciones legales, programas no aprobados, alteración de protecciones, minería de datos y modificación; apartados 5.9–5.11 restringen servicios/servidores y retransmisión de protocolos no autorizados. Art. 6 contempla parches; art. 10 permite terminar la licencia por incumplimiento. | Un túnel cooperativo/servidor persistente entra en una zona de riesgo contractual explícito. No se ha encontrado en esta fuente permiso específico para este mod. No afirmar legalidad universal, infracción judicial ya resuelta o sanción automática: importan jurisdicción, derecho aplicable y autorización concreta. |
| [Steam Subscriber Agreement](https://store.steampowered.com/subscriber_agreement/), revisión 10-09-2026 mostrada en la página | [R] §2.G limita modificaciones/ingeniería inversa con excepciones contractuales/legales y regula hosting, matchmaking y emulación/redirección de protocolos. §4 restringe manipulación no autorizada, evasión de controles y automatización, y contempla medidas sobre cuenta/suscripción. §2.E permite determinados servidores dedicados de productos Valve; no concede por sí solo servidor de Crimson Desert. | SteamNetworkingMessages como API técnica no equivale a permiso de Valve/Pearl Abyss para alterar el juego. Los peers sintéticos del diseño son identidades locales del banco, no cuentas Steam falsas ni generadores de progreso oficial. La actualización de términos requiere relectura antes de operación/distribución pública. |
| [Fan Content Guidelines de Pearl Abyss](https://crimsondesert.pearlabyss.com/en-US/Policy?_policyNo=130) | [R] Describe contenido de aficionados y exige respetar términos/derechos; no comercialidad y clara identificación como no oficial. El silencio ante una consulta no equivale a aprobación. | No utilizar estas pautas para declarar que cualquier mod o servidor está permitido por ser gratuito. Un aviso «no oficial» tampoco elimina restricciones. Una consulta jurídica/permiso escrito para distribución es trabajo posterior; no se envió mensaje alguno. |
| [Privacidad de Crimson Desert](https://crimsondesert.pearlabyss.com/en-US/Policy?_policyNo=128), efectiva 8-07-2026 | [R] Describe identificadores, datos técnicos/uso y proveedores para operación/protección, así como transferencias internacionales y derechos según territorio. | Diferenciar datos que conserva nuestro anfitrión de lo que pueda recopilar el juego; documentar catálogo SteamID, guardados, capturas y borrado/exportación en un futuro servicio. No inferir consentimiento de los miembros por un archivo técnico. |

Esta revisión identifica restricciones y riesgos; no resuelve la ejecutabilidad de cada cláusula bajo todas las leyes. No se investigaron evasiones de DRM, no se obtuvieron paquetes de servidor filtrados y no se copió código de referencia sin licencia.

### 5.2 Distribuir a desconocidos exige algo distinto de que funcione entre amigos

[E/S] A2 encontró que el servidor valida algunas acciones, pero no demuestra seguridad por defecto de todas las Req. `VaryStatByAccInputReq` (2181), handler `0x2972720`, es un contraejemplo local a una comprobación explícita de dueño en cada cuerpo; no prueba que cualquier atacante pueda explotarlo sin más. El whitelist de nombres actual (`server.cpp:448`) bloquea familias Save/Cheat/Dev, pero los nombres no expresan todas las autorizaciones. **Mantener acceso de conocidos mientras se audita cada Req admitida** (D-26), aunque el protocolo técnico pueda aceptar más.

[S] Antes de una eventual distribución —fuera del alcance de esta tarea— hacen falta:

- Revisión independiente del parser de entrada y de autorización por SteamID/sesión/época, límites por conexión y globales, rutas de archivo escogidas exclusivamente por catálogo, slots 3/206 bloqueadas y ningún comando de administración aceptado del jugador. Tratar todo cliente como fuente no fiable, incluso si su cuenta está autenticada.
- Paquete únicamente del código propio y dependencias redistribuibles con sus licencias; manifiesto de build/hash/protocolo, firmas propias verificables, configuración conservadora, instalación/desinstalación revisable y mecanismo de volver a la versión previa. No incluir exe, assets, claves internas, partidas de otros usuarios, archivos BDO o código de `reference/CrimsonDesertCoop` sin licencia. Las AOB breves publicadas no son una autorización para redistribuir binarios del juego.
- Instrucciones claras sobre alcance probado, copias/recuperación, reportes externos y pérdida de progreso. Las capturas se comparten con consentimiento y minimización; logs públicos sin SteamID completo, credenciales o datos de personaje innecesarios. Hash de archivo verifica integridad, no ausencia de malicia.
- Resolver el riesgo contractual específico antes de anunciar u operar públicamente. No comercializar plazas, objetos o ventajas mientras no exista autorización aplicable. Ninguna de estas medidas técnicas garantiza ausencia de sanciones ni transforma el mod en producto respaldado por Pearl Abyss.

Prueba futura **EXT-2**, inicialmente sin juego: un cliente del banco que declara otra identidad, otro slot, época antigua, build distinta o límites excesivos debe rechazarse sin tocar partidas ajenas; un paquete de distribución de laboratorio sólo contiene archivos autorizados y documentados. Un resultado correcto no sustituye auditoría de contenido, derechos ni pruebas reales de 8/10.

## 6. Afirmaciones para verificación independiente

Estas diez afirmaciones se seleccionaron para intentar refutarlas en el exe. Las normativas [R], predicciones [S] y recuentos de fuentes del repositorio requieren otra clase de contraste; no se presentan como instrucciones observadas del motor.

| ID | Afirmación y confianza | Puntos exactos / intento de refutación solicitado |
|---|---|---|
| A7-C1 | [E-x/E] Un commit que alcanza y falla las rutas load/valid/save/delete puede solicitar un reporte de Savedata; no todo commit lo hace. | `0x2355700→0xEF51850`; revisar guardas +0x69C/+0x699/+0x698 y las cuatro llamadas `0xEF51913/19B8/1A63/1B07` a `0x2356270`. Prioridad máxima por E1. |
| A7-C2 | [E/E-x] El reporte de Savedata construye raíz del handler + nombre de ranura, enumera `*.save` y entrega sus rutas a Sentry, condicionado por contador/activación/scope. | `0x2356291`, `0x235629E`, `0x23562D8–634D`, `0x2356434/439`; `0x1317D60/75`, `0x1317E24/91`; origen H+0x20 en `0xEF62216–E3`. Refutar confusión entre «todos los usuarios» y ranura actual, y entre adjuntar/entregar al SDK y envío externo. |
| A7-C3 | [E] La rutina Sentry configura un DSN de Pearl Abyss y ruta de Crashpad, con llamadas reales a imports. | `0x1315E87/91`, `0x1315FB1`, `0x131605C`, `0x1316226`; IAT `0x51ECA08/0x51EC990/0x51EC950`. Comprobar condiciones y no inferir recepción externa. |
| A7-C4 | [E] La ruta de minidump llama al SDK y, con UUID no nulo, pide flush con argumento 0x1388. | `0x1323482–0x1323507`; comprobar salto por reportero inactivo/UUID nulo. No afirmar todo cierre ni semántica de la DLL no leída. |
| A7-C5 | [E] Slot 3 en la ruta de guardado puede alcanzar UploadCrossSaveFileAsync. | `0x186E6CD`, `0x186EE49→0x15AC100`; condiciones de cuenta/credenciales/archivos en `0x15AC100`. El rechazo nativo previo no vuelve segura la ranura 3. |
| A7-C6 | [E] El inicializador de MoveActorReq cambia la vtable de instancia estática a la derivada. | `0x221D45/4C`, vtable `0x5B24CF8`, instancia `0x6A6FBA0`; contrastar campos opcode 2556 / kind 4 / handler `0x2977E10` con RTTI. |
| A7-C7 | [E] Troc::Find filtra por máscara de bits kind, no por equivalencia. | `0x143B345–354`; seguir unwind encadenado desde `0x143B2C0`. Contrastar 2319 / kind `0x7FFFFFFE` en instancia del CSV sin ejecutar. |
| A7-C8 | [E] ServerOnRecv consulta canal y mínimo/máximo del handler y tiene ruta diferida por +0x1C. | `0x2773D49`, `0x2773DB4/BF`, `0x2774033`; refutar que esa validación equivalga a permisos de dueño. |
| A7-C9 | [E] Las dos vf1 de ExceptOne comparten prefijo y su identidad exige discriminar por vtable/firma no ambigua. | `0x2A75880/0x2A759B0`, vtables `0x5B25378/0x5B25358`; volver a contar las AOB del catálogo común y comprobar qué ocurre al enmascarar rel32. |
| A7-C10 | [E] El punto P8 `0x35BAD4` es interior de `0x35B800`, no entrada independiente. | `.pdata`/unwind y desensamblado alrededor; cotejar primeros bytes y AOB en `vision-firmas-2976.json`. No autorizar inline por coincidencia única. |

## 7. Archivos de evidencia y límites reproducibles

- [vision-a7-estatico.json](data/vision-a7-estatico.json): huella, resultados del catálogo 1115, ventanas de instrucciones, hashes de scripts/volcados y alcance de las comprobaciones. Se omite el identificador del DSN en las ventanas publicadas.
- [vision-a7-rvas-fuente.csv](data/vision-a7-rvas-fuente.csv): 58 declaraciones/lecturas de RVAs/VA de código existente; no modifica ninguna fuente.
- `C:\Emulador CD\build\research\vision\a7\estatico.py`: lectura PE, contraste CSV, cadenas e inventario heurístico; `volcar.py`: disassembly/imports, siguiendo fragmentos encadenados; `exportar.py`: resumen y CSV de evidencia. Todos usaron `PYTHONDONTWRITEBYTECODE=1`.
- En esa misma carpeta: `cadenas.json`, `imports.json`, `tabla_comprobada.json`, `constantes_fuente.json` y los `.asm` citados. Una búsqueda de cadenas produjo falsos positivos como «sEntry»; sólo se han extraído conclusiones de imports y llamadas contrastadas, no del recuento 137.

No se leyó ningún guardado real completo, no se abrió el juego, no se contactó con sus servidores desde el exe, ni se enviaron reportes/pruebas. Las visitas web se limitaron a fuentes públicas primarias. Persisten incertidumbres explícitas: compatibilidad con builds futuras, contenido exacto y destino efectivo de reportes, seguridad frente a clientes desconocidos, aforo real y permisos de distribución. Son resultados de investigación, no defectos ocultados con una promesa de funcionamiento.

## 8. Verificación independiente y refutaciones

El revisor `verifica_a7` volvió a leer el exe de disco, sin consumir los volcados del autor como evidencia. Scripts propios: `C:\Emulador CD\build\research\vision\verificacion\a7\revisar.py` y `complementar.py`; resumen persistido en [vision-verifica-a7.json](data/vision-verifica-a7.json). Huella, tamaño, timestamp y base coinciden con §2.1. Se siguió `.pdata` con unwind encadenado, se reconstruyeron los imports desde el PE y se consultó RTTI directamente mediante COL/TypeDescriptor, sin el caché RTTI del autor. El contraste del catálogo parte de sus filas para localizar candidatos: **no es un descubrimiento independiente de las instancias en una build desconocida**.

Resultado: **9 afirmaciones confirmadas en su alcance estático y una identificación de objeto falsa, corregida (C2)**. No se observó tráfico externo; ninguna llamada al SDK se convierte en prueba de subida. La corrección C2 no elimina el riesgo de adjuntar partidas y mantiene RPT-0 antes de E1 con fallo deliberado. Todas las propuestas de aforo, banco de pruebas y distribución siguen siendo [S], y las normas externas siguen siendo [R].

| ID | Dictamen | Evidencia independiente e intento de refutación |
|---|---|---|
| A7-C1 | **Confirmada [E-x/E]** | `commit_thunk.asm`, `commit_vm.asm`: `0x2355700` salta a `0xEF51850`; +0x69C/+0x699 activos o +0x698=0 devuelven éxito sin procesar. Cada retorno falso de vf+0x20/+0x48/+0x28/+0x30 lleva respectivamente a `0xEF51913/19B8/1A63/1B07→0x2356270`. Cadenas leídas de nuevo en `0x5983990/38B8/3908/3B50`. Hay rutas sin reporte y no se deduce que todo error P6 alcance ésta. |
| A7-C2 | **Falsa en el objeto de origen; corregida [E/E-x]** | `savedata_report.asm` refuta `H=[S+0x10]`: `0x235629E` carga **[RCX+0x10]**, mientras `0x23562D8` toma **[R8+0x28]**. `commit_vm.asm` pasa RCX=M y R8=S. Confirmados patrón `%#/%#`, `*.save`, guarda global <3, H no nulo, archivo adicional condicional y `0x1317E24/91` hacia los imports de adjuntos/evento. La invocación consume contador incluso si el reportero no crea el evento; sin garantía de transmisión. |
| A7-C3 | **Confirmada [E]** | `sentry_init.asm`, `sentry_inicio_guardas.asm` e imports propios: DSN en `0x5711280` con host `pentry-relay.pearlabyss.io` (identificador omitido); `DumpCache` y `crashpad_handler.exe` en las cadenas citadas. `0x1315ED1` condiciona el backend; `0x1316038/42` rechaza archivo ausente/directorio. `sentry_init` en `0x1316226` sólo activa el byte del reportero si retorna 0 (`0x131622C–23B`). No prueba activación en todas las ejecuciones. |
| A7-C4 | **Confirmada [E]** | `minidump.asm`: `0x1323482/85` comprueba reportero activo; `0x1323493` llama `sentry_capture_minidump`; `0x13234AE/B0` salta por UUID nulo y evita `sentry_flush`; `0x13234FC/3501` pasa exactamente 0x1388. Ni el éxito del flush ni la recepción exterior fueron observados. |
| A7-C5 | **Confirmada [E]** | `save_prologo.asm` preserva el argumento de ranura R8D en R14D; `save_slot3.asm` compara R14D con 3 en `0x186E6CD`; `save_slot3_call.asm` confirma `0x186EE49→0x15AC100`. `crosssave_start/send.asm` y cadenas propias confirman guardas de disponibilidad, nombre/lista de archivos y credencial, y `0x15AD425→0x1365850`. Petición cursada no equivale a subida completada. |
| A7-C6 | **Confirmada [E/datos PE]** | `moveactor_init.asm`: `0x221D45/4C` escribe vtable `0x5B24CF8` en `0x6A6FBA0`. Lectura directa: RTTI MoveActorReq, kind=4, opcode=2556, vf2=`0x2977E10`, vtable inicial en disco=`0x5726410`. No se infiere un formato de payload sólo por coincidir el nombre. |
| A7-C7 | **Confirmada [E/datos PE]** | `troc_find.asm`: la instrucción de `0x143B351` es `test dword ptr [rcx+8],esi`; `cmovne` conserva el handler cuando comparte bits. Entrada real `0x143B2C0`, también para fragmento `0x143B2DF`. Instancia `0x6A7A580`: opcode 2319, kind `0x7FFFFFFE`, que incluye el bit 4. |
| A7-C8 | **Confirmada [E]** | `server_recv.asm` y `cola_diferida.asm`: igualdad del canal en `0x2773D49–50`; mínimo y máximo inclusivos en `0x2773DB4–DC4`; +0x1C=1 selecciona construcción de paquete y `0x27740CF→0x143A580`. Existen más condiciones de sesión/actor en el despachador; estos tres filtros por sí solos no demuestran autorización de cada Req. |
| A7-C9 | **Confirmada [E/datos PE]** | RTTI directo de `0x5B25378/358` identifica Self/Control y vf1 `0x2A75880/9B0`. Búsqueda independiente en **todo el archivo**: cada AOB exacta de 40 bytes tiene una coincidencia; al enmascarar sólo los cuatro bytes rel32 del primer `call`, ambas firmas tienen **dos** coincidencias, justamente esas dos funciones. No queda certificada portabilidad entre builds. |
| A7-C10 | **Confirmada [E/datos PE]** | `.pdata` y unwind devuelven `0x35B800` como entrada que contiene `0x35BAD4`. Los 16 bytes iniciales son `49 8B 87 E0 00 00 00 48 8B 38 48 89 7D C7 48 8B`; AOB exacta común de 40 bytes, una coincidencia. Es un punto interior aunque la firma sea única; esta revisión no habilita el hook ni valida registros en vivo. |

Complementos: se reprodujeron las **1.115/1.115** correspondencias de nombre RTTI, kind, opcode y vf2, todas con vtable base en disco, más los seis campos del CSV `min_size/max_size/flag_10/flags_14/flag_1c/flag_88`, sin diferencias. `tabla_independiente.json` conserva el detalle local. Esto no verifica 1.115 payloads, controles de dueño ni los inicializadores individuales de todos los mensajes. En la ruta de error de escritura se volvió a seguir `0x23578A9/AE/B0→0x2357A51`, intento de restauración `0x2357AA9` y retorno falso `0x2357BFC/7C3A`; el alcance E1→reporte continúa siendo una hipótesis [S] a validar bajo RPT-0. El archivo adicional de `0x13003D0` conserva identidad desconocida: la rutina obtiene candidatos mediante vf+0x30, pero esta revisión no atribuye un nombre o contenido sin resolver sus instancias.
