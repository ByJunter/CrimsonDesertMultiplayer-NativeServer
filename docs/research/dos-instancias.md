# Dos instancias del juego en un PC (anfitrión + invitado)

Build del juego 1.0.0.2976 (PE `0x6AB28F00`). Rama `opcion-d-2inst`. Estado: implementado y probado sin el juego
(tests); **sin probar en vivo**.

## 1. Para qué

Hasta ahora una prueba con dos clientes reales necesitaba Shadow como invitado. Desde el 14-10 sabemos que en un PC
caben dos copias del juego con la misma cuenta de Steam (ver `CLAUDE.md`). Con esto, el anfitrión y el invitado
corren en este PC, cada uno con su papel, su ini, sus logs y sus partidas, y se conectan por el mismo túnel UDP por
loopback que ya usa `cdmp_peer`. Así la primera prueba de dos clientes se hace aquí, con los dos logs a mano.

Shadow **sigue siendo la prueba final**: ver §6.

## 2. Cómo funciona

### 2.1 El papel de cada proceso: `CDMP_ROLE`

La variable de entorno `CDMP_ROLE` vale `host` o `guest`. **Sin ella, el mod hace exactamente lo de antes**: lee
`cdmp.ini`, escribe `cdmp.log` y usa la carpeta de partidas real. Un valor que no sea `host` ni `guest` (una errata)
deja el mod **apagado** en ese proceso, con un ERR en `cdmp.log`. Así nunca corre un invitado sobre las partidas
reales. Código: `src/mod/role_logic.h` (lógica pura), `src/mod/role.h` y `worker()` en `src/mod/mod.cpp`.

Con papel:

| Qué | Sin papel | Con `CDMP_ROLE=host` / `guest` |
|---|---|---|
| ini | `bin64\cdmp.ini` | `bin64\cdmp.<papel>.ini`. Si falta, usa `cdmp.ini` con un WRN. Si faltan los dos, escribe el de por defecto como `cdmp.<papel>.ini` |
| log (alias) | `cdmp.log` | `cdmp.<papel>.log` |
| log de la ejecución | `cdmp_<fecha>.log` | `cdmp.<papel>_<fecha>.log` (se guardan los 10 últimos de cada papel) |
| captura del sniffer | `cdmp_packets_<fecha>.log/.bin` | `cdmp_packets.<papel>_<fecha>.log/.bin` |
| volcados del invitado | `cdmp_crash_<fecha>_<PID>_<n>.dmp` | `cdmp_crash.<papel>_<fecha>_<PID>_<n>.dmp` (se guardan 2 de cada papel) |
| partidas | `...\CD\save` | anfitrión: igual que sin papel (con `save_guard=redirect`, `cdmp_save_sandbox`). Invitado: `...\CD\save_cdmp_guest` (§2.3) |

Las segundas líneas de cada log son el papel, el ini que se leyó, el log de esta ejecución y si el proceso es el
primero de su papel. Después vienen el PID del padre y la línea de órdenes:

```
Role: guest (CDMP_ROLE=guest) | ini '...\bin64\cdmp.guest.ini' | log '...\cdmp.guest_20261014-201500.log' (alias cdmp.guest.log) | first process of this role
Role: parent PID 1234 (powershell.exe) | command line: "...\CrimsonDesert.exe" PlatformServiceType=Steam
```

Además se avisa cuando el ini no encaja con el papel:

- WRN si el papel es `guest` y el ini no tiene `[session2] enabled=1 guest=1`;
- WRN si el papel es `host` y el ini tiene `guest=1`;
- WRN si el ini usa `transport=steam`, porque las dos copias comparten SteamID.

Un proceso con papel de invitado **nunca** ejecuta las etapas del anfitrión que escriben: `save_hooks_install` las
rechaza con un ERR y la opción D queda apagada en ese proceso.

### 2.2 El segundo `CrimsonDesert.exe` de corta vida

