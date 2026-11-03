# Opción D: pruebas en el juego, en orden

Build del juego 1.0.0.2976. Rama `opcion-d` (worktree `C:\Emulador CD-opcion-d`). Todo lo de la
opción D está detrás de `[session2]` en `cdmp.ini` y apagado por defecto. **Aforo: 4 jugadores**
(decisión D-33).

Esta guía sigue la secuencia única de [vision-sintesis.md](vision-sintesis.md) §5 (pruebas T00-T21,
que prevalece sobre la guía anterior) y la adapta a 4 jugadores. Cada prueba lleva su código Txx
para seguirla en la síntesis.

Las pruebas van en tres bloques:

- **A. Puedes hacerlas ya:** no escriben en el juego ni provocan errores.
- **B. Esperan a RPT-0:** el código existe, pero activan el guardián del guardado, y eso puede
  provocar guardados fallidos con informe de error.
- **C. Necesitan trabajo nuevo antes:** hay que programar y revisar piezas; no se pueden hacer
  todavía.

Dentro de cada bloque, **en orden**: cada prueba solo tiene sentido si la anterior salió bien. Si
una falla, para y mándame `bin64\cdmp.log` (y la captura `cdmp_packets_*.bin` si la hay).

## Antes de empezar (una vez)

1. **Coordinar con el otro proyecto:** las dos ramas comparten la carpeta del juego. Instalar esta
   rama pisa `cdmp.asi`, `winmm.dll` y `cdmp.ini` del cooperativo de `main`. Además, esta rama usa
   el **protocolo 8**: no conecta con un kit de `main` (v7) ni con un `cdmp_peer` viejo.
2. Compilar e instalar desde el worktree:
   ```
   cd "C:\Emulador CD-opcion-d"
   .\scripts\build.ps1
   .\scripts\deploy.ps1
   ```
