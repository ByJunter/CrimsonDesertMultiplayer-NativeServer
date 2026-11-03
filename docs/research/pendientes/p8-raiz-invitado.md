# P8: causa de los fallos del cliente real del invitado

Investigación independiente, 9 de octubre de 2026. Crimson Desert PC **1.0.0.2976**, base de imagen **0x140000000**. Todas las direcciones de funciones e instrucciones se expresan como **RVA: exe+0x…**, salvo que se indique otra cosa. Opcodes en decimal; offsets de estructuras en hexadecimal.

**[V]** significa comprobado en instrucciones, referencias cruzadas, código del mod, registros o paquetes conservados, con la evidencia indicada. **[E]** significa inferencia causal o propuesta todavía no probada en ejecución. «Dato aportado» distingue el relato del encargo de una comprobación independiente. Una ausencia en la captura significa ausencia en sus registros conservados, no prueba de que el proceso no produjera nada después de su último registro.

## 1. Resumen de una página

### S1: no se encuentra una acción de secuenciador

**[V]** Los hashes `837487D4` y `35DD392F` son, respectivamente, **`seq_bodyanim_action_xxx`** y **`seq_bodyanim_action_loop_xxx`**, creados por `SeqBodyAnimationActionChartPackage`. No son identificadores desconocidos de un asiento: el constructor `0x2433640` calcula ambos hashes y la ruta de `GameBodyAnimationEventData` instala sus gráficos dinámicos en `CC+0x70/+0x78`. La restauración busca allí después del gráfico de cada capa, pero los 15 actores fallidos no tienen esos gráficos alternativos y no consiguen una tripleta válida; devuelve `eErrNoInvalidActionChart`. Evidencia: constructor `0x2433A5B/60`, `0x2433BAB/B0`; búsquedas `0x86390F/925`; diagnóstico invitado, líneas 165–235 detalladas en §3.

**[E, fuerte]** Se restaura el estado de animación de esos NPC sin haber reconstruido su contexto dinámico de BodyAnimation: secuenciador, binding de actores, clip y tiempos. Está identificado el productor nativo del recurso, pero no el paquete o evento concreto omitido o retrasado para cada NPC; no puede darse por demostrado que recibir únicamente 2322 lo solucione.

### S2: el cliente espera una preparación de secuenciadores que la entrada compartida no reproduce

**[V]** El estado `0x11` exige **dos condiciones**: `M+0x231 >= 2` y `0xA97D60(M,posición) != 0`. `PlayAllPreLoadSequencerAck=2322` escribe 2, pero su ruta de emisión durante la entrada de campo exige que el viajero sea el dueño del campo. El usuario 2 omite esa rama. La captura solo tiene un 2322, del primer campo privado; ninguno del segundo viaje compartido. Evidencia: `0x779E48–0x779E7F`, `0xB87A67`, `0x2BC8470/73/84D4`, paquete binario #794 y texto de captura línea 804.

**[E, fuerte]** La incorporación del observador carece de un ciclo completo de sincronización de stages/secuenciadores dirigido a su personaje. No se midieron en vivo el byte `+0x231` ni la causa particular del segundo predicado, por lo que el bloqueo exacto de esa sesión sigue pendiente. **[V]** No hay transición registrada de `0x11` al siguiente estado. **[E]** El vínculo preciso «cerrar Extra Rewards List → desbloquear carga» no está demostrado: aumentan las apariciones y acciones, pero no aparece otro 2322; falta la captura local del anfitrión y el instante nativo del cierre.

### S3: se actualiza una capa antes de disponer de una acción utilizable

**[V]** `0x232D5FF` desreferencia la **acción actual** del snapshot de capa, procedente de `layer+0x10`; la ruta cliente de `0x232D2A0` puede llegar con gráfico presente y sin comprobar esa acción. El actor de la última guarda es **Crow `B01002A0`**, nuevo y con restauración 0: no pertenece a los 15 fallidos de S1. Su primer 2723 llega 322,470 ms después de su 2005. Además, el consumidor nativo de 2723 exige control local y puede devolver éxito sin aplicar nada; 2991 usa otro consumidor. Evidencia: `0x232D51E–0x232D5FF`, `0x48B191–0x48B1AD`, captura líneas 12362/12494/12511 e invitado líneas 313–314.

**[E, fuerte]** Hay un defecto de activación inicial de actores observados: publicación con restauración 0, actualización habilitada antes de una acción válida y primera acción enviada por una vía que puede descartarla por control remoto. **[E]** Crow es un candidato concreto al cierre, no la víctima demostrada: no se entregó el WER original ni un dump con el CC del fallo. Otras 41 apariciones de la ráfaga también llevan restauración 0.

**[E] Relación entre los tres:** la opción D incorpora un observador donde varias rutas nativas asumen dueño/controlador único. Eso explica una familia de dependencias incompletas, pero **no demuestra una única rama causal para los tres síntomas**. S1 necesita el gráfico dinámico correcto; S2 necesita completar su propio ciclo de preparación; S3 necesita una primera acción aplicable antes de consumirla. Saltar la carga o añadir comprobaciones de nulos no cumple esas condiciones.

## 2. Material, método y trazabilidad

**[V]** Se trabajó con tres agentes para S1, S2 y S3, lectura cruzada posterior y comprobaciones independientes de capturas y consumidores. Se consultaron decompilación y referencias de Ghidra en una **copia propia**, `-readOnly -noanalysis`, contrastadas con Capstone sobre el PE autorizado. No se ejecutó el juego, no se accedió a su carpeta ni se implementaron modificaciones del mod.

Fuentes primarias y abreviaturas utilizadas:

| Referencia | Archivo real |
|---|---|
| G | `C:\Emulador CD\backups\p8-sesion4-shadow\cdmp.log` |
| P | `C:\Emulador CD\backups\p8-sesion4-shadow\cdmp_packets_20261009-170454.log` |
| B | `C:\Emulador CD\backups\p8-sesion4-shadow\cdmp_packets_20261009-170454.bin` |
| H | `C:\Emulador CD\backups\logs\cdmp_20261009-1801_p8f-host.log` |
| PE | `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` |
| Código / CSV | `C:\Emulador CD-opcion-d\src\…` / `docs\research\data\troc_messages_2976.csv` |

**[V]** El PE mide 384.521.624 bytes, SHA-256 `57DA440D72F4DB974F25FEF047CF84C4DADD999A88CB2A3C5AF4C9BD67FDE1E7`. B mide 13.426.462 bytes, SHA-256 `6101DE6ED9DA58D49B8282C876A85738845D114E8690430578419CF6BA565AA6`. Los hashes del resto de entradas están en el manifiesto de trabajo.

