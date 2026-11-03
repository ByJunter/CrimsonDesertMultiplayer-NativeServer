# Auditoría independiente S34 — investigación 1, cierres

Estado: FINAL. Fecha 2026-10-14. Código contrastado contra `d835806`; el HEAD leído es `f79545c0f18f933ca0f9e185f60455b1bcc38501`. `git diff d835806 -- src/mod/session2/child_hold.cpp src/mod/session2/child_hold.h src/mod/session2/user2_stages.cpp src/mod/session2/user2_stages_logic.h` no devuelve diferencias. Después se leyó la instantánea `codigo-d835806` extraída con `git archive` por el coordinador. Sólo lectura; scripts nuevos y resultados en esta carpeta temporal. No se leyeron resultados de otros auditores.

## Dictamen

**APROBADA CON CAMBIOS.** Las pilas, las claves de actor y la exposición a restore 0 de los dos cierres son reproducibles. La explicación familia partida → falta de hijos → publicación insegura es sólida, pero algunas frases convierten inferencias en certezas universales. **No aprobaría implementar R2 solamente cambiando `Timeout` por `Keep`: existen otras rutas de publicación que no aplican esa regla, y una de ellas ya entregó un tipo 6 con restore 0 en el ensayo B.** R1 es una dirección razonable; no está demostrado que garantice la activación de todos los tipos, ni que una tabla limitada al Troc capture las piezas que nacen posteriormente.

Cambios obligatorios de precisión: distinguir reproducción de acción en el primer cierre; distinguir ocho Timeout de ocho entregas; retirar la afirmación universal de que no existe un punto común razonable; explicitar falta de observación de la destrucción concreta en el anfitrión; mantener R2 como barrera de seguridad con diagnóstico y recuperación acotada, no como solución funcional completa.

## Evidencia ya reproducida

`audit_dumps.py` es un lector independiente de MINIDUMP y PE, escrito para esta auditoría. Desenrolla con UNWIND_INFO y `.pdata`, incluidos chained unwind, PUSH_NONVOL/ALLOC/SET_FPREG/SAVE_NONVOL; no hace stack scan. Detiene la pila al entrar en otro módulo cuyo PE no leyó. Los registros volátiles de marcos superiores no se reconstruyen y no deben interpretarse como valores del llamador. No se ejecutaron los desenrolladores del autor.

- [V] Cierre A: excepción de lectura `0x38` en RVA `0x88F832`, hilo 9020. Pila reproducida: `88F832 → 88EFA7 → 860EBC → 8B5773 → 13FB680 → 1414EBF → ucrtbase`. RBX recuperado por unwind en el marco 2 identifica actor `B01004A5`, controlador `90100000`, padre `B01004A4`. Offsets del DMP: clave `0x7BFA0`, controlador `0x7BFD0`, padre `0x7BFEC`.
- [V] Cierre C: excepción de lectura `0x10B` en RVA `0x1831598`, hilo 5104. Pila reproducida entera hasta KERNEL32: `1831598 → 439B85 → 15ECF73 → 99A3454 → 8F7990 → 8DA224 → 4FE095 → 98CB108 → 140F1C8 → 141145D → 13F885D → 13F50E1 → 140496F → 6173E9 → 3F412E6 → 14C8803A → 3F44B28 → 86D2ADC → 4895C3E`.
- [V] El actor cuya capa se desreferencia en C es `B01004BF`, no `B0100578`: R14 actor, RDX CC, CC+8 vuelve a R14; clave en DMP `0x73652`. La capa tiene acción y reproducción cero (`0x1668B7`, `0x1668BF`) y chart no nulo (`0x1669B7`). El componente RSI es jerarquía vtable RVA `0x5578BB8`, dueño `B0100578`, padre jerárquico `B01004BF` (`0x112083`). Restore cero (`0x75AC2`).
- [V] Parser independiente de seis capturas: familias partidas observadas en Shadow A=2 tipo6+1 tipo7; B=1 tipo6+1 tipo7; C=4 tipo6+6 tipo7. Los hijos tipo6 de A llegan 10,0845/10,0847 s después de su padre, los de C 10,0492/10,0887 s, el de B 20,0683 s. El JSON registra offsets absolutos, número de registro, controlador, padre, hijos y restore. No hay truncamientos de registros ni huecos en estas tres capturas del invitado. Sí hay 18 huecos de un registro en A-host y 30 en C-host, al inicio; hay que preservar esta limitación al hablar de exhaustividad.
- [V] Contadores del anfitrión de Timeout restore0: A 0→2 (líneas 177/14048), B se queda en 0, C 0→1→3→5→6 (líneas 177/13010/15983/17064/17544). Total=8 eventos del contador, no ocho entregas confirmadas a Shadow: el sexto C se cuenta después del cierre.
- [V] En capturas Shadow hay ocho altas con controlador anfitrión y restore0: A=2, B=1, C=5. La de B (`B01003AE`) NO es un Timeout de NPC y debe contarse como otra vía de exposición.

