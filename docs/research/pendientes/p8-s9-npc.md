# P8 · Sesión 9: NPC en el invitado (tenderos, animaciones, sustituidos, fase 2)

10 de octubre de 2026, rama `opcion-d`. Build **1.0.0.2976**, direcciones en **RVA** (base `0x140000000`). Solo
lectura: no se tocó `src/`, no hubo commit y no se lanzó el juego.

**[V]** = comprobado (log, captura, capstone sobre el PE de solo lectura). **[E]** = inferencia con apoyo.
**[H]** = hipótesis sin medir.

## 0. Datos

| Ref. | Archivo |
|---|---|
| GL1 / GC1 | `C:\Emulador CD\backups\p8-sesion9-shadow\cdmp_packets_20261010-151331\cdmp.log` y `.bin/.log` (prueba 1, `guest_seq_play=1`, `live=0`) |
| GL2 / GC2 | `…\cdmp_packets_20261010-153446\cdmp.log` y `.bin/.log` (prueba 2, `live=1`) |
| HL1 / HL2 | `…\p8-sesion9-host\cdmp_fase1.log`, `cdmp_fase2.log` |
| HC1 / HC2 | `…\p8-sesion9-host\cdmp_packets_20261010-161029.*`, `-163017.*` |
| Fotos | `…\p8-sesion9-visual\Host 1 …jpg`, `Invitado 1 …jpg` (los `.MOV` no se pudieron ver aquí) |

- **Reloj:** GC1 t=0 ≈ 15:13:59; `t(HC1) ≈ t(GC1) + 211 s` (anfitrión = invitado + 1 h). Las ventanas de
  comparación por clave usan GC1 75–750 s y HC1 290–960 s; los recuentos 1:1 de §2.2 confirman que la ventana cuadra.
- **Nombres [V]:** de los registros del 2005 (`u16 len | u8 tipo | u32 clave | …`, nombre UTF-16 en el registro).
  Las claves de la sesión 9 **no** son las de la 8: Rhett `B0100156`, Turnali `B0100BE6`, Tina `B0100BE8`, posadero
  «Host» `B0100A49`, Grover `B0100A4F`, Dahlia `B0100A50`, Pedro `B0100A47`, mesa `B0100A48` + Freeswords
  `A4A–A4E` (secuenciador `0x1A4`), grupo de Damiane `0x13` (`6D`, Matthias `71`, Freeswords `72–74`, Barden `75`,
  Stranger `76`). La lista `guest_seq_watch` del ini (`C09`, `C0F`, `C12`, `143`, `F3B`, `F3C`, `1108`, `110A`) era de
  la sesión 8: en la 9 son «Game Mechanic», un halcón y una paloma. Los tenderos reales los vigiló G5 por los enlaces
  de los 2661.
- Scripts (lector CDPK, nombres, relevo por clave): scratchpad de la sesión, no se conservan.

## 1. Resumen

1. **El tick y el 3 funcionan [V].** GL1: `3 written 1` (t=84,09), `+0x1B0 17 → 0`, `started ~39`. **Todos** los NPC
   de secuenciador vigilados pasan a `+0x3D8 0` y `+0xCF 1` en ≤ 5 s (Rhett, Turnali, Tina, Dahlia, Grover, posadero,
   niños de la taberna, grupo de Damiane, Freeswords de la mesa). Por eso «muchísimo mejor»: sus acciones normales
   ya se instalan.
2. **Animaciones de escena (sentados, agachados, gestos) en NPC de secuenciador: faltan porque son `seq_bodyanim`,
   que el servidor no reenvía, y la vida de las escenas de X no corrió nunca [V].** En la prueba 1 estaba apagada
   (`live=0`); en la prueba 2 la compuerta la retuvo el 100 % del tiempo por **`X+0x2A8`**, que es un puntero
   persistente, no una bandera (§5).
3. **NPC de horario (criadas, niños, gatos): reciben el 100 % de sus acciones [V]**; lo que les falta es local: los
   ficheros de horario (`LOADED 0 failed 204`) [E].
