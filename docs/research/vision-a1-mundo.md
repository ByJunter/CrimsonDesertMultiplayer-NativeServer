# Visión A1: un mundo compartido, objetivo ocho jugadores y techo diez

Build **1.0.0.2976**, timestamp PE **0x6AB28F00**, base **0x140000000**. Todas las direcciones son RVA. Investigación sin ejecutar el juego ni modificar el mod. Análisis terminado y revisión independiente de las diez afirmaciones principales completada; alcance y correcciones en §12.

Leyenda: **[V]** captura/observación viva identificada; **[E]** desensamblado `.rsrc`; **[E-x]** código `.xtls` (no se engancha); **[S]** inferencia o diseño; **[R]** fuente externa no contrastada. Los informes antiguos que llaman [V] al estático se reclasifican aquí.

**Actualización transversal de las pruebas:** [S, D-28] antes de la parte de E1 que provoca un guardado rechazado y de cualquier ensayo deliberado de blob inválido, disco lleno, corte de guardado o crash, debe completarse **RPT-0** de [A7 §4.3](vision-a7-transversal.md). P6 bloquea escrituras, pero no aísla lecturas ni adjuntos de reportes. [E/E-x] La ruta `0x2355700 → 0xEF51850 → 0x2356270 → 0x1317D40` puede solicitar adjuntos de la ranura fallida al SDK de Sentry, bajo condiciones; no se observó transmisión. Esta precondición completa todas las secuencias de pruebas que aparecen después, sin ejecutar ni habilitar ninguna.

## 1. Veredicto y unidades

**Un campo compartido es una hipótesis implementable, todavía sin demostración con dos clientes.** El límite explícito observado es un usuario nuevo por campo: `ServerField::AddActor`, `.xtls 0x1064C806`, rechaza al usuario ausente de la lista cuando `field+0x2A8 >= 1` [E-x]. E3b propone rodear esta condición usando las entradas `.rsrc` `0x2B07790` y `0x2BC04F0`, sin modificar el cuerpo protegido. Ocho personas es el objetivo y diez el techo de diseño, no capacidad probada; la sesión técnica adicional se contabiliza aparte como se explica abajo. Fuentes: `opcion-d-campo-compartido.md` §1–3; `vision-a0-base.md`.

**U significa sesiones `ServerUserActor` simultáneas; H significa personas jugando.** Si el anfitrión también juega, U=H. Si mantiene un personaje técnico aparte, U=H+1: ocho personas necesitan nueve sesiones. U=10 permitiría nueve personas remotas mientras haga falta ese personaje. Esta es una restricción operativa prudente del proyecto, **no un límite de nueve humanos descubierto en el motor**. Diez personas sin anfitrión humano exige eliminar la sesión técnica; no se amplía el diseño a once sesiones [S, D-22].

La reserva común inicial tiene 15.501 claves; cada usuario y sus cien hijos consumen 101. Quedan, antes del mundo, **15.299 / 15.097 / 14.693 / 14.491** para U=2/4/8/10 [E/E-x para estructura; S para aritmética; `0x2A7DEA0`, `0x2A83310`, `.xtls 0x10538980`; `opcion-d-d0.md` §1]. No se debe restar el número acumulado de `ActorLoadDataAck`: los actores se retiran, reaparecen, se reciclan y un cliente no observa todos los actores del SAM.

Los límites más próximos son **semánticos**: E3b, dependencia del dueño del campo, P10, contenido por jugador, guardado y desconexión. La aritmética de claves no demuestra un servidor de ocho; tampoco obliga todavía a bajar el objetivo. Recomendación [S]: validar dos, luego cuatro, y permitir ocho sólo después de medir zonas separadas y salida del controlador. Diez es una ampliación condicionada a esas mediciones.

## 2. Pasar de sesión 2 a U=10

### 2.1 Estructuras y destinatarios

| Elemento | Resultado y evidencia | Riesgo / prueba que lo decide |
|---|---|---|
| Sesión de red del servidor | `ServerNet+0x18` es una única sesión; el bucle `0x27735C7` la pasa a `OnRecvPacket 0x2773C20` [E; D0 §4]. Cada sesión adicional ocupa 0xD0 y tiene su usuario en `+0x90`; no se copia con `memcpy` | Tabla del mod de hasta diez entradas, identificador y época por conexión; E2 con 2/4/8/10 cuentas diferentes, nunca sobrescribir la sesión del anfitrión |
| Implementación actual | `src/mod/session2/server.cpp:48` usa un solo `g_session2`; cola global de salida 32 MiB en `:32/:64`, espejo global con 4.096 entradas en `:82`; P5 distingue sólo anfitrión y usuario 2 en `:381` [estado del repositorio] | No basta repetir el guion diez veces. Hay que separar destino, cola, cuenta, cancelación y época por conexión. No se ha programado en esta investigación |
| `ClientNet+0x38+i*8`, 17 entradas | Canales/clases de sesión del cliente; slot 4 es conexión virtual al servidor [E; `OPCION-D-EMULADOR.md` §2.2]. No son diecisiete jugadores | Mantener una conexión remota por cliente; no usar este tamaño para afirmar aforo 17 |
| Hijos del usuario | Cien huecos reservados por usuario, no cien usuarios; índice Troc firmado selecciona hijo/usuario, `0x2773C20`, `0x206AD10` [E; D0 §1] | Los acompañantes consumen claves ya reservadas; los objetos de mundo de equipo/spawn pueden consumir otras. E2 mide −101; E3 mide lo adicional |
| `ToNear` | `0x2A75860 → 0x2BC51E0 → 0x2BC4F70`; observadores del sector, `usuario+0x1F8`, vector que crece mediante `0x35E3F0`; nueva lectura `0x2BC5071–0x2BC50FD` [E; `vision/a1/d_2bc4f70.asm`] | No hay tope 2/4/8/10 en este tramo. Comprobar vector de U sesiones y ausencia de duplicados cuando todos están juntos |
| `ToField` | `0x2AFFBF0` copia/recorre `field+0x2A0`, llama `vf(0xE8)` por usuario y añade su sesión [E; nuevo volcado y E3b §1.3] | Incluye usuarios que estén en ese campo; no incluye automáticamente otros niveles del mismo mundo. Medir U destinatarios dentro del campo |
| `ToWorld` | `0x2A75F00 → .xtls 0x104EB5B0 → 0x2A83A40`. Buffer inicial de **1.000** attachers (`0x104EB5E6`), recorre la lista y omite `+0x1F8==0`; vector de salida dinámico [E-x/E; nuevos volcados] | La capacidad inicial 1000 no es un límite 10 ni una autorización para escalar más. U≤10 queda por debajo; comprobar que los desconectados no reciben |
| `ExceptOneControl` / `ExceptOneSelf` | `0x2A759B0` / `0x2A75880` no escriben destinatarios; cotejo de sus cuerpos [E; nuevos volcados, D0 §9]. P5 actual los rodea | Generalizar: ejecutar `ToNear`, deduplicar y excluir la sesión del controlador, o la propia sesión del actor para Self. No excluir todos los hijos por accidente ni convertir un NPC sin sesión propia en difusión global |
| `ToNearAndParty` | `.xtls 0x104E6420` llama `ToNear`, recorre miembros de 16 B en `grupo+0x58`, cantidad `+0x60`; busca cada sesión y deduplica contra la salida (`0x104E6580–0x104E65D7`) [E-x; nuevo volcado] | Es selección de destinatarios. No es algoritmo de reparto de botín/XP; §7 demuestra restricciones adicionales del grupo |

