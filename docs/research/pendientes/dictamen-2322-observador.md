# Dictamen independiente: 2322 para el observador y separación de S2/S1

**[V]** Revisión del 9 de octubre de 2026. Build 1.0.0.2976; direcciones expresadas como **RVA**, base `0x140000000`. Repositorio revisado: `C:\Emulador CD-opcion-d`, HEAD `5288876659a58e7a665bc318d593f9ea2fafcba1`. Solo análisis; ninguna implementación ni prueba nueva dentro del juego.

**[V]** `[V]` identifica una comprobación en código, instrucciones, captura o log, con su referencia. `[E]` identifica una inferencia, recomendación o resultado pendiente de validación. Los nombres `FUN_*` y `local_res8` son etiquetas de análisis, no símbolos oficiales. Las anotaciones heredadas del proyecto Ghidra se han contrastado con instrucciones cuando sostienen una conclusión.

## 1. Respuestas cortas

| Pregunta | Dictamen |
|---|---|
| ¿El gestor propio M del observador debe estar vacío en un campo ajeno? | **[E] Depende de qué se llame vacío.** Es coherente que no tenga precargas propias del campo del anfitrión. No está demostrado que deba carecer de todo nodo, instancia o escena personal, ni que exista en esta build un flujo nativo completo de observador que establezca esa regla. |
| ¿Existe un camino nativo demostrado que prepare esas escenas del anfitrión en el gestor propio del no dueño? | **[V] No aparece en las rutas examinadas.** Los handlers eligen el gestor del dueño que conoce el campo cliente; no eligen automáticamente al jugador local por ser receptor del paquete (`0x9A5BE0`). **[E]** Esto no demuestra la inexistencia de cualquier otro camino en todo el ejecutable. |
| ¿Hay precedente nativo de precarga vacía y 2322 inmediato? | **[V] Sí, para entradas al campo inicial propio.** HC #789→#794: 10 µs entre 2144 y 2322; GC #797→#802: 18 µs. La línea base L:87–88 muestra listas vacías, contador de preparación cero y predicado nativo verdadero. **[E]** Es un precedente del contrato de carga, no una prueba de soporte nativo de campos ajenos. |
| ¿2322 nativo corrige la raíz de S2? | **[E] Sí como corrección candidata del hito omitido en la adaptación actual**, con las condiciones de §4. No lo considero un sustituto prohibido por el mero hecho de no enviar las escenas del anfitrión a M. Su eficacia en Shadow todavía debe probarse; no corrige S1 por sí solo. |
| ¿Salir de 0x11 resolverá los NPC y los mensajes tardíos? | **[V] No los reconstruye.** 2322 escribe el hito; no crea los objetos que buscan los handlers posteriores ni cambia el dueño del binding de los NPC. **[E]** Puede permitir completar la carga mientras S1 sigue fallando. |
| ¿La reescritura de 2174 forma parte de la causa? | **[V] Sí, de la selección de M en el cliente.** `server.cpp:324–338` cambia el dueño de la copia del invitado; `0x9A5BE0` lo usa. **[E]** No conviene retirarla como arreglo inmediato: evita un cierre documentado y cambiaría más funciones que el enrutado de 2661. |
| ¿Crear las réplicas de S1 en X es una vía de raíz y segura? | **[E] Vía de raíz plausible; seguridad y suficiencia no demostradas.** X coincide con el destino que busca el NPC, pero hay que resolver el nodo previo, la preparación asíncrona, el momento del binding, el tiempo de reproducción y los efectos/autoridad. El par 2981/2661 y tres filtros de opcodes no bastan como prueba. |
| ¿Próxima sesión en dos tramos? | **[E] Sí: medida y después experimento exclusivo de S2.** La réplica de S1 debe tener su propia prueba posterior, condicionada a lo observado; no activar ambas modificaciones a la vez. |

## 2. Fuentes y comprobaciones independientes

**[V]** Las referencias abreviadas de este informe apuntan a estos materiales:

| Clave | Fuente |
|---|---|
| GL | [Log del invitado, sesión 5](<C:/Emulador CD/backups/p8-sesion5-shadow/cdmp.log>) |
| GC | [Captura binaria del invitado](<C:/Emulador CD/backups/p8-sesion5-shadow/cdmp_packets_20261009-194117.bin>) |
| HL | [Log del anfitrión, sesión 5](<C:/Emulador CD/backups/logs/cdmp_20261009-2038_p8g-host.log>) |
| HC | [Captura del cliente propio del anfitrión](<D:/SteamLibrary/steamapps/common/Crimson Desert/bin64/cdmp_packets_20261009-203910.bin>) |
| L | [Línea base local del invitado](<C:/Emulador CD/backups/logs/cdmp_20261009-2157_p8guestlocal2.log>) |
| P | [Prueba local de anfitrión y peer](<C:/Emulador CD/backups/logs/cdmp_20261009-2123_p8local4.log>) |
| PE | [Ejecutable de investigación](<D:/RE/CrimsonDesert_2976/bin/CrimsonDesert.exe>) |
| W | [Directorio de evidencia de esta revisión](<<codex-workspace>/dictamen-2322-work>) |

