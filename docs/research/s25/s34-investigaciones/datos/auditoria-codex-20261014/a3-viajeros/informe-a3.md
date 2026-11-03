# Auditoría independiente S34 — investigación 3: viajeros

Fecha: 14-10-2026. Auditor dedicado a viajeros, sin leer veredictos de otros auditores. Documento auditado: `docs/research/s25/s34-investigaciones/3-viajeros.md`. Código: instantánea exacta `d835806` extraída por el coordinador en `%TEMP%\codex-s34-auditoria-20261014\codigo-d835806`. Todas las referencias `src/...:línea` corresponden a ella, no a un HEAD móvil.

**Dictamen: aprobada con cambios como investigación; el diseño de `user2_road_control=1` no debe implementarse tal cual ni activarse aún.** Los recuentos y el contraste lejos/cerca se reproducen. El hallazgo nuevo principal es que **la marca `desc+0x3CD=1` no es exclusiva del constructor de viajeros de una petición 2550 actual**: otro productor del mismo descriptor genérico recupera `AutoSpawnOwnerData`. Además, reutilizar la limpieza actual hereda dos comportamientos contrarios a las garantías declaradas (fallo global y retorno no estricto), y continuar «en el siguiente Troc del invitado» no garantiza vaciado tras una desconexión.

No se ha compilado, desplegado, ejecutado el juego, escrito en bin64/partidas, modificado código ni realizado commits. Los únicos productos son scripts/resultados propios y este informe. Se leyó el exe en disco y se consultó Ghidra mediante su script existente en modo `-noanalysis -readOnly`. La consulta generó el export de una función nueva; no se anotó ni modificó el programa Ghidra.

## 1. Fuentes y método

Carpeta del auditor: `%TEMP%\codex-s34-auditoria-20261014\a3-viajeros`.

| Alias | Captura y log |
|---|---|
| H1b | `C:\Emulador CD\backups\p8-sesion34\paso1b\cdmp_packets_20261014-185010.bin`; log `cdmp_20261014-184959.log` en esa carpeta |
| H2b | `C:\Emulador CD\backups\p8-sesion34\paso2b\cdmp_packets_20261014-190242.bin`; log `cdmp_20261014-190232.log` |
| G1b | `C:\Emulador CD\backups\p8-sesion34\shadow-p1b-p2b\cdmp_packets_20261014-175057.bin` |
| G2b | Misma carpeta, `cdmp_packets_20261014-180517.bin` |
| Exe | `D:\SteamLibrary\steamapps\common\Crimson Desert\bin64\CrimsonDesert.exe`, build 2976, timestamp leído `0x6AB28F00`, base `0x140000000` |
| Ghidra | `D:\RE\CrimsonDesert_2976\exports\decomp\0x27ea160.c`, `0x278ea70.c`, `0x2a78ca0.c`, `0x2977e10.c`, `0x29c4930.c`; nuevo `0x28981b0.c` |

Se escribió un lector Python propio, sin importar ni ejecutar los scripts de los autores. Cabecera CDPK de 12 bytes y registro de 24 (`<QBBHHHII`) según `src/common/capture_file.h:3`; cabecera de paquete de 5 bytes y registros 2005 `[u16 longitud][registro]`. Clave y controlador en registro +1/+0x12; solicitante y carretera tras nombre UTF-16 según `npc_probe_client_logic.h:25–40`. Las colas familiares usan la heurística explícita del formato existente; no se presenta esa heurística como una demostración de todos los formatos del juego.

Posiciones: 2223 payload+0x8E, 2556+4, 2991+0x8F y 2697+4. Métrica: máximo desplazamiento X/Z desde la primera posición del intervalo, umbral >1 m. Se separa emisión C>S del controlador y recepción S>C del observador. Se omiten registros derivados marcados por el mod. La recalculación restringida a posiciones recibidas después del primer 2005 conserva el contraste principal.

Los tiempos son segundos desde el primer paquete de cada fichero. La mediana de primeras altas compartidas reproduce H1b=G1b+60,797011 s (4418 claves) y H2b=G2b+167,1700195 s (976). **No son latencias de red**: incorporan diferentes orígenes y retrasos de publicación. Las duraciones se calculan dentro de un mismo PC.

Integridad: H1b 541764 registros, H2b 190962, G1b 357882, G2b 59774. H2b registra 30 pérdidas de disco a H67,5616..67,5635 s, offsets 4170406..4199473, antes de la cohorte del invitado; no hay marcadores de pérdida en G1b/G2b. Hay paquetes iniciales con longitud declarada especial/mayor que el registro (6/3/6/1), omitidos explícitamente. No hay 2005 mal formado ni truncamientos. La ausencia de marcador no prueba que todo lo ocurrido hasta el cierre alcanzara disco.

