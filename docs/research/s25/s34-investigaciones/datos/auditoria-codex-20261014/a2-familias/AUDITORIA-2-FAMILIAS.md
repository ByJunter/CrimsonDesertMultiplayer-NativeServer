# Auditoría independiente 2 — Familias NPC (S34)

Fecha 2026-10-14. Sólo lectura de fuentes y análisis propios; sin compilación, implementación, despliegue, juego, partidas ni commits. Base de código fijada en `d835806`; no se asume que HEAD sea ese commit (HEAD leído: `f79545c0f18f933ca0f9e185f60455b1bcc38501`). Investigación auditada: `2-familias-npc.md` (388 líneas). Este informe no ha usado dictámenes de otros auditores.

## Dictamen

**Aprobada con cambios importantes como investigación; el diseño `npc_family_control=1` no está listo para implementarse literalmente.** Se reproducen las autoridades partidas, los animales tardíos y el vínculo descriptor-padre en las rutas legibles. Hay errores de censo, afirmaciones universales apoyadas sólo por muestras tipo 3, y huecos de seguridad en el diseño: reserva incompleta de familias, marco TLS que pierde el contexto exterior, `NoCc` confundido con lectura fallida, retirada no necesariamente de hojas a raíz y enlaces familiares sin generación del padre.

La herencia familiar al nacer es una dirección razonable para una prueba posterior, pero la seguridad de tipo 6 en el invitado permanece **[NV]**. El informe original lo reconoce en §6, por lo que su «Sí» en §3.5 no debe leerse como autorización técnica para activar directamente.

## Fuentes y reproducción

- Repositorio: `C:\Emulador CD-npc-s33-fix`; código exacto consultado mediante `git show d835806:<ruta>` y copiado a `d835806-*.cpp/.h` en esta carpeta temporal.
- Logs/capturas: `C:\Emulador CD\backups\p8-sesion34`, subcarpetas del índice original. En este informe A/H = `cdmp_20261014-180308.log`; A/G = `shadow\cdmp_20261014-170438.log`; B/H = `paso1b\cdmp_20261014-184959.log`; B/G = `shadow-p1b-p2b\cdmp_20261014-175039.log`; C/H = `paso2b\cdmp_20261014-190232.log`; C/G = `shadow-p1b-p2b\cdmp_20261014-180502.log`.
- BIN A/G = `shadow\cdmp_packets_20261014-170455.bin`; B/G = `shadow-p1b-p2b\cdmp_packets_20261014-175057.bin`; C/G = `shadow-p1b-p2b\cdmp_packets_20261014-180517.bin`.
- EXE: `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`, abierto exclusivamente rb/mmap ACCESS_READ. El lector propio confirma timestamp `0x6AB28F00`, ImageBase `0x140000000`.
- Ghidra: exports existentes `D:\RE\CrimsonDesert_2976\exports\decomp\0x<RVA>.c`. Se contrastaron las instrucciones significativas con los bytes del exe usando Capstone; no se modificó el proyecto Ghidra.
- Scripts propios: `censo_independiente.py`, `log_independiente.py`, `estatico_independiente.py`, `resumen_estatico.py`, `latencia2223.py`, `resumen_evidencia.py`, `rtti_independiente.py`, `contraejemplos_diseno.py`. No importan scripts de los investigadores. El primero reconstruye el formato CDPK desde `src/common/capture_file.h:4-13`, cabecera Troc de 5 B y registros 2005 de longitud u16; enumera candidatos a la cola familiar y señala ambigüedades. La cola familiar sigue siendo una identificación por formato/consistencia, no una decompilación completa del serializador.
- Comando por script: `& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<carpeta>\<script>.py'`. Primero censo, después log y latencia; estático y contraejemplos son independientes.
- Resultados: `A-censo.json`, `B-censo.json`, `C-censo.json`, `censo-log-vs-bin.json`, `estatico.json`, `estatico-disasm.txt`, `rtti.json`, `contraejemplos_diseno.json`. Cada actor conserva offset absoluto de registro/paquete, tiempo y controlador; las parejas de nacimiento conservan líneas de log.