Coste mínimo de relevar un flujo por jugador a los demás, estando todos juntos: **U(U−1)=2/12/56/90 entregas por ronda** para U=2/4/8/10 [S aritmética sobre buscadores]. El coste de NPC se multiplica por sus observadores, no por el total de cuentas registradas. Decenas de cuentas guardadas en disco no necesitan decenas de usuarios residentes [S; A4 debe cerrar carga/descarga].

### 2.2 E3b extendido a diez usuarios

`field+0x2A0` es vector de entradas `{ServerUserActor*, contador}` de 16 B; `+0x2A8` es número de usuarios y `+0x2B0` total de actores de usuario [E/E-x; E3b §1.3]. El rechazo de `.xtls 0x1064C806` se aplica cuando el usuario **no está**. Insertar `{u,0}` bajo los locks en orden `field+0x18 → [field+8]` antes de entrar hace que el original tome la rama existente, incremente los contadores y añada personaje/acompañante [S apoyado por E-x; E3b §3.2]. El mismo principio sirve para u2…u10; no demuestra que nueve inserciones concurrentes sean seguras.

Diseño prudente [S]: altas al campo serializadas, una transacción por usuario; comprobar identidad/generación del campo, límite externo U≤10, usuario aún válido, inexistencia previa, entrada n=0 revertible si falla. No incrementar a mano también `+0x2B0`: lo hace `AddActor`. El campo reutilizado debe tener `out+0x20=0`, pues `MoveFieldOnceTimer 0x2BC006A` cerraría el campo si la entrada falla y lo considera nuevo [E; E3b §2.2]. Al salir, usar salida nativa y exigir vuelta exacta de contadores. No llamar manualmente al thunk protegido de `AddActor`.

**Un solo mundo no equivale necesariamente a un solo `ServerField` para todo el mapa.** E3b sólo comparte el nivel actual del anfitrión; para otro nivel cae en el original y crea una instancia por usuario [E/S; `0x2B07790`, E3b §3.1]. La arquitectura final necesita un registro de mundo `{época del mundo, FieldInfoKey → única instancia nativa activa}` [S], de manera que dos usuarios en el mismo nivel compartan instancia y dos niveles distintos pertenezcan al mismo mundo persistente. Ese registro debe conservar propietario, referencias, stages y guardado; no basta cambiar el puntero del campo.

Riesgo principal: el dueño `field+0x180` sale, viaja o muere y deja a los demás en un campo que todavía lo consulta (`0x20DF100`). E3b inicial prohíbe viajes; persistencia necesita dueño estable o separación explícita del estado del mundo. El cierre `0x2AFEA00` depende de `+0x2B0==0`, no sólo de referencias: mantener un puntero no mantiene por sí solo el campo abierto [E; E3b §3–5]. Prueba: entradas/salidas 2→4→8→10, incluidas salida del primero y del último, viaje del propietario y reentrada de una cuenta.

## 3. Medición nueva de actores y regiones

### 3.1 Método reproducible y límites

Script nuevo: `C:\Emulador CD\build\research\vision\a1\capturas.py`, ejecutado con el Python indicado y `PYTHONDONTWRITEBYTECODE=1`. Sólo lee las ocho capturas de `C:\Emulador CD\captures`. No importa `pk.py` ni genera sus cachés. Cabecera y registros CDPK conforme a `src/common/capture_file.h`; SHA-256 de cada captura incluido en [vision-a1-capturas.json](data/vision-a1-capturas.json).

- Se recorren cronológicamente **todos** los registros `[u16 longitud][registro]` de cada `ActorLoadDataAck2005`. Se filtran claves clase 0xB, índice<15.501, excluyendo claves sintéticas del mod. Se retiran las claves de `RemoveActorAck2516`, payload+6, y se reinicia el conjunto por `MoveFieldAck2174` [V con formato de `enemigos-del-anfitrion.md` §1/ §3 y `opcion-d-login.md` §4].
- **Cero `ScopeSenderAck2042` en las ocho capturas.** No se omitió contenido agrupado de ese opcode en estas muestras; no se presume que futuras capturas tengan esa propiedad [V; JSON].
- 105149 es versión 1, truncada a 1 KiB. Se cuenta la cabecera del primer registro aunque su cuerpo esté cortado; no se pueden recuperar otros registros tras un cuerpo cortado. Las demás son versión 2, cero paquetes truncados [V].
- El resultado es **conjunto de actores de mundo cargados reconstruido a partir del cable**, no lectura del SAM ni garantía de actor visible en pantalla. `RemoveActorAck` puede ser pérdida de interés, no liberación de clave; actores fuera de interés y actores sólo servidor no aparecen. El inicio de algunas capturas tampoco demuestra conjunto inicial vacío. No se convierte el máximo en una cota superior del consumo SAM [S, límite de observabilidad].
- Se incluyen acompañantes clase 0xB si aparecen; algunos proceden de los cien hijos ya reservados. Multiplicar esta muestra por U y añadir 101U puede contar algunos dos veces: es un modelo orientativo, no contabilidad exacta [S].

### 3.2 Resultado

Todos los números de esta tabla son **[V] medición de captura**; fuente JSON y [serie por segundo](data/vision-a1-visibilidad-segundos.csv).

| Captura 20261003 | Segundos | Claves mundo distintas acumuladas | Pico del conjunto cargado | Momento del pico | Observación |
|---|---:|---:|---:|---:|---|
|105149|1.163,64|3.993|281|527,51 s|3.993 cuerpos mundo truncados; cota parcial|
|112627|856,45|3.664|821|580,09 s|Completa|
|125936|174,19|237|144|105,87 s|Completa|
|131004|166,65|711|149|57,92 s|Completa|
|131648|255,90|402|136|58,85 s|Completa|
|134008|183,97|473|143|77,51 s|Completa|
|160139|742,89|1.155|240|455,67 s|Completa|
|200328|1.845,76|7.655|1.006|1.802,57 s|Completa|

