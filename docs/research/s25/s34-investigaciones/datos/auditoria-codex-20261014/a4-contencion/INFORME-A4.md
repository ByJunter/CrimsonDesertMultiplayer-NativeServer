# Auditoría independiente A4: contención y relevo

Trabajo de solo lectura, build 1.0.0.2976, PE 0x6AB28F00, código fijado d835806. Informe final del área A4. No se han compilado, desplegado o ejecutado juego/host/peer ni cambiado código, partidas o bin64.

## Evidencia ya contrastada

- [V] El contrato nativo de 0x2A78CA0 no autoriza interpretar param4 como NwVirtualAsyncSession. `0x2a78ca0.c:39-44` invoca vf+0x160 y obtiene un puntero a clave por vf+0x170(param4, scratch); la compara con actor+0x90. `src/mod/session2/npc_action_probe.cpp:41-42` en d835806 interpreta param4+off::kSession_User y lee user+0x60. La identidad correcta es el resultado de vf+0x170; actor+0x90 es el controlador esperado y sólo equivale tras pasar la comprobación nativa.
- [V] `npc_activation_logic.h:106-108` exige sender==controller; `user2_stages.cpp:3219` pasa el emisor mal leído; `no_progress():87-92` no consulta first_req_us y depende de applied_us.
- [V] Ejemplo host C líneas 7896/10055/10090/10903-10906: 0xB0100391 nace/publica, primera petición +1.671541 s, aplicación +1.705458 s, diagnóstico +5.035230 s, retirada +5.056492 s. Invitado C líneas 1984/4526/4774: recibe, pide y recibe baja motivo25. No prueba desplazamiento, sí acción válida nativa antes de retirada.

La tabla cuantitativa, las comprobaciones binarias y el dictamen siguen a continuación.

## Recuentos independientes (recuento.py / recuentos.json)

| Ensayo | Líneas OnServer | emisor=0 | aplicada=1 / progreso=0 | Publicados con registro | Recibidos emparejados | Aplicación observada |
|---|---:|---:|---:|---:|---:|---:|
| A | 261 | 261 | 261 / 261 | 268 | 267 | 261 |
| B (1b) | 223 | 223 | 223 / 223 | 230 | 230 | 223 |
| C (2b) | 359 | 359 | 359 / 359 | 397 | 371 | 359 |

[V] Total 843/843 con emisor cero, todas las líneas emitidas dicen aplicada1/progreso0. No equivale al total de llamadas al motor: el registro está limitado por actor (`action_lines`); es el censo de líneas. En C, 110 diagnósticos, 109 con aplicación previa; 0xB0100592 (host C:17732) es la excepción, posterior al fin del invitado. Antes del corte aproximado qpc69380.44 se reproducen 71 diagnósticos, 60 raíces retiradas (60 claves), 65 líneas de miembro y 60 bajas motivo25 emparejadas en el invitado. Cinco nodos bloqueados. Esto demuestra falsas retiradas por el criterio de activación, no que todos estuvieran caminando ni que todas las 110 unidades diagnosticadas fueran retiradas.

| Distribución (segundos) | n | p50 | p90 | p99 | máx | >5 s |
|---|---:|---:|---:|---:|---:|---:|
| C primera Req invitado (exceso sobre offset mínimo) | 365 | 1.202336 | 2.628087 | 2.676350 | 10.316795 | 2 |
| C primera Req recibida anfitrión | 365 | 1.242376 | 2.731602 | 2.864167 | 10.360084 | 2 |
| C aplicación nativa | 359 | 1.292090 | 2.799579 | 2.872051 | 10.376853 | 2 |
| B aplicación nativa | 223 | 2.313684 | 3.069247 | 3.336343 | 3.343923 | 0 |
| A aplicación nativa (omitida por el informe 4 al elegir plazo) | 261 | 3.249626 | 4.895625 | 5.545440 | 10.358199 | 22 |