## Censo reproducido

Definición inequívoca: raíz = tipo 3, controlador user 2, padre cero. Familia completa = todos los descendientes anunciados por la cola familiar se recibieron y todos tienen controlador user 2. Partida observada = al menos un descendiente recibido con controlador distinto. Pendiente = algún descendiente nombrado nunca se recibió. Se recorre transitivamente, sin volver a contar a un nieto como raíz.

| Ensayo | Raíces | Con hijos | Completas user 2 | Partidas observadas | Pendientes | Hijos tipo 3/user 2 | Tipo 6/host recibidos | Tipo 7/host recibidos |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| A | 220 | 36 | **32** | 3 | 1 | 44 | 2 | 1 |
| B | 202 | 24 | **22** | 2 | 0 | 26 | 1 | 1 |
| C | 304 | **48** | **39** | 8 | 1 | 57 | 4 | 6 |

**[V]** Reproducido tanto desde los BIN como desde las líneas `recibido-tunel` con dos lectores propios. **[F] tal como están rotulados como raíces/familias enteras; diferencia de definición si el autor quería contar otra cosa.** Los valores originales 33 familias enteras en A, 49 con hijos y 41 enteras en C no se reproducen con la definición explícita anterior. No se rechazan como imposibles recuentos de nodos padre o familias de controlador todavía desconocido; se rechaza presentarlos como familias raíz completas. A incluye una familia incompleta en «enteras» si sólo resta partidas. En C hay además un nodo tipo 3 que es padre y a la vez hijo (`B0100474`, bajo `B0100473`), por lo que contar todos los padres en lugar de raíces altera el denominador. No afirmar «completa» sólo porque no se recibió todavía ningún miembro de otra autoridad.

Los BIN contienen A=42.607, B=357.882, C=59.774 registros CDPK; no se encontraron registros de pérdida ni paquetes truncados. Hay 1/2/1 registros NPC sin cola familiar identificable, y cero colas ambiguas; esos registros no se convierten en evidencia de ausencia de familia. Ningún miembro de tipo 4/5 cuelga de estas raíces observadas; esto no demuestra ausencia universal.

## Tabla de afirmaciones