En 112627, el pico 821 se descompone en tipo 3=280, tipo 4=12, tipo 5=1, tipo 6=32, tipo 7=496. En 200328, pico 1.006: tipo 3=356, tipo 4=12, tipo 5=4, tipo 6=105, tipo 7=529 [V; JSON]. Por tanto, medir sólo enemigos hostiles infravalora mucho el presupuesto: **gimmicks/objetos constituyen aproximadamente la mitad** de estos dos picos [V/S, división sobre esos datos].

No hubo una clave completa cargada dos veces en el conjunto o recargada tras retirada en estas ocho capturas [V; JSON]. Esto no prueba ausencia de duplicados de contenido: dos spawns duplicados pueden llevar claves diferentes. Tampoco prueba generaciones 2+; el índice máximo de 200328 fue 7.752, por debajo de 15.501. La diferencia entre 7.655 acumulados y 1.006 simultáneos ilustra por qué las altas acumuladas no equivalen a ocupación.

### 3.3 Regiones: superposición, no suma de zonas

`RegionInOutReq3067` = clave u32, región u16, entrada u8, byte final; `0x2979070` [E, `opcion-d-login.md` §4]. Se asocian las claves de mundo que el cliente comunica a cada región y se mide su intersección con el conjunto cargado. Tabla completa: [vision-a1-regiones.csv](data/vision-a1-regiones.csv) [V]. No se han asignado nombres geográficos a IDs sin tabla contrastada.

| Captura | ID de región | Claves distintas asociadas durante toda la captura | Pico cargado y dentro de esa región |
|---|---:|---:|---:|
|112627|1|3.645|798|
|112627|4 /13|3.644 cada una|797 cada una|
|112627|1201|1.175|739|
|112627|10011|2.023|180|
|112627|11005|1.252|790|
|112627|41127|267|62|
|200328|1|7.590|982|
|200328|4 /13|7.589 cada una|981 cada una|
|200328|10000|7.484|980|
|200328|11005|7.356|974|
|200328|1201|3.552|906|
|200328|41121|3.216|342|
|200328|41202|349|132|

Los mismos actores están en varias regiones: **no sumar estas filas** ni llamarlas catorce zonas independientes. Una «zona activa» en el presupuesto de §4 será un conjunto espacial disjunto de spawns que mantiene un grupo de usuarios, no un ID `RegionInOut` [S].

### 3.4 Radio observado

Para cada registro completo se busca el último Transform con escala (1,1,1), padre FF×8, cuaternión normalizado y coordenadas finitas; se descartan coordenadas horizontales de origen sin significado en estas escenas. Se calcula distancia XZ al último `MoveActorReq` del jugador (`payload+4`, nunca `+0x94` a pie), o a `MoveFieldAck+25`, sólo si tiene ≤1 s [V con extracción heurística S; formatos en `mirror-ack-formats.md` §1 y `enemigos-del-anfitrion.md` §1.2]. Datos por muestra: [vision-a1-radio-observado.csv](data/vision-a1-radio-observado.csv).

| Captura | Muestras válidas | Distancia mediana | P90 | P99 | Máxima observada |
|---|---:|---:|---:|---:|---:|
|112627|2.500|23,5 m|80,1 m|255,7 m|507,1 m|
|131648|325|23,7 m|77,7 m|80,2 m|91,6 m|
|200328|5.617|23,9 m|79,8 m|118,1 m|579,7 m|

**No existe un radio único demostrado de 80 m.** Son distancias de altas observadas; scripts, gimmicks, escenas, actores montados, retraso de posición y la heurística del Transform alteran la distribución. No se infiere densidad por km² ni número de zonas de un círculo. Confirmación en vivo: caminar desde el mismo punto en ocho direcciones y registrar sector/radio nativo, altas/bajas, referencia de spawn y `SAM+0xD0` simultáneamente; repetir ciudad, campamento y fauna, incluyendo carga de otro jugador muy lejos.

## 4. Presupuesto de claves y agotamiento

### 4.1 Qué se sabe y qué falta

`SAM::Initialize 0x2A7DEA0` suma cuotas en `param+0x44` y llena la FIFO; `0x2A82D57` falla si la cuenta es 0, y `0x2A82DC6` extrae de la cola. `0x2A846B0` devuelve índices al final bajo lock conservando bits de generación [E; nuevos volcados]. D0 demuestra 101 por usuario y el valor inicial 15.501; la observación histórica `sam_dump1.txt` da libres 15.318 en una toma (183 ocupadas), **una instantánea, no máximo** [V histórico; `pendiente-rango-claves.md` §1.3].

Definir `Wreal`=claves de mundo residentes, `M`=reserva operativa. Condición de admisión [S]:

`101U + Wreal + M <= 15501`.

No se conoce `Wreal` por zona en las capturas. Para convertir el pico visible 1.006 en presupuesto hace falta medir factor oculto, actores retenidos tras salir y duplicación por usuario. El dato que decide es la caída de `SAM+0xD0` por zona y su recuperación al abandonarla, no la cantidad de Ack.

### 4.2 Escenarios numéricos, explícitamente condicionales

Se usa **M=3.100** (aproximadamente 20% de la reserva) como margen propuesto, no constante nativa. Se comparan tres costes de zona disjunta:1.006 como escenario optimista inspirado en la captura,2.000 y 4.000 como sensibilidad [S]. **No son tres clases reales de región.**

| U | Reserva tras 101U | Zonas de 1.006 sin margen | Zonas de 1.006 con M | Zonas de 2.000 con M | Zonas de 4.000 con M | Máximo medio por zona si hay U zonas y M |
|---:|---:|---:|---:|---:|---:|---:|
|2|15.299|15|12|6|3|6.099|
|4|15.097|15|11|5|2|2.999|
|8|14.693|14|11|5|2|1.449|
|10|14.491|14|11|5|2|1.139|

Estas cifras de zonas son divisiones aritméticas, **no diseño para más de diez jugadores ni garantía de cargar esas zonas**. Para ocho zonas dispersas a 2.000 claves/zona el presupuesto falla (16.000>11.593 con M); para diez zonas a 1.006 sobran sólo 1.331 claves por encima del margen (11.391−10.060). Para diez a 2.000 también falla. Juntos y con deduplicación efectiva, el mundo se aproxima a **una zona compartida** más equipo/acompañantes; dispersos, se aproxima a la unión de zonas activas. Solapamiento reduce consumo; duplicación por solicitante lo aumenta [S].

| Personas H con anfitrión técnico aparte | U | Reserva tras 101U | Capacidad con M antes del mundo |
|---:|---:|---:|---:|
|2|3|15.198|12.098|
|4|5|14.996|11.896|
|8|9|14.592|11.492|
|9|10|14.491|11.391|
|10|11|Fuera del sobre de diseño|No se propone|

La última fila no declara imposibilidad técnica de once: conserva el techo prudente de diez sesiones durante esta investigación. Diez humanos requieren que anfitrión humano cuente dentro de U, o quitar la sesión técnica [S].

### 4.3 Cuando se acaba y posibilidad de ampliar