3. **Copia de las partidas:** `%LOCALAPPDATA%\Pearl Abyss\CD\save` a `backups\`, con la fecha en
   el nombre.
4. En `cdmp.ini`: `[coop] enabled=0` (la opción D no arranca con el cooperativo de copias).
5. Regla de todas las pruebas: **no guardar a mano** salvo que la prueba lo pida, y cerrar el juego
   **sin guardar**.

---

## Bloque A: puedes hacerlas ya

Ninguna activa el guardián ni escribe en el servidor del juego. El juego guarda con normalidad.

### A1 (T01, E0): sonda de solo lectura

```ini
[session2]
enabled=1
```
(`probe=1` es el valor por defecto. Solo lee: ni hooks en el servidor ni guardián.)

1. Abre el juego, carga tu partida y entra en el mundo. Juega 2 minutos.
2. Cierra sin guardar.

En el log, en las líneas `Session2 probe (install)` y `(minute N)`:
- `out queue ... (ClientNet+0x18 same)` y `Q2 ... Q3 ... (... same)`;
- `user ... key 0x90100000 +0x1F8 == session` y `crypto +0x24 0x0`;
- el número de `free actor keys` (apúntalo: E2 lo comparará);
- la línea `host character ... field 0x... owner ... users 0x1 ...`;
- `server FieldTime ...`.

**Si algún dato sale `DIFFERENT` o `?`, para aquí:** la estructura en memoria no es la que dice el
análisis y no hay que pasar a pruebas que escriben.

### A2 (T01, E0b): buscadores vaciados (solo registro)

```ini
[session2]
enabled=1
finder_log=1
```
Muévete 2 minutos cerca de NPC y pelea un poco. Cierra sin guardar.

En las líneas `Session2 probe (minute N): finders control A/B nonempty`, **A debe ser 0**. Eso confirma que el
relevo "excepto el que controla" está vaciado en esta build.

### A3 (T06, G0): los hooks del invitado en Shadow, sin cambiar nada (P8, observe)

Solo en Shadow, sin tu PC. `cdmp.ini` de Shadow: `[session2] enabled=1 guest=1` (sin
`guest_mode`, que por defecto es `observe`: todo sigue local, solo se cuenta). Arranca, juega 2
minutos y cierra sin guardar.

- `Session2 guest (P8): hooked ... mode observe` **sin** `TOO LATE`. Si sale `TOO LATE`, el mod
  llegó tarde al arranque: dímelo.
- `connection local | connections remote 0 local 1 (first packet of the last one: 2319)`: la
  primera Req de la conexión es `ConnectCryptogramToServerReq`, como dice el análisis.
- `local packets N` crece mientras juegas, y la partida va normal.

### P8-T: refresco de ranuras (un PC, antes de jugar con Shadow)

Comprueba en tu PC, sin Shadow, el arreglo del título del invitado
([pendientes/p8-titulo.md](pendientes/p8-titulo.md)): que la llamada nativa que rehace la tabla de
ranuras (`0x1870C20`) funciona desde el NetworkThread y que el login normal de después no se
queda «ocupado». No escribe partidas (ver §5 de p8-titulo.md); aun así, haz antes la copia de
`%LOCALAPPDATA%\Pearl Abyss\CD\save`.

1. `cdmp.ini`: `[coop] enabled=0` y en `[session2]` **solo** `enabled=1`, `guest=1`,
   `guest_mode=observe` (o sin la línea) y `guest_slot_refresh_test=1`. Nada de `create_session`,
   `accept_tunnel`, `relay_fix`, `share_field` ni `mirror_host` (con ellas el mod no arranca P8).
2. Arranca el juego y espera al título **sin tocar nada** 30 s. Mira que «Continuar»/«Jugar» y
   «Cargar» estén activos, como siempre.
3. Pulsa «Continuar», entra en el mundo, anda unos segundos y cierra el juego **sin guardar**.
4. Quita `guest_slot_refresh_test=1` (o ponlo a 0) y deja `guest=0` si vuelves a otras pruebas.

En `bin64\cdmp.log`, en este orden:
- `Config: ... guest (P8) true mode observe slot refresh test true`;
- `Session2 guest (P8): hooked ...` **sin** `TOO LATE` (si sale, el mod llegó tarde: repite);
- `DIAGNOSTIC guest_slot_refresh_test=1: the title's slot refresh runs once ...`;
- quizá `diagnostic title slot refresh waits (no WorldSystem yet / no save manager ...)`: normal
  mientras arranca;
- **`title slots refreshed locally (no local login, DIAGNOSTIC ...): N slots [...] | before 0 |
  slot folders M | Play/Load enabled | X ms | manager 0x... (server's the same / not set yet) |
  busy byte now 0 | before the client's connection ...`**. Lo esperado: `before 0`, `N` = tus
  ranuras con partida (las de `slot0..2`/`slot100..108` de la carpeta de guardado), `busy byte now
  0`, unos pocos cientos de ms como mucho;
- después, la conexión normal (`connection ... -> local: guest_mode=observe`) y la partida carga
  como siempre al pulsar «Continuar»: eso demuestra que el login nativo no encontró el gestor
  «ocupado»;
- al minuto, `title slot refresh: 1 done, last N slots` en el resumen del invitado.

Para y mándame el log si: sale `title slots NOT refreshed` (con el motivo: `differs at`,
`is not its own WindowsLock`, `is not the server's`, `exception 0x...`, `gave up`), el juego se
cuelga en el arranque o en «Continuar», el título sale con los botones grises, o «Continuar» da un
error. Si sale `the client had already connected`, el mod llegó tarde: repite el arranque.

En la prueba real con Shadow (`guest_mode=remote`, sin `guest_slot_refresh_test`) la línea
esperada es `the client's LoginUserToFieldServerReq 2490 went to the host ...` seguida de
`title slots refreshed locally (no local login): N slots [...]`, **antes** de que aparezca el menú
del título, y «Continuar»/«Cargar» activos.

### A4 + RPT-1: la partida de capítulo 2, sin crashpad — **hecha** (RPT-1 pasa; ver diario)

