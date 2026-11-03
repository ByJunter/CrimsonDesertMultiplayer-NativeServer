# S33: NPC en bucle y viajeros quietos — causas, diseño y pruebas

Fecha: 14-10-2026. Investigación de capturas, logs, código del mod y ejecutable en disco. **No se ha implementado, compilado, desplegado ni ejecutado el juego.** No se han tocado partidas ni `bin64`. El código y el índice del repositorio permanecen sin cambios.

Juego **1.0.0.2976**, PE timestamp **0x6AB28F00**, base **0x140000000**. Todas las direcciones del juego son **RVA** y sólo valen para esta build. S33 paso 2 ejecutó el mod **883864EE** (H33.2, línea 1); el HEAD del repositorio, `c7ae132`, incluye trabajo posterior de S34. El diff entre ambos no cambia `user2_stages`, `child_hold` ni `docked_gimmicks`; no se atribuyen retrospectivamente a S33 los cambios de misión de S34.

Etiquetas: **[V]** observado en captura/log; **[E]** leído en código o inferido con fuerza; **[H]** hipótesis que requiere una medida adicional. Una propuesta de diseño no es una solución ya probada. Se distingue el cuerpo `.xtls` cuando interviene; ninguno se propone como punto de enganche.

## 1. Resultado y límites

| Problema | Resultado sustentado | Lo que aún falta demostrar |
|---|---|---|
| A: bucle masivo de NPC | **[V/E] Un solo pastor sin primera solicitud dispara el aborto global de U-e. Las altas posteriores vuelven al controlador anfitrión lejano; su IA solicita `logout`, se produce RemoveActor con motivo 42 y la escena repone el NPC con otra clave.** Las dos marcas caen después del aborto. | **[H] Por qué el pastor no inicia su IA.** Se comprueba una familia dividida entre controladores y entrega tardía de sus hijos, pero no el predicado interno que bloquea al padre. |
| B: viajeros quietos | **[V/E] Los viajeros del invitado siguen controlados por el anfitrión. Su cliente recibe las altas y emite IA/acciones sin desplazamiento; muchas de las mismas claves empiezan a transmitir desplazamiento cuando el anfitrión carga la zona. El patrón ya existe en S28.** | **[H] La condición exacta del cliente:** navegación, recursos de caminos, streaming, radio de simulación o dependencia de la posición del personaje. No se ha aislado una instrucción que pruebe cuál. |
| Métrica de U-e | **[E] «First action accepted» no acredita aceptación.** `0x296CAA0` devuelve cero también después de descartes y errores de la lógica interna. | Medir el resultado y la rama de `0x2A78CA0`, la acción efectiva y su restauración; no sustituir un contador ambiguo por otro. |
| Solución común | **[E, diseño] Autoridad coherente por unidad de spawn/familia, activación por fases y contención local; incorporar los viajeros mediante su procedencia 2550.** | Verificar familias y capacidad de simulación en Shadow antes de activar nuevas escrituras. El relevo de un actor vivo es una etapa separada. |

Fuentes: anexos A, B y estático del §2; `user2_stages_logic.h:710–726,762,891,933`; `user2_stages.cpp:2019–2045`; decompilados `0x296caa0.c`, `0x2a78ca0.c`, `0x2791cd0.c`.

**No basta ampliar cinco segundos.** Eso podría cambiar cuándo ocurre el aborto, pero no corrige su alcance global, el contador erróneo, la familia dividida ni el controlador de los viajeros. Tampoco se puede asegurar que el pastor nunca arrancaría con más tiempo: fue retirado aproximadamente seis segundos después de su alta.

## 2. Fuentes, método y reproducción

Los informes especializados y los resultados pequeños se conservan en [build/research/s33-npc-20261014](../../../build/research/s33-npc-20261014/README.md), fuera del código de producción:

- [A: capturas y cronología](../../../build/research/s33-npc-20261014/a/informe-a.md), con CSV por clave, `cohorts.py`, `family.py`, `detail.py` y lector `parse.py`.
- [B: viajeros](../../../build/research/s33-npc-20261014/b/informe-b.md), con cohortes JSON y scripts `index.py`, `phases.py`, `actionposes.py`.
- [Análisis estático](../../../build/research/s33-npc-20261014/estatico/informe.md), decompilados citados y verificación de firmas.
- Auditoría independiente de integridad: `root/audit_captures.py` y `audit_captures.json` en ese directorio.
- Las revisiones independientes y sus correcciones se incorporan al §10.

Los originales no se modificaron. El manifiesto del paquete identifica sus rutas y los hashes de los artefactos conservados. Los índices `.pkl` son derivados regenerables, no fuentes ni pruebas independientes; no se duplican los cientos de MB de índices.

### Convenciones de fuentes

| Alias | Log principal / captura |
|---|---|
| H33.1 | `C:\Emulador CD\backups\p8-sesion33-host\cdmp_20261014-130203.log` / `cdmp_packets_20261014-130215.bin` |
| H33.2 | misma carpeta, `cdmp_20261014-130936.log` / `cdmp_packets_20261014-130947.bin` |
| G33.1 | `scratchpad\s33-shadow\cdmp_20261014-120241.log` / `cdmp_packets_20261014-120257.bin` |
| G33.2 | misma carpeta, `cdmp_20261014-121104.log` / `cdmp_packets_20261014-121119.bin` |
| H31 / G31 | host `p8-sesion31-host\cdmp_20261014-080229.log`, captura `080240`; invitado `scratchpad\s31-shadow`, captura `070254` |
| H28 / G28 | host `p8-sesion28-host\cdmp_20261013-053042.log`, captura `053053`; invitado `scratchpad\s28-shadow-p1`, captura `043307` |