4. **Los 23 sustituidos no desaparecieron por la sustitución [V]:** existieron con `+0xCF 1` durante minutos; el
   resumen dice `NOT on this client` porque el servidor los retiró después con su `RemoveActorAck` 2516 al alejarse.
5. **Tenderos sin menú [V en red, H en causa]:** el invitado nunca envía nada al intentar hablar (ningún C>S hacia
   el NPC). El 2857/2948 (lista de diálogos por usuario) sí funciona y da un diálogo. El bloqueo está en el cliente.
6. **Prueba 2 = prueba 1 en la práctica [V]:** 0 llamadas a `0xAA3E70(X)` y **0 2524 enrutados** por un error de
   desplazamiento en el gancho del 2524 (§6.2).
7. **Riesgo nuevo [V]:** el invitado envía `ChangeSequencerPivotReq` 2888 sobre un secuenciador **del anfitrión**
   (`0x163`) a ~90 Hz (4442 en la prueba 1, 3890 en la 2). El filtro del anfitrión no lo descarta.

## 2. Síntoma 2: qué NPC siguen sin animación

### 2.1 Estado de X y de los NPC (GL1) [V]

| Momento | X | NPC |
|---|---|---|
| 15:15:18 (antes del 3) | `+0x231 2`, `+0x1B0 17`, `+0x28 17` | 26 vigilados `+0xCF 0`, `+0x3D8 1` |
| 15:15:23 | `[3] WROTE X+0x231 2 -> 3 … at frame 3382` | — |
| 15:15:28 | `+0x231 3`, `+0x1B0 0` | los 26: `+0xCF 1`, `+0x3D8 0` |
| 15:17:53–58 (mesa y taberna) | `created 54`, `+0x1B0 4 → 0` | `A47–A50`, `A6F–A72` → `+0xCF 1` en < 5 s |
| 15:18:28 / 15:22 | `created 58 … 78` | Turnali `BE6` (`0x1C5`), Tina `BE8` (`0x1C6`), Erich, aves… → `+0xCF 1` |

- `+0x28` (lo que G5 llama «live scenes») ya valía 17 **antes** del 3: cuenta instancias, no escenas reproduciéndose.
  «live list +0x28 grew 0» no mide la reproducción. Corrige la lectura de `p8-impl-escenas-play.md` §6.
- `GuestSeqPlay summary … faults 0, re-entries 0, routing hooks on another thread 0`: sin cierres ni hilos raros.

### 2.2 Relevo de `StartAction` por clase de NPC (HC1 C>S 2223 frente a GC1 S>C 2991) [V]

| Clase (en el invitado) | Claves | Req del anfitrión | Ack que recibe el invitado | De ellas `seq_bodyanim` |
|---|---:|---:|---:|---:|
| Horario (G1 con recurso ≠ 0) | 590 | 19 051 | 19 091 | 0 |
| Secuenciador (G5, dueño `A0100001`) | 48 | 2 759 | **930** | **1 831** |
| Resto (fauna, genéricos) | 1 908 | 38 366 | 36 747 | 23 |

- Los 1 831 que faltan son **exactamente** las `seq_bodyanim` (`0x837487D4` / `0x35DD392F`, acciones con bandera
  `0x2D3`, `p8-arquitectura-observador.md` §2.3), que el servidor aplica y no difunde. Por clave: Strangers `77/78` 308
  cada uno, guardias `7E/7F` 205, sirviente `82` 205, **Freeswords de la mesa `A4A/A4B` 132 cada uno**, grupo de
  Damiane `71–74` 54, Pedro `A47` 44, caballo `154` 13. Todas con 0 recibidas.
- Los tenderos y la taberna **sí** reciben lo suyo: Rhett 42/40, Turnali 70/71, Tina 24/25, posadero 27/28, Dahlia
  22/23, Grover 64/65, niños `A6F/A70` 58/59.