Junta dos pruebas. **RPT-1:** el juego arranca y va normal sin `crashpad_handler.exe` (K2).
**A4 (T16, A5-IN1):** en vez de jugar la introducción (varias horas), se usa una partida que ya
está en el punto de exploración libre: "Chapter 2 Normal" (Nexus 2863), guardada en
`C:\Emulador CD\backups\plantillas\` e instalada como **ranura 108**. Puede cambiar más adelante.

Ini: [ini/cdmp.rpt1-a4.ini](ini/cdmp.rpt1-a4.ini) (sonda, red local para las marcas, grabación de
paquetes). Yo lo dejo todo instalado.

1. Abre el juego. Si no arranca o sale algún aviso raro, ciérralo y dímelo (es RPT-1).
2. Carga la partida de capítulo 2 (ranura 108). Si el juego no la acepta, dímelo: eso también es
   un resultado (una partida de otra cuenta no serviría de plantilla tal cual).
3. Pulsa **Ctrl+F12** en cuanto controles al personaje, y otra vez tras 2-3 minutos jugando.
   Apunta qué pasaba en cada marca (dónde estás, si hubo escena o tutorial).
4. Cierra **sin guardar**. Avísame: leo yo los logs, la grabación y `DumpCache`.

---

## Bloque B: listo para empezar (RPT-0 y RPT-1 hechos)

El código ya existe, pero estas pruebas activan el **guardián del guardado** (P6): en cuanto una
opción escribe en el juego, el mod rechaza toda escritura en la carpeta de partidas. Cualquier
guardado, también un **autoguardado**, fallará. Según el análisis (`vision-a7-transversal.md` §4),
un guardado fallido puede pedir adjuntar los `*.save` de esa ranura a un **informe de error para
Pearl Abyss**. El guardián impide escribir, pero no impide leer ni adjuntar.

### B0 (T02, RPT-0): informes de error contenidos — **hecho, falta RPT-1**

Auditoría: [opcion-d-rpt0.md](opcion-d-rpt0.md). Contención elegida (D-37): **K1 + K2**.
- **K1** (en el mod): mientras una etapa escribe, el mod corta la función del juego que manda los
  informes con archivos adjuntos (guardado fallido). En `cdmp.log` sale `Session2 report guard: ...
  NOT sent` con el motivo y los archivos que se habrían adjuntado.
- **K2** (en la carpeta del juego): `crashpad_handler.exe` está apartado en
  `C:\Emulador CD\backups\crashpad` mientras dure el bloque B. Sin él no sale ningún informe.
  `.\scripts\crashpad.ps1` dice el estado y si hay algo pendiente en `DumpCache`;
  `-On` lo devuelve (también lo devuelve "verificar archivos" de Steam).

**Antes de B1:** que RPT-1 (abajo, junto con A4) salga bien. **Después de cada prueba del bloque
B:** `.\scripts\crashpad.ps1` debe decir `nothing pending`.

### B1 (T03, E1): sesión 2 dormida

Con la partida de laboratorio (RPT-0 cerrado).
```ini
[session2]
enabled=1
create_session=1
```
1. Juega **10 minutos** con normalidad: combate, montar, menú y viaje rápido si puedes.
2. **Intenta guardar a mano una vez** (menú). Apunta qué hace el juego: mensaje de error, nada, se
   cuelga… Si se cuelga o se cierra, para aquí y mándame el log.
3. Cierra sin guardar.

En el log:
- `Session2 save guard ON: writes under '...\CD\save' are refused` al arrancar, y al guardar,
  líneas `Session2 save guard: refused create '...save...'`;
- `Session2: hooked ServerNet::OnRecvPacket` al arrancar;
- `session 2 ready` en las líneas periódicas y `Session2 probe ... session 2 0x...`;
- `out: 0 packets` (nadie ha hecho login);
- **el juego no debe notar nada**: ni tirones ni cierres.

### B2 (T04, E2): login del usuario 2 (un PC, con `cdmp_peer`)

`cdmp.ini`:
```ini
[net]
enabled=1
transport=udp
role=guest
peer_host=127.0.0.1
peer_port=37015
[session2]
enabled=1
create_session=1
accept_tunnel=1
```
1. Carga la partida de laboratorio y entra en el mundo.
2. En una consola, desde el worktree:
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e2_112627.troc --record D:\cdmp-grabaciones\e2.bin
   ```