**[V]** PE tiene SHA-256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`. Se obtuvieron 17 decompilaciones nuevas y referencias cruzadas mediante Ghidra, en copia propia, con `-readOnly -noanalysis`; las ramas decisivas se contrastaron con Capstone. Evidencia conservada: `W/decomp`, `W/dis-*.txt`, `W/Dictamen2322.java`, `W/headless.log`.

**[V]** Se reanalizaron los binarios CDPK como datos, usando Python `-I`: GC contiene 29.584 registros y HC 526.593, sin registros parcialmente copiados según sus cabeceras. `#n` es el registro binario desde 1, no la línea de la captura de texto. `t` es el tiempo relativo al primer registro de cada archivo, por lo que no se restan directamente tiempos de HC y GC. Resultados reproducibles: `W/review.py`, `W/GC-capture.json`, `W/HC-capture.json`, `W/crosscheck.py`.

**[V]** Se contrastaron el informe anterior [p8-raiz-invitado.md](<C:/Emulador CD-opcion-d/docs/research/pendientes/p8-raiz-invitado.md>), los informes de implementación y validación, [p8-observador-escenas.md](<C:/Emulador CD-opcion-d/docs/research/pendientes/p8-observador-escenas.md>), su evidencia `observador/`, el diagnóstico del hilo de red, el diario, el catálogo de mensajes y las rutas pertinentes del código actual. Las afirmaciones de esos documentos no se han tratado como pruebas suficientes por sí mismas.

### 2.1 Qué demuestra la sesión 5

**[V]** En el primer viaje de GC, 2144 #797 a `28,843599 s` precede a 2322 #802 a `28,843617 s`. Este último contiene `12 09 FF 04 00 34 03 10 A0`: opcode 2322, payload de cuatro bytes y actor `A0100334`. GL:173 registra la escritura nativa `M+0x231: 1→2`, con M=`0x3B07621E400`.

**[V]** En el segundo viaje, GC contiene 2174 #1190 a `46,732033 s`, 2549 #1451 a `55,259972 s` y 2144 #9114 a `67,556077 s`, pero ningún segundo 2322. GL:1308 vuelve a observar **el mismo M**, ahora con `+0x231=1`, `+0x174=0`, `+0x2D4=0` y longitudes `+0x1B8=0`, `+0x1C8=0`. El puntero `+0x1C0` es no nulo: una lista vacía se determina por su longitud, no por exigir puntero nulo.

**[V]** GL:2374 registra `0x11→0x18` tras **250,306 s**. No es `0x11→0x12`. Después aparece una excepción de salida en `0x2045A6A` (GL:2376). **[E]** Por tanto, esos 250 s no acreditan que la carga acabara por sí sola ni un temporizador nativo de éxito.

**[V]** La rama de `0x2BC8470/0x2BC8473` compara el usuario viajero con el dueño y salta a `0x2BC84D9` cuando difieren, omitiendo la llamada a `0x287EF60` en `0x2BC84D4`. HL:4468 documenta precisamente `90100333 != 90100000` para la entrada compartida. La ausencia del segundo 2322 concuerda con esa decisión del servidor.

**[V]** La cifra precisa de S3 en GL:2377 es: **412 registros 2005**, 409 con restauración 1; **2577 recepciones 2991 contabilizadas**, **2214 entradas en su consumidor y 2214 instalaciones**, sin descartes en ese consumidor ni acciones nulas detectadas ni intervención de la guarda. Hay 15 restauraciones fallidas y una excepción posterior registrada. **[E]** Esto respalda la mejora observada de S3; no demuestra que toda recepción 2991 fuera aplicada ni que la sesión completa estuviera libre de cierres.

## 3. ¿Qué significa «M vacío» y qué demuestra el caso nativo?

### 3.1 Hay dos gestores conceptualmente distintos

**[V]** `0x9A5BE0` obtiene el usuario indicado por `field+0x180` mediante `0x8AE390`, su personaje principal mediante `0x206BA40` y su gestor mediante `[[[actor+0x68]+0xA8]+0x18]`. Los handlers examinados obtienen el campo actual con la cadena `0x17FA6C0→0x9F8560` y llaman a la vf48 del campo. La clave del actor del paquete no selecciona por sí sola su gestor personal.

**[V]** Con el 2174 reescrito, ese dueño para el cliente invitado es `90100333`: el gestor seleccionado es **M**, del personaje del invitado. En cambio, `0x9ED290` toma el dueño del secuenciador de `comp+0x2D8`, que en los NPC estudiados es `A0100001`, y consulta **X**, el gestor de la representación del Kliff del anfitrión **dentro del proceso del invitado**. No es un acceso al gestor del otro PC. Búsqueda del actor: `0x9ED415`; acceso a X: `0x9ED453–0x9ED470`; consulta de instancia a través de `X+0x148`.

