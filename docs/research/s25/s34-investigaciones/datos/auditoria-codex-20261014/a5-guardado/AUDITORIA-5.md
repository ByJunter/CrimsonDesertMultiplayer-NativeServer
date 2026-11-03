# Auditoría independiente S34-5 — Guardado del anfitrión

Fecha: 14-10-2026. Dictamen: **APROBADA CON CAMBIOS para explicar el mecanismo y construir `sandbox/probe`; NO aprobada todavía la activación de `real` con el diseño §5 tal como está escrito.** M1 y una M2 ampliada son condiciones de salida, no recomendaciones para después de publicar partidas reales.

Este informe se emitió sin leer dictámenes ni carpetas de otros auditores. No se ejecutó juego, peer, host, compilación ni pruebas con partidas; no se leyó la carpeta real de partidas ni `bin64` salvo el EXE autorizado. Solo scripts propios, logs archivados, código fijado, consultas decompiladas de lectura y documentación pública oficial. No hay conclusiones de validación en vivo.

## Referencias y alcance

- `INV`: `C:\Emulador CD-npc-s33-fix\docs\research\s25\s34-investigaciones\5-guardado-anfitrion.md`.
- `SRC`: instantánea `%TEMP%\codex-s34-auditoria-20261014\codigo-d835806\`, código `d835806c46da1cc3ec9ad3e5367dc0d1feb95db9`. Las citas `src/...` de este informe son de esa instantánea.
- `A`: `C:\Emulador CD\backups\p8-sesion34\cdmp_20261014-180308.log`.
- `B`: `C:\Emulador CD\backups\p8-sesion34\paso1b\cdmp_20261014-184959.log`.
- `C`: `C:\Emulador CD\backups\p8-sesion34\paso2b\cdmp_20261014-190232.log`.
- `OWN`: `%TEMP%\codex-s34-auditoria-20261014\a5-guardado\`. Decompilados `OWN/decomp-<RVA>.txt`; `1871930` se leyó de `D:\RE\CrimsonDesert_2976\exports\decomp\0x1871930.c`.
- EXE: build 1.0.0.2976, timestamp leído `0x6AB28F00`, ImageBase leído `0x140000000`. `OWN/pe-check.json` contiene offsets de disco, bytes, pdata, firmas y llamadas directas.
- Se leyeron primero `CLAUDE.md`, README y PROMPT-AUDITORIA; contexto pertinente: `vision-a3-guardado-mundo.md`, `noche/progreso-invitado.md`, `pendientes/p8-guardado-desviado.md`. No se aceptan como prueba primaria las etiquetas [V]/[E] de esos documentos.

## Resumen ejecutivo

**La explicación técnica de la pérdida entre sesiones es correcta.** `prepare_sandbox` borra el `_prev` antiguo, mueve la caja actual a `_prev` y copia otra vez la carpeta real antes de instalar los hooks. El guardado de una ejecución desaparece al segundo arranque exitoso posterior si nadie lo exportó. Los tres logs confirman instalaciones con una copia nueva de 7 archivos, 3 carpetas y 2.686.266 bytes. No es una carga deliberada de la caja antigua.

**No queda demostrada toda la narración histórica.** El A archivado termina a las 18:10:02, antes de los supuestos autoguardados 18:21/18:36. Los dos eventos y el inventario real del 3 de octubre son [NV] bajo este encargo; no se debe presentarlos como reproducidos. B y C sí muestran cero `SavePlayerToSlot` del anfitrión, una llamada del invitado remapeada a 204 y un `Flush` del anfitrión sin pendiente. El log C también contiene ese `Flush` del anfitrión, omitido en su fila resumida de INV.

**La publicación propuesta abre una frontera nueva y delicada.** TLS + `CreateFile` + renombrado + cabecera/SHA no prueba por sí solo que los dos archivos pertenezcan a una transacción completa del anfitrión ni que sus componentes no hayan incorporado estado global del invitado. Falta definir la exclusión mutua hasta la instantánea, las generaciones de contaminación, la identidad positiva estricta, la procedencia del guardado diferido y una recuperación duradera con fallos en cada paso. La prueba M2 basada solo en paquetes y un precio/hora visibles deja fuera estado no emitido y `_currentRestoreCodeVersionXXX`.

**Recomendación:** primero exportar generaciones inmutables del anfitrión fuera de la real y observar con `probe`; después cerrar aislamiento semántico y recuperación; publicar a la real solo tras cumplir estas condiciones. No quitar el remapeo a 204 ni la caja.

## Tabla de afirmaciones

| Afirmación auditada | Clase | Evidencia y alcance |
|---|---|---|
| La caja se rehace de la real cada arranque; `_prev` solo retiene una ejecución. | [V] | `save_hooks.cpp:571-606`: borrado previo 577, renombrado 581, copia 591. Preparación anterior a hooks 986-1005. Logs A:108, B:106, C:108. |
| Todo guardado interceptado bajo `save` se desvía, incluidas lecturas/consultas, y no hay publicador host. | [V] | `save_hooks.cpp:364-411,611-960,1005-1027`; `session2.cpp:855-858`. Alcance: APIs instaladas dentro del proceso, después de instalación; no es prueba universal sobre procesos externos/handles preexistentes. |
| Los host autosaves de A fueron a 0 y 2 a las 18:21/18:36 y fueron borrados a las 19:02. | [NV] | A archivado: 19.841 líneas, 18:03:08.502–18:10:02.000; no abarca esos eventos. Único archivo con ese nombre bajo `backups` según `rg --files`. El mecanismo de borrado sí es [V]. |
| La carpeta real solo tiene slot1 del 03-10 y slot108; no queda progreso más nuevo en ninguna caja. | [NV] | No hay inventario primario fechado exportado que pueda reproducirse sin leer partidas; listado de INV:34-47 es afirmación del autor. Tampoco se comprobó ausencia en copias manuales. |
| B/C no hicieron `SavePlayerToSlot` del anfitrión. | [V] | Parser propio: 0/0 entradas host en las ventanas archivadas; totales periódicos `saves 1 (user 2 1)` en B:52038, C:19260. Hay una entrada guest en cada log. |
| Guest guardó a 204 y sus flush no tenían pendiente. | [V] | A:14942-15018, B:50592-50783, C:17757-17839: slot100→204, resultado0, export gen24/25/26, `pending -1`. Confirma export local; no afirma por sí solo que Shadow ya aplicó la generación. |
| B/C solo tienen flush host de salida, sin E/S de guardado. | [V] | B:51532 y C:19466: `pending -1`, resultado0. `0x1871930.c` devuelve0 directamente en opción8 + pendiente−1; no llama CommitSlot en ese camino. La columna C «ninguno» debe matizarse si se refiere a todas las llamadas. |
| `guest_own_save=1` evita el `SavePlayerToSlot` de user2 en ranura host. | [V] | `guest_save_logic.h:595-618`, `guest_save_host.cpp:870-909`: guest→204 solo si healthy y same_login; indeterminado→rechazo; antes del original. No equivale a aislamiento global ni protege todas las rutas por handles. |
| `owner==Host` siempre procede de hechos positivos. | [F] | `guest_save_logic.h:454-458` devuelve Host si `!any_guest_session` aun sin `is_host/session_is_host`. `guest_save_host.cpp:228-250` suministra esos hechos. Para publicar hay que exigir prueba host explícita, no reciclar silenciosamente ese fallback. |
| Flush es inocuo para la real por limitarse a la tabla. | [P] | En estas trazas, sí es no-op por −1. En general `0x1871930` toma `mgr+0x5C`, llama CommitSlot y PostSave con `[character+0xA0]`; no serializa de nuevo los dos actores. Propiedad del invocador del flush ≠ propiedad del buffer pendiente. |
| Save serializa lobby del usuario y save del personaje suministrado. | [V] | `decomp-186E450.txt:128-157`: `[param4+0xA0]`, dos `processSaveDataToFile`, error si falla cualquiera; no recorre la lista de usuarios. No demuestra aislamiento de los globales consultados por sus componentes. |
| Hay dos llamadores únicos de SavePlayerToSlot. | [P] | Escaneo propio reproduce exactamente dos E8 rel32 en secciones ejecutables: `0xCBD86CE`, `0x103BE99A`. No demuestra ausencia de llamadas indirectas o rutas generadas; reconocer llamadores es otra condición conservadora para publicar. Ningún hook propuesto en `.xtls`. |
| Aperturas lobby+save y el Move posterior prueban completitud. | [F] como condición suficiente | Los hooks no observan WriteFile/FlushFileBuffers/CloseHandle. Un handle válido acredita apertura, no cantidad escrita, cierre, durabilidad ni generación. `save_hooks.cpp:389-411,473-478,1005-1027`; contraejemplo de especificación en `counterexamples.json`. No se afirma que el motor haya producido ese fallo en estas sesiones. |
| `check_header` y SHA certifican que la partida es del host/coherente. | [F] | `guest_save_logic.h:148-166` solo valida magic/versión/tamaños/flags/límites. No interpreta dueño, cuenta, misiones o relaciones lobby/save. SHA prueba igualdad de bytes respecto de una fuente, no autoría/semántica. Dos payloads arbitrarios pasan el predicado en `audit_models.py`. |
| Recuperación `_new/_backup` existe. | [V] con alcance | `decomp-2358EF0.txt:225-289,292-417`: limpia `_new`, trata backup, sustituye normal por backup; `decomp-235A800.txt:22-108` intenta recuperación y puede devolver0. No autentica una transacción host por SHA. |
| La publicación es «la misma secuencia» que el juego. | [P] | Los logs archivados prueban dos ciclos por llamada guest, uno por archivo; §5.4 publica ambos juntos en un ciclo. Es deseable tratarlos como pareja, pero la equivalencia completa no está probada, menos aún ante corte de energía. |
| M1/M2 bastan tal como están descritas. | [F] | M1 solo cubre una muestra y no handles/concurrencia/fallos; M2 solo observa algunos Acks, precio y hora. Deben ampliarse y ambas son obligatorias antes de real. |
| Backups hermanos de save están fuera de Steam Cloud. | [NV] | No se inspeccionó la configuración Cloud efectiva. Valve permite raíces/patrones recursivos; un directorio hermano no certifica exclusión. Ver apartado Cloud. |
| Publicación tarda <1s y instantánea solo pocos ms. | [NV] | Estimación sin medición de latencia, antivirus, disco lleno, espera de handles o cola. INV:298-299 no tiene trazas del publicador, todavía inexistente. |

## Recuento independiente y cobertura

`audit_log_coverage.py` lee línea a línea los tres logs, identifica exclusivamente mensajes de entrada/salida y correlaciona E/S por TID entre entrada y retorno. Guarda SHA-256 del archivo fuente, cada línea original y JSON. No ejecuta ningún script del autor.

| Fuente | Cobertura | Load host/guest (entradas) | Save host/guest | Flush host/guest | Redirects registrados |
|---|---|---:|---:|---:|---:|
| A | 18:03:08.502–18:10:02.000; 19.841 líneas | 3/3 | 0/1 | 0/1 | 30, todos slot204 |
| B | 18:49:59.321–18:57:09.275; 52.082 líneas | 4/4 | 0/1 | 1/1 | 30, todos slot204 |
| C | 19:02:32.125–19:08:06.591; 19.803 líneas | 3/3 | 0/1 | 1/1 | 30, todos slot204 |

B añade otro login al final, después de su último resumen periódico; de ahí 4/4 del parser frente a 6 cargas (3 guest) en B:52038. Los contadores no reemplazan la lectura de eventos. Las aperturas de las cargas host son siempre slot1→pass1 en los extractos.

Cada guardado guest registró 6 MoveFileExW, 2 CreateDirectoryW, 4 CopyFileW, 8 SetFileAttributesA, 2 SetFileAttributesW, 2 CreateFileW, 4 DeleteFileW y 2 RemoveDirectoryW. Es cuenta de operaciones, no 30 archivos. No se alcanzó el límite de 200 líneas de redirect (`save_hooks.cpp:398-404`).

Secuencia reducida por cada llamada (misma en A/B/C): #1 slot204→backup; #8 apertura lobby; #9 new→slot; #10 backup→delete; #16 slot→backup; #23 apertura save; #24 new→slot; #25 backup→delete. Las copias conservan el archivo no actualizado entre ciclos. Líneas decisivas: A:14944/14951/14952/14962/15003/15004; B:50593/50600/50601/50619/50626/50627; C:17758/17765/17766/17773/17780/17781. Retornos: A:15011, B:50634, C:17788. Exportados: A:15012, B:50635, C:17789.

Las duraciones de entrada Save→retorno son 118/126/116 ms, exclusivamente para esos guardados guest; **no** son medidas de duración de la propuesta de publicación host. El `committed slot 1163343936` en los Flush con −1 no identifica una ranura real: el camino de salida sin pendiente no escribe `slot_out`. El campo debe declararse no válido en ese caso para no convertir basura del parámetro de salida en evidencia.

## Inventario de direcciones y contratos (solo build 2976)

| RVA | Offset de disco | Entrada `.pdata` [inicio,fin) | Primera instrucción | Uso |
|---|---:|---|---|---|
| 0x186E450 | 0x186D850 | [0x186E450,0x186EFCE) | `4c894c2420`, mov [rsp+20],r9 (5B) | Save `(mgr,res*,slot,character,opts*) -> res*`; éxito se lee en `*res`, no se compara RAX con0. |
| 0x186F570 | 0x186E970 | [0x186F570,0x186F916) | `48895c2408`, mov [rsp+8],rbx (5B) | Load `(mgr,res*,slot,actor) -> res*`; guarda busy `+0x60`. |
| 0x1871930 | 0x1870D30 | [0x1871930,0x1871D55) | `48895c2410`, mov [rsp+10],rbx (5B) | Flush `(mgr,res*,character,u16,opts*,slot_out*) -> res*`; usa `+0x58/+0x5C`, no nueva serialización. |
| 0x20688A0 | 0x2067CA0 | [0x20688A0,0x2069634) | `4889542410`, mov [rsp+10],rdx (5B) | LoadComponents; actor+0x78 vector de componentes, +0x80 cuenta; llama vf+0x50. |
| 0x235A800 | 0x2359C00 | [0x235A800,0x235AB00) | `48895c2408` (5B) | Recuperación al cargar; referencia, no nuevo hook necesario. |
| 0x2358EF0 | 0x23582F0 | [0x2358EF0,0x2359FEB) | `48894c2408` (5B) | vf7: enumeración/limpieza/reconstrucción del almacén; referencia. |

Todos están en `.rsrc`. `audit_branches.py` no encontró destinos de ramas directas E8/E9/Jcc-rel32 dentro de los cuatro bytes posteriores a estas entradas; tampoco ramas directas de las funciones vecinas decodificadas que entren en ese intervalo (incluye saltos cortos). Resultado en `branch-check.json`; no certifica destinos indirectos ni código mutado en memoria. Firmas **completas** `kLoadSlotHead`=47B, `kSaveHead`=47B y `kFlushHead`=43B: coincidencia exacta y una única aparición de cada una en todo el EXE, en su RVA esperada. No se comprobó solo un prefijo. `audit_pe.py` utiliza parser PE propio + Capstone y registra las instrucciones de los primeros64B. Los tres primeros hooks existentes parten de una instrucción de5B y de entrada pdata; el diseño no requiere añadir parches a mitad ni enganchar sus llamadores `.xtls`.

Llamadas E8 encontradas: Load `0x2C17881/0x2C3E1CE/0x1042ABF3`; Save `0xCBD86CE/0x103BE99A`; Flush `0x28F273A/0x108393C8`. No confundir RVA de llamada con retorno (+5). Este inventario no certifica todo flujo indirecto del ejecutable.

## Cambios obligatorios en el diseño antes de implementar `real`

1. **Identidad y transacción explícitas.** Registrar host positivo (usuario validado + sesión, realm servidor, gestor esperado, época/mundo/cuenta) y no el fallback `!any_guest_session`. Registrar motivo/caller, secuencia monótona, ranura final y código leído con protección. Todo dato de memoria nuevo sigue `safe_read`/SEH; nunca conservar punteros de actor para el worker. La referencia `guest_save_mode()==On` debe representar salud efectiva, hooks instalados y estado no poisoned/disabled, no solo el ini inicial. Fallar a sandbox si se pierde la precondición.
2. **Flush requiere procedencia del buffer pendiente.** No abrir una autorización de publicación solo porque `character` sea host. Asociar `mgr+0x5C` a la generación que serializó lobby/save y comprobar dueño/ranura/mundo, incluso si el invocador del flush cambia o se desconecta el guest. Si no se demuestra, solo anotar/manchar. No atribuir al guest la semántica de un buffer host simplemente por ejecutar su salida. `0x1871930` confirma datos previos y después llama PostSave con el actor de esta llamada.
3. **Registro TLS reentrante y contaminación versionada.** Usar stack/contador de contexto con RAII, no un único begin/end que una llamada anidada pueda borrar. Un guest anidado suspende la autorización host; al salir se restaura el contexto anterior. Estado de contaminación por `(cuenta, mundo/época, ranura, generación)` bajo sincronización; una llamada limpia no borra el evento de otro hilo ocurrido después de empezar. Evitar booleans con carrera check/clear. No mantener cerrojos del mod mientras se llama el original ni durante E/S o logging; fijar orden de locks y usar generación para validar al cerrar.
4. **Observar destino real y resultado, no nombres aislados.** La política base sí respeta frontera de componente y rechaza raíces superpuestas (`src/common/save_guard.cpp:174-205`); `normalize` no resuelve `..` por sí mismo (161-171), depende de `FullPath` en hooks. Su predicado `create_may_write` indica posibilidad de mutación, no escritura completada (`save_guard.h:93-98`). `redirected()` retorna en `!hit` (`save_hooks.cpp:390`): las llamadas que ya traen ruta de caja no pasan por el registro propuesto en ese lugar. Contar ambas formas canónicas y evitar doble recuento por llamadas A→W o CopyFile→CopyFileEx. Clasificar mutaciones en source/dest de Move, Copy y Replace, borrado, creación y atributos por su efecto. `slot1` no debe coincidir con `slot108`; cuentas, sufijos exactos, mayúsculas, `\\?\`, normalización, reparse points y cambios de CWD deben quedar definidos. Fallo de observación/tabla llena no significa limpio.
5. **Compleción por handles o garantía equivalente demostrada.** Definir qué éxito prueba escritura completa y cierre de ambos archivos de la misma generación. Aperturas anteriores al scope, handles heredados/duplicados, escritura posterior, operaciones asíncronas, memoria mapeada y `SetFileInformationByHandle` no quedan acreditadas por hooks de ruta. No se pide enganchar indiscriminadamente todo Windows: se puede acotar el soporte a rutas nativas verificadas y fallar cerrado fuera de ellas, pero debe existir prueba de esa cobertura. No limpiar taint a partir de dos aperturas exitosas.
6. **Instantánea indivisible.** Extender la exclusión de la transacción hasta tener pareja inmutable; copiar primero lobby y luego save sin exclusión permite generaciones distintas. Abrir/validar ambos archivos con una política de compartición que impida cambio y comprobar identidad/generación, o conservar un lock nativo demostrado que cubra el intervalo. La barrera2077 no se debe suponer para 2910/Flush ni para otros procesos. Mantener serializador nativo en su hilo/realm; solo bytes y metadatos inmutables pasan al worker. Medir p95/p99/máximo de la copia bajo barrera y límites de cola.
7. **`RawScope` acotado y total.** Todos los detours deben respetarlo antes de redirigir/bloquear, con restauración ante excepciones y anidamiento. Solo worker de E/S propio, nunca callbacks de juego dentro del scope. Los helpers de backup/publicación no deben emitir operaciones de juego inadvertidamente bajo bypass. Verificar en raíz ficticia que lectura/escritura/enumeración raw y normal divergen como se espera y que el registro no se auto-mancha por el propio publicador.
8. **Manifiesto durable y recuperación, antes de borrar nada.** Cada generación debe guardar build/esquema, cuenta/mundo, sesión/época, ranura, dueño, secuencia, tamaños y hashes de ambos archivos, estado del commit y fuente. Copia de sesión debe verificarse y marcarse completa antes de primera mutación real; `copy_tree` actual no verifica hashes ni flush y tolera fallos de fechas (`save_hooks.cpp:551,559`). No borrar `_new`/`_backup` preexistentes a ciegas: clasificar y conservar lo recuperable en cuarentena. Rollback idempotente, sin podar la última generación válida ni la copia de sesión pertinente. Nombres únicos de sesión/seq (no solo segundo del reloj), prohibición de reparse/hardlink y comprobación de volumen/raíz final.
9. **Cierre y cola explícitos.** FIFO con “solo la más nueva” por ranura necesita secuencia confirmada y regla que no publique una generación vieja después de una nueva. No fusionar antes de que la más nueva esté validada. Cola acotada con overflow cerrado; cierre ordenado drena con plazo, interrupción conserva manifiestos verificables. El log debe distinguir `snapshot`, `prepared`, `installed` y `durable/committed`; no anunciar éxito antes del punto durable definido. Conservar los snapshots huérfanos con su manifiesto, no solo el nombre.
10. **M2 ampliada y política de mundo como puerta obligatoria.** Nada de real con aislamiento semántico pendiente. No basta backup para autorizar persistencia de estado corrupto ni dos SHA distintos para certificar separación.

## Recuperación, arranque y fallos

La recuperación nativa **no elige la generación nueva por estar completa o por tener mejor SHA**. En vf7, `_new` se elimina; si hay `_backup`, se elimina/retira la ranura normal y se renombra backup a normal. Se verificó además que `0x5983C04` contiene `_new\0` (`extra-bytes.json`). `0x2358A20` retira por renombrado y después intenta eliminación; `0x1329EB0` es el auxiliar de renombrado llamado por estas rutas. Pueden fallar y retornar error.

Modelo de corte (sin ejecutar I/O real):

| Punto del publicador | Estado visible | Próximo arranque nativo esperado |
|---|---|---|
| Nueva pareja escrita en `_new`, aún existe slot viejo | slot viejo + new | limpia new; conserva viejo |
| Viejo pasó a backup | backup viejo + new, falta slot | limpia new; restaura viejo |
| New pasó a slot, backup aún existe | slot nuevo + backup viejo | restaura viejo, aunque nuevo fuera válido |
| Backup salió de la raíz | slot nuevo | conserva nuevo si archivos legibles |

Por tanto, crash tras instalar y antes de archivar backup pierde el último guardado; el snapshot externo permite recuperación manual si es durable. No es necesariamente corrupción, pero contradice cualquier promesa de que el renombrado `new→slot` ya haya terminado el commit. La pareja completa también puede faltar si se anuncia/encola antes de finalizar y verificar su instantánea.

`FlushFileBuffers` se debe comprobar para ambos archivos y para los metadatos/journal que el diseño elija; no basta releer el SHA desde caché. `MoveFileExW` individual no vuelve atómica la secuencia de varios renombrados. Documentar durabilidad asumida y comportamiento ante energía/disco, sin prometer que “igual al juego” prueba esas garantías. Referencias oficiales: [FlushFileBuffers](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers), [MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw).

La propuesta reconoce que el mapa S ya existe antes del mod. La evidencia de §5 no mide cuándo vf7 corre respecto al mod en cada arranque: esa cronología exacta queda [NV] hasta trazarla. Es obligatorio resolver la recuperación antes de que el juego construya su mapa/seleccione partida, o delegarla íntegramente al camino nativo probado y limitar el mod posterior a clasificar huérfanos externos. Una limpieza tardía no puede reinterpretar con seguridad lo que el juego ya restauró. El siguiente arranque también puede recibir sincronización Cloud antes de que nuestro worker exista.

La poda no debe ejecutarse como primer remedio a disco lleno. Probar fallos al crear directorio, apertura, escritura parcial, flush, lectura de verificación, primer rename, segundo rename, archivado y poda, además de sharing violation y permisos. Ante fallo preservar al menos una pareja completa y no destruir `_backup` desconocido. Mantener disponible una copia externa y verificada incluso si el rollback posterior también falla.

## Contaminación del mundo, slots y reglas del usuario

La carga genérica `0x20688A0` recorre componentes del actor y llama vf+0x50 (`decomp-20688A0.txt:392-408,562-569`). El hecho de cargar “otro actor” no impide que una función virtual escriba un singleton. `vision-a3-guardado-mundo.md:99-105` ya advierte que nombres GlobalGameEvent/TradeItemPriceRate no prueban ni descartan ese efecto; no he certificado su aislamiento en esta auditoría.

Hay al menos una **escritura global estática real** que M2 no puede excluir mirando solo precios y hora: `0x1791B98` escribe en RVA `0x6C8CD98` desde EAX y `0xB74302` lo hace desde R14D. Bytes leídos del EXE y decodificados en `extra-bytes.json`; concuerdan con `identity_probe.cpp:65-82,467-500`. No hubo mensajes de ese probe con cambios de valor en A/B/C. Por tanto, escritura global [V], cambio perjudicial de valor por el guest en estos ensayos [NV], inocuidad [NV]. El mapeo de reflexión de `0x187C490` nombra `_currentRestoreCodeVersionXXX` y `ExecuteRestoreCodeVersion`.

M2 debe medir valores/contenedores del host y campo antes/después de carga guest, no únicamente paquetes emitidos. Usar partidas de laboratorio con marcadores deliberadamente diferentes de versión, eventos/precios, hora, misiones, vivienda/gimmicks, inventario y dinero; observar carga, viaje rápido, salida, reconexión y guardado/carga posterior. Comparar con control sin guest. `clock_gate` contiene rutas concretas del reloj, no todas las variables del mundo. No revertir globales con snapshots ciegos si eso borraría progreso legítimo del host ocurrido entre tanto; contener la aplicación de estado guest en su origen y bajo sincronización apropiada cuando se pruebe la ruta.

La política obligatoria es: mundo y misiones del anfitrión; acciones del invitado sí cuentan para ese mundo; **ninguna misión propia del invitado progresa**. El guardado no puede suplir el enrutamiento de misiones. Validar una acción de misión hecha solo por el invitado, persistencia host tras reinicio y diario propio guest inalterado. No confundir contaminación con consecuencias legítimas de sus acciones, ni restaurar todo el mundo al estado anterior a su llegada para evitar contaminación. Monturas, compañeros, mascotas, gimmicks, familias retiradas, NPC de misión y relevo de control necesitan prueba de persistencia semántica: autoría de archivo correcta no prueba que se serialicen en estado válido.

Lista blanca de publicación `0-2,100-108` es apropiada para el objetivo acotado. Debe ser exacta por cuenta y rechazar 3/200-207 (incluidas204/206), negativos, overflow y sufijos ambiguos. El guest On remapea/refusa antes del original y por tanto no llega a la rama slot3 por esa entrada. **No hay aislamiento absoluto de CrossSave:** host sigue pasando cualquier ranura salvo204 (`guest_save_logic.h:597-599`), y `0x186E450` toma la rama3 y llama UploadCrossSaveFileAsync. Redirección de archivos y whitelist de publicación no interceptan una subida HTTP nativa. Slot206 tiene borrado condicional tras carga exitosa de personaje en `decomp-186F570.txt:81-100`; excluirla del publicador no elimina esa semántica. No reutilizar3/206 para staging. Slot204 debe quedarse en caja/export guest y jamás mezclarse con publicación host.

## Steam Auto-Cloud

La documentación de Valve dice que Auto-Cloud sincroniza grupos configurados al iniciar y salir; las raíces pueden tener patrones y recursión. `steam_autocloud.vdf` marca una ubicación configurada, no documenta por sí solo todos los patrones ni certifica que las carpetas hermanas estén excluidas. También existe sincronización dinámica para aplicaciones que la habilitan; **no se ha demostrado que este juego la habilite**. Fuente: [Steam Cloud, Auto-Cloud y Root Paths](https://partner.steamgames.com/doc/features/cloud).

Por ello son [NV] los supuestos “backup siempre local/fuera de nube” y “solo hay sincronización al cerrar” para esta instalación concreta. Antes de real, confirmar raíces/patrones efectivos sin subir datos privados, usar una ubicación de respaldo excluida y probar conflictos/restauración con cuentas y datos de laboratorio. No restaurar mientras Steam pueda descargar/subir una versión rival; el proceso de recuperación debe coordinar cierre y estado de sincronización. La carga de una generación corrupta en nube puede propagarla a otro PC aunque el mod local tenga buenos checksums.

## Pruebas mínimas futuras (ninguna ejecutada aquí)

**Puras y en raíces ficticias, antes de tocar partidas:** parser de rutas y whitelist; versión/build; identidad fallback; retornos puntero/resultado; scopes anidados host/guest/Flush; epoch/cuenta; contaminación concurrente entre inicio/fin; handle que se abre antes y escribe después; escrituras directas a caja; fallo y overflow del registro; instantánea mezclada; pending de otro dueño; pair lobby/save distinta; estado íntegro de cola; RawScope completo. Modelo de estados de recovery con corte en cada paso, inyección de escritura parcial/disco lleno/permisos/flush/rename y restauración idempotente. Asegurar que un fallo nunca elimina última pareja válida y que el journal no acepta una mezcla vieja/nueva. Probar ranura nueva y existente y restos preexistentes `_new/_backup`.

**Un PC con `cdmp_peer`, primero sandbox/probe:** dos marcadores host/guest conocidos, autosave real del host y 2910 en cada clase de ranura, salida host/guest con pending−1 y pending real, remap/fail-closed/guest_off-probe-poisoned; trazas sin truncar hasta más allá del autosave. Medir llamada, barrera, snapshot y worker por separado; ambos archivos y generación en cada candidato. Backup real idéntico solo cuando posteriormente exista autorización para pruebas de real; primero publicar a raíz ficticia. Ejecutar M2 ampliada con observación protegida de globales/contenedores y sin depender solo de Acks. Recargar en single-player y cotejar inventario, diario, mundo, posición, tiempo, NPC/monturas.

**Shadow después:** dos partidas de laboratorio distintas, acciones de misión solo guest que avanzan host y nunca guest; viajes/cargas simultáneos, guest sale/reentra, pérdida de conexión y cierre abrupto en etapas autorizadas; host llega/se va, familias NPC y caballos/compañeros; guest export intacto. Comparar generaciones pre/post y recarga sin mod. Cloud en última fase, con exclusión de backup verificada y procedimiento de conflicto/restauración probado. No asumir que una sesión feliz de dos PCs sustituye pruebas de falla.

## Alternativa más robusta y orden recomendado

Primero añadir export host a generaciones inmutables fuera de `save` con manifiesto y validación de pareja, conservando `sandbox` como defecto; `probe` recorre el mismo clasificador y snapshot que real pero nunca abre bypass a la carpeta real. Así ya se puede conservar progreso de laboratorio y revisar candidatos sin riesgo de nube. La promoción inicial puede hacerse mediante herramienta fuera del proceso del juego, juego cerrado y sincronización controlada, con recovery antes del siguiente inicio. Esto evita parte de la concurrencia entre cliente/juego/publicador, aunque no arregla contaminación semántica del mundo: M2 sigue siendo obligatoria.

Después: identidad/procedencia de diferidos y snapshot coherente; aislamiento global/misiones; transacción durable y pruebas de fallos; pruebas peer; Shadow; Cloud; finalmente activación explícita real. La publicación automática durante juego se justifica solo si su beneficio compensa ese protocolo adicional. Esta auditoría no decide el orden interno de los otros cuatro estudios ni afirma haber contrastado sus conclusiones; sí exige que los cambios de NPC/contención estén estables antes de persistir sus efectos en partidas reales.

## Scripts y reproducción

Todos los scripts son propios y solo leen fuentes autorizadas; escriben resultados bajo OWN. Ejecutar con:

```powershell
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<OWN>\audit_log_coverage.py'
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<OWN>\summarize_sequence.py'
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<OWN>\audit_pe.py'
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<OWN>\audit_extra.py'
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<OWN>\audit_branches.py'
& 'C:\Emulador CD\build\research\venv\Scripts\python.exe' -I -B '<OWN>\audit_models.py'
```

- `audit_logs.py` es el lector base llamado por `audit_log_coverage.py`; genera `logs.json`, `log-A/B/C-extract.txt`. `log-summary.txt` conserva salida del lector ampliado. SHA de logs fuente en `logs.json`.
- `audit_pe.py`: parser PE/pdata propio, verificación de firmas completas y búsqueda de E8 a destinos; `pe-check.json`.
- `audit_extra.py`: usa el lector PE propio, lee bytes de las escrituras globales y string `_new`; `extra-bytes.json`.
- `audit_branches.py`: scan rel32 en secciones ejecutables y ramas decodificadas próximas a las entradas; `branch-check.json`.
- `audit_models.py`: contramodelos **de la especificación**, sin ejecutar juego ni codebase; reproduce predicado estructural y estados de recovery; `counterexamples.json`. No constituye prueba de que una partida real contenga esos payloads arbitrarios.
- Consultas Ghidra reproducibles: `powershell -NoProfile -ExecutionPolicy Bypass -File D:\RE\CrimsonDesert_2976\scripts\cdmp-query.ps1 decomp 0x14<RVA>` para186E450,186F570,20688A0,235A800,2358EF0,2358A20,1329EB0,187C490; salida copiada a OWN. Las consultas se ejecutaron secuencialmente al comprobar que headless no admite abrir el mismo proyecto concurrentemente. Los primeros intentos sandbox fallaron por política de scripts/ruta TEMP; se repitieron con lectura escalada autorizada. No se cambió política global ni código.