Datos completos: `dumps-independientes.json`, `capturas-independientes.json`, `estatico-independiente.json`.

## Fuentes y convenciones

- Informe auditado: `C:\Emulador CD-npc-s33-fix\docs\research\s25\s34-investigaciones\1-cierres-invitado.md`, llamado **I1**.
- **AH**: `C:\Emulador CD\backups\p8-sesion34\cdmp_20261014-180308.log`; **AG**: `shadow\cdmp_20261014-170438.log` bajo esa carpeta.
- **BH**: `paso1b\cdmp_20261014-184959.log`; **BG**: `shadow-p1b-p2b\cdmp_20261014-175039.log`.
- **CH**: `paso2b\cdmp_20261014-190232.log`; **CG**: `shadow-p1b-p2b\cdmp_20261014-180502.log`.
- Capturas correspondientes: AH `180319`, AG `170455`, BH `185010`, BG `175057`, CH `190242`, CG `180517`.
- Código citado por línea: instantánea exacta `d835806`, no las investigaciones del commit posterior.
- Los offsets BIN son absolutos dentro del archivo. En el JSON, `t` es el timestamp crudo dividido por 1e6, NO un reloj común entre PC. Para tiempo relativo, restar `first_packet_t_us/1e6`. Las latencias aquí usan el mismo reloj de captura y el mismo PC. Los logs incluyen qpc, capture t y hora estimada; la hora de emisión diferida de una línea no es el tiempo del suceso.
- [V] evidencia reproducida; [P] conclusión parcialmente demostrada; [F] afirmación falsa o mal interpretada; [NV] no demostrable con estos datos.

## Tabla de afirmaciones

