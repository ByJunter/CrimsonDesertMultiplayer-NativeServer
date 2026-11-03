# P8: preparación de la primera sesión real (Shadow → servidor del anfitrión)

Auditoría de solo lectura, 2026-10-09, rama `opcion-d`. No se ha ejecutado el juego ni cambiado código.
**Actualización (mismo día, sin abrir el juego):** G1 queda resuelto **en el anfitrión** con `child_hold`
generalizado a los hijos de cualquier usuario, y G2/G3 están programados (solo registro). Ver §3.
Convención: **[E]** comprobado en código o desensamblado; **[V]** visto en logs o grabaciones; **[S]**
inferencia.

Fuentes: `src/mod/session2/guest.{h,cpp}`, `src/mod/mod.cpp` (`run_network`, arranque),
`src/common/troc_{frame,link}.*`, `src/mod/config.*`, `src/mod/session2/session2.cpp`
(`tunnel_tick_reconnect`, `inject_or_kill`), `server.cpp`, `guest_lifecycle.h`, `child_hold.h`,
`clock_gate.h`, `pendientes/salida.md` y sus evidencias, `pendientes/companeros.md` (Revisión),
`pendientes/reloj.md`, `opcion-d-p8.md`, `opcion-d-pruebas.md`, el diario (resultados del 09-10) y
`docs/research/ini/`.

---

## 1. Qué hace hoy el mod del invitado y qué exige lo nuevo del anfitrión

### 1.1 `guest.cpp` hoy [E]

- **Instalación.** Con `[session2] enabled=1 guest=1` arranca un hilo propio en cuanto existe
  `ClientNet`. Antes de instalar comprueba la build (PE `0x6AB28F00`) y los prólogos de `Send`
  `0x14380B0`, de la bomba `0x35BAD4`, de `OnRecvPacket` y del push de Q3. Si el cliente ya conectó
  (`ClientNet+0x58`), escribe `TOO LATE` y no instala nada. En ese caso, en el PC del invitado
  **no se instala nada del anfitrión** (`session2 = false` en `mod.cpp`): ni guardián de guardado,
  ni reloj, ni contenciones D-44/D-52. Las herramientas dev que inyectan quedan apagadas (`p8_guest`).
- **Decisión por conexión.** Se decide con la primera trama de cada época del cliente
  (`[session+0xB0]`). Es REMOTA solo si se cumplen todas estas condiciones:
  - `guest_mode=remote`;
  - todavía no hubo ninguna conexión remota en este arranque (`g_had_remote`);
  - la primera trama es la 2319;
  - el túnel está arriba;
  - llegó un "ready" del anfitrión hace menos de 3 s, sin "dead".

  Si falta cualquiera, la conexión es local: partida normal de un jugador.
- **Salida.** Cada trama C→S se valida con `parse_wire(ToHostServer)`, que acepta los paquetes
  normales y la 2001 de 3 bytes. Después va a la cola del túnel y el búfer vuelve a Q3, con
  `*res = 0`. La **2790 se consume en local** (`kept_local`): no va al anfitrión y tampoco al
  servidor local. Cualquier fallo marca `end_remote` y la conexión queda BROKEN.
- **Entrada.** `guest_deliver` separa los paquetes de control del túnel (opcode 1, `ready`=1 y
  `dead`=2) y encola el resto. La bomba los entrega con `ClientNet::OnRecvPacket`, pero solo a la
  época vigente.
- **Reconexión.** Si la época del cliente cambia mientras está en remoto (por ejemplo, al volver al
  título), la conexión se corta con `end_remote`, el modo pasa a local y la siguiente conexión
  **siempre** es local (`g_had_remote`). Hay que reiniciar el juego.
- **Lazo de red (`mod.cpp`).** El `TrocLink` del invitado se crea cuando los hooks están puestos. Si
  se rompe y no hay conexión remota en curso, se resetea. `guest_set_tunnel_up` se llama en cada
  vuelta.