## 2. Afirmaciones verificadas o refutadas

| Afirmación | Estado | Prueba y corrección |
|---|---|---|
| 245 actores B y 82 C con metadatos del invitado y controlador host | [V] | `recuentos.json`: B 47 tipo 3 + 10 tipo 5 + 188 tipo 6; C 23 + 4 + 55. Los 327 aparecen en ambos clientes con controlador `90100000`. Es una cohorte definida por metadatos. |
| Los 327 nacieron necesariamente de un 2550 actual | [P] | Se reproducen 242/245 y 81/82 con carretera presente entre sus 2550. Los cuatro restantes son insectos de carretera 8: B01004B0, B01004ED, B0100641 en B y B0100453 en C. No se puede inferir procedencia sólo por el 2005; véase el productor adicional 28981B0. `comprobaciones-extra.json`. |
| 3550/1110 entradas 2550 del invitado | [V] | Parser propio: 708/90 paquetes. Payload de cabecera 5 y elementos de 24 bytes, como handler `29C4930`; suma 3550/1110. Log host: 3525/1110 elementos. La diferencia de 25 en B también se reproduce. No confundir paquetes con elementos del lote. |
| 21/13 hijos tipo 7 sin metadatos | [V] | Familias de G1b/G2b: 21/13 claves fuera de la cohorte marcada, todas tipo 7. `cohortes-evidencias.json:children1b/children2b`. No demuestra que todas nazcan dentro de los dos constructores dock propuestos. |
| El host empieza a enviar IA pronto | [V] | Alta host→primera Req examinada: B mediana 145,860 ms, máximo 1113,618 ms, n=245; C 139,0185 ms, máximo 273,895 ms, n=82. No equivale a locomoción ni a acción aceptada. |
| Lejos, 139/140 B y 81/82 C no superan 1 m en origen | [V] | B termina en H261,952745 y exige alta 15 s antes. C usa toda la captura del host, que continúa tras el cierre de Shadow. Excepciones: Wagon Driver B01003D6=2,897434 m y B01004FF=2,897212 m. JSON con offsets. |
| 0/82 del invitado desplazados frente a 73/76 del host | [V] con límite | C: cero desplazamientos recibidos >1 m en 82 claves, pero sólo 78 tienen posiciones recibidas. Faltan B01004FA, B01004E9, B0100500 y B010055F. El control 73/76 sólo comprende tipos 3/6 (12/12+61/64). Hay además tres tipo 5 del host, los tres desplazados: cohorte total 76/79. Igual resultado tras restringir a después del 2005 (`posiciones-tras-alta.json`). No afirmar que la inmovilidad de los 82 esté demostrada desde Shadow. |
| El contraste de posiciones confirma contextos de simulación distintos | [V] | G2b B0100355 Stranger: 2005 registro offset 16071989; 2991 offset 20355971 t64,974562 hasta 33019606 t84,190160, X/Z idénticos. Host-requester B01001F3 Fisherman: 2005 registro 16155200; 2697 offsets 11026417 t53,201245 y 27207225 t76,273811, 23,0841 m. Hay movimiento previo a su alta retenida; la recalculación posterior conserva el resultado agregado. |
| Los 46 sobrevivientes se mueven al acercarse el host | [V] | B: 46/46, ventana H271,126643..325,397111, mismas claves/controlador. Log H1b:35716 carga, :36424 preparación, :43697 siguiente carga. `cohortes-evidencias.json:survivors1b`. |
| «El cliente host no puede iniciar locomoción lejos», como mecanismo nativo exacto | [P] | Contraste fuerte dentro de la misma sesión; sitúa el problema en el contexto de simulación/carga del controlador. No aísla navmesh, streaming, caché, predicado de IA ni efecto exacto de la preparación. El informe conserva esta duda en §5, pero su resumen suena más definitivo. No elevarlo a contrato universal. |
| El resumen del invitado mezcla viajeros propios y ajenos | [V] | `npc_probe_client.cpp:301` sólo comprueba `a.road && active`; `npc_probe_client_logic.h:40` no exige solicitante local. Los grupos medidos explican el resumen engañoso. |
| 27EA160 escribe los tres campos en raíz y acompañantes | [V] | Exe: 27EAD9B/27EADAC/27EAE24 y 27EB40A/27EB411/27EB483; el último escribe word +3CC=0x100. Constructor 278EA70 inicializa los campos a cero. `estatico.json`; decomp 27EA160:1433–1480 y :1660–1713. |
| La marca +3CD=1 es exclusiva de 2550 | [F] | Nuevo contraejemplo: 28981B0 crea descriptor 0x4E8 con 278EA70 (:347), asigna +3B8 (:380), lee AutoSpawnOwnerData (:836–872), copia +3C8 (:873) y escribe word +3CC=0x101 (:888). Instrucción RVA 28995E9: `66 C7 80 CC 03 00 00 01 01`. Es el mismo descriptor genérico; la marca puede provenir de reconstrucción de datos. El original sólo decía unicidad entre 827 exports como [H]; esa cobertura no sirve para elegir autoridad. |
| Predicado completo del descriptor corresponde 1:1 al 2005 | [NV] | La sonda S34 mide entradas 2550 y 2005 tipo 3, no el descriptor al crearlo (`user2_stages.cpp:3529,3596`). No está medido si el productor adicional satisface también req_key/req_ptr. No afirmar una asignación incorrecta ya ocurrida: se demuestra una clasificación incompleta. |
| Reutilizar mid 2792A7B es un punto válido | [V] con alcance | .rsrc, función pdata 2791CD0..2792FC8; no es entrada. Instrucción de 7 bytes `41 89 86 90 00 00 00`; AOB de 20 bytes única `41 89 86 90 00 00 00 48 8B 06 4D 8B CE 4D 8B C5 48 8D 55 67`. Sin salto directo desde la misma función al interior de esos 7 bytes. El hook existente es create_mid (`docked_gimmicks.cpp:395–405,598–615`) y debe seguir siendo único. No se certifican destinos indirectos no enumerados. |
| Una única escritura +90 al nacer elimina doble autoridad | [P] | Una clave de controlador es buena base. StartAction `2A78CA0:38–44` y MoveActor `2977E10:174–180` comparan vf170 del contexto con actor+90. No demuestra que toda ruta secundaria, gimmick o trama sintética respete la misma autoridad. Hace falta medir familia y modo local. |
| El invitado no necesita cambios en ningún módulo | [P] | `guest_actor_diag_logic.h:31–46` excluye control local; remote_motion usa modo nativo. `remote_interp.cpp:239–331` no compara +90: decide por clase B y buffer consumido; comprobar modo local antes de prometer ausencia total de efectos. `combat_follow.cpp:246,380` usa +C8=1/2 en el emisor host; medirlo en viajeros de combate. |
| Reutilizar cleanup_one tal cual mantiene el fallo sólo local | [F] | `user2_stages.cpp:1627–1637` cleanup_fault cambia g_activation_fault o g_ctl_aborted y propaga el fallo nativo; cleanup_one lo usa en :1670. Contradice §4.6 del diseño (sólo g_road_stopped y demás módulos intactos). No se debe resolver tragando excepciones del motor. |
| cleanup_one tal cual confirma retirada o ya-logouting | [F] para rechazo | :1643 default expected_parent=UINT32_MAX; :1653 local=false; :1661 llama logout_result(...,local). `npc_activation_logic.h:121–124` cuenta cualquier error desconocido como Done si strict=false. El nuevo diseño necesita resultado estricto y confirmación de desaparición. |
| 64 por llamada y próximo Troc garantiza limpieza al salir | [F] como garantía | Tras logout/desconexión puede no existir próximo Troc del invitado. ModLock ocupado o SAM indisponible (:1690–1708) puede dejar cero retirados. El censo de 2005 sin 2516 en host alcanza 91 B (H197,524213) y 81 C (H252,261192): no equivale a SAM exacto, pero 64 no cubre de una vez lo observado. Necesita continuación independiente. |
| KeyTable<4096> ya proporciona época+nacimiento | [F] | `user2_stages_logic.h:767–877` guarda clave, flags, put_us y removed_us; no época/token comprobado por mark. get combina atómicos, no una instantánea atómica. La identidad fuerte pedida en §4.2 no aparece por elegir esa tabla. |
| Activar primero raíces y dejar tipo 7 en host es seguro | [NV]; no aprobado | Hay 34 hijos tipo 7. En G2b emiten 15 Req 2556 pese a tener controlador host; emitir no prueba aceptación. No hay evidencia de seguridad para la familia con padre invitado/hijo host. No usar esta excepción como primera activación con jugadores reales. |