**[E]** La política «las escenas del anfitrión deben reconstruirse en X y no incorporarse indiscriminadamente a M» es coherente con esas búsquedas. La afirmación más fuerte «todo M de cualquier observador debe estar siempre vacío» no se sigue de ellas: podrían existir nodos o secuencias personales del invitado, otra entrada de campo o necesidades futuras del mod.

### 3.2 Las listas de precarga no son todo el gestor

**[V]** `0xA97D60` comprueba un contexto válido, `+0x231>=2`, `+0x174==0`, la condición temporal `+0x2D4/+0x2D0` y las dependencias de las listas `+0x1B0/+0x1C0`. No exige que haya al menos una escena. Con contexto válido, contador cero, temporizador no bloqueante y listas vacías, devuelve verdadero. Evidencia: nueva decompilación `W/decomp/0xa97d60.c` y llamadas en `0x779E43–0x779E7F`.

**[V]** El contador `+0x174` pertenece a una estructura diferente de la tabla que consulta `0x1F6C190`: esta usa `+0x108/+0x10C`, buckets en `+0x118` y entradas en `+0x120`. `0xA9CA40` introduce trabajo de preparación en la estructura de `+0x170` y programa trabajo asíncrono. **[E]** Llamar «número total de instancias existentes» a `+0x174` es demasiado fuerte; cero allí no demuestra ausencia de todos los objetos que pueden encontrar los handlers de stages.

**[V]** Hay un contraejemplo directo a «listas vacías bastan»: GL:174–175 tiene ambas listas vacías y `+0x231=2`, pero `+0x174=1` hace falso `0xA97D60`. En GL:176–177 el contador baja a cero y el predicado se vuelve verdadero. No se debe eliminar ese segundo predicado.

### 3.3 Precedentes y límites

**[V]** En HC, el primer 2144 #789 (`19,703615 s`) va seguido del primer 2322 #794 (`19,703625 s`), sin 2661 anterior en la captura. En L:86–88, la entrada al campo inicial en `(0,1000,0)` recibe 2322, tiene las listas vacías y el predicado devuelve verdadero; L:137 registra salida de `0x11` en `0,000 s`. Son precedentes de **precarga local sin trabajo pendiente**, no una medición exhaustiva de toda la tabla de nodos.

**[V]** En la entrada al mundo del dueño, HC registra 2144 #1199 a `49,569047 s`, doce 2661 entre #1680 (`50,420055 s`) y #38031 (`50,859367 s`), y 2322 #38370 a `50,925809 s`: unos `1,357 s` después del 2144. Hay **otros siete 2661 posteriores** al 2322 durante la captura. **[E]** 2322 cierra una fase de precarga; no significa que nunca puedan aparecer nuevos secuenciadores.

**[E]** No se ha identificado un ejemplo nativo, sin adaptación del mod, de un usuario no dueño que entre en un campo ajeno y reciba este ciclo completo con M propio vacío. El precedente permite justificar el contrato vacío, pero no llamar a toda la propuesta «comportamiento multijugador nativo ya demostrado».

## 4. Dictamen sobre 2322 y condiciones de aceptación del diseño

**[E]** Rectifico la aplicación absoluta de mi regla anterior. **Enviar el 2322 nativo puede ser la corrección de raíz del hito que falta en S2**, si la precarga que corresponde a M en esa entrada está completa o no tiene trabajo. Exigir como condición una instantánea de las escenas del anfitrión dentro de M mezclaba dos gestores y dos obligaciones diferentes. Mantengo la regla para el caso en que sí falte preparación propia, y para cualquier intento de presentar 2322 como reparación de S1.

**[E]** La causa corregida sería: el servidor omite el hito porque el viajero no es dueño, mientras el cliente adaptado sí inicia un ciclo de carga que espera ese hito. Completar el protocolo del observador conserva sus comprobaciones nativas. Cambiar directamente `M+0x231`, saltar `0x11` o declarar falsas las dependencias sí eludiría esas comprobaciones.

**[V]** `0x2999D80` serializa el 2322 con la clave del actor que recibe como primer argumento y los destinatarios que obtiene del buscador. La ruta nativa construye `ToTarget` en `0x287F194–0x287F1C7`: vtable base `exe+0x5B253F8`, vtable derivada `exe+0x5B25438`, byte `+8=0`, puntero `+0x10=actor->vfE8()`, y llama al emisor en `0x287F1D4`. El objeto ocupa al menos los 24 bytes de esos campos. La clave del payload y la sesión destinataria cumplen funciones distintas.

**[V]** `0x2999D80` utiliza almacenamiento y listas de destinatarios ligados al TLS, incluido el contexto de envío en `TLS+0x250` y la lista en `+0x2B0/+0x2B8`. Puede no emitir nada si no hay destinatario; no ofrece un resultado booleano simple de «paquete entregado». No prepara stages ni instala los temporizadores de `0x287EF60`, pero no es una función sin efectos ni independiente del hilo.