### 1.2 Cambio por cambio: ¿hace falta tocar el invitado?

| Tema | Veredicto | Detalle |
|---|---|---|
| **2790 / 2038** (salida.md §4.1) | **Hecho** [E] | `guest_send` cuenta la 2790 como `kept_local` y no la envía. La 2038 nunca sale del cliente. El anfitrión también las rechaza (`never_from_guest`). El contador `kept_local` ya aparece en `guest_describe()` (G2, hecho). Riesgo menor [S]: el servidor local del invitado nunca recibe su 2790, así que salir al escritorio podría esperar más (`0xACD400` espera de forma acotada). Si se cuelga, se mata el proceso. |
| **2001 de 3 bytes** | **Hecho** [E] | `frame_packet` usa `parse_wire`, que admite exactamente `D1 07 FF`. El anfitrión la trata como despedida (C4a, C4r). |
| **2002/2003 de 7 bytes** | **Hecho** [E] | `guest_deliver` usa `parse_wire(ToGuestClient)`. Con `reconnect=1`, el anfitrión las reenvía en RETIRING sobre la misma conexión. |
| **Índice u32 de la 2002** | **Lo da bien el anfitrión** [E-x] | Ver §1.3. |
| **Reconexión** (`g_had_remote`) | **Sin cambio para la primera sesión** | El anfitrión, con `reconnect=1`, solo abre una época nueva sobre una conexión de transporte **más nueva** que la del READY (`make_ready(link.session_epoch())`, `may_start`). Aunque el invitado aceptara un segundo remoto en el mismo proceso, el anfitrión no mandaría "ready" por el mismo enlace. Con la política actual (reiniciar el juego del invitado) las dos partes encajan. Trampa [E]: si el invitado vuelve a arrancar **antes** de que el anfitrión registre `reconnect epoch=N ready`, su conexión nueva queda por debajo de `ready_link_` y no se acepta nunca. Hay que esperar al `ready` antes de relanzar. |
| **Bytes `ready`/`dead`** | **Funciona** [E] | El anfitrión envía cada segundo `ready` (ACTIVE, conexión propia y `server_accepts_guest`) o `dead` (solo en QUARANTINED); en RETIRING, CLEAN y READY no envía nada. El invitado exige que el último `ready` tenga menos de 3 s. No hay aviso de muerte por corte, porque el anfitrión no puede enviarlo, ni cuando el anfitrión inicia por su cuenta la salida del usuario 2 (fallo de identidad). En ese caso llegan 2679/2003/2002 nativos y el cliente vuelve al título [S]. |
| **`characterNo` F002** (D-54) | **Sin cambio** [E/V] | `identity_fix` normaliza el lobby **antes** del 2869 y el `Status` del personaje. El cliente real lo aprende del 2869 y lo devuelve en su 3093, como hizo `cdmp_peer` en C2 (aceptado). Riesgo nuevo [S]: en el cliente del invitado, el Kliff del **anfitrión** llega con `7FFF…FFFF`, la constante de un jugador, y el suyo con F002. Si algún código del cliente toma esa constante por "yo", habría confusión (cámara, interfaz, propiedad). Hay que vigilarlo en P8-1 y P8-2. Tampoco hay que cambiar de personaje: `identity_fix` solo normaliza el del login. |
| **`LoadingCompleteReq 01`** (reloj, D-50) | **Sin cambio en el invitado** [E] | `clock_gate` llama al original con flag 0: la carga termina (`0x219D941`) y el Ack 2762 sale. Consecuencias: (a) el ancla `TS+0x468` del personaje del invitado no se renueva; (b) un campo **privado** del invitado (el aparcamiento, o su propio campo si viaja a otro nivel) no se despausa, y `reloj.md` §5.1 preveía despausarlo aparte, cosa que **no** está programada. El aparcamiento tiene hora fija y dura poco, así que no se nota. En el campo compartido no cambia nada, que es lo buscado. **Regla operativa:** el invitado no viaja a otro nivel. |
| **2042 `ScopeSenderAck` y 2005 mixtos** (companeros.md, Revisión) | **Sin cambio obligatorio; añadir observación** | En el **anfitrión**, D-53 retiene al hijo en `NotifyAppear`, antes de serializar, así que lo que se aplaza en el cliente queda fuera de juego para los hijos de cualquier usuario
(desde G1, también los del anfitrión hacia el invitado) [E]. Queda sin demostrar si algún camino de 2042 publica actores sin pasar por `NotifyAppear`: en las capturas hubo cero 2042 [V]. En el **invitado**, los 2005 mixtos (su Kliff, su hijo y los actores del anfitrión, como C1g #944) son el caso nativo de un jugador para lo propio. P8 dice "no hacer nada" con 2042 (§5.4); el invitado ya los cuenta (G3, hecho). |
| **Primera acción del hijo** (D-53) | **Sin cambio** [E] | La IA del cliente real manda la 2223 del hijo por el túnel. `server_inject` la pasa a Q0 y la libera en el hilo del manejador `StartActionReq` (comprobación TLS), con un respaldo estricto desde `OnRecvPacket` si la primera acción llega por otro opcode. La mediana de los hijos propios en 112627 es de 0,3–0,4 s. Si la mascota nunca actúa, sigue retenida: el resultado es seguro, aunque no se ve. |
| **Hijos NUEVOS del anfitrión hacia el invitado** | **Resuelto en el anfitrión (G1)** [E; falta verlo en vivo] | Antes, `child_hold` solo retenía a los hijos cuyo dueño no era el anfitrión, y un caballo o una mascota que el anfitrión invocara durante la sesión llegaba al cliente del invitado con restauración 0 [S, por simetría con C2-0 y COMP], el estado que tumbó a seis lectores nativos (el invitado **no tiene** D-44/D-52). Ahora `child_hold` retiene el hijo de **cualquier** usuario validado (anfitrión incluido) frente a toda sesión que no sea la de su dueño, y se lo da a la sesión 2 en cuanto el cliente del anfitrión le da su primera acción. |

### 1.3 El u32 de la 2002: de dónde lo toma `0x1051F190` [E-x]

En `salida_evidencias/capstone_1051f190.txt`:

```
01051F1E0  mov r14d, dword ptr [rip-0x9AB9C83]   ; 0x1051F1E7-0x9AB9C83 = exe+0x6A65564
01051F236  mov ebp, 0x7D2                         ; opcode 2002
01051F243  mov dword ptr [rsp+0x8B], r14d         ; u32 en +3 del paquete (opcode en +0x88, FF en +0x8A)
01051F2C8  call 0x2773630                          ; envío de 7 B a la sesión guardada de user+0x1F8
```

- `exe+0x6A65564` es `g_ServerFrameworkMode` (`known_names_repo.csv`; `opcion-d-d0.md` §1.1:
  "modo 0x6A65564 = 4"). Es un **global del proceso, no de la sesión**. Por eso la captura 105149
  trae `D2 07 FF 04 00 00 00`.
- En el cliente, `0xA79CA40` usa ese u32 como `idx` de `ClientNet::PostPacket` para publicar el 1003
  local (desconexión). La conexión del cliente es el slot **4**: el 1001 sale con
  `PostPacket(ClientNet, …, 4, …)` (`0x8C8689`, `opcion-d-p8.md` §2.2).
- **Conclusión:** el servidor del anfitrión escribe su propio modo, 4, y eso coincide con el slot 4
  del cliente del invitado (misma build y mismo modo). **El anfitrión ya da un índice correcto;** no
  hay que inventar ni reescribir nada.
- Que el modo valga lo mismo que el slot es una coincidencia de esta build [S]. Por eso la
  aceptación debe comprobar en P8-4 que el invitado recibe `D2 07 FF 04 00 00 00` y que su propio
  `[exe+0x6A65564]` vale 4 (G2).

---

## 2. Lista para la primera sesión con Shadow

### 2.0 Antes del día (una vez)

1. **Kit nuevo para Shadow.** `dist/cdmp-a3-shadow` es anterior a `bd85171` y `a80e0c2` (mensajes
   internos de 3 y 7 bytes, 2790): con ese kit **no vale**. En el worktree, ejecuta
   `.\scripts\build.ps1` y empaqueta `cdmp.asi` y `winmm.dll` de la misma build en los dos PC. Al
   arrancar, las dos líneas `=== Crimson Desert Multiplayer … (build XXXXXXXX, protocol 8)` deben
   coincidir.
2. **Build del juego 1.0.0.2976 en los dos.** Desactiva las actualizaciones automáticas de Steam en
   Shadow. Si la build no cuadra, el log dice `unsupported game build; P8 off`.
3. **Copias de seguridad.** `%LOCALAPPDATA%\Pearl Abyss\CD\save` en los dos PC. En el anfitrión,
   `crashpad_handler.exe` sigue apartado (K2), como en las pruebas de hoy.
   - **Shadow también tiene que apartar `crashpad_handler.exe` (K2).** En la sesión P8 del
     2026-10-09 el Sentry/crashpad del invitado envió un informe de fallo con partidas y una captura.
     Desde esta build el invitado instala además la guarda de informes K1 (`0x1317D40`) al arrancar,
     en cualquier modo (`observe` o `remote`): en su log debe salir
     `Session2 guest report guard ON (K1)` y la línea por minuto `report guard (K1) on`. K1 no cubre
     los volcados de crashpad (un fallo real del proceso): eso solo lo evita K2.
4. **Ranura 108 en Shadow.** El número que el invitado elige en su título se carga **del disco del
   anfitrión** (`opcion-d-p8.md` §2.3; `0x186F570`). El título de Shadow lista su propio disco, así
   que copia `save\<id>\slot108\` del anfitrión (plantilla del capítulo 2) a
   `save\<id-de-Shadow>\slot108\`. Una partida de otra cuenta sirve (A4).
   - **Nunca** elijas la ranura 3: el anfitrión la rechaza y corta el túnel.
   - Tampoco elijas las ranuras 0-2: son autoguardados del anfitrión.
5. **SteamID64 de cada uno.** El de Shadow va en el ini del anfitrión y el del anfitrión en el de
   Shadow. Los dos deben ser amigos en Steam.

### 2.1 Ini del anfitrión (`bin64\cdmp.ini`)

Partir de [ini/cdmp.c4r.ini](ini/cdmp.c4r.ini) y cambiar `[net]`:

```ini
[general]
player_name=Host
[net]
enabled=1
transport=steam
role=host
[steam]
peer_steamid=<SteamID64 de Shadow>
channel=67
[overlay]
enabled=1
self_marker=0
[sniffer]
enabled=1
record=1            ; grabacion de la sesion (50-100 MB)
[coop]
enabled=0
[session2]
enabled=1
probe=1
save_guard=1
create_session=1
accept_tunnel=1
share_field=1
relay_fix=1
reconnect=1         ; imprescindible: sin el, un reinicio de Shadow mata el tunel hasta reiniciar
clock_gate=1
identity_fix=1
child_hold=1
companion_probe=1   ; solo lectura: cuenta 2042 y CompCreate (paso en D-53r con ella)
```

**Variante UDP** (alternativa si Steam se queda corto, ver R1): `transport=udp`, `role=host`,
`peer_port=37015` y `host_loopback_only=0`, con el router redirigiendo UDP 37015 al PC del
anfitrión.

### 2.2 Ini de Shadow (`bin64\cdmp.ini`)

```ini
[general]
player_name=Shadow
[net]
enabled=1
transport=steam
role=guest
[steam]
peer_steamid=<SteamID64 del anfitrion>
channel=67
[overlay]
enabled=1
self_marker=0
[sniffer]
enabled=1
record=1
[coop]
enabled=0
[session2]
enabled=1
guest=1
guest_mode=remote
```

- **No** pongas `create_session`, `accept_tunnel`, `relay_fix`, `mirror_host` ni `share_field`: con
  cualquiera de ellas el mod no arranca la opción D.
- **Variante UDP:** `transport=udp`, `role=guest`, `peer_host=<IP pública del anfitrión>` y
  `peer_port=37015`.

### 2.3 Registros que hay que recoger

- `bin64\cdmp.log` de cada PC **después de cada arranque del juego**. El fichero se sobrescribe al
  arrancar (`CREATE_ALWAYS`): renómbralo (por ejemplo, `shadow_run1.log`) antes de volver a abrir el
  juego.
- `bin64\cdmp_packets_*.bin` y `.log` de los dos PC.
- Visor de eventos de Windows (Aplicación, evento 1000) si algo se cierra.
- Capturas de pantalla de los dos lados.
- **Ctrl+F12 a la vez en los dos PC** en cada hito (`=== MARK N ===`), para alinear los logs. Apunta
  qué pasaba en cada marca.

### 2.4 Orden y criterios

**Paso 0 (anfitrión).**
1. Abre el juego, carga la ranura 108 y quédate quieto en un sitio tranquilo junto al punto de
   carga. Allí aparecerá el Kliff del invitado, que usa la misma partida.
2. En el log deben salir `Session2 C4: reconnect on`, `Session2 E3b ON` y
   `Session2: Troc tunnel open…`.
3. A partir de aquí no viajes, no abras el menú (pausa) y no invoques caballo ni mascota. La
   retención de G1 cubre al caballo o la mascota del anfitrión, pero hasta verla en vivo con `cdmp_peer`
   (ver §3, G1) la regla se mantiene en la primera sesión.

**P8-1: login y verse.**
1. Shadow arranca el juego **después** del paso 0 y espera en el título.
2. El anfitrión confirma en su log `Network: player … is joining` y, en la línea `Session2 guest…`
   o el sondeo, `user 2 logged in`.
3. Entonces Shadow carga la **ranura 108**.

- **Pasa si:**
  - en Shadow: `hooked … REMOTE`, sin `TOO LATE`; `connection REMOTE (host's server) | host ready
    yes | connections remote 1`; `in N delivered, 0 errors`; nada de BROKEN;
  - en el anfitrión: `E3b H2 … users 2`, `C2 identity: … invariant ok … F002`,
    `clock gate … flag 1 -> 0` (primera vez con un 01 real) y el reloj del mundo sin
    publicaciones;
  - cada uno ve al otro Kliff animado.
- **Para si:**
  - Shadow dice `connection local` o `host ready no`: cierra Shadow y vuelve a arrancarlo (R2);
  - la carga de Shadow dura más de 3 minutos;
  - sale `Server sent Nak`, `QUARANTINED`, `TUNNEL STOPPED` o BROKEN;
  - cualquiera de los dos juegos se cierra o se cuelga.

**P8-2: moverse y pelear los dos, 2 minutos.**
1. Caminad cerca el uno del otro y atacad al aire o a un enemigo débil.
2. Ni viajes, ni muertes, ni dormir, ni guardar, ni menú de pausa, ni cambio de personaje.

- **Pasa si:**
  - no hay cierres;
  - cada uno ve al otro moverse con fluidez;
  - en la línea de 5 s del anfitrión, `Troc tunnel … backlog` se mantiene bajo (menos de 1 MB),
    `reliable refused 0` y el `rtt` es estable;
  - P5 dice `relayed … 0 errors`;
  - no hay `Steam: SendMessageToUser returned EResult`.
- **Para si:**
  - el backlog crece sin bajar, pasa de 8 MB o la rtt supera 1 s de forma sostenida (R1);
  - el Kliff de alguno se congela más de 10 s;
  - hay cierres o expulsiones al título.

**P8-3: la mascota del invitado aparece en el anfitrión.**
1. Shadow deja que su mascota le siga. Si no está fuera, la llama.

- **Pasa si:** en el anfitrión salen `child hold: held 0xB01…` y luego
  `released … restore 1 (after StartActionReq)` en menos de 2 s; después, `CompCreate` con byte 1, y
  la mascota se ve moverse.
- **Fallo no grave:** `CompHold timeout` sin `released` durante más de 30 s. Es seguro: apunta el
  opcode de su primera acción en la grabación de Shadow.
- **Para si:** hay un cierre en cualquiera de los dos PC.

**P8-4: el invitado sale por el menú.**
1. Shadow elige "Volver al título".

- **Pasa si:**
  - en el anfitrión salen `logout begin epoch=1 … trigger=bye`, `2001 handed to the server`, las
    cuatro `barrier … pass`, `logout complete epoch=1` y `reconnect epoch=2 ready`; el segundo Kliff
    y su mascota desaparecen, y el anfitrión juega 60 s normal;
  - Shadow llega al título en menos de 30 s. En su log saldrá el `ERR` esperado
    `the client reconnected; restart the game…` y `connections remote 1 local 1`.
- **Para si:**
  - el anfitrión entra en `QUARANTINED` o se cierra;
  - Shadow se queda colgado más de 60 s. En ese caso, mata el proceso: el anfitrión ya tiene la
    2001.
- **Después:** Shadow cierra el juego desde el título (ahora es local) **sin cargar ninguna
  partida**, y renombra su `cdmp.log`.

**P8-5: el invitado vuelve a entrar.**
1. Solo cuando el anfitrión tenga `reconnect epoch=2 ready`, Shadow arranca el juego otra vez y
   repite P8-1.

- **Pasa si:** en el anfitrión sale `epoch=2 begins on a new guest connection` y, después, los mismos
  criterios de P8-1, con F002 de nuevo.
- **Para si:** se cumple cualquier criterio de parada de P8-1. Si Shadow arrancó antes del `ready`,
  queda en local para siempre (§1.2): cierra y vuelve a abrir.

**P8-6: pérdida del enlace.**
1. Con el invitado en el mundo, termina en Shadow `CrimsonDesert.exe` desde el Administrador de
   tareas. Así no se envía la 2001.

- **Pasa si:**
  - en el anfitrión salen, en unos 20 s, `Network: … left`,
    `the Troc tunnel stops for epoch 2: the guest has been gone for 15 s`, `trigger=link_lost`, la
    barrera en `pass` y `reconnect epoch=3 ready`;
  - el anfitrión juega 60 s normal.
- **Al terminar:** cierra el anfitrión sin guardar, comprueba que la ranura 108 no se escribió y
  recoge todo.

### 2.5 Riesgos principales y plan B

| # | Riesgo | Señal | Plan B |
|---|---|---|---|
| R1 | **Ancho de banda o atasco.** La carga del usuario 2 son unos 12 MB en unos 17 s (grabaciones `c4r_*.bin`) y la estancia, unos 250 KB/s (`c4r_soak.bin`: 75 MB en 5 min) [V]. Steam envía por defecto a unos 256 KB/s (`steam-transporte.md`). La ventana fiable es de 128×1100 B; el tope del backlog del túnel, 16 MB; el *timeout* de sesión, 5 s [E]. Un desbordamiento rompe el enlace y provoca la salida del usuario 2. | backlog creciente, `reliable refused`, EResult de Steam, rtt alta | Repetir con UDP directo y puerto redirigido. Si sigue igual, medir el caudal en P8-1 y no pasar a P8-2. |
| R2 | El `ready` llega tarde o caducado al salir la 2319 | `connection local`, `host ready no` | Reiniciar Shadow con el anfitrión ya en el mundo (como en B4). |
| R3 | **Un hijo nuevo del anfitrión llega a Shadow sin acción** y Shadow no tiene contención | Cierre de Shadow al aparecer un caballo o una mascota del anfitrión; en el anfitrión, un hijo del anfitrión **sin** `child hold: held … owner host` | G1 hecho en el anfitrión (retención generalizada). Mientras no se vea en vivo: el anfitrión no invoca nada. |
| R4 | El cliente de Shadow se queda colgado al salir (2002 o 1003) | Sin título en 60 s | Matar el proceso. El anfitrión ya tiene la 2001 o la salida por corte. |
| R5 | Shadow vuelve a arrancar antes del `ready` | Sin `begins on a new guest connection` | Cerrar, esperar el `ready` y volver a abrir. |
| R6 | Reloj o pausa: un campo privado del invitado sigue en pausa; la pausa del anfitrión congela al invitado (P8 §5.3) | Cielo congelado en Shadow; Shadow quieto | Nadie viaja; el anfitrión no pausa. |
| R7 | Guardar o dormir: el anfitrión descarta las Req de guardado y la 2125; la interfaz de Shadow puede quedarse esperando | "Guardando…" eterno | No guardar ni dormir. Más adelante, P9. |
| R8 | La misma partida duplica objetos (`itemNo`); el cliente puede confundir el `characterNo` `7FFF…` con "yo" [S] | Inventario o cámara raros | No intercambiar ni soltar objetos; apuntarlo y seguir si no hay cierre. |
| R9 | Después de P8-4, Shadow juega **en local y sin guardián**: podría guardar en su disco | — | No cargar ninguna partida tras volver al título; hay copia de seguridad. |
| R10 | Hooks tarde (`TOO LATE`) o prólogo distinto si el *sniffer* se adelanta | `TOO LATE` o `not the expected ones` | Reiniciar Shadow. Si se repite, `[sniffer] enabled=0` en Shadow. |
| R11 | 2042 o 2005 mixtos con un cliente real (nunca vistos) | `companion_probe` del anfitrión: entradas 2042 | Si hay cierre en el anfitrión, D-52 sigue como red. Guardar la grabación. |

---

## 3. Cambios recomendados en el invitado antes de la sesión (sin implementar)

**G0. Obligatorio (empaquetado, sin código).** Kit nuevo de Shadow con la build actual (§2.0.1).

**G1. Hecho en el anfitrión: `child_hold` generalizado (la alternativa del anfitrión).**
- **Qué hace ahora** (`src/mod/session2/child_hold.{h,cpp}`): un hijo (clave de clase `0xB`) cuyo dueño
  `[actor+0xA0]` es un `ServerUserActor` validado (el **puntero exacto** del usuario del anfitrión, el del
  usuario 2 o, a prueba de fallos, otro usuario con la vtable de `ServerUserActor`) y cuyo byte de
  restauración es 0 se publica **solo** a la sesión de su dueño. Las demás sesiones receptoras quedan
  anotadas y reciben el re-anuncio nativo (`NotifyAppear([obs],[hijo])`) cuando el byte vale 1, por la
  misma liberación de D-53r (hilo del manejador `StartActionReq` con la comprobación TLS `+0x1EC`, y el
  respaldo estricto).
- **Garantías:** el dueño recibe siempre su hijo en la misma llamada (el anfitrión nunca pierde los
  suyos); los NPC (sin dueño) no se tocan; si el puntero del anfitrión no se puede leer, o la clave del
  anfitrión aparece en otro puntero, el hijo queda nativo; cualquier lectura fallida llama al original;
  los 2005 mixtos conservan el orden; la tabla sigue acotada a 64.
- **Vaciado:** cuando el usuario 2 se va o entrega su 2001, se borran las entradas cuyo **dueño** es el
  usuario 2 y las cuyo **observador** es el usuario 2. Un hijo del anfitrión observado solo por el
  anfitrión nunca se anota, así que con dos usuarios la tabla queda vacía.
- **Registro:** `child hold: held 0xB… (type N, owner host|user 2 0x…, restore 0) from session 0x… (user 2|host
  0x…)`, `released … (type N, owner host) to session … (user 2 …) after N ms, restore 1 (after
  StartActionReq)`; `describe()` separa `held N (host's A, user 2's B)`.
- **Falta verlo en vivo (un solo PC, `cdmp_peer` como usuario 2):** con el usuario 2 dentro, el anfitrión
  invoca su caballo o su mascota. Debe salir `held 0xB… (… owner host …) from session … (user 2 …)` y,
  en menos de un segundo, `released … owner host … restore 1 (after StartActionReq)`; el anfitrión ve su
  caballo normal (su sesión lo recibe al momento) y no hay `CompHold timeout`. Después, en P8 con Shadow.
- **Lo que sigue sin contención en el invitado:**
  - Los actores que lleguen al invitado **sin pasar por `NotifyAppear`** (un posible camino de 2042: cero
    vistos hasta hoy). G3 los cuenta en el invitado.
  - Un hijo del anfitrión cuyo byte de restauración nunca llegue a 1 (la acción rechazada en el
    servidor): no se verá en Shadow, pero sin cierre (`CompHold timeout`).
  - Cualquier otro estado de carga parcial que no sea "hijo sin acción" (personajes, monturas en otros
    estados): el cliente de Shadow sigue sin D-44/D-52. Instalar esas guardas en el invitado (`mod.cpp`,
    rama `s2_guest`, con el predicado "dueño ≠ yo" en `remote_puppet.cpp`) queda como capa defensiva
    opcional, ya no como requisito de P8.

**G2. Hecho: diagnósticos en `guest.cpp` (solo registro, sin cambio de comportamiento).**
- `decide()`: una línea por decisión (tope 32): `connection (epoch 0x…) -> REMOTE|local: <motivo> | first
  packet N | tunnel up|down | last host ready N ms ago|never | host dead | had remote | guest_mode`. Los
  motivos siguen el orden de `host_ready()`: `guest_mode=observe`, ya hubo un remoto, primera trama no
  2319, túnel caído, `dead`, sin `ready`, `ready` caducado, Q3 ilegible.
- `guest_describe()`: `kept local (2790) N | logout: out 2001 N, in 2002 N 2003 N (last 2002 index X vs own
  mode Y: equal|DIFFERENT)`.
- Bytes registrados (tope 16): la 2001 que sale y las 2002/2003 que entran. Para la 2002 se compara su u32
  con el `[exe+0x6A65564]` propio del invitado; si difieren, solo se registra (`DIFFERENT (logged only, not
  rewritten)`). Criterio de P8-4: `D2 07 FF 04 00 00 00` y `equal`.

**G3. Hecho: contadores de lo que entra.**
- `guest_describe()`: `delivered 2042 N 2005 N 2869 N 2679 N` (lo entregado a `OnRecvPacket`); la primera
  2042 entregada deja una línea.
- El 3093 que sale del invitado: `out LoginCharacterToFieldServerReq 3093: characterNo 0x… (F002…), save
  slot N` (tope 8) y, en `guest_describe()`, `out 3093 N (characterNo 0x…, slot N)`. Con el anfitrión de
  hoy debe ser `0x7FFFFFFFFFFFF002`; `7FFF…FFFF` significaría que el cliente no aprendió el F002 del 2869.

**G4. Media: aviso visible al invitado.**
- **Qué:** cuando ocurre `end_remote`, o cuando la decisión es local con `remote_allowed`, mostrar un
  mensaje en el HUD del mod: "conexión con el anfitrión perdida o no lista: cierra y vuelve a abrir
  el juego".
- **Por qué:** el cliente se queda colgado sin *timeout* (P8 §5.7).

**G5. Después de la primera sesión.**
- **Reconexión sin reiniciar:** quitar `g_had_remote` solo junto con un mensaje de control "nueva
  conversación, época X" (P8 §2.1), o forzando que el invitado reconecte su `ClientSession`. El
  anfitrión exige un enlace más nuevo que el del READY.
- **2790 hacia el servidor local:** reenviarla al servidor local (llamando al `Send` original) solo
  si se observa que salir al escritorio se cuelga.
- Nada de esto toca `.xtls`.