Unos 40 s después de arrancar, el juego abre un segundo `CrimsonDesert.exe` (S28, `u0-resultados-s28.md` §8.8). Ese
proceso carga el ASI, hereda `CDMP_ROLE` y antes sobrescribía `cdmp.log`. Para evitarlo, cada papel tiene un mutex
con nombre, `Local\cdmp_role_<papel>`, que crea el primer proceso y que se libera cuando ese proceso termina.

El segundo proceso del mismo papel encuentra el mutex ya creado y entonces:

- escribe solo en `cdmp.<papel>.extra.log`, con su PID, su padre y su línea de órdenes, para identificarlo en la
  prueba;
- no instala nada más: ni ganchos, ni red, ni sniffer, ni overlay;
- si es del papel invitado, **sí** instala la redirección de partidas, como el primero (§2.3).

Sin papel no cambia nada: el segundo proceso sigue haciendo lo de antes.

### 2.3 Las partidas del invitado: `save_cdmp_guest`

Las dos copias comparten `%LOCALAPPDATA%\Pearl Abyss\CD\save\<cuenta>\`. En esa misma carpeta están también los
ajustes del juego: `save\user_engine_option_save.xml` (gráficos y ventana) y `save\initialize_input.save` (controles)
(`p8-guardado-desviado.md` §1).

En el proceso con papel de invitado, el mod reutiliza los ganchos de KernelBase de `save_guard=redirect`
(`src/mod/session2/save_hooks.cpp`, `save_hooks_install_guest_role`). Toda ruta bajo `...\CD\save` pasa a
`...\CD\save_cdmp_guest`, con las mismas subcarpetas: `CreateFile*`, borrar, mover, copiar, atributos, carpetas,
`FindFirstFile*` y `GetFileAttributes*`, en sus versiones A y W.

Diferencias con la caja del anfitrión:

- **Es persistente.** No se copia nada de la carpeta real ni hay rotación, `_prev` ni `host_save`. Lo que guarde el
  invitado se queda ahí para la siguiente prueba.
- **Se instala al principio del hilo del mod**, antes de leer el ini y milisegundos después de cargar el ASI, mucho
  antes de que el juego lea su lista de partidas. El anfitrión, en cambio, la instala en la pantalla de título, lo que
  vale porque su caja es una copia de la carpeta real.
- **Si `save_cdmp_guest` no existe**, el mod la crea vacía y lo avisa con un WRN (`did NOT exist: created EMPTY`). El
  invitado verá entonces un título sin partidas. El mod nunca copia nada de la real: eso lo hace el lanzador (§3).
- **Si `save_cdmp_guest` es un enlace** o no es una carpeta, se rechaza, porque podría llevar de vuelta a la real.
- **Si la redirección no se puede instalar**, el mod termina el juego en ese momento (`TerminateProcess`, código
  `0xCD60`), antes de que toque ninguna partida. Es preferible a un invitado escribiendo en la carpeta real.
- En `cdmp_host.exe` (pruebas sin juego) la redirección **no** se instala: ese programa descarga el mod y los ganchos
  de KernelBase quedarían apuntando a la nada. Se prueba aparte en `optd_guest_role` (§4).
- Cada 60 s el log dice dónde han ido sus escrituras: `Role guest: save redirect to '...save_cdmp_guest', writes N,
  reads N, queries N`.
- `guest_own_save` del invitado elige su ranura en `save_cdmp_guest\<cuenta>\`, que es la copiada.

`save_cdmp_guest` está fuera de `save\`, así que Steam Auto-Cloud no la sincroniza y no choca con la
`cdmp_save_sandbox` del anfitrión.

Con el anfitrión no cambia nada: ni `save_guard`, ni `host_save=sandbox|export|probe`, ni su
`cdmp_save_sandbox(_prev)`, ni el buzón `guest_own_save`, ni `cdmp_guest_save_staging`. El buzón del anfitrión
(`%LOCALAPPDATA%\cdmp\guest_saves\unknown`; por UDP no hay SteamID) y el del invitado
(`%LOCALAPPDATA%\cdmp\guest_progress\unknown-host`) son carpetas distintas, y cada una la escribe un solo papel.

`tools\scripts\aplicar-progreso-invitado.ps1` aplica una generación sobre la carpeta **real** por defecto. Para el
invitado de este PC, pásale `-CarpetaPartidas "%LOCALAPPDATA%\Pearl Abyss\CD\save_cdmp_guest"`.

### 2.4 La conexión local

El código ya lo permitía sin cambios. El transporte UDP del mod (`run_network`, `src/common/transport.h`) es el mismo
para un cliente real que para `cdmp_peer`:

- un `role=host` escucha en `peer_port`, y con `host_loopback_only=1` solo en 127.0.0.1;
- un `role=guest` se conecta a `peer_host:peer_port`;
- el túnel Troc (`session2::tunnel_wanted` en el anfitrión, `guest_active` en el invitado) va encima de esa
  `ClientSession` y no depende del transporte;
- el anfitrión se engancha al primer `Hello`, igual que con `cdmp_peer`, y el invitado P8 (`guest=1
  guest_mode=remote`) habla el mismo túnel que por Steam.

La línea `udp port 54461 (loopback only)` de las pruebas con `cdmp_peer` es el puerto efímero del anfitrión **como
cliente de red**: en esas pruebas `cdmp_peer` escuchaba en 37015 y el juego se conectaba a él. Aquí es al revés: el
juego anfitrión escucha en 37015 y el juego invitado se conecta. Por eso no se puede arrancar `cdmp_peer` a la vez.

Los inis:

- `docs/research/ini/cdmp.2inst-host.ini`: el `cdmp.p8-s36-host-paso1.ini` del kit S36 con `[net] transport=udp
  role=host peer_port=37015 host_loopback_only=1`.
- `docs/research/ini/cdmp.2inst-guest.ini`: el `cdmp.p8-s36-shadow.ini` con `[net] transport=udp role=guest
  peer_host=127.0.0.1 peer_port=37015`.

Solo cambian eso y `player_name`. `[session2]` es idéntico al del kit. No hace falta sala ni invitación de Steam.

### 2.5 Otros recursos compartidos

| Recurso | Riesgo | Qué se hizo |
|---|---|---|
| Objetos con nombre del mod (mutex, mapeos, tuberías) | No había ninguno | Solo el nuevo `Local\cdmp_role_<papel>` |
| Puerto UDP | El anfitrión ocupa 37015 en 127.0.0.1 y el invitado un puerto efímero | No arrancar `cdmp_peer` a la vez |
| Transporte Steam | Misma SteamID en los dos procesos. Uno no es "amigo" del otro y los dos leen el canal 67 | Se usa UDP (WRN si un ini con papel usa Steam) |
| F8 (mostrar u ocultar el panel) | `GetAsyncKeyState` es global: las dos copias lo cambiaban | Con papel, solo cuenta en la copia que está en primer plano |
| Ctrl+F12 (MARCA) | Global | Se deja así: la marca cae en los dos logs a la vez, que es justo lo que sirve para alinearlos |
| Teclas de desarrollo (`[dev]` clone, combat, aggro…) | Globales | Déjalas apagadas (los inis del kit lo hacen) |
| Overlay | Cada proceso busca su ventana por su PID y usa su ventana falsa | Sin cambio |
| Ajustes del juego (`save\user_engine_option_save.xml`, `initialize_input.save`) | Viven en `save\` | El invitado tiene los suyos en `save_cdmp_guest` (el lanzador copia los de la real si faltan). Los del anfitrión, con `save_guard=redirect`, van a su caja y **se pierden** al arrancar otra vez: el modo ventana del anfitrión se fija en una partida normal |
| Caché de sombreadores y otros ficheros del juego | Desconocido; la documentación no los menciona | Comprobar en vivo (§5) |
| GPU, VRAM y CPU | Dos copias del juego en una GPU | Resolución baja y ventana en las dos. La copia sin el foco cae a 2-4 fps: §2.6 |
| Sonido | Suenan las dos | Silenciar una en el mezclador de Windows |

### 2.6 La copia sin el foco: `background_full_speed`

**Síntoma (14-10, en vivo).** La ventana que no tiene el foco cae a 2-4 fps. Eso falsea las pruebas, porque el cliente
de esa copia también mueve sus NPC (U-e) y su jugador.

**Causa (build 2976).** No es un freno del juego. Lo que hace el juego al perder el foco es esto, y nada más:

| Mensaje | Qué hace el juego | Dónde |
|---|---|---|
| `WM_ACTIVATE` inactivo / `WM_KILLFOCUS` | Evento `PlatformWindowActivateEvent_INACTIVE` / `_KILLFOCUS`. El gestor de entrada deja de leer la entrada y suelta las teclas | WndProc `0x3F41310` (y `0x3F45A00`); manejador `0x2DFB1A0` |
| `WM_ACTIVATEAPP` inactivo | `WinApp+0x74 = 0` y evento `PlatformApplicationActivateEvent_INACTIVE`: el renderizador apaga Reflex (bit 8) y la generación de fotogramas (bit `0x20`) en `device+0x98`. En pantalla completa exclusiva, además, minimiza la ventana | `0x3F41310`; `0x3DC6CF0` → `0x14A8D6D0` (`.xtls`, código normal) |
| Entrada | El dispositivo de entrada solo acepta entrada si `GetActiveWindow()` y `GetForegroundWindow()` son su ventana | `0x2E30E50` → `0x10FE4610` (`.xtls`) |

El bucle de `WinApp::Run` (`0x14C87FD0`, en `.xtls` pero sin ofuscar) tiene un `Sleep` por vuelta, pero depende de
dos `GlobalVariable` (bool `0x6C9B6B0`, valor en `0x6C9B6E8`; ms `0x6C9B750`, valor en `0x6C9B788`) que en el juego
no cambia nadie: solo `WinAppLauncher::vf4` (`0x359C90`) copia en ella sus 100 ms. Los nombres de las variables van
con hash y no hay más referencias en el exe.

Lo que frena a la copia sin foco se ve en los `FreezeWatch` del 14-10 (los dos papeles, cientos de muestras en cada
uno, con episodios de 300 a 530 ms, es decir, 2-3 fps). El hilo principal está casi siempre esperando **su propia valla de
la GPU**:

`0xAD2234` → … → `0x3837640+0x6E` → `0x38D9F20` → `0x3E00790` (`ID3D12Fence::SetEventOnCompletion` y un bucle de
`WaitForSingleObject` de 10 ms).

Es decir, el trabajo de GPU de esa copia termina tarde. La explicación **[inferencia]** es que la otra copia, con el
foco, llena la GPU y Windows planifica primero el trabajo de GPU del proceso en primer plano. Las dos copias tenían
ventana, DLSS 4.5, generación de fotogramas x3 (`_numFramesToGenerate=2`), Reflex, sin VSync y sin límite de
fotogramas.

**Por qué no se "engaña" al juego con el foco.** Hacer creer al juego que tiene el foco (interceptar `WM_ACTIVATE` o
`GetForegroundWindow`/`GetActiveWindow`) no quitaría nada, porque el juego no se frena. Además, rompería la entrada:
el ratón se registra con `RIDEV_INPUTSINK` (`0x3F41310`, caso `WM_SETFOCUS`), así que las dos copias moverían la
cámara a la vez.

**La opción.** `[general] background_full_speed=1` (también vale en `[session2]`; `[general]` manda).

- Solo funciona con `CDMP_ROLE`. Sin papel, escribe un WRN y no hace nada. En `cdmp_host` tampoco hace nada.
- No pone ganchos ni lee memoria del juego. Código: `src/mod/bg_speed.cpp`, con la lógica en `bg_speed_logic.h`.
- Un hilo del mod mira la ventana en primer plano cada 250 ms.
  - Mientras es de otro proceso, sube la clase de prioridad de GPU de este proceso
    (`D3DKMTSetProcessSchedulingPriorityClass`, de gdi32) a **High**. Si se rechaza, prueba **AboveNormal**.
  - Cuando el proceso recupera el primer plano, vuelve a la clase original (Normal).
  - Tras 8 fallos deja de intentarlo.
- Al arrancar, saca el proceso del *power throttling* de Windows: EcoQoS y el límite de resolución del temporizador
  para ventanas ocultas.
- Lo que hace el juego con el foco sigue igual: la entrada solo va a la ventana con el foco, y Reflex y la generación
  de fotogramas se apagan en la copia sin foco (esto le quita trabajo a la GPU).

En este PC, el test `optd_bg_speed` comprueba que **High se acepta sin administrador** y que se lee de vuelta.

Líneas del log:

```
BgSpeed ON (host): no hook, no game memory. ... GPU scheduling class ... is High (original Normal); power throttling off (...)
BgSpeed: GPU class Normal -> High (another window has the foreground)
BgSpeed: background N s, foreground N s | switches to background N, to foreground N | GPU class ... | sets ok N, failed N | power throttling off (...)   (cada 60 s)
```

**Medido en vivo (15-10): la opción sola NO basta.** La clase de GPU cambia bien en cada cambio de ventana (31 cambios,
0 fallos), pero la copia de atrás siguió a unos 4 fps. Lo que sí lo arregla es dar margen a la GPU:

- **Límite de fotogramas en el controlador de NVIDIA**: «Velocidad máxima de fotogramas» = 45 para Crimson Desert (el
  juego no tiene esa opción). Con eso **las dos copias van a 45 fps fijos**, también la de atrás; solo bajan un
  momento mientras el invitado carga a todos los NPC.
- Preset **Minimum**, ventana (1152×864 en el anfitrión) y la generación de fotogramas apagada en las dos copias.
- La opción «Velocidad máxima de fotogramas de la aplicación en segundo plano» de NVIDIA estaba desactivada: no era
  la causa.

`background_full_speed=1` se queda en los inis (no estorba), pero el ajuste que importa es el límite de NVIDIA.

Los ajustes del anfitrión con `save_guard=redirect` se pierden al reiniciar (§2.5): se fijan una vez con el juego
**sin el mod** (renombrar `cdmp.asi`), en la pantalla de título. Los del invitado se cambian en su ventana y se quedan
en `save_cdmp_guest`.

## 3. Cómo lanzarlo

Antes:

- compila e instala (`.\scripts\build.ps1`, `.\scripts\deploy.ps1`);
- abre Steam;
- cierra el juego.

Luego:

```
powershell -ExecutionPolicy Bypass -File "C:\Emulador CD-2inst\scripts\launch-2inst.ps1"
```

`scripts\launch-2inst.ps1` hace esto, en orden, y se para al primer fallo sin lanzar nada:

1. Se niega si hay algún `CrimsonDesert.exe` abierto o si Steam no está abierto.
2. Copia `...\CD\save`, y `save_cdmp_guest` si existe, a `<BackupDir>\saves-2inst-<fecha>\` y comprueba la copia
   (mismos ficheros y mismos bytes). `-BackupDir` vale por defecto `C:\Emulador CD\backups`.
3. Prepara `save_cdmp_guest\<cuenta>\<GuestSlot>`. `-GuestSlot` vale por defecto `slot108`, el personaje de prueba
   del invitado, el mismo que usa `cdmp_peer`. Si esa ranura no está, la copia de la real y verifica el SHA-256. Si
   ya está, la deja como está, porque es el progreso del invitado. Se niega si la real tiene un `slot108_new` o un
   `slot108_backup` (guardado interrumpido). También copia los dos ficheros de ajustes de `save\` si faltan. Con
   varias cuentas hay que pasar `-Account <id>`.
4. Copia `-HostIni` y `-GuestIni` (por defecto los dos de §2.4) a `bin64\cdmp.host.ini` y `bin64\cdmp.guest.ini`.
5. Lanza el anfitrión **directamente** (§3.1): el `.exe` con `SteamAppId=3321460`, `SteamGameId=3321460`,
   `CDMP_ROLE=host` y el argumento `PlatformServiceType=Steam`.
6. Espera hasta 300 s (`-HostWaitSec`) a que `cdmp.host.log`, con el PID del anfitrión, diga `Late init: game is
   up`, es decir, la pantalla de título. Si el proceso del anfitrión termina antes, se para y lo explica.
7. Lanza el invitado igual que el anfitrión, con `CDMP_ROLE=guest`.
8. Imprime los dos PID y los logs.

Con `-NoLaunch` solo hace los pasos 1 a 4 y no exige Steam.

En el juego:

1. En el anfitrión, carga su partida y espera a estar en el mundo.
2. En el invitado, pulsa **Continuar**. Su ranura es la copiada.
3. Pon las dos ventanas en modo ventana y a una resolución que quepa (§2.5).

Logs que hay que leer, en `bin64`:

- `cdmp.host.log` y `cdmp.guest.log`, que son alias del `cdmp.<papel>_<fecha>.log` de esta ejecución;
- `cdmp_packets.host_*` y `cdmp_packets.guest_*`;
- si aparece el proceso de corta vida, `cdmp.<papel>.extra.log`.

Ctrl+F12 marca los dos logs a la vez.

### 3.1 Por qué el anfitrión no se lanza con `steam://`