| Afirmación de I1 | Estado | Comprobación independiente y límite |
|---|---|---|
| Pila A y actor `B01004A5`, control anfitrión, padre `B01004A4` (§a.1) | [V] | `audit_dumps.py`; DMP A excepción 0x38, seis marcos del juego recuperados por metadatos. Actor en RBX restaurado del marco 2; CC por `[RSI+0x60]`, vtable RVA `55B5460`, CC+8 vuelve al mismo actor. No se tomó la dirección de memoria publicada por el autor como identidad entre PC. |
| En A, `[ctx+0x10]` contiene acciones actuales (§a.1, tabla) | [F] | El pseudocódigo `0x88f4e0.c`, líneas aproximadas 187–224, copia **layer+0x18**, reproducción/playback, al vector de `param_3+0x10`. `0x88F82B/82F/832` lee ese vector y luego playback+0x38. DMP: vector `0x36328B8D940`, contenido cero. La clase de estado incompleto sigue siendo correcta, pero el puntero que causa esta AV es reproducción, no acción. |
| Cierre C: actor `B01004BF`; `B0100578` sólo provoca la consulta jerárquica (§a.2) | [V] | DMP C: CC en RDX, actor en R14; CC+8, vtable, capa y puntero restore consistentes. RSI es componente con vtable `145578BB8` y jerarquía+1C=`B01004BF`. Su dueño tiene clave `B0100578`. Pila `1831598 ← 439B85 ← 15ECF73` confirma lectura desde otro actor. |
| La sonda GuestS3 interpreta erróneamente RSI como CC (§a.2) | [V] | CG:7055–7058 más valores DMP anteriores. No usar aquella atribución para diagnosticar `B0100578` como actor sin chart. |
| La guarda de Update/PostUpdate no protege esta consulta de otro actor (§a.2) | [V] | CG:6959 dice que retiene `B01004BF`; C llega a `1831598` por `439AB0` y `15ECA10`, sin esos dos marcos. Se confirma la limitación concreta; no se prueba que todas las demás guardas sean inútiles. |
| U-e sólo cambia tipo 3 y deja tipo 6 con controlador anfitrión (§b.1) | [V] | `user2_stages_logic.h:709–725`, `user2_stages.cpp:1136–1230`; capturas partidas con padre `90100249` y subnodos `90100000` en C. Los filtros de propietario y descriptor siguen siendo relevantes; no significa que todo tipo 6 deba reasignarse. |
| El anfitrión recibió los hijos antes del padre (§b.2) | [V] | Parser independiente: AH huérfanos `B01003CD`, `B010049E`, `B01004A5`; CH `B010048F`, `B0100490`, `B01004BF`, `B01004C0`, `B0100577`; BH `B01003AE`. Para BF/C0, su padre aparece 10,3766/10,3764 s después. |
| El anfitrión destruyó exactamente esos hijos en `0x8B7D78` y nunca podrán actuar (§b.2–b.3) | [P] | `0x8B7D78` **lee** actor+AC; no es la destrucción misma. `0x8B7B50` encola los que no tienen padre y más abajo aplica `eErrNoParentsNotExist` y vf+0x110 (decompilado:203–301, 455–503). CH:13898–13899 muestra ausente **o lectura fallida**, y CH:17401/17403 primera Req=0 hasta baja. Falta una sonda de esa rama por clave para llamar [V] al evento de destrucción exacto. No se puede demostrar «nunca» más allá de esa vida/captura. |
| El padre espera a los hijos y empieza después de su entrega (§b.2) | [V]/[P] | [V] cronología y transición de estado: CG:5786 padre `5E=8`, máscara0/2; CG:6940 padre `5E=16`, máscara3/2 tras hijos; CG:6951 primera Req. [P] que la dependencia familiar sea la única condición de activación; las muestras son no atómicas y no registran todas las condiciones internas. |
| Primera Req a 10,24–10,31 s y 160–217 ms después de hijos, cuatro casos (§b.2) | [P] | Por 2223 en BIN se reproducen: A `B01004A4`: 10,253326 s /168,873 ms; A `B010049D`: 10,230697 s /145,987 ms; C `B010048E`: 10,239637 s /190,414 ms; C `B01004BE`:10,304009 s /215,294 ms. El rango publicado no coincide exactamente; debe especificar si usa 2734, 2223, encolado, entrega o aceptación. |
| `8646D0` con restore0 retorna sin restaurar (§a.1) | [V] | Decompilado `0x8646d0.c:73–101`; lee `[CC+40]+30`, escribe resultado0, suelta lock y sale. Es entrada `.rsrc`, pdata `[8646D0,864B47)`. |
| Restore1 garantiza una acción válida y un observador nativo nunca ve restore0 (§a.1) | [NV] como universal | Se confirma la rama restore0, no un contrato exhaustivo de éxito para restore1 ni la totalidad de escenarios nativos. Hay código de restauración con más pasos después; no se auditó aquí toda su posible salida. El guardado del byte es evidencia de un estado del servidor, no comprobación atómica de todas las capas del cliente. |
| Cinco lectores y 10 llamadores directos de `1831560` (§c.iii) | [V] | `audit_static.py` confirma instrucciones y `.rsrc` de los cinco sitios; reproduce los 10 CALL E8 hacia `1831560` y valida alineación en pdata. Hay lectores de acción y de reproducción, no un único campo. |
| Recuento 395 `.rsrc` +22 `.xtls` (§c.iii) | [V] como heurística | Expresión de bytes propia reproduce exactamente 395/22. No tipa el registro base: no demuestra que cada coincidencia sea CC ni que todas sean lectores peligrosos. No justifica por sí sola una prueba de dominancia. |
| «No existe ningún punto común razonable» / «el único es 8646D0» (§c.iii) | [NV] | No hay análisis completo del grafo, ni de publicación/carga/planificación. Sí está refutado que una guarda sólo en Update/PostUpdate o sólo `1831560` cubra ambos caminos. La publicación a un observador es una frontera candidata, aunque también requiere cubrir varias rutas y distinguir actores sin CC. No hay prueba para el superlativo «único». |
| Ocho liberaciones Timeout con restore0 (§d) | [V] contador; [P] identidad de las ocho | AH:14048=2, BH=0, CH:17544=6. Siete de esas altas están en BIN Shadow antes de los cierres; el sexto C ocurre después. La identidad `B0100577` del sexto C es una inferencia temporal, no una entrega confirmada al invitado. |
| «8 con riesgo, 7 clase A y1 claseB;2deellas cerraron» (§d) | [P] | Distinguir eventos servidor, paquetes entregados y atribución causal. En BIN Shadow hay 8 restore0 no controlados: A2+B1+C5. B `B01003AE` llegó por familia, no por TimeoutNPC. Esa exposición adicional no debe desaparecer del análisis. Los cierres están asociados a A`B01004A5`/C`B01004BF`; no existe ensayo controlado de eliminación de causa. |
| R1+R2 eliminan esta clase de cierre (§c.iv) | [P], no garantía | Evitan el mecanismo de Timeout conocido **si** la regla de admisión se impone a todas las rutas pertinentes y si la familia puede progresar. El diseño literal de R2 no cubre las otras liberaciones nativas. R1 no demuestra aún el flujo de tipo6 bajo autoridad invitado ni las piezas tardías de tipo7. |