**[V]** Se revisaron también los informes anteriores `p8-actores-anfitrion.md`, `companeros-cierre-salida.md`, `companeros-publicacion.md`, `enemigos-del-anfitrion.md`, `opcion-d-accion-usuario2.md`, `opcion-d-campo-compartido.md`, el diario y el código de las sondas/retenciones. Se utilizaron para orientar búsquedas; las afirmaciones que se corrigen abajo se contrastaron con evidencia primaria. Las sesiones anteriores carecen del diagnóstico adicional de sesión 4 y no resuelven las lagunas de esta.

**[V]** Evidencia auxiliar conservada en:

`<codex-workspace>\p8-raiz-work\`

- `s1/findings.md`, `hash-evidence.txt`, `actors.json`, `d863880.txt`, `d24314c0.txt`, `d24315a0.txt`, `d2433640.txt`, `da79770.txt`, `d9ed290.txt` y registros extraídos.
- `s2/findings.md`, `capture-analysis.txt`, `disasm-loading.txt`, `sender2322.txt`, `movefieldcore.txt`, `writers231.txt`, `ui-close-readonly.txt`.
- `s3/findings.md`, `disasm.txt`, `actor-timeline.txt`, `late-actors.txt` y registros extraídos.
- `ghidra/exports/decomp/`: decompilaciones nuevas; `ghidra/logs/`: consultas de esta instancia; `input-manifest.json`, `original-project-before.json`, `cleanup.json`.

**[V]** Los scripts propios leen datos; Python se invocó con `-I`. Los ficheros de entrada no se ejecutaron. Las lecturas estáticas de `.xtls` permitieron seguir algunos thunks, pero **ningún punto de instrumentación o modificación propuesto está allí ni en un thunk hacia ella**.

### Numeración, formato y reloj

**[V]** `#n` significa registro binario CDPK, numerado desde 1; `P:n` es línea del fichero de texto, que contiene líneas adicionales. No se deben intercambiar. El formato se contrastó con `src/common/capture_file.h`: cabecera CDPK de 12 bytes; cada registro tiene 24 bytes de metadatos (`u64 t_us`, dirección, tamaños, sesión e hilo) y el paquete copiado. Paquete Troc: `u16 opcode | i8 slot | u16 longitud | payload`. Se consumieron exactamente los **13.034 registros**, sin registros truncados. Esto no acredita que se vaciara la cola del sniffer antes del cierre.

**[V]** En 2005, cada registro lleva delante una longitud `u16`. Los offsets de este informe empiezan **después de esa longitud**: tipo `+0`, actor key `+1`, dueño `+5`, controlador `+0x12`, variante `+0x16`, siguiente campo `+0x1A`, longitud del nombre `+0x1E` y UTF-16 `+0x22`. Contar el prefijo explica la diferencia de 2 bytes respecto a otros informes. El campo `+0x1A` se conserva como dato crudo; este trabajo no demuestra su identificación con una tabla de plantillas.

**[V]** Para tiempos relativos se resta el primer timestamp CDPK. No se usa la hora del nombre del fichero como inicio real. El anclaje `2490` en G:92 (17:05:20.748) y P:11 (t=0,140) da inicio aproximado **17:05:20.608**; otro anclaje con 3093 difiere 11 ms. La última captura, t=112,802728, corresponde aproximadamente a **17:07:13.40–13.41**. La conversión no permite ordenar sucesos entre hilos con precisión de pocos milisegundos. Para el anfitrión se usa la diferencia de una hora indicada en el encargo.

## 3. S1: origen de los hashes y gráfico dinámico ausente

### 3.1 Cadena de fallo y condición exacta

**[V]** La cadena reconstruida es:

```text
FinishGroup 0x4FD2E0
  → loginCharacter 0x17E94E0
    → CC::vf19, .xtls 0x984D830 [solo lectura]
      → RestoreActionsFromLoadData 0x8646D0
        → RestartLayerAction 0x863880
          → búsqueda 0x24314C0 en gráfico de capa y después CC+70/+78
          → error 0x349EFD61 si falta una tripleta necesaria
```

**[V]** `0x863880` procesa dos descriptores de 0x78 bytes. Para cada hash no cero consulta `[layer+0x110]` en `0x86390F` y, si no encuentra, `[CC+0x70+8*i]` en `0x863925`. Requiere al menos una acción y tres punteros válidos por hash no cero; valida la capa 0 en `0x863952–0x863968` y la capa 1 en `0x863970–0x86398F`. La rama de error `0x863995–0x86399B` no llama a `0x2329A90`, que es la continuación de arranque en `0x8639C8`. Evidencia: desensamblado `s1/d863880.txt` y decompilación de esa función.

**[V]** `0x24314C0` hace búsqueda binaria por hash unsigned en un vector ordenado de **tripletas de 24 bytes**: inicio `[graph+0x18]`, cantidad `u32[graph+0x20]`, hash en `u32[tercer_puntero+0x18]`. Si encuentra copia 16+8 bytes a R8 y devuelve AL=1; si falta, AL=0. `0x24315A0` reconstruye y ordena ese vector a partir de los paquetes del gráfico; no es una lista de actores recibidos ni una tabla global de todos los hashes. Evidencia: `0x24314CF/E7`, `0x2431522`, `0x243155A`, `0x2431570–0x2431599`; construcción `0x24315D9`, `0x2431714/1735`, `0x24317E8–0x24317FF`.

**[V]** En los 15 fallos, el diagnóstico encuentra gráfico de capa, restauración 1, los hashes sintéticos y gráficos alternativos nulos. No se capturó cada entrada del vector en vivo. **[E]** La explicación mejor sustentada es gráfico dinámico no reconstruido o no disponible a tiempo; una tabla corrupta o tripleta incompleta también satisface el error y no queda excluida únicamente por ese log.

### 3.2 Los nombres son verificables, no una conjetura semántica

| Hash observado | Nombre usado por el hash [V] | Cadena minúscula en PE | Creación nativa [V] |
|---|---|---|---|
| `837487D4` | `seq_bodyanim_action_xxx` | `0x59C1518` | hash en `0x2433A5B`, escritura de acción+0x18 en `0x2433A60` |
| `35DD392F` | `seq_bodyanim_action_loop_xxx` | `0x59C1540` | hash en `0x2433BAB`, escritura en `0x2433BB0` |