Una partida lanzada con `steam://rungameid/3321460` la arranca Steam, no el script, así que **no hereda
`CDMP_ROLE`**: el anfitrión arrancaría sin papel (`cdmp.ini`, `cdmp.log`). Por eso las dos copias se lanzan igual que
se comprobó con la segunda el 14-10: el `.exe` directamente, con las variables de Steam y `PlatformServiceType=Steam`.
Así el juego no se relanza a través de Steam.

Que esto funcione **también para la primera copia** es lo lógico, porque es el mismo mecanismo, pero **está sin
comprobar**. Es lo primero que hay que ver en vivo (§5).

Si el anfitrión se relanzara por Steam:

- el lanzador lo detecta, porque el proceso que lanzó termina antes del título;
- el juego que arranque Steam no tendrá papel;
- la alternativa es arrancar el anfitrión a mano por Steam con su ini en `cdmp.ini` (sin papel, como siempre) y lanzar
  solo el invitado con papel. El script no hace esto todavía.

## 4. Tests (sin el juego)

- `optd_two_inst` (`tests/optd_two_inst_tests.cpp`): cubre
  - cómo se lee `CDMP_ROLE`, incluidas las erratas, que dejan el mod apagado;
  - la elección del ini;
  - los nombres con papel, y que la poda de logs de un papel nunca toca los de otro ni los de sin papel;
  - el mapeo de `save` a `save_cdmp_guest`;
  - con `log.cpp` de verdad: un proceso anfitrión con su log abierto, un invitado y un segundo proceso del anfitrión
    en la misma carpeta, sin que ninguno toque los ficheros de otro.