Percentiles lineales propios. La tabla B/C del autor se reproduce aproximadamente; pequeñas diferencias de p99 son convención. **[F] como afirmación general «sin el bug ninguna unidad normal habría superado 5 s»**: A ya contiene 22 aplicaciones >5 s, y los datos B/C tampoco son una muestra completa de éxito; C tiene 38 publicados sin aplicación observada (26 sin recepción emparejada), B siete y A siete. Hay tipos de actores distintos, bajas y censura por cierre. No convertir los 582 éxitos B/C en una tasa 582/582 ni su máximo en cota de seguridad. La propuesta de 8 s es margen provisional, no plazo demostrado.

[V] Offset mínimo invitado-anfitrión: B -68241.565370 s (230 pares), C -68241.540323 s (371). Ráfaga C +8..+10 s del primer armado: n199, mediana exceso51.068 ms, p90 89.643 ms, máximo96.365 ms. **[P] «túnel no cuello de botella»** sólo en ventanas observadas, no garantía futura: el mínimo estima offset+latencia mínima, no latencia absoluta; 12 ms no medidos unidireccionalmente por RTT/2. No se ha estimado deriva del reloj, asimetría ni colas locales de entrega. QPC de PCs distintos no es comparable sin calibrar. Los registros textuales se imprimen con retraso; usar `qpc`/`~hora` del evento, no el prefijo de escritura. El reloj de pared del invitado va una hora detrás, con diferencia adicional aproximada; restar únicamente una hora no sirve para medir subsegundos.

[V] Capturas invitado B/C: cabecera CDPK v2, timestamp6AB28F00, cero registros truncados y cero gap-records. El lector `capturas.py` conserva offset físico de cada alta/baja. **t_us binario es QPC absoluto**, mientras la columna textual capture t resta el primer paquete. Los 6/1 paquetes con longitud no convencional se conservan como anomalía (incluyen apertura del protocolo); no se afirma pérdida cero en toda la tubería sólo porque el fichero no tenga gaps.

## Dictamen del diseño: aprobada con cambios obligatorios

El bug de atribución es real y explica las falsas retiradas medidas. El informe es útil para corregirlo, pero no autoriza todavía las tres políticas nuevas (contención 8 s + Req, salto de distancia como viaje rápido, conservar stages al acercarse el anfitrión). Tampoco demuestra seguridad universal de familias o ausencia universal de duplicados.

### Contrato de acción y corrección mínima

| Afirmación | Dictamen y prueba |
|---|---|
| param4 es una sesión NwVirtualAsyncSession | [F]. `0x296caa0.c:211-213` pasa su contexto param3 como cuarto argumento de StartActionLogic. `0x2a78ca0.c:39-44` y PE RVA2A78CF2 (`call [rax+170]`), 2A78CF8 (`mov ecx,[rbx+90]`), 2A78CFE (`cmp [rax],ecx`) muestran el contrato real. El nombre C++ dinámico exacto del contexto queda [NV] sin vtable runtime identificada; no hace falta inventarlo para corregir el bug. |
| H1b implica controlador autorizado y OnServer exitoso | [V]. Desensamblado desde la entrada pdata2A78CA0, retorno de OnServer y rama a2A79060; decompilado2a78ca0.c. `host_scene_diag.cpp:309-314` acredita sólo el Pending TLS correspondiente; `npc_action_probe.cpp:26-55,118-121` conserva pila TLS y revalida actor/clave y birth. |
| Un resultado externo0 basta | [F]. Existe la rama payload+281 y otras rutas de retorno; conservar evidencia H1b o evidencia específica de la rama281. `npc_activation_logic.h:110-116` y `user2_stages.cpp:3213-3219`. Las 843 líneas observadas son rama0; la rama281 está sustentada estáticamente, no ejercitada por esta cohorte. |
| Eliminar sender==controller resuelve el bug | [V] para la atribución en esta ruta, [P] como arreglo completo. Debe conservar nacimiento/época, actor y componente estables, no removido, aplicación nativa acreditada. No sustituir la validación nativa por confianza en cualquier Req. No rotular actor+90 como «emisor observado»: es controlador esperado. Para telemetría de identidad real, capturar de manera protegida el resultado nativo de vf170 en su punto ya demostrado, o dejarlo explícitamente desconocido; no invocar una vtable desconocida desde el worker. |
| Aplicación implica progreso de misión/desplazamiento | [F]. H1b prueba aceptación del motor; rama281 puede ser una acción ficticia del secuenciador. Es señal de activación, no avance de misión ni movimiento. El mundo y las misiones siguen perteneciendo al anfitrión. |
| Test «acción aplicada con emisor0 ⇒ progreso» suficiente | [P]. Añadir rechazos, retorno0 sin H1b, rama281 válida/ilegible, cambio de controlador, actor destruido/reutilizado y reentrada TLS. Una API que recibe `normal_applied` fabricado por cualquier llamador ampliaría indebidamente la confianza. |