## 3. Contraejemplo de procedencia

`comprobar_extra.py` busca escrituras directas byte+3CD=1 y word+3CC con byte alto 1 en toda .rsrc; comprueba límites con desensamblado/pdata. Encuentra 28995E9, que el barrido de exports del autor no incluía. También encuentra escrituras sobre otras estructuras: **no** se atribuyen a descriptores sin comprobarlas.

El nuevo export `0x28981b0.c` permite seguir el mismo objeto desde 278EA70 hasta +3CC=0x101. Lee NPCScheduleCharacterSaveData/FieldCharacterSaveData y AutoSpawnOwnerData. Refuta la exclusividad de la marca y de «vtable genérica + marca». El riesgo es confundir restitución de estado con spawn nuevo pedido por el invitado. Que también pase +3B8/+3C8 en vivo sigue siendo [NV].

**Cambio obligatorio:** sonda positiva y negativa que distinga productor 27EA160, productor 28981B0 y otros, conservando descriptor→claves→2005 por tipo y familia. La decisión de autoridad no puede apoyarse sólo en una prueba de carretera que pase. Esta auditoría no propone instalar otro hook sin su comprobación completa ni autoriza escrituras.

## 4. Cambios obligatorios del diseño

1. **Unidad familiar completa.** O la unidad completa tiene procedencia y control medidos, o mantiene su autoridad actual. Una guarda fallida, tabla llena, hijo tardío o tipo 7 excluido no debe dejar padre invitado/hijo host. Distinguir bandadas, carros, caballos, monturas, mascotas, compañeros y NPC de misión; «tipo 6» no es un contrato de IA. Fuentes: 34 hijos tipo 7 y guardas de `3-viajeros.md:144–158,181–186`.
2. **Registro antes de autoridad.** Reservar un registro de época/token de nacimiento antes de cambiar EAX; no asignar y luego descubrir que put falló. Publicar estado coherente y revalidarlo en limpieza. El predicado propuesto permite session_key2=0 (:157): primero medir y cerrar el contrato de identidad de sesión; no tratar desconocido como coincidencia comprobada. Probar las lecturas en probe conforme a CLAUDE.md.
3. **Limpieza estricta y aislada.** Separar su estado de fallo del callback compartido, sin ocultar excepciones nativas. Comprobar out=0 o ya-logouting; otros errores quedan pendientes/rechazados. Mantener ActorScope y realm del servidor. ModLock no reemplaza el contrato del motor; definir orden respecto a familia/publicación y reentrada Logout→on_logout.
4. **Cancelar publicación pendiente.** Coordinar child_hold, observer_hold_acks, altas 2005 y acciones encoladas antes de retirar. Un paquete anterior no debe resucitar al actor después de 2516. Tombstones por época+nacimiento; no heredar retirada antigua al reutilizar clave. El diseño de viajeros omite esta coordinación explícita, que la contención S33 ya necesitó.
5. **Continuación independiente del invitado.** En carga/logout/desconexión: detener nuevas asignaciones de esa época, conservar identidad de sus unidades y continuar la limpieza en contexto servidor aunque no llegue otro Troc suyo. Una tarea acotada que agota reintentos debe quedar visible como pendiente/fallida, no borrar el registro como si el actor hubiera desaparecido.
6. **Req no es doble control demostrado.** Medir autor, resultado y efecto de las operaciones con validación de controlador. Los hijos tipo 7 observados ya emiten peticiones desde el observador. Una Req host es indicio; no basta para diagnosticar dos autoridades efectivas. Separar movimiento, acción, IA, gimmick y daño.
7. **Misiones del anfitrión.** Cambiar quién simula IA no autoriza reencaminar progreso al invitado. AutoSpawnOwnerData puede reconstruirse desde estado escénico; los metadatos de carretera no excluyen por sí solos esa ruta. Medir crédito de interacciones/muertes y objetivos del host, sin progreso personal del invitado.
8. **Seguridad y visibilidad son criterios distintos.** WithheldUser2 evita la liberación restore 0 de su rama, pero no demuestra seguridad de todos los lectores ni que una bandada llegue a ser observable. Un animal retenido para siempre puede ocultar parte de una familia al host. Exigir publicación segura funcional o excluir la unidad completa; cero cierres no basta.