| Afirmación del informe original | Dictamen | Evidencia y corrección |
|---|---|---|
| Raíces user 2 con animales tipo 6 controlados por host en A/B/C | **[V]** | `*-censo.json`: A `B010049D→49E`, `B01004A4→4A5`; B `B01003AD→3AE`; C `B010048E→48F/490`, `B01004BE→4BF/4C0`. Controladores de los miembros `90100000`, padres user 2. |
| 3/2/8 familias partidas recibidas | **[V]** | Censo BIN y log propio; faltan además los hijos A `B01003CD`, C `B0100577`. No son familias completas ni puede verificarse su controlador desde un 2005 que no llegó. |
| 4/4 raíces arrancan después del último animal | **[V] temporal / [P] causal** | Desde entrega BIN: primera petición cualquiera +133,554 / 147,275 / 177,973 / 202,671 ms; primera 2223 +145,987 / 168,873 / 190,414 / 215,294 ms. El original mide desde recepción del túnel (unos ms antes de entrega al juego). No mezclar fases. Las muestras `estado` muestran hijos-máscara 0→completa con cerebro/stage ya presentes: A/G:2818,2824,5686,5687; C/G:5171,5786,6603,6940. Correlación fuerte, pero no se identificó en este estudio el predicado exacto del cliente que espera la familia. |
| La seguridad de asignar animales tipo 6 está demostrada | **[NV]** | Ninguno de estos animales se recibió con controlador user 2. `0x27A6160` usa la misma infraestructura NPCSchedule, pero eso no valida IA, combate, acciones y baja en otro cliente. Requiere Shadow. |
| `[desc+2F0]` enlaza el descriptor padre y `+2F8` guarda envoltorios de hijos | **[V] en rutas enumeradas** | Constructor base `0x278EDD1/EDD8/EDDF`; horario `0x281DE14→281DE73`, grupo `0x282120D→282126E`, más tabla estática siguiente. No se propone enganchar `.xtls`. |
| Todos los constructores citados están comprobados | **[P]** | Se confirman los que tienen ruta concreta. `_Rider` y `_NPCScheduleGroupActionMember` no tienen constructor/ruta demostrada en el informe; no extrapolarles el contrato. |
| 0 de 878 nacimientos tiene padre | **[V] muestra / [F] alcance universal** | 264+228+386 líneas nacimiento, todas tipo 3 seleccionadas por U-e. `npc_observe_birth`, d835806 `user2_stages.cpp:1195-1212`, descarta actores no asignados sin padre rastreado; no es un censo de todos los tipos. Es válida la necesidad de buscar otro vínculo para esos 130 hijos tipo 3 observados, no «878 nacimientos de cualquier clase». |
| Padre antes que hijo, mismo hilo y mismo Troc siempre | **[P]** | 44/26/60 parejas con ambos nacimientos registrados: padre primero, mismo hilo; diferencias 34–917 / 34–358 / 37–243 µs. No existe ID de Troc por nacimiento que demuestre el mismo marco ni cobertura de tipo 6/7. `0x281DBD0` prueba construcción de descriptores anidados; creación de actores pasa por `.xtls 0x10649310`, insuficiente para un universal. |
| `0x27A7110→.xtls1010EEC0` demuestra escritura de actor+AC | **[P]** | El decompilado de `1010EEC0` consultado configura ramas de gimmicks y llamadas; no muestra esa escritura. La lectura posterior del padre está demostrada en logs, pero el punto exacto de escritura necesita su propia fuente. |
| «not type 3» implica hijo de la propia stage | **[F]** | `ctl_verdict`, d835806 `user2_stages_logic.h:710-724`, no examina descriptor-padre ni actor+AC: puede ser raíz tipo 6. Las exclusiones previas son dueño/controlador/campo, no parentesco. |
| Tipo 7 descriptor `5B04C68` es InstantGimmick; no leer +4E8 como nodo | **[V]** | Constructor `.xtls100F8790` llama `278EA70` y escribe vtable `145B04C68`, byte +4E8=8. `27A4A40` llama `2791CD0`; éste tiene rama tipo 7 y `27A1430`. En d835806 `user2_stages.cpp:1218` exige vtable NPCSchedule antes de leer handle. |
| Los tipo 7 hijos «no son IA» y son seguros por NoCc | **[P]/[F] como garantía** | Los 8 hijos recibidos en familias partidas no retrasan la raíz; conservar autoridad host es prudente. Pero `restore_byte_with` devuelve nullopt también por lecturas fallidas, componente/state ilegible o backlink distinto (`child_hold.cpp:62-69`), y `NoCc` se asigna a todo nullopt (`:146-149`). No prueba ausencia de CC ni seguridad universal. |
| Hook compartido `2792A7B` es un punto válido | **[V] estático** | `.rsrc`, dentro de pdata `[2791CD0,2792FC8)`, instrucción de 7 B `41 89 86 90 00 00 00`. Firma 16 B única `41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5`; no salto directo interior en la función. Uso correcto: `create_mid`, nunca inline. Código d835806 `docked_gimmicks.cpp:395-403,589-620` mantiene un solo propietario. |
| Progreso OnServer siempre tiene emisor cero | **[P] en esta auditoría de familias** | Se verifica la lectura incorrectamente tipada en d835806 `npc_action_probe.cpp:40-41` y la condición `Table::progress`; el recuento exhaustivo y corrección semántica corresponden a auditoría 4. No tomar el capítulo lateral como autorización para sustituir emisor por controlador en toda rama sin revalidar contrato. |
| Excluir tipo 4 garantiza que no se afectan misiones | **[NV]** | El tipo no acredita todos los papeles de misión. Deben comprobarse eventos/crédito de acciones contra gestor del anfitrión. Se mantiene la regla de mundo y misiones del host; no introducir gestores del invitado ni cambiar dueño de usuario para obtener autoridad IA. |

