# A2. Anfitrión doméstico como servidor persistente

Investigación del 2026-10-08. Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`.
Todas las direcciones son RVA. Trabajo estático: no se ejecuta el juego, no se compila ni se
modifica código del mod, y no se leen partidas completas. Scripts y volcados propios:
`C:\Emulador CD\build\research\vision\a2\`.

Etiquetas: **[V]** captura o medición previa en vivo; **[E]** desensamblado `.rsrc`;
**[E-x]** `.xtls`, solo evidencia parcial, nunca destino de hook; **[S]** diseño/inferencia;
**[R]** documentación externa sin contrastar con esta build. Los hechos de código fuente del
mod se citan como fuente de diseño, sin convertirlos en validación del juego.

**Actualización transversal de las pruebas:** [S, D-28] antes de la parte de E1 que provoca un guardado rechazado y de cualquier ensayo deliberado de blob inválido, disco lleno, corte de guardado o crash, debe completarse **RPT-0** de [A7 §4.3](vision-a7-transversal.md). P6 bloquea escrituras, pero no aísla lecturas ni adjuntos de reportes. [E/E-x] La ruta `0x2355700 → 0xEF51850 → 0x2356270 → 0x1317D40` puede solicitar adjuntos de la ranura fallida al SDK de Sentry, bajo condiciones; no se observó transmisión. Esta precondición completa todas las secuencias de pruebas que aparecen después, sin ejecutar ni habilitar ninguna.

## 1. Dictamen y contabilidad

**[S]** Un PC doméstico siempre encendido con el cliente anfitrión aparcado es una vía plausible;
un servidor autónomo sin cliente, escena ni jugador no está demostrado. La dependencia de escena,
física, navegación y la IA delegada a clientes es la puerta B5/P10, no la cantidad de hilos.
Fuentes: `segunda-sesion.md` §5 B5/B10; `vision-a0-base.md` §3 y §7;
`opcion-d-pruebas.md` E2/E3/E4/G2 y `opcion-d-p8.md` G3/G4.

**[S]** Se distingue `H` (personas jugando), `U` (usuarios del motor) y `G` (clientes remotos):
si el propietario juega, `U=H` y `G=H-1`; si su personaje es técnico, `U=H+1` y `G=H`.
Con techo de diseño `U≤10`, ocho personas requieren nueve usuarios cuando hay anfitrión técnico;
diez personas externas requerirían once, fuera del alcance. Esto fija por prudencia nueve
invitados como máximo con anfitrión técnico; no prueba un límite nativo de nueve.
Fuente: D-22, `vision-a0-base.md` y D0 §1 (101 claves por usuario).

**[S]** Aforo operativo demostrado por esta investigación: ninguno nuevo. El primer objetivo
sigue siendo dos jugadores; ocho personas es un objetivo condicionado, diez un techo de diseño.
En el perfil de tráfico intenso medido, ocho personas con anfitrión técnico producen escenarios
de **24–37 Mbit/s de subida modelada**, antes de ráfagas de carga. Un enlace nominal de
30 Mbit/s resulta demasiado ajustado para ese caso; 100 Mbit/s permite margen de red, pero no
resuelve B5, CPU, IA, guardado o contenido. Fuentes y fórmulas: §5 y
`data/vision-a2-capacidad.json`. No son resultados de una prueba multijugador.

## 2. Qué significa funcionar sin jugador activo

| Variante | Respuesta y evidencia | Riesgo | Prueba futura que decide |
|---|---|---|---|
| PC dedicado, proceso completo, personaje aparcado en zona tranquila | **[S] plausible**. Los hilos servidor existen separados, pero el proceso conserva cliente, GPU, escenas y cuenta Steam; `troc-bdo-y-runtime.md` §2.2 [V], `0x2773390` [E] | IA de NPC asignados al anfitrión y streaming remoto dependientes de escena | G3/E5: invitado combate cerca y luego lejos con anfitrión quieto; registrar dueño de NPC, posiciones, colisiones, navegación y daño |
| Ventana desenfocada o minimizada, calidad/resolución bajas | **[S] sin probar**. Menú/desenfoque/minimización no son equivalentes; no se localizó ni verificó aquí una política completa de trabajo en segundo plano | Suspensión o reducción del cliente puede quitar IA, drenaje de colas o física aunque continúe la red | G4: 10 min de cada estado por separado, misma escena del invitado, comparar TPS/latencia/IA/Q0–Q3 y consumo; repetir 2→4→8→10 |
| Cerrar cliente, retirar jugador técnico o dejar el título | **[S] no demostrado**. El servidor embebido arranca y termina con el juego; la sesión local participa en inicialización/campo/guardado. `segunda-sesion.md` B3/B5, P8 G2–G3 | Destruir el ancla puede destruir campo, reloj o servicios que todavía se comparten | Sólo después de ancla estable: salida del personaje técnico sin cerrar proceso, comprobar campo/propiedad/guardados y reingreso; no asumirlo por disponer de tres hilos |
| Zonas sin ninguna persona cerca | **[S] persistencia de estado factible; simulación continua no demostrada**. Los spawns se solicitan por cliente y la IA también; A1/P10, `0x2792A7B` [E] asigna controlador | No hay cliente que calcule navegación/IA; mantener todo activo consume claves y CPU | Vaciar zona, dejar volver a otro usuario y comprobar estado guardado, muerte/respawn, temporizadores y ausencia de duplicados |

**[S] Diseño prudente:** conservar primero un anfitrión técnico dentro del mundo, sin menús ni
minimización hasta medirlos; después probar segundo plano. No prometer un ejecutable dedicado sin
renderizado ni «headless». No se propone parche gráfico ni hook nuevo para ello. La prueba decisiva
de B5 es que el invitado opere en una zona que **no está cargada en el cliente del anfitrión**;
dos clientes juntos o un replay sintético no bastan. Fuente: `segunda-sesion.md` B5 y
`vision-a1-mundo.md` (streaming e IA).

## 3. Pausa, reloj e hilos

**[V] Menú:** en las capturas de `pausa-copia-congelada.md` §1 el cliente sigue recibiendo
`ServerTimeInfoAck` cada cinco segundos de pared mientras su reloj transmitido no avanza y
deja de producir Req; es evidencia del cooperativo anterior, no de ocho usuarios de opción D.
**[E] Barrera:** `0x27733F0` consulta TLS `+0x4460`; después de sacar un paquete pone byte `+2=1`
(`0x2773503`), si byte `+0!=0` confirma `+1=1` y espera `Sleep(10)` (`0x2773510–0x2773522`).
La sesión todavía se toma de `[ServerNet+0x18]` en `0x27735C7`.
Volcados: `vision/a2/27733f0.asm`, `27731c0.asm`.

**[E]** `SaveGameDataAutoTimer` 2077 (`0x2C35EB0`) recorre el registro de hilos de
`ServerNet+0xF8/+0x100`, solicita la barrera (`0x2C35F35`), espera a los hilos activos, llama
`0x28FC6F0` (`0x2C35F94`) y la levanta al volver (`0x2C35FC4`). No aparece timeout en esa espera.
`0x27751D0` registra el bloque TLS. Esto **no demuestra** que toda pausa de menú use esa barrera:
la búsqueda previa no encontró tal ruta. Tampoco prueba liberación de la barrera tras una
excepción. Fuentes: `vision/a2/2c35eb0.asm`, `27751d0.asm`, `opcion-d-e3-riesgos.md` §4,
`vision-a0-base.md` A0-C10. La frase P8 §5.3 «la pausa del anfitrión congela su servidor» es
demasiado amplia; puede detener IA del cliente sin detener el despacho de red [S].

| Hilo/familia | Evidencia y tarea | Límite relevante |
|---|---|---|
| `ServerRunThread` | **[E]** `0x2773390` fija TLS `+0x1EC=1` y llama al bucle `0x27733F0`: Q0, Req síncronas, devolución a Q3 | Un hilo para ese despacho; más núcleos no multiplican automáticamente su capacidad |
| `ServerWalkThread` | **[E-x]** proc `0x100B1F10` fija realm, registra TLS y llama `0x27731C0` con Q38 (`0x100B1FB6`); nombre de hilo visto en runtime [V] | Diferidas serializadas por contexto/usuario, locks compartidos; no asumir un worker por jugador |
| `ServerRepeatTimerThread` | **[E-x]** proc `0x100B2F40` fija realm y salta a `0x27725F0` (`0x100B2FB1`); nombre runtime [V] | Temporizadores, mantenimiento y devolución de búferes; no es un simulador autónomo completo |
| Cliente anfitrión | **[E]** `0x35B800` NetworkThread y `0x35D0C0` ejecución de Ack; principal ejecuta escena/IA | Sigue siendo necesario hasta demostrar lo contrario; Q2/Q3 deben drenar incluso cuando no renderiza |

Fuentes de tabla: `vision/a2/2773390.asm`, `100b1f10.asm`, `100b2f40.asm`, `27725f0.asm`;
`opcion-d-d0.md` §§3/5 y `troc-bdo-y-runtime.md` §2.2. No enganchar los dos procs `.xtls`.

**[S] Mantener servicio:** cola de operaciones de guardado global, sin guardados simultáneos;
preservar barrera para tomar una instantánea consistente. Ningún hook del hilo servidor espera
red, disco o usuario. La E/S externa y publicación de generaciones se diseñan en A3; no se
presupone que cada llamada nativa termine escribiendo. Riesgo: una sola llamada bloqueada puede
retener la barrera de todos. Prueba: medir duración de cada barrera con 2/4/8/10, disco lento y
desconexión durante guardado; éxito si termina o falla de modo acotado sin corrupción ni pérdida
de servicio. Fuente: `vision-a3-guardado-mundo.md` y revisión de `0x2355700 → .xtls 0xEF51850`.

## 4. CPU, memoria y escala

**[E]** `SendPacket` `0x2773870` recorre el vector de sesiones y deriva cada entrega a vf9;
`ToNear` `0x2BC4F70` recoge observadores. **[S]** Si cada uno genera `f` actualizaciones/s y todos
ven a todos, hay hasta `U(U−1)·f` entregas de relevo; es coste de difusión, no una ley del coste
total de CPU. Dispersos disminuyen destinatarios, pero aumentan regiones/NPC/escenas residentes.
Fuentes: `vision/a2/2773870.asm`, `segunda-sesion.md` §2.3, A1.

| Usuarios del motor U | Entregas por actualización por usuario, todos juntos | Factor respecto a U=2 | Claves de usuario/hijos | Mínimo de objetos de sesión/usuario/hijos |
|---:|---:|---:|---:|---:|
| 2 | 2 | 1 | 202 | 44.688 B |
| 4 | 12 | 6 | 404 | 89.376 B |
| 8 | 56 | 28 | 808 | 178.752 B |
| 10 | 90 | 45 | 1.010 | 223.440 B |

Tabla **[S] aritmética** sobre tamaños **[E]**: por usuario `0xD0 + 0x218 + 100·0xD8 = 22.344 B`,
`0x2A83310` y D0 §1. No incluye componentes, inventario, terreno, texturas, IA, asignador ni
objetos de sincronización; no es estimación de RAM del juego. Para ocho humanos con ancla
`U=9`: 909 claves, 201.096 B mínimos y 72 entregas por ciclo como envolvente conservadora
(el ancla quieta puede generar menos movimiento).

**[E]/[S]** Los búferes nativos tienen 0x8030 B C→S y 0x8060 B S→C (D0 §§3–4,
`0x35C670` y `0x2771B30`). Si ocho remotos **dispersos** reprodujeran cada uno los 529 Req/s
de la captura intensa (§5), implicarían aproximadamente **139 MB/s de reservas C→S** si cada
Req conserva esa ruta; juntos, la simulación NPC de un único controlador no se multiplica por 8.
Eso es rotación de
memoria, no 139 MB residentes por segundo. Una cola que retenga 100 ms serían ~13,9 MB de esos
búferes. Un segundo serían ~139 MB. El cuello puede ser el asignador/copia/locks aunque los
paquetes útiles sean pequeños. Las 100 ranuras hijas reservadas no equivalen a 100 personajes
activos con todos sus componentes.

**[S]** Replicar ingenuamente por ocho pares los topes actuales de 16 MiB de emisor +32 MiB
de cola elevaría la reserva potencial a **384 MiB**, antes de fragmentos, receptores y búferes
del juego (`troc_frame.h`, `server.cpp`). No se recomienda hacerlo: presupuesto global además
del presupuesto por par, reparto justo y admisión escalonada. Hoy esas colas no son ocho colas
independientes ni existen ocho sesiones externas (§6).

**[S] No hay cifra responsable de CPU/GHz o RAM total para 8/10** en estas fuentes. Plan de
medición: CPU por hilo (no sólo porcentaje total), tiempo p50/p95/p99 de despacho, antigüedad
de Q0/Q38, tamaño Q0–Q3, bytes privados/comprometidos, actores vivos, claves libres, NPC por
controlador y ms de barrera. Criterio propuesto: sin crecimiento sostenido de colas, p99 de
despacho adicional <50 ms fuera de cargas, reserva de CPU y RAM ≥30% y misma cadencia del
cliente que controla IA al minimizar. Son umbrales de proyecto, no capacidades comprobadas.
Repetir por etapa en zona tranquila, ciudad intensa, combate juntos y regiones dispersas.

## 5. Tráfico medido y subida doméstica

### 5.1 Método reproducible y límites

**[V]** Se leyeron las ocho capturas de `C:\Emulador CD\captures\cdmp_packets_20261003-*.bin`
con `vision/a2/trafico.py` y `ventanas.py`. Formato de `src/common/capture_file.h`:
cabecera 12 B; registros `<QBBHHHII>` de 24 B; dirección 1=C→S, 2=S→C. Se sumó **size**, no
`copied` ni tamaño del fichero. 105149 es v1, con 6.882 registros truncados; su tamaño declarado
permite medir volumen, no recuperar sus payload completos. Las otras siete son v2 sin truncar.
Se conservan hashes SHA-256, eventos de carga, número de registros y sumas en
[`data/vision-a2-trafico.json`](data/vision-a2-trafico.json); ocho CSV `vision-a2-bins_20261003-*.csv`
guardan bins de un segundo. No se suman otra vez los subpaquetes de `ScopeSenderAck`.

**[V]** No basta descartar los primeros 60 s: 105149 espera hasta t=90,45 para
`LoginCharacterToFieldServerReq3093` y 200328 hasta t=1232,99. `LoginCharacterToGameReq2601`
llega después, a t=121,533 y t=1265,406 respectivamente. Se excluyen **los dos ciclos iniciales**: comienzo de juego en
`ceil(segundo LoadingCompleteReq)+1 s`; se descarta el último segundo parcial. La columna
«sin cargas» excluye además intervalos de `GameLoadingStartReq` posterior hasta el próximo
`LoadingCompleteReq`, con margen de un segundo a ambos lados. Los percentiles incluyen los
segundos a cero. `after_60s` permanece en JSON sólo como auxiliar, **no alimenta estimaciones**.
Capturas con herramientas/mod y pausas: no representan una sesión limpia ni ocho usuarios reales.

Todos los tamaños de esta sección usan unidades decimales: **kB=1.000 B, MB=1.000.000 B,
Mbit/s=1.000.000 bits/s**; MiB sólo para límites de memoria del código.

| Captura | Inicio de juego (s) | Segundos sin cargas | C→S kB/s / paquetes/s | S→C kB/s / paquetes/s | p95 S→C kB en 1 s | Máximo S→C kB en 1 s sin cargas |
|---|---:|---:|---:|---:|---:|---:|
| 105149 | 145 | 1.006 | 62,50 /185,3 | 54,23 /129,1 | 247,09 | 714,17 |
| 112627 | 56 | 765 | 72,39 /215,8 | 59,18 /148,8 | 263,77 | 1.460,41 |
| 125936 | 99 | 75 | 36,29 /98,2 | 26,86 /94,6 | 88,29 | 226,75 |
| 131004 | 56 | 110 | 68,42 /209,9 | 97,18 /252,9 | 354,50 | 615,69 |
| 131648 | 56 | 199 | 54,55 /139,0 | 44,47 /156,1 | 216,14 | 368,69 |
| 134008 | 76 | 107 | 59,42 /166,8 | 50,41 /147,5 | 201,55 | 305,32 |
| 160139 | 56 | 686 | 25,60 /74,7 | 24,90 /69,5 | 136,91 | 522,11 |
| 200328 | 1.289 | 533 | 158,66 /529,2 | 169,41 /400,7 | 565,14 | 1.928,54 |

Toda la tabla es **[V] medición de captura**, no red física. Incluyendo recargas posteriores,
S→C medio es 56,48 kB/s en 105149, 86,80 kB/s en 112627 y 205,44 kB/s en 200328.
**[V]** El pico de login inicial 112627 es 5.394.433 B/1 s, consistente con D-19; el mayor pico
posterior medido es **6.555.235 B/1 s**, 200328 t=1792, durante recarga. El máximo de paquetes
de 112627 llega a 38.692/1 s en otra recarga. Una media modesta oculta cargas muy intensas.
Fuente: `vision-a2-trafico.json`, campos `login_peak_1s`, `post_login`, `steady_excluding_loads`.

### 5.2 Modelo para 2, 4, 8 y 10

**[S]** `B=P+W=169.411 B/s` es la media observada del perfil intenso 200328: mezcla respuestas
personales P y mundo W. No se pudo separar P/W ni descontar de forma fiable la actividad de
herramientas. Usar `G·B` como base es una hipótesis conservadora para esa mezcla, **insuficiente
por sí sola**: faltan los relevos vaciados `ExceptOne` de humanos **y NPC**.

`vision/a2/relevos.py` recuenta Move/Start/Rotate/ChangeAI Req de clases `0xA` y `0xB`, valida
su tamaño y calcula su Ack hipotético con los formatos de `mirror-ack-formats.md`:
payloads 235/725/177/672 B +5 B de cabecera, escritores `0x2974C00`, `0x2963FF0`, `0x29750E0`,
`0x297CF50` **[E]**. Los conteos de Req son **[V]**; que cada Req válida produzca un Ack aceptado
es **[S]**. No se fija generación B01. En 200328 sin cargas se obtienen:
`m=6.724 B/s` humanos (25,1 paquetes/s) y `J=91.594 B/s` NPC (203 paquetes/s). Escenario de
relevos intensos: `m=12.990 B/1 s` (p95 humano de 131648), `J=203.864 B/1 s` (p95 NPC de
200328). Datos: [`data/vision-a2-relevos.json`](data/vision-a2-relevos.json).

Fórmula juntos: **`G·[P+W+(U−1)·m+J]+b_extra`**. J se genera **una vez por conjunto de NPC**
y se replica a sus observadores, no se multiplica la simulación C→S por U si hay un controlador
único. Se usa G como envolvente de observadores remotos de los NPC; si el controlador es remoto,
no todos reciben su relevo. `b_extra` representa relevos no incluidos, p. ej. SyncActor y cambios
de física; **no está acotado por estas capturas**. Dispersos `G·B` presupone un controlador local
por región y ausencia de observadores de NPC ajenos. Más regiones aumenta trabajo de mundo.

Los números siguientes fijan `b_extra=0` y añaden **20%** para cabeceras/confirmaciones/pérdida
moderada: son **escenarios, no límites inferior/superior garantizados**. «Juntos A–B» usa la media
y p95 de relevos anteriores con igual base B. «Estrés» sustituye además B por 565.137 B/1 s:
no es un percentil del agregado. Script `capacidad.py`; datos `data/vision-a2-capacidad.json`.

| Personas H | Anfitrión también juega: U/G | Subida separados / juntos A–B (Mbit/s) | Estrés juntos (Mbit/s) | Anfitrión técnico: U/G | Subida separados / juntos A–B (Mbit/s) | Estrés juntos (Mbit/s) |
|---:|---:|---:|---:|---:|---:|---:|
| 2 | 2/1 | 1,6 /2,6–3,7 | 7,5 | 3/2 | 3,3 /5,3–7,7 | 15,3 |
| 4 | 4/3 | 4,9 /8,1–11,9 | 23,3 | 5/4 | 6,5 /11,1–16,3 | 31,5 |
| 8 | 8/7 | 11,4 /20,7–31,2 | 57,8 | 9/8 | 13,0 /24,2–36,6 | 67,0 |
| 10 | 10/9 | 14,6 /27,8–42,4 | 76,5 | Fuera de alcance | Requiere retirar ancla o que cuente como una de las diez personas | — |

**[S]** Consecuencia para conexiones **nominales** (no «velocidad típica» afirmada como hecho):
10 Mbit/s sirve para investigar dos y, con perfil ligero, cuatro; los escenarios intensos de
cuatro con ancla superan 10 Mbit/s antes de cargas. 30 Mbit/s permite investigar la fase de cuatro y
demasiado justo para ocho con ancla en el perfil elegido; 100 Mbit/s tiene margen para los
perfiles de ocho/diez, sujeto a RTT, configuración Steam y ráfagas. Reservar un 30% de la
subida para otras colas/jitter es política propuesta. En perfiles tranquilos la tabla sobreestima;
ciudades, nuevos relevos de IA o pérdidas pueden superarla. La prueba de red debe reproducir
datos intensos y una carga, no únicamente mover dos personajes en un campamento.

### 5.3 Ráfagas y admisión

**[S]** Para una ráfaga de **5 MB por remoto**, el tiempo mínimo de vaciado de red con 20%
de sobrecarga es `48·G / subida_Mbit_s` segundos. No incluye RTT, disco, descifrado, escenario,
tráfico de juego simultáneo ni límites del túnel; 6,56 MB necesita 1,31 veces esos tiempos.

| Personas y rol | G remotos | Subida 10 Mbit/s | 30 Mbit/s | 100 Mbit/s |
|---|---:|---:|---:|---:|
| 2, anfitrión jugador | 1 | 4,80 s | 1,60 s | 0,48 s |
| 4, anfitrión jugador | 3 | 14,40 s | 4,80 s | 1,44 s |
| 8, anfitrión jugador | 7 | 33,60 s | 11,20 s | 3,36 s |
| 10, anfitrión jugador | 9 | 43,20 s | 14,40 s | 4,32 s |
| 2, anfitrión técnico | 2 | 9,60 s | 3,20 s | 0,96 s |
| 4, anfitrión técnico | 4 | 19,20 s | 6,40 s | 1,92 s |
| 8, anfitrión técnico | 8 | 38,40 s | 12,80 s | 3,84 s |

**[S]** Una carga a la vez, con reserva previa de usuario/claves/cola y permiso de entrada
del servidor, evita ocho ráfagas que simultáneamente suman 40 MB. La cola de espera de personas
registradas debe vivir fuera del motor; esperar no crea `ServerUserActor`, ni gasta 101 claves.
La plaza no se libera hasta completar salida y comprobar limpieza (§7). El tiempo de servicio
real se mide en G3, con un remoto lento y uno jugando; si la antigüedad de paquetes del jugador
activo crece, no se admite la siguiente carga. Referencias: D-19, §6 y A4.

## 6. Steam y capacidad real del túnel actual

**[R] Consulta oficial del 2026-10-08:** `ISteamNetworkingMessages` crea sesiones por identidad,
usa la misma infraestructura que Sockets y puede utilizar SDR. Admite múltiples pares; la
documentación consultada no publica un tope de dos, ocho o diez. Eso no demuestra que la versión
cargada por este juego ni su configuración del AppID admitan nuestro caudal. La recepción
requiere aceptación; el orden fiable sólo está garantizado dentro del mismo par y canal. Una
sesión puede expirar por inactividad sin callback. Fuente:
[Valve, ISteamNetworkingMessages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages).

**[R]** La cabecera pública actual declara tamaño máximo de envío 512 KiB, búfer de envío
predeterminado 512 KiB, y límites de tasa `SendRateMin/Max` descritos como 256 K por defecto en
bytes/s. **No son valores medidos en la Steam API de Crimson Desert.** Hay que consultar la
configuración efectiva y `GetSessionConnectionInfo`, detectar `LimitExceeded`, caudal, pérdida y
tiempo de cola por par. Fuente:
[Valve, steamnetworkingtypes.h](https://raw.githubusercontent.com/ValveSoftware/GameNetworkingSockets/master/include/steam/steamnetworkingtypes.h),
constante `k_cbMaxSteamNetworkingSocketsMessageSizeSend` y opciones 9/10/11. Si se aplicara un
techo de 256 KiB/s por par, 5 MB tardarían como mínimo ~19 s antes de otros tráficos: D-19 no
puede presuponer drenaje superior a 1 MB/s sólo por tener colas grandes.

**[R]/[S]** No hace falta un relevo propio **por ser diez**. SDR ya ofrece comunicación P2P;
su ruta de servidor dedicado con certificados/tickets exige infraestructura y autorización
del proveedor del AppID, que aquí no se presume. Un relevo propio ordinario tampoco elimina
la subida total del anfitrión: seguiría transmitiendo copias por destinatario, salvo rediseñar
protocolo y replicación. No es requisito de esta primera arquitectura. Fuente:
[Valve, Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay).

**Estado del código, no evidencia en vivo:**

| Capa | Lo que existe y fuente | Consecuencia para 8/10 [S] |
|---|---|---|
| Elección de par | `src/mod/mod.cpp:234` construye **un** `ClientSession`; `peer` se fija al primer Hello, después sólo ese par entra al despachador; un `s2_link` | No admite ocho invitados cambiando una constante. Hace falta mapa SteamID→sesión→época→TrocLink y cola por par |
| Sesión del motor | `src/mod/session2/server.cpp` guarda una sesión 2 y una cola global; marcas para esa sesión. D-8 limita el relevo a los dos usuarios conocidos | Generalizar a U≤10, presupuesto global, equidad y exclusión por controlador; E2/E3b primero |
| Steam | `steam_transport.cpp:336–372`: flags **1|32**, envío no fiable sin Nagle y reinicio automático; la fiabilidad la añade `ReliableChannel` | No confundir «Troc fiable» con Steam Reliable; no añadir dos capas de retransmisión sin medir |
| Captura/túnel | Troc hasta `0x8000−0x20`; lotes de 64 KiB, emisor16 MiB, cola servidor32 MiB (`troc_frame.h`, `server.cpp:32`) | Un pico medido cabe aislado; ocho picos no quedan garantizados por una cola32 MiB |
| Canal fiable | `reliable.h`: ventana128 fragmentos; fragmento1100 B; cola2048; TrocLink frena en1024; reintento250 ms y máximo64 reenvíos/tick | El tamaño de ventana y RTT pueden limitar mucho antes que la tarjeta de red |
| Entrada Steam | Lee16 mensajes por llamada; `inbox_` conserva1024 (`steam_transport.cpp:355–373`); bucle principal limita512 datagramas por pasada (`mod.cpp`) | Hay que medir equidad/latencia y descartes con diez pares; no multiplicar512 por frecuencia supuesta |

**[S] Límite aritmético del canal fiable:** 128·1100=**140.800 B** pendientes. Sin pérdida,
llenado perfecto y ACK por RTT, el techo útil aproximado es `140800/RTT` B/s; `on_ack` sólo
retira y `tick` vuelve a llenar (`reliable.cpp`). A RTT de 20/50/100/200 ms: **7,04/2,82/1,41/0,70
MB/s** (56,32/22,53/11,26/5,63 Mbit/s). No es throughput probado: lotes pequeños, pérdida, tick y
configuración Steam lo bajan. El retraso de 250 ms puede retransmitir prematuramente si RTT
o cola se acerca a ese valor. Una mejora exige una ventana dimensionada con el producto
caudal·RTT, pacing y medición, no sólo aumentar16 MiB.

**[S] Sobrecarga:** cada lote lleva3 B+2 B por Troc; cada fragmento CDMP lleva16 B de cabecera
y15 B RelData, y cada confirmación22 B (`protocol.h`, `protocol.cpp:194–205`). En UDP/IPv4
hay además28 B por datagrama; Steam/SDR agrega transporte/cifrado propio que no está capturado.
Por eso ni los24 B del fichero CDPK ni los31 B de loopback nativo se añaden sin más a la red.
El20% de §5 sólo es reserva inicial de planificación; las confirmaciones de C→S también
consumen subida del anfitrión. La prueba debe medir bytes de NIC y Troc por separado.

**[S] Riesgo y prueba:** diez enlaces lentos pueden retener memoria y bloquear a los sanos.
Antes de aceptar cuatro: replay futuro de login+tráfico real a RTT20/100/200 ms, pérdida0/1/5%,
subida10/30/100; métricas por par y globales, atraso del jugador activo y cierre controlado del
par que exceda presupuesto. No descartar Troc arbitrarios para «ponerse al día»: el protocolo
actual presupone orden y entrega; reordenar o separar canales exige diseñar dependencias.

## 7. Entrada, salida, pérdida de conexión y aforo

### 7.1 La salida nativa no equivale a borrar un puntero

| Paso | Evidencia nueva/contrastada | Lo que todavía no acredita |
|---|---|---|
| Cliente solicita salir | **[E]** `0x8CAD10` publica2001 para sesiones; CSV: `LogoutSessionToServerReq`, handler `0x2C482E0` | D-16 lo bloquea actualmente en túnel; G5 sin validar |
| Handler2001 | Salto **[E]** `0x2C482E0→.xtls0x1089A360`; cuerpo **[E-x]** `0x1089A379–0x1089A380` llama vf3 si no hay actor; con actor llama `0x2068340(...,4,...)` en `0x1089A3AE` | El retorno éxito no demuestra que ya se hayan retirado personaje, sectores, claves y campo |
| `NwVirtualAsyncSession::Close` | **[E-x]** `.xtls0xB8A9028–0xB8A9031` toma y limpia `sesión+0x90`; reinicia crypto y transporte; con `+0xA9==0`/actor válido llama vfA8 en `0xB8A90E1` | No legitima enganchar thunk `0x1437600` ni invocar manualmente Close fuera de su ciclo nativo |
| `ServerUserActor::vfA8` | **[E]** vtable `0x5B257A8+0xA8→0x2A7D790`; compara sesión entregada con `usuario+0x1F8` (`0x2A7D7D1`), sólo si coincide pone0 (`0x2A7D830`), escribe hora `+0x200` (`0x2A7D837`) y avisa componentes vfF0 (`0x2A7D8AB`) | No hay borrado inmediato de usuario en esa función. Protege frente a que un Close viejo quite una sesión nueva, pero no drena nuestro túnel |
| Predicado de expiración | **[E]** vfB0 `0x2A7D8D0`: para ciertos estados compara `+0x208` con argumentoRDX; si sesión1F8==0 y timestamp200!=0, compara200 con umbralR8 (`0x2A7D91A–0x2A7D933`) y devuelve verdadero al vencer | Se encontró el predicado, **no quedó cerrado quién lo programa ni el plazo efectivo**. No afirmar limpieza en30s/60s |
| Temporizadores de logout | **[E]**2063 `LogoutAllActorOnceTimer` `0x2C34ED0→0x2A84160`, trabaja sobre SAM;1007 `0x2777700` sólo decrementa contador global. **[E-x]**2004 `0x2C48410→0x1089C4A0` usa referencias internas |2063 es global, **no usarlo para echar a un invitado**. No sustituir cleanup por1007 ni permitir2004 procedente de red |

Fuentes: `vision/a2/8cad10.asm`, `2c482e0.asm`, `1089a360.asm` (cuerpo hasta `0x1089A493`),
`b8a9000.asm` (hasta `0xB8A9132`), `2a7d790.asm`, `2a7d8d0.asm`, `2c34ed0.asm`,
`2a84160.asm`, `1089c4a0.asm`; script `logout.py` y CSV de Troc. El código tras un `ret/int3`
en un volcado de ventana `.xtls` no se toma como continuación semántica.

### 7.2 Protocolo de salida propuesto [S]

1. Identidad autenticada y época propia por conexión. Al recibir salida o agotar heartbeat,
   pasar a «retirándose», bloquear nuevas Req de esa época y suspender admisiones que la reutilicen.
   Desconexión de Steam no borra una sesión virtual del motor; existe que traducirla a salida nativa.
2. Terminar transferencia de IA antes de perder controlador, o poner sus NPC en estado inactivo
   coherente hasta recuperarlos. `ChangeAiControlAck2508` sólo lleva clave, no dueño/época, y el
   cheat exige solicitante dueño: A1; no se considera migración lista.
3. Serializar guardado del jugador con A3/A4 y barrera nativa; verificar publicación de generación,
   no sólo retorno0. Un corte no promete conservar el último instante anterior al fallo.
4. Originar desde el servidor **su mensaje nativo2001** por la sesión correcta y su hilo, tras
   verificar formato/canal interno; no abrir indiscriminadamente todos los temporizadores al túnel.
   En corte abrupto el coordinador origina esta acción: no confía en recibir un Bye del cliente.
5. Comprobar `sesión+0x90`, `usuario+0x1F8`, SAM, hijos, campo+0x2A0, observadores/refcounts y
   NPC controlados. Dejar que el ciclo nativo libere referencias. **No borrar entradas del campo
   a mano** ni reciclar la sesión mientras pueda haber diferidas o mensajes viejos.
6. Vaciar colas del mod de esa época y rechazar toda llegada tardía. Sólo liberar plaza cuando
   la limpieza esté observada; si queda huérfano, aislar plaza, registrar fallo y detener nuevas
   admisiones antes de arriesgar mundo. Reinicio recuperable exige un guardado publicado válido.

Riesgos: campo compartido cuyo dueño se va, claves reutilizadas, pérdida de IA, guardado parcial,
relogin de la misma cuenta antes de limpieza. **Prueba G5 ampliada** después de G3/E5: salida al
título, cerrar cliente, cortar red, reconectar misma cuenta y corte durante autoguardado; 50 ciclos
en dos usuarios, después100 ciclos con4 y8, diez sólo al superar esas puertas. Comparar baseline
de referencias/colas/usuarios/claves después de cada ciclo. El número de ciclos es diseño de
prueba, no evidencia. Fuente: D-18, A1/A3/A4 y rutas de §7.1.

**[S] Cola de espera:** registro de30 SteamID no requiere30 usuarios nativos vivos. Guardar
registro fuera del motor; aforo incluye usuarios cargando, retirándose y el ancla. FIFO con
token de reserva breve y heartbeat propio, una carga activa; cancelación no crea personaje.
AlcanzadoU=2/4/8/10 según etapa, los demás esperan fuera. La recuperación de una plaza tras
corte depende de limpieza real, no de que el socket parezca cerrado. Fuente: D-22 y A4.

## 8. Duración de horas o días

| Riesgo | Evidencia y alcance | Verificación antes de prometer24/7 [S] |
|---|---|---|
| Colas devueltas que no drenan | **[E]** Q2/Q3 devuelven para liberar, no son reservas reutilizables; D0 §3; `0x27725F0`/`0x35B800` | Ingreso/egreso y pendientes por cola; ancla minimizada frente a visible; memoria debe estabilizarse tras cargar y descargar |
| Sesiones viejas retenidas | Diseño actual D-13/D-14 deja la sesión vieja al cambiar servidor; `server.cpp` | Contar objetos por época/reinicio; no convertir la retención de seguridad de pruebas en un supuesto ciclo de vida de varios días |
| Índices/generaciones de actor | **[E]** `pendiente-rango-claves.md`: índice20 bits/generación8, Find `0x2A81900` por índice; A1 | La primera reutilización puede importar antes del wrap de256 generaciones. Cortar colas antiguas/épocas; no asumir válido un paquete por coincidir índice |
| Contadores de tiempo | Código del mod usa ms `u32`; diferencias sin signo en `reliable.cpp` | Wrap aritmético a49,71 días; prueba sintética futura cruzando wrap de reloj e IDs y ejecución continua real. No declarar fallo automático: algunas comparaciones ya soportan wrap |
| Acks aplazados del cliente | **[E]** `ClientNet+0xF0`, `0x35CD50`; P8 §5.5 | Actor nunca listo/retirado puede mantener trabajo repetido: medir profundidad/edad y eliminar sólo con ciclo nativo o reinicio coherente |
| Barrera/E/S y commit aplazado | **[E]**2077 espera sin timeout visible; A3, `0x2355700→.xtls0xEF51850` puede diferir escritura | Latencia/errores de disco y generación publicada; forzar corte en cada fase con copias de prueba del usuario |
| Capturas y logs | **[V]**200328 acumula235 MB de fichero en30,8min, tras~20,5min esperando en título; `vision-a2-trafico.json` | Capturas de diagnóstico acotadas/rotación y espacio reservado; no grabación completa ilimitada por defecto |

**[S]** Prueba de duración por etapas:2 usuarios2h,4 usuarios8h,8 usuarios24h; diez sólo tras
8 estable y un ensayo de24h con el perfil más caro aceptado. Repetir cargas, muerte, viaje,
entrada/salida y dejar periodos vacíos. Estos plazos son criterios propuestos, no capacidades
observadas. Registrar tendencia después del calentamiento (memoria, actores vivos, claves libres,
referencias, colas, temporizadores), no sólo pico de RAM; un ascenso que vuelve al baseline puede
ser caché, un ascenso por cada reconexión es señal de fuga. Ventanas distintas de calendario y
tiempo monotónico: el tiempo de espera usa reloj monotónico, la semana del personaje corresponde A6.

## 9. Clientes de confianza y desconocidos

**[E] Validación real pero localizada:** `ServerNet::OnRecvPacket` `0x2773C20` valida handler,
canal/tamaños y usuario/slot (D0 §2); `DoFrameEventReq` `0x296D130` comprueba atacante/controlador,
estructura y objetivos, y `0x2A896E0` comprueba la acción que el servidor tiene en curso.
En `0x2A8986C` compara hash, `0x2A8987A` id u16 y `0x2A8989E–0x2A898A0` rango de evento.
**[E] Precisión del verificador:** una diferencia de hash/id salta a `0x2A89E7E` y devuelve
resultado 0 sin ejecutar el evento; el rango inválido también puede devolver 0 cuando el byte
de estado `+0xC0` es positivo (`0x2A898A8–0x2A898B5`). La comprobación existe, pero un retorno
de éxito no prueba aceptación ni ejecución: la prueba futura debe observar efectos además del código.
Fuentes: `vision/a2/296d130.asm`, `2a896e0.asm`; `combate-coop.md` §1.4. No demuestra verificación
autoritativa de colisiones, navegación o de las demás 486 Req.

**[E] Contraejemplo concreto:** `VaryStatByAccInputReq2181` (`0x2972720`) lee clave/stat/delta,
busca cualquier actor por la clave recibida (`0x2972889`), resuelve stat y llama
`0x17AE4B0` (`0x2972969`). Entre la deserialización correcta y esa modificación no aparece
comparación de dueño con el usuario de la sesión ni lista de stats autorizados. El stat debe
existir en la tabla; **no significa que todo identificador sea válido**. El usuario
`[TrocParams+0]` sólo se consulta en la rama de error de formato para diagnóstico
(`0x29729C5`), no para autorizar esa ruta exitosa. **No equivale a decir que TrocParams no se
use:** su payload, tamaño y bandera se leen antes en `0x2972742–0x297276B`.
Fuente: `vision/a2/2972720.asm` completo, `combate-coop.md` §1.4. La repercusión exacta sobre cada
stat exige prueba controlada, pero ya impide afirmar que el servidor valida todo.

**[S]** Primera explotación sólo con lista de personas conocidas. Para desconocidos hace falta
inventario positivo de Req necesarias, permisos por estado/SteamID/actor/época, límites por par,
restricciones de cambios de inventario/progreso, y revisión por mensaje de rutas que confían en
datos del cliente. La lista negra cheat/dev/guardado existente no cubre el contraejemplo2181
clasificado «combat». La identidad Steam autentica el origen del transporte, no honestidad del
cliente; la cuenta u64 del login se asigna desde la identidad aceptada, no desde el payload.
Esto es el diseño del catálogo externo: **no está demostrada una identidad nativa durable**.
A4 encuentra que `UserLogin0x2C17520` puede escribir−1 en `UserLoginComponent+0x78`
(`0x2C1759D`), mientras relogin `0x2C17AB0` compara ese campo con cuenta
(`0x2C17B84–0x2C17B8C`); el almacenamiento efectivo de la cuenta en rutas VM sigue abierto.
Fuentes: `server.cpp` filtros/D-14/D-16, CSV, §6 y A4. Riesgo alto: corrupción de progreso o
denegación de servicio por paquetes válidos. Prueba futura primero en harness y partidas de
ensayo: sesión A intenta una operación sobre actor de B y debe rechazarse antes del handler.
No se ejecutó esa prueba ni se cambió el filtro en esta investigación.

## 10. Verificación independiente y refutación de diez afirmaciones

Revisión independiente del 2026-10-08 contra el exe real en disco, SHA-256
`57da440d72f4db974f25fef047cf84c4dadd999a88cb2a3c5af4c9bd67fde1e7`, PE `0x6AB28F00`.
Los scripts propios están en `C:\Emulador CD\build\research\vision\verificacion\a2\`.
`verifica_estatico.py` relee trece rangos, sin usar los volcados anteriores para decidir.
`verifica_red.py` relee dos capturas y recalcula el modelo.
Evidencias y hashes de cada rango: [`data/vision-verifica-a2.json`](data/vision-verifica-a2.json).
**Resultado: diez confirmadas dentro de su alcance estático, ninguna promovida a validación
en vivo.** «Confirmada» en `.xtls` conserva **[E-x]**, no habilita un hook ni garantiza que
el runtime protegido siga exactamente esos bytes. Los demás riesgos/pruebas están en §§2–9.

| ID | Veredicto | Evidencia propia y límite que se intentó refutar |
|---|---|---|
| A2-C1 | Confirmada [E] | `0x27733AB` escribe TLS `+0x1EC=1`; `0x27733E4` salta a `0x27733F0`; en la rama de paquete normal `0x27735C7` carga la sesión de `ServerNet+0x18`. No acredita una sesión única en todo el motor ni resuelve el despacho multiusuario. |
| A2-C2 | Confirmada [E] | Pop en `0x27734EA`; sólo con paquete pone `TLS4460+2=1` en `0x2773503`. `0x2773507–0x2773522` espera con `Sleep(10)`; IAT `0x51EB490` resuelto independientemente a `KERNEL32!Sleep`. No es evidencia de pausa de menú. |
| A2-C3 | Confirmada [E] | `0x2775232–0x2775337` registra el puntero TLS4460 en el vector `+0xF8/+0x100`; `0x2C35F35` solicita parada; `0x2C35F74–0x2C35F7E` espera sólo activos sin confirmación; guarda en `0x2C35F94` y libera en `0x2C35FC4`. Sin timeout visible; sólo liberación en retorno normal. |
| A2-C4 | Confirmada [E-x] | `0x100B1F30` realm 1; espera habilitación `+0x108`, registra TLS en `0x100B1F5C`, pasa cola `+0x38` en `0x100B1FAD` y llama `0x27731C0` en `0x100B1FB6`. No acredita un worker por usuario; lectura termina en el ret `0x100B1FCA`. |
| A2-C5 | Confirmada [E-x] | `0x100B2F5B` realm 1, espera `+0x108`, salta a `0x27725F0` en `0x100B2FB1`. Confirma entrada al bucle ya identificado; no demuestra simulación autónoma ni servicio sin cliente. |
| A2-C6 | Confirmada [E-x] | Thunk `.rsrc` `0x2C482E0→0x1089A360`; sin actor llama sesión vf3 en `0x1089A380`; con actor fija `r8b=4` en `0x1089A3A3` y llama `0x2068340`. Solicita transición 4; no prueba limpieza terminada al retornar 0. |
| A2-C7 | Confirmada [E-x] | `0xB8A9028` conserva actor y `0xB8A9031` limpia sesión `+0x90`; sólo si `+0xA9=0` y referencia válida alcanza vfA8 en `0xB8A90E1`, pasando la sesión en R8. No acredita borrado inmediato del usuario. |
| A2-C8 | Confirmada [E] | Vtable `0x5B257A8+0xA8→0x2A7D790`; `0x2A7D7D1` exige coincidencia con `usuario+0x1F8` antes de cero/timestamp/vfF0 (`0x2A7D830/837/8AB`). Vtable `+0xB0→0x2A7D8D0`: compara timestamp con umbral recibido, sin plazo fijo demostrado. |
| A2-C9 | Confirmada, precisada [E] | Compara hash/id/rango en `0x2A8986C/87A/89E`; hash/id distintos salen por `0x2A89E7E` con resultado 0 sin procesar. El rango inválido puede devolver también 0 (`0x2A898A8–0x2A898B5`). No confundir comparación y descarte con un error explícito ni con validación de toda Req. |
| A2-C10 | Confirmada en el handler [E] | Cuerpo completo `0x2972720–0x2972A94`: búsqueda por clave `0x2972889`, resolución de stat existente `0x297289A–0x297291A`, aplicación `0x2972969`; no compara dueño con usuario en la ruta exitosa. `[TrocParams+0]` sólo se consulta para diagnóstico `0x29729C5`; tamaño/payload/flags sí se usan. No demuestra efecto arbitrario en todo stat ni ausencia de controles fuera del handler. |

**[V] Contraste acotado de capturas:** se reprodujeron registros, hashes, truncamientos,
ventanas, medias y p95 de 105149 y 200328; no se vuelven a certificar aquí las otras seis.
En 105149, ventana de juego **[555,556) s**, se declaran **842.360 B** (C→S 128.188,
S→C 714.172), pero sólo se copiaron **297.327 B**, con **123 registros truncados**.
La ventana está después del login y fuera de la recarga [464,476); sumar `copied` perdería
545.033 B. En 200328, 3093 llega a 1232,986 s, 2601 a 1265,406 s y segundo
LoadingComplete a 1287,074 s; juego desde 1289 s y **533 segundos sin cargas**. Esto evita
mezclar la espera del título con el tráfico jugable. Fuente propia: `verifica_red.py`.

**[S] Contraste aritmético:** las siete filas dentro del techo de §5, ráfagas de 5 MB y
ventana de 140.800 B coinciden con la fórmula declarada. Unidades decimales y MiB están
diferenciadas correctamente. **24,18–36,65 Mbit/s con ocho personas y ancla no es un intervalo
garantizado, benchmark ni percentil agregado:** falta `b_extra`, la correlación entre flujos
y medir sobrecarga/RTT/Steam. Que las cuentas cuadren no valida sus hipótesis ni garantiza
que una conexión de 100 Mbit/s sostenga el juego. No se cambian las cifras del escenario.

## 11. Archivos y cierre del área

- Informe: `docs/research/vision-a2-servidor.md`.
- Datos versionables: `data/vision-a2-trafico.json`, ocho `vision-a2-bins_20261003-*.csv`,
  `vision-a2-relevos.json`, `vision-a2-capacidad.json`, `vision-a2-estatico.json` y
  `vision-verifica-a2.json`.
- Scripts/volcados: `C:\Emulador CD\build\research\vision\a2\trafico.py`, `ventanas.py`,
  `relevos.py`, `capacidad.py`, `estatico.py`, `logout.py` y `*.asm` (lectura del exe).
- No se propone ningún hook nuevo. Se reutilizan puntos ya documentados y firmados en
  `data/vision-firmas-2976.json` para futura observación; no enganchar ni thunks ni `.xtls`.
- Propuesta de decisión para diario: ancla primero, U≤10 incluido ancla; admisión de una carga,
  presupuestos globales/por par; conservar barrera; validar salida+IA+B5 antes de aforo4; no abrir
  a desconocidos por el contraejemplo2181. El coordinador decide y numera la entrada.