3. Espera a que `cdmp_peer` diga `script done` (o `FAILED`) y **cierra el juego del todo** (sin
   guardar, y sin volver a la pantalla de título: en el primer B2 se colgó ahí).

Repetición tras D-39/D-40 (id 0): si `cdmp_peer` dice `Server sent Nak 1013`, apúntalo (el
servidor rechazó el login; suele ser porque estaba guardando) y prueba otra vez. Tras el login
verás llegar `ServerTimeInfoAck` (2188) cada ~5 s: es normal.

Qué tiene que pasar:
- en `cdmp_peer`: el guion llega al final y recibe `2716` (`MainUserDataEndAck`);
- en el log: `Session2 probe (user 2 logged in)` con `session 2 ... user 0x... key 0x9010xxxx`;
- `free actor keys` baja **101** respecto a la línea de la sonda de justo antes del login (no a A1:
  depende de la zona);
- **el anfitrión sigue jugando como si nada**. Si te echa al título, el servidor tomó el login por
  un relogin del anfitrión: mándame el log. El análisis advierte que reescribir la cuenta del
  login puede no bastar (`vision-a4-identidad.md` §2).

### B3 (T05, E3): personaje del usuario 2 (misma ranura que el anfitrión)

Ini: [ini/cdmp.b3.ini](ini/cdmp.b3.ini). En B2 la ranura fue la **108**: usa `--var SAVE_SLOT=108`.

Mismo `cdmp.ini` que B2. Mira en el log de B2 la línea `host save slot N` (la ranura cargada).
**Si es la 3 (CrossSave) o la 206, usa una ranura normal (0, 1 o 2)**: el anfitrión rechaza esas
dos para el usuario 2.