## Direcciones de enlace comprobadas directamente

Todas estas rutas de enlace están en `.rsrc`; los sitios de escritura no son entradas de función y se citan sólo como evidencia, no como nuevos hooks.

| Función/ruta | Escritura inner+2F0 | Inserción +2F8 |
|---|---|---|
| `281DBD0` Simple horario | `281DE14` | call `281DE73→43DB70` |
| `2820D80` Simple grupo | `282120D` | `282126E→43DB70` |
| `2799C30` CatchTarget/Simple | `279A8F8`, `279A9E5`, `279BD2D`, `279CC5A`, `279D62B` | `279AA4F`, `279BD6A`, `279CC97`, `279D668` → `43DB70` |
| `278BBB0` Simple | `278CB97` | `278CBCA→43DB70` |
| `27A0DB0` AutoSpawnGimmick | `27A1317` | `27A1357→43DB70` |
| `279D910` CombinationGimmick | `279FAD0` | `279FB03→43DB70` |
| `279FCA0` SavedAttached | `27A013D` | `27A017D→43DB70` |
| `2797510` LinkedVehicle | `27979AC` | `27979F4→43DB70` |
| `288CFD0` Docking | `288D176` | `288D1AD→43DB70` |

`estatico-disasm.txt` conserva los contextos de instrucciones. Los puntos `27A1317`, `279FAD0`, `27A013D`, `27979AC` no son funciones distintas: el pdata los sitúa en sus funciones de la primera columna. «Estos constructores sólo se referencian desde tablas .xtls» no vale literalmente para todas las funciones enumeradas: por ejemplo `281CEE0` llama directamente `281DBD0` en `281D929` (Ghidra `0x281cee0.c:394-396`).

## Cambios obligatorios al diseño antes de implementar