**[E]** Estas son las condiciones del experimento y de una implementación posterior:

1. **Una entrada válida y concreta.** Solo usuario 2 observador, actor y sesión vivos, entrada al campo completada sin error, campo actual coincidente y generación vigente. `0x2BC84E2` es un punto candidato en la continuación de `MoveFieldCompleteCore`; no basta alcanzarlo para acreditar todos esos requisitos. Exigir resultado correcto y asociación del gestor de campo con ese campo.
2. **Después del 2144 de esa entrada, en el mismo orden fiable.** Debe estar aceptado en la cola que llega al invitado, no solo observado en una sonda. El 2174 correspondiente y el reinicio del ciclo cliente deben preceder a la aplicación del hito. No esperar a ciegas a que la pantalla muestre `0x11`: el juego acepta 2322 antes de alcanzar ese estado, como en GL:173–177.
3. **Hilo lógico del servidor y contexto de envío válido.** Usar el contexto de la ruta nativa o diferir a un punto lógico verificado. No llamar desde el trabajador de diagnóstico, el hilo de Steam ni recursivamente desde `take_frame`/la interceptación de envío. Revisar también los ámbitos nativos de agrupación de mensajes para no mezclar destinatarios.
4. **ToTarget exclusivo del viajero.** Construir y destruir el buscador respetando la secuencia nativa y la ABI; obtener la sesión viva mediante el actor correcto. No reutilizar un puntero de otra entrada, no usar ToNear ni enviar el hito del actor del anfitrión como si fuera el del invitado. Confirmar el destinatario real en ambos extremos.
5. **Precisamente una vez por entrada.** Identificar conexión/época, sesión, generación del actor, campo e identificador de viaje. La época de conexión o el actor key, solos, no distinguen dos viajes del mismo personaje. Registrar «programado», «encolado» y «aplicado» por separado, y coexistir con cualquier 2322 que sí emita la ruta nativa.
6. **Cancelar trabajo obsoleto.** Nueva entrada/2174, salida, desconexión, sustitución de sesión, destrucción del campo o cambio de generación invalidan una emisión pendiente. Una reconexión comienza una transacción nueva; no rehabilita una antigua. No reenviar un 2322 atrasado para intentar recuperar una sesión: su payload no lleva campo ni generación de viaje.
7. **Precarga de M bien definida.** Confirmar que en ese ciclo no se omitió preparación que sí correspondía al invitado. Listas vacías por falta de entrega y listas vacías porque nada corresponde no son semánticamente lo mismo. Mantener `0xA97D60` y medirlo después del hito; si falla, investigar la dependencia en vez de forzarla.
8. **Sin efectos de dueño añadidos.** No llamar a `0x287EF60` ni `0x2910DE0` para el observador, ni crear sus temporizadores, repetir el preparado del anfitrión o cambiar el dueño real del campo. No crear un temporizador que repita 2322 cada cierto tiempo. El temporizador de vigilancia de una prueba puede informar y abortar; no inventar éxito.

**[V]** Dos razones concretas para las condiciones de hilo y orden: `server.cpp:292–295` actualiza la fase observada **antes** de `g_outgoing.push` en la línea 358; esta inserción puede fallar. Además, `child_hold.cpp:1462–1470` distingue explícitamente el hilo receptor con contexto `server_realm` del hilo lógico que puede ejecutar `NotifyAppear`. **[E]** Ver `WINDOW_B` o TLS de servidor no demuestra, por sí solo, ni entrega previa del 2144 ni idoneidad del hilo.

**[V]** `0x287EF60` tiene preparación, cambios de estado y caminos de temporización/reintento además de la emisión final. `0xB87A67` escribe 2 sin codificar la época de viaje en el mensaje. **[E]** Invocar la función completa o permitir duplicados tardíos introduce efectos innecesarios y puede tocar un ciclo ya avanzado o un viaje posterior.

## 5. Qué ocurre después de 0x11

### 5.1 La lista de 0x12 no comprueba S1

**[V]** En `0x779ED0–0x779F12`, el estado `0x12` busca el actor de la primera entrada. Si no existe, elimina la entrada; si existe y su virtual `+0x1D8` devuelve verdadero, también la elimina. Si existe y devuelve falso, conserva la espera. Al vaciarse pasa a `0x13` (`0x779EA8–0x779EC3`). Este recorrido no busca los dos hashes de BodyAnimation ni comprueba que se haya reconstruido su gráfico dinámico.

**[V]** GL:1370 inspeccionó las 193 entradas: 192 listas, una no encontrada, ninguna no lista ni ilegible. **[E]** Con ese estado, el recorrido de `0x12` debería vaciarse; es una predicción pendiente de observar tras el nuevo 2322, no una autorización para borrar la lista manualmente. «Actor no encontrado» se elimina por diseño de ese recorrido, no equivale a demostrar que todos los NPC sean correctos.