El agotamiento devuelve **`eErrNoInvalidActorKey`**, global `0x6CDB9E4`, sin fabricar otra clave [E/E-x; `0x2A82D57`, `.xtls 0x10538980`; D0 §1.3]. En creación de usuario la ruta revierte claves ya reservadas y el login va a error; no se deduce que todo spawn complejo sea atómico [E/S]. Consecuencias probables: rechazos de login, NPC/gimmicks ausentes y misiones que no arrancan, antes de una eventual cascada de errores [S]. No se debe provocar agotamiento con una partida valiosa.

**Hay una vía estática para ampliar sin parchear `.xtls`:** inicializar el SAM con N=10 en lugar de 1 eleva su total a 16.410, **sólo 909 claves adicionales**, no multiplica por 10 la capacidad [E para suma, S para propuesta; `0x2A7DEA0`, D0 §1]. Un candidato de entrada `.rsrc` es `0x2A7DEA0`, que recibe N en r8d; sólo al arranque, antes de actores, sin cambiar el byte monojugador ni el modo del módulo. Primera secuencia 16 B: `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57`. AOB exacta 32 B única en todo el exe: `48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 20 41 8B F0 4C 8B FA 48 8B` [E; `firmas.py`, [vision-a1-firmas.json](data/vision-a1-firmas.json)]. **Candidato no habilitado ni recomendado todavía.**

Cambiar cuotas de mundo antes de inicializar podría añadir más; requiere auditar todos los consumidores y registros que dependen de `param+0x44`, no sólo la cola. Los 20 bits del índice son un formato, no permiso para reservar un millón de actores. Ampliar una cola viva manipulando cabeza/capacidad está descartado; también ampliar de modo que colisione con los índices del sistema de copias. La primera respuesta al consumo excesivo debe ser corregir duplicados/liberaciones y limitar zonas activas, no agrandar la reserva [S].

Prueba por etapa: registrar libres mínimos, ocupadas por tipo, tasas de alta/baja y cola al volver a la zona inicial; aprobar U=2/4/8/10 sólo si queda margen M con todos dispersos y tras viajes repetidos. Ocho o diez juntos y ocho o diez dispersos son perfiles distintos, ambos necesarios.

## 5. P10: control de IA y dueño con época

### 5.1 Asignación y límites nativos

Corrección de dirección: **`0x2792A11` lee/comprueba `actor+0x90`; la escritura es `0x2792A7B`**, dentro de `0x2791CD0` [E; `vision/a1/d_2792a11.asm`, contraste independiente A0 y §12]. Si no había controlador, usa `desc+0x40 → clave`, o el usuario padre `actor+0xA0`, o la clave del contexto `ctx+0x180`; no sustituye un controlador ya fijado [E; D0 §7.3]. Precisión de la revisión independiente: el salto `0x2792A1A` lleva también el valor previo no nulo a la escritura `0x2792A7B`, que lo reescribe sin cambiarlo. **Sólo la selección de un controlador nuevo está condicionada a que el anterior sea cero; la instrucción de escritura no lo está** [E; `vision/verificacion/a1/f_2792a11.asm`]. La ruta de creación y el mundo común tienen por tanto que acordar quién solicita/posee cada spawn.

El servidor valida propiedad de peticiones de IA/movimiento/combate, pero **la IA, objetivos, animación y detección de golpes salen del cliente** [V/E; `combate-coop.md` §0–2, `MoveActorReq0x2977E10`, `DoFrameEventReq0x296D130 → 0x2A896E0`]. Distancia/desconexión no habilita por sí sola a otro cliente a enviar esas peticiones. D0 no terminó de clasificar las 114 escrituras `dword+0x90` de todo el rango servidor; por ello la frase antigua «no hay reparto posterior» debe leerse **no se ha localizado una ruta normal**, no prueba de inexistencia en toda `.xtls`.

`ChangeAiControlAck2508` merece especial cautela:

- El único llamador directo documentado del escritor `0x297B430` es `0x2C41553`, en `AiControlChangeCheatReq → 0x2C41360` [E; D0 §7.1 y nuevo volcado]. El barrido de inmediatos no descarta emisores virtualizados/indirectos.
- La función exige permiso y que **el controlador actual ya sea el solicitante**, `0x2C414E4–0x2C41500`; después reinicia secuencias `0x28FA4C0`/`0x28FBAC0` y manda el Ack [E].
- El escritor **serializa sólo u32 clave del actor**: `0x297B511–0x297B524` o `0x297B5F7–0x297B623`; paquete 9 B incluyendo cabecera. **No lleva ID del nuevo dueño ni época** [E; nuevo volcado]. Emitir 2508 no constituye un traspaso completo.
- `OverriedOwnershipStateTypeyAck2168` corresponde al byte de propiedad de un componente (`0x2A9B670`, componente+0x61), no demuestra cambio de actor+0x90 [E; D0 §7.2].

### 5.2 Diseño de traspaso, todavía [S]

El servidor mantiene `{clave completa, generación del mundo, SteamID controlador, época u64, estado}`. Estados: activo, revocando, transfiriendo, suspendido. No usa una dirección de memoria como identidad entre PCs. Elige un único cliente que tenga escena/actor listos, distancia válida y capacidad de simulación; evita alternar continuamente entre dos jugadores cercanos mediante un plazo mínimo y margen de distancia.

Secuencia: revocar fuente anterior en el filtro de entrada, incrementar época, descartar pendientes de la época antigua, esperar preparación del nuevo cliente, aplicar el cambio nativo de controlador y de modo en los clientes, sincronizar transform/acción/HP/aggro/secuencias y habilitar la nueva fuente. **La época es del sobre del mod; Troc no la trae.** Se valida antes de encolar en Q0; cada petición debe corresponder a la clave completa actual, no sólo a su índice reciclado. El orden de las acciones y su compatibilidad con cliente/servidor requieren una prueba específica.

Todavía falta identificar una ruta `.rsrc` completa y segura que actualice dueño en servidor y clientes; no se propone un hook en `0x2792A7B` ni en el cheat ni en `.xtls`. Un simple cambio de 4bytes sólo en servidor podría dejar ambos clientes en modo remoto o dos clientes simulando. El control de daño seguirá validándolo el servidor y el cliente saliente no podrá continuar enviando `DoFrameEventReq` aceptadas.

| Situación | Política prudente [S] | Riesgo y prueba |
|---|---|---|
| Dos clientes juntos | Un dueño por NPC, ambos observan | Mover, golpear y aggro alternado; correlacionar sesión solicitante, actor+0x90 y respuestas |
| Dueño se aleja, otro se queda | Transferir sólo cuando el nuevo ha cargado actor/escena | No es suficiente distancia. Esperar preparación y comprobar que viejo dueño deja de producir efectos |
| Dueño desconecta sin aviso | Revocar época inmediatamente; transferir a observador listo | Cola tardía y hits duplicados. Interrumpir transporte con NPC atacando y comprobar HP/muerte exactamente una vez |
| Sin observadores | Suspender/descargar según semántica nativa y conservar estado persistente; no inventar batallas fuera de cámara | No se demuestra IA de servidor. Reentrada debe recuperar NPC/HP/estado permitido sin duplicar recompensas |
| U=4/8/10 en zonas separadas | Repartir dueños por zonas/carga, no obligar al cliente técnico a simular todas | Medir número de NPC controlados por cliente, tick/latencia, memoria de escena y reserva global |