`scratchpad` designa exactamente:

```text
<scratch>
```

Se leyeron `CLAUDE.md`, el diseño de opción U, **§8 completo** de `u0-resultados-s28.md`, contexto de política de misiones, diario reciente y los documentos de familias, viajeros, observadores y cierre `0x1831598`. Consultas Ghidra mediante `D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1`; exports en `D:\RE\CrimsonDesert_2976\exports\decomp\0x<RVA>.c`. Python aislado con `-I -B`; se utilizó el intérprete del entorno de investigación porque el alias WindowsApps `py.exe` no arrancaba en el sandbox.

**[V] Integridad:** ocho capturas, cero truncados y cero mensajes 2042 pendientes de expansión. Los GAP del host S31/S33 están sólo al comienzo del login, antes de las ventanas estudiadas; las capturas del invitado no tienen GAP. En H33.2 hay 35 registros perdidos a t=12.388719–12.400395, frente a los NPC estudiados desde t≈214. Se ordenan eventos por timestamp porque distintos hilos pueden escribir fuera de orden. Fuente: auditoría root y anexo A §1.

**[E] Framing:** `src/common/capture_file.h` y **`src/mod/net/troc_sniffer.cpp`**: cabecera CDPKv2 de 12 B; registro de 24 B, `<QBBHHHII>`; Troc tiene cabecera de 5 B. Se revisaron lectores existentes antes de reutilizarlos. No se equiparan direcciones de memoria de ambos PC.

**[V] Alineación A:** 237 stages nuevas del usuario 2 emparejadas por clave y payload 2335 dan H−G entre 101.343563 y 101.414007 s, mediana **101.382933 s**. Es una alineación aproximada con dispersión de entrega/colas, no una medición exacta de latencia. Se excluyen reenvíos tardíos de stages del anfitrión. Las duraciones críticas de activación se calculan en un solo PC. Fuente: A, `audit.py`, `summary.py`, `cohorts.py`.

## 3. Fallo A: una excepción local se convierte en bucle global

### 3.1. Cronología y recuentos

**[V] El anfitrión es usuario `90100000`; Shadow, `901001FB`; su personaje, `A01001FC`.** La siguiente tabla usa segundos relativos a cada captura, no la diferencia de una hora de los relojes de pared.

| Evento | Captura host H | Captura invitado G | Fuente |
|---|---:|---:|---|
| Primera asignación U-e, `B0100357` | 214.768220 | ≈113.38 | H33.2 línea 4493 |
| Nacimiento de pastor `B01004C8` | 228.581937 | alta 127.189226 | H33.2 línea 8131; G2005 |
| Aborto global | **234.547727** | ≈133.16 | H33.2 línea **8406**, pared 13:13:58.471 |
| Remove del pastor, motivo **25 decimal** | 234.572903 | 133.164081 | 2516; cleanup línea 8421 |
| Marca Ctrl+F12 1 | ≈254.54 | **153.157** | G33.2 línea **4676**, 12:14:17.528 |
| Marca Ctrl+F12 2 | ≈264.21 | **162.830** | G33.2 línea **5386**, 12:14:27.201 |

Las marcas están unos 20 y 30 s después del aborto. No contienen una clave seleccionada: se demuestra el ciclo de NPC cercanos, pero no cuál miraba exactamente el usuario. Los prefijos de hora del trabajador pueden ser posteriores al evento; se usa `capture t` para ordenarlo.

**[V] Cohorte ligada a stages del invitado:** el vínculo 2005→stage exige recurso coincidente y clave de stage válida; no se usa un escaneo de u32 sin estructura. De 1733 claves, 1718 quedan además corroboradas por logs de IA/binding; las 15 restantes no tienen esa línea de sonda. Los casos decisivos sí tienen corroboración doble. Fuente: A, `parse.py`, CSV y auditoría de vínculos.

| Cohorte | Claves | Controlador y tipo | Bajas con motivo 42 |
|---|---:|---|---:|
| Observadas antes del aborto | 310 | 286 tipo 3 del invitado; 24 tipo 6 del host | 0 |
| Primera alta observada en host después del aborto | **1423** | 1375 tipo 3, 47 tipo 6, 1 tipo 4; todos del host | **773** |
| Total | 1733 | 286 del invitado; 1447 del host | 773 |

«Primera alta observada» no equivale siempre a nacimiento. Para los 286 asignados hay timestamp del gancho; para el resto se usa su primer 2005 en el controlador. Si se cuenta primera entrega al invitado después del aborto salen 1437: incluye 14 ovejas nacidas antes y entregadas tarde.

**[V] Las 773 bajas 42 están precedidas por una 2291 del anfitrión para la misma clave**, con grafo `A17A8F3E`, paquete `1256388E`, función `40BC1B1E` (`logout`, nomenclatura contrastada en S31), evento `513FC4FF`. Separación 0.156–17.120 ms, mediana 7.326 ms. Vida mediana desde la primera alta host: 2.685 s. Fuente: A, `detail.py` y eventos por clave.