- **Lectura [E fuerte]:** en el anfitrión esas `seq_bodyanim` las genera la propia escena al avanzar (vida de las
  escenas, `0xAA3E70` y su `BodyAnim 0xA79770`). En el invitado la escena de X arranca (`0x602940`, `+0xCF 1`) pero no
  avanza, así que nunca pide su `seq_bodyanim`: el NPC se queda en su acción restaurada (de pie, en espera).
  `P8Probe [BodyAnim]` estaba apagado (`s1_diag=0`), por eso no hay medida directa.

### 2.3 Cruce con las fotos [V imagen, E identidad]

- Anfitrión: dos hombres sentados a la mesa, el gigante sentado en el murete. Invitado, mismo sitio, con «Host» a
  2 m y el plato de pan en venta: los tres **de pie en espera**.
- Es la mesa del posadero; en la sesión 9 es el secuenciador `0x1A4` (mesa `A48`, posadero `A49`, Freeswords
  `A4A–A4E`), cuyos Freeswords son los de 132 `seq_bodyanim` cada uno sin relevo y los que sustituyó
  `seq_actor_restore` por la espera `0x26107D3C`. Encaja con «de pie en espera»; la correspondencia de cada figura con
  su clave es [E].

### 2.4 Por grupo

| Grupo | Ejemplos (sesión 9) | Qué les falta | Se arregla con |
|---|---|---|---|
| **Secuenciador con `seq_bodyanim`** | mesa `0x1A4`, Damiane `0x13`, Strangers `0x19`, guardias `0x16B`, Pedro `0x1A1` | vida de la escena en X (fase 2) | §5 (compuerta) y §6.2 (2524) |
| **Secuenciador sin `seq_bodyanim`** | Rhett, Turnali, Tina, Dahlia, Grover, posadero, niños `A6F/A70` | nada de red; `+0xCF 1` | ya se mueven. Si algún gesto falta, es de la escena (fase 2) [E] |
| **Horario** | criadas `0x20000184` (recurso `79AF6B66`, 16 «Maid»), niños `0x20000014`/`0x20000185`, gatos `B0100126` (`0x20000185`) y `B0100F18` (`0x200001B0`) | **nada de red** (100 % de acciones); fichero de horario sin cargar | la carga de horarios (otro agente) [E] |

- **La barrendera [E]:** sin clave en el log. Las criadas de horario `0x20000184` reciben todas sus acciones
  (p. ej. `B0100094` 97/96, `B010008B` 88/87); su «barrer» sería la actividad del punto de horario, que necesita el
  `NPCScheduleData_File`. No es de secuenciador.
- **Niños y gato de los vídeos [E]:** si son los de horario, mismo caso; si son los de la taberna (`A6F/A70`,
  `0x1B4`), reciben sus 58 acciones y les faltaría solo la parte de escena.

## 3. Síntoma 1: tenderos visibles pero sin menú de compra/venta

### 3.1 Lo que pasa por la red [V]

- **2857/2948 funcionan.** `SetQuestDialogReq` 2857 (`u32 NPC`) es la consulta de diálogos disponibles; el 2948 responde
  `u8 4 | u32 personaje | u32 NPC | lista de (u32, u32)`. Prueba 1: 647 Req → 647 Ack, ninguno de error.
- Para los tenderos el servidor **sí** da diálogo al invitado (len 17 = una entrada):

  | NPC | Invitado (GC1) | Anfitrión (HC1) |
  |---|---|---|
  | Rhett `156` | `(1, 0x1222)` | `(0xF4440, 0)`, `(1, 0x1222)` |
  | Turnali `BE6` | `(1, 0x1228)` | `(0xF4440, 0x0F)`, `(0xF4440, 0x10)`, `(1, 0x1229)` |
  | Tina `BE8` | `(1, 0x1240)` | `(0xF4440, 4)`, `(0xF435B, 0xA3)` |
  | Dahlia `A50` / Grover `A4F` | `(1, 0x11CA)` / `(1, 0x1286)` | 2 entradas `0xF4440` + … / `(1, 0x1288)` |
  | Posadero `A49`, Pedro `A47` | vacío (len 9) | vacío (len 9) |

  Las entradas `0xF4440`/`0xF435B` son del progreso de misiones de **cada** personaje [E]: son diálogos por usuario
  (`p8-arquitectura-observador.md` §2.6), no algo que el anfitrión deba copiar.