Mientras P10 no pase con dos clientes, «anfitrión dedicado» significa proceso siempre encendido, **no mundo autónomo que combate sin ningún cliente activo**. Dormancia del mundo sin personas es compatible con persistencia, pero se debe explicar como decisión de juego [S].

## 6. Stages, jefes, cinemáticas y contenido

La investigación previa encontró 125 etapas con barra de jefe en 3.552 `.pastage`; 102 fijan aggro al jugador y 111 tienen condición de fallo al salir del área; 118 usan fases. Son datos extraídos, no observación de combate con varios jugadores [S: recuento documental no revalidado aquí; `pendiente-jefes.md` §1–2, scripts `p_boss/fights.py`, `boss_tables.tsv`]. Los mensajes `StageBranchStateReq2436`/Ack2981 y `UpdateStageChartEventReq2591`/Ack2111 transportan estado ligado al jugador [V/E por informe; handlers `0x2A5C4F0`, `0x2A5CC70`]. Compartir un NPC no comparte automáticamente el secuenciador ni la misión.

E3b salta para el no dueño la sincronización de etapas de `MoveFieldComplete` (`0x2910DE0`, `0x2B00C70`) [E; E3b §3.3]. Hay que enviar un estado coherente de etapa a quienes entren tarde, y decidir qué estados pertenecen al mundo y cuáles son personales. A3/A5 desarrollan la partición; aquí no se replica a todos la partida de historia de quien remata.

| Contenido | Viabilidad para 2/4/8/10 [S salvo evidencia citada] | Riesgo / prueba |
|---|---|---|
| Combate normal de campo | Candidato inicial para 2;4 después de relevos/aggro;8 objetivo;10 condicionado por IA/red | Nativo de daño y control por sesión, pero no observado multijugador. E5: dos jugadores atacan/mueren/reviven |
| Jefe de encuentro sin cinemática ni `noDie` | Primer experimento con 2; luego 4;8 y 10 requieren reglas/medición de dificultad |34 encuentros son candidatos del informe de jefes; no todos son equivalentes. Confirmar inicio/final, aggro al invitado, muerte y botín |
| Jefe de misión con fases | No aprobado automáticamente ni para 2. Elegir un director de etapa y participantes; snapshot al entrar tarde | `Player.` local, QTE, barreras y cambio de actor/fase; salir el director puede abortar. Prueba antes de escalar aforo |
| Cinemática | Director único; transición pactada para participantes o espectadores inmóviles con estado sincronizado | Duplicar `StageBranchStateReq` desde U clientes puede desordenar etapas. `SetAvoidByCutSceneAck2561` es señal parcial, no protocolo de sincronización |
| Misión que cambia el campo | Autoridad del mundo aplica una sola transición; progreso personal sólo para elegibles | No conceder misión personal ni repetir spawn por cada usuario. Prueba: nuevo usuario entra en campamento ya liberado |
| Contenido que carga otro nivel | Una instancia de ese nivel por mundo, o aplazar hasta definir la política | El H1 original sólo cubre el nivel del anfitrión; viaje puede dividir el mundo. Debe verificarse con muerte/recarga/viaje |

No se trasladan aquí las recomendaciones de trucos del antiguo modelo de copias (`KillCheat`, `VaryStatCheat` o «muerte espejo»): opción D dispone de actor real por sesión y debe usar el daño nativo. No se demuestra escalado nativo de HP/IA por 2/4/8/10; cualquier multiplicador sería una regla nueva y requiere estudio propio. Que ocho puedan pegar no implica que el jefe mantenga fases o dificultad [S; `combate-coop.md`, `pendiente-jefes.md`].

## 7. Muerte, experiencia, botín, aggro y grupo

### 7.1 Lo que el servidor acredita realmente

`0x29431A0` emite `PlayerDead0x6B` al muerto jugador, `DeadActor0x62` al dueño de grupo/escenario y **`KillActor0x61` a un acreditado**, después de resolver asesino/dueño de ataque/último atacante [E; nuevo volcado, puntos `0x29432F2`, `0x29436CB`, `0x2943B8B`; `pendiente-recompensas-progreso.md` §2]. No se ha encontrado en este tramo un bucle «dar crédito a todos los jugadores cercanos». El lector de experiencia por evento está en `.xtls 0x107CCA50`; la experiencia observada es de subnivel/ puntos de habilidad, no asumir nivel general de MMO [E-x/V documental; mismo informe §1/ §2.3].

`0x2942350` forma parte del posprocesado de muerte y telemetría; el camino de botín pasa por `0x2942730 → … → 0x2B83960`, condicionado por variante y estado [E; nuevos volcados y `pieza4-botin-reaccion.md` §1.2]. Es incorrecto deducir que el simple Ack de muerte reparte objetos. Las capturas de un usuario no deciden si, en campo compartido, recoge el rematador, el dueño del campo o cualquier jugador autorizado. **Botín personal para U usuarios requeriría diseño adicional**; no se promete nativo.

Aggro: objetivo y cambios de IA los notifica el cliente controlador (`CurrentTargetChangeReq`, `ChangeAIStateReq`) y el servidor valida dueño [V/E; combate §1–2]. El nuevo jugador real evita la inexistencia de la marioneta en servidor, pero no elimina condiciones de scripts que fijan `Player.`. Prueba: alternar golpes entre usuarios y pedir al jefe atacar al que no controla la IA; medir tanto objetivo servidor como ejecución del ataque en cliente.

### 7.2 Hallazgo nuevo: grupo de actores del mismo controlador

La tabla Troc contiene `OpenPartyReq2453`, `InvitePartyMemberReq2865`, `JoinPartyOfReq3084`, `JoinPartyAck2425`. **La presencia de estos nombres no prueba grupo humano.** Contraste nuevo:

- `InvitePartyMemberReq0x2A375B0 → 0x2C3C610` exige que la clave de controlador del solicitante (`vf0x170`) coincida con `actorObjetivo+0x90` en `0x2C3C6DA–0x2C3C6E2`; luego usa componente `+0x108` y `0x2BD5120` [E; `d_2c3c610.asm`].
- `JoinPartyOfReq0x2A379D0 → 0x2C3CA80` obtiene dos actores y exige **la misma coincidencia para ambos**, en `0x2C3CBD4` y `0x2C3CC08`; falla si uno es de otro controlador [E; `d_2c3ca80.asm`].
- En el login 112627 los `JoinPartyAck` preceden cargas de mercenarios `B0100002/3/4` [V documental; `opcion-d-login.md` §3]. Esto encaja con grupos de compañeros del mismo usuario [S], no una invitación entre dos SteamID diferentes.
- `ToNearAndParty` sí añade sesiones del grupo y deduplica ( §2), pero no calcula crédito ni crea un grupo que esos handlers rechazan [E-x/S].