**[V] En ±5 s de la primera marca hay 94 bajas 42; en ±5 s de la segunda, 81**, todas de esa cohorte con controlador host. Las ventanas se solapan 0.327 s y comparten cinco bajas: **la unión contiene 170 bajas distintas**, no 175. En ellas se entregan respectivamente 108 y 122 primeras altas al invitado, todas controladas por el host. Fuente adicional: revisión A, lector independiente `audit.py`.

Ejemplo de reposición comprobada, stage `20000165`, recurso `5FB728ED`, posición exacta `(-10671.873047,607.256226,-3918.589111)`:

| Clave | Alta G | Baja 42 G | Req del host durante su vida |
|---|---:|---:|---|
| `B01005F8` | 149.566395 | 151.866115 | 2734×7, 2291×5, 2223×4, 2405×1 |
| `B010061F` | 152.554159 | 154.597614 | mismos recuentos |
| `B010068B` | 159.782598 | 161.960892 | mismos recuentos |
| `B01006A8` | 162.157420 | 164.416267 | mismos recuentos |

Estaban aproximadamente a 34–36 m de la posición registrada del invitado; no se afirma que estuvieran en el encuadre de su cámara. Se observan nuevas claves en la misma stage y posición, no un retroceso de posición de una clave persistente. Fuente: A, `detail.txt`.

### 3.2. Por qué se desactiva U-e

**[E]** `user2_stages_logic.h:762,891,933` mide cinco segundos **desde nacimiento** y considera suficiente una clave viva sin marcador `Accepted`. `user2_stages.cpp:2019–2045` fija `g_ctl_aborted=true`; U-1 continúa, pero las nuevas creaciones dejan de recibir el controlador invitado. La limpieza posterior sólo alcanza los no marcados, en la siguiente petición del invitado (`:2833`). El Logout de `:1413` usa `0x19`, es decir **25 decimal**.

**[V]** De los 286 asignados, 285 emitieron 2223 y sólo `B01004C8` no emitió ninguna Req de IA/acción/movimiento estudiada. Ninguno de los 286 sufrió baja 42. Shadow emitió para esta cohorte 1811 mensajes 2556 y 1738 mensajes 2223; el host recibió 1785 relevos 2697 y 1448 relevos 2991. No es sólo un contador interno del mod. Fuente: A §3 y captura por clave.

**[E] Cadena causal del bucle marcado:** una excepción de activación → aborto global → nuevas altas bajo host lejano → `logout` de su IA → baja 42 → reposición. El último motivo interno por el que la IA lejana escoge logout permanece abierto; esta limitación no cambia la demostración del fallback defectuoso.

### 3.3. Pastor y ovejas: evidencia frente a hipótesis

**[V]** `B01004C8` es Herder, tipo 3, stage `20000174`, recurso `3F8972FE`, nodo 0, horario `cd_m11_m04_shepherd_city_00_0001_01.paschedule`. Nace en `(-10619.395508,600.082092,-3927.210205)` con controlador Shadow. Su alta anuncia 14 hijos `B01004C9`…`B01004D6`; todos son tipo 6 y conservan controlador host.

| Hecho observado | Fuente |
|---|---|
| El invitado recibe el padre en G127.189226; stage encontrada y handle IA VALID unos 14 ms después | G33.2 líneas 3152–3155 y 3180; 2005 |
| No emite ninguna Req antes de su retirada en G133.164081 | A `family.py`, eventos por clave |
| El host recibe las 14 ovejas en H228.582760–228.584198; nunca recibe el 2005 del padre | H2005; H33.2 líneas 8045–8100 |
| El invitado recibe las ovejas en G147.353827–147.354133 | G2005: 20.165 s después del padre y **14.190 s después de su baja** |
| La espera familiar y los huérfanos registrados afectan al destinatario usuario 2 | H33.2 líneas 8489–8502 y 10074–10077 |
| En paso 1, el mismo recurso y punto, padre `B0100B6A` controlado por host, emite primera Req a +229 ms | H33.1 alta 322.774760, Req 323.004119; A `family.txt` |

**[V] Está comprobada la familia partida y una limpieza que deja anuncios de hijos huérfanos. [H] No está comprobado que esperar a las ovejas sea la causa interna de la falta de IA del pastor.** El control comparativo descarta que esa plantilla sea siempre inerte, pero cambian más condiciones que sólo la autoridad.

**[E]** La selección U-e exige tipo 3, descriptor NPCSchedule `0x5B04CB0`, temporizadores 2043/2045/2046 y ausencia de otros dueños (`actor+90`, descriptor+40, actor+A0). Los tipo 6 quedan fuera. Los ocho «another descriptor» registrados antes del bloqueo eran tipo 7, vtable `0x5B04C68`. El contador `aborted=1507` se evalúa antes de otras condiciones: **no demuestra 1507 NPC elegibles**. Fuentes: `user2_stages_logic.h:710–726`, H33.2 resumen final y anexo A §6.

**[E]** `child_hold.cpp:548–648` evita la espera por restore al alcanzar la rama `ByControl`; hay condiciones previas de época, existencia, carga y propietario, por lo que no garantiza entrega inmediata universal al controlador. Puede retener a un observador con restore 0. La excepción host observador + controlador invitado + restore 0 mantiene la retención para evitar el cierre conocido. Por eso no es correcto afirmar que las ovejas no llegaron al host: sí llegaron, sin su padre. La ruta `prepare_family` desde `:1634` debe interpretarse junto al veredicto previo de retención.