Acreditar con una señal nativa ya validada es más simple que ejecutar de nuevo vf170. Leer actor+90 a la entrada sirve sólo como etiqueta del controlador al entrar; no autentica a un emisor que la función haya rechazado. Mantener los seis argumentos y las lecturas protegidas de `fast_read`/`fast_ptr`.

### Familia retenida y censura

[V] `familias-tardios.py` reproduce cinco hijos tipo6 con controlador90100000 en C, pero corrige el intervalo del informe: 048F/0490 se encolan **10.04546 s** después de048E (host C12848,15945-15946); 04BF/04C0 **10.09573 s** después de04BE (14027,16684-16685);0577 **10.09726 s** después de0576 (16977,17502). Los dos primeros padres aplican a10.296457/10.376853 s (16324,16705). El quinto hijo se publica después del fin de la captura invitada: no cuenta como arranque tardío exitoso ni como prueba de una familia completa recibida.

[V] B:03AE llega20.065240 s después de03AD (9828,13349); CONTROL ABORT qpc68505.534317, línea11570, indica un único no aceptado. [P] Es consistente con el bloqueo familiar y posterior retirada, no demuestra por sí solo causalidad exclusiva ni que todos los tipo6 tengan esa dependencia. A tiene otras dos raíces familiares tardías; además **20 aplicaciones sin hijo6 publicado identificado tardan5.080987..5.634932 s** (A12458..12523). Este dato cambia el argumento de margen para8 s.

No aprobar la alternativa «soltar el hijo hacia user2 aunque siga siendo del anfitrión» sin el arreglo de publicación segura: puede entregar restore0 sin haber arrancado. La herencia de control requiere la auditoría del área2 y exclusiones de propietario/usuario, monturas, mascotas, compañeros, combatientes y gimmicks. Una correlación de dos raíces no prueba que asignar todo tipo6 al invitado sea correcto.

### Nuevo criterio 8 s + Req + invitado activo + familia completa

**[P], aceptable como punto de partida conservador; insuficiente como especificación. Cambios obligatorios:**

