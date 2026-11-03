# P-COMPAÑEROS — revisión independiente

Fecha: 2026-10-09. Build 1.0.0.2976; base `0x140000000`; direcciones RVA. Autor: orquestador, cuarta línea de investigación del equipo. Solo consultas de Ghidra, Capstone sobre la copia de `D:\RE`, código y capturas existentes. Ninguna prueba en juego ejecutada.

## Resumen y límite del diagnóstico

- [E][V] Hay **dos problemas distintos**: crear el hijo de un usuario cuyo personaje principal acaba de llegar, y activar las acciones de un actor remoto que llega con restauración 0. Corregir el segundo no demuestra ni resuelve el primero.
- [V] C1g corrige la acción inicial de Kliff: en la carga compartida lleva restauración 1. Su mercenario sigue llegando con 0 y sigue siendo rechazado. El log identifica `owner=0x9010052B`, `actor=0xB010052D`; no identifica la rama interna que falló.
- [E][S] El candidato de personaje principal todavía no listo es razonable, **pero no está probado**. `ConfirmSlot` puede saltarse precisamente esa comprobación cuando no logra adquirir una referencia al principal. Sin estados/retornos internos, afirmar que «35 ms implica fallo de estado 8» sería circular.
- [V] Tampoco existe una latencia universal de 0,3–0,6 s: los hijos propios de 112627 envían su primera Req a 0,372–0,386 s; dos NPC nuevos de C1g reciben su primer Ack a **11,57 ms**. El mercenario del peer no tiene primera acción en la grabación; `cdmp_peer` no ejecuta su IA.
- [S] Confianza alta en la ausencia de restauración y en el contrato de creación; media en la carrera de preparación; baja en identificar la rama concreta que rechazó el mercenario. El diseño siguiente separa una instrumentación decidible de la corrección condicionada a su resultado.

## Evidencias reproducibles

Las salidas nuevas están en [companeros/](companeros/): `capstone.txt`, `capturas.txt`, `log-c1g.txt`, `decomp_*.txt`, `xrefs_0x35D2A0.txt` y el script de análisis. Los nombres `cdmp_*` son anotaciones, no símbolos del fabricante. El script abre explícitamente la copia del EXE y se ejecutó con `-I -B`.

[V] El lector CDPK conserva todos los registros y los numera desde cero. Para 2005 exige captura completa y recorre `u16 longitud + registro` hasta el final. La cabecera de registro sin ese prefijo es: `u8 tipo`, `u32 clave @1`, `u32 dueño @5`, `u8 índice @9`, `u64 @0xA`. El CC se reconoce por su gramática serializada y una ruta `character/...app_xml`; no se interpreta un byte fijo de todos los personajes. Estas comprobaciones reducen falsos positivos, pero no equivalen a un lector completo de todos los componentes.

### 1. Cronología nueva, no copiada del informe anterior