La búsqueda estática no encontró una barrera familiar en los binders `0x9ED290`, `0x43B4B0` o `0x43CEF0`. El thunk `.rsrc` `0x17EA310` lleva a cuerpo `.xtls` `0xC867CC0`, que lee parentesco pero no demuestra por sí solo la espera. `0x9245B0` requiere cerebro `[[AI+48]+8]` para aplicar ChangeAIState. **Handle IA válido y stage encontrada no prueban que el cerebro esté ejecutando.** La sonda debe separar familia, cerebro y preparación del horario; no declarar culpable a uno antes de medirlo. Fuente: anexo estático §3.

### 3.4. Latencia y falso indicador de aceptación

**[V]** Tiempo entre primer 2005 recibido y primera 2223 emitida, ambos en Shadow, para los 285 que sí enviaron:

| n | mínimo | mediana | p90 | p95 | p99 | máximo | media |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 285 | 161 ms | 1700 ms | 2909 ms | 2982 ms | 3039 ms | 3040 ms | 1518 ms |

193 superan un segundo. El log host da media 1681 ms y máximo 3241 ms desde su propia medida de nacimiento/retorno; son otra duración. No hay evidencia de que los 285 excedieran cinco segundos en esta prueba, ni se extrapolan percentiles a peores redes o cargas. Fuente: A §5, `summary.py`.

**[E]** `child_hold.cpp:3743–3755` pasa el retorno del handler `0x296CAA0` a `user2_stages_start_action_done`; el cero marca `Accepted`. Sin embargo, Ghidra `0x296caa0.c:211` llama a `0x2A78CA0` y maneja sus errores; en `:276–277`, `LAB_14296CDAF` termina poniendo el resultado externo a cero también tras descartes. La lógica interna verifica sesión/controlador (`0x2a78ca0.c:40–43`), posición, capas y navegación; su rama especial `payload+281` tampoco equivale al broadcast normal. **Hay que clasificar la rama y observar acción aplicada/restaurable, no limitarse a cambiar de función cuyo cero se cuenta.**

## 4. Fallo B: el origen del spawn y el controlador son distintos

### 4.1. Identificación y comparación entre sesiones

**[V]** Los viajeros se identifican en 2005 por tipo 3, personaje solicitante e ID de carretera presentes en 2550. Controlador está en `record+0x12`; nombre UTF16 tiene longitud en +30 decimal e inicio +34; con `fin=34+2*longitud`, solicitante en `fin+88`, metadatos en `fin+92`, carretera en `fin+96`. Se cotejan además muestras por posición exacta y subíndice. No se atribuye todo NPC próximo a un 2550. Es una cohorte de tipo 3, no un censo de animales y todos los miembros de cada caravana. Fuente: B `cohorts.py`, `match.py`.

| Sesión | Solicitante | Viajeros | Altas recibidas por host | Controlador host | Req de IA/movimiento examinadas del invitado |
|---|---|---:|---:|---:|---:|
| S28 | `A0100336` | 64 | 64 | 64 | 0 |
| S31 | `A010006F` | 37 | 37 | 37 | 0 |
| S33 paso 1 | `A010018A` | 17 | 17 | 17 | 0 |
| S33 paso 2 | `A01001FC` | 41 | 41 | 41 | 0 |

**[V]** Se separa la fase con host lejos de los 30 s siguientes a su preparación local. Se consideran actores nacidos al menos 15 s antes de la carga; las posiciones se verifican tanto en 2223 como en 2556. El máximo X/Z es distancia respecto al nacimiento, no longitud recorrida. Se evita contar el ajuste de altura como caminar. La evidencia propia de este estudio es el desplazamiento transmitido, no una inspección de vídeo de la animación.

| Sesión | Carga / preparación host | Casos con posición inicial válida | ≤1 m X/Z antes de carga | De éstos, >1 m tras preparación |
|---|---|---:|---:|---:|
| S28 | 660.283430 / 666.322087 | 45 | 6 | 6 |
| S31 | 240.134220 / 247.891812 | 34 | 33 | 21 |
| S33 paso 1 | 269.235410 / 278.026647 | 11 | 11 | 11 |
| S33 paso 2 | 326.415483 / 334.208108 | 37 | **36** | **23** |

Fuentes: B `actionposes.py` y `phases.py`, revisión B `baseline.py`. El lector heredado de posición 2005 es heurístico y falló en dos registros S28. El revisor usó la primera posición real 2223/2556, comprobando que sus 12 bytes aparecen en los 2005 de ambos PC: recuperó los 45 casos S28 y reprodujo todos los recuentos restantes sin esa heurística. La lista de Req examinadas es 2223, 2734, 2556, 2405, 2291, 2828 y 2508; no se afirma clasificar todos los opcodes posibles. MoveData de 2223 está en payload+0x8E; referencias `mirror-ack-formats.md` §2, deserializadores `0x2442BF0`/`0x2444040`. Posiciones de 2556/2697: `TS::GetMoveData 0x21A1950`, suma sector×1000.

No todos los inmóviles siguen presentes al terminar la ventana: algunos se retiran por el recolector con motivo 25. No se interpreta su desaparición como éxito o fracaso de locomoción. La excepción S33.2 es `B01004B9`, un carro que alcanza unos 2.9 m antes de la carga. Fuente: B §Recuentos.

### 4.2. Caso completo: `B0100450`

**[V]** Wandering Freesword, carretera `3FD`, categoría/índice 1/1, solicitado por `A01001FC` en G121.107467. Punto pedido y anunciado: `(-10588.9590,599.5,-3903.7012)`. Alta H222.597384 / G121.412004, controlador **90100000**; **un único 2005 por PC**.