1. Separar estados publicado, recibido/entregado si hay evidencia, solicitado, aplicado y retirado. Una Req es **actividad**, no prueba de aceptación o progreso: validar emisor de la sesión real, clave, época, nacimiento y controlador. No reutilizar el campo `applied_us` para una Req.
2. No retirar sólo por un reloj desde encolado cuando hay pérdida, cola, carga o congelación del cliente. Medir edad de cola, pérdida/rechazo, último evento de usuario y pausas. Cualquier Req del invitado en2 s no demuestra entrega a ese actor ni que su IA esté ejecutándose. Excluir temporizadores sintéticos2043/2045/2046 y tráfico viejo/reproducido de esa señal de vida.
3. Una Req puede suspender la retirada destructiva mientras se investiga; no convertir una petición repetidamente rechazada en progreso permanente. Registrar por separado razón de rechazo, última petición, aceptación y salud de la tubería. Ante evidencia incompleta, posponer y diagnosticar, sin consumir el presupuesto del nodo ni activar un aborto global.
4. «Familia completa» debe ser una instantánea con versión/nacimiento, vínculos padre-hijo y entregas de los miembros pertinentes al controlador. No basta «no queda nada en hold»: puede haberse perdido, descartado o retirado un miembro. Congelar o reiniciar justificadamente el plazo al cambiar la familia; hijos que nacen tarde no deben producir un reinicio infinito silencioso.
5. Al decidir y justo antes de retirar, revalidar **Req y aplicación** bajo la misma sincronización. Hoy `user2_stages.cpp:1913` sólo vuelve a mirar applied_us: cambiar sólo `no_progress` dejaría una carrera en la que una Req llegada después de reservar el plan no cancela la retirada. Mantener orden ModLock→barrera de Trocs→ActivationLock y no llamar a child_hold bajo ActivationLock (`:1862-1922`).
6. Separar muerte nativa/logout/despawn previsto del diagnóstico de falta de activación. No reanimar ni vetar el nodo de una baja válida, y no convertir cadáveres/animales/actor de escena quieto en «bloqueado» por no moverse. Identificar por época+clave+nacimiento; invalidar temporizadores y contabilidad al salir/reconectar, drenar, recargar campo o reutilizar clave/stage.
7. Evitar que estas pausas devuelvan indirectamente el aborto antiguo: `npc_activation_logic.h:127-135` y `user2_stages.cpp:2508-2519` lo reactivan si readiness falla. La ruta vieja mantiene5 s y puede volver a retirar/reasignar todo por una unidad familiar. Especificar degradación segura por unidad y distinguir fallo de infraestructura de «NPC muerto».
8. **8 s** es una política inicial, no garantía. Repetir distribuciones por cohorte completa, tipo/familia, carga y tamaño de ráfaga, con censura explícita por cierre/baja/pérdida. Medir también el máximo/percentiles de Req→OnServer; no prometer que toda primera Req lleva a aplicación30..70 ms.

### Salida del anfitrión: qué se reproduce y qué debe corregirse

La espera dominante sí es histéresis + temporizador periódico. Las cifras exactas del informe no se reproducen todas con un lector independiente.

| Evento primario B | QPC anfitrión (s) | Fuente |
|---|---:|---|
| Última baja19 recibida en invitado de la ráfaga de salida | 68701.091553 | invitado log24868; primer evento24620 a68701.091067 |
| ARM solicitado | 68711.895745 | host45820-45821 |
| ARMED (salida Req2556) | 68711.899255 | host45825 |
|2043 llega, estado2 reschedule1 |68721.951478| host47174 |
| listas reconstruidas |68722.082045| host47175 |
| primera/última de55 stages user2 |68722.092415..68722.266024| host47184..47299 |
| primer2005 tipo3 posterior al rearmado recibido |68722.494118| invitado27271 |
| mismo primer2005 capturado al entregar |68722.504265| B.bin offset146811672, registro146811703, claveB010103A |

[V] Espera ARMED→2043 **10.052223 s**. [V] Última baja19→ARMED **10.807702 s**, incluye muestreo y confirmación de distancia: la primera muestra lejana no está registrada, es una inferencia del dwell. [F] «55 stages en24 ms»: abarcan **173.609 ms**, y listas→última stage183.979 ms; el primer create está10.37 ms tras listas. [P] «21,2 s exactos»: el intervalo homogéneo de **recepción a recepción da21.402565 s** y no requiere sincronizar PCs. La captura, en fase de entrega, da aproximadamente21.409 s. La cifra21.2 puede ser aproximación o una cohorte/phase distinta no explicitada; no debe usarse como dato exacto.

[P] La ciudad vacía es lo percibido en vivo; el paquete no acredita visibilidad de toda Hernand. En la ráfaga de salida la captura da241 bajas19 (232 tipo3 identificados,8 tipo6,1 tipo5),56 paquetes2126. El informe habla de222/55 sin definir suficientemente su filtro espacial/de stages. No confundir ese subconjunto con el censo total.