### Ciclo de vida

| Situación | Requisito previo a activar |
|---|---|
| El host se acerca o se marcha y el invitado sigue cerca | Mantener controlador invitado al nacer es razonable [P] y evita un traspaso vivo no demostrado. Medir familia, restauración, observador y combate. La llegada del host por sí sola no justifica drenar viajeros. |
| Viaje rápido/cambio de campo del invitado | Cancelar publicación y retirar sus unidades de la época anterior con resultado verificable, preservando orden para ambos clientes. |
| Logout o desconexión abrupta | Continuación en servidor tras abandonar tareas user2; la propuesta «64 + siguiente Troc» no cierra este caso. |
| El invitado se aleja cabalgando sin carga | [NV] esencial: el nuevo controlador también puede quedar lejos. Medir vida, distancia y competencia; no inventar radio ni reutilizar 10 celdas de U-1 sin medida. |
| Cambio de propietario, montura o mascota | Revalidar owner/controlador/familia/nacimiento antes de retirar; no aplicar política antigua a una nueva relación. |
| NPC de misión o restaurado | Separar 27EA160 de 28981B0 y otros productores; cohorte negativa obligatoria. No asumir NotRoad sólo por vtable/marca. |

## 5. Pruebas mínimas y orden

Estas son propuestas; **no se han ejecutado**.