- Antes de la carga host H326.415483: 2734×92, 2291×60, 2223×29, 2405×18, **2556×0** desde el host. Las 29 acciones mantienen exactamente el X/Z inicial.
- La primera actividad de IA aparece a H222.761, unos **164 ms** tras su alta. No está esperando que `child_hold` lo entregue a su controlador.
- Host lejos: H33.2 línea 17953, t314.711307, posición host `(-6727,478,-3047)`, invitado `(-10636,611,-3865)`, 61 celdas.
- Carga host H326.415483 (línea 19078); host en Hernand a t329.733015, una celda del invitado (línea 19227); preparación H334.208108 (línea 19563).
- Primera 2556 a **H336.875790**, unos **114.278 s** tras el alta. En los 30 s siguientes a preparación: 2556×18, 2223×16; máximo X/Z combinado **39.117 m**. Shadow recibe los 18 relevos 2697 correspondientes durante la vida capturada, con las mismas posiciones y en el mismo orden (`revision-b/relay.json`).

Fuente: B, `cohort332.json`, `actionposes.txt`, `phases.txt`. El hecho de emitir una primera acción no demuestra locomoción: este NPC produce acciones durante casi dos minutos sin avanzar.

### 4.3. Qué cambia respecto a la interpretación de S28

**[V]** S31 `B01002B7` reproduce el patrón antes de U-e: alta H84.476849, 28 acciones sin cambio de X/Z y cero 2556 antes de carga H240.134220; tras preparación H247.891812 empieza a moverse sobre H250.64. Misma clave, un alta por PC.

**[V]** S28 contiene ambos comportamientos. `B0101582`, a unos 416 m del host, empieza a moverse a +1.6 s. En cambio, `B0101877`…`B010187A`, a unos 617–618 m, envían cada uno 23 acciones sin cambio de X/Z ni 2556 antes de volver el host. Nacen sobre H624.003; primeras 2556 sobre H681.60–681.66, tras carga H660.283430 y preparación H666.322087. `B01018DD`, a unos 705 m, permanece igualmente quieto hasta después de la carga. Los cinco conservan las claves y un único 2005 por PC. Fuentes: B §S28, H28 líneas 24588,24773,24910,25236,26290.

Por tanto, **aparecer o tener IA/animaciones a 940 m no demuestra caminar allí**. Se precisa la interpretación del apartado de viajeros S28, sin cambiar su registro histórico. No se deduce un radio universal de 600 m: tipo, carga previa, ruta y estado pueden influir.

### 4.4. Ruta nativa y módulos examinados

**[E]** `AutoSpawnRoadReq 2550`: handler `.rsrc 0x29C4930` → thunk `0x2C41E50` → cuerpo **`.xtls 0x10880460`** → `.rsrc 0x27E8C90` → `0x27EA160`. La cabecera deserializada es de **5 B**, las entradas de **0x18 B**. El solicitante se conserva en descriptor+0x3B8/+0x3C8; el constructor `0x278EA70` deja descriptor+0x40 vacío. El selector central `0x2792A11–0x2792A7B` prioriza controlador existente, descriptor+0x40, actor+0xA0 y finalmente **field+0x180**. El solicitante no es automáticamente controlador. Fuente: estático §4, exports de esos RVA.

**[E]** U-e no cubre 2550: exige los temporizadores y descriptor de horario. La retención nueva del host observador tampoco explica estos viajeros: el host es controlador, recibe su alta y emite IA. El paquete no contiene nueva traslación mientras está lejos; una interpolación del invitado no puede ser la causa primaria de esa ausencia en origen.

Revisión de rutas del invitado, sin afirmar que queden descartados todos sus posibles defectos:

| Módulo | Evidencia y consecuencia |
|---|---|
| `remote_motion` | **[E]** `remote_motion_logic.h:114,434` selecciona personajes A01 y modo remoto; no convierte por sí solo todo B en remoto. |
| `remote_interp` | **[E]** `remote_interp.cpp:239`, lógica `:755`: afecta a B con historial de actualización; no escribe controlador/IA. Una transferencia futura debe invalidar ese historial. |
| `combat_follow` | **[E]** `combat_follow.cpp:239–249,379–384` usa modo cliente +C8 y actividad de combate reciente; no actúa sobre todo NPC pacífico sólo por ser B. Debe auditarse otra vez antes de ampliar control a enemigos. |
| Salida del invitado | **[E]** `guest.cpp:1048–1061`, `mount_sync.cpp:2451`: filtro de montura remota; `docked_gimmicks_logic.h:90` tiene whitelist de mensajes de gimmicks, no un veto genérico de 2223/2734/2556/2405/2291. |
| Módulos posteriores, incluido `quest_policy` | **[V/E]** Su presencia no es necesaria para B, que ya aparece en S28. Esto descarta atribuirles la causa principal de esta cohorte; no prueba ausencia de efectos adicionales. |

**[E] Causa arquitectónica sustentada:** la autoridad del host se usa incluso para una zona que sólo simula correctamente el invitado cercano. **[H] Mecanismo interno pendiente:** la coincidencia con carga/acercamiento no separa navegación, streaming y posición del controlador. No se afirma «navmesh demostrada» ni se propone una guarda de distancia nueva basada en un valor no medido.

## 5. Diseño de solución: autoridad, activación y familias