**[V]** El constructor `0x2433640` toma las versiones mayúsculas (`0x59C1210`, `0x59C14F8`), convierte a minúsculas y llama a `0x1364780`, hashlittle con semilla `0xC5EDE`. Una reproducción independiente de lookup3 da exactamente ambos valores; también reproduce `wait_default→26107D3C` y `eErrNoInvalidActionChart→349EFD61`. Ninguno de los dos hashes aparece como constante u32 literal en el PE: se calculan durante la construcción. Pruebas: `s1/hash-evidence.txt`, `d2433640.txt` y decompilación nueva `0x2433640.c`.

**[V]** Ese constructor nombra **`SeqBodyAnimationActionChartPackage`** en `0x243405A/4081` y fija dos acciones en `0x24340FE`. Por tanto, la coincidencia de hashes entre NPC distintos identifica acciones genéricas de BodyAnimation; no identifica por sí sola el recurso animado o su estado temporal.

### 3.3 Quién proporciona el gráfico en la ruta nativa

**[V]** La ruta comprobada es:

```text
pa::GameBodyAnimationEventData (RTTI; vtable 0x55E0980)
  0xAE0A50 → 0xADEBF0 → 0xADED90 → 0xA79770
    → constructor gráfico [thunk 0x2432D60 → .xtls 0xF58C420, solo leído]
      → 0x2433640 → actualización 0x2430E50 → tabla 0x24315A0
    → asigna CC+70 / CC+78
    → 0x2432F60 configura recursos y tiempos de animación
```

**[V]** `0xADED90` toma el actor enlazado en `arg2+0x1B8`, obtiene contexto vía `[actor+0x68] → [+0x40] → [+0x110]` y llama a `0xA79770` en `0xADF1A2`. En esta última, las llamadas `0xA79B94` y `0xA79F34` crean gráficos; las escrituras `0xA79BAC` y `0xA79F4C` los instalan en `CC+0x70/+0x78`. Las llamadas `0xA79C1F` y `0xA79FC5` a `0x2432F60` configuran datos específicos de la animación. El recorrido completo figura en las decompilaciones nuevas `0xae0a50.c`, `0xadebf0.c`, `0xaded90.c`, `0xa79770.c` y los desensamblados de S1.

**[V]** `0xADEBF0` exige estados adecuados del recurso/evento y actor enlazado no nulo. `0xAE0A50` puede despachar directamente o programar el evento según `arg2+0x5C & 0x10`. No se encontró en el tramo que crea estos gráficos un veto equivalente al de control local de 2723. **[E]** Falta determinar si, para cada NPC fallido, no se recibió el contexto, no se enlazó el actor, no estaba listo el recurso o se ejecutó tarde; atribuirlo directamente a «modo 2» no está probado.

**[V]** Existe además una dependencia concreta de binding. `ClientSequencerPlayDataActorComponent::vf13=0x9ED290` utiliza `comp+0x2D8` y `comp+0x2E0`, busca el actor con `0x8AE1A0` y la instancia de secuenciador en su gestor. Si no resuelve actor o instancia, puede devolver éxito **sin binding** (`0x9ED431` y `0x9ED47F`). El lector de su vf11 deserializa esos campos desde el flujo; parte del contexto puede viajar ya en el 2005. La ruta de lectura virtualizada se inspeccionó estáticamente, sin proponer instrumentarla. **[E]** Es un diagnóstico más específico que buscar un asiento inexistente: medir si falta el actor enlazado o la instancia.

**[E]** Restaurar solo dos nombres de acción, copiar un puntero de otro actor o sustituirlos por `wait_default` no reconstruye el clip y tiempo que configura `0x2432F60`. Se necesita el contexto de **ese** secuenciador y de **ese** actor.

### 3.4 Los 15 actores y sus paquetes

**[V]** Tabla decodificada de B; todos tienen dueño 0 y controlador `90100000`. Los offsets excluyen el prefijo u16 del registro.

| Actor | Tipo | Variante | Nombre UTF-16 | B # / P línea |
|---|---:|---:|---|---|
| `B010006D` | 4 | 4 | Damiane | 1187 / 1203 |
| `B0100071` | 4 | 50901 | Matthias | 1189 / 1205 |
| `B0100072` | 3 | 58007 | Wandering Freesword | 1189 / 1205 |
| `B0100073` | 3 | 54605 | Wandering Freesword | 1189 / 1205 |
| `B0100074` | 3 | 54608 | Wandering Freesword | 1190 / 1206 |
| `B0100075` | 3 | 620 | Barden Middler | 1190 / 1206 |
| `B0100076` | 3 | 3101 | Stranger | 1190 / 1206 |
| `B0100077` | 3 | 3053 | Stranger | 1190 / 1206 |
| `B0100078` | 3 | 3052 | Stranger | 1191 / 1207 |
| `B010007E` | 3 | 53213 | Hernandian Guard | 1192 / 1208 |
| `B0100080` | 3 | 53209 | Hernandian Guard | 1193 / 1209 |
| `B0100082` | 3 | 1002190 | House Roberts's Servant | 1193 / 1209 |
| `B0100143` | 5 | 32806 | Elantrima Packhorse | 1242 / 1258 |
| `B01001FD` | 3 | 3770 | Maid | 1504 / 1522 |
| `B01001FC` | 3 | 3181 | Servant | 1504 / 1522 |

**[V]** Sus cargas llegan entre t=52,048 y t=55,390. G registra fallos en líneas **165, 170, 174, 178, 182, 186, 190, 194, 198, 205, 210, 212, 228, 230 y 235**. `B0100078`, ejemplo solicitado, tiene los hashes en registro+`0x1240/0x124F`. `B0100143` solo lleva la primera capa: también falla, por lo que la capa secundaria no es condición necesaria del problema. Ninguno de los 15 recibe 2723/2991 en B. Prueba reproducible: `s1/actors.json`, `s3/actor-timeline.txt`.

**[V]** Un barrido de palabras u32 con aspecto de actor-key en el registro de `B0100078` solo identifica su clave y `A0100001`. **[E]** Esto no demuestra ausencia de relación con otro objeto: un binding puede usar otra codificación, un identificador de secuencia o contexto indirecto. No se encontró evidencia suficiente para afirmar «falta tal silla, carro o gimmick».

**[V] Corrección del diagnóstico:** `on_restore` guarda cur/play **antes** de llamar a `0x8646D0`; `on_restore_exit` imprime ese snapshot junto al resultado final (`guest_actor_diag.cpp:314–378`). Una línea de restauración exitosa con cur=0/play=0 no demuestra que siga nulo al retornar. En los fallos, el retorno y la rama de búsqueda sí acreditan que esa restauración no arrancó la acción; falta un snapshot posterior para describir todo el objeto.

