# S42 — enemigos del invitado y puertas intermitentes

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`. Rama `opcion-d-enemigos`, base `effae477f74302fe2759ce5be272c37387b2169b`.
Investigación y desarrollo sin abrir el juego, desplegar ni tocar `bin64` o partidas. Las pruebas en vivo quedan a cargo del usuario.

**Estado: investigación y sondas implementadas; reparación funcional pendiente de evidencia.** No se entrega un arreglo seguro de ambos síntomas. `npc_family_vehicles=1` se degrada explícitamente a `probe`; las otras dos claves con `1` sólo amplían diagnóstico. La revisión independiente aprueba este alcance observacional, no una reparación. [V] fuente primaria reproducida; [E] conclusión fuerte del código; [H] hipótesis; [NV] falta una medida. Un resultado 2922 no prueba daño positivo; una publicación 2005 no prueba que el actor esté renderizado.

## 1. Dictamen por síntoma

| Síntoma | Resultado de esta investigación |
|---|---|
| Bandidos lejanos controlados por el PC incorrecto | [V] La familia de horario del invitado se parte cuando nace un vehículo tipo 5 no admitido por fase B; se retira y su reposición queda con el anfitrión. Es un defecto concreto de cobertura familiar. |
| No atacan / reaccionan brevemente | [E] El fallback deja su simulación en el anfitrión lejano; [H] el vínculo con la instancia de horario del invitado falla en ese cliente. Falta demostrar que corregir la familia elimina todos los síntomas. |
| No mueren | [V] Hay una muerte nativa de un bandido; otros reciben numerosos 2922 sin 2147. [NV] Aún no se conoce la decisión nativa que impide variar su vida. No se fuerza HP ni muerte. |
| No aparecen hostiles en minimapa | [NV] Sin lectura de la relación efectiva y del filtro del minimapa, no hay causa exacta demostrada. La instantánea 2597 de S38 coincide entre clientes. |
| Delito al atacar | [V] El único delito capturado identifica un caballo de carro; no demuestra que el juego convierta esos bandidos en civiles. |
| Puerta intermitente | [V] La escena candidata se crea en la prueba positiva y recibe rechazos de creación en S38. [NV] Falta el código nativo del rechazo y vincular visualmente esa escena a la puerta. No hay todavía arreglo funcional de puertas. |

## 2. Enemigos: evidencia reproducible

Fuentes: `C:\Emulador CD\backups\p8-enemigos-invitado-20261015\host.log`, `guest.log` y sus CDPK; S38 en `C:\Emulador CD\backups\p8-sesion38\`. Los lectores propios y resultados compactos se conservan en [s42-datos/README.md](s42-datos/README.md). Los offsets siguientes son bytes absolutos del `.bin` del invitado, no RVA.

| Clave | Nombre | Tipo | Facción | Controlador | 2922 recibidos | 2147 / 2530 | Offset 2005 invitado |
|---|---|---:|---:|---|---:|---|---:|
| B010164D | Bandit, raíz | 3 | 1000128 | 90100000 anfitrión | 12 | 0 / 0 | 212295522 |
| B010164E | Bandit | 3 | 1000128 | anfitrión | 4 | 1 / 1 | 212305099 |
| B010164F | Bandit | 3 | 1000128 | anfitrión | 5 | 0 / 0 | 212315336 |
| B0101650 | Bandit | 3 | 1000128 | anfitrión | 42 | 0 / 0 | 212325165 |
| B0101654 | Basic Wagon | 5 | 0 | anfitrión | 0 | 0 / 0 | 212364844 |
| B0101656 | Priden Wagon Horse | 5 | 0 | anfitrión | 1 | 0 / 0 | 212456029 |

### 2.1 Cronología causal del controlador

- [V] `host.log:60280`: stage `200002F6`, recurso `C12CB783`. `guest.log:34665`: `stageseq/sequencerspawn/npc/cd_seq_spawn_docu_npc_extralarge_0021.paschedule`.
- [V] `host.log:60872–60879,60959`: raíz inicial `B0101636`, descriptor `CreateServerActorDesc_NPCSchedule`, timer 2045 del invitado; U-e escribe controlador `90100065`. El miembro `163D` es tipo 5 y parte la unidad.
- [V] `host.log:60911`: siete nodos preexaminados, veinte nacimientos, seis miembros asignados, tres host-kept, once partidas, trece tardíos. Los descendientes de vehículo y la carga bajo gimmick amplían la rotura.
- [V] `host.log:60912–60955`: contención retira la unidad en unos 166–239 ms tras la publicación, por estar partida; no es un simple timeout de ocho segundos.
- [V] `host.log:60956`: reposición `164D` alojada con el anfitrión por «su nodo ya partió una unidad». Ambos 2005 confirman el controlador real. El controlador mostrado en el censo es el contexto de la unidad y no sustituye al dato del 2005.
- [V] El anfitrión origina todas las 2223 de D/E/F/1650: 35/27/33/68; el invitado ninguna. [H] `host.log:61208/61210/61212/61214` encuentra dueño A0100066 pero instancia de escena NULL; falta probar causalidad respecto de IA/daño.

La fase C (`user2_road_control`, AutoSpawnRoadReq 2550) no es el origen de esta familia. Ampliar sólo los carros de carretera S40 no arregla esta ruta de horario.

### 2.2 Vida, delito y relación

[V] E recibe HP=0 y muerte por A0100066 a 12:40:47.571 en el anfitrión y 47.577 en el invitado. Offsets host 247502265/247502386; invitado 237927293/237927414. El delito del caballo aparece a 12:41:06.985, offset invitado 246453700. Las bajas de D/F/1650 por motivo 19 no se contabilizan como muertes.

[E, .rsrc] Escritor 2147 `0x2991440`: clave u32, bloque serializado por `0x2445250`, fuente u32 y cuatro bytes. `0x2445250` escribe stat u32, ocho valores de 8 B, u16, dos bytes y otro valor de 8 B: 80 B; por ello la fuente posterior queda en payload +0x54. El lector conserva los bytes para no confundir resultado de impacto con vida.

[V] S38: veinte pares 2597 idénticos salvo destinatario (A0100001/A010027B); offsets 5171087/6116834. Sólo hay nueve 2922, todos A010027B contra sí mismo. No permite identificar al enemigo de sus marcas ni demostrar combate con NPC. No se deduce neutralidad de que la facción 1000128 no figure en los pares: existe fallback nativo.

[E, .rsrc] `0xA35B90` elige tabla por igualdad entre usuario local y dueño del campo. `server.cpp` reescribe dueño de 2174/2643 para el invitado; no es válido asumir que éste toma siempre el fallback. `0xB92C80` aplica 2597 sólo al personaje local. `0xE6984B0`/`0xC66E170` están en `.xtls`: referencias parciales de lectura, nunca hooks propuestos.

### 2.3 Calidad y reloj

[V] Local: 685.811 registros host y 621.501 invitado; 79/13 huecos explícitos, sin registros cortados. «No capturado» no equivale a ausencia absoluta. QPC se alinea con las anclas `capture t=`; la hora impresa de las líneas de anillos es la del vaciado y puede retrasarse respecto al evento.

[V] Nueve 2922 idénticos alinean Shadow+1h con un delta residual de 0,618–0,634 s (mediana 0,627); mezcla reloj y tránsito, no es latencia pura. Las pulsaciones Ctrl+F12 son distintas y no se usan como sincronizador.

## 3. Diseño e implementación

### 3.1 Familias con vehículos

`npc_family_vehicles=0|probe|1`, anfitrión, defecto 0. **En esta entrega `1` equivale a `probe`, con aviso explícito de degradación.** La sonda utiliza el `create_mid` existente `0x2792A7B` cuyo propietario sigue siendo `docked_gimmicks`; no añade otro encima. La cohorte es el recurso medido `C12CB783`. No cambia `npc_family_types`, la autoridad, las reservas ni la retirada efectiva.

Observa tipo 5 de horario, LinkedVehicle (`0x2797510`, descriptor base, envoltorio correspondiente, padre tipo 5 y clave en +0x490) y carga transitiva bajo gimmicks. El resultado «sombra» clasifica qué decidiría una extensión; no simula capacidad acumulada ni garantiza reservas. Se imprime `capacidad_sombra_no_reservada=1`. Las identidades entre PC siguen siendo claves del servidor; direcciones sólo se usan para lecturas locales protegidas. El log incluye nacimiento y clave de raíz, recurso/nodo y clase real frente a sombra. Detalle máximo: 1024 nacimientos por proceso; el censo y las pérdidas del anillo permiten detectar una medida incompleta.

**Por qué no se activó la asignación:** [E] `logout_drain` no impide continuar el Logout original. Bloquear sólo `drain_now` no protege frente a la destrucción nativa durante 2001/2283/2362, cargas o salida. `0x2068340` es un thunk hacia `.xtls 0xE0D9AF0`; el Unload `0x287D4C0` también delega en `.xtls 0x10225FC0`. No se demuestra desmontaje seguro ni exclusión entre comprobar asientos y retirar. No se proponen hooks en esos tramos virtualizados. Fuentes y límites: [análisis estático](s42-datos/estatico/informe.md), [revisión independiente](s42-datos/revision/informe.md).

El futuro arreglo debe admitir la **unidad entera**, reservar todos sus miembros por generación y mantener host-kept/publicación segura de los tipo 7. Debe demostrar drenaje ordenado, carreras de temporizadores, monturas y caminos nativos de salida antes de cambiar control. Admitir sólo el carro o bloquear una retirada del mod dejaría la familia parcialmente cubierta. La partición y reposición conocidas **seguirán ocurriendo con estos INI**.

### 3.2 Decisión de inmunidad

`enemy_damage_probe=0|probe|1`, anfitrión, defecto 0. Ambos valores no cero sólo observan: `probe` hasta 12 detalles/s; `1` hasta 60. Entrada `.rsrc` `0x209CB30`, `.pdata [0x209CB30,0x209CD2C)`, firma de 40 B única. Parche permitido 5 o 15 B, instrucciones completas. Llama al original una sola vez con los cinco argumentos intactos y devuelve el mismo bool.

Registra clave/controlador, tipo, vtable y caller RVA, parámetros, `contexto+4`, máscara de lecturas válidas y retorno nativo. Ring acotado y contadores de omisiones/pérdidas; se formatea fuera del hook. Vtable/caller permiten distinguir actores cliente/servidor dentro del anfitrión. `false` no prueba daño positivo: esta función sólo comprueba una de las puertas nativas. No altera HP, facción, objetivos ni delitos.

### 3.3 Puertas

`door_trace=0|probe|1`, ambos PC, defecto 0. `probe`: totales y resultado nativo de creación; `1`: añade hasta 2000 detalles por proceso. **Ningún modo fuerza apertura ni crea entidades adicionales.** La candidata es el recurso `DE7B52AA`, UUID en bytes `4fb9eeab670f74563c343d640c000000`, posición (-10668.78515625, 617.0676879882812, -3838.27294921875). Su identificación visual como hoja de la taberna sigue [H].

[V] Recuento independiente de los `.bin`, sólo mensajes de esa escena:

| Captura | Req2928 | Ack2145 | Nak2953 | 2005 con geometría candidata |
|---|---:|---:|---:|---:|
| Positiva anfitrión | 1 | 1 | 0 | 2 |
| Positiva invitado | 144 | 0 | 144 | 2 |
| S38 anfitrión | 39 | 0 | 39 | 0 |
| S38 Shadow | 45 | 0 | 45 | 0 |

Un Nak **no basta** para afirmar ausencia: el invitado positivo ya tenía por snapshot B0100393/B0100394. En el host positivo aparecen a offsets 26882427/26889899, seguidos de Ack2145 a 26896307; los 3089 del hijo cerca de la marca 08:37:19.932 son compatibles con una constraint física activa, no prueban por sí mismos apertura. S38 recibe Sync2665 del mismo UUID/posición pero no esa alta geométrica; reintenta durante 199/241 s. Fuente: [informe de puertas](s42-datos/puertas/informe.md), [recuento con offsets](s42-datos/puertas/evidencia-resumen.json).

[E] Ruta 2928 → `0x29C1C80` → `0x282DC80` → 2953 por `0x29A2F70`. El último mensaje **omite el error**. La sonda en la entrada `.rsrc 0x282DC80`, `.pdata [0x282DC80,0x282E694)`, firma única de 47 B y parche exacto de 5 B, conserva argumentos, retorno y error del original. Mide el código bruto y los globales `0x6CF7018/1C/24/28`: recurso sin nombre, recurso no preparado, sin candidatos, creación fallida; no son una lista exhaustiva. Registro con máscara de validez: ilegible nunca significa cero válido. Firma y límites en [hook-verificado.json](s42-datos/puertas/hook-verificado.json).

Los taps distinguen envío aceptado del invitado, llegada al servidor, resultado nativo, salida y entrega aceptada por el cliente; no confunden tráfico local de Shadow con anfitrión. Colas acotadas, contadores de pérdida/variantes/budget; sin formato dentro de los detours. La observación jamás rechaza una petición nativa por su longitud o por una lectura fallida.

[V/E] S38 tiene cero 2779: `gimmick_event_dedup` no explica directamente el rechazo observado. El positivo conserva controlador host en ambos tipo 7. `child_hold`/`observer_hold` podrían afectar publicación posterior, pero no explican por sí solos la divergencia candidata antes del alta. No hay correlación demostrada con vaciado U-1 ni justificación para cambiar progreso o control de todos los gimmicks. La prueba del 14-10 sólo tiene logs, sin `.bin`: no se extrapola la candidata a aquella puerta como hecho.

## 4. Pruebas y revisión

El `scripts/build.ps1` final sobre `c7e62393` terminó con **78/78 tests**, exit 0, incluidos `optd_npc_family_vehicles`, `optd_enemy_damage_probe` y `optd_door_trace`. También pasaron los tests de scripts; no hay fallos que atribuir al juego abierto. Registro: [final.log](s42-datos/compilacion/final.log). Los tests puros verifican clasificación y baseline con sombra ilegible, reutilización de registros, lecturas protegidas simuladas, modos efectivos, parsers reales, truncamientos, roles y firma aceptada. No validan IA, apertura física ni seguridad de un futuro cambio de autoridad.

La [revisión independiente](s42-datos/revision/informe.md) encontró y corrigió dos P1: hook de puertas habilitable tras rechazo de bytes, y lectura de `probe` que podía alterar la clasificación real de un hijo reservado. También corrigió lecturas ilegibles representadas como cero y etiquetas de rol erróneas. Se cotejó de nuevo el ensamblado final y su hash, con las mismas instrucciones en los bloques revisados de rutas invocadas desde mid: sin nuevas llamadas CRT de copia/ceros, `__chkstk` ni instrucciones YMM en los bloques examinados. Ninguna aprobación estática sustituye la prueba en vivo.

## 5. INI y plan de prueba en vivo

No se han ejecutado estos pasos ni se ha desplegado el ASI. Los cuatro INI conservan todas las claves de sus bases `cdmp.2inst-host-carros-1.ini` y `cdmp.2inst-guest.ini`; sólo añaden estas tres:

| Paso/PC | npc_family_vehicles | enemy_damage_probe | door_trace |
|---|---|---|---|
| probe host | probe | probe | probe |
| probe guest | 0 | 0 | probe |
| activo host (diagnóstico ampliado) | 1 → probe | 1 | 1 |
| activo guest (diagnóstico ampliado) | 0 | 0 | 1 |

Los nombres «activo» satisfacen la pareja solicitada de ficheros, **no significan reparación activada**. No se acepta una comparación probe/1 como ensayo de un arreglo: sólo cambia detalle de daño/puertas. Restablecer procesos reinicia presupuesto y nodos ya vetados. Las partidas deben respaldarse antes de las pruebas en vivo que escriban memoria, como exige CLAUDE.md. Las opciones heredadas activas conservan sus propias precauciones.

Comandos preparados para el usuario, **no ejecutados** (requieren el binario de esta rama y seguir el despliegue documentado en `dos-instancias.md`):

```powershell
.\scripts\launch-2inst.ps1 -HostIni docs\research\ini\cdmp.2inst-s42-probe-host.ini -GuestIni docs\research\ini\cdmp.2inst-s42-probe-guest.ini
.\scripts\launch-2inst.ps1 -HostIni docs\research\ini\cdmp.2inst-s42-activo-host.ini -GuestIni docs\research\ini\cdmp.2inst-s42-activo-guest.ini
```

1. **Arranque y alcance:** comprobar los tres modos solicitados/efectivos y las líneas de instalación. Si se rechaza el hook, no interpretar la falta de eventos como ausencia de llamadas. Capturar desde antes de acercarse al encuentro/puerta.
2. **Bandido lejos:** anfitrión ≥10 celdas; invitado en el encuentro C12CB783. Marcar antes de aparecer y antes de atacar; anotar ventana, nombre/posición del objetivo, minimapa, reacción, HP visible y delito. Distinguir bandido, caballo y carro. Buscar censo real/sombra y 2005 por clave/controlador. Se espera la partición/fallback ya conocidos; eso no es una regresión nueva.
3. **Daño:** correlacionar la misma clave en 2922, 2147 y 2530 con `EnemyDamageProbe [inmunidad]`, máscara, vtable/caller y retorno. El muestreo limitado puede omitir un golpe: exigir ventana sin pérdidas/recortes o repetir una medida aislada; no concluir «nunca se llama» por ausencia de detalle. false sólo descarta esta comprobación de inmunidad, no acredita daño positivo.
4. **Controles separados:** repetir con anfitrión cerca. En otra sesión, B/C=0 (`npc_family_control=0`, `user2_road_control=0`, `user2_road_carts=0`), manteniendo el resto documentado; no mezclar con la comparación de detalle. Si desaparece el síntoma, sigue siendo necesaria la clave/cohorte y el flujo nativo para atribuirlo. No hay datos previos suficientes de esos controles.
5. **Taberna:** anfitrión solo antes del invitado; después ambos cerca; luego invitado solo en la zona. En cada ventana marcar antes/después, intentar abrir/cerrar/cruzar y registrar posición. Vincular visualmente la candidata y correlacionar 2928/2145/2953 con `DoorTrace [nativo]` por hora, sujeto y época. Un Ack no prueba apertura física ni un Nak prueba ausencia.
6. **Paso 2:** repetir una situación equivalente con el INI «activo» sólo para obtener detalle. No añadir pruebas de montura/viaje como si hubiera una extensión de vehículos funcionando: no la hay. Conservar logs y capturas de ambas ventanas. Repetir con Shadow una vez interpretada la medida local; el reloj de Shadow exige alineación con payloads coincidentes y anclas.

**Éxito de la medida:** al menos caso positivo y fallido de la misma escena, máscara nativa legible, ruta sin pérdidas; para combate, clave de bandido inequívoca, familia/controlador y decisión de inmunidad con paquetes de vida/muerte. **Éxito del arreglo futuro:** hostilidad, minimapa, daño/muerte y puerta funcional en ambos clientes, cerca/lejos y tras relevo, sin duplicación, cierres ni cambios de progreso. Esta entrega sólo persigue el primer criterio.

Abortar ante cierre, nueva publicación restore 0 a observador, doble control, nueva partición repetida fuera del baseline, degradación fuerte de rendimiento, baja de vehículo montado, misiones propias o pérdida de guardado. Conservar evidencia y volver las claves nuevas a 0; no reintentar hooks en otra dirección. Con `cdmp_peer` sin juego pueden comprobarse parsers/tránsito simulado, pero **no** IA, puerta física ni daño del cliente; no se ejecutó un vivo contra el juego en este encargo.

## 6. Decisiones pendientes, en orden

1. Medir el error de creación de `0x282DC80` para la candidata y confirmar su identidad visual. Recurso-no-listo → preparación del mapa; sin-candidatos → UUID/flags/activo; creación-fallida → `0x28308E0`; éxito+alta pero puerta bloqueada → publicación/constraint. No se cambia ninguna de esas condiciones por anticipado.
2. Medir inmunidad y relación efectiva de los bandidos con controles cerca y B/C apagados. Si la primera no explica HP, seguir la decisión nativa posterior. Para minimapa aún falta instrumentar la relación/filtro específico: esta sonda no lo resuelve por sí sola.
3. Resolver el ciclo de vida de la familia completa durante Logout, viaje, temporizadores y llegada del anfitrión. Sólo después implementar autoridad tipo 5 y carga con reservas y publicación seguras, apagado por defecto y validación probe previa.
4. Validar solución funcional con dos clientes reales y Shadow. No hay promesa de que ampliar la familia por sí sola arregle todos los síntomas.

No se sincronizan misiones propias ni se cambia progreso del anfitrión. Los INI conservan las opciones heredadas; eso no constituye prueba de corrección global de la política de misiones. El alcance solicitado de reparación completa queda **pendiente**, no se presenta como terminado por haber pasado los tests.

## 7. Trazabilidad de la entrega

- Código y tests: commit `c7e62393537fc0d1771692daab2942cb116fe852`, rama `opcion-d-enemigos`, desde `effae47`.
- ASI: `C:\Emulador CD-enemigos\build\bin\RelWithDebInfo\cdmp.asi`. Build ID `c7e62393`, `CDMP_BUILD_DIRTY=0`; el commit posterior de documentación no cambia estas fuentes.
- SHA-256 obtenido con `certutil -hashfile build\bin\RelWithDebInfo\cdmp.asi SHA256`: `0b373a026b7630335c0a818c5dc31e13babec8cdb0cd0f1f1a7d9a858a8f0f5c`.
- [Resultado y hashes de objetos](s42-datos/compilacion/resultado.json), [revisión independiente](s42-datos/revision/informe.md), [índice de evidencia y reproducción](s42-datos/README.md).
- Referencias de código en este commit: `enemy_damage_probe.cpp:37/78/119` (detour/instalación/vaciado), `door_trace.cpp:59/136/191/256/275` (detour/verificación/instalación/ingress/vaciado), `user2_stages.cpp:1741/1925/1939/7240` (censo/sombra/baseline/modo efectivo), `session2.cpp:1215/1562` (configuración/consumidor), `config.cpp:304` (defecto 0). Parsers y tests homónimos están junto a cada módulo y en `tests/`.

Incidencia de ejecución: la revisión automática rechazó un intento de añadir ledger, bloqueo de nacimientos y protecciones de drenaje/Logout por considerarlo amplio y no verificado, con riesgo de cierres o estado retenido. Falló antes de arrancar el script y no aplicó ese bloque. El encargo sí autorizaba implementar; la entrega observacional se decidió por el límite técnico independiente del Logout nativo, no por falta de autorización del usuario. La alternativa de sondas y retirada de la rama activa sí quedó aplicada y revisada.