Política inicial [S]: conservar una sola muerte autoritativa, observar crédito nativo y botín sin duplicarlo. Para progreso cooperativo justo hará falta una tabla de participación/eligibilidad por encuentro y una regla explícita de reparto, **después de** cerrar inventarios y persistencia A3/A4. No dar a todos el evento `KillActor` a ciegas: activa misiones, desafíos y habilidades con condiciones distintas.

| U | Prueba de muerte y recompensa |
|---:|---|
|2|A da casi todo el daño, B remata; invertir roles. Registrar `KillActor`, XP, inventario, dueño/recogida de botín y guardado de cada uno|
|4|Dos ayudan, uno llega tarde, uno fuera de rango. Confirmar una muerte y elegibilidad prevista; recoger simultáneamente|
|8|Repetir con ataques/proyectiles/acompañantes, caída del controlador y dos grupos espaciales|
|10|Mismo caso límite más cola de entrada/carga; comparar total de objetos concedidos con reglas, cero duplicación por reenvío/reconexión|

Cada resultado decide política de crédito; ninguno puede deducirse contando `ToNear` o `JoinPartyAck`.

## 8. Spawns, jugadores dispersos y viaje rápido

### 8.1 Rutas de spawn contrastadas de nuevo

| Req | Entrada `.rsrc` y ruta | Hallazgo / incertidumbre |
|---|---|---|
|`ActivateSpawningPoolReq2424`|`0x29C3530 → 0x2C41AF0 → .xtls 0x108797A0 → 0x27EE3E0`|El cuerpo protegido obtiene el campo por `0x2BC4280` y llama al gestor **`field+0x228`**, pasando campo y actor solicitante (`0x10879821–0x1087983C`) [E-x]. `0x27EE3E0` consulta además al dueño del campo vía `0x2AFFF50` [E]|
|`AutoSpawnRoadReq2550`|`0x29C4930 → 0x2C41E50 → .xtls 0x10880460 → 0x27E8C90`|Exige tipo de registro 1 y byte actor+0x96≠0; vuelve a usar `field+0x228`, pero conserva actor solicitante [E-x; `0x10880488–0x10880515`]|
|`MeshGroupAutoSpawnReq2809`|`0x29C5120 → 0x2BC8F40 → .xtls 0x10701110 → 0x27F29D0`|Valida índices de datos estáticos, obtiene el campo y pasa `field+0x228` [E/E-x; `0x10701264–0x107012C6`]|
|`TerrainRegionVoxelSpawnReq2327` / `SceneLevelOnDemandGimmickSpawnReq2928`|`0x29C4300` / `0x29C1C80`|Formato y presencia en captura documentados; deduplicación completa no demostrada [V/E documental; login §4, tabla Troc]|

Esto mejora la hipótesis de trabajo: compartiendo campo hay **gestor de spawns común**, no necesariamente un gestor separado por cliente [E-x/S]. Pero unas rutas miran al dueño del campo y otras al solicitante; no se ha probado idempotencia para dos peticiones del mismo pool desde usuarios distintos. En especial, que haya un hash en el gestor no demuestra clave de deduplicación correcta para cooperativo.

Diseño [S]: primero auditar idempotencia nativa por `{campo/generación, identidad estable de pool/nodo/instancia}`, nunca sólo por coordenadas. Dos animales legítimos pueden compartir posición/variante. Si hace falta una capa del mod, debe agregar observadores y conservar el spawn hasta que ninguno lo necesite; una petición de desactivación de A no debe quitarle a B un actor activo. Separar los spawns de mundo de mercenarios/equipo y contenido personal. Filtrar todas las Req de spawn del invitado impediría explorar zonas que el anfitrión no carga: sólo sirve para el replay sintético inicial [S; login §5].

Prueba de duplicación: dos clientes llegan al mismo pool en orden A→B y B→A, luego juntos; comparar claves, identidad del nodo, `SAM+0xD0`, directorio de spawns y retiradas. Repetir salida A mientras B permanece. **Duplicado de contenido** significa dos claves distintas para el mismo nodo lógico, no dos Ack iguales a destinatarios diferentes.

### 8.2 Zonas separadas y viaje

En zonas separadas cada cliente debe poder cargar escena y generar peticiones que el servidor atienda aunque el cliente del anfitrión permanezca lejos; navegación/física dependientes de éste son B5 [S pendiente; `OPCION-D-EMULADOR.md` §3, A2]. Se necesita un dueño de IA listo por zona, un gestor de spawn compartido por nivel y contabilidad global. Mantener todos cerca reduce presión de claves, pero aumenta reenvío/red; dispersarlos invierte ese compromiso.

`MoveFieldAck2174` no es un simple teletransporte gráfico. La secuencia incluye salida nativa, adquisición de campo, entrada, carga de escena y `MoveFieldCompleteReq`; `0x2BBFA80`, `0x2BBEAB0`, `0x2B07790`, `0x2BC04F0` [E/E-x; E3b §2]. Con U usuarios, un viaje no debe limpiar observadores ni stages de los restantes ni crear una segunda instancia del mismo nivel. `LoadingCompleteReq00` evita una escritura de hora documentada, pero no resuelve por sí solo la historia del mundo [E/S; E3 riesgos R1].

Política prudente [S]: antes de ocho, probar **viaje individual del no dueño**, viaje del dueño, muerte con recarga y cambio de nivel de misión. Para el primer hito compartir sólo un nivel y aplazar viaje simultáneo; para persistencia completa debe existir la política de registro de campos de §2.2, sin trasladar a todos contra su voluntad como solución permanente.

## 9. Puntos de enganche y verificación

Sólo se consideran entradas `.rsrc` ya descritas: `0x2B07790`, `0x2BC04F0`, `0x2A759B0`, `0x2A75880`, y candidato de inicialización `0x2A7DEA0`. Sus primeros 16 B, firmas exactas únicas, longitudes y SHA del exe están en [vision-a1-firmas.json](data/vision-a1-firmas.json), reproducibles con `vision/a1/firmas.py`; los cuatro primeros también figuran en `data/vision-firmas-2976.json`. Una firma exacta es válida para esta build, **no una firma resistente a parches**. Los dos ExceptOne sólo se distinguen mediante bytes de desplazamiento; una máscara ingenua vuelve a producir dos coincidencias [E; D0 §9]. No se habilita ningún hook aquí.