## Recuentos y cronología: puntos que importan para la consolidación

### Familias partidas entregadas al invitado

| Ensayo | Tipo6 con padre de usuario2 y controlador anfitrión | Tipo7 con la misma divergencia por vínculo serializado | Fuente BIN |
|---|---|---|---|
| A | `B01004A5→B01004A4`, `B010049E→B010049D` | `B01003EB→B01003EA` | `capturas-independientes.json` A-guest.split |
| B | `B01003AE→B01003AD` | `B010032A→B0100329` | B-guest.split |
| C | `B010048F/90→B010048E`, `B01004BF/C0→B01004BE` | `B010042A`, `B0100471`, `B0100475`, `B010049F`, `B01004C6`, `B0100573` | C-guest.split |

Esto **no incluye** el vínculo jerárquico de `B0100578→B01004BF`, porque su cola de familia serializada tiene padre0 y el vínculo se comprueba en memoria del DMP. Es un ejemplo real de que «familia» no puede reducirse al campo +AC o a una sola cola del 2005.

### Ocho altas restore0 confirmadas en Shadow

| Ensayo | Clave | Offset registro / byte restore | Vía atribuida |
|---|---|---|---|
| A | `B01004A5` | `0x1A22A00` / `0x1A23C53` | TimeoutNPC; actor del cierre A |
| A | `B010049E` | `0x1A24405` / `0x1A2564C` | TimeoutNPC |
| B | `B01003AE` | `0x206DD17` / `0x206EF6A` | Liberación familiar después de20,0683 s; contador TimeoutNPC=0 |
| C | `B01001F6` | `0x1779E1C` / `0x177B07D` | TimeoutNPC; tipo3, cría de ciervo por rutaapp_xml; no toda exposición problemática es tipo6 |
| C | `B0100490` | `0x1EF0E1B` / `0x1EF2063` | TimeoutNPC |
| C | `B010048F` | `0x1EF2A45` / `0x1EF3C84` | TimeoutNPC |
| C | `B01004BF` | `0x20480CD` / `0x204930C` | TimeoutNPC; actor cuya capa causa cierre C |
| C | `B01004C0` | `0x2049ABE` / `0x204AD06` | TimeoutNPC |