**Puras:** predicado con productores positivos/negativos incluyendo 28981B0; tabla llena antes de asignar; familias 3/5/6/7; hijos tardíos/faltantes; sesión desconocida; claves reutilizadas y cambio de época; Logout rechazado; lectura fallida; ModLock ocupado; 2005 mixtos y acciones canceladas; logout con 65, 91 y 128 actores sin más Troc del invitado; desconexión entre selección y ejecución. Propiedades: ninguna autoridad sin registro, ninguna familia a medias, ningún paquete antiguo revive una baja, rechazo nunca cuenta como éxito.

**Un PC + cdmp_peer, probe:** descriptor→familia→2005 de viajeros del host y negativos restaurados/escénicos; contadores de lectura, correlación y pérdida de anillo; salida/reconexión y continuación pendiente. El peer no ejecuta IA: no valida locomoción, bandadas, restauración segura ni presentación de misiones.

**Shadow, probe:** repetir lejos/cerca/lejos con cohorte identificada; registrar productor, familia completa, controlador, modo +C8, primer Req/resultado/desplazamiento y muestras faltantes. Incluir andar/volar, carro/caballo, hostiles, alejamiento sin viaje rápido, acercamiento del host y desconexión abrupta.

**Activación posterior, acotada:** sólo unidades cuyo contrato esté cerrado. No activar padre de caravana dejando hijos excluidos. El criterio «≥90% envían Req en ≤3 s» no es suficiente: S34 ya presenta Req rápidas sin locomoción. Exigir paridad de registro/control/familia del 100%, ninguna asignación fuera de alcance, movimiento donde el comportamiento lo exige, publicación segura y ordenada, retirada confirmada y progreso exclusivo del host. Medir primero la distribución por cohorte antes de usar un límite temporal como aborto.

**Orden recomendado:** (1) instrumentación correcta de autoridad y publicación familiar segura; (2) procedencia real, identidad y limpieza terminal; (3) sonda de viajeros por familias completas; (4) activación de cohorte verificada; (5) bandadas, caravanas, combate y salidas. Misiones/guardado siguen siendo invariantes, aunque se desarrollen por separado.

## 6. Reproducción y productos propios

```powershell
$py='C:\Emulador CD\build\research\venv\Scripts\python.exe'
$a='%TEMP%\codex-s34-auditoria-20261014\a3-viajeros'
& $py -I -B "$a\auditar_viajeros.py"
& $py -I -B "$a\auditar_pe.py"
& $py -I -B "$a\comprobar_extra.py"
& $py -I -B "$a\comprobar_ventana.py"
```

| Script/producto | Función |
|---|---|
| `auditar_viajeros.py` | Lector CDPK propio; cohortes, control, movimiento, latencias, alineación y familias. Produce `recuentos.json` y `cohortes-evidencias.json`, con offsets de paquete y registro, posiciones primera/pico/última, controlador y retiradas. |
| `auditar_pe.py` | Lector PE propio + Capstone; timestamp, secciones, exception table, instrucciones y unicidad AOB. Produce `estatico.json`. |
| `comprobar_extra.py` | Usa sólo los dos lectores propios; elementos 2550, carreteras, picos de actores anunciados sin baja, anclas de log y escrituras directas 3CD. Produce `comprobaciones-extra.json`. No prueba ausencia de escrituras por alias/copia/.xtls. |
| `comprobar_ventana.py` | Recalcula movimiento S>C después del primer 2005. Produce `posiciones-tras-alta.json`. |
| `ghidra-0x28981b0.c` | Copia del decompilado obtenido con `powershell -NoProfile -ExecutionPolicy Bypass -File D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1 decomp 0x1428981B0`. El wrapper ejecutó headless de sólo lectura. |

Límites: no se auditan aquí las pilas de los volcados; no se identifica aún el predicado de navegación; no se afirma impacto en misiones de un cambio no implementado. Capturas demuestran tráfico y posiciones observadas, no toda la presencia ni estado de acción en memoria. La investigación de viajeros queda cerrada para consolidación; no se ha implementado ningún arreglo.