[P] Las altas posteriores llevan controlador90100000 y el aborto previo explica que U-e no reasigne (`user2_stages.cpp:2515-2524`). El ciclo motivo42 es real: en la ventana alineada[68724.422,68753.422],321 altas tipo3 y307 bajas42; vida completa mediana2.526265 s. Las **483 altas** se reproducen incluyendo la primera ráfaga desde68722, no sólo desde18:56:15. Las vidas restantes están censuradas al cerrar la sesión. El número de vivos depende de región/cohorte y del punto final; no se deduce presencia en pantalla.

### Llegada: relevo local respaldado, universalidad no demostrada

[V] B36130-36131: DRAIN held a68642.841536 tras5.2 s de carga. B36424-36425: preparación nativa desde retorno289604E, hilo servidor,0 Trocs user2 en vuelo y52 stages. B37797: DRAINED a68647.011021. B36418: WRN a68646.844940 **dentro** de la operación; `user2_stages.cpp:2299-2305` no excluye g_p_draining. Guardar por operación/generación real, no un parche temporal «durante1 s».

[V] Invitado B20102..20318:209 bajas19, QPC alineado68647.000869..68647.028808. Captura B offsets113047926..113067914:209 bajas19;52 paquetes2126. Primer2005 (tipo7) entrega68647.213376 (offset113105108), primer tipo3 entrega68647.230399 (113256925). Por recepción el primer tipo3 llega68647.225863 (log20345): **197.055 ms** desde la última baja19; para cualquier tipo el hueco ronda0.17..0.18 s. Hasta68648.545025 llegan222 tipo3, el siguiente a68649.2138 en captura. El orden baja→alta y el relevo rápido se sostienen; especificar tipo y fase al publicar los0.17/1.5 s.

[P] «0 duplicados/0 restos»: no se observan repeticiones de clave viva en el lector de B ni solapamiento diagnosticado por U-f2 en esa llegada. Eso no prueba ausencia de dos actores distintos de un mismo nodo, de otra familia o en otra sesión. U-f2 compara data+lugar en su ámbito; un contadorYES0 no es una demostración universal. Probar identidad semántica de nodo/stage/familia y vida local en ambos clientes al llegar/salir, además de la clave en el cable.

### Rearmado rápido y conservación estructural

**[F] «salto de≤7 a≥10 en una muestra implica viaje rápido».** Es sólo un cambio de distancia; no distingue quién se movió, corrección de posición, lectura vieja/nueva, salto de campo, carga/reconexión ni una muestra omitida. `step():user2_stages_logic.h:220-245` hoy sólo conserva far_since/near_since y resetea ambos ante unknown, no guarda identidad y generación de las muestras. Una aceleración debe requerir evidencia de carga/viaje del anfitrión, mismo campo y época estables, muestras válidas consecutivas, invitado disponible y ausencia de preparación/drain; reevaluar antes de actuar y conservar ruta con histéresis para los demás casos. Si ambos viajan o el invitado no carga, no armar sobre coordenadas obsoletas.

[V] En el cuerpo normal de `0x287DBB0`, `param3` (r8b, payload+0xB) controla reencolado: `0x287dbb0.c:118-140`. Con estado2, campo válido y preparación inactiva, valor0 evalúa sin reencolar. [P] Esto no prueba que «encolar siempre» sea inocuo: si cambia el estado antes de despachar, estado1 descarga y la ruta de preparación delega otro temporizador; una cola vieja puede alcanzar otra generación. `arm_locked():1530-1559` revisa estado/listas/campo/distancia antes de llamar, y hoy repara una cadena periódica muerta. Mantener esa reparación: **un one-shot no sustituye la cadena periódica**.