**[V]** La línea base L:137–163 pasa rápidamente por `0x11/0x12`, pero después permanece **14,098 s en 0x14** y **2,314 s en 0x15**. `0x77CFB0`, usado en la continuación de carga, vuelve a obtener el gestor con `0x9A5BE0`; la ruta `0xA97B30` conduce a la escritura de fase 3 documentada en el análisis previo. **[E]** Aceptar S2 exige observar también la terminación normal de los estados posteriores, no medir solo que desapareció `0x11`.

### 5.2 2981, 2524 y 2111 tardíos: no crean lo que falta

**[V]** GC conserva seis 2981, seis 2524 y doce 2111 de tipo 4 para `0x164/0x165/0x167`, y ningún 2661. Se verificaron coincidencias byte a byte con HC; por ejemplo GC #17684 es idéntico a HC #139918. Los handlers seleccionan el gestor del dueño del campo cliente antes de consultar el objeto de destino.

| Mensaje y ruta | Comportamiento estático comprobado [V] |
|---|---|
| 2981: `0xC073A0→0xA9E100`, caso tipo 4 | Busca por clave con `0x1F6C190` en `0xA9E329`. Si el objeto es nulo, `0xA9E34D` salta a limpieza en `0xA9E44F`. Si existe, todavía exige su bandera `+0x28B`. |
| 2524: `0xC07D00→0xA9E480` | Selección del gestor en `0xC08175`, aplicación en `0xC0819B`; búsqueda en `0xA9E50F`. Si no hay objeto, la rama de `0xA9E561–0xA9E5BA` termina en limpieza, sin crearlo ni almacenar el paquete para después. |
| 2111: `0xC08970` | Búsqueda en `0xC08D91`; comprobación en `0xC08DAF`; objeto nulo salta en `0xC08DB2` a `0xC08E05`. El handler puede terminar con resultado de éxito aunque no aplicara el evento. |

**[V]** Estas ramas no contienen una cola de actualizaciones pendientes para un identificador ausente. 2322 tampoco recorre una cola de esos mensajes. **[E]** Si el objeto no existe en M, los mensajes se pierden funcionalmente para ese objeto; crear después una instancia no los reproduce automáticamente. Si existe otro objeto con la misma clave, la posibilidad cambia: podrían aplicarse al contexto equivocado. Hay que medir el resultado de la búsqueda, no deducirlo de `+0x174=0`.

**[E]** Esto además limita la propuesta «2981 justo antes de 2661 basta». La rama tipo 4 de 2981 necesita ya un objeto en la tabla base; 2661 programa preparación en otra estructura. Falta comprobar qué estado inicial aporta ese objeto y cuándo está utilizable en X. Repetir el orden de bytes del dueño sin reproducir sus precondiciones no garantiza el mismo resultado. Tampoco justifica copiar indiscriminadamente los 2264 personales del dueño.

### 5.3 Los NPC siguen buscando X

**[V]** El 2322 no cambia `comp+0x2D8/+0x2E0`, no resuelve `local_res8` de `0x9ED290`, no inserta la instancia que consulta `X+0x148` y no instala `CC+0x70/+0x78`. La ruta de BodyAnimation que sí instala esos gráficos es `0xADED90→0xA79770`, documentada en el informe raíz con escrituras `0xA79BAC/0xA79F4C`.

**[E]** Por ello es esperable que un experimento correcto de S2 deje vivos los fallos de S1. Deben seguir visibles en la telemetría; no ocultarlos ni declarar terminada la sincronización por haber completado la carga.

## 6. Reescritura de 2174: parte de la causa, no cambio aislado seguro

**[V]** La reescritura está en la copia destinada al invitado (`payload+9`, `server.cpp:324–338`); el campo del servidor mantiene su dueño. El cliente sí usa ese dato al seleccionar el gestor. Hay, por tanto, dos decisiones distintas: la rama de dueño del servidor sigue excluyendo al usuario 2, mientras el cliente adaptado considera suyo el contexto de carga.

**[E]** Si el invitado conociera de forma válida al usuario anfitrión y a su personaje principal, y su campo cliente conservara el dueño `90100000`, `0x9A5BE0` seleccionaría X. En ese caso 2661 y 2322, cuando superasen sus otras validaciones, y la búsqueda del NPC podrían converger en X. Cambiar solo la clave de actor dentro de un 2661 no consigue ese resultado.

**[V]** El cierre que motivó la adaptación está documentado en [p8-cierre-campo.md](<C:/Emulador CD-opcion-d/docs/research/pendientes/p8-cierre-campo.md>), §1–§3: la búsqueda del usuario dueño no se resolvía; `0x9A5BE0` devolvía cero y un consumidor posterior del estado `0x0A`, en `.xtls 0x940B223`, desreferenciaba el resultado. La decompilación nueva confirma que `0x9A5BE0` devuelve cero si no obtiene usuario o personaje válidos. No debe atribuirse el acceso inválido directamente a la entrada de `0x8AE390`.