1. **Reserva familiar completa antes de asignar la raíz.** El diseño sólo prueba `n==64` al entrar y reserva un elemento. Contraejemplo: 63 registros ocupados, raíz con un hijo; la raíz se asigna y el hijo ya no cabe. Debe reservarse el subárbol completo en todos los registros implicados (TLS, KeyTable, activación) antes de escribir EAX, o rechazar la unidad completa. Un límite de vector/profundidad debe medirse en probe; límite alcanzado nunca puede producir asignación parcial.
2. **Marcos TLS anidados reales.** El único `t_fam` de §3.3 se reinicia al entrar en un Troc anidado y otra vez al volver: pierde el padre exterior. Los hijos posteriores pueden quedar en host con raíz guest. Usar pila de marcos o ámbito de lote comprobado; toda pérdida de vínculo después de asignar la raíz exige contención local coherente, no «queda como hoy».
3. **Vincular por nacimiento del padre/raíz, además de clave.** `Table::family_into` sólo usa parent-key y epoch (`npc_activation_logic.h:393-414`). Un hijo superviviente de una encarnación anterior puede incorporarse a una nueva raíz con la misma clave en la misma época. La propia identidad birth de cada miembro no basta. Guardar y revalidar parent_birth/root_birth o token de unidad; no derivar familia sólo de claves cuando se retira. `contraejemplos_diseno.py` reproduce el fallo lógico de selección sin ejecutar el mod.
4. **Distinguir «sin CC demostrado» de «lectura fallida/inicialización incompleta».** Cambiar el contrato de lectura a estados distintos antes de autorizar la excepción NoCc. El diseño actual hereda un camino fail-open que contradice «ningún host-kept con CC y restore 0». No desplegar esa supuesta garantía como suficiente.
5. **No dejar las raíces tipo 6/7 fuera del registro de seguridad.** §3.5 dice que serán host-kept, pero §3.4 sólo crea esa marca después de encontrar un padre registrado. Definir explícitamente el alta de raíces rechazadas por tipo/descriptor y su generación; de otro modo siguen entrando por Timeout/Native sin esta protección.
6. **Rechazo de la unidad, no sólo del miembro.** Preexaminar sólo vtables de envoltorio/interior no demuestra el tipo del actor futuro: un `_Simple+NPCSchedule` cuyo tipo final sea distinto de 3/6 puede pasar el preexamen. Capacidad, vínculo perdido, lectura fallida o tipo inesperado deben llevar a una política coherente de unidad; no asignar raíz y después dar hijos a host sin un estado explícito de retirada/recuperación.
7. **Baja postorden en todas las salidas.** Dos pasadas «miembros y raíces» de §3.8 no ordenan nietos antes de sus padres miembros. Existe profundidad 2 en C (`B0100473→474→475`). Utilizar el mismo orden de familia/topológico para contención, drenaje, logout y desconexión, con revalidación de generación y gestión explícita de retirada parcial. No asumir que bajar al padre libera siempre cualquier accesorio.
8. **No usar progreso de la raíz como garantía ilimitada de salud de hijos.** Evita vencimientos independientes injustificados, pero una raíz activa puede tener un hijo perdido definitivamente. Distinguir espera de familia, hijo autónomo y accesorio sin IA; diagnosticar familia incompleta persistente y resolver por unidad sin retirar una familia sana por un reloj fijo. No exigir primera Req de cada gimmick.
9. **La retención requiere salida funcional.** Host-kept con CC/restore0 puede quedar invisible para siempre; el código ya documenta animales tipo 6 que envían acciones pero siguen serializando restore0 (`child_hold.cpp:622-627`). Es razonable impedir publicación insegura, pero no venderla como solución completa: medir primer ACK aplicable/estado de inicialización, retirar y reponer coherentemente cuando toque, y contar retenidos sin límite. No liberar por timeout sólo para vaciar tablas.
10. **Cerrajos y hilos:** conservar el único mid. Las lecturas nuevas deben ser protegidas y el paseo acotado; SEH no da consistencia ni vida útil a un descriptor. Registrar token de hilo/Troc/lote y probar ventanas contra drain/cleanup. `npc_observe_birth` ya toma `ActivationLock` exclusivo (`user2_stages.cpp:1223`, SRW en `:338-347`); no describir toda la ruta como sólo atómicos/POD sin contabilizar este lock ni introducir inversión con la barrera de publicación. No mantener locks del mod durante llamadas nativas.
11. **Misiones/propiedad:** conservar `actor+0xA0` y `desc+0x40`; autoridad IA sólo en la rama comprobada del dueño de campo. Las exclusiones de monturas, docking y guardado son sensatas, pero deben propagarse a todo el subárbol. Verificar explícitamente que acciones del invitado acreditan al anfitrión y nunca alteran misiones propias; el tipo 4 excluido no prueba ese resultado.

## Pruebas mínimas y orden

