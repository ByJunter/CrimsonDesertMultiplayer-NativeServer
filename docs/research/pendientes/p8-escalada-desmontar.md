# P8, sesión 7: escalada, subidas a salientes y desmontar vistos por el otro jugador

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`; RVA). Trabajo de solo lectura: capturas
de la sesión 7, código de `main` (commits 1b610a4, 9cc4607, 375134a, 911b900, 6a3bab1, 6bc8fa4,
22f61b4, 03c4f24, ad0e9dd, 64fd8f4, 1c434b2) y análisis estático del exe en disco (capstone,
`build/research/cddis.py` de `C:\Emulador CD`). No se tocó `src/` ni se lanzó el juego.

Notación: **[V]** = verificado con evidencia citada; **[H]** = hipótesis.

## 0. Resumen

| Síntoma | Causa raíz | Confianza | Arreglo (lado) |
|---|---|---|---|
| 1. Escalada libre: anima en el sitio y "teletransporte" arriba | Red bien: los `MoveActorAck` del escalador llegan continuos y subiendo [V]. En el cliente que observa, `PointClimb::Enter` (`0x691400`) falla en el actor remoto (modo de control 2) → `método+0x44 = 0` → no se interpola ni se hace `SetPosition` hasta que cambia el método. Misma causa que el clon de `main` | **Alta** | Portar el hook de 1b610a4, con la condición "modo de control 2" en vez de "dueño = usuario del clon". En **los dos** clientes |
| 2. Subir a salientes / saltar cajas y muros: desaparece | **Nadie envía `UpdateWallUpDataReq` (2851)**: 0 paquetes 2851/2893 en las cuatro capturas [V]. El emisor del cliente está apagado por el bool `exe+0x6D76D5C` [V]. Sin 2893 el `WallUpPath`/`WallPassPath` del actor remoto queda a cero y su modelo se dibuja fuera de sitio (medido en vivo en `main`) | **Media-alta** (el "no se envía" es [V]; "ruta a cero ⇒ desaparece" está medido en el clon, no en el actor nativo) | Poner `byte exe+0x6D76D5C = 1` en **los dos** clientes: el servidor interno ya reenvía 2851 → 2893 a `ToNearExceptOneControl` (con el relay fix) [V estático]. Sin hooks |
| 3. Desmontar: pose de montar encogida, flotando junto al caballo | El cliente que observa **nunca recibe el vínculo jinete↔caballo**: los Ack de interacción (2663/2916) van solo al que monta [V]. Sin vínculo, `RideOn::Enter`/`RideOff::Enter` fallan [V estático en `main`] → `+0x44 = 0` → el jinete se congela a la altura de la silla (y ≈ 618,0, 1,5 m sobre el suelo) en la última pose. Además el caballo remoto solo recibe `MoveActorAck` a 1 Hz (frente a 60 Hz del jinete) [V], así que queda "al lado" | **Media** (falta medir en memoria por qué no se recupera con las acciones siguientes) | Medir primero (§7). Candidatos: Move del caballo sintetizados desde el jinete; forzar `RideOn::Enter` en modo 2; vínculo real copiando las Ack de interacción, o "reaparecer" al jinete tras desmontar |

Las direcciones de los hooks de `main` siguen valiendo en esta build (bytes comprobados en el exe
en disco, §3.3).

---

## 1. Datos y método

| Captura | Rango | Contenido |
|---|---|---|
| `backups/p8-sesion7-host/cdmp_packets_20261010-060302.bin` | 06:03–06:18, 918 s | anfitrión, invitado `0xA01002E9` (usuario `0x901002E8`) y luego `0xA0101847` |
| `backups/p8-sesion7-shadow/cdmp_packets_20261010-050511.bin` y `-051629.bin` | 05:05–05:18 (anfitrión − 1 h) | invitado |
| **`D:\SteamLibrary\…\bin64\cdmp_packets_20261010-062508.bin`** (no está en `backups/`) | 06:25–06:31 | anfitrión, **segunda partida**: invitado `0xA0100309` (usuario `0x90100308`), su caballo `0xB010030B`; caballo del anfitrión `0xB0100003`. **El desmontar está aquí**; la captura del Shadow de esa partida (≈05:25–05:31) no se copió |

- Personajes: anfitrión `0xA0100001` (usuario `0x90100000`), invitado `0xA01002E9` (primera
  entrada), `0xA0101847` (reentrada del fichero 051629) y `0xA0100309` (segunda partida).
- **Alineación de relojes [V]:** la marca `+0xD5` de Move (y `+0x15F`/`+0x160` de StartAction) del
  emisor llega **sin cambios** al otro cliente (el servidor no la reescribe). Emparejando 9.944
  Move por esa marca: `t_anfitrión = t_invitado + 34242,675 s`; retardo de un sentido ≈ 35–40 ms.
- Ninguno de los dos montó a caballo en la primera partida: no hay ningún Move de jugador con padre
  = caballo. El único padre distinto de −1 es el gimmick `0xB01001F2` (16 Move, 0,5 s, un salto
  sobre un objeto).

Scripts temporales (Python, en el scratchpad de la sesión; no forman parte del repo): `cap.py`
(lector CDPK), `extract.py`/`ext2.py` (extrae Move/StartAction/Rotate/WallUp por clave),
`tscheck.py` (relevo por marca de tiempo), `events.py` (huecos de Move y tramos de escalada),
`cmp.py` (emisor frente a receptor en ventanas de 200 ms), `missing.py`, `ptrans.py`, `rate.py`,
`tl2.py`, `own.py`, `verify.py`/`heads.py`/`vt.py` (bytes y vtables del exe).

---

## 2. (a) ¿Reenvía el servidor los paquetes de escalada?

### 2.1 Todo lo que el escalador envía se reenvía [V]

| Sentido | Req del emisor (C>S) | Ack que recibe el otro (S>C), emparejados por marca de tiempo |
|---|---|---|
| invitado → anfitrión | Move 10.047 · StartAction 738 · Rotate 356 | 9.944 · 723 · 356 |
| anfitrión → invitado | Move 1.858 · StartAction 211 · Rotate 15 | 1.805 · 176 · 15 |
| cualquiera | **`UpdateWallUpDataReq` 2851: 0** | **`UpdateWallUpDataAck` 2893: 0** |

0 paquetes 2851/2893 también en `051629` y en `062508`.

Los pocos Move que no llegan son Move parados (misma posición) y los 16 Move con padre = gimmick
`0xB01001F2`; no afectan a los síntomas (anotado como curiosidad).

### 2.2 Escalada libre: la red está bien [V]

Invitado, escalada `96775acf` (t invitado 403,4–408,4; ventanas de 200 ms):

```
 t    | invitado envía     y        | anfitrión recibe   y
 0.0  | M13                619.82   | M13                619.78
 1.0  | M11                621.00   | M14                620.99
 2.0  | M12 R1             623.98   | M12 R1             623.88
 2.8  | M5                 625.15   | M9                 625.15
 3.0  | A2 R11 (b0044755)  -        | A2 R9 (b0044755)   -        <- subida a saliente: sin Move
 3.2–3.8 | R13 ×4          -        | R12–R14            -
 4.0  | A2 M5 R7           627.59   | A2 M2 R10          627.59