**[E]** Publicar el actor `A0100001` no basta para retirar la reescritura: hay que acreditar el **objeto usuario** del anfitrión en el registro cliente, su validez, su relación con el personaje principal y X utilizable **antes** de que los estados de carga lo consulten. El orden debe sostenerse también al reconectar, salir o cambiar de campo.

**[V]** `0x9A5BE0` tiene referencias desde varios pasos de carga, entre ellos `0x77AA6C`, `0x77AE9A`, `0x77D03C` y `0x779E43`; las sondas existentes observan escrituras de fase 0 en `0x77AA8B` y de fase 1 en `0x77B0CE`. El análisis anterior de `p8-cierre-campo.md` también relaciona el nodo de destino del viajero con este contexto.

**[E]** Cambiar globalmente el dueño del campo cliente haría que el ciclo de carga del invitado operara sobre X, incluida su inicialización/fase, y podría mezclar el destino local con escenas remotas ya vivas. No lo califico como «mejor» sin reconstruir todo ese contrato. Recomiendo conservar 2174 en el primer experimento de S2 y estudiar la convergencia de gestores como alternativa de arquitectura separada.

## 7. S1: valoración de la alternativa X

### 7.1 La medición propuesta es adecuada, pero tiene una laguna

**[V]** `guest_stage_diag.cpp` dispone de puntos en la entrada de vf13 `0x9ED290`, el dato previo `0x9ED401`, el dueño hallado `0x9ED453` y la instancia hallada `0x9ED48C`. Su código registra X mediante `manager_of(actor)` cuando alcanza el punto de dueño hallado. Fuente: `guest_stage_diag.cpp:252–258, 484–510, 722–770`.

**[V]** La salida temprana en `0x9ED401` ocurre si el low32 de `local_res8` es cero; después también se comprueba el high32 contra `0xFFFFFFFF`. Este dato procede del nodo/contexto del propio NPC antes de buscar al dueño del secuenciador. La función también puede salir antes por clave de secuenciador no válida. Evidencia: nueva decompilación de `0x9ED290` y desensamblado `0x9ED2A8–0x9ED415`.

**[E]** Si todos los NPC salen en `local_res8`, la sonda situada en `0x9ED453` no llegará a observar X, aunque X exista. Hace falta distinguir «X ausente» de «la búsqueda nunca se ejecutó». Una lectura protegida independiente del actor anfitrión ya registrado, en un punto seguro, puede cubrir esa laguna. No se ha añadido en este trabajo.

**[E]** Cero aciertos en `0x9ED431` no demuestra por sí solo la falta de una instancia: vf13 pudo no ejecutarse, tomar una salida anterior o quedar fuera del intervalo observado. Deben cruzarse entrada, clave, `local_res8`, resultado de búsqueda del dueño e instancia, con contadores de límites/pérdidas de las sondas.

### 7.2 Crear en X: correcto destino, contrato todavía incompleto

**[E]** Reconstruir en X el contexto visual de la escena que el servidor ya ejecuta es una vía de raíz para S1: apunta al productor del gráfico y al gestor que consulta el binding. Es más defendible que imponer una acción genérica. No doy por seguro ni suficiente «meter una instancia en X y filtrar C>S».

**[V]** `0xA9CA40` puede abortar la preparación si falta un actor enlazado (`0xA9CE81`), realiza trabajo asíncrono y usa una estructura de pendientes en `+0x170`. La consulta del NPC entra por `X+0x148`. **[E]** Un resultado inicial de éxito o incremento del contador de preparación no acredita todavía una instancia disponible para ese NPC ni un gráfico BodyAnimation instalado.

**[E]** El diseño debería resolver, en este orden:

1. **Dependencias completas.** Deben estar registrados y utilizables el actor anfitrión y todos los actores de tipo requerido del 2661, además del contexto de nodo que necesita cada NPC. Encolar un 2005 no prueba que su actor haya terminado de crearse. La barrera es por secuenciador y conjunto de enlaces, no «un par por cada NPC».
2. **Momento de enlace y ausencia de ciclos.** Determinar cuántas veces se llama vf13 y si vuelve a intentarlo. Esperar a un 2005 cuyo proceso de alta ya ejecutó vf13 puede ser tarde; esperar a que el NPC esté completamente listo cuando precisamente necesita la escena puede producir una dependencia circular. Conservar la creación/registro y la activación como hitos distintos y localizar una continuación nativa apta antes de diseñar la retención.
3. **Alta nativa completa en X.** Identificar la preparación, el nodo base, la finalización asíncrona y la disponibilidad a través de `X+0x148`. No escribir punteros manualmente en una tabla ni asumir que el contador que muestra la sonda es toda la población del gestor.
4. **Estado temporal de entrada tardía.** Una copia del último 2661 conserva el preparado original, no demuestra el tiempo actual de la animación ni los eventos ya consumidos. El anfitrión puede llevar minutos en la escena. Se necesitan el estado vigente de rama, tiempo y ciclo de vida; no repetir desde el principio una cinemática ni reproducir todos sus eventos históricos.
5. **Enrutado coherente durante toda la estancia.** Alta, actualizaciones 2981/2524/2111, borrado 2810 y nueva preparación deben referirse al mismo contexto X y a su generación. Cambiar solo el destino de 2661 dejaría los otros mensajes en M. Resolver duplicados, escenas acabadas, actores fuera de interés y salida/reentrada antes de habilitarlo para muchos NPC.
6. **Autoridad exclusivamente del anfitrión.** Bloquear peticiones del observador que pretendan conducir escenas ajenas usando sesión, propietario, campo, generación e id; no una lista global de ids pequeños ni un bloqueo indiscriminado de todas las escenas personales. Medir 2767/2591/2436 y auditar el resto de salidas asociadas, incluido el 2547 observado. No afirmar que esos tres opcodes agotan todos los efectos.
7. **Efectos locales auditados.** El filtrado de C>S protege la autoridad remota, pero no evita por sí mismo cámara, bloqueo de controles, interfaz, sonidos, teletransportes visuales o eventos locales disparados al reproducir. Seleccionar una escena sencilla, comprobar sus acciones y después ampliar. No considerar inocua una escena solo porque sea una réplica en el cliente.