Esta sección conserva el diseño de investigación. La autorización posterior y el alcance implementado se documentan en [s33-npc-implementacion.md](s33-npc-implementacion.md). El criterio es corregir contratos de control y ciclo de vida, no añadir excepciones al timeout.

### 5.1. Una autoridad por unidad de simulación

Separar la propiedad de escena/progreso de quién ejecuta la IA. **El mundo y las misiones siguen siendo del anfitrión.** La autoridad de IA se decide por procedencia, familia y disponibilidad del cliente. La clave B no identifica por sí sola un actor remoto; se requiere concordancia entre sesión autenticada, controlador anunciado y estado local del cliente.

La unidad debe incluir raíz y dependientes comprobados. Para horarios: stage, nodo, generación y familia. Para viajeros: solicitante, ID/categoría/índice de carretera, generación y acompañantes. Un hash de recurso aislado no identifica una instancia. No convertir indiscriminadamente todos los tipo 6: monturas, mascotas, animales, piezas y gimmicks tienen contratos diferentes. La atribución se mide primero en `probe`.

La decisión de nacimiento se aplica antes del 2005 mediante el **único** `create_mid` existente en `0x2792A7B`, coordinado con `docked_gimmicks` y U-e. No instalar otro hook superpuesto. No cambiar directamente `actor+0x90` de actores vivos en esta primera etapa.

### 5.2. Activación por fases, no «acción en cinco segundos»

Registro por `(época de conexión, clave completa de servidor, generación de asignación)`:

```text
reservado → creado → alta encolada → creado en cliente controlador
          → recursos/cerebro preparados → acción efectiva → observable
          → retirada completa
```

Las fases de recursos, cerebro y familia se registran separadamente: no todas tienen necesariamente orden lineal. Un guardia puede estar legítimamente inmóvil; no exigir 2556 para considerarlo vivo. Animales con restore 0 y acciones ya se han observado (`child_hold.cpp:623`), por lo que tampoco se impone restore 1 universal a todo tipo.

Registrar nacimiento, encolado, recepción/aplicación del 2005, presencia de miembros, cerebro `[[AI+48]+8]`, stage/playdata+0x318, stage+0x178, NPCSchedule+0x3D9, primera Req, emisor, validación y rama de `0x2A78CA0`, acción actual, restore, 2991 y publicación al observador. Todas las lecturas futuras: `game::fast_read`/SEH; no desreferenciar punteros recibidos por red. Cada extremo mide sus propias duraciones monotónicas.

La falta de progreso de una fase genera diagnóstico local. No impone un rechazo por un valor nunca medido, ni una devolución global al host a 4 km. Si es necesaria la retirada, debe abarcar la unidad completa, en hilo/scopes/cerrojos nativos correctos, cancelando altas/Acks retenidos de esa generación. La retirada del padre no puede dejar hijos publicados después.

Si una stage repone continuamente una unidad fallida, el diseño necesita contención por nodo/generación y reintentos acotados cuando cambie la condición causal. El punto exacto para esa contención debe medirse antes de activarlo: no se autoriza un filtro nuevo de escenas válidas por mera ausencia de acción.

### 5.3. Conservar la protección contra el cierre

**[E/V histórica]** Soltar al cliente host un NPC del invitado con restore 0 puede reproducir el cierre en `0x1831598`, asociado a acción actual nula de un padre. Fuentes: `u0-resultados-s28.md` §8.17 y documento de cierre citado allí; `child_hold.cpp:548–648`.

No romper una posible espera familiar liberando por timeout ese 2005, ni saltarse la lectura que acaba fallando. El controlador debe recibir una familia inicializable y el observador un estado nativo seguro, con el contrato específico del tipo ya medido. Una familia incoherente se diagnostica y aísla; no se publica para «ver si funciona».

### 5.4. Viajeros y relevo al acercarse

La política de autoridad puede ser común, pero **2550 tiene su propio ciclo de spawn**. Su control debe ser independiente del armado de stages y de su aborto. Inicialmente se valida la asignación al nacimiento, con anfitrión lejos y cliente invitado preparado, conservando las condiciones nativas ya medidas.

**[E] No hay transferencia de autoridad demostrada en los mensajes citados.** `2508 ChangeAiControlAck` sale del cheat `0x2C41360`/serializador `0x297B430`; exige el controlador actual y no lleva una nueva clave de usuario. Cliente `0xB681F0` actúa sólo bajo ese dueño. `2697/2991` propagan movimiento/acción. `NotifyDisappear 0x2B00380`/`NotifyAppear 0x2B000C0` son visibilidad, no una transacción completa de autoridad. Esto no demuestra que no exista ninguna otra ruta en todo el exe.

Primera etapa: conservar el relevo de stages mediante retirada/recreación que ya se ha medido, y corregir su coherencia familiar. La continuidad de un actor vivo queda separada. Requeriría detener admisión del emisor anterior, barrera de cola y generación, instantánea restaurable, reconstrucción de control en los clientes, confirmación del nuevo y reanudación. Cambiar +0x90 sólo rechaza peticiones del usuario anterior; no resuelve por sí solo comandos antiguos tras un viaje de ida y vuelta al mismo dueño.

El protocolo de épocas debe existir en el túnel/mod: no se presupone un campo libre nativo. Preservar HP, aggro, acción, relaciones y progreso; invalidar historiales de interpolación/seguimiento. No usar una espera fija de 200 ms como prueba de drenaje. Esta propuesta es **[H/diseño]** hasta demostrar las operaciones nativas y ensayarlas.