Cambios obligatorios: one-shot separado de `queue_chain_2043` (que hoy siempre pone+0xB=1 y actualiza g_chain_last_us), uno pendiente por armado/época/sujeto, resultado de encolado comprobado, consumo/cancelación trazable y ejecución sólo en hilo servidor con el cerrojo existente. Distinguir tick puntual de cadena en watchdog y estadísticas (`timer_hit():801-849` actualiza g_last_mgr2_tick_us para ambos); no enmascarar una cadena muerta con puntuales. Medir además retrasos de cadenas viejas: el criterio «sin ticks1.5 periodos» puede confundir atraso con muerte, y crear otra cadena periódica que coincida después. Evitar temporizadores pendientes que sobrevivan a drain/logout/rearmado y aclarar la política cuando el EnqueueTimer falle. La promesa de hueco<2 s queda [NV] hasta medir estas rutas.

[P] No aprobar aún. Conservar stages user2 al llegar el anfitrión no es una optimización inocua. U-f2 no acredita las escenas, dependencias, visibilidad, misión y persistencia correctas con controlador invitado presente; tampoco define el traspaso si éste sale o se congela. Hace falta contrato explícito de propietario del mundo, un controlador por familia, qué gestor posee cada stage y transiciones completas. **El progreso y las misiones consultados y modificados siempre serán los del anfitrión; nunca usar como alternativa el estado de misión propio del invitado.** Mantener el drenado actual mientras se corrige lo demostrado es la alternativa más simple.


## Verificación binaria de puntos (build2976)

Ejecutable leído, no ejecutado ni escrito: `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`; SHA256 `57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`. `pe.py` leyó PE/pdata con struct, desensambló con Capstone y barrió bytes de secciones ejecutables para candidatos rel8/rel32. `pe-detalle.py` cotejó las firmas completas del código.

| RVA | Naturaleza comprobada | Parche actual / bytes |
|---|---|---|
|2A78CA0| [V] entrada pdata[2A78CA0,2A790B6), .rsrc |inline5:48 89 5C 24 10; kActionBytes completa coincide y es única |
|2A79060| [V] límite de instrucción dentro de la anterior, .rsrc|mid7:80 BD F0 00 00 00 00; kH1bBytes única |
|281CEE0| [V] entrada pdata[281CEE0,281D962), .rsrc|inline5:48 89 5C 24 08; kNodeBytes completa única |
|2792A7B| [V] límite en pdata[2791CD0,2792FC8), .rsrc|mid7:41 89 86 90 00 00 00; firma20 única; je2792A79 salta al final2792A82, no dentro |
|287EF60|[V] entrada pdata[287EF60,287F325), .rsrc|5 bytes48 89 5C 24 10 |
|287DBB0|[V] entrada pdata[287DBB0,287DFDB), .rsrc|5 bytes48 89 5C 24 18 |
|287D4C0|[V] entrada pdata[287D4C0,287DA4A), .rsrc|5 bytes48 89 5C 24 18 |
|289604E|[V] retorno de CALL287EF60 en2896049, dentro pdata[2895EF0,289611B), .rsrc|no proponer inline aquí; es referencia del llamador |
|287DA50|[V] thunk .rsrc de una instrucción E9 DB4FA40D→102C2A30 .xtls; no tiene entrada pdata|reutilización actual por mid de5; jamás enganchar el destino .xtls |

[P] No se ha demostrado globalmente «ningún salto cae dentro» para todo posible código generado/indirecto. El barrido conservador encuentra candidatos de bytes sin decodificar:2A78C35→2A78CA4,38F256F→2A79064,287D53E→287D4C4 y287DA88→287DA52. El tercero es **falso positivo demostrado**, byte70 dentro del MOV4C8B6C2470 en287D53A;2A78C35 también está dentro del desplazamiento de un E9 local. Los otros están fuera de funciones pdata reconocidas y no se deben llamar ramas reales sólo por sus bytes, pero tampoco descartar únicamente por no tener pdata. Antes de reinstalar/modificar hooks, documentar su bloque de origen/alcanzabilidad con Ghidra o CFG; [NV] garantía absoluta de ausencia de entradas indirectas. En los cuerpos funcionales decodificados no se observa entrada interior en los parches. Las firmas cortas de prólogo no son únicas; **las firmas completas** kActionBytes/kNodeBytes sí lo son. El diseño A4 puede reutilizar los hooks existentes y no necesita ganchos nuevos.