**[V]** HC contiene C>S 2767×431, 2591×316, 2436×143 y 2547×1; GC no contiene esos cuatro opcodes. Estos recuentos muestran el comportamiento observado del dueño y del invitado antes de la réplica. **[E]** No demuestran que un X remoto recién preparado vaya a ser pasivo: eso debe medirse con la modificación concreta.

### 7.3 Alternativas

**[E]** Mi primera opción es estudiar una réplica visual en X que reutilice la preparación y el binding nativos, con seguimiento de estado y efectos delimitados. Si no puede separarse la reproducción visual de la conducción de escenas, la alternativa es un estado adicional explícito por NPC para reconstruir el contexto mínimo de BodyAnimation —recurso, tiempo, bucle y bindings—, usando su productor nativo después de verificar sus precondiciones. Los hashes genéricos, por sí solos, no contienen ese estado.

**[E]** La otra alternativa es dar al cliente un modelo completo del usuario anfitrión y conservar su dueño real en 2174. Puede reducir adaptaciones de enrutado, pero exige más investigación de inicialización, carga y destrucción. No la recomiendo como el siguiente cambio de la sesión con Shadow.

## 8. Recomendación final ordenada

### Tramo A: solo medir con Shadow

**[E]** Mantener la retención de S3 y las adaptaciones actuales, activar las sondas de lectura existentes (`guest_stage_diag=1`, `load_diag=1` y las necesarias de S1 en el invitado; `observer_stage_sync=1` en el anfitrión) y capturar simultáneamente ambos lados y el cliente local del anfitrión. Verificar la instalación y pérdidas de las sondas antes de interpretar ausencias.

**[V]** `observer_stage_sync` sigue siendo de medida: el código y `p8-observador-escenas.md` rechazan el modo de envío solicitado con 2 o más. **[E]** No usar `=2` como si esta revisión hubiera implementado o habilitado el experimento.

**[E]** Recoger una secuencia correlacionada por conexión y viaje: 2174, reinicio de M, 2144 realmente encolado y recibido, decisión dueño/no dueño, posible 2322, ambos predicados de `0x11`, estado de `0x12` y salida normal o abortada. Para M, medir listas, contador, condición temporal y, cuando se afirme ausencia de un id, la búsqueda o tabla correspondiente. Para S1, recoger las cinco etapas de §7.1 por los NPC concretos fallidos, no solo un total global.

**[E]** Separar también las pausas del hilo de red de la espera nativa de carga. Los diagnósticos de `p8-hilo-red.md` explican otro retraso medido de varios segundos; un cambio de instrumentación o transporte entre ejecuciones debe quedar registrado para no atribuir toda mejora al 2322.

### Tramo B: experimento aislado de S2

**[E]** Después de implementar y revisar aparte las condiciones de §4, probar un único 2322 nativo para la entrada compartida. Mantener 2174 y no introducir simultáneamente réplica de S1. Comparar una ejecución con emisión desactivada y otra activada, registrando la misma situación y los tiempos de recepción/aplicación. Una prueba puede cerrarse por límite de observación sin esperar otros 250 s; ese límite no debe enviar ni forzar nada.

**[E]** Criterios de aceptación:

- Un hito por entrada completada, dirigido solo a la sesión y actor correctos, después de su 2144; ningún hito durante una entrada abortada o perteneciente a otra generación.
- El handler nativo registra la escritura sobre el M esperado; `0xA97D60` devuelve verdadero por sus condiciones reales y se observa `0x11→0x12`, no `0x11→0x18`.
- La carga continúa por los estados siguientes y produce los hitos normales de finalización —correlacionar 2445/2762—, sin borrar listas ni forzar banderas. Registrar latencias; no imponer los 0–0,5 s locales como garantía de toda la carga remota.
- No aparecen preparaciones, temporizadores, acciones o alteraciones de campo del anfitrión debidas a esta emisión. S3 mantiene sus invariantes; S1 se cuenta por separado.
- Repetir entrada, salida/reentrada, reconexión y un cambio de campo; comprobar cancelación de trabajos pendientes y ausencia de aplicación atrasada. Una sola entrada exitosa acredita el mecanismo en ese caso, no su robustez completa.