Pruebas recomendadas en orden [S; prerrequisitos y criterios históricos en `opcion-d-pruebas.md`]:

1. E0/E0b/E1: mapa vivo, seguridad de sesión y protección del guardado.
2. E2: −101 claves por alta, dos cuentas y sesiones distintas, respuestas al destinatario correcto.
3. E3/E3b: mismo campo, altas recíprocas, usuario no dueño, contadores exactos después de salir; luego E4.
4. G2/G3: dos clientes remotos reales; anfitrión quieto, invitado carga y combate lejos (B5).
5. P10: controlador se aleja y desconecta durante combate; exactamente un dueño/época y ningún daño tardío.
6. Spawn conjunto/separado: dos solicitantes, retiro de uno, zonas no superpuestas; comparar SAM contra medición de cable.
7. Crédito/botín: reparto observado con rematadores alternos, recogida simultánea, muerte/revivir y nueva entrada.
8. Viajes individuales y salida del dueño/último usuario; confirmar que campo, hora y stages no se duplican ni se cierran antes.
9. Escala 4→8→10: tanto agrupados como dispersos, control de IA y claves/M por cada etapa; ocho personas más anfitrión técnico exige U=9.
10. Jefe de encuentro y después jefe de misión/fases; la aprobación del mundo libre no aprueba todo el contenido.

Aborto: campo de otro usuario cerrado, clave compartida entre actores vivos, dos dueños que aplican daño, objetos duplicados, pérdida de progreso o reserva bajo margen sostenidamente. Los abortos son criterios de futuras pruebas, no acciones ejecutadas aquí.

## 10. Diez afirmaciones prioritarias para revisión independiente

| # | Afirmación que intentar refutar | RVA y evidencia |
|---:|---|---|
|1|Inicialización suma 100N y N más cuotas de mundo; no hay cuota efectiva de un usuario en reserva conocida|`0x2A7DEA0`, `0x2A83310`, D0 §1; `estatico.py`|
|2|Cola común vacía provoca `eErrNoInvalidActorKey`; baja devuelve generación/índice|`0x2A82D57`, `0x2A846B0`|
|3|ToNear recorre sesiones de observadores y amplía salida sin tope 10|`0x2BC5071–0x2BC50FD`|
|4|ToField lee lista de usuarios; ToWorld recorre SAM y tiene buffer inicial 1000, no aforo 10|`0x2AFFBF0`, `.xtls 0x104EB5B0`, `0x2A83A40`|
|5|Tope 1 se aplica a usuario nuevo en AddActor; H2 usa rama de usuario existente|`.xtls 0x1064C806`/`0x1064C813`, E3b §1.3/ §3.2, contraste A0|
|6|Controlador inicial: lectura 0x2792A11; escritura 0x2792A7B; selecciona dueño nuevo sólo si estaba 0 y, si no, reescribe el mismo valor|`0x2791CD0`, `d_2792a11.asm`; corrección independiente §12|
|7|Cheat 2508 exige controlador actual; Ack sólo lleva clave actor, no nuevo dueño|`0x2C414E4–0x2C41553`, `0x297B511–0x297B623`|
|8|JoinPartyOf exige ambos actores del mismo controlador; no basta para grupo humano|`0x2C3CBD4`, `0x2C3CC08`, `d_2c3ca80.asm`|
|9|KillActor se emite a acreditado y no se demuestra reparto a todos; distinguir botín de muerte|`0x29431A0`, `0x2943B8B`, `0x2942350`, `0x2942730`|
|10|Tres rutas de spawn obtienen gestor field+0x228; eso no prueba idempotencia multicliente|`.xtls 0x10879835`, `0x1088050E`, `0x107012BF`; núcleos `.rsrc 0x27EE3E0`/`0x27E8C90`/`0x27F29D0`|

Además, conviene repetir la mediciónV: la separación entre pico 1.006 y acumulado 7.655 es central para cualquier conclusión de aforo. El script no emplea `pk.py`; los hashes de entrada permiten reproducirla sin juego.

## 11. Archivos de evidencia

- `C:\Emulador CD\build\research\vision\a1\capturas.py`: lector y mediciones; `estatico.py`: volcados de funciones desde directorio PE de excepciones; `firmas.py`: búsqueda de firma exacta en fichero completo. Ninguno ejecuta el juego.
- En el mismo directorio: `d_<rva>.asm`, `contraste*.json`. Los thunks sólo sirven para hallar destino; bytes tras un salto terminal o tablas de salto no se interpretan como flujo ejecutado. `.xtls` siempre [E-x].
- En el repositorio: `data/vision-a1-capturas.json`, `vision-a1-regiones.csv`, `vision-a1-radio-observado.csv`, `vision-a1-visibilidad-segundos.csv`, `vision-a1-firmas.json`.
- Informes leídos: `CLAUDE.md`, `OPCION-D-EMULADOR.md`, diario, D0/login/E3-riesgos/campo-compartido/P8/P9, `segunda-sesion.md`, `apariencia-estado-coop.md`, `pendiente-rango-claves.md`, `enemigos-del-anfitrion.md`, `pendiente-3g-suprimir-enemigos-invitado.md`, `combate-coop.md`, `pendiente-recompensas-progreso.md`, `pendiente-jefes.md`, `bdo-emuladores-monstruos.md`, `genshin-coop-referencia.md`, `hora-del-dia.md`, `pausa-copia-congelada.md`. Las referencias externas históricas no se usan como prueba de comportamiento del exe y no se consultó material filtrado ni se copió código de `reference/CrimsonDesertCoop`.

## 12. Verificación independiente contra el ejecutable y una captura

Un agente distinto del autor ha leído directamente el PE del juego y la captura 200328 con un lector propio: `C:\Emulador CD\build\research\vision\verificacion\a1\verifica.py`. No importa los scripts de A1 ni sus volcados. Los límites de funciones `.rsrc` proceden del directorio PE de excepciones, siguiendo los registros unwind encadenados; los tramos protegidos se mantienen [E-x]. El SHA-256 del exe coincide con §9: `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`; timestamp `0x6AB28F00`. Script y volcados sólo en esa carpeta; resumen versionable: [vision-verifica-a1.json](data/vision-verifica-a1.json).