| Evidencia [V] | Paquete / tiempo relativo | Resultado |
|---|---|---|
| `D:\cdmp-grabaciones\e3b.bin` | jugador #940 1,372401 s; mercenario #7247 1,406537 s | Diferencia **34,136 ms**; ambos restauración 0. Dueño `901002BF`, jugador `A01002C0`, hijo `B01002C1`, ranura 1. |
| `D:\cdmp-grabaciones\c1g.bin` | jugador aparcamiento #778 1,113258 s; Req2223 #788 1,113618 s | Primera acción pedida 0,360 ms después del registro privado. |
| C1g | jugador compartido #943 2,115022 s y #1076 2,119598 s | Restauración **1**, tras la Req anterior; no confundirlos con #778, que tiene 0. |
| C1g | mercenario #7431 2,131281 s | Restauración **0**, hash capa principal `866C7489`, secundaria vacía; ranura 1. No se observa 2223/2723/2991 dirigido a este actor en la grabación. |
| C1g | #944 2,115061 s | El peer recibe juntos al anfitrión `A0100001` y su mercenario `B0100002`; ambos restauración 1. Es un paquete con varias dependencias, no un solo actor. |
| `C:\Emulador CD\backups\logs\cdmp_20261009-0733_c1g.log` | 07:40:17.833 / 17.864 / 18.063 / 18.116 | Cliente anfitrión observa plantilla del PJ; error de creación del hijo **31 ms** después; entra en restauración de Kliff 199 ms después del error; finalmente sus dos capas tienen acción y reproducción. |
| C1g | NPC `B01005B2`, #11244 10,516600 s; primer Ack #11444 10,528170 s | Restauración 0 → primer Ack de acción en **11,570 ms**; el segundo NPC `B01005B3` da 11,569 ms. Son datos del túnel, no una sonda de punteros del cliente remoto. |
| `C:\Emulador CD\captures\cdmp_packets_20261003-112627.bin` | PJ #808 15,019232 s; recarga #1075 42,813323 s | El personaje propio existe **27,79 s antes** de su segunda carga. Su primera Req2223 fue 13,330 ms tras #808; la recarga lleva restauración 1. |
| misma captura | hijos #1449 42,898863; #1470 42,905679; #1517 42,911983 s | Tipos 4/5/6, claves `B0100002/3/4`, ranuras 1/2/3, restauración 0; primeras Req tras 0,385738 / 0,378693 / 0,372141 s. |
| misma captura | 592 primeras apariciones tipo 3 con CC identificable y restauración 0 | 592 primeras Req2223 posteriores; mediana **0,287055 s**. No extrapolar a todos los tipos ni a otros clientes. |

[V] Los tiempos del túnel y del log local son observaciones distintas; **no se restan entre sí**. En C1g el intervalo del túnel PJ/hijo es 16,259 ms, mientras que las líneas locales distan 31 ms. Ambos muestran orden, ninguno mide el estado `+0x5E` en el instante de `ConfirmSlot`.

### 2. Qué falla antes de desempaquetar el personaje

[E] Ghidra `8BD530` y Capstone `8BDC17–8BDC94` verifican: primero se lee dueño/índice/u64 y se llama `8AF580`; solo tras un resultado válido se desempaquetan los componentes. El texto coreano de creación y el retorno `8BDF7C` encajan con el error de creación; el retorno solo no distingue otros fallos que comparten logger. El log aporta también el texto.

| Condición candidata | Lo comprobado ahora | Qué falta |
|---|---|---|
| Dueño de clase incorrecta | [E] `8AF580` valida bits de clave. [V] `9010052B` y `901002BF` pasan la clase usuario. | No explica estas capturas. |
| Usuario no encontrado / no adquirible | [E] `8ADFE0`, resultado en `8AF61B`; búsqueda y estado válido. | [S] No hay sonda del resultado para el error. Recibir antes el registro de usuario no acredita que la referencia siga siendo válida. |
| Fallo de fábrica / referencia / componente | [E-x] `4634C0 → 898D890`; retorno observado desde `.rsrc 8AF696`. | [S] Ausencia de mensajes secundarios favorece otras ramas, pero no prueba éxito de ésta. |
| Contenedor de hijos ausente | [E-x] `898DAEC–898DAF7`, `[[user+68]+18]`. | No está en CDPK ni en el log. |
| Ranura ocupada | [E-x] `898DB03–898DB1C`, ranura indexada antes de `206B2D0`. | [V] Primer registro visible en ranura 1 no demuestra ranura cliente vacía. Tampoco hay prueba de que el `characterNo` duplicado la cause. |
| Confirmación de ranura | [E] `206B39A/39E`, retorno de vf+140. | Hace falta el código real devuelto. |
| Principal no preparado para adjuntar | [E] `206B49E` pasa máscaras 4 y 0x10 a vf+0xC0; `206B535–585` comprueba `main+5E & 0x10` y `main+4A`. | [S] Solo se llega a esta segunda prueba si la primera adquisición tuvo éxito. La falta de estado 0x10 **no basta** para demostrar la rama. |
| Inserción duplicada en mapa | [E] `8AF6D7` lee resultado de `463320 → 898D400`. | Falta resultado y contenido previo del mapa cliente. |