### Después: prueba separada de S1

**[E]** Solo avanzar si se conoce el primer paso divergente de §7. Elegir una escena representativa con todos sus enlaces disponibles; comprobar X, finalización de preparación, búsqueda satisfactoria por el NPC, creación de `CC+0x70/+0x78` y restauración efectiva de la acción con recurso/tiempo coherentes. Exigir ausencia de conducción del invitado, efectos locales indebidos y divergencia al borrar/recrear la escena. Extender después al conjunto de 15 fallos y a entrada tardía.

**[E]** No hacer: reenviar ciegamente los 2661 del dueño a M; reescribir bindings del anfitrión hacia el Kliff invitado; enviar 2322 en bucle; llamar funciones completas de preparación de dueño para el observador; retirar 2174 sin resolver usuario y ciclo de carga; forzar `local_res8`, fases, listas o hashes; insertar punteros directamente; enganchar `.xtls` o sus thunks; declarar éxito porque desaparezca un error o se cierre la pantalla de carga.

## 9. Correcciones expresas a conclusiones anteriores

| Afirmación anterior | Corrección y prueba |
|---|---|
| Mi `p8-raiz-invitado.md`, §6: enviar solo 2322 se agrupaba con medidas que no podían ser solución final. | **[E]** Demasiado general aplicado a S2 con precarga propia vacía. GL:1308 y la rama de dueño muestran un hito concreto omitido; `0xA97D60` y L:87–88 admiten precarga vacía. Se mantiene la objeción para suplir estado propio realmente ausente y para presentar 2322 como solución de S1. |
| Mi informe anterior, §8: no se conocía cuál de los dos predicados bloqueaba. | **[V]** Actualizado por sesión 5: GL:1308 demuestra que el primero bloquea con `+0x231=1`. **[E]** El resultado del segundo tras el nuevo hito debe observarse; sus campos visibles apuntan a que no bloquearía, pero no se ejecutaba mientras fallaba el primero. |
| `p8-observador-escenas.md`, §5/§15: sin la instantánea del dueño, 2322 sería necesariamente una solución rápida prohibida. | **[E]** Esa necesidad no está justificada: instantánea en M y binding en X no son la misma dependencia. Aprobar completar S2 no implica aprobar réplica ciega ni afirmar que S1 esté resuelto. |
| M «nunca» debe contener secuenciadores y su estado nativo en campo ajeno es vacío (§16.C). | **[E]** Es una propuesta de separación de responsabilidades, no un hecho nativo demostrado para todos los observadores. Los precedentes son campos propios iniciales y vaciedad de precarga, no un inventario completo del gestor. |
| `+0x174` equivale a todas las instancias; cero aciertos de binding prueba que faltan instancias. | **[V]** Hay estructuras distintas (`0x1F6C190` frente a `0xA9CA40`) y salidas anteriores en vf13 (`0x9ED401`). **[E]** Ambas simplificaciones deben abandonarse para interpretar las sondas. |
| Los 2981/2524/2111 recibidos probablemente no hacen nada si falta su destino (§14.6). | **[V]** Confirmado estáticamente para las ramas examinadas: omiten la aplicación si la búsqueda devuelve nulo (§5.2). **[E]** Falta medir el resultado real de esa búsqueda en M; las listas vacías no lo demuestran por sí solas. |
| S3: «recibió 2214 acciones 2991 y todas se instalaron». | **[V]** La medida exacta es 2577 recepciones contabilizadas y 2214 entradas al consumidor, con 2214 instalaciones (GL:2377). La excepción posterior de salida se registra por separado. |

## 10. Cierre y preservación

**[V]** El proceso de Ghidra de esta revisión terminó con código 0. Se utilizó procesamiento local por lotes, sin abrir un servidor MCP. La comprobación posterior no encontró `java.exe` ni `javaw.exe`; no se terminó ningún proceso ajeno.

**[V]** Los diez archivos del proyecto original de Ghidra conservaron SHA-256, tamaño y fecha de modificación. Los manifiestos `W/original-before.json`, `W/original-after.json` y `W/original-comparison.json` conservan la comprobación. Se eliminó únicamente la copia propia, después de verificar su ruta y la ausencia de Java: **4.033.578.056 bytes** recuperados; decompilaciones, scripts y resultados permanecen en W.

**[V]** No se lanzó el juego, no se modificó su carpeta, no se cambiaron código ni configuraciones y no se hicieron commits. Dentro de la carpeta del juego solo se leyó la captura expresamente indicada en el encargo. El archivo `consulta-2322-observador.md` ya estaba sin seguimiento al comenzar y se conservó; el único archivo añadido al repositorio por esta revisión es este dictamen.