1. Primero corregir y medir la señal de progreso (auditoría 4) y la política segura de publicación (auditoría 1). Ninguna prueba de autoridad familiar debe apoyarse en `emisor=0` ni en la etiqueta ambigua NoCc.
2. Añadir sólo sonda del vínculo descriptor en el mid existente: key, birth, descriptor, descriptor-padre, parent-key/birth cuando exista, hilo, Troc-id/profundidad, tipo, vtables interior/envoltorio, pertenencia y resultado de reserva familiar. Los punteros permanecen locales al proceso y al marco; no cruzan red.
3. Un PC/cdmp_peer puede verificar construcción, vínculo, capacidad, tiempos del servidor, retirada, colas, nodos/reintentos y salidas. No puede certificar IA de un cliente que no ejecuta, ni que los animales caminaban o el observador no se cierra.
4. Tests puros futuros: reserva 63+familia2 y varios grupos por Troc; 64/65 y profundidad 3/4 con estadísticas; Troc anidado y retorno; lectura fallida vs CC ausente; pariente de clave reutilizada con distinto birth; árbol profundidad 2/3 y baja postorden; padre retirado y alta hija tardía; familia parcialmente publicada; desconexión/viaje/epoch; rechazo por montura/vehículo/docking/misión; miembro que no manda Req pero raíz sí.
5. Shadow, activación gradual: una familia guardia-perro, niño-perro, pastor-cabras/ovejas; registrar todos los 2005/controladores y primeras acciones; afirmar éxito sólo con familia íntegra, IA funcional, sin acciones duplicadas, sin publicaciones inseguras y sin huérfanos. Probar llegada/salida del anfitrión, viaje rápido y salida brusca del invitado; comprobar reaparición nativa y cero restos de la generación anterior.
6. Mantener tipo 7 con host es una elección prudente para la primera etapa sólo si se ha demostrado el contrato de publicación de cada clase observada. Para tipo 7 con CC o lectura desconocida, no usar el atajo NoCc. Medir accesorios visibles/interactuables y si la raíz depende de ellos; un accesorio invisible de misión no es un éxito aceptable.
7. Si la unidad no puede demostrar coherencia antes del nacimiento, alternativa más simple para la primera etapa: conservarla íntegra bajo su autoridad nativa y marcarla no soportada; no prometer que eso arregla el bucle del host lejano. La solución completa requerirá una política coherente de creación/control/publicación y recuperación, no ampliar únicamente los timeouts.

## Límites explícitos

No se auditó aquí la pila completa de los volcados (tema 1), ni todos los recuentos de contención (tema 4). La atribución original de RSI a `B0100578` debe contrastarse en esa auditoría; no se utiliza como premisa para recomendar autoridad de gimmicks. El contrato global de creación en `.xtls10649310` no se eleva a garantía desde pseudocódigo; los hooks propuestos permanecen en `.rsrc`. No existe evidencia de IA de tipo 6 controlada por Shadow en las cohortes A/B/C examinadas.

## Comprobación final de RTTI

`rtti_independiente.py` lee directamente COL y TypeDescriptor del PE, no el CSV del investigador: `5B04C68 → COL 5F78098 → TD 6BDA828 → nombre 6BDA838 .?AVCreateServerActorDesc_InstantGimmick@pa@@`. Confirma los ocho slots publicados. También confirma las nueve vtables de envoltorios de la tabla; `rtti.json` conserva las cadenas y slots. No demuestra por sí solo seguridad de ejecución.

## Muestras binarias concretas

Offsets absolutos del BIN, no RVA. `r` = comienzo del registro de actor tras longitud; `p` = cabecera CDPK del paquete.

| Ensayo/raíz | Alta raíz r | Hijo(s), alta r | Primera 2223 p | Retardo desde último hijo a 2223 |
|---|---|---|---|---|
| A/B010049D | 0x14d24c5 | B010049E @ 0x1a24405 | 0x1a34374 | 145.987 ms |
| A/B01004A4 | 0x14ddf8b | B01004A5 @ 0x1a22a00 | 0x1a3582e | 168.873 ms |
| B/B01003AD | 0x15ce36e | B01003AE @ 0x206dd17 | ninguna | — |
| C/B010048E | 0x17bc38e | B010048F @ 0x1ef2a45, B0100490 @ 0x1ef0e1b | 0x1f06acf | 190.414 ms |
| C/B01004BE | 0x1960749 | B01004BF @ 0x20480cd, B01004C0 @ 0x2049abe | 0x2065971 | 215.294 ms |

La comparación de tiempos se hace dentro de cada PC: no resta directamente QPC de anfitrión y Shadow ni usa la hora de escritura del logger como hora del evento. Las familias se cruzan entre PC por claves de servidor; nunca por punteros.
