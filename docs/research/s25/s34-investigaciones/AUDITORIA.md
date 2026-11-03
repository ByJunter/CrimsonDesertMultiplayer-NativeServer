# Auditoría independiente de las cinco investigaciones S34

**14 de octubre de 2026. Código auditado: `d835806c46da1cc3ec9ad3e5367dc0d1feb95db9`. Build del juego: 1.0.0.2976, PE timestamp `0x6AB28F00`, base `0x140000000`.**

## Resumen ejecutivo

Las investigaciones identifican mecanismos reales, pero sus diseños no están listos para implementarse literalmente. Se confirman los cierres por estado incompleto, la contención que ignora acciones aplicadas y el contraste de viajeros lejos/cerca. La auditoría encuentra condiciones omitidas que pueden repetir cierres, partir familias o dejar actores sin retirar.

| Investigación | Dictamen | Condición principal |
|---|---|---|
|1. Cierres | **Aprobada con cambios** | El actor del segundo cierre es `B01004BF`; `B0100578` lo consulta. R2 debe cubrir todas las vías de publicación, no sólo Timeout. |
|2. Familias | **Aprobada con cambios importantes** | Reservar la unidad completa, conservar marcos anidados, vincular nacimiento del padre y separar NoCc de lectura fallida. Tipo6 con Shadow sigue pendiente. |
|3. Viajeros | **Aprobada con cambios importantes** | La marca+3CD no es exclusiva de2550 actual. Falta procedencia completa y limpieza estricta que sobreviva a la desconexión. |
|4. Contención/relevo | **Aprobada con cambios** | Emisor0 confirmado; hay retiradas con aplicación previa.8s no es garantía de salud y el rearmado necesita generación/cadena. |
|5. Guardado | **Aprobada con cambios; real bloqueado** | Redirect explicado; faltan identidad positiva, procedencia de Flush, pareja coherente y M1/M2 ampliadas. |

**Prioridad:** corregir la evidencia de progreso y cerrar la publicación insegura antes de ampliar autoridad de NPC. Primero familias medidas, después viajeros con procedencia inequívoca; la optimización de rearmado y la publicación a partidas reales quedan detrás de sus pruebas de seguridad. No se repitió ninguna prueba en vivo durante esta auditoría.

Los riesgos principales son: publicación remota con estado incompleto por rutas alternativas; familia partida por capacidad/reentrada/generación; clasificación de viajeros restaurados como spawn nuevo; limpieza que miente sobre una baja o no continúa tras desconectar; persistencia de un mundo incoherente pese a que sus archivos tengan cabecera y hash correctos. Cada riesgo tiene una prueba concreta en §8.

## Método, alcance y fuentes

Se leyeron `CLAUDE.md`, el índice, el encargo y las fuentes relevantes. La documentación observada corresponde al HEAD `f79545c0f18f933ca0f9e185f60455b1bcc38501`; el código se fijó por `git archive d835806` para evitar mezclarlo con cambios de otra sesión. Cinco auditores separados trabajaron en dos tandas por disponibilidad de plazas. No recibieron dictámenes de otros antes de terminar. Un revisor adicional contrastó la consolidación; el coordinador reprodujo sondas, integridad de capturas y puntos del PE con lectores propios.

No se modificó código, compiló, desplegó, ejecutó el juego/host/peer, tocó partidas ni hizo commits. El ejecutable se leyó en disco. Las consultas nuevas de Ghidra usaron el wrapper de lectura; sus exports se conservaron como evidencia. Sólo se escribieron scripts/resultados propios, dictámenes y este informe.

- **[V]** verificado con las fuentes disponibles y dentro del alcance indicado.
- **[P]** parcialmente verificado; se identifica la condición que falta.
- **[F]** falso o formulado con un alcance refutado por la evidencia.
- **[NV]** no verificable con estos datos; requiere la medida indicada.

Un contrato estático no acredita que una nueva cohorte funcione con Shadow. Ausencia en una captura sólo se aplica a sus ventanas y cobertura. Los cambios obligatorios y pruebas futuras son recomendaciones de diseño, no acciones ejecutadas.

### Alias de fuentes primarias