## 6. Opciones y plan de medida

Contrato propuesto durante la investigación; las tres primeras sondas y sólo la contención local de `npc_activation=1` se implementan después en [s33-npc-implementacion.md](s33-npc-implementacion.md). Familia y viajeros no admiten todavía modo activo. `0` conserva el comportamiento previo de ese cambio; `probe` sólo observa y calcula decisiones, sin nuevas escrituras ni llamadas mutantes; `1` activa únicamente la pieza ya medida. Mantener otras opciones constantes en cada ensayo.

| Cambio | Clave propuesta | Medida en `probe` | Criterio para activar `1` |
|---|---|---|---|
| Métricas y contención local | `npc_activation=0|probe|1` | Fases, resultado real, emisor, motivo de bloqueo, plan de limpieza | Una entidad lenta no desactiva el resto; retirada por generación sin huérfanos ni fallback global |
| Familia de horarios | `npc_family_control=0|probe|1` | Padre/hijos, tipos/descriptores, autoridad y orden de entrega; cerebro/horario | Unidad elegible bajo un controlador competente; inicialización completa y publicación segura |
| Viajeros del invitado | `user2_road_control=0|probe|1` | 2550→descriptor→familia→2005; solicitante frente a dueño; posiciones de Req/Ack | Movimiento sostenido con host lejos; misma trayectoria en observador; sin doble control |
| Transferencia en vida | `npc_authority_transfer=0|probe|1` | Candidatos, dependencias, comandos pendientes y preparación de ambos clientes | Protocolo demostrado, generaciones y barreras; pruebas de desconexión y retorno de dueño |

`probe` conserva también los defectos actuales si se deja U-e con el comportamiento de S33; no se presentará como reparación. No combinar todas las escrituras en una única prueba.

Para el pastor: observar antes/después de cada llegada de hijo. Si cerebro y horario están listos, pero sólo hay progreso con familia completa, se refuerza la hipótesis familiar; si falta cerebro, se investiga activación; si la stage sigue pendiente, carga/binding. La corrección no puede darse por terminada con «AI VALID».

Para viajeros: separar primera Req, aplicación de acción y locomoción. Medir desplazamiento X/Z en origen y observador; la ausencia de 2556 sola no basta, porque 2223 también lleva posición. Registrar altas, bajas, restauración y cambio de interés para no contar actores retirados como congelados.

**Aborto:** un fallo de lectura/identidad, actor fuera del ámbito medido, doble controlador, generación imposible o excepción detiene las nuevas escrituras de la pieza afectada y conserva el diagnóstico. Un hijo anunciado tras retirada de su raíz o una publicación host/restore 0 prohibida detienen la activación. Un plazo de preparación incumplido por una entidad se trata localmente; no cambia la autoridad de toda la población. No modificar progreso del anfitrión.

## 7. Puntos de enganche candidatos verificados

Sólo `.rsrc`. Entradas comprobadas con pdata; el punto intermedio es **`safetyhook::create_mid`**. Firmas exactas únicas en el archivo de esta build; son firmas de identificación, no el tamaño de parche que debe escribirse. Reutilizar hooks existentes siempre que sea posible.

| Uso | RVA / forma / primeras instrucciones | Firma AOB exacta, una coincidencia |
|---|---|---|
| Decisión de controlador, dueño actual `docked_gimmicks` | `0x2792A7B`, mid, store completo de 7 B | `41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5 48 8D 55 67` |
| Lógica real StartAction | `0x2A78CA0`, entrada, primer mov 5 B | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 50 FF FF FF 48 81 EC B0 01 00 00 4D 8B E9` |
| Procedencia de carretera | `0x27E8C90`, entrada opcional, primer mov 5 B | `4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D8 FD FE FF` |
| 2550, alternativa a la anterior | `0x29C4930`, entrada, primer mov 5 B | `48 89 5C 24 08 4C 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 10 F0 FF FF` |
| Handler ya instrumentado | `0x296CAA0`, entrada, primer mov 5 B | `48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 D0 FC FF FF` |
| Binding de horario, reutilizar sonda | `0x9ED290`, entrada, primer mov 5 B | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 81 EC 80 00 00 00 48 8B F2 4C 8B F1` |

Fuente: estático §7, `verify_hooks_unique.py`, `hooks_unique.json`, `hooks.json`. El mid pertenece a `[0x2791CD0,0x2792FC8)`; salto `0x2792A1A` llega al primer byte, salto `0x2792A79` sale a `0x2792A82`. El barrido de saltos directos de esa función no encuentra destino en el interior de los siete bytes. Las cinco entradas no tienen destinos intrafunción dentro de sus primeros cinco bytes.

**Límite de esta verificación:** no es un barrido completo de destinos indirectos y referencias externas ni prueba de ABI/relocación del trampolín en ejecución. Antes de implementar se completa esa revisión y la convivencia con los hooks H1 existentes; no se engancha un sitio nuevo sólo porque la AOB sea única. No se propone enganchar `.xtls`, ni nuevos puntos intermedios sin esta verificación.

## 8. Orden de trabajo y pruebas futuras

Ninguna prueba de esta sección se ejecutó durante la investigación. La implementación requiere autorización y una rama/worktree propio desde la base acordada; despliegue y juego necesitan su autorización específica. Antes de cualquier prueba que escriba memoria del juego se hace copia de partidas, según `CLAUDE.md` y el encargo.