## Pruebas mínimas futuras (no ejecutadas en esta auditoría)

**Puras/replay, antes del juego:**

- Reproducir A/B/C con todos los nacimientos y sus censuras, no sólo éxitos; la corrección debe acreditar los843 eventos nativos válidos y eliminar las falsas retiradas por atribución. Separar ramas normales/dummy y los límites de2 líneas por actor. Casos de rechazo con retorno no exitoso, retorno externo0 sin evento interno, lectura fallida y token reutilizado.
- Simular8 s con recepción tardía, cola local acumulada, pérdida, silencio>2 s, carga, muerte válida, familia incompleta, hijos tardíos, familia mayor que capacidad, generación/clave reutilizada y Req en la ventana entre selección del plan y última revalidación. Ninguno consume presupuesto por falta de evidencia.
- Fallo de sonda/readiness después de una unidad pendiente: no volver de forma sorpresiva al aborto global de5 s. Probar las condiciones de abandono, recuperación y mensajes de diagnóstico.
- Máquina de distancia con secuencias7→10 por viaje del anfitrión, viaje del invitado, unknown entre muestras, cambio de campo, corrección de posición, drift de muestreo y cancelación durante drain. Puntos en7/8/9/10 y relojes monótonos.
- Temporizador puntual con cadena viva/muerta, encolado fallido, cola retardada por más de1.5 periodos, arm→drain→arm, disconnect/rejoin y manager reutilizado. Máximo un puntual pendiente por generación; sólo ticks periódicos cuentan como continuidad de cadena; no modificar estado de otra época.

**Un PC con cdmp_peer, después de aprobar implementación:**

- El peer no ejecuta IA real: sirve para hilo/colas/protocolo/estado, no para demostrar activación ni desplazamiento. Inyectar retrasos/rechazos y silencios controlados, observar que faltas de IA no se vuelven bajas destructivas injustificadas.
- Viajes repetidos del anfitrión y del peer: registrar hilo/época/subject, barreras, lista antes/después, puntual vs periódico, multiplicidad de cadenas y errores EnqueueTimer. Verificar cero llamadas nativas desde worker y ausencia de timer viejo que afecte un armado nuevo.
- Verificar bytes/build/pdata/CFG de todos los puntos conservados; no nuevas escrituras sin lectura protegida. Retirada familiar en hilo servidor con ModLock, barrera y revalidación completa; sin cerrojos invertidos con child_hold.

**Shadow, sólo tras corregir atribución y publicación/familias seguras:**

- Repetir ráfagas mayores que200, FPS bajo, red con latencia/pérdida y reconexión. Medir publicación→recepción→OnRecvPacket→primera Req→H1b por nacimiento; contabilizar fracasos/censura y colas, no sólo percentiles de éxitos. Ningún NPC previamente aplicado debe recibir retirada local por inactividad.
- Llegada/salida y viajes simultáneos: emparejar bajas/altas por stage+nodo+familia y por época en ambos clientes; medir hueco de recepción, de presencia y visible por separado. Cero doble controlador y cero duplicados semánticos, no sólo «clave repetida0».
- Familias con animales, monturas montadas/desmontadas, mascotas, compañeros, gimmicks, NPC de misión, combate, muerte y salida del invitado. Verificar acción/schedule/escena, salud del invitado y familia completa; ninguna publicación insegura con restore0 a un observador.
- Acciones del invitado deben contar únicamente en el mundo/misiones del anfitrión. Verificar una misión iniciada por anfitrión y acción compartida del invitado, progresos dispares entre sus partidas, salida/reentrada y recarga. Nunca resolver una falta del anfitrión consultando el progreso propio del invitado.

## Orden recomendado para A4