**[V] Contraprueba de una explicación demasiado amplia:** B #11864, t=111,251907, contiene `StartActionReq=2223` del propio `A010022E` con `35DD392F`. El invitado puede ejecutar alguna secuencia propia. **[E]** El problema es de reconstrucción/contexto de esos NPC ajenos, no incapacidad universal del cliente invitado para usar secuenciadores. No hay una captura de memoria del anfitrión que permita comparar las tablas de esos mismos 15 actores.

## 4. S2: condición de salida, dueño del campo y aviso modal

### 4.1 Lo que espera realmente el estado 0x11

**[V]** `0x7797F0` obtiene por `0x9A5BE0` el gestor de stages/secuenciadores asociado al contexto del viaje. RTTI de la familia: `pa::ClientSequencerStageManager`, vtable `0x55C9720`. La condición comprobada es:

```text
M = 0x9A5BE0(**(P+0xC0))
si u8[M+0x231] < 2: permanecer en estado 0x11
si 0xA97D60(M, vec3(P+0x150)) == false: permanecer en estado 0x11
P+0x15C = 0x12
```

**[V]** Instrucciones decisivas: `0x779E48 cmp byte [rax+231],2`; `0x779E4F jb`; `0x779E72 call 0xA97D60`; `0x779E77 test al,al`; `0x779E79 je`; `0x779E7F mov byte [rdi+15C],12`. Prueba: `s2/disasm-loading.txt` y decompilación nueva `0x7797f0.c`.

**[V]** El segundo predicado comprueba dependencias reales, no repite únicamente el byte. `0xA97D60` puede bloquear por contexto de campo no disponible, `M+0x174 != 0`, estado/tiempo `M+0x2D4/+0x2D0`, entradas pendientes en `M+0x1B0/+0x1B8` y recursos en `M+0x1C0/+0x1C8`. En la primera colección examina `entrada+0x28C`, resultado de `0x1AC2830`, modo `+0x60`, byte `+0x61` y caja de posición `+0xD8..+0xEC`; en la segunda puede exigir `0x5DB510([entrada+0x58])`. **[E]** Los nombres semánticos exactos de cada modo aún no se han resuelto; los offsets y predicados son los del decompilado y Capstone, no tipos oficiales inventados.

**[V]** Se identificaron escrituras 0 en `0x77AA8B`, 1 en `0x77B0CE` y 2 en `0xB87A67`. Los resets dependen de ramas y flags `P+0x162/+0x163`; no se registraron sus valores. Otra ruta escribe 3, pero su llamador ordinario `0x77D061` pertenece a la preparación del estado **0x14**, posterior a 0x11. Se siguieron estáticamente sus cuerpos virtualizados, sin proponer enganches. El barrido no encontró otro escritor inmediato de 2; no descarta escrituras indirectas o anchas. Evidencia: `s2/writers231.txt` y decompilaciones `0x77cfb0.c`, `0x7797f0.c`.

**[E]** No basta afirmar que el único 2322 del primer campo mantiene el segundo listo: el gestor puede resetearse/recrearse. Tampoco basta suponer que quedó en 1: sin medir M y sus flags no se conoce qué rama concreta bloqueó en sesión 4. Incluso conservando 2, el segundo predicado puede impedir avanzar.

### 4.2 Quién produce el hito 2322 y a quién se dirige

**[V]** `PlayAllPreLoadSequencerAck=2322` contiene una actor-key u32, 9 bytes totales. Handler `0xB87890`: lee 4 bytes, resuelve el actor, obtiene contexto y gestor; en **`0xB87A67`** ejecuta `C6 80 31 02 00 00 02`, es decir, `[M+231]=2`. CSV: línea 101; lectura de 4 bytes `0xB87941/47`, uso `0xB87977`, escritura indicada.

**[V]** El decompilado heredado tiene aquí un prototipo del deserializador que simplifica mal la clave/longitud y elimina bloques reales. No se tomó esa pseudocódigo como formato de protocolo: las instrucciones y el paquete de 9 bytes prueban la lectura. No se cambiaron tipos del proyecto original para corregirla.

**[V]** Emisor nativo `0x2999D80`: serializa `actor+0x60`, escribe opcode `0x0912` en `0x2999E77` o `0x2999F89` (la segunda rama carga primero la constante en `0x2999F83`) y envía mediante el finder recibido. El único CALL directo encontrado es `0x287F1D4`, dentro de `0x287EF60`. Esta última prepara stages/secuenciadores y temporizadores y construye **`ToTargetSendTargetFinder` para el actor indicado**; no difunde automáticamente a todos los usuarios cercanos. Los reintentos de `0x287DBB0/0x2895EF0` usan el actor `manager+0x18`, no un recorrido de observadores. Una llamada indirecta desconocida no queda excluida por la búsqueda de CALL directos.

**[V]** En `MoveFieldCompleteCore=0x2BC7670`:

```text
0x2BC8452  rax = [r14+0xA0]       ; usuario del actor que viaja
0x2BC8461  eax = [rax+0x60]       ; userKey
0x2BC8469  r8d = [r13+0x180]      ; dueño del campo
0x2BC8470  cmp eax,r8d
0x2BC8473  jne 0x2BC84D9          ; no-dueño omite la preparación
...
0x2BC84D4  call 0x287EF60
```

**[V]** Otra comprobación de dueño en `0x2BC7885/8C` omite la sincronización de stages `0x2910DE0` para el no-dueño. Por ello la ruta normal de entrada del usuario 2 al campo del anfitrión salta ambos pasos. **[E, fuerte]** Modificar el owner anunciado al cliente por 2174 no modifica este owner real del campo servidor; permite al cliente elegir su propio contexto, pero deja pendiente una incorporación coherente a los stages existentes. No se propone alterar ese owner global.

### 4.3 Cronología verificable frente a la observación de pantalla

| Hecho [V] | B # | Tiempo relativo | Referencia |
|---|---:|---:|---|
| 2549 del primer viaje | 539 | 22,868031 | P:549 |
| 2144 del primer viaje | 789 | 23,354625 | P:799 |
| Único 2322, actor `A010022E` | 794 | 23,354647 | P:804 |
| Única 2445, payload 01 | 994 | 38,480342 | P:1008 |
| Única 2762 | 995 | 38,534201 | P:1009 |
| 2549 del segundo viaje | 1047 | 50,316843 | P:1063 |
| 2144 del segundo viaje | 8863 | 61,262869 | P:8881 |
| 3066 del segundo viaje | 8873 | 61,262890 | P:8891 |
| Acción de secuenciador propia, 2223 | 11864 | 111,251907 | payload de B |
| Primera nueva aparición de la ráfaga | 12334 | 112,014219 | P:12362 |