El parser reconoce restore por la ruta `character/...app_xml` con longitud serializada válida y byte siguiente tras u64. Es independiente del script del investigador, pero sigue siendo un reconocimiento estructural, no un deserializador completo de cada variante. Todos estos ocho tienen un único candidato y la memoria/logs corroboran los actores de los dos cierres. La cola familiar se enumera hasta el máximo permitido por longitud, exige claves plausibles y un único candidato; los JSON registran ambigüedades.

### La instrucción exacta del cierre A

```
88F82B  49 8B 46 10  mov rax,[r14+10]
88F82F  48 8B 08     mov rcx,[rax]
88F832  48 8B 41 38  mov rax,[rcx+38]   ; RCX=0, AV en38
```

En A, la capa original no está incluida en el minidump: no afirmar que el DMP permite leer directamente sus tres campos. Sí incluye el vector de reproducción nulo y permite enlazar MovementController→CC→actor. La acción/chart de esa capa se corroboran por AG:5781–5783 y el análisis de carga, no por memoria ausente.

## Auditoría de R1, R2 y R3

### R1: herencia familiar al nacer

**Aprobación condicionada a medición.** Es más coherente que fabricar una acción o instalar guardas independientes en todos los lectores. Pero el informe debe cambiar «es la raíz, funciona a0,2s» por «candidato de raíz, falta validar tipo6 y unidad completa».

Requisitos previos:

1. Identificar descriptor interno, enlace al padre, clave y generación de nacimiento en la misma operación; registrar ambas claves y controlador previo/nuevo. No convertir la regla en «todo tipo6» ni «todo descendiente de cualquier usuario». Proteger cada lectura, incluidos punteros intermedios.
2. Tabla por hilo **y ámbito anidado del Troc**, con limpieza al salir, token de nacimiento y rechazo local diagnosticado si está llena/incompleta. Un puntero local a descriptor es válido como relación efímera, no como identidad durable ni entre PC. No asumir que una llamada reentrante comparte la misma vida de descriptor.
3. Tratar por separado hijos creados después, piezas jerárquicas con +AC0 y tipos sin CharacterControl. C`B0100578` prueba ese caso real. La estrategia debe conservar la dependencia de publicación aunque su controlador sea anfitrión.
4. Probar ausencia de un segundo emisor de IA para un mismo nacimiento; el controlador en2005 no basta como prueba de quién envía. En capturas, varias piezas tipo7 con controlador anfitrión originan2556 en Shadow, p.ej. A`B01003EB`, C`B01004C6`/`B0100573`; puede ser movimiento ligado y no IA. No reasignarlas automáticamente usando el mismo criterio que los animales.
5. No reutilizar la autoridad de U-e para avanzar misiones del invitado. Cambiar controlador es sólo una decisión de simulación: toda acción con efectos de misión sigue requiriendo atribución a la partida del anfitrión. El informe no demuestra ese contrato para animales, compañeros o NPC de misión; quedan fuera de una activación amplia sin pruebas.

Gancho existente, validación independiente para build2976:

- RVA `0x2792A7B`, `.rsrc`, dentro de pdata `[0x2791CD0,0x2792FC8)`, **no entrada**.
- `create_mid`,7bytes: `41 89 86 90 00 00 00` (`mov [r14+90],eax`).
- Firma exacta de24bytes, una sola coincidencia en el archivo: `41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5 48 8D 55 67 48 8B CE FF`.
- No hay salto directo intrafunción al interior de esos7bytes; el salto de`2792A79` va justo después, a`2792A82`. Se reutiliza el dueño del gancho `docked_gimmicks.cpp:586–619`, no otro parche solapado. No se afirma aquí ausencia absoluta de destinos indirectos externos no representados en pdata.