- `optd_guest_role` (`tests/optd_guest_role_tests.cpp`): la redirección del invitado con los ganchos reales en un
  `%LOCALAPPDATA%` falso.
  - En el título no se ve ninguna ranura de la real.
  - La secuencia del autoguardado (`_new`, `_backup`, `_delete`), las API A y W, las rutas relativas, `\\?\` y `/`
    acaban todas en `save_cdmp_guest`.
  - `save_hooks_install` del anfitrión se rechaza.
  - **La carpeta real queda idéntica byte a byte.** Lo comprueba un proceso hijo sin ganchos.
  - Una carpeta de invitado que ya existe se usa tal cual, sin copia.
  - Una que es un fichero se rechaza.
- `launch_2inst_script` (`tests/launch_2inst_tests.ps1`): el lanzador con `-NoLaunch` sobre carpetas falsas.
  - Copia de seguridad comprobada.
  - Se copia solo la ranura elegida, y una sola vez: el progreso del invitado no se pisa.
  - Copia los ajustes y los inis.
  - La carpeta real no cambia.
  - Se niega con: dos cuentas sin `-Account`, guardado interrumpido, ranura sin su par, carpeta del invitado que es un
    enlace, nombre de ranura malo, ini que falta, sin `cdmp.asi`.
- `optd_bg_speed` (`tests/optd_bg_speed_tests.cpp`): la lógica de `background_full_speed` (§2.6).
  - La compuerta: solo con papel, y nada sin juego.
  - Quién tiene el primer plano.
  - Las clases que se ponen al cambiar el foco, el tiempo y los cambios contados.
  - Que High rechazada pasa a AboveNormal, y si se rechazan las dos, ninguna.
  - El tope de 8 fallos, y una clase original ya alta.
  - Además, a título informativo, si este PC acepta High sin administrador.

Prueba a mano sin el juego (14-10): tres `cdmp_host.exe` en una carpeta temporal, uno con `CDMP_ROLE=host`
(UDP 37116 en loopback), otro con `host` también y otro con `guest`. El primero y el invitado se conectaron (RTT
0,4-0,8 ms). El segundo `host` escribió solo `cdmp.host.extra.log` y se quedó inerte. Cada uno dejó su
`cdmp.<papel>_<fecha>.log` y no hubo `cdmp.log`.

## 5. Qué mirar primero en vivo (en este orden)

1. **El anfitrión lanzado directamente no se relanza por Steam.** Comprueba que el PID que imprime el lanzador es el
   del juego y que `cdmp.host.log` dice `Role: host`.
2. **El invitado arranca y su título no muestra las partidas de la real**: solo `slot108` (`Role guest: save
   REDIRECT ON` en las primeras líneas). Después de jugar, `save\<cuenta>\` no debe tener fechas nuevas y
   `save_cdmp_guest` sí.
3. **El proceso de corta vida:** ¿aparece `cdmp.<papel>.extra.log`? ¿Quién es su padre y con qué línea de órdenes?
   ¿Le pasa algo al juego por no tener el mod?
4. **El foco.** Medido el 14-10: el juego no pausa sin foco, pero la copia sin foco cae a 2-4 fps porque espera a
   su GPU (§2.6). Con `background_full_speed=1` (los dos inis de §2.4), comprueba:
   - **La entrada.** Teclado, ratón y mando solo deben mover la copia que tiene el foco. Con las dos ventanas visibles,
     mueve el ratón y pulsa teclas sobre una: la cámara y el personaje de la otra no deben moverse.
   - **Los fps de la copia sin foco**, contra la misma prueba sin la opción. En el log: la línea `BgSpeed: GPU class
     Normal -> High` y menos episodios `FreezeWatch`.
   - **La carga de la GPU.** Mírala en el Administrador de tareas (GPU por proceso) y comprueba que la copia con el
     foco no se hunde.
   - Si no mejora, aplica §2.6, "Si no basta".
   - Minimizado, el bucle principal no corre (`UiPause: NO FRAMES`): no minimices ninguna de las dos.
5. **Rendimiento y memoria con dos copias**, y si el anfitrión, con su servidor dentro, aguanta al invitado igual
   que en Shadow.
6. Ajustes: que el invitado pueda ponerse en ventana sin tocar los del anfitrión (están en `save_cdmp_guest`).

## 6. Lo que un PC no puede validar

- **La latencia y las pérdidas de una red real.** Por loopback el RTT es de ~0 ms; con Shadow era de 30 a 130 ms.
  Los fallos que dependen del tiempo (interpolación, retenciones, cargas que se cruzan) pueden no aparecer aquí.
- **El transporte de Steam** (`SteamTransport`: límites de envío, cola, reconexión, el amigo que juega) no se usa.
- **Cargas a distinta velocidad.** Las dos copias comparten disco, CPU y GPU: cargan casi a la vez o se frenan entre
  sí. En dos PC el invitado carga a su ritmo.
- **Dos cuentas de Steam.** Aquí las dos copias son la misma cuenta y la misma `<cuenta>` de partidas. El
  `guest_own_save` va por UDP con id `unknown`, no con el SteamID del invitado.

**Shadow sigue siendo la prueba final antes de dar algo por bueno.**