**[V]** No hay otro 2322, ni 2873 `PreLoadSequencerReq`, 2661 `PrepareSequencerAck` o 2810 `DeleteSequencerAck` en B. G:245 registra estado 0x11 a **17:06:26.936** y G:309 lo conserva a **17:07:11.849**: al menos **44,913 segundos** observados. No hay muestra posterior que pruebe la salida del estado. `host ready yes` es el latido del túnel del mod (`guest.cpp:167–170,884–890`), no una confirmación de carga nativa.

**[V] Corrección necesaria del encargo/informe previo:** la lista tiene 173 claves, pero la sonda solo comprueba **64** y muestra como máximo 16: `kWaitRead=64`, `kWaitShown=16`, `guest_actor_diag.cpp:93,440–456`. Su texto «state0x12 waits for nobody» significa que la muestra no encontró un bloqueo. No demuestra que las 173 estén listas ni que el estado 0x12 nunca pueda bloquear después. Las muestras de estado sí demuestran que, en esos instantes, la espera actual era 0x11.

**[V]** El tráfico hacia el usuario 2 sube de 8.865 a 11.893 paquetes entre H:773 (18:07:08.035) y H:967 (18:07:18.041), **3.028 paquetes**. En los segundos 111–112 de B crecen movimientos 2697, acciones 2991, cambios de IA 2404, apariciones 2005 y primeras acciones 2723. No aparece el segundo hito de precarga o carga. Fuente: `s2/capture-analysis.txt`.

**[V]** El servidor no estaba totalmente parado: H:699–700,715–716 y775–780 registra actualización del usuario 2 y su compañero durante la espera, mientras siguen llegando respuestas de red al invitado. **[E]** El aumento tras cerrar el modal es compatible con reactivación de la simulación de NPC controlada por el cliente anfitrión; no prueba que se parara todo el servidor ni identifica un opcode concreto de despausa.

**[V]** Se localizó por RTTI `pa::uiCommonScript::UIGamePlayControl_Root_ExtraReward`, vtable `0x569C6B8`. `0xE919C0` procesa eventos y una rama hacia `vf28` puede entrar al cierre nativo; el cuerpo virtualizado leído modifica flags de UI `+0x26C`. **[E]** No se estableció la cadena desde ese cambio hasta la simulación del servidor. Faltan evento de cierre con timestamp, captura cliente-anfitrión→servidor-anfitrión y comparación de estados antes/después. La observación de pantalla del usuario se conserva como dato aportado, sin convertirla en una transición de máquina no registrada.

## 5. S3: nueva aparición, acción inicial y acceso inválido

### 5.1 Cadena exacta: ambas llamadas están en Update

**[V]** `ActorUpdate_ParallelTask=0x8B56D0` llama a `CharacterControl::Update=0x85FBA0` y después a `PostUpdate=0x860D30`. Las llamadas **`0x85FC94` y `0x860110` a `0x232D2A0` están ambas dentro de Update**, que termina en 0x8601AE. La segunda no está en PostUpdate. Lo verifican los límites de función, Capstone y xrefs de Ghidra.

**[V]** La rama corta de Update llama antes a `0x863E80` y `0x23299B0`. Puede llegar a `0x232D2A0` si `CC+0xA8==0`, el tercer argumento es 0 y se cumplen los filtros de estado/tipo de `0x85FC0E–0x85FC46`; ahí no exige acción actual. La rama larga tiene la segunda llamada. La guarda existente en `0x2329700`, llamada desde `0x863E80`, retorna de esa función, **no de todo Update** (`remote_puppet.cpp:635–651` y `0x8641C6`).

**[V]** En `0x232D2A0`, la rama cliente (`TLS+0x1EC==0`) exige gráfico en `layer+0x110`, pero no exige los tres punteros de acción antes de llamar al consumidor. La rama servidor incorpora comprobaciones adicionales de control y presencia. La prueba de la acción de la capa 0 en la rama cliente decide si omitir la segunda capa; no evita el consumo de una primera acción nula. Evidencia: `0x232D33A–0x232D3B2`, `0x232D4E1–0x232D51E`.

**[V]** Se copia a la pila `layer+0`, `layer+8` y `layer+0x10` (`0x232D543/54D`) y se llama con RCX=CC, EDX=índice y R8=snapshot (`0x232D5A2–0x232D5AD`). El consumidor hace:

```text
0x232D5FB  49 8B 40 10             mov rax,[r8+0x10]
0x232D5FF  0F B7 80 E4 00 00 00    movzx eax,word [rax+0xE4]
```

**[V]** Es la **acción actual**, no el playback `layer+0x18`. R8 apunta al snapshot de tres punteros, no a la capa original. La decompilación confirma la lectura antes de validar el puntero; no se adopta la anotación antigua que lo llamaba playback. **[E, dato aportado]** Para este cierre de Shadow, RVA, hora y RAX=0 proceden del encargo; sin WER/dump original no se acreditan de forma independiente los registros de CPU de ese accidente.

### 5.2 Crow no es uno de S1; cronología de su primera acción

| Suceso [V] | Evidencia | Resultado |
|---|---|---|
| 2005 de `B01002A0` | B #12334, P:12362, t=112,014219 | tipo 6, Crow, dueño 0, control `90100000` |
| Estado guardado | registro extraído, `+0x123B` | restauración **0**, sesión de acción 1 |
| Guarda de capas | G:313, 17:07:12.940 | gráfico presente y acción **o** playback ausente |
| Guarda de animación | G:314, 17:07:12.945 | misma clave |
| Primer 2723 | B #12466, P:12494, t=112,336689 | hashes `968C694E/0`, 322,470 ms después del 2005 |
| Primer 2991 | B #12483, P:12511, t=112,369287 | hashes `AD6A1103/0`, 32,598 ms después del 2723 |

**[V]** Ruta de apariencia del registro: `character/appearance/2_mon/cd_m0003_00_bird/cd_m0003_00_crow/cd_m0003_00_crow_0001_00000.app_xml`. La ruta empieza en `+0x11D3`, longitud 96. El formato se contrastó con serializador `0x231FC90` y lector `0x2320200`: longitud/ruta, sesión u64 y byte de restauración. `0x8646D0` no restaura acciones cuando ese byte es 0. Evidencia: `s3/rec-B01002A0-12334.bin`, desensamblado y cronología.