### R2: nunca publicar restore0 a un observador que no controla

**Obligatoria como principio; insuficiente en los puntos propuestos.** El cambio literal `npc_release_step:637 Timeout→Keep` no es la invariante anunciada:

- `child_hold.cpp:590`: tras lecturas indeterminadas, `Release/Native` si sigue en rango.
- `:595`: otra instancia detrás de la misma clave, `Release/Native`.
- `:606`: propietario cambiado y child_hold apagado, `Release/Native`.
- `family_release_step:485–514`: libera por MaxAge, Orphan/AfterParent/OrphanTimeout sin comprobar restore/controlador; B`B01003AE` demuestra una exposición fuera de TimeoutNPC.
- La política de tabla llena y lecturas fallidas deja rutas nativas; cambiar sólo un enum no elimina sus altas.
- `character_hold` mantiene su propio Timeout/Native (`:697–716`), importante para monturas y personajes aunque los dos cierres actuales sean claves B.

Antes de programarlo, enumerar todos los puntos que deciden publicar2005 y formular la admisión según **capacidad del actor**: actor con CC remoto necesita estado inicial apto; una pieza sin CC no tiene byte restore y necesita que sus dependencias jerárquicas estén publicadas y aptas. No basta buscar un byte en cualquier tipo ni retener todas las piezas7 para siempre.

Qué quedaría invisible/bloqueado:

- Hijos partidos que su controlador ya perdió; sus padres esperan el grupo.
- NPC cuya primera acción no es restaurable, caso documentado en el propio código de ringneck y en C`B01001F6`.
- Familia cuyo vínculo no se puede resolver, tabla llena, desconexión, padre retirado o clave reutilizada.
- Piezas que consultan un padre ya presente pero aún no apto: presencia no equivale a seguridad de lectura.

Mantenerlos invisibles puede ser una degradación temporal más segura que entregar un estado que cerró el juego, **no es un resultado funcional aceptable indefinido**. Necesita contadores por causa, máximo de espera observado, diagnóstico de unidad y una recuperación definida: reanuncio correcto cuando el controlador haya producido estado utilizable, retirada nativa local acotada cuando esté autorizada y sea seguro retirar esa unidad, o fallo explícito de la función. No convertir un timeout en una liberación insegura por falta de capacidad. No retirar sin más NPC de misión del mundo del anfitrión.

La excepción opcional «hay2723/2991 retenido, entonces se puede publicar restore0» sigue [NV]: estar en la cola no demuestra que el cliente lo aplique antes de un lector. Debe preservarse mismo nacimiento, orden2005→Ack, capacidad de la cola, aceptación y momento de actualización. No aprobar esa excepción únicamente por coincidencia de clave o presencia de un paquete.

### R3: contención y guardas

Mantener las guardas como defensa en profundidad es razonable, pero no afirmar «sin cierre en ningún lado»: el cierre C ya pasó por un lector ajeno al actor retenido. Una guarda `create_mid` en`1831594` cubre ese lector, no el de`88F832` ni otros.

Validación de sitio de defensa, **no propuesta de implementación autorizada**: `.rsrc`, pdata `[1831560,18315F3)`, instrucciones `48 8B 48 10` + `0F B6 91 0B 01 00 00`,11bytes; firma de24bytes única en archivo; sin destino directo intrafunción dentro del bloque. Además de los bytes, cualquier diseño tendría que especificar semántica de salida y registros/flags, sin fabricar un éxito para todos los llamadores.

R3 depende de que la contención mida progreso correcto, capture toda la familia y no destruya una unidad sana. La investigación1 no demuestra esa condición; debe cruzarse con investigación4 antes de aprobar implementación. En `d835806`, `user2_stages.cpp:2508–2515` puede volver al aborto global si la contención no está lista; por tanto, no presentar la contención como garantía incondicional.