```

Los Move con y creciente llegan al anfitrión a la misma cadencia (~40–60 Hz). Si el actor remoto no
sube, la causa está en el cliente que observa.

### 2.3 Subida a saliente / salto de obstáculo [V]

El emisor deja de mandar Move durante 0,3–1,5 s y solo manda `RotateActorReq` (posición congelada)
y `StartActionReq` (`96775acf→b0044755` en subidas, ~1,0–1,26 s, +1,8–2,6 m; `542a0481→a7d51c05` en
cajas, 0,27–0,7 s, +0,9–1,4 m). Todo eso se reenvía. Lo que **falta** es la ruta del saliente.

### 2.4 Por qué no hay 2851 y qué haría el servidor con ella [V estático]

- Emisor `0x87DD00` (llamado desde `X::Update 0x87CBC0` solo para el personaje local):
  `0x87DD2F cmp byte [exe+0x6D76D5C], 0 / je salir`; luego `X+0xF0 == 5` (WallUp) y
  `[X+0x120] != 0`. El bool está en la cola sin datos en disco de la sección de datos RW (nombrada
  `.rdata`, flags `0xC0000040`): vale 0 y nada lo escribe (broadcast-servidor-movimiento.md §10.1).
- Manejador del servidor `0x297A4A0` (TrocTr kind 4, no bloqueado por `load_channels`): busca el
  actor por clave (`0x2A827B0`), arma `ToNearExceptOneControlSendTargetFinder`
  (`lea rdx, [exe+0x5B25358]` en `0x297A65A`) y llama al relevo `0x2977130`. **No comprueba quién
  controla el actor.** El hook del finder (`relay fix`, `server.cpp` `finder_common`) ya excluye la
  sesión del controlador y deja la otra, igual que para `MoveActorAck`.
- Conclusión: **si los clientes enviaran 2851, el servidor interno ya la reenviaría al otro
  usuario sin tocar nada en el servidor.**

---

## 3. (b) ¿Aplican las causas del lado cliente al actor remoto nativo?

### 3.1 Modo de control del actor remoto nativo = 2, igual que el clon [V]

`0x88E8E0(mc)` (`mc+0x68` = actor):

```
0x88E8EE call 0x17F2A50(actor)  -> control local          => 1
0x88E903 test byte [[[mc+0x60]+0x60]+0x10], 2              => 3
0x88E90D cmp dword [actor+0x90], 0 / jne                   => 2   (tiene usuario dueño y no es local)
```

Los registros de `ActorLoadDataAck` de la sesión 7 dan `actor+0x90` (primer u32 de los datos):
- en el anfitrión, el personaje del invitado `0xA01002E9` → **`0x901002E8`**;
- en el invitado, el del anfitrión `0xA0100001` → **`0x90100000`**.

Los dos son ≠ 0 y no locales → **modo 2**, exactamente la ruta del clon (`0x9010F001`).

### 3.2 Síntoma 1: PointClimb [V el mecanismo, alta confianza en la aplicación]

Mecanismo verificado en `pointclimb-remoto.md` y confirmado en juego en `main` (64fd8f4):
`Enter` necesita un punto de agarre que solo rellena el sensor completo del personaje local
(`0x7087B0 → 0x7089B0`). En modo 2 `D+0x19C8…` queda vacío, `0x696EF0` falla y `0x87D00A` escribe
`byte [método+0x44] = 0`. Con 0 se saltan `0x890A78` (interpolación) y `0x8913B6` (`SetPosition`).

Encaja con lo que ve el usuario: el actor se queda en el punto de agarre mientras la animación
avanza. Al pasar a la subida (`b0044755`, método 5, cuyo `Enter` sí devuelve 1 en modo 2) vuelve a
interpolar y salta los ~5 m acumulados.

### 3.3 Síntoma 2: WallUp [V que no llega 2893; H medio-alta la consecuencia visual]

- `WallUp::Enter` (`0x6A5710`, vf7 de la vtable `0x55968B8`), en modo 2 **no falla por una ruta a
  cero**: solo devuelve 0 si un flag de animación vale 6 y `D+0xEE4 != 0xF` (`0x6A5920–0x6A5970`).
  Lee la ruta de `D` (`D+0x11F8`, `D+0x1200`, y después `D+0x1170…`) para colocar la animación. Por
  eso el actor **no se congela: se dibuja fuera de sitio** ("desaparece"), como midió `main` en el
  clon (`WallUpPath = (0,0,0)`, transform subiendo bien; espejo-refinamiento.md punto 4).
- `D` de un actor remoto solo se escribe con `UpdateWallUpDataAck` (`0xB64460`; puertas: actor
  tipo 1, `byte actor+0x96 == 0`). Sin 2893, a cero.
- Los saltos de caja y muro bajo usan `WallPassPath` (`D+0x11C0…`), que va en el mismo paquete.
- Además, durante 0,3–1,5 s no hay Move. Si el hueco supera 1 s, el búfer de interpolación se
  reinicia y el final se ve como un pop. Con la ruta correcta, la animación se coloca en el borde
  real y el pop final debería quedar pequeño. En el MMO no había otra cosa.

### 3.4 Direcciones de `main` en esta build [V]

| Qué | RVA | Bytes en disco | Igual que `offsets.h` |
|---|---|---|---|
| `PointClimb::Enter` (vf7 de `0x5597028`) | `0x691400` | `4C 8B DC 49 89 5B 10 49 89 6B 18 49 89 73 20 57` | sí (`kPointClimbEnterHead`) |
| `RideOn::Enter` | `0x681C80` | `48 8B C4 48 89 58 10 48 89 70 18 55 57 41 54 41` | sí (`kRideEnterHead`) |
| `RideOff::Enter` | `0x682C20` | igual | sí |
| Modo de control | `0x88E8E0` | `48 83 EC 28 4C 8B 51 68 4C 8B D9 49 8B CA E8 5D` | nuevo |
| `WallUp::Enter` (solo lectura) | `0x6A5710` | `48 8B C4 53 56 57 41 54 41 55 41 56 41 57 48 81` | — |
| Emisor WallUp / bool | `0x87DD00` / `exe+0x6D76D5C` | `cmp byte [rip+0x64F9026], r14b` en `0x87DD2F` | — |
| Manejador cliente 2893 | `0xB64460` | `48 89 5C 24 08 55 56 57 41 54 41 55 41 56 41 57` | — |

Todas caen en `[0x1000, 0x51EB000)` (la sección de código, que en este exe se llama `.rsrc`).
Ninguna está en `.xtls`. El bool es un dato, no código.

En la rama `opcion-d` el código del hook ya existe (`remote_puppet.cpp: puppet_install_climb_fix`,
`point_climb_enter`). Pero **solo se instala con `[coop]`/espejo** (`mod.cpp` 700/759), nunca en
session2, y su condición es `owner == kPuppetUser (0x9010F001)`, que ningún actor nativo cumple.

### 3.5 Cómo reconocer "actor remoto de otro jugador" (en lugar de "el clon")

Dentro del hook, con `m` = método (`rcx`):
- `actor = [m+0x20]` (`kMethod_Actor`; `[actor+0x68]` = tabla de componentes, comprobado en
  `0x6A5892`);
- `CC = [m+0x18]` (`[CC+8]` = actor, `0x691431`);
- `mc = [CC+0xC8]`.

Condición recomendada: **`0x88E8E0(mc) == 2`** (la misma función que usa el juego) y
`(u32 [actor+0x60] >> 20) == 0xA01` (personaje de jugador).

Como alternativa sin llamar al juego: `u32 [actor+0x90] != 0`, el actor no es el jugador local
(`PlayerReader::resolve_actor`) y el bit 2 de `byte [[[mc+0x60]+0x60]+0x10]` está a 0.

**Nunca forzar en modo 1 o 3:** ahí `vf1 0x691DB0` usaría el handle de punto inválido.

---

## 4. (c) Desmontar: qué llega al otro cliente

Solo hay datos del anfitrión observando al invitado (`062508`; t = segundos de la captura):

| t | S>C en el anfitrión | Nota |
|---|---|---|
| 35906,18–35906,71 | StartAction del invitado con objetivo `+0x16D` = caballo `0xB010030B`; caballo `7b6aff57` | acercarse y montar |
| — | **ningún `InteractionPreemptionStepSuccessAck` 2663 ni `InteractionDoStepSuccessAck` 2916 del invitado** | [V] |
| 35909,63 | StartAction del invitado con padre `+0x117` = caballo | montado |
| 35909,7–35924,8 | jinete: 832 Move (mediana **16 ms**), padre = caballo; caballo: **11 Move (mediana 1,005 s, máx. 4,0 s)** | [V] `rate.py` |
| 35924,58 / 35924,84 | jinete `667e3b35` y `c9356ceb` (A1 `7b6aff57`), padre caballo, y ≈ 618,03 | desmontar |
| 35924,86 | primer Move con padre −1 (y 618,03) | |
| 35925,77 | `e21887a3`, `841a7c99`, `RotateActorAck`; y 616,65 → 616,49 (suelo) | aterrizaje |
| 35926,46 → | `25dc7fb9`/`26107d3c` (espera), Move en el suelo | |

Lo mismo en la primera partida: las 3 interacciones del invitado con `0xB01001A5` (2671 → 2663,
2720 → 2916) **no aparecen** en la captura del anfitrión. Las del anfitrión tampoco van al invitado.
`p8-sistemas-juego.md` ya clasificaba 2916 como `ToTarget` (manejador del servidor `0x29E9490`
mutado).

**Causa [H, media]:**
1. El vínculo jinete↔vehículo del cliente (`L = [[actor+0x68]+0x48]+0x18`, `u32 L+0x10`) solo lo
   escribe el paso de interacción en el cliente del que monta (`vf53` `0x2228260`, que llama a
   `0x222AFD0`, mutado). En el cliente que observa no existe nunca.
2. Sin vínculo fallan `RideOn::Enter` (`0x681C80`) y `RideOff::Enter` (`0x682C20`) (caballo-coop.md
   §3.4), y `+0x44 = 0` congela al actor.
3. Mientras cabalga, el método Vehicle (10, Enter siempre 1) interpola y el jinete "va" en postura
   de montar. Al desmontar se congela a la altura de la silla (618,0 frente a 616,5 del suelo) con
   la pose de RideOff o de montar.
4. El caballo remoto, movido solo por sus Move a 1 Hz (con reinicio del búfer a > 1 s), queda
   desplazado ⇒ "flotando junto al caballo ensillado".

En `main`:
- forzar `RideOff::Enter` dejó la copia atascada para siempre (1c434b2);
- sin forzar, también acababa atascada, y la solución fue **recargar la copia al desmontar**
  (22f61b4, 03c4f24, ad0e9dd).

**No cerrado:** por qué las acciones siguientes (`25dc7fb9`, de método Normal) no la recuperan.
Hay que medirlo (§7).

---

## 5. Diseño de los arreglos

### F1. PointClimb en modo 2 (síntoma 1). Los dos clientes. Prioridad 1

- Un módulo en `session2/` (o reutilizar `point_climb_enter`), instalado **también** con
  `s2_enabled`, en anfitrión e invitado, tras "local player in world".
- Hook inline `0x691400` con comprobación de `kPointClimbEnterHead`:

```cpp
uint8_t point_climb_enter(void* m, uint8_t same) {
    const uint8_t ok = hook.unsafe_call<uint8_t>(m, same);   // unsafe_call: corre en el hilo del juego
    if (ok) return ok;
    // __try: actor=[m+0x20], cc=[m+0x18], mc=[cc+0xC8]; key=[actor+0x60]
    if ((key >> 20) == 0xA01 && control_mode(mc) == 2) { ++forced; return 1; }  // control_mode = 0x88E8E0
    return ok;
}
```

- Contadores en el log: forzados, fallos de otros actores (clave y modo).
- `0x88E8E0` solo lee; verificar su cabecera antes de llamarla. Si no coincide, usar la condición
  alternativa de §3.5.

### F2. Ruta del saliente nativa (síntoma 2). Los dos clientes. Prioridad 1

- **F2a (recomendado):** en cada cliente, `byte exe+0x6D76D5C = 1` con `safe_write` y SEH, tras
  entrar en el mundo, y vuelta a 0 al salir.
  - El cliente del que escala manda 2851 en cada frame del modo 5 (~70–110 paquetes de 137 B por
    subida). En el invitado van por el túnel.
  - El servidor interno la reenvía como 2893 al otro usuario (§2.4).
  - Coordenadas de mundo comunes (mismo campo). El id `+0x58` (`D+0x1200`) es una clave del mismo
    servidor, así que vale en los dos PCs: es una ventaja frente a `main`.
- **F2b (si F2a no gusta):** el mod lee su propio `D` (código ya existente:
  `local_capture.cpp read_player_wallup`, 132 B) y lo publica como Req 2851 por `PostPacket` en el
  hilo del juego. Es más código y el mismo efecto.
- No hace falta tocar el servidor.

### F3. Desmontar (síntoma 3). Medir antes y luego, por orden

1. **F3a. Move del caballo sintetizados**, en el cliente que observa. Cuando llega un
   `MoveActorAck` de un jugador con padre = caballo, inyectar uno del caballo:
   - posición `+0x94` → `+0x04`, quat `+0xA0` → `+0x10`, padre −1, misma marca de tiempo;
   - es `ride_horse()` de `main`;
   - quita el desfase caballo-jinete (1 Hz frente a 60 Hz). Riesgo bajo.
2. **F3b. Forzar `RideOn::Enter`** (`0x681C80`) en modo 2, como `main`; **`RideOff::Enter` sin
   forzar** (forzarlo atascó a la copia en `main`).
3. **F3c (experimental). Vínculo real.**
   - El anfitrión, en el servidor, copia a la otra sesión las `2663`/`2916` de una monta
     (interacción `0x190`, objetivo = caballo del usuario).
   - Al desmontar, envía `RideOffByServerAck` 2938 (`caballo | jinete | asiento | 0 | flag`; el
     cliente `0xB59C20` llama a `0x2228E40`).
   - Riesgo: `0x9B6560` (el paso de interacción) está mutado y puede actuar sobre la cámara o el
     control local.
4. **F3d (equivalente a la recarga de `main`).** Tras 5 Move sin padre después de montar, el
   servidor hace "desaparecer y reaparecer" al jinete solo para el otro usuario (NotifyDisappear y
   NotifyAppear, que el mod ya engancha para `character_hold`). Es lo más seguro visualmente y lo
   más pesado.

---

## 6. Riesgos

- **F1:**
  - forzar en un actor de modo 1/3 rompería la escalada local o haría fallar `vf1` (por eso la
    condición de modo 2);
  - deslizamiento leve de la animación (ya visto en `main`);
  - el hook corre en el hilo del juego: solo lecturas con SEH, sin bloqueos.
- **F2a:**
  - el bool es de un solo lector [V], pero Denuvo podría calcular la dirección de otra forma (no
    se puede descartar del todo);
  - orden: 2893 sale **después** del `StartActionReq` de la subida (el emisor solo manda en
    modo 5). Si el `Enter` del observador corre antes de recibirla, usará una ruta vieja en el
    primer frame. En `main` se inyectaba 40 ms **antes**. Hay que medir (§7). Mitigación: retener en
    el receptor ≤ 60 ms el `StartActionAck` de un jugador remoto si llega 2893 en ese margen;
  - más tráfico por el túnel (~10 KB por subida: despreciable).
- **F3:**
  - inyectar Move del caballo mientras su propia IA o los Move reales llegan: deben ganar los
    sintéticos solo mientras haya jinete;
  - F3c toca código mutado;
  - F3d genera cargas `2005` (heap y orden; ver p8-heap-inyeccion.md).

---

## 7. Qué medir en la próxima prueba

1. **Copiar también la captura del Shadow de la segunda partida**, y la de `062508` del anfitrión a
   `backups/`.
2. **Escalada (con F1):**
   - en el observador, log del hook: veces forzado, clave del actor y modo;
   - sonda de lectura a 30 ms del actor remoto:
     - `mc+0xF0` (método);
     - `byte [[mc+0xF8+8·0x12]+0x44]`;
     - `TS+0xB4` (debe subir con los Move).
3. **Subidas a salientes (con F2a):**
   - en el sniffer del escalador, 2851 C>S (≈1 por frame en modo 5);
   - en el del observador, 2893 S>C, y **su orden frente al `StartActionAck b0044755`** (Δt);
   - en memoria del observador: `D' = [[[actor+0x68]+0x40]+0xD8]`, campos `+0x1170` (≠ 0) y
     `+0xEE0`.
   - Visual: ¿sigue desapareciendo? Prueba de cajas (`542a0481/a7d51c05`) y de saliente alto
     (`b0044755`).
4. **Desmontar (antes de cualquier F3):** sonda del jinete remoto, desde 1 s antes hasta 5 s
   después de que el Move pase a padre −1:
   - `mc+0xF0`, `+0x44` del método actual y `TS+0xB4` y;
   - el hash de acción de las capas `CC+0x88/+0x90`;
   - `u32 [[[actor+0x68]+0x48]+0x18]+0x10` (vínculo; se espera 0).
   - Del caballo: `TS+0xB4` frente a `Path.world` del jinete.
   - Con eso se decide entre F3a/F3b o F3c/F3d.
5. **Sentidos:** repetir cada prueba con los dos jugadores como escalador o jinete. El código es
   simétrico, pero solo se ha observado el lado anfitrión del desmontar.