[E] La vf+0xC0 de `ClientChildOnlyInGameActor` es `4F8FF0`: pasa `R8W` a `1436A90`. Esta última admite la máscara de estado **o** una referencia preexistente de contexto (`1436910`, TLS). Por ello puede haber adquisición exitosa con principal aún no listo; pero también puede no adquirirlo y saltar el bloque de adjunción. El segundo caso no devuelve por sí mismo error de `ConfirmSlot`. El recorrido posterior vf+148 también puede fallar.

[E-x] La fábrica devuelve sus efectos de ranura al fallar `206B2D0` (`898DB3B–48` limpia provisional). No reintentar invocando la fábrica sobre objetos parciales ni convertir su error en éxito. Estas direcciones `.xtls` son evidencia estática, **no puntos de hook**.

### 3. Cómo carga el anfitrión y qué espera existe realmente

[E][V] El principal local se creó en el aparcamiento mucho antes. `8BD530` contiene un camino especial para el principal local ya existente, en vez de crear siempre otro actor. Las capturas confirman primera carga, primera acción, segunda carga y luego hijos. Esto rompe la equivalencia con un cliente vecino que ve por primera vez al usuario 2 poco antes de su hijo.

[E] Hay preparación asíncrona real: `4F9C60` programa `AsyncPostProcessForCreate`; `8B7B50` exige estado 8, `+CA`, ausencia de padre pendiente y recorre componentes; `4FC930` construye el grupo y espera a sus componentes; `4FD2E0` termina el grupo comprobando estado 8 y llama vf+78/vf+140. `+CF` se establece en otra etapa de `8B7B50` tras comprobar componentes. No son sinónimos `restaurado`, `+5E=0x10`, `+CF=1` y `registrado en mapa`.

[S] No se ha encontrado ni demostrado un Ack de «el vecino terminó recursos» que ordene al servidor emitir el hijo. El juego local tiene su propia fase de aparcamiento y agrupación; una espera fija de 35, 300 o 3000 ms no reproduce ese contrato. El caso `#944` obliga además a soportar padre e hijo en un mismo 2005 en P8.

### 4. Acción inicial: ventana real, pero duración y efecto dependen del receptor

[E] `8646D0` toma el lock del actor, y si `[[CC+40]+30]==0` devuelve éxito sin `RestartLayerAction`, `AfterRestoreAction` ni `ProcessPendingActions`. Con 1 usa hashes, gráficos y datos recibidos, llama `863880`, después `85ED00` y `2329D70`. No contiene una prohibición general de restaurar modo 2.

[E] `2329700` puede avanzar una capa con gráfico por `1F63F70` sin comprobar allí que su reproducción exista. `88E8E0` diferencia control local/simulado/remoto a partir de banderas, dueño y Vehicle; no es correcto reducir todos sus casos a comparar dos claves. [V] Los NPC propios emiten sus Req iniciales; [S] los remotos dependen de estado restaurable o de las acciones que reciba P5.

[V] Los datos prueban **un intervalo de protocolo** con restauración 0 antes de primera acción, no un puntero nulo durante toda esa duración. Las capturas no registran cuándo acaba la carga de recursos. [S] El cliente local puede ejecutar su IA y arrancar antes de que un consumidor peligroso use la capa; esa explicación es consistente con los registros. **No está demostrada una garantía universal de que los NPC locales nunca puedan caer**, ni una barrera exacta que proteja a todos. El informe previo del caballo incluye además incertidumbre sobre la identidad del actor del minidump: no se transforma esa atribución en evidencia nueva.

## Causa raíz y diseño de corrección

### A. Creación del mercenario: primero identificar la condición exacta