## Pruebas mínimas recomendadas

**Puras, sin juego:** reglas de admisión por capacidad, controlador y generación; todos los motivos Native/Timeout/familia; padre e hijo en un paquete mezclado; profundidad>1; padre+AC0 con relación jerárquica; familia incompleta; tabla llena; lectura indeterminada; viaje/disconnect; clave reutilizada con Ack viejo; interrupción entre prevalidación y publicación; no retirar otros actores del mismo2005. Comprobar que ningún camino de degradación transforma «no sé si es seguro» en publicación restore0 remota.

**Un PC con cdmp_peer:** validar contabilidad, límites, orden y limpieza de colas; padres que nunca emiten, familia que no llega y duplicados/reutilización; comprobar que el host no recibe un hijo antes de su padre y que retiradas locales no se vuelven globales. Este entorno no puede demostrar que Shadow inicia IA de animales: el peer no tiene cliente simulando NPC.

**Shadow, primero probe:** repetir familias de perros/cabras y cría de ciervo, incluyendo tipo7 tardío; registrar para cada nacimiento descriptor raíz, relación nativa, controlador,2005 a ambos, restore, presencia, estado de login, primera Req por opcode y aplicación. Medir ruta de destrucción de huérfano por clave si sigue siendo dudosa. Éxito: familia completa y activa en controlador; observador recibe estado utilizable; cero publicaciones inseguras y cero huérfanos del controlador. Probar anfitrión acercándose/alejándose, viaje rápido, desconexión durante espera, regreso y claves reutilizadas.

**Después activar por cohorte medida:** tipo6 descendiente de raíz U-e demostrada. Mantener los tipos7 y demás clases por su política de dependencia; no extender a monturas, mascotas de usuario, compañeros ni NPC de misión sin validación específica. Ausencia de crash en una sesión no demuestra eliminación de todos los lectores; conservar contadores y dumps.

Orden local recomendado: corregir medida/diagnóstico de progreso; cerrar todas las rutas de publicación insegura y sus salidas acotadas; sonda de herencia familiar con generaciones; activar por cohorte validada; probar transiciones y después ampliar tipos. El orden global debe consolidarse con las otras cuatro auditorías.

## Reproducción

Desde cualquier directorio, Python `C:\Emulador CD\build\research\venv\Scripts\python.exe`, siempre `-I -B`:

1. `audit_dumps.py`: crea`dumps-independientes.json`; excepción, registros, pila por pdata y lecturas con offsets de ambos minidumps. Sin importar código del investigador.
2. `audit_captures.py`: crea`capturas-independientes.json`; parsea seis BIN, enumera altas/familias/restore/Req, cuenta huérfanos y cambios de contadores en logs. Formato contrastado con`src/common/capture_file.h` de`d835806`. No ejecuta el juego.
3. `audit_static.py`: crea`estatico-independiente.json`; PE timestamp/base/secciones/pdata, instrucciones con Capstone, firmas y sitios CALL, heurística395/22. Usa el lector PE propio de`audit_dumps.py` por ruta explícita; no módulos del autor.

Consulta Ghidra utilizada para una función no exportada previamente: `powershell -NoProfile -ExecutionPolicy Bypass -File D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1 decomp 0x1408B7D78`. El wrapper ejecuta`-noanalysis -readOnly`; produjo`exports/decomp/0x8b7b50.c`. Los otros decompilados consultados ya existían (`88f4e0`, `1831560`, `8646d0`). Se leyeron los bytes del exe en disco; no se ejecutó ni escribió.

Limitaciones explícitas: no deserialización completa de todos los tipos; no ejecución real; no prueba de orden causal de todas las escrituras concurrentes; no dominancia global sobre código virtualizado; se detiene unwind al salir del PE del juego; no validación aquí de los diseños completos de familia/viajeros/guardado de otros informes. Nada de esto invalida las dos pilas recuperadas ni la identificación`B01004BF`.