- **Al intentar abrir el menú, el invitado no envía nada al servidor.** En GC1 los únicos `InteractionPreemptionStepReq`
  2671 son de objetos (`0xF4274`, la mesa de venta: 11 éxitos, 7 «Fail» por otro que ya lo tenía) y de su mascota.
  Ninguno sobre `156`, `BE6`, `BE8` ni otro NPC. Tampoco `QuestDialogEndReq`, `ClearQuestDialogReq` ni Req de
  tienda (`BuyStockFromStoreReq`, `SellItemToItemStoreReq`…). El anfitrión tampoco abrió ninguna tienda en estas
  capturas: no hay referencia nativa del paquete de apertura.
- **«Compro objetos de su mesa»** = interacción con gimmick `0xF4274` por el servidor (2671 → 2663 → 2720 → 2916). Va
  por otro camino y funciona.

### 3.2 Causa [H, en orden]

1. **El estado de la escena del puesto.** Los tres tenderos son de secuenciador (Rhett `0x19D`, Turnali `0x1C5`, Tina
   `0x1C6`) con la escena arrancada pero congelada (sin `0xAA3E70(X)`, sin 2524/2111 en X). Si la opción de comercio
   la habilita una rama o un evento de la escena del puesto, no llega nunca. Es coherente con §2: las ramas no avanzan.
2. **Algo del cliente propio del NPC** (`+0xCF` ya es 1 [V], así que no es el login). Podría ser el modo 2 (control
   ajeno). Pero los NPC de horario, también de control ajeno, ya eran conversables según la sesión 7/8 [E].
3. No es el servidor: el 2948 da diálogo y no hay ningún Nak.

- **Cómo distinguir 1 de 2 sin código nuevo:** en la próxima sesión, el invitado intenta hablar con un comerciante
  **de horario** (p. ej. «Goldleaf Merchant» `B01000DD`, escena `0x2000001B`) y con uno de secuenciador. Si el de horario
  abre la tienda, la causa es la escena (1). Medida barata: `guest_s3_diag=1` y G5 con `comp+0x2E0` del tendero al
  pulsar.

## 4. Síntoma 3: los 23 sustituidos «NOT on this client»

- **La sustitución funcionó [V].** 23 × `SUBSTITUTED (idle wait) … APPLIED` (L0 y L1 a `0x26107D3C`), 0 `login FAILED`.
- Resumen de las 15:17:13: `0xB010006D exists +5E 0x0010 C2 1 CF 1, …` para los 13 primeros. G5 los ve `exists … +0xCF
  1` de 15:15:28 a 15:21.
- Desaparecen después, de forma nativa: `RemoveActorAck` 2516 de `B0100A4E` en GC1 t=494,8 y de `B010006D` en t=505,8
  (≈15:22:25), y G5 pasa a `NOT in this client` a las 15:22:18/28. Sus 2810 llegan a la vez (`created 64 → 70`,
  `+0x28 24 → 4`).
- El resumen de `seq_actor_restore` lee el estado **actual** cada 60 s. A partir de las 15:22 ya no estaban por
  distancia, no por un fallo. Los cinco de 15:26 (`2426`, `2429–242C`) llegaron al final (t=725,8); X se destruye a las
  15:26:13 (salida del campo) y es probable que se retiraran con ella [E].
- **Lo que no resuelve la sustitución:** la postura de escena (§2.2). Quedan en la espera hasta que la escena les pida
  su `seq_bodyanim`, y eso requiere la fase 2.

## 5. Fase 2 (prueba 2): por qué no corrió nunca

### 5.1 El campo [V]