**[V]** La guarda realmente prueba `chart && (!current || !playback)`. Aunque el log diga «no action», no identifica cuál de los dos falta. **[E]** La guarda y la primera 2723 caen dentro de la incertidumbre de alineación entre logs; no se puede demostrar un orden exacto de 5 ms ni afirmar que Crow tuviera ambos punteros nulos.

**[V]** La ráfaga t=112,014219–112,594295 contiene **53 actores nuevos**: 42 con CC (26 tipo 6, 13 tipo 3, 3 tipo 5), todos con restauración 0, y 11 gimmicks tipo 7. Los 42 reciben 2723; nueve no reciben 2991 antes del final de B: `B01002C0`, `C1`, `C2`, `C3`, `C6`, `C7`, `CB`, `CC`, `CD` (prefijo común B01002). Fuente: `s3/late-actors.txt`.

**[E]** Crow es un candidato directo porque la guarda lo identifica; la población expuesta es más amplia. El siguiente acceso inválido podría pertenecer a otro actor nuevo o a uno de S1. No se puede reconstruir la víctima exacta usando únicamente paquetes: faltan CC/actor del fallo y estado posterior a cada handler.

**[V]** Solo hay tres archivos en la carpeta entregada de sesión 4; no contienen WER ni dump. B acaba alrededor de 17:07:13.41; G:315–316 contiene actividad de otro hilo hasta 17:07:16.747. **[E]** La hora del WER aportada no demuestra cuatro segundos de actualización correcta después de la guarda: puede incluir demora de notificación o actividad de otros hilos.

### 5.3 Por qué recibir 2723 no garantiza una primera acción remota

**[V]** `StartSpawnActionAck=2723` sigue `0xB57BC0 → 0x48B120`. Allí:

```text
0x48B18F  mov dl,5
0x48B191  call [vtable+0x188]
0x48B197  test al,al
0x48B199  jne 0x48B1B2       ; aplica solo si la prueba es verdadera
0x48B19B  mov [resultado],0  ; falso: éxito sin aplicación
0x48B1AD  jmp 0x48B3D7      ; salida
```

**[V]** `0x4FA620`, implementación de esa prueba con argumento 5, devuelve `actor+0xC8 != 0`. `0x4FA530` calcula C8 y, en la ruta normal de actor con controlador distinto del usuario local y sin bit 30 en la clave, lo fuerza a 0 (`0x4FA60F`). Hay excepciones, incluido tipo 7, y retornos previos; no es una regla universal para cualquier objeto. Este byte no es el «modo 2» del controlador de movimiento. **[E, fuerte]** Se espera que Crow remoto pase por el rechazo, pero su C8 y retorno virtual no se registraron: hace falta medirlos para afirmar que su 2723 particular fue descartada.

**[V]** `StartActionAck=2991` usa otra ruta: `0xB57720 → 0xB57A5A call 0x48AD40 → 0x48AE9D call 0x863530`. No tiene aquel veto; sí puede descartar por `[[CC+0x60]+0x10]&1` si no se fuerza, por sesión de acción antigua o fallar al resolver un hash. El handler ignora el resultado (`0xB57A5F`). `0x863530` intenta iniciar/restaurar mediante `0x863880`; no se encontró una cola automática que garantice esperar recursos. **[E]** La llegada de 2991 tampoco acredita una acción instalada.

**[V]** El motor dispone de una vía para un remoto ya activo: 2005 con restauración 1 y gráfico válido, seguido de acciones por 2991. Para la aparición nueva observada, restauración 0 no hace ese trabajo y 2723 depende de control local. Un NPC local puede superar esta última puerta; su contexto de control y lógica local es diferente. **[E]** Esta asimetría explica la ventana insegura sin afirmar que el motor carezca de todo soporte remoto. No se ha demostrado el orden íntegro de la IA que garantice la primera acción de cualquier NPC en un jugador.

**[V]** D-53 no retiene a este Crow: `child_hold.cpp:130–145` examina dueño `actor+0xA0` y devuelve `NoOwner` para NPC sin dueño. Extenderla a «cualquier dueño» no cubre actores cuyo dueño es 0 pero cuyo **controlador** es el anfitrión.

## 6. Arreglos de raíz propuestos, por preferencia

Todas las opciones siguientes son **[E] propuestas**, no cambios implementados ni soluciones ya validadas. Las direcciones son puntos candidatos de integración en código real `.rsrc`; los bytes comprobados se reúnen en §7. No se propone enganchar `.xtls`, sus cuerpos virtualizados ni sus thunks.

| Prioridad | Lugar y punto candidato | Efecto buscado | Riesgo y condición previa |
|---|---|---|---|
| 1 | Anfitrión: entrada compartida `0x2BC7670`; emisor final `0x2999D80` | Incorporar al usuario 2 como observador del estado existente de stages/secuenciadores, con dependencias ordenadas y hito final 2322 dirigido a su personaje | Separar snapshot de mutación del mundo. No ejecutar indiscriminadamente `0x287EF60`: prepara stages y timers. Evitar repetir recompensas, secuencias globales o cambios de estado. No hay aún lista completa demostrada de paquetes del snapshot. |
| 2 | Invitado: restauración `0x863880`; preparación nativa de BodyAnimation observada en `0xA79BAC/0xA79F4C` | Reconstruir binding, instancia, clip y tiempos correctos antes de restaurar los hashes de S1 | Primero identificar qué contexto falta. Reutilizar la ruta nativa sin conferir autoridad/IA local. Los dos hashes genéricos y el hito 2322 no contienen toda esa información. Si el protocolo nativo no la entrega, estudiar un snapshot mínimo adicional verificable. |
| 3 | Anfitrión: `NotifyAppear=0x2B000C0`, aceptación de primera acción `0x296CAA0` | Publicar inmediatamente al controlador; retener para cada observador solo hasta disponer de estado inicial restaurable; reserializar el 2005 vigente al liberar | Resolver controlador por +0x90, no solo dueño +0xA0. Mantener referencias, época y generación; cancelar en despawn/cambio de campo/salida. No reutilizar el 2005 antiguo con byte 0. La bandera 1 sola no asegura que S1 tenga sus gráficos. No bloquear al controlador que debe iniciar la IA. |
| 4 | Anfitrión/invitado: serializador 2991 `0x2963FF0`, consumidores `0x48B120/0x48AD40` | Si se confirma el veto de 2723, entregar a observadores una primera acción por la semántica remota correcta | 2723 y 2991 no tienen el mismo formato: 2991 incorpora un flag adicional. No basta cambiar opcode ni forzar C8 globalmente. Validar sesiones y resultado. Esta adaptación conserva la ventana entre 2005 y acción si no se coordina con activación. |
| 5 | Invitado: ciclo de activación antes de `Update=0x85FBA0` y `PostUpdate=0x860D30` | Separar recepción/carga del actor de su habilitación para consumidores de acciones; activar tras dependencias y primera acción válidas | Una barrera solo pasa a ser parte del arreglo completo si hay progreso real hacia la acción y tratamiento de error/retirada. Esperar indefinidamente o salir por nulo es contención. Revisar otros consumidores fuera de Update. |