Todas las rutas siguientes parten de `C:\Emulador CD\backups\p8-sesion34\`:

| Alias | Log | Captura BIN | Volcado, si existe |
|---|---|---|---|
| AH/AG | `cdmp_20261014-180308.log` / `shadow\cdmp_20261014-170438.log` | `cdmp_packets_20261014-180319.bin` / `shadow\cdmp_packets_20261014-170455.bin` | `shadow\cdmp_crash_20261014-170638_1704_1.dmp` |
| BH/BG | `paso1b\cdmp_20261014-184959.log` / `shadow-p1b-p2b\cdmp_20261014-175039.log` | `paso1b\cdmp_packets_20261014-185010.bin` / `shadow-p1b-p2b\cdmp_packets_20261014-175057.bin` | — |
| CH/CG | `paso2b\cdmp_20261014-190232.log` / `shadow-p1b-p2b\cdmp_20261014-180502.log` | `paso2b\cdmp_packets_20261014-190242.bin` / `shadow-p1b-p2b\cdmp_packets_20261014-180517.bin` | `shadow-p1b-p2b\cdmp_crash_20261014-180710_6364_1.dmp` |

AH:123 significa línea123 del log AH. I1–I5 son los cinco informes originales de esta carpeta. Las referencias de código sin prefijo corresponden a `src/mod/session2/` en d835806, no a HEAD. Los offsets BIN/DMP son absolutos dentro del archivo; las direcciones del juego son RVA. Exports: `D:\RE\CrimsonDesert_2976\exports\decomp\0x<rva>.c`.

### Integridad y relojes

| Captura | Registros | Payloads truncados | Registros perdidos declarados |
|---|---:|---:|---:|
| AH |237849|0|18|
| AG |42607|0|0|
| BH |541764|0|0|
| BG |357882|0|0|
| CH |190962|0|30|
| CG |59774|0|0|

[V] Lector propio `consolidacion/inventario_fuentes.py`, resultados `inventario-fuentes.json`: hashes, offsets de huecos y validación de las seis cabeceras. Sin cabeceras/payloads cortados al final. Algunos paquetes iniciales tienen longitud Troc no convencional; los parsers explicitan su descarte o clasificación. Ausencia de gaps no prueba que todo evento anterior al cierre llegara a disco.

Restar una hora a Shadow no basta para ordenar sucesos subsegundo. Se usan QPC, pares por clave/nacimiento y relojes del mismo PC cuando es posible. El mínimo de tránsito mezcla offset con latencia mínima; sus residuos no son latencia absoluta ni prueban simetría. El prefijo horario de una línea diferida no siempre es la hora del evento.

## 1. Cierres del invitado: aprobada con cambios

Las dos pilas y la exposición a estado incompleto son reproducibles. La herencia familiar y una barrera de publicación segura son líneas justificadas, pero el diseño literal de R2 no cubre todas las rutas. Véase el [dictamen independiente completo](datos/auditoria-codex-20261014/a1-cierres/INFORME-A1.md).

| Afirmación | Dictamen | Evidencia primaria y corrección |
|---|---|---|
| Pila del cierre A y actor afectado | [V] | DMP `170638_1704_1`: lectura de `0x38` en RVA `0x88F832`; unwind por pdata `88F832→88EFA7→860EBC→8B5773→13FB680→1414EBF`. Actor `B01004A5`, controlador anfitrión, padre `B01004A4`; offsets DMP clave `0x7BFA0`, controlador `0x7BFD0`, padre `0x7BFEC`. |
| El puntero nulo de A es la acción actual | [F] | `0x88F82B/82F/832` lee el vector de reproducción/playback; el export `0x88f4e0.c` copia `layer+0x18` a ese vector. La causa inmediata es reproducción nula. La capa original no está incluida en el minidump; no atribuirle lecturas que no contiene. |
| En C falla `B01004BF`, no `B0100578` | [V] | DMP `180710_6364_1`: R14 actor y RDX CC con CC+8 de vuelta al actor; clave en offset `0x73652`. Capa: acción y reproducción cero en `0x1668B7/0x1668BF`, chart no nulo en `0x1669B7`. `B0100578` es dueño del componente jerárquico que consulta a `B01004BF`; enlace en `0x112083`. Pila comienza `1831598→439B85→15ECF73`. |
| Guarda de Update/PostUpdate suficiente | [F] | CG:6959 ya retiene `B01004BF`; otro actor entra por jerarquía a `0x1831598`. La guarda cubre un recorrido, no todos los lectores. |
| Familias partidas y entrega tardía | [V] | BIN del invitado: A dos hijos tipo6 y uno tipo7; B uno y uno; C cuatro y seis. Tipo6 llegan ~10,05–10,09 s después del padre en A/C y 20,0683 s en B. Censo por vínculo serializado, no por toda relación jerárquica. |
| El anfitrión destruyó exactamente cada hijo y nunca actuará | [P] | Llegada antes del padre confirmada; ruta nativa de huérfano y destrucción en `0x8B7B50`, pero `0x8B7D78` sólo lee +AC. CH:13898–13899 no distingue ausencia de lectura fallida. Falta registrar la rama de destrucción por clave; «nunca» excede la ventana. |
| Ocho Timeout son ocho entregas a Shadow | [F] | Contador A2+B0+C6=8; el sexto C es posterior al cierre. Entregas restore0 remotas comprobadas: A2+B1+C5=8, conjunto distinto. B `B01003AE` llega por liberación familiar con TimeoutNPC=0. |
| No hay ningún punto común razonable que proteja lectores | [NV] | La búsqueda reproduce 395 coincidencias de bytes en `.rsrc` y22 en `.xtls`, y diez CALL directos a `0x1831560`; no prueba tipos de todas las coincidencias ni dominancia del grafo. Sí se demuestra que proteger sólo Update/PostUpdate o sólo ese lector resulta insuficiente. |
| R1+R2 eliminan todos los cierres de esta clase | [P] | Atacan el mecanismo observado, pero R2 literal no cubre `Native`, huérfanos, saturación ni tipos sin CC. No hay aún prueba de tipo6 bajo control del invitado ni de todas las dependencias tardías. |

**Ocho entregas restore0 observadas**, con offsets absolutos del registro en BIN Shadow: A `B01004A5@0x1A22A00`, `B010049E@0x1A24405`; B `B01003AE@0x206DD17`; C `B01001F6@0x1779E1C`, `B0100490@0x1EF0E1B`, `B010048F@0x1EF2A45`, `B01004BF@0x20480CD`, `B01004C0@0x2049ABE`. Fuente reproducible: `a1-cierres/capturas-independientes.json`.

**Cambios obligatorios.** R2 debe ser una invariante de admisión, no un cambio aislado `Timeout→Keep`. Cubrir `child_hold.cpp:590,595,606` (`Native`), `family_release_step:485–514`, tablas llenas, lecturas indeterminadas y el tratamiento distinto de actores sin CC. Presencia del padre no demuestra que su capa sea utilizable. Un Ack en cola tampoco demuestra que se aplicará antes de que otro actor lea esa capa.

La retención puede dejar invisibles hijos cuyo controlador los perdió, animales sin acción restaurable y familias no resolubles. Es una degradación temporal aceptable para impedir la exposición conocida, pero exige contabilidad y recuperación acotada por familia; no es una solución funcional indefinida. Conservar las guardas existentes como defensa adicional. No retirar indiscriminadamente NPC de misión.

**Pruebas mínimas.** Puras: todas las salidas de publicación, generación, lectura fallida, familia parcial/profunda, +AC0 con vínculo jerárquico, paquetes mezclados, saturación y Ack antiguo. Peer: orden y limpieza; no acredita IA. Shadow: padre/hijos presentes y aptos en ambos clientes, cero publicaciones inseguras, primera acción y movimiento por controlador, viajes, salida y retorno. La eliminación universal de cierres queda abierta hasta esos experimentos.

## 2. Familias NPC: aprobada con cambios importantes

El parentesco de descriptores tiene fundamento estático, pero el diseño literal puede volver a partir familias. La activación de animales con Shadow sigue sin demostrarse. [Dictamen completo](datos/auditoria-codex-20261014/a2-familias/AUDITORIA-2-FAMILIAS.md).

| Afirmación | Dictamen | Evidencia primaria y corrección |
|---|---|---|
| Censo de familias enteras | [F] con la definición «raíz y todos sus descendientes recibidos bajo user2» | Lectores propios de BIN y log: A 36 raíces con hijos =32 completas+3 partidas+1 pendiente; B24=22+2+0; C48=39+8+1. Los33/41 originales incluyen incompletas o mezclan padres con raíces; en C `B0100474` es padre e hijo. Véanse `*-censo.json` y `censo-log-vs-bin.json`. |
| Hijos tipo6 con autoridad anfitrión | [V] | A2, B1, C4 recibidos bajo raíces user2; los vínculos y claves coinciden con §1. Hay además A1/B1/C6 hijos tipo7 bajo autoridad anfitrión. |
| `desc+0x2F0` y vector `+0x2F8` | [V] en rutas concretas | Escrituras .rsrc: Simple horario `0x281DE14/281DE73`, grupo `0x282120D/282126E`; también CatchTarget/Simple, AutoSpawnGimmick, CombinationGimmick, SavedAttached, LinkedVehicle y Docking, con sus sitios enumerados en el dictamen. Las inserciones llaman `0x43DB70`. `_Rider` y `_NPCScheduleGroupActionMember` no quedan demostrados por esa enumeración. |
| 878 nacimientos prueban que el padre siempre falta en el mid | [P] | 264+228+386 líneas, todas tipo3 seleccionadas por U-e. El filtro `user2_stages.cpp:1195–1212` limita la muestra. No es evidencia universal de todos los tipos ni constructores. |
| Padre siempre primero, mismo hilo y Troc | [P] | 44/26/60 parejas registradas tienen padre primero y mismo hilo, con separación de34–917 µs. Falta token de Troc por nacimiento y cobertura equivalente de6/7. La creación de actores pasa por `.xtls 0x10649310`; no elevarla a contrato legible universal. |
| Los animales empiezan después de entregarse los hijos | [V] cronología / [P] causalidad exclusiva | Primera2223 tras último hijo: A145,987/168,873ms y C190,414/215,294ms, desde entrega BIN. Hay transición de máscara de hijos en A/G:2818,2824,5686,5687 y C/G:5171,5786,6603,6940; falta el predicado exacto de espera. |
| Es seguro asignar tipo6 al invitado | [NV] | En estas cohortes esos animales no estuvieron bajo autoridad user2. Compartir infraestructura NPCSchedule no prueba IA, combate ni baja correctos en Shadow. |
| `not type3` identifica hijos de stage | [F] | `ctl_verdict`, `user2_stages_logic.h:710–724`, no comprueba parentesco; puede clasificar una raíz tipo6. |
| `NoCc` demuestra ausencia de CC y publicación segura | [F] | `child_hold.cpp:62–69` devuelve nullopt tanto por ausencia como por fallo de lectura/backlink/estado; `:146–149` llama NoCc a todos esos casos. Además, el cierre C demuestra una consulta jerárquica desde otro actor (§1); no demuestra que el consultante carezca de CC propio. |
| Excluir tipo4 garantiza no alterar misiones | [NV] | El tipo no prueba el papel de un NPC en el progreso. Deben medirse el destino y crédito de eventos; el contrato del usuario exige progreso del anfitrión. |

**Cambios obligatorios al diseño.** Reservar todo el subárbol conocido antes de cambiar EAX: el contraejemplo «63 entradas ocupadas + raíz con hijo» desborda el marco después de asignar la raíz. Los hijos tardíos o dependencias fuera de ese preexamen requieren una política posterior de admisión/retirada; no se ha demostrado que puedan enumerarse todos antes del mid. Mantener una pila de marcos TLS anidados; el único `t_fam` de I2 §3.3 se pierde al entrar/salir de otro Troc. Añadir generación del padre y token de unidad: `npc_activation_logic.h:393–414` une por clave de padre y época, por lo que puede incorporar un hijo superviviente a otra encarnación de esa clave. Los contraejemplos propios se conservan en `contraejemplos_diseno.py/.json`.

Distinguir «CC ausente demostrado» de «no puedo leerlo». Registrar también raíces no asignadas que necesiten protección; el diseño sólo llega a HostKept después de encontrar un padre. Toda salida por capacidad, tipo imprevisto o lectura fallida debe resolver la unidad completa. Conservar el orden de hojas a raíz en todas las salidas: la contención actual ya recorre la familia en reversa (`user2_stages.cpp:1937–1945`); las dos pasadas miembros/raíces propuestas por I2 §3.8 para `ctl_cleanup` no bastan para `B0100473→474→475`. Medir coste y cerrojos: `npc_observe_birth` ya usa `ActivationLock` (`user2_stages.cpp:1223,338–347`); no describir toda la ruta como libre de locks.

**Tipo7.** Conservar autoridad del anfitrión es una opción prudente para una primera cohorte, condicionada a publicación segura y dependencias demostradas. No basta NoCc; tampoco asignarlo al invitado sólo por ser hijo. Los accesorios, monturas, compañeros, mascotas, estado guardado y NPC de misión necesitan políticas por capacidad y por unidad, no una regla general de tipo.

**Pruebas mínimas.** Puras: reserva63+2, tabla llena, marcos anidados, generación del padre, árbol de profundidad2/3, lectura fallida vs ausencia, hijo tardío y retiro parcial. Peer: vínculo real de descriptor, envoltorio y token de Troc, capacidad y limpieza. Shadow: guardia/perro y pastor/animales, primera acción, seguimiento/combate, visibilidad de la familia en ambos clientes y transiciones; ninguna prueba con peer sustituye esto.

## 3. Viajeros: aprobada con cambios; selector y limpieza no aprobados tal cual

Se reproduce el contraste lejos/cerca, pero no el predicado nativo exacto que inhibe locomoción. La marca propuesta para atribuir procedencia tiene un productor adicional. [Dictamen completo](datos/auditoria-codex-20261014/a3-viajeros/informe-a3.md).

| Afirmación | Dictamen | Evidencia primaria y corrección |
|---|---|---|
| Cohorte B245/C82 con metadatos del invitado y autoridad host | [V] | B47 tipo3+10 tipo5+188 tipo6; C23+4+55. Los327 aparecen en ambos clientes con controlador `90100000`. `recuentos.json` y offsets en `cohortes-evidencias.json`. |
| Todos son inequívocamente nacimientos de2550 actual | [P] | La carretera se encuentra en sus2550 para242/245 y81/82. Cuatro insectos carretera8 quedan sin esa correspondencia. Además existe reconstrucción de AutoSpawnOwnerData por otra función. |
| 3550/1110 elementos de2550 | [V] | Lector independiente:708/90 paquetes; cabecera payload5B, elementos24B, handler `0x29C4930`. El log B sólo recoge3525 elementos: no mezclar paquetes, elementos y líneas de sonda. |
| Emisión de Req rápida, sin locomoción observada lejos | [V] para la muestra | Primera Req examinada del host: medianas145,860/139,019ms; en B139/140 y C81/82 no superan1m X/Z en origen. Las excepciones Wagon Driver alcanzan2,897m. No demuestra acción aplicada ni mecanismo de navegación. |
| 0/82 frente a73/76 | [V] como recuento con límites | Shadow no registra desplazamiento>1m de las82 claves, pero sólo78 tienen posiciones recibidas;4 sin muestras quedan indeterminadas. El control73/76 incluye sólo3/6; añadiendo los tres tipo5, todos móviles, es76/79. Recalcular desde el primer2005 conserva el contraste. |
| Los46 sobrevivientes empiezan al acercarse el host | [V] | Ventana B H271,126643..325,397111, mismas claves/controladores. BH:35716,36424,43697; `survivors1b` del JSON. No requiere reasignación ni recreación de esos actores. |
| Causa exacta: navegación/streaming | [NV] | El contexto de simulación del controlador explica fuertemente el contraste; estas fuentes no distinguen navmesh, streaming, caché ni todas las condiciones de IA. |
| `0x27EA160` escribe campos de procedencia | [V] | Exe: `27EAD9B/27EADAC/27EAE24` y `27EB40A/27EB411/27EB483`; constructor `278EA70` inicializa los campos. Raíz y acompañantes llevan los datos. |
| `+0x3CD=1` es exclusiva de2550 actual | [F] para esa hipótesis | Contraejemplo nuevo: función `0x28981B0` construye el mismo descriptor con `278EA70`, recupera `AutoSpawnOwnerData` y escribe WORD+3CC=`0x101` en `0x28995E9` (`66 C7 80 CC 03 00 00 01 01`). El informe original lo dejaba como hipótesis por búsqueda en827 exports; esa cobertura resulta insuficiente. |
| Predicado completo descriptor→2005 coincide1:1 | [NV] | Falta sonda del descriptor al nacer. No se demuestra que el productor alternativo pase todas las guardas req_key/req_ptr ni que ya haya habido asignación errónea. La marca sola, o vtable+marca, sí quedan refutadas como prueba de spawn nuevo. |
| Reusar `cleanup_one` mantiene errores locales | [F] | `user2_stages.cpp:1627–1637,1670`: el callback SEH afecta `g_activation_fault` o `g_ctl_aborted`. Contradice el aislamiento prometido para viajeros. |
| Su resultado predeterminado acredita baja | [F] | `cleanup_one:1643,1652,1661` usa strict=false; `npc_activation_logic.h:121–124` convierte errores desconocidos de Logout en Done. Exigir resultado estricto y desaparición confirmada. |
|64 por llamada y siguiente Troc terminan al salir | [F] como garantía | Puede no haber otro Troc; SAM/ModLock pueden impedir incluso la primera tanda. Censo de altas sin baja alcanza91/81 en B/C: no es SAM exacto, pero tampoco permite asumir que64 baste. |
| Dejar34 hijos tipo7 para después es seguro | [NV] | Hay21/13 hijos tipo7 sin metadatos; algunos emiten2556 desde Shadow pese a controlador host. Emitir no prueba aceptación/doble IA. No aprobar una primera activación que divida la unidad sin contrato demostrado. |

**Cambios obligatorios.** Medir productores positivos y negativos, incluyendo `0x28981B0`, y cerrar la relación descriptor→unidad→2005. Mantener un único mid y reservar seguimiento por época/nacimiento antes de modificar EAX. `KeyTable` no contiene por sí sola época ni token en `mark`; una colección de atómicos tampoco equivale a una instantánea coherente.

La limpieza necesita resultado estricto, estado de fallo propio sin ocultar excepciones del motor, continuación en servidor después de desconexión y cancelación coordinada de altas/Acks pendientes. Una clave retirada no debe reaparecer por un2005 viejo. Conservar el orden familiar y revalidar propiedad/generación. No usar «una Req del host» como demostración automática de doble control: comprobar emisor autenticado, aceptación y efecto según opcode.

**Ciclo de vida.** Mantener controlador invitado cuando el anfitrión se acerca es razonable si el invitado sigue competente; evita un traspaso en vida aún no demostrado. Viaje/cambio de campo/logout requieren cancelación y retirada terminal de su época. Alejarse andando sin cargar sigue abierto: el nuevo controlador también puede quedar lejos; medir el comportamiento del recolector y no inventar un radio. Bandadas, caravanas, monturas, compañeros y NPC reconstruidos no se consideran equivalentes.

**Pruebas mínimas.** Puras: productores alternativos, sesión desconocida, capacidad, unidad3/5/6/7, Logout rechazado,65/91/128 pendientes sin siguiente Troc, clave reutilizada y paquetes anteriores a la baja. Peer probe: procedencia y limpieza, nunca locomoción. Shadow: lejos/cerca/lejos, carro/animales, alejamiento sin carga, desconexión, recepción segura de ambos clientes y crédito de misiones del anfitrión. Reqs rápidas no bastan como éxito: S34 ya las tenía sin movimiento.

## 4. Contención y relevo: aprobada con cambios importantes

El bug del emisor y las retiradas de NPC con acción aplicada están demostrados. La solución debe corregir la extracción de evidencia, las carreras de retirada y el comportamiento de degradación; aumentar un timeout no basta. [Dictamen completo](datos/auditoria-codex-20261014/a4-contencion/INFORME-A4.md).

| Afirmación | Dictamen | Evidencia primaria y corrección |
|---|---|---|
|843/843 líneas emisor0, aplicada1, progreso0 | [V] | A261+B223+C359, reproducidas por auditor4 y coordinador. Son líneas de sonda, limitadas por actor, no todas las llamadas al motor. `recuentos.json` y `verificacion-transversal.json` conservan líneas y valores. |
| param4 es la sesión que permite leer +90 | [F] | `npc_action_probe.cpp:41–42` interpreta incorrectamente ese argumento. `0x2A78CF2` llama vf+170; `2A78CF8/2A78CFE` compara la clave devuelta con actor+90. `0x2a78ca0.c:39–44` y caller `0x296caa0.c:211–213`. El nombre dinámico exacto del contexto permanece desconocido. |
| El bug provoca retiradas falsas | [V] | C110 diagnósticos,109 con aplicación previa; antes del corte aproximado QPC69380,44:71 diagnósticos,60 raíces retiradas,65 líneas de miembro y60 bajas25 recibidas. Ejemplo `B0100391`: CH7896/10055/10090/10903–10906, Req+1,671541s, aplicación+1,705458s, diagnóstico+5,035230s, retirada+5,056492s; CG1984/4526/4774. No equivale a demostrar que todos caminaban. |
| Quitar comparación de sender basta | [V] para este falso negativo / [P] como arreglo completo | H1b acredita la ruta nativa autorizada y aplicada, conservando Pending TLS, nacimiento/época, actor/CC y no retirado. No aceptar cualquier Req ni cualquier retorno0. actor+90 es controlador esperado, no emisor observado. No llamar vf170 desde un worker desconocido sólo para medirlo. |
| Rama281 y retorno0 equivalen a H1b | [P] | Rama281 es un camino específico con contrato propio y acción ficticia de secuenciador; las843 líneas son rama0. Puede acreditar activación bajo su condición nativa, no movimiento ni misión. Retorno0 aislado sigue siendo insuficiente. |
| Ninguna unidad normal supera5s; máximo3,34s | [F] como generalización | A tiene22 aplicaciones>5s, de las cuales20 sin hijo6 publicado identificado tardan5,080987..5,634932s (AH12458..12523). Excluir A y seleccionar sólo éxitos B/C invalida el argumento de cota. Hay nacimientos sin recepción/aplicación y censura por baja/cierre. |
|8s, Req y cualquier tráfico en2s constituyen criterio suficiente | [P] | Son puntos de partida. Req acredita actividad, no aceptación; tráfico de otro actor no acredita entrega a esta unidad. No contar timers sintéticos como vida del cliente. Revalidar Req/aplicación también al ejecutar el plan (`user2_stages.cpp:1913`), no sólo al seleccionarlo. |
| Al salir el host hay21,2s exactos | [P] | Intervalo homogéneo BG recepción→recepción: **21,402565s**, desde última baja19 hasta primer2005 tipo3 del rearmado; entrega BIN≈21,409s.21,2 sólo es aproximación/cohorte distinta no explicitada. |
|55 stages se crean en24ms | [F] | Eventos nativos QPC abarcan **173,609ms**; BH47184..47299. Listas→última stage183,979ms. No usar el momento de impresión diferida como momento del evento. |
|483 altas y306 bajas42 pertenecen a la misma ventana declarada | [P] | Ventana alineada68724,422..68753,422:321 altas tipo3 y307 bajas42, vida completa mediana2,526265s.483 altas incluye la ráfaga desde68722. Conservar límites y censura, no mezclar cohortes. |
| Relevo de llegada correcto, cero duplicados universales | [V] local / [P] universal |209 bajas19 recibidas; hueco hasta cualquier tipo≈0,17–0,18s, hasta tipo3 por recepción197,055ms. DRAIN espera preparación y0 Trocs en vuelo. No se observaron duplicados de clave viva/U-f2 en esa llegada; no demuestra ausencia de duplicados semánticos con claves distintas. |
| Salto≤7→≥10 prueba viaje rápido | [F] | La distancia no identifica quién se movió, corrección, carga, campo, muestra omitida o época. Exigir evento de carga/viaje y muestras/contexto coherentes antes de acelerar. |
|2043 con +B=0 siempre es inocuo | [P] | `0x287DBB0`, export:132–155, evalúa sin reencolar en su estado normal2. Un estado/campo/preparación distinto al despacharse altera la ruta. One-shot no reemplaza reparar una cadena periódica muerta. |

### Latencias reproducidas, sin ocultar la cola tardía

Desde encolado, salvo la Req del invitado alineada mediante mínimo tránsito; percentiles lineales. No tratar la muestra de aplicaciones como tasa de éxito de todos los nacimientos.

| Métrica | n | p50s | p99s | Máxs | >5s |
|---|---:|---:|---:|---:|---:|
| A aplicación nativa |261|3,250|5,545|10,358|22|
| B aplicación nativa |223|2,314|3,336|3,344|0|
| C primera Req invitado, alineada |365|1,202|2,676|10,317|2|
| C primera Req recibida por host |365|1,242|2,864|10,360|2|
| C aplicación nativa |359|1,292|2,872|10,377|2|

Offsets mínimos invitado−anfitrión: B−68241,565370s/230 pares; C−68241,540323s/371 pares. Ráfaga C199 altas: exceso mediano51,068ms, máximo96,365ms. Esto describe esas ventanas; RTT/2 no mide latencia unidireccional ni elimina deriva/asimetría.

### Desglose corregido del rearmado B

Última baja19 recibida: QPC host alineado68701,091553 (BG24868); ARMED68711,899255 (BH45825);2043 a68721,951478 (BH47174); listas68722,082045 (BH47175); primera/última stage68722,092415..68722,266024; primera alta tipo3 recibida68722,494118 (BG27271, clave `B010103A`; entrega BIN: paquete/CDPK@146811672, registro del actor@146811703).

Así, baja→armado **10,807702s**, armado→2043 **10,052223s**,2043→primera alta recibida **0,542640s**. La primera parte incluye muestreo e histéresis; no se registró la primera muestra lejana. No demuestra que toda Hernand estuviera visualmente vacía. La ráfaga completa de salida contiene241 bajas19 y56 paquetes2126; los222/55 originales necesitan su filtro espacial/de stage explícito.

**Cambios obligatorios.** Separar publicado, entregado si se conoce, Req autenticada, aplicación, estado restaurable y retirada. Una Req puede suspender una decisión destructiva mientras se diagnostica; no debe proteger para siempre una operación rechazada. No retirar por cola/carga/silencio del cliente ni consumir presupuesto del nodo con evidencia incompleta. La ausencia de miembros en hold no demuestra familia completa. Conservar versión de unidad y comprobar nuevamente al retirar, con el orden de locks existente y sin llamar child_hold bajo ActivationLock.

La degradación también forma parte del arreglo: `containment_ready` falso reactiva el vigilante antiguo de5s (`npc_activation_logic.h:127–135`; `user2_stages.cpp:2508–2519`). Una pausa local no debe reintroducir ese aborto global. Diferenciar fallo de infraestructura de NPC sin activación; conservar límites y observabilidad de retirada parcial.

Para el rearmado: un puntual pendiente por sujeto/época/armado, resultado de encolado y cancelación trazables, reparación periódica separada y estadísticas que no confundan puntual con cadena viva. Invalidar trabajo viejo al drenar/salir/rearmar. Eliminar el WRN de drenaje usando el estado real `g_p_draining`, no una espera fija de1s. La promesa de hueco<2s requiere medida posterior. Conservar stages user2 al llegar el host cambia U-1 y queda fuera de esta corrección: U-f2 no prueba propiedad de escenas ni misiones correctas.

**Pruebas mínimas.** Fixtures A/B/C, rechazo, rama281, reentrada TLS, Req entre selección y ejecución, familia incompleta y pérdidas. Peer: readiness, colas antiguas, chain/one-shot, cambio de campo y fallo de encolado. Shadow: distribuciones con carga y censura, cero retiradas falsas, llegada/salida por fases homogéneas, sin duplicados semánticos y con misiones sólo del anfitrión.

## 5. Guardado del anfitrión

**Dictamen: aprobada con cambios para el diagnóstico y sandbox/probe. La publicación `real` no está aprobada con el diseño actual.** M1 y M2 ampliadas son requisitos previos, no comprobaciones posteriores a escribir la partida real.

### Afirmaciones y recuentos

| Afirmación | Clase | Resultado y fuente primaria |
|---|---|---|
| La caja se vuelve a copiar de la real al arrancar | [V] | `save_hooks.cpp:571–606,986–1005`: borra el `_prev` antiguo, mueve caja a `_prev` y copia la real antes de instalar hooks. AH:108/BH:106/CH:108 registran7 archivos,3 carpetas,2686266 bytes. Sin exportación, una ejecución deja de conservarse al segundo arranque exitoso posterior. |
| Los autoguardados A de18:21/18:36 están demostrados | [NV] | El AH disponible termina18:10:02 y tiene19841 líneas. No cubre esos eventos. Tampoco se reprodujo el inventario histórico de partidas reales del03-10. No confundir mecanismo probado con historia reconstruida sin fuente disponible. |
| B/C no guardaron al anfitrión | [V] en sus ventanas | Cero entradas host a Save; una guest en cada uno. BH:52038/CH:19260 y lector propio `audit_log_coverage.py`. No prueba ausencia de guardados fuera de la ventana. |
| Guest guarda a204 y los flush observados no tienen pendiente | [V] | AH:14942–15018, BH:50592–50783, CH:17757–17839:100→204, resultado0, export24/25/26, pending−1. BH:51532 y CH:19466 sí contienen Flush host sin E/S; corregir la omisión del C en I5. |
| `owner==Host` siempre es identificación positiva | [F] | `guest_save_logic.h:454–458` devuelve Host cuando `!any_guest_session`, sin exigir `is_host/session_is_host`; datos en `guest_save_host.cpp:228–250`. No reutilizar ese fallback para autorizar publicación real. |
| Flush host acredita que el buffer pendiente pertenece al host | [F] como regla | `0x1871930` usa `mgr+0x5C`, CommitSlot y PostSave con el actor de la llamada. No vuelve a serializar la pareja. Hace falta procedencia del buffer, no sólo del invocador. En S34 fue no-op por−1. |
| Save serializa el usuario/personaje indicados | [V] con límite | `0x186E450`, export propio líneas128–157: lobby de `[character+0xA0]` y save del personaje, dos serializaciones. No demuestra aislamiento de los globales leídos por sus componentes. |
| Dos aperturas y el renombrado prueban guardado completo | [F] como condición suficiente | Los hooks actuales no observan WriteFile/FlushFileBuffers/CloseHandle; un handle abierto no acredita cierre, bytes completos ni generación. `save_hooks.cpp:389–411,473–478,1005–1027`. El modelo de fallo es de especificación, no un incidente observado del motor. |
| Cabecera y SHA acreditan autoría y pareja coherente | [F] | `guest_save_logic.h:148–166` valida estructura/tamaños. SHA certifica igualdad con una fuente; no dueño, misiones ni coherencia lobby/save. Contramodelos propios en `counterexamples.json`. |
| Recuperación `_new/_backup` existe | [V] | `0x2358EF0`, export225–289/292–417, y `0x235A800`: elimina `_new` y puede restaurar backup sobre slot normal. No elige automáticamente la nueva pareja por tener mejor hash. |
| El publicador replica exactamente la secuencia nativa | [P] | Los logs guest muestran dos ciclos por Save, uno por archivo; la propuesta publica una pareja en un ciclo. La equivalencia ante corte no está demostrada. |
| M1/M2 originales bastan; backups hermanos están fuera de Cloud; publicar tarda<1s | [F]/[NV] | M1/M2 omiten condiciones detalladas abajo. Ubicación efectiva Cloud y coste del publicador inexistente no se midieron. No convertir estimaciones en garantías. |

Recuento independiente, entradas de función, no resúmenes periódicos:

| Log | Load host/guest | Save host/guest | Flush host/guest | Operaciones redirect |
|---|---:|---:|---:|---:|
| AH |3/3|0/1|0/1|30, todas slot204|
| BH |4/4|0/1|1/1|30, todas slot204|
| CH |3/3|0/1|1/1|30, todas slot204|

BH incluye un login posterior a su último resumen. Las30 operaciones no son30 archivos. Cada Save guest duró118/126/116ms: no mide publicación host. Un `committed slot 1163343936` de Flush sin pendiente es un parámetro de salida no escrito; no una ranura real (`0x1871930`, camino−1). Fuentes y secuencias completas: `a5-guardado/logs.json`, extractos y dictamen independiente.

### Diseño obligatorio antes de `real`

1. **Identidad positiva y procedencia completa.** Host, cuenta, mundo/época, sesión, gestor, ranura, generación, salud efectiva de guest_own_save y resultado protegido. Un `Flush` sólo hereda autorización del buffer serializado identificado. Resultado nativo es `res*`: leer `*res` con protección; RAX no es el código0.
2. **Contextos anidados y contaminación versionada.** Stack TLS con RAII y registro sincronizado por generación, no un booleano que otra llamada limpia pueda borrar. No sostener locks del mod al invocar el original ni durante E/S. Sólo bytes/metadatos inmutables pasan al worker, nunca punteros de juego.
3. **Cobertura de E/S demostrada.** Contar rutas ya dirigidas a caja, además de las redirigidas: `redirected()` sale en `!hit` (`save_hooks.cpp:390`). Canonicalizar cuenta/ranura/componentes, variantes A/W y origen/destino; evitar doble registro. Aperturas anteriores, handles duplicados, escritura diferida o mapeada requieren cobertura o rechazo explícito. Observación fallida/tabla llena nunca significa limpio.
4. **Pareja indivisible.** Extender exclusión hasta una instantánea lobby/save de la misma generación. Copiarlos consecutivamente sin exclusión permite mezcla. La barrera2077 no acredita por sí sola2910, Flush ni procesos externos. Medir espera y copia; validar ambos archivos antes de encolar.
5. **Publicación durable y recuperable.** Manifiesto con build/esquema, cuenta/mundo, dueño, secuencia, ranura, hashes, tamaños y fases. Backup verificado antes de mutar. No borrar `_new/_backup` desconocidos: preservar/clasificar. Cola acotada y ordenada, cierre, fallos y rollback idempotente. No podar la última pareja válida para solucionar disco lleno. RawScope debe ser reentrante, completo y exclusivo del worker propio.
6. **M1 y M2 ampliadas antes de real.** M1 demuestra procedencia, completitud, concurrencia y recuperación; M2 demuestra aislamiento semántico. Un archivo correctamente atribuido al host puede contener estado global cambiado por guest. Las dos pruebas cubren riesgos distintos.

La alternativa de menor riesgo es exportar primero generaciones inmutables del anfitrión fuera de la carpeta real, manteniendo sandbox por defecto. `probe` clasifica y verifica candidatos sin publicar. Una promoción inicial fuera del proceso, juego cerrado y Cloud controlado, reduce concurrencia; no sustituye M2. Es una propuesta futura, no una herramienta implementada aquí.

### Qué puede corromper o hacer perder progreso

[V] Recuperación nativa observada estáticamente: con backup viejo y slot nuevo presentes, `0x2358EF0` puede restaurar el viejo. Por tanto `new→slot` no es todavía un commit durable del protocolo propuesto. Un cierre antes de archivar backup puede perder el último guardado aunque ambos sean válidos. Otros riesgos son pareja mezclada, copia incompleta, autoría ambigua, cola fuera de orden, borrado de recuperación ajena o contaminación de globales. Fuentes: exports propios de2358EF0/235A800 y `audit_models.py`.

La secuencia de varias operaciones no se vuelve atómica por usar MoveFileExW. Debe definirse y probarse la durabilidad de archivos/manifiesto y el punto de recuperación. Véanse [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw) y [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers). [NV] Falta medir cuándo vf7 reconstruye el almacén respecto a la instalación del mod: recuperar después de que el juego ya haya seleccionado su partida puede llegar tarde.

[V] Hay escrituras globales en RVA `0x1791B98` y `0xB74302` hacia `0x6C8CD98`, relacionadas con `_currentRestoreCodeVersionXXX`/`ExecuteRestoreCodeVersion` (`0x187C490`, `identity_probe.cpp:65–82,467–500`, `extra-bytes.json`). [NV] Estos ensayos no muestran que guest cambiara perjudicialmente ese valor; tampoco prueban inocuidad. El recorrido `0x20688A0` por componentes no descarta que un vf escriba un singleton.

M2 debe comparar marcadores deliberadamente distintos de host/guest: versión, eventos/precios, reloj, misiones, vivienda/gimmicks, inventario y dinero, con control sin guest y recarga posterior. Observar contenedores/valores, además de Acks. No restaurar globales ciegamente si eso borra progreso legítimo compartido. Una acción de misión hecha sólo por guest debe persistir en host sin avanzar su diario propio.

**Política propuesta:** whitelist de publicación0–2/100–108, exacta por cuenta; excluir3/200–207, incluidas204/206. [V] **No aísla CrossSave por sí misma:** la ruta host sigue pasando ranuras salvo204 (`guest_save_logic.h:597–599`); `0x186E450` rama3 llama UploadCrossSaveFileAsync. Redirigir archivos no bloquea esa subida. `0x186F570` borra206 bajo su condición de carga exitosa; no usarla de staging.

[NV] No se comprobó configuración Cloud efectiva. Valve permite grupos, raíces/patrones y recursión: una carpeta hermana no prueba exclusión. Confirmar ubicación de backup y sincronización antes de promoción/recuperación; una nube rival puede reemplazar o propagar una generación. No se afirma que este juego tenga sincronización dinámica habilitada. Fuente: [Steam Cloud](https://partner.steamgames.com/doc/features/cloud).

**Pruebas mínimas:** puras en archivos sintéticos para owner fallback, Flush ajeno, anidación, carreras, pareja mezclada, handles y fallos en cada escritura/flush/rename; peer en sandbox/probe para M1/M2 y autosave host realmente capturado; Shadow para cargas simultáneas, misiones y recarga host; Cloud al final con datos de laboratorio. Ninguna se ejecutó aquí. El plan completo, bytes/firmas y secuencia de recuperación están en [dictamen independiente5](datos/auditoria-codex-20261014/a5-guardado/AUDITORIA-5.md).

## 6. Contraste transversal y condiciones comunes

### Contradicciones que deben resolverse antes de programar

| Cruce | Dictamen de la auditoría |
|---|---|
| I1 identifica `B01004BF`; I4 atribuye el cierre C a `B0100578` | Los registros y la cadena CC→actor del DMP resuelven la discrepancia: falla la capa de `B01004BF`; `B0100578` la consulta por jerarquía. Corregir el diagnóstico GuestS3 y el texto de I4. |
| I1 exige seguridad de publicación general; I2 conserva excepciones NoCc y ambientación ajena | La política parcial no elimina la clase de fallo. NoCc incluye lecturas fallidas; el cierre C demuestra que otro actor consulta por jerarquía la capa de su padre, incluso con +AC=0. El DMP no permite afirmar si ese consultante carecía de CC propio. La seguridad debe alcanzar todas las rutas pertinentes y dependencias, sin aplicar el campo restore a tipos que no lo tienen. |
| I2 conserva tipo7 en host; I3 propone herencia de tipo7 de caravanas | No es necesariamente contradictorio si son capacidades/procedencias distintas; falta demostrar esa separación y su prioridad. No elegir por tipo ni por cuál módulo ejecuta primero. |
| I2 depende de npc_activation=1; I3 quiere autonomía respecto a U-e/contención | Pueden tener ámbitos de decisión distintos, pero necesitan compartir las invariantes de nacimiento, publicación, generación y retirada. Reutilizar una función con estados de error globales no preserva independencia. |
| I4 propone conservar stages del invitado al llegar el anfitrión | Cambia el contrato de U-1 y puede rozar la autoridad de misiones del anfitrión. Deduplicar posiciones no prueba equivalencia de stages, eventos, dueños ni progreso. No incluir esta variante en la corrección inmediata. |
| I5 publica sólo escrituras atribuidas al anfitrión | Esa procedencia no impide que el estado serializado contenga globales alterados por la carga del invitado. M1 y M2 cubren riesgos distintos. |

Fuentes: los cinco dictámenes y sus tablas primarias; para el primer cruce, DMP C offsets `0x73652/0x112083`; para NoCc, `child_hold.cpp:62–69,146–149`; para selección familiar, `npc_activation_logic.h:393–414`.

### Problemas adicionales detectados

1. **Una clave de padre no identifica su encarnación.** El token birth del hijo evita ciertas mezclas, pero no enlaza ese hijo al nacimiento correcto del padre. Se reproduce la insuficiencia del selector con el modelo propio basado en `family_into`; no se ha demostrado ese incidente en una captura S34. Añadir parent_birth/root_birth o token equivalente, y definir cómo enlazar hijos tardíos sin inventar su generación.
2. **Asignación parcial por capacidad o reentrada.** Reservar sólo la raíz y reiniciar un marco TLS único permite padres guest con hijos host aun después de implementar la herencia. Los límites deben admitir o rechazar el subárbol conocido antes de cambiar autoridad, con telemetría de saturación y una política explícita para hijos tardíos.
3. **Lectura protegida no significa lectura válida.** Las lecturas protegidas pueden contener fallos de acceso del lector; eso no demuestra ausencia de CC, consistencia del descriptor ni vida útil entre dos lecturas. Las excepciones nativas de las llamadas al motor conservan su política de propagación (`user2_stages.cpp:1637`). Mantener estados «desconocido», «ausente demostrado» y «presente» separados.
4. **Desconexión sin siguiente Troc.** La limpieza de hasta64 actores por llamada no garantiza terminar si quedan más actores, la pasada no progresa o se interrumpe y no llega otro Troc del invitado. La tarea debe sobrevivir con un punto seguro del servidor y no reutilizar punteros de la sesión destruida.
5. **Pruebas puras con ABI supuesto.** Los tests de progreso pueden pasar aunque el cuarto argumento se haya interpretado mal. Añadir fixtures derivados de los valores/caminos reales y comprobar la extracción de evidencia, además de la política pura. `tests/optd_npc_activation_tests.cpp:261–265` cubre condiciones de rama, pero no acredita el ABI de `npc_action_probe.cpp:41–42`.
6. **Compatibilidad con módulos del cliente.** No hay aquí prueba suficiente para culpar a remote_interp, remote_motion o combat_follow de los fallos observados. La futura asignación local de nuevos tipos sí exige verificar modo nativo de movimiento y ausencia de correcciones concurrentes por esos módulos (`remote_interp.cpp:239–335`; `combat_follow.cpp:239–249`). Tratarlo como prueba pendiente, no como regresión demostrada.

### Fronteras de autoridad y concurrencia

Cambiar `actor+0x90` atribuye simulación de IA; no autoriza cambiar `actor+0xA0`, la partida del mundo ni el gestor de misiones del invitado. El criterio de aceptación incluye que una acción del invitado produzca el crédito esperado en la misión del anfitrión y ninguno en la propia. «No se tocó quest_policy» no demuestra ese resultado para una nueva cohorte.

Una unidad coherente exige una política completa de control y publicación para sus dependencias; no implica por definición el mismo controlador para todos los tipos. La homogeneidad de autoridad es una restricción prudente de la primera cohorte3/6. Una excepción tipo7 sólo puede aceptarse después de demostrar su contrato; hoy esa seguridad no está probada.

Toda decisión diferida identifica `(época, clave, nacimiento, unidad)`. Reservar seguimiento antes de asignar; validar de nuevo antes de llamar a funciones del juego; no marcar baja completada por entrar en Logout o recibir un resultado no interpretado. Mantener un único dueño del mid compartido y especificar orden de locks/reentrada. Una parada de subsistema no puede reactivar silenciosamente un aborto global ni liberar estado inseguro.

## 7. Puntos de enganche y límites de lo comprobado

Se leyó el PE en disco: timestamp `0x6AB28F00`, base `0x140000000`. Los puntos siguientes son evidencia para esta build, no autorización para instalar nada. `consolidacion/verificacion-transversal.json` conserva bytes y pdata; los dictámenes independientes añaden contexto y ramas.

| Punto | Resultado |
|---|---|
| `0x2792A7B` | `.rsrc`, interior de `[2791CD0,2792FC8)`. Sólo `create_mid`, instrucción completa7B `41 89 86 90 00 00 00`. AOB24B única: `41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5 48 8D 55 67 48 8B CE FF`. Ningún salto directo intrafunción entra en el interior; `2792A79` salta a2792A82. Reutilizar el hook existente de docked_gimmicks. |
| `0x2A78CA0` | Entrada pdata `.rsrc`; primer MOV5B. Firma completa kActionBytes40B de `npc_action_probe.cpp:22–23` coincide y es única. Los primeros32B por sí solos tienen3 coincidencias. |
| `0x281CEE0` | Entrada pdata `.rsrc`; primer MOV5B. Firma completa kNodeBytes32B única. No confundir la validez física del hook con que devolver false sólo retire un nodo en todos los llamadores. |
| `0x186E450`, `0x186F570`, `0x1871930`, `0x20688A0`, `0x287EF60` | Entradas pdata `.rsrc` comprobadas. Ampliar lógica de los hooks existentes, no solaparlos. Usar sus firmas completas; varios prefijos32B no son únicos. El dictamen5 verifica las firmas completas de Load/Save/Flush de47/47/43B, respectivamente, únicas en todo el EXE. |
| `0x88F832`, `0x1831598` | Lugares de excepción interiores, no entradas. No instalar inline aquí. Verificar bytes y semántica completa si se estudia otra defensa; una guarda no reemplaza el contrato de publicación. |
| `0x2A827B0`, `0x1434880` | Entradas pdata `.rsrc` para búsqueda y destructor del scope. Eso no demuestra que cualquier hilo o cerrojo permita llamarlas. |
| `0x2068340` | Está en `.rsrc`, pero es trampolín `E9 AB 17 07 0C` hacia `0xE0D9AF0` y no tiene entrada pdata propia. No presentarlo como cuerpo de Logout legible. Ya existe el mid O sobre ese salto (`user2_stages.cpp:78,1026–1027,3012–3024`): reutilizar/verificar su contrato sin segundo parche; la ausencia de pdata no demuestra que el thunk sea inenganchable. La llamada existente necesita resultados nativos comprobados y contexto seguro. |

La comprobación de saltos descrita es intrafunción y de destinos directos; no prueba ausencia universal de destinos indirectos en código virtualizado. No se recomienda ningún enganche en `.xtls`. Las escrituras de descriptor enumeradas en los informes son sitios de evidencia, no nuevas entradas de función.

## 8. Orden recomendado y puertas de aceptación

Este orden es una propuesta para trabajo posterior; la auditoría no ejecutó las pruebas ni autoriza automáticamente su implementación.

| Orden | Trabajo | Qué debe demostrarse antes de avanzar |
|---|---|---|
| 1 | Corregir atribución de actor y extracción de progreso; actualizar fixtures de S34 | Puras: aplicación nativa con emisor diagnóstico0 cuenta; retorno0 sin aplicación no cuenta; dueño rechazado, nacimiento viejo y lectura desconocida no se aceptan. Peer prueba instrumentación/estados; Shadow confirma la señal real. No repetir C con el criterio defectuoso. |
| 2 | Cerrar publicación insegura y definir salida funcional | Matriz completa de Native/Timeout/familia/NoCc, capacidad, fallos de lectura y jerarquía. Ningún caso desconocido se libera por expirar. Cola y dependencias por generación. Peer mide retención/retirada; Shadow valida inicialización y visibilidad. |
| 3 | Sondas y registro completo de familias | Descriptor padre/envoltorio, Troc anidado, capacidad reservada, parent_birth y hojas antes de padres. Activar sólo una cohorte medida de3/6; tipo7 según capacidad/dependencia demostrada. Misiones del anfitrión intactas. |
| 4 | Procedencia y ciclo de vida de viajeros | Distinguir2550 actual de reconstrucción `28981B0`; negativos obligatorios. Limpieza estricta y continuable tras desconexión, publicación cancelada y unidades completas. Luego Shadow mide movimiento, no sólo Reqs. |
| 5 | Optimizar rearmado/2043 | Probar estados/cadenas/generaciones y una señal real de viaje/carga. Primero medir; después reducir la latencia sin crear temporizadores persistentes duplicados ni invadir preparación del anfitrión. |
| 6 | Persistencia del mundo: procedencia y recuperación, antes de carpeta real | M1+M2, instantánea coherente, pruebas de fallos sintéticos en cada paso de publicación y recuperación anterior a lectura nativa. Cloud y copias fuera de su conjunto efectivo comprobados. Publicar a real permanece bloqueado hasta cerrar estos contratos. |

### Lista mínima de pruebas futuras, sin duplicados

1. **Puras de evidencia/progreso:** reproducir las líneas de A/B/C y el contrato nativo; separar Req, aplicación, restore, movimiento; probar claves y padres reutilizados.
2. **Puras de unidad/publicación:** tablas llenas, lectura fallida, NoCc real, grupos anidados, hijo tardío, árbol profundo, paquetes mezclados, Ack viejo, retirada parcial y reintentos acotados.
3. **Puras de ciclo de vida:** caída de conexión sin más Troc,65/91/128 pendientes, SAM/lock temporalmente indisponible, Logout rechazado, nueva época y cancelación de publicación.
4. **Peer en un PC, sólo cuando se autorice:** sonda de descriptores y productores, tokens de Troc, orden/capacidad/colas, rearmado2043 y limpieza terminal. El peer no valida IA ni render de un segundo cliente.
5. **Shadow probe:** familias seleccionadas y viajeros positivos/negativos; logs por nacimiento y familia, controlador, entrega a ambos, primera Req y resultado, restore/movimiento. Incorporar carga real y saturación; no deducir salud de una petición aislada.
6. **Shadow activación por cohorte:** familia íntegra3/6, accesorios con contrato cerrado, luego viajeros. No activar varias políticas nuevas a la vez. Cero publicaciones inseguras/huérfanos, bajas confirmadas, comportamiento esperado y crédito sólo a misiones del anfitrión.
7. **Transiciones Shadow:** host cerca/lejos, invitado se aleja sin carga, viaje, cambio de campo, salida ordenada/abrupta, reconexión y claves reutilizadas. Que el host llegue no convierte por sí solo una Req tardía en doble autoridad.
8. **Guardado M1:** procedencia completa y parejas lobby/save, host/invitado/flush/manual/automático, anidación y operaciones fuera de ventana, sin publicación real.
9. **Guardado M2:** comparar estado global y progreso del anfitrión antes/después de carga del invitado y de acciones compartidas. Ausencia de un Ack no demuestra ausencia de una escritura global.
10. **Recuperación:** simulación sobre archivos sintéticos propios de caída entre cada escritura/flush/renombrado, permisos, disco lleno, archivos abiertos, reinicio y cola pendiente. Después, con autorización y copia, prueba real de cargar y verificar el estado del anfitrión y la interacción con Cloud.

Criterios comunes de rechazo: autoridad sin registro, mezcla de generaciones, publicación con estado indeterminado, baja marcada completa con error, trabajo que queda sin dueño al desconectar, misión propia del invitado que avanza, o cualquier escritura real sin procedencia/recuperación demostradas. Los plazos8s/2s y los radios deben tratarse como parámetros experimentales, no leyes del motor.

## 9. Evidencia propia y reproducción

Los cinco dictámenes independientes forman parte de esta auditoría; contienen tablas ampliadas, límites de cada lectura y más offsets que la síntesis. Se conservan junto con nuestros lectores, resultados y exports nuevos en [datos/auditoria-codex-20261014](datos/auditoria-codex-20261014/README-REPRODUCCION.md). Las capturas, volcados y ejecutable originales no se duplican ni se modifican.

| Área y cifra | Lector propio | Salida de referencia |
|---|---|---|
| Cierres: pila/actor/capa | `a1-cierres/audit_dumps.py` | `dumps-independientes.json`; desenrollado pdata/UNWIND_INFO, límites explícitos fuera del PE |
| Ocho entregas restore0 y familias recibidas | `a1-cierres/audit_captures.py` | `capturas-independientes.json`, offsets BIN absolutos |
| Cierres: lectores/bytes/mid | `a1-cierres/audit_static.py` | `estatico-independiente.json` |
| Familias completas/partidas/pendientes | `a2-familias/censo_independiente.py`, `log_independiente.py` | `A/B/C-censo.json`, `censo-log-vs-bin.json` |
| Constructores, RTTI y latencias familiares | `estatico_independiente.py`, `rtti_independiente.py`, `latencia2223.py` dentro de `a2-familias` | `estatico.json`, `rtti.json`; salida de latencias y dictamen2 |
| Casos que refutan el diseño literal | `a2-familias/contraejemplos_diseno.py` | `contraejemplos_diseno.json`; modelos de especificación, no incidentes en vivo |
| Viajeros327,0/82 y73/76, supervivientes46 | `a3-viajeros/auditar_viajeros.py`, `comprobar_ventana.py` | `recuentos.json`, `cohortes-evidencias.json`, `posiciones-tras-alta.json` |
| Productor adicional y límites de procedencia | `a3-viajeros/auditar_pe.py`, `comprobar_extra.py` | `estatico.json`, `comprobaciones-extra.json`, `ghidra-0x28981b0.c` |
|843 emisores0, retiradas, latencias y21,402565s | `a4-contencion/recuento.py`, `capturas.py`, `detalle.py`, `familias-tardios.py` | `recuentos.json`, `capturas.json`, `detalle.txt`, `familias-tardios.json` |
| Contención: PE/ramas/firmas | `a4-contencion/pe.py`, `pe-detalle.py` | `pe.json`, `pe-detalle.json` |
| Guardado: cobertura, llamadas y30 operaciones | `a5-guardado/audit_log_coverage.py` (usa `audit_logs.py`), `summarize_sequence.py` | `logs.json`, `log-summary.txt`, extractos A/B/C |
| Guardado: bytes, firmas completas y globales | `a5-guardado/audit_pe.py`, `audit_extra.py`, `audit_branches.py` | `pe-check.json`, `extra-bytes.json`, `branch-check.json` |
| Guardado: límites de cabecera y recuperación | `a5-guardado/audit_models.py` | `counterexamples.json`; modelo propio de las condiciones propuestas |
| Segunda revisión: actor consultante y thunk Logout | `revision-consolidacion/verificar_primarias.py`, `verificar_thunk.py` | `verificaciones-primarias.json`, `thunk-logout.json`, revisiones escritas |
| Contraste del coordinador, integridad y hashes | `consolidacion/verificar_transversal.py`, `inventario_fuentes.py` | `verificacion-transversal.json`, `inventario-fuentes.json` |

Ejemplo de ejecución de un lector autónomo, desde una **copia de trabajo** de la evidencia para no sobreescribir los resultados publicados:

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<copia-de-evidencia>\a1-cierres\audit_captures.py'
```

No todos los scripts son portátiles sin preparar rutas: algunos conservan el directorio temporal original y otros requieren la instantánea `codigo-d835806`. El [README de reproducción](datos/auditoria-codex-20261014/README-REPRODUCCION.md) especifica estructura, dependencias, orden y comandos. `-I -B` evita importar desde el entorno ordinario y escribir bytecode; los scripts que usan otros lectores los cargan explícitamente. Usan Python y, para desensamblado, Capstone del venv existente; no descargan herramientas ni ejecutan código del juego.

Para Ghidra se usó la VA calculada como `0x140000000 + RVA`, no concatenación textual. Ejemplo: RVA `0x28981B0` → VA `0x1428981B0`. Los exports originales se leyeron y los nuevos se conservaron. La recuperación de una pila depende de memoria realmente incluida en el minidump; no se sustituyeron huecos por valores inventados.

El inventario de fuentes fija hashes SHA-256 de los informes originales y las seis capturas. El manifiesto de entrega fija los archivos propios publicados. Las conclusiones se limitan a d835806 y build2976; aplicar estos resultados a otro commit/build exige contrastar diferencias y repetir los contratos afectados. Ninguno de los dictámenes aprueba activar a la vez todos los cambios propuestos.