GL2 15:36:32: `[live held] … X+0x2A4 0, +0x288 0x0, +0x2A8 0x471A8C166E0, +0x2D4 0, +0x2CC 0`. Desde el 3 (15:36:19)
hasta el final: `0xAA3E70(X) calls 0`, `not idle 26424`, `held > 10 s 1`. **El único campo no en reposo es
`+0x2A8`.**

### 5.2 Qué es `+0x2A8` (capstone) [V]

- **Escritor:** `0xA9C841 mov [rsi+0x2A8],rax`, en `0xA9C6B0`. Este crea un objeto con
  `0x3B58180([[0x6D691E0]+0xA8]+0xF38, …)` en la transformación del actor del gestor (`[G+0x18]` →
  `[[a+0x68]+0x1A0]+0x98`), y le pone `+0x94 = 0.0` (`0x3457100`). Su único llamador es `.xtls 0x11997D12` (un `jmp`). Es
  un **asa de objeto que vive con el gestor**, no una petición pendiente.
- **Lector `0xAA5040`:** `if ([G+0x2A8] && [G+0x2B0] >= 0) { o = 0x41F8A0([G+0x2A8]); interpola o+0x94 hacia
  G+0x2B4; G+0x2B0 -= dt; }`. **No lo limpia nunca**; solo descuenta el temporizador `+0x2B0`. Lo limpia la vf1
  (`0xA94A02`, reinicio).
- **En `0xAA3E70` no se lee** ningún campo de la compuerta con base G (`+0x2A4/+0x288/+0x2A8/+0x2B0/+0x2CC/+0x2D4`):
  barrido del cuerpo entero; los accesos `+0x28x/+0x2Ax` del bucle son de la escena (`rsi`/`r12`) o del marco (`rbp`).
  Solo los usan `0xAA5040`/`0xAA5400`, que el gancho de salto ya evita para X.
- **Conclusión:** la condición `+0x2A8 == 0` está mal planteada: con un asa viva la compuerta no se abre nunca, y en M
  probablemente tampoco valdría 0 [E]. El trabajo pendiente real de ese bloque es `+0x2B0 >= 0`.

### 5.3 Salida segura

- **Opción mínima (recomendada):** en la compuerta, cambiar `+0x2A8 == 0` por `+0x2A8 == 0 || +0x2B0 < 0`
  (float, NaN = no reposo, como `+0x2CC`). Dejar igual `+0x2A4`, `+0x288`, `+0x2D4` y `+0x2CC`, que **sí** son
  peticiones que solo limpia el código saltado (`p8-impl-escenas-play.md` §13.7).
- **Alternativa:** quitar `+0x2A8` de la compuerta. Con el salto puesto, la vida de X no lo lee ni lo escribe (§5.2).
  Lo que se perdería es solo la interpolación de ese objeto en X, que el gancho ya omite.
- No se debe escribir `X+0x2A8 = 0`: soltaría la referencia del gestor a su objeto sin liberarlo.
- Añadir a la línea `[live held]` `+0x2B0` y `+0x2B4`, y el `+0x2A8/+0x2B0` de M para comparar.
- Si `+0x2B0 >= 0` se queda fijo en X (nadie lo descuenta), la compuerta vuelve a retener. Se vería en la misma línea.
  En ese caso basta la alternativa.

## 6. Prueba 2 (`live=1`) frente a la prueba 1, por las capturas [V]

### 6.1 Iguales

- 3 escrito una vez (GL2 t=69,75), 31–32 2661 y 74 2981 enrutados, `2322 x1`.
- **Ninguna llamada a la vida** (§5), así que ningún 2105. HL1/HL2: `C>S dropped … 2276 0, 2105 0`, y `passed own
  0`.
- 2888 sobre `0x163`: 3890 (prueba 1: 4442).

### 6.2 El 2524 no se enrutó nunca: error de desplazamiento

- GL2: `2524 StageOnBranchEventAck 0xC08178 [own hook]: the resolved actor is not the packet's key x44`, `ROUTED 0`.
- Payload real (GC2 t=61,409): `04 | 01 00 10 A0 | 64 01 00 00 00 00 00 00 | 01 00 00 00 00` = `u8 4 | u32 actor @1 |
  u64 secuenciador @5 | …`. Es el mismo formato que el 2981.