**Quédate quieto en un sitio tranquilo** durante todo el guion: no mueras, no viajes, no guardes.
El gestor de guardado es único, y una carga tuya a la vez que la del usuario 2 se pisarían.
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_112627.troc --var SAVE_SLOT=N --record D:\cdmp-grabaciones\e3.bin
```
- El guion pasa por los dos ciclos de carga y acaba en `LoadingCompleteAck`.
- **Si se atasca en `LoadingCompleteAck`, no repitas con la variante `01`:** esa variante puede
  meter en el mundo la hora de la otra partida. Mándame el log y se investiga antes.
- **Lo esperado:** el servidor da a cada usuario **su propia instancia de campo**, así que lo más
  probable es que **no veas** al segundo Kliff aunque todo vaya bien. Lo decide la línea
  `Session2 probe (...): host character ... field 0x... | user 2 character ... field 0x...`:
  - si los dos `field 0x...` son distintos, se confirma el análisis y la siguiente pieza es E3b
    (bloque C);
  - si **sí** ves un segundo Kliff, mejor de lo esperado: dime dónde y cómo se ve.
- `server FieldTime` antes y después: no debería cambiar de golpe.
- Puede salir un aviso de "no se pudo guardar": el autoguardado incluye al usuario 2 y el guardián
  lo rechaza. Es lo esperado (y la razón de RPT-0).
- En el log: `user 2 character 0xA010xxxx`.

### B4 (T06, G2): el invitado de verdad (Shadow) hace login en tu servidor (P8)

Solo si B2 y A3 salieron bien. Es la primera con dos PC.

- **Tu PC (anfitrión), `cdmp.ini`:** `[net] transport=steam role=host` y `[session2] enabled=1
  create_session=1 accept_tunnel=1`. Partida de laboratorio (RPT-0).
- **Shadow (invitado), `cdmp.ini`:** `[net] transport=steam role=guest` (con tu SteamID64 en
  `[steam] peer_steamid`) y `[session2] enabled=1 guest=1 guest_mode=remote`. Sin `[coop]`.

1. Tú: entra en el mundo y espera a ver `Session2: Troc tunnel open` en tu log.
2. Shadow: **arranca el juego después**, con tu juego ya en el mundo, y **quédate en la pantalla
   de título**: no elijas partida.
3. Espera 30 s y cerrad los dos sin guardar.

En el log de Shadow:
- `Session2 guest (P8): hooked ... in time`;
- `Session2 guest: ... connection REMOTE (host's server) | host ready yes | connections remote 1`,
  `out 2 packets` (`ConnectCryptogram` y `LoginUser`) e `in N delivered`;
- si sale `connection local` con `host ready no`, Shadow arrancó antes de que tu sesión 2 estuviera
  lista: cierra Shadow y vuelve a arrancarlo.

En tu log, `user 2 logged in` en el sondeo, como en B2. En Shadow, la pantalla de título debe
seguir funcionando normal. **Esto no demuestra todavía la carga de zona ni el juego remoto.**

---

### C1 (T07, E3b): el usuario 2 entra en tu campo

Programado y revisado (D-42), listo para probar. Igual que B3 pero con
[ini/cdmp.e3b.ini](ini/cdmp.e3b.ini) (`share_field=1`) y el mismo guion
(`login_e3lc00_112627.troc --var SAVE_SLOT=108`).

- **Quieto en un sitio tranquilo** del mismo sitio donde cargas la 108; sin viajar, sin morir y sin
  menús. Si viajas mientras el usuario 2 está en tu campo, se queda en un campo sin dueño (riesgo C1).
- En el log: `Session2 E3b ON` al arrancar; durante el guion `E3b H1: ... given the host's field` y
  `E3b H2: user 2 is in field ...: users 2`. La sonda: los dos `field 0x...` **iguales**,
  `users 0x2`.
- **Lo que deberías ver:** un segundo Kliff cerca de donde estaba el personaje de la captura 112627
  (no donde estás tú). Dime dónde y cómo se ve. Puede salir con tu mismo equipo o raro: el
  `characterNo` es el mismo que el tuyo (riesgo C2).
- Si el usuario 2 viaja a otro nivel distinto del tuyo, H1 lo deja en su propio campo como en B3 y
  lo dice en el log (`own field as before (another level ...)`).
- **Para y mándame el log** si el juego se cuelga, te echa al título, o aparece
  `eErrNoHasntEnterFieldPermision` / un error de campo cerrado.
- Al terminar, cierra el juego del todo.

### C4a (T10, salida): el usuario 2 se despide y desaparece de tu mundo (D-49)

Misma configuración que C1g ([ini/cdmp.c1f.ini](ini/cdmp.c1f.ini)), ranura 108, quieto. Guion
[data/login_e3lc00bye_112627.troc](data/login_e3lc00bye_112627.troc): entra como en C1g, se queda 20 s
y se despide (`logout_session`).

Pasa si:
- en `cdmp_peer`: `script done` con 2003 y 2002 recibidos;
- en el log: `user 2's logout asked for (goodbye from user 2)`, `2001 handed to the server`, y en la
  sonda `user 2 gone`, con tu campo de vuelta a `users 0x1`, el mismo campo (dirección y generación)
  y abierto;
- **el segundo Kliff desaparece de tu pantalla** y tu juego sigue normal (moverte, luchar) 60 s más.

Después, **C4b** (sin despedida): el guion de C1g y corto la conexión del segundo jugador; a los 15 s tu
servidor genera la salida. Mismos criterios. La reconexión (volver a entrar en la misma partida) es
**C4r**, justo debajo.

### C4r (T10, reconexión): el usuario 2 sale y vuelve a entrar en la misma partida

Ini [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini) (= `cdmp.c1f.ini` + `reconnect=1`), ranura 108, quieto en un
sitio tranquilo **y sin viajar** durante toda la prueba: antes de cada entrada se toma la base de tu
campo (dirección, dueño, generación, abierto, `users`, `userActors`) y tras cada salida tiene que
volver a ser la misma; si viajas, la barrera falla y la opción D se pone en cuarentena (es lo
previsto, no un cierre). **Un solo arranque del juego** y cuatro `cdmp_peer`, siempre uno detrás de
otro, nunca dos a la vez.

1. Carga la 108 y entra en el mundo. En el log: `Session2 C4: reconnect on`.
2. Ronda 1 (con despedida), desde el worktree:
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00bye_112627.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\c4r-1.bin
   ```
   Espera a que `cdmp_peer` termine solo (tras `script done` espera 10 s más) y a ver en el log
   `Session2 C4: reconnect epoch=2 ready`.
3. Ronda 2: el mismo comando con `c4r-2.bin`. Ahora el log dice primero
   `epoch=2 begins on a new guest connection`. Espera a `reconnect epoch=3 ready`.
4. Ronda 3 (sin despedida: el enlace se pierde al cerrarse `cdmp_peer`):
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_112627.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\c4r-3.bin
   ```
   A los 15 s de cerrarse, tu servidor genera la salida. Espera a `reconnect epoch=4 ready`.
5. Ronda 4: otra vez el guion con despedida (`c4r-4.bin`). Espera a `reconnect epoch=5 ready`; luego
   muévete y lucha 60 s y cierra el juego.

Si lanzas un `cdmp_peer` antes del `ready`, no entra (sus paquetes se tiran:
`guest packets of another connection dropped`): ciérralo y vuelve a lanzarlo tras el `ready`.

Pasa si, en **cada** ronda E (1 a 4):
- `cdmp_peer` llega a `script done` (en las rondas con despedida, con 2003 y 2002 recibidos);
- en el log, en este orden: `user 2 of epoch E exists; host field baseline ...` (la base de tu campo,
  no `NOT TAKEN`), la sonda `user 2 logged in` con `users 0x2`, `logout begin epoch=E ... trigger=bye`
  (rondas 1, 2 y 4) o `trigger=link_lost` (ronda 3), `2001 handed to the server`, las líneas
  `barrier epoch=E check a session / b sam / c field / d queues` hasta que las cuatro digan `pass`,
  `logout complete epoch=E` y `reconnect epoch=E+1 ready`;
- `check b sam pass` (apunta cuántas claves comprueba: usuario, personaje e hijos) y
  `check c field pass` con el mismo campo, dueño, generación, `open 1` y los `users`/`userActors` de
  la base (con tú solo en el mundo fue `users 0x1 userActors 0x2` en C4b: no esperes `0x1`);
- el segundo Kliff aparece y desaparece en cada ronda, y tu juego sigue normal;
- en la línea periódica `Session2: ...`: `C4 guest epoch 4 READY` al final, sin `QUARANTINED` y con
  `old-epoch buffers routed ... 0 untagged 0` si aparece esa parte;
- ningún `ERR` salvo `the Troc tunnel stops for epoch 3: the guest has been gone for 15 s` (ronda 3).

**Para y mándame el log y las grabaciones** si:
- sale `Session2 C4: QUARANTINED: ...` (la línea dice qué comprobación falló y con qué valores; la
  siguiente, `barrier at quarantine`, las cuatro). El juego debe seguir; no lances más `cdmp_peer`;
- una ronda posterior a la primera no llega a `script done` (apunta si sale `Server sent Nak 1013`);
- tu campo cambia de dirección o generación, tu Kliff deja de responder, te echa al título o el juego
  se cierra.

### D-53r (T07, compañeros): el hijo del usuario 2 recibe su primera acción y se libera (D-53)

D-53 retiene en el servidor al hijo del usuario 2 (su mascota hatch, tipo 6) mientras lleva el byte de
restauración a 0: solo lo recibe su dueño. Hasta ahora `cdmp_peer` nunca le daba acción, así que
nunca se liberaba. El guion nuevo
[data/login_e3lc00bye_child_112627.troc](data/login_e3lc00bye_child_112627.troc) es el de C4a más una
cosa: tras la carga compartida (`MoveFieldCompleteAck` del ciclo 2) aprende la clave del hijo del
primer `ActorLoadDataAck` que trae un registro suyo, y 350 ms después manda su primera
`StartActionReq` 2223. Es la plantilla del perro de 112627 (#3449), con la clave del hijo, la del Kliff
del usuario 2 en `+0x16C`, el hash de la capa 0 que trae el propio registro y la posición del último
`MoveFieldAck`. Se queda 20 s y se despide.

Ini [ini/cdmp.d53.ini](ini/cdmp.d53.ini) (`child_hold=1` y `companion_probe=1`; sin la sonda vale
[ini/cdmp.c4r.ini](ini/cdmp.c4r.ini), pero no verás las líneas `CompAction`). Ranura 108, quieto en un
sitio tranquilo y sin viajar. Un arranque del juego, un solo `cdmp_peer`:

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00bye_child_112627.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\d53r-1.bin
```

Pasa si:
- en `cdmp_peer`: `Script: CHILD_KEY = 0xB01…: child … type 6 … of owner 0x9010…` con
  `CHILD_HASH = 0x866C7489 (… restore 0)`, y al final `script done` con 2003 y 2002 recibidos;
- en el log, en este orden: `child hold: held 0xB01… (type 6, … restore 0)` y, menos de 1 s después,
  `child hold: released 0xB01… (type 6) to session … after … ms, restore 1 (after StartActionReq)`
  (o `(fallback)`), con la misma clave que aprendió el peer;
- con la sonda: el cliente del anfitrión crea el hijo y lo restaura con el byte a 1
  (`CompAction restore BEFORE actor 0xB01… … restore byte 1`, y después `restore AFTER`);
- ves aparecer la mascota del segundo Kliff y tu juego sigue normal (moverte, luchar) **60 s** desde
  la liberación: unos 25 s con el hijo a la vista, la despedida y lo que sigue;
- la despedida es limpia: `user 2's logout asked for`, `2001 handed to the server`, la sonda `user 2
  gone`, y ningún evento 1000 de Windows (ni al aparecer ni ~1 s tras la salida).

**Para y mándame el log y la grabación** si:
- el juego se cierra o hay un evento 1000 de Windows (apunta la hora);
- sale `child hold: CompHold timeout 0xB01…` y no llega ningún `released`: el servidor no aceptó la
  acción. Es un resultado **seguro** (el hijo sigue retenido, nunca sale con 0). Repite entonces una
  vez, con el otro hash candidato (la acción aceptada del hatch del anfitrión):
  `… --var CHILD_HASH=0x94D333D6 --record D:\cdmp-grabaciones\d53r-2.bin` (el valor de `--var` manda:
  la línea `Script:` dirá `CHILD_HASH = 0x94D333D6 (record's layer-0 hash 0x866C7489 …)`);
- `cdmp_peer` acaba con `CHILD_KEY not learned within 10000 ms` (no llegó ningún registro del hijo:
  sin acción ni despedida; tu servidor da la salida a los 15 s, como en C4b) o con
  `bad record length`;
- sale `released` con otra clave, o `restore byte 0` en la sonda para el hijo.

### C1g (T07, E3b + P5): con la primera acción en su sitio (D-48)

Causa de C1–C1f ([opcion-d-accion-usuario2.md](opcion-d-accion-usuario2.md)): el guion no mandaba la
primera acción del personaje del usuario 2 durante su **primera** carga, como hace un cliente real;
el servidor dejaba a 0 su byte de restauración y tu juego lo creaba sin acción. Arreglado en el guion
(paso 4b). Ini [ini/cdmp.c1f.ini](ini/cdmp.c1f.ini) (`share_field=1`, `relay_fix=1`), guion
`login_e3lc00_112627.troc` (ya incluye el paso 4b), ranura 108, quieto en un sitio tranquilo.

Pasa si:
- en el log: `HorseDiag ... RESTORE ... USER 2 ... restoreByte=1` y luego el Kliff con `cur=true`;
- los contadores de contención (`animation skipped`, `character update skipped`) se quedan en 0;
- tu juego aguanta **60 s** sin cerrarse con el segundo Kliff a la vista. Dime dónde está y si se mueve
  o respira (animación de reposo).

Las variantes `login_e3lc00act*` de C1c–C1f quedan obsoletas.

### C1b y C1c (T07, E3b): otra vez, con la guarda (D-44)

C1 (2026-10-09): H1/H2 metieron al usuario 2 en tu campo, pero el juego se cerró 2 s después al
animar a su Kliff sin acción. Ya está contenido (D-44). Mismo ini que C1
([ini/cdmp.e3b.ini](ini/cdmp.e3b.ini)), ranura 108, quieto en un sitio tranquilo.

1. **C1b** (guion de siempre). Lo esperado: el juego **no** se cierra; en el log, `animation skipped`
   sube (la guarda actuó) y quizá veas un Kliff quieto o en pose rara. Si aguanta 2 minutos, ciérralo
   del todo.
2. **C1c** (solo si C1b aguanta): guion `login_e3lc00act_112627.troc`, que además manda la primera
   acción del segundo Kliff. Lo esperado: un Kliff que se anima (de pie, en reposo). Dime dónde
   está y cómo se ve.

Para y mándame el log si se cuelga, se cierra o te echa al título. Cierra el juego del todo al
acabar cada una.

## Bloque C: necesitan trabajo nuevo antes

No se pueden hacer todavía. Cada una necesita diseño, código, revisión y banco de pruebas fuera
del juego. Están en el orden de la síntesis, con lo que decide cada una (detalle en
`vision-sintesis.md` §5):

| Prueba | Qué falta antes | Qué decide |
|---|---|---|
| **T07, E3b → E4** | Programar E3b (meter al usuario 2 en tu campo: `opcion-d-campo-compartido.md`). El espejo E4 ya existe. | Un mundo compartido básico: los dos en el mismo campo, se ven y se mueven. |
| **T08, identidad** | Catálogo SteamID → cuenta → personaje; números de personaje distintos. | Que cada jugador sea siempre el mismo y no se crucen equipo ni cachés. |
| **T09, G3/E5 + IA y escenas** | Sondas nuevas y transferencia del control de los NPC. | **El riesgo nº 1:** si el invitado puede cargar zonas y combatir lejos del anfitrión. |
| **T10, G5: salir y reconectar** | Cierre remoto controlado (hoy el logout nativo está bloqueado). | Entrar y salir 50 veces sin dejar restos. |
| **T11-T15, persistencia** | Partidas por jugador en el servidor (P9 al revés), separar mundo y jugador, ranuras aisladas, recuperación. | Que el mundo no retroceda y cada jugador recupere lo suyo. |
| **T16 resto, IN2-IN3: plantilla** | Plantilla de jugador nuevo a partir de A4. | Que un jugador nuevo empiece en la exploración libre sin duplicar premios ni mundo. |
| **T17, historia y jefes** | Política de contenido compartido. | Qué misiones, jefes y cinemáticas funcionan con varios jugadores. |
| **T19, permisos y contención** | Banco de rechazos: identidad ajena, ranuras 3/206, build distinta… | Que nadie pueda tocar lo de otro jugador. |
| **T20, escala 2 → 4** | Tabla de sesiones (de 1 a 3-4 extra) y banco sintético de 4-5 sesiones. | Juntos y dispersos, con latencia y pérdidas: claves, CPU, colas y subida (30 Mbit/s deberían bastar). |
| **T21, duración** | Instrumentación de memoria, colas y claves. | **H=2 durante 2 h y H=4 durante 24 h** sin crecimiento sostenido ni corrupción. |

T18 (personalización semanal) queda **retirada** (D-36): no hay cambio de aspecto una vez por
semana ni reloj semanal del servidor.

---

## Cuándo parar en cualquier prueba

Para, cierra sin guardar y mándame los logs si pasa cualquiera de estas cosas:
- el juego se cuelga o se cierra;
- te echa al título;
- aparece `DIFFERENT`, `CREATION FAILED`, `BROKEN`, `TUNNEL STOPPED` o `TOO LATE`;
- el guardián rechaza algo **fuera** de una prueba del bloque B;
- ves datos de una partida que no es la que cargaste.