[S] La hipótesis de integración es publicar dependencias de un usuario remoto antes de que cada receptor pueda aceptarlas. Está demostrado el rechazo, no que la falta de preparación sea su causa concreta. La rama de rechazo sigue abierta. Una corrección de producción no debe forzar bits de actor, ocupar ranuras, saltarse `ConfirmSlot` ni cambiar `characterNo` esperando que lo resuelva.

[S] **Instrumentación mínima previa**, solo lectura sobre entradas `.rsrc` y retornos propios de wrappers: `8AF580` (argumentos, salida válida), `8ADFE0` (resultado), `206B2D0` (user/main/child, estado y resultado). Para separar fábrica/mapa y ramas intermedias usar mid-hooks exclusivamente observadores en `.rsrc`: `8AF61B` resultado de búsqueda; `8AF696` RAX fábrica; `8AF6D7` `*[RAX]`; `206B39E` EAX error de slot; `206B4B5` AL de adquisición principal; `206B535` principal y estado; `206B5DA` R13D resultado. Validar antes bytes/build/límites de lectura. No llamar desde el hook a helpers que creen nuevas referencias TLS: alterarían la condición que se observa. Emitir copia escalar a cola de log, sin escritura en actores.

[S] **Si se confirma preparación del principal**, barrera de dependencias en la entrada `.rsrc 35D2A0` (`ExecuteTroc`), antes de ejecutar vf2: limitarse a actores remotos del dominio de sesión opción D, validar opcode/longitud/época y todas las fronteras de registros. Si un 2005 contiene solo hijos cuyo principal remoto existe pero no está listo, devolver por el `int* res` el código nativo **leído** de `6CDB9D0`, sin llamar al original. No se inventa valor numérico ni se conserva un puntero prestado.

[E] `35D0C0` envía ese resultado a la cola `ClientNet+F0`, y `35CD50` reintenta y conserva/libera correctamente el `TrBufferPoolValue`. Se verificó con decompilado **y** Capstone. La reutilización para este nuevo motivo es un diseño [S], no comportamiento que ya ocurra con mercenarios.

[S] La barrera debe tener registro por `(época de conexión, destinatario, userKey, actorKey, secuencia)` y retener igualmente los Acks dependientes conocidos mientras falta el actor. Dar paso a cargas del principal, recursos y acciones que lo preparan: retener toda la sesión impediría resolver la dependencia. Cuando el actor esté listo, ejecutar una sola vez el original y después drenar en orden. Timeout inicial de prueba **3 s**, tope de memoria/paquetes explícito (propuesta: 8 MiB/4096 por destinatario), muerte/unload/cambio de campo/salida invalidan la época; nunca reproducir paquetes viejos en una sesión reconectada.

[S] **Límite de implementación que no se debe esconder:** si padre e hijo vienen en el mismo 2005 (`C1g #944`), aplazar el paquete entero crearía un bloqueo. Hay que dividir la lista validada en tramas 2005 por orden de dependencias conservando los registros byte por byte y la sesión/contexto de origen, o demostrar con sondas que el agrupamiento nativo ya admite ese caso y dejarlo intacto. No está validada aquí una ABI de constructor de contextos divididos: **no habilitar la división en P8 hasta verificarla**. Rechazar solo la incorporación cooperativa si el patrón no está cubierto; nunca bloquear al anfitrión. La corrección del caso simple C1g puede probarse sin esa división. Si falla ranura/mapa en lugar de preparación, corregir la duplicación/época detectada; esta espera no sería su arreglo.

### B. Acción inicial: corregir el contrato de activación

[S] Objetivo: un actor remoto no se publica como utilizable con gráficos cargados y ninguna acción. Hay dos vías; no confundir una con la otra:

1. **Vía preferida de autoridad:** conservar la inicialización de Kliff por Req2223 en el primer campo. En P8, el cliente que controla cada hijo/NPC debe producir su primera acción; el servidor la acepta antes de publicar a otros una instantánea restaurable. No basta retrasar el paquete 2005 ya serializado: ese paquete seguirá llevando 0. Debe volver a serializarse después de la acción y ordenar las actualizaciones posteriores por destinatario. `231FC90` es el empaquetador de CC y `2327320` la aplicación de acción, pero **no se ha verificado en este análisis un emisor completo de snapshots reutilizable para esta retención**. No proponer llamar a `231FC90` como si emitiera un actor entero.
2. **Vía cliente experimental, condicionada:** wrapper en entrada `.rsrc 8646D0`, exclusivamente para actor remoto nuevo de opción D con restauración 0, gráfico presente y hash/subid de la carga válidos. Resolver las acciones con el mecanismo nativo y restaurar los datos recibidos mediante la misma ruta `863880 → 85ED00 → 2329D70`, antes de habilitar actualización. No inventar hash `wait`, punteros de acción o reproducción ni copiar los de otro actor. El valor 0 indica ausencia de primera acción aceptada; convertirlo en 1 sin comprobar la acción sería solo una apuesta. **La revisión encontró que el original suelta su lock antes de esas llamadas finales:** una adaptación temporal del flag no es segura hasta resolver reentrancia y orden de bloqueos, y nunca debe restaurar 0 sobre una acción auténtica que haya llegado mientras tanto. Mantener la marca recibida fuera del campo nativo y comprobar resultado y capas. Esta vía inicializaría presentación local con el estado serializado del servidor, sin enviar Req ni reclamar autoridad de IA; la siguiente acción auténtica debe prevalecer. **Sigue siendo [S], no lista para incorporar**, hasta verificar ese contrato y la validez del estado inicial de cada familia de actor. Véase «Revisión» para los puntos exactos y la observación necesaria.

[S] No activar ambas variantes a la vez en la prueba. El peer no tiene IA de compañeros: necesita un guion de primera acción capturado y validado por tipo/clave/control, o se espera un fallo acotado; nunca fabricar una Req universal para todos. Para NPC nuevos controlados por anfitrión se dispone de Req/Ack reales con los que validar la vía elegida.

### C. Contenciones D-44/D-46

[E, lectura de código] `session2.cpp:129–146`, `remote_puppet.cpp:507–569` y `horse_diag.cpp:104–175`: ya hay una contención de reproducción vacía global en `1F63F70`; las que omiten actualización completa están acotadas a claves de copias/usuario 2. Por tanto «todas solo cubren al PJ2» es demasiado amplio.

[S] Mantenerlas durante diagnóstico. Generalizar las contenciones por estado solo a actores remotos de la sesión/época cooperativa, nunca por nombre/tipo de enemigo ni globalmente a NPC propios. Plazo 3 s y contadores por actor. Un timeout no significa quitar la protección y volver a ejecutar un puntero nulo: significa cancelar la incorporación remota por la salida nativa C4, mantener el actor aislado hasta su retirada y desactivar nuevas incorporaciones. No son arreglo de raíz; lectores externos al CC pueden seguir necesitando una barrera de activación. Retirarlas del camino normal únicamente cuando las pruebas registren cero activaciones y las capas sean válidas; conservar comprobaciones diagnósticas durante P8.

## Una prueba propuesta y sus criterios

[S] **Una sesión instrumentada, dos fases sin reiniciar:** anfitrión quieto, `cdmp_peer` C1g y compañeros; primera incorporación solo observa las ramas y acciones, sale por C4; segunda incorporación activa únicamente la corrección correspondiente a la rama observada y provoca la aparición de NPC nuevos. No cambiar el arreglo a ciegas en la misma ejecución: preparar antes las variantes desactivadas, o realizar solo la fase observadora si la evidencia contradice la hipótesis. No se ejecutó esta prueba.

[S] Líneas nuevas esperadas (propuestas):

```text
CompCreate epoch=… user=… child=… slot=… find=… factory=… confirm=… map=… mainState=… mainRef=…
CompGate hold/release epoch=… actor=… reason=… ageMs=… queuedBytes=…
CompAction actor=… remote=1 receivedRestore=0 init=serialized/native-ack result=0 layersValid=1
CompReady actor=… state=0x10 loginComplete=1 pending=0 guards=0
CompEpochEnd epoch=… queued=0 nativeRemoved=1
```