**[E]** El orden práctico es completar primero el diagnóstico faltante de §7, reconstruir la preparación dirigida al observador y establecer publicación/activación coherentes. Medir éxito como «contexto correcto + acción aplicada + actualización segura», no como «desapareció el mensaje de error».

**[E]** La preparación del anfitrión y la del observador son condiciones diferentes. Esperar restauración 1 en el servidor puede cerrar la ventana de Crow, pero los 15 fallos de S1 ya tenían esa bandera: el invitado debe resolver su propio gráfico/binding. Durante una retención hay que ordenar también movimiento, acciones, desaparición y salida del controlador. Una barrera del observador debe permitir recibir y procesar las dependencias que la liberan, para no introducir una espera circular.

**[E]** Si se demuestra que el aviso del anfitrión suspende preparación necesaria para el invitado, separar esa preparación de la presentación local es una corrección adicional. Todavía no hay un punto de cambio demostrado; `0xE919C0` es una sonda de correlación, no una propuesta de desactivar el modal.

**[E, solo contención]** Ampliar las guardas a Update/PostUpdate de actores no controlados localmente limita el acceso conocido mientras se construye la corrección. Devolver éxito artificial en `0x8646D0`, fijar `wait_default`, enviar solo 2322, forzar `M+0x231=2` o saltar el estado 0x11 no reconstruye dependencias. Cambiar dueño global o autoridad C8 puede romper la simulación del anfitrión. Ninguna de esas medidas se presenta como solución final.

## 7. Una sesión adicional: diagnóstico exacto sin cambiar decisiones del juego

**[E]** Propuesta de instrumentación futura de lectura, **no añadida al código en este trabajo**. Registrar QPC, hilo, época, clave y generación del actor; usar lecturas protegidas y buffers acotados. No llamar métodos del motor para obtener datos desde una sonda. Preservar registros, flags y SIMD; revisar instrucciones completas, relocación y saltos entrantes antes de instalarla. Los bytes de una tabla no constituyen por sí solos un trampoline seguro.

**[V]** Todos los puntos de las tablas siguientes caen en `.rsrc [0x1000,0x51EB000)` y sus bytes se contrastaron con el PE de este informe. Para sitios interiores se muestran instrucciones desde un límite confirmado; si los bytes incluyen un salto, debe conservarse su semántica completa.

### 7.1 S1: tabla, productor y binding

| Punto | Bytes verificados | Lectura propuesta [E] |
|---|---|---|
| `0x863880` | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 54 41 56` | RCX=CC, RDX=resultado, R8=2 descriptores de 0x78. Actor `[CC+8]`, hashes, capas, CC+70/+78. Capturar también salida y capas posteriores. |
| `0x863914` | `84 C0 75 12 48 8B 0F 48 85 C9 74 0A` | AL del lookup principal, RSI=hash, RDI=ranura fallback, RBX=tripleta destino. Distinguir falta de hash de tripleta incompleta. |
| `0x86392A` | `48 83 C7 08 48 83 C3 78 48 83 ED 01 75 BB` | Tripleta final y camino tomado tras lookup principal/fallback/hash 0, correlacionado por hilo. |
| `0x24314C0` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18` | Solo para ambos hashes: RCX=graph, EDX=hash, R8=out; cantidad+20, tabla+18 y resultado, con límites estrictos. |
| `0xA79770` | `48 8B C4 48 89 58 18 44 88 48 20 55 56 57 41 54` | RCX=contexto cuyo primer qword es CC; registrar descriptor de animación, recurso y tiempos de los argumentos nativos y correlacionarlos con las escrituras siguientes. No invocar esta función artificialmente con solo un hash. |
| `0xA79BAC` | `48 89 0B 4C 89 3F 48 8B 03 48 85 C0` | Antes de escritura: RBX=ranura CC+70, RCX=graph, RSI=contexto, CC=[RSI]. Comparar actor/descriptor anfitrión e invitado. |
| `0xA79F4C` | `48 89 0B 33 C0 49 89 07 48 8B 03` | Lo mismo para CC+78. Correlacionar creación, clip/tiempo y restauración. |
| `0x9ED431` | `80 7D E8 00 75 1C 44 89 2E` | R14=componente; booleano `[RBP-18]`, actor `[RBP-20]`, claves comp+2D8/+2E0. Acredita si se resolvió el actor del binding. |
| `0x9ED47F` | `48 8B 7D A8 48 85 FF 0F 84` | Instancia `[RBP-58]`, componente R14. Continúa un rel32: validar instrucción completa al instrumentar. Separar actor encontrado de secuenciador encontrado. |

**[E] Prueba decisiva de S1:** para `B0100078` o un equivalente de la nueva época, comparar los gráficos/descriptor de su CC en anfitrión antes de publicar 2005 y en invitado al restaurar. Si el anfitrión ejecutó la escritura BodyAnimation y el invitado no, seguir su instancia/binding/evento hasta el primer paso divergente. Si ambos la ejecutan, comparar hash, tripletas, recurso y vigencia. No basta repetir el error numérico.

### 7.2 S2: ambos predicados y cierre del aviso