«Confirmada» significa que los bytes sostienen la afirmación **dentro del tramo inspeccionado**. No convierte una ausencia local de comprobación en demostración global frente a todas las llamadas virtuales o protegidas, ni sustituye E2/E3/P10. Resultado: **nueve confirmadas en ese alcance y una formulación literal falsa, corregida (#6)**; ninguna refutación cambia el objetivo condicionado de ocho o el techo de diseño diez.

| # | Estado | Evidencia independiente y límite de la conclusión |
|---:|---|---|
|1|**Confirmada, alcance local**|[E] `0x2A7DEDB` multiplica N por 100; suma en `param+0x44` en `0x2A7DEF6`; `0x2A7E044` suma N y `0x2A7E04C–0x2A7E066` registra la entrada tipo 2 sin volver a sumarlo. `0x2A83439` reserva usuario y `0x2A83474–0x2A83603` recorre los huecos hijos. No hay comparación «usuarios ≥ N» en la reserva inspeccionada; no se acredita su ausencia global en todo login/VM. Datos 15.501 y cien hijos conservan su procedencia histórica D0, no son una lectura viva nueva. `f_2a7dea0.asm`, `f_2a83310.asm`.|
|2|**Confirmada**|[E] `0x2A82D57–0x2A82D63` comprueba `SAM+0xD0` y toma el error global `0x6CDB9E4` si está vacío; [E-x] `0x105389AE–0x105389C2` hace lo mismo para el pop del usuario. [E] `0x2A846DD–0x2A84731` devuelve índice de veinte bits y generación de ocho bits a la FIFO, sin demostrar atomicidad de todo spawn. El nombre `eErrNoInvalidActorKey` conserva la identificación documental D0. `f_2a82bf0.asm`, `x_10538980.asm`, `f_2a846b0.asm`.|
|3|**Confirmada, alcance local**|[E] `0x2BC5071` obtiene la lista; `0x2BC50A9` lee sesión `+0x1F8`; `0x2BC5085/0x2BC50E4` amplían salida y `0x2BC50FD` itera por la cantidad. No aparece un tope diez en ese tramo; tampoco prueba que toda la cadena de interés soporte diez sesiones. `f_2bc4f70.asm`.|
|4|**Confirmada, alcance local**|[E] ToField lee `+0x2A0/+0x2A8` y recorre entradas de 16 B (`0x2AFFD01–0x2AFFD35`). [E-x] ToWorld escribe capacidad inicial `0x3E8` en `0x104EB5E6`, recorre attachers de 32 B y filtra sesión nula en `0x104EB644–0x104EB64E`; [E] `0x2A83AD4–0x2A83B1E` recorre SAM y selecciona tipo usuario. No se deduce un aforo nativo de diez ni mil. `f_2affbf0.asm`, `x_104eb5b0.asm`, `f_2a83a40.asm`.|
|5|**Confirmada estáticamente**|[E-x] `0x1064C7E8–0x1064C7EB` salta a `0x1064C813` cuando encuentra el usuario; sólo el ausente pasa por `cmp [field+0x2A8],1` en `0x1064C806`. `0x1064C821` incrementa `+0x2B0`. Se confirma la rama que pretende aprovechar H2; la preinserción y su concurrencia siguen [S] sin prueba viva. `x_1064c700.asm`, tramo válido a partir de `0x1064C770`; no se interpreta el código ajeno anterior como parte de AddActor.|
|6|**Falsa en su formulación literal; corregida**|[E] `0x2792A11` lee, pero `jne 0x2792A7B` en `0x2792A1A` prueba que la escritura también se ejecuta con controlador previo no nulo. En ese caso conserva exactamente su valor. Se corrigen §5.1 y §10: elegir un dueño nuevo requiere anterior cero; escribir no. No se ha encontrado aquí una transferencia posterior. `f_2792a11.asm`.|
|7|**Confirmada**|[E] `0x2C414E4–0x2C41500` exige controlador existente igual al obtenido del solicitante por `vf(0x170)`. `0x297B511–0x297B524` serializa la clave `actor+0x60` con longitud cuatro; la otra rama reserva nueve bytes (`0x297B553`) y vuelve a escribir cuatro (`0x297B5F7–0x297B623`). No hay nuevo dueño ni época en 2508. Esto no demuestra que el cheat sea el único emisor posible entre llamadas indirectas/VM. `f_2c41360.asm`, `f_297b430.asm`.|
|8|**Confirmada**|[E] `0x2C3CBD4` y `0x2C3CC08` comparan contra `+0x90` de los dos actores el controlador del mismo solicitante; ambos desacuerdos llevan a `0x2C3CD42`. El flujo no admite actores con controladores distintos. No prueba que no exista otro mecanismo de grupo, y no demuestra reparto de XP/botín. `f_2c3ca80.asm`.|
|9|**Confirmada, alcance local**|[E] `0x2943B8B` construye el evento `0x61`; `0x2943C9C–0x2943CA1` lo entrega al único actor seleccionado en `[rsp+0x38]`, sin iteración de usuarios cercanos en ese tramo. `0x294253E` llama a `0x2942730`, cuya lógica distingue estado/tipo. Confirma separar evento de muerte y posprocesado; no demuestra que receptores posteriores no propaguen efectos, ni determina reparto real a humanos. Los nombres de eventos conservan la identificación de `pendiente-recompensas-progreso.md`. `f_29431a0.asm`, `f_2942350.asm`, `f_2942730.asm`.|
|10|**Confirmada, sin idempotencia probada**|[E-x] `0x10879835`, `0x1088050E` y `0x107012BF` cargan `field+0x228` y llaman respectivamente a `.rsrc 0x27EE3E0`, `0x27E8C90` y `0x27F29D0`; los llamadores conservan campo/solicitante. Compartir gestor no prueba deduplicación, retención por observadores ni seguridad de desactivar desde otro cliente. `x_108797a0.asm`, `x_10880460.asm`, `x_10701110.asm`.|

Las cinco firmas de §9 se han vuelto a buscar en **todo el fichero**: una coincidencia exacta cada una, bytes coincidentes y entrada `.rsrc` corroborada por unwind. No se habilita ningún hook y no se acredita resistencia de esas firmas a otra build [E; `pe_verificado.json`].

La medición independiente [V] de `cdmp_packets_20261003-200328.bin` —SHA-256 `1b8a388d7418d206b5324359bee73c298870a117c303cb3eb85cf7c5fcc3c5d5`— reproduce **7.655** claves distintas y **1.006** en el pico a **1.802,573141 s**, con tipos 3=356, 4=12, 5=4, 6=105, 7=529. Recorre 684.396 registros, cero truncamientos y cero `ScopeSenderAck2042`; reinicia por `MoveFieldAck2174` y retira claves por `RemoveActorAck2516`, payload+6. Los dos primeros mensajes, 1001/1002, no encajan en la cabecera Troc genérica de cinco bytes; no participan en estas altas/bajas ni afectan al resultado. No hay registros CDPK rotos [V; `captura_verificada.json`].

Este pico sigue siendo **conjunto cargado reconstruido para el cliente de la captura, no actores simultáneos del SAM**. El radio no se ha vuelto a medir en esta revisión y conserva la extracción heurística [S] de §3.4; tampoco se ratifica un radio nativo de 80 m. Para A2 y la síntesis: no transformar estas cifras en capacidad garantizada para 8/10, no presentar 2508 como traspaso completo ni `JoinPartyAck` como reparto entre humanos, y mantener pendientes las pruebas de reserva real, salida del controlador e idempotencia de spawns.