[S] Éxito: identifica una rama única del rechazo; la corrección evita ese fallo y no oculta otro; usuario/mercenario/caballo se crean una vez con ranuras correctas; primeras acciones válidas antes de usar capas; NPC nuevos no quedan congelados; sin escrituras persistentes y el anfitrión conserva sus actores. Para P8 repetir la matriz en cliente real, incluido 2005 mixto, y observar sus punteros: `cdmp_peer` no puede demostrar animación remota. Para aforo 4, ampliar a tres invitados con dependencias y límites independientes; una sesión lenta no bloquea las otras ni el reloj.

[S] Parada: estado/ABI/cabecera no reconocidos, error de slot o mapa, principal que nunca progresa, contador creciente, timeout, alteración de actor propio o paquete mixto no soportado. Desactivar nuevas incorporaciones, descartar la época y usar C4; no borrar manualmente actores/listas ni soltar sin referencias una cola del motor. Mientras C4 no esté validada, no prometer recuperación limpia de una prueba fallida.

## Qué queda sin confirmar

[S] Rama exacta del mercenario y qué valor la causó; contrato completo de agrupación local en todas las variantes; duración real de punteros vacíos en cliente P8; validez de restauración de estado inicial en todas las especies; ABI para separar 2005 mixtos y para generar snapshot nuevo; seguridad completa de compañeros de partidas distintas. Son límites materiales, no detalles opcionales. No están resueltos por Ghidra estático ni por el éxito de C1g.

## Revisión