| Punto | Bytes verificados | Lectura propuesta [E] |
|---|---|---|
| `0x779E48` | `80 B8 31 02 00 00 02 0F 82 53 03 00 00` | RDI=P, RAX=M; estado, flags P+162/163, M+231, +174, +2D4, float+2D0, listas+1B0/+1B8 y+1C0/+1C8, posición P+150. |
| `0x779E77` | `84 C0 0F 84 29 03 00 00 C6 87 5C 01 00 00 12` | AL del segundo predicado, RDI=P; registrar transición real, no solo informe cada 5 s. |
| `0xB87890` | `48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57` | Entrada 2322: R8=wrapper; raw=[R8+18], actor en raw+5; época y resultado del handler. |
| `0xB87A67` | `C6 80 31 02 00 00 02 48 8D 05 EB B3 D8 04` | RAX=M y byte anterior; comprobar que es el mismo gestor de 779E48. |
| `0x2BC8470` | `41 3B C0 75 64 48 8D 55 80` | EAX=usuario viajero, R8D=owner, R13=campo, R14=actor; resultado de la rama y época. |
| `0x287F1D4` | `E8 A7 AB 11 00 90 48 89 7C 24 50` | RCX=actor destinatario, RDX=finder; correlacionar emisión dirigida. |
| `0xE919C0` | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40` | Anfitrión: RCX=thisUI, R8=objeto evento, R9B; comparar R8 con [RCX+160], identificar RTTI ExtraReward y registrar evento de cierre. |

**[E]** Recorrer las **173 entradas reales** o informar explícitamente cuáles no pudieron leerse; registrar la primera dependencia que hace falso `0xA97D60`. Capturar simultáneamente tráfico local cliente↔servidor del anfitrión y tráfico del invitado, manteniendo separados «recibido en túnel», «entregado al handler» y «aplicado». Comparar al menos un segundo antes/después del cierre nativo. Si la carga permanece en 0x11 mientras ya se ve el mundo, registrar ambos hechos sin equipararlos. Esta sesión no requiere forzar flags ni enviar paquetes inventados.

### 7.3 S3: identificar la víctima y acreditar la primera acción

| Punto | Bytes verificados | Lectura propuesta [E] |
|---|---|---|
| `0x232D5C0` | `4C 89 4C 24 20 4C 89 44 24 18 88 54 24 10 48 89` | RCX=CC, DL=capa, R8/R9=snapshots, retorno; actor=[CC+8], key+60, control+90, C8, flags+5E/C1/C2/CF. Disparar al detectar snapshot+10 nulo. Registrar ambas capas +0/+8/+10/+18/+110/+3C, CC+70/+78/+A8/+1FC. |
| `0x232D2A0` | `48 89 5C 24 18 55 56 57 41 54 41 55 41 56 41 57` | CC, retorno 85FC99/860115, TLS+1EC y condiciones de Update. |
| `0x48B120` | `48 89 5C 24 10 48 89 74 24 18 55 57 41 54 41 56` | [RCX]=CC, actor, C8/control; R8=dos acciones deserializadas, stride 0x50; resultado y capas posteriores. |
| `0x48B197` | `84 C0 75 17 44 89 26 48 8B 8D 80 00 00 00` | AL tras vf188(5), con actor correlacionado al entrar; no modificarlo. Prueba del descarte de 2723. |
| `0x48AD40` | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55` | [RCX]=CC, R8=acciones, R9B=flag, quinto argumento; bit [[CC+60]+10] y salida. |
| `0x863530` | `48 89 5C 24 10 4C 89 44 24 18 48 89 4C 24 08 55` | Sesión R9, hashes, gráficos, retorno y estado posterior. Distinguir éxito sin aplicación de acción instalada. |
| `0x85FBA0` | `48 89 5C 24 10 55 56 57 41 54 41 55 41 56 41 57` | Actor/capas antes del primer Update; ordenar con fin de creación, 2723 y 2991. |

**[E] Prueba decisiva de S3:** una secuencia por actor `2005 restore=0 → fin de creación → Update sin acción → 2723 descartada/aplicada → 2991 descartada/aplicada`, más el CC del acceso inválido. Conservar un buffer de los últimos eventos y dump de excepción que permita recuperar actor y capas. Si resulta uno de S1, correlacionar con su fallo de restauración; si es nuevo, comprobar la publicación y primera acción. La identidad en `0x232D5C0` resuelve lo que el RVA de WER por sí solo no puede resolver.

### 7.4 Bytes de los restantes puntos candidatos de integración

| RVA `.rsrc` [V] | Bytes de entrada verificados |
|---|---|
| `0x2BC7670` | `48 89 54 24 10 48 89 4C 24 08 55 53 56 57 41 54` |
| `0x2999D80` | `48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57` |
| `0x2B000C0` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55` |
| `0x296CAA0` | `48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57` |
| `0x2963FF0` | `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 20 55` |
| `0x860D30` | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83` |

## 8. Qué queda sin demostrar

- **[E] S1:** el primer evento o paquete divergente entre anfitrión e invitado, los clips/tiempos y la instancia de los 15 NPC; falta memoria/traza emparejada del anfitrión. Está demostrado el nombre de las acciones, el productor del gráfico y la condición de fallo, no el punto exacto de omisión del contexto.
- **[E] S2:** cuál de los dos predicados detuvo realmente cada frame, si el gestor se reinició en ese viaje y el opcode/estado que cambia al cerrar Extra Rewards List. Falta la captura local del anfitrión y las sondas del gestor/UI. No está registrada una salida efectiva del estado 0x11.
- **[E] S3:** el actor concreto del acceso inválido y si su 2723 se rechazó o su 2991 se aplicó. Falta el dump/CC del fallo y resultados de handlers. Crow y las otras apariciones muestran un mecanismo de exposición, no una identidad de crash demostrada.
- **[E] Relación global:** no se ha demostrado que S1 cause S2 o que el hito 2322 repare S1. Tampoco que todos los actores remotos fallen: hay restauraciones correctas y acciones nativas aplicables a remotos.
- **[V] Alcance de pruebas:** se verificaron análisis estático y capturas existentes; no se hizo una nueva sesión con Shadow, no se arrancó el juego y no se probó ninguna corrección. No se presentan propuestas como resultados de una prueba inexistente.

## 9. Cierre y preservación del entorno

**[V]** Ghidra funcionó en la copia aislada, instancia `8b6e572de8cd4709b39d715df92163ab`, PID 37648, loopback 18509. Se cerró mediante su comando de apagado; el proceso terminó con código 0. La comprobación posterior no encontró `java.exe` ni `javaw.exe`; no quedó listener de Ghidra. Los estados TCP residuales de cierre no pertenecían a ningún proceso.

**[V]** Los diez archivos del proyecto original `D:\RE\CrimsonDesert_2976\project` conservaron **SHA-256, tamaño y fecha de modificación** respecto al manifiesto inicial. Se eliminó únicamente la copia temporal propia del proyecto, tras comprobar su ruta absoluta y el proceso terminado: 4.033.578.056 bytes recuperados. Se conservaron desensamblados, decompilaciones, consultas, scripts de lectura y manifiestos para reproducir la investigación.

**[V]** El repositorio estaba limpio al iniciar y seguía limpio inmediatamente antes de crear este informe. El único archivo añadido al repositorio por esta investigación es `docs/research/pendientes/p8-raiz-invitado.md`. No se realizaron commits, modificaciones de código ni operaciones en la carpeta del juego.