- `guest_seq_route.cpp` 580–581 lee `packet_key = [raw+kHeader]` y `seq = [raw+kHeader+4]`, un byte antes. El log lo
  delata: `corr=2524:A0100001:0x164A0` (= bytes `A0 64 01 00…`).
- Debe ser `+kHeader+1` y `+kHeader+5`, como el 2981 (líneas 591–592). `p8-impl-escenas-play.md` §1.3 («clave u32 @5,
  secuenciador u64 @9») también está mal.

### 6.3 Riesgo nuevo: 2888 del invitado sobre una escena del anfitrión

- **Cuándo y qué:** `ChangeSequencerPivotReq` 2888 (`u64 secuenciador @0 | posición/orientación`). Todos con
  `0x163` (escena de `B010007C`, «Homing Pigeon», enlazado a X).
- Empieza en GC1 t=85,06, 1 s después del 3, y va a ~90 Hz.
- El cliente del anfitrión no envía **ninguno** (HC1/HC2: 0). En la sesión 8 el invitado tampoco (0).
- **Efecto:** el filtro del anfitrión solo descarta 2276/2105 (y 2100 con su clave). Si el servidor lo aplica al
  gestor del dueño, mueve el pivote de la escena del anfitrión desde el invitado [E]: es el «conductor doble» (R1).

## 7. Siguientes pasos (en orden)

1. **Compuerta de la fase 2:** `+0x2A8` → `+0x2A8 == 0 || +0x2B0 < 0` (o fuera, §5.3), con `+0x2B0/+0x2B4` y los de
   M en `[live held]`. Prueba en Shadow con `cdmp.p8-s7-shadow-fase2.ini`.
   - **Éxito:** `0xAA3E70(X) calls` > 0, `NOT skipped 0`, y los Freeswords de la mesa `0x1A4` sentados.
2. **Corregir el 2524** (`+1`/`+5`) y su prueba en `optd_seq_copy`/`optd_seq_play` con el payload real de §6.2.
3. **Filtro del anfitrión:** añadir **2888** (id `u64 @0`) a los C>S del usuario 2 que se descartan cuando el id es
   del anfitrión, con contador. Antes, `probe` para contar.
4. **Tenderos:** prueba de §3.2 (comerciante de horario frente a uno de secuenciador), con `guest_s3_diag=1`, y repetir
   después del paso 1 y 2. Si con la escena viva siguen sin menú, rastrear en estático qué consulta el cliente para la
   opción de tienda (siguiente encargo).
5. **Horarios:** el arreglo de la carga (otro agente) cubre criadas, niños y gatos de horario: su red ya llega entera.
6. **Ini:** actualizar `guest_seq_watch` con las claves de esta sesión. Mejor: vigilar por nombre o por enlace de 2661,
   como ya hace G5.

## 8. Riesgos

| Id | Riesgo | Nota |
|---|---|---|
| S9-1 | Con la vida de X activa, el invitado manda 2105/2591/2436/2888 de escenas del anfitrión | 2105 ya se filtra; 2888 no (§6.3). Medir 2591/2436 con id del anfitrión |
| S9-2 | Con la compuerta abierta, una escena de X pone `+0x2A4/+0x2CC/+0x2D4` | Se retiene de nuevo (correcto) y se ve en `[live held]` |
| S9-3 | El 2524 corregido engancha actores a X (`.xtls 0x8F80300`) con el pivote local | Medir `ROUTED` y efectos en la cámara del invitado |
| S9-4 | Las `seq_bodyanim` las generará el invitado como acciones propias de NPC ajenos | ¿Las envía al servidor (2223 con `0x2D3`) sobre NPC de control del anfitrión? Medir C>S 2223 con claves `B…` ajenas tras el paso 1 |
| S9-5 | La causa de los tenderos sigue siendo [H] | §3.2 |