[E][V] Revisión independiente del agente de identidad, 09-10-2026: nuevas consultas al único Ghidra de solo lectura, Capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` con `PE(path)` explícito, y relectura independiente de CDPK mediante el lector del proyecto. Evidencias en [identidad-evidencias/](identidad-evidencias/): `decomp_0x206B2D0.txt`, `decomp_0x1436A90.txt`, `decomp_0x35D0C0.txt`, `decomp_0x35CD50.txt`, `decomp_0x35D2A0.txt`, `decomp_0xCB4160.txt`, `decomp_0x8646D0.txt`, `decomp_0x2329D70.txt`, `decomp_0x232AC80.txt`, `decomp_0x2327320.txt`, `capstone_206b49e.txt`, `capstone_4f8ff0.txt`, `capstone_35ce08.txt`, `capstone_cb4160.txt`, `capstone_cb43ea.txt`, `capstone_8646d0.txt`, `capstone_864aea.txt`, `capstone_2328890.txt`, `revision-comp-capturas.json`. El script de contraste es `<scratch>\pendientes\identidad_review_comp.py`; se ejecutó con `-I -B`. No se ejecutaron pruebas en el juego.

1. **«El mercenario falla porque el principal no está listo».** [E] El intento de confirmarla falla: `206B49E–206B4AF` pasa 4/0x10 a vf+C0, pero `206B4F7` salta a `206B6BC` si no obtuvo referencia. `4F8FF0` mueve R8W a EDX antes de saltar a `1436A90`; esta última contempla el contexto TLS de `1436910`. Por tanto se confirma la prudencia del informe: ni el tiempo ni el bit por sí solos identifican la rama. [V] La relectura independiente reproduce C1g PJ #943 2,115022 s e hijo #7431 2,131281 s, y e3b #940/#7247 a 1,372401/1,406537 s. Ninguna captura contiene el resultado interno de `ConfirmSlot`. [S] Se corrigió arriba la frase «defecto ... demostrado»: el orden temporal demuestra exposición a una dependencia, no causalidad suficiente del rechazo.

2. **«Una barrera en 35D2A0 puede usar la cola nativa».** [E] Confirmado para el despacho externo: `35D0C0` retiene en +F0 al recibir `*6CDB9D0`; `35CE1E` vuelve a ejecutar `35D2A0`, `35CE23–35CE34` compara ese resultado y retiene el objeto, y la otra rama lo destruye/libera. [S] Esto conserva su vida útil, pero no aporta por sí mismo timeout, límites ni orden respecto a mensajes que adelanten al retenido. Esos contratos siguen perteneciendo al diseño y se deben comprobar; muerte/unload nunca puede dejar que una carga vieja reaparezca posteriormente.

   **Corrección de cobertura obligatoria para P8:** [E] `ScopeSenderAck` 2042 usa la entrada `.rsrc CB4160`; `CB43F4` busca el Troc y `CB4448` llama directamente a su vf2. Ese submensaje no vuelve por `35D2A0`. `CB444D` reconoce también `*6CDB9D0` y mantiene estado de reintento por submensaje. [V] El nuevo conteo da cero 2042 en C1g, e3b y 112627; no demuestra que P8 nunca lo use. [S] Una barrera que solo reconozca 2005 en la entrada externa no cubre la carga anidada. Antes de habilitar P8 se debe observar y admitir explícitamente el contrato de 2042/submensajes, incluidas sus vidas útiles y dependencias mixtas, o cancelar de forma segura la incorporación cooperativa si aparece un contenedor no soportado. No tratar 2042 como si fuera un 2005 ni copiar un contexto prestado. La entrada `.rsrc CB4160` es un punto de observación posible; no se propone modificar su llamada interna ni una ABI de división no verificada. No se certifica aquí soporte P8 de contenedores.

   [V] También se confirmó que C1g #944 contiene tipos 7, 0, 1, 0 y 6: incluye al anfitrión `A0100001` y su hijo `B0100002`. Por tanto el bloqueo por aplazar el paquete mixto entero es un riesgo concreto, no meramente hipotético.

3. **«Restauración 0 omite el arranque, y su ventana es 0,3–0,6 s».** [E] Se confirma únicamente la primera parte: `8646D0` retorna sin la secuencia de arranque en la rama 0; la rama 1 llama `863880` en `864AF9`, `85ED00` en `864B0D` y `2329D70` en `864B15`. [V] La relectura independiente confirma el NPC `B01005B2` a 10,516600 s y su primer Ack2723 a 10,528170 s: 11,570 ms. No hay 2223/2723/2991 para `B010052D` en C1g. La formulación universal queda refutada y el informe ya la corrige. Ninguno de esos tiempos mide punteros de reproducción.

   **Límite adicional de la variante cliente:** [E] `8646D0` libera su lock tras copiar los descriptores y antes de ejecutar las tres llamadas finales. `2329D70` puede procesar pendientes mediante `232AC80`, que llama a una vf+2C8; `2327320` contiene la escritura auténtica a `[[CC+40]+30]=1` en `2328890–2328894`. No se demostró en esta revisión que toda clase concreta use esa vf+2C8 para dicha escritura. [S] En consecuencia, «adaptar el flag bajo lock y restaurarlo al volver» **no constituye aún una receta segura**: no se debe mantener un lock exterior sobre llamadas que el original hace fuera del suyo sin verificar reentrancia/orden, ni restaurar ciegamente 0 después de que una acción real pudiera haber actualizado el estado. La marca recibida debe mantenerse fuera del campo nativo; cualquier solución debe distinguir inicialización provisional de acción auténtica y dejar prevalecer esta última. Hasta verificar ese contrato, la variante es experimental condicionada, no un arreglo listo para incorporar sin más. La vía de autoridad evita esa manipulación, pero conserva la laguna ya declarada de su emisor de snapshot.

[S] Resultado de la revisión: los datos y la incertidumbre de la rama raíz se sostienen; la cola externa existe. Se añaden dos límites materiales de implementación: contenedores 2042 y atomicidad/reentrancia del flag de restauración. La única prueba propuesta debe añadir contadores de 2042/submensajes y trazas de transición del flag/acciones pendientes, con parada si el patrón no está cubierto. Ninguno exige escribir o enganchar `.xtls`.