1. **Sondas, antes de cambiar comportamiento.** Corregir la semántica de métricas; registrar activación familiar/cerebro/horario y origen 2550. No añadir guardas sobre valores todavía no medidos. Tests de análisis/estado y `scripts\build.ps1` tras implementar, sin despliegue automático.
2. **Tests puros de invariantes.** Una entidad sin acción no desactiva otras; entrega tardía no se confunde con IA fallida; cero externo con error interno no cuenta como aceptación; confirmación de época vieja no vale; retirada cancela hijos/Acks; reintentos acotados; tipo 6 con restore 0 no se considera roto automáticamente.
3. **Un PC con `cdmp_peer`.** Validar atribución de sesión/2550, familias, orden de anuncios, generaciones, limpieza, desconexión y protección de observador. El peer no ejecuta IA: cero acciones es esperado y no sirve para ajustar un timeout de Shadow ni demostrar locomoción. Los ensayos que carguen el juego siguen necesitando permiso.
4. **Shadow en `probe`: pastor y viajeros con host quieto lejos.** Capturas completas y marcas que incluyan clave o posición/cámara; seguir `3F8972FE` y sus 14 hijos. Medir el punto donde deja de progresar. Para viajeros, conservar configuración y comparar host lejos/acercándose, sin recrear voluntariamente las claves. La carga previa con posición controlada ayudaría a separar streaming y distancia.
5. **Activar contención local, aislada.** Un fallo reproducido debe dejar funcionando al resto de U-e y no entregar huérfanos ni provocar el fallback global. No se declara resuelto el pastor si sólo se ha ocultado su familia.
6. **Activar familia coherente tras su medida.** Pastor y 14 hijos, guardias individuales, animales, aparición/desaparición y relevo. Comprobar autoridad única, actividad real, recursos listos y publicación segura. Repetir más allá de los momentos de las antiguas marcas.
7. **Activar viajeros por separado.** Host a distancia durante al menos 60 s y después acercamiento; comparar mismos tipos/rutas y trayectorias de origen/observador. No basta una animación ni la primera 2223. Verificar caminantes, acompañantes y caravanas por su contrato, sin asumir que todos son tipo 3.
8. **Relevo de actores vivos, sólo después.** Primero demostrar protocolo; luego pérdida/reordenación, desconexión, combate y retorno al antiguo controlador. Confirmar que no se pierde HP/aggro/progreso ni se acepta una época caducada. Si no se demuestra continuidad segura, conservar retirada/recreación coherente como límite explícito.

Éxito global: desaparece el ciclo repetido de bajas 42/reposición en las cohortes cercanas asignadas; horarios y familias arrancan; viajeros se desplazan con el host lejos; observador sigue el estado autorizado; sin doble control, huérfanos, liberaciones prohibidas con restore 0 ni cambio del progreso del host. No exigir cero bajas 42 en todo el mundo: pueden existir bajas nativas legítimas fuera de la cohorte.

## 9. Qué no debe implementarse como atajo

- Aumentar sólo el plazo global o desactivar el watchdog sin sustituir su diagnóstico y limpieza.
- Interpretar retorno cero de `0x296CAA0` como aceptación, o primera acción como locomoción.
- Liberar restore 0 al host para romper la espera del pastor.
- Asignar todos los B o todos los tipo 6 al invitado sin procedencia/familia medidas.
- Cambiar +0x90 en caliente suponiendo que 2508, 2697 o 2991 transfieren autoridad.
- Atribuir B a U-e/retenciones nuevas ignorando los casos S28/S31, o afirmar que ya se ha demostrado un radio universal/navmesh.

Estos límites derivan de las capturas y rutas citadas, no de una preferencia de implementación. El arreglo definitivo sólo puede declararse después de observar el bloqueo interno restante y validar las nuevas autoridades en Shadow.

## 10. Revisión independiente

Tres revisores distintos de los autores contrastaron diez afirmaciones principales de cada área, con lectores independientes sobre los `.bin` originales y verificación del PE. No se limitaron a volver a ejecutar los scripts del autor.

| Revisión | Resultado y correcciones incorporadas |
|---|---|
| [A](../../../build/research/s33-npc-20261014/revision-a/revision.md) | Reproduce 286/285, 1733, 1423, 773, alineación y latencias. Corrige la unión de ventanas a **170**, y rechaza inferir del padre retirado que nunca podría arrancar más tarde. Mantiene la espera familiar como [H]. |
| [B](../../../build/research/s33-npc-20261014/revision-b/revision.md) | Reproduce las cuatro cohortes, **36/37 y 23/36**, con posiciones independientes del parser heurístico. Recupera 45 casos válidos S28; corrige primer movimiento a **H336.875790**; verifica 18 posiciones de Req/Ack idénticas. Mantiene desconocido el predicado de navegación/streaming. |
| [Estática](../../../build/research/s33-npc-20261014/revision-estatico/revision.md) | Confirma el aborto, el falso indicador de aceptación, el selector y seis firmas. Corrige cabecera 2550 a **5 B**, matiza `ByControl` y limita 2508 a reaplicar estado de IA del dueño existente, sin transferencia. |

Las correcciones se han integrado en este documento. Los informes de autores conservados en el paquete incluyen notas de revisión; los resultados intermedios se identifican como tales en su README. Quedan abiertas dos condiciones internas, expresamente señaladas: habilitación de la IA del pastor y dependencia local que impide trasladarse a los viajeros. No se ha ejecutado ningún experimento nuevo en el juego para cerrarlas.