1. Corregir atribución y observabilidad, mantener contención destructiva deshabilitada hasta replay/puras y condiciones de seguridad completas.
2. Corregir publicación segura y familias según los dictámenes de esas áreas; no usar el timeout/restore0 como salida del bloqueo.
3. Especificar y probar contención conservadora con vida/actividad separadas, generaciones, cancelación y política de readiness;8 s provisional.
4. Introducir el puntual2043 como cambio separado y medible conservando la cadena periódica; después acelerar sólo el viaje del anfitrión acreditado.
5. Mantener el relevo por drain actual. Posponer conservar stagesuser2 al acercarse el anfitrión hasta un diseño explícito de autoridad/misiones/desconexión.

## Reproducción y límites de esta auditoría

Todos mis archivos están en `%TEMP%\codex-s34-auditoria-20261014\a4-contencion` (ruta estable, no $env:TEMP). Ejecutar cada script con `C:\Emulador CD\build\research\venv\Scripts\python.exe -I -B <ruta-absoluta-script>`.

- `recuento.py` → `recuentos.json`:843/843, percentiles,offsets,110/109,60/65 y censura; conserva líneas fuente.
- `capturas.py` → `capturas.json`: lector CDPK independiente, integridad, altas/bajas y offsets físicos B/C. Los intervalos declarados permiten comprobar qué cohorte se cuenta.
- `detalle.py` → salida conservada `detalle.txt`: censo por ventanas de logs, amplitud de las55 stages y fuente exacta. Depende de recuentos.json.
- `familias-tardios.py` → `familias-tardios.json`: cinco hijos C,20.065 s B,20 arranques normales A>5 s y dos familiares.
- `pe.py` → `pe.json`/salida `pe.txt`: encabezado,hash,pdata,desensamblado,barrido conservador. `pe-detalle.py` → `pe-detalle.json`: firmas completas y revisión de candidatos; depende de pe.json.

Fuentes primarias abreviadas: A host=`C:\Emulador CD\backups\p8-sesion34\cdmp_20261014-180308.log`; B host=`...\paso1b\cdmp_20261014-184959.log`, B invitado=`...\shadow-p1b-p2b\cdmp_20261014-175039.log`, B.bin=`...\shadow-p1b-p2b\cdmp_packets_20261014-175057.bin`; C host=`...\paso2b\cdmp_20261014-190232.log`, C invitado=`...\shadow-p1b-p2b\cdmp_20261014-180502.log`, C.bin=`...\shadow-p1b-p2b\cdmp_packets_20261014-180517.bin`. Código citado bajo snapshot `codigo-d835806\src\mod\session2\`; formato CDPK verificado en `src/common/capture_file.h` y lector de paquete `npc_probe_client.cpp:73-127`/`npc_probe_client_logic.h:24-65`. Decompilados bajo `D:\RE\CrimsonDesert_2976\exports\decomp\`.

No he leído informes de otras áreas ni sus veredictos; referencias a familias/publicación son condiciones de integración, no aprobación por tercero. No he auditado aquí la identificación del actor del volcado: la afirmación del informe4 sobreB0100578 queda [NV] en A4 y debe reemplazarse por la lectura primaria del auditor1, sin usarla como evidencia de contención. Tampoco hay evidencia visual suficiente para convertir presencia de paquetes en ciudad completamente visible.


Precisiones finales para consolidación: el intervalo21.402565 s usa **QPC del mismo invitado**: última baja19 `459.526183` en B invitado24868 y primera alta tipo3 `480.928748` en27271, sin offset entre PCs. La fase de entrega de esos eventos en B.bin va de`t_us459530135` (offset132743842) a`t_us480938895` (offset146811672):21.408760 s. Los483 nacimientos tipo3 pertenecen a[68722,68753.5] en reloj anfitrión alineado; la ventana[68724.422,68753.422] contiene321, diferencia162 de la ráfaga inicial. Censos de publicación usados en el emparejado: A268 líneas/268 claves únicas, B230/230, C397/397; no hubo colisión de clave en esa cohorte de2005 registrados. Esto no elimina la necesidad de manejar reutilización en el diseño.
