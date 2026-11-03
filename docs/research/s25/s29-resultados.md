# Sesión 29 con Shadow: resultados de los pasos 1 y 2 (13 de octubre de 2026)

Análisis de solo lectura. Build 1.0.0.2976 (PE `0x6AB28F00`, base `0x140000000`).

| Id | Fichero | Configuración |
|---|---|---|
| H1 | `C:\Emulador CD\backups\p8-sesion29-host\cdmp_20261013-124327.log` (+ `cdmp_packets_20261013-124341.*`) | `docs/research/ini/cdmp.p8-s29-host-paso1.ini` |
| H2 | `…\cdmp_20261013-130045.log` (+ `cdmp_packets_20261013-130056.*`) | `cdmp.p8-s29-host-paso2.ini` |
| G1 | Shadow `cdmp_20261013-114512.log` (+ packets) | `cdmp.p8-s29-shadow.ini` |
| G2 | Shadow `cdmp_20261013-120923.log` (+ `cdmp_packets_20261013-120937.*`) | `cdmp.p8-s29-shadow-paso2.ini` |

El reloj de Shadow va una hora por detrás del del anfitrión. Lo que vio el usuario:
- **Paso 1:** Shadow murió y revivió en un punto de reaparición, con una pantalla de carga muy corta. Todo bien.
- **Paso 2:** Shadow **vio la cuerda del anfitrión**. Revivir funcionó cerca y lejos. Nada más raro. El anfitrión no
  cambió de zona después de los revives. La reentrada se rechazó (§1).

## 1. Reentrada rechazada: la baseline del campo no se tomó

**Causa:** el invitado entró mientras el anfitrión seguía en la pantalla de título, sin personaje principal. Por eso
`server_host_field` falló en todas las muestras. Los detalles, el arreglo (comprobación c estructural) y por qué es
seguro están en `reentrada.md` §6.

- Shadow se une a las 13:09:50 y el usuario 2 existe a las 13:10:02,962. El anfitrión carga su personaje a las
  13:10:04,6 y entra en el mundo a las 13:10:46.
- Tomar la baseline tarde no habría servido. A las 13:10:07 el campo del anfitrión era el transitorio `…17700`, con
  generación `0x81020000`; el del adiós fue `…17A10`, con `0x81020001`.
- En el adiós, a, b y d pasan en 250 ms. Solo falla c (`no baseline`), y el campo estaba limpio.

## 2. F4 (`user2_revive_manager`)

La línea `TravelRevive` del paso 1 existe, pero va con nivel **ERR**: `H1 [12:57:06.251] ERR … TravelRevive trip 2`.

**Paso 1 (`probe`):**
- El 2362 llega al setter `0x2BC4950`, retorno `exe+0x2BC4C53`: `probe: the ORIGINAL (the owner's +0x6A0 is zeroed)`
  y `user 2's would be 0x22C4D513100`.
- `TravelRevive`: `host manager +0x6A0 2 -> 0 … HOST TOUCHED`.
- `User2Stages [state]` del anfitrión: `state 2` a las 12:56:52; `state 0` desde las 12:57:07 hasta el final, con
  `seq 18` fijo.
- La puerta de la stage del anfitrión (ShopProbe) se congela mientras sus actualizaciones siguen subiendo:

  | | 12:56:43 | 12:57:43 | 12:58:43 | 12:59:43 |
  |---|---|---|---|---|
  | `gate` | 274 | 284 | 284 | 284 |
  | `updates` | 279 | 313 | 335 | 358 |

**Paso 2 (`1`):**
- Dos revives, a las 13:17:09 y a las 13:18:43. Los dos dan `F4 ACTED: user 2's own manager`; el resumen,
  `F4 ACTED … 2`.
- `TravelRevive`: `host manager +0x6A0 2 -> 2 … ARMED … the host's untouched`.
- El anfitrión sigue en `state 2` hasta las 13:21:28, y su puerta sigue subiendo:

  | | 13:16:57 | 13:17:57 | 13:18:57 | 13:19:57 | 13:20:57 |
  |---|---|---|---|---|---|
  | `gate` | 154 | 180 | 207 | 225 | 243 |

Las creaciones de stages no aportan nada: no hubo ninguna después de los revives en ningún paso, porque el anfitrión
no cambió de celda. **Veredicto: F4=1 funciona y se queda.**

## 3. `docked_gimmicks_other` (`gimmicks-dueno.md` §8.5): pasa

| Criterio | Paso 1 | Paso 2 |
|---|---|---|
| `would rewrite (other player, probe)` / `REWRITTEN (other player, local drive)` | 6 / — | — / 6, «Game Mechanic» |
| Comprobación de `+0x90` | match 14, DIFFERENT 0 | match 14, DIFFERENT 0 (`0x90100065`) |
| `withheld/would other player` | 2774 0/6 | 3082 146, 2758 90, 2609 42, 2774 6 |
| `0x8E59D0` sobre el dispositivo del otro | 1026 llamadas, 560 rechazadas | 632 llamadas, 0 rechazadas |
| `TOTAL for its own` / `for the host's` (anfitrión) | 0 / 6 (la base) | 0 / 0 |
| 3082 / 2609 del anfitrión para su propio dispositivo (captura) | 167 / 49 | 163 / 50 |
| Errores de juego | `responseTrChangeAIStateAck`, RelaseCatch ×3 | solo `responseTrChangeAIStateAck` |
| Cierres | ninguno | ninguno |

**M1, por *opcode*.** Tiempo en el mundo del anfitrión: unos 750 s en el paso 1 y unos 564 s en el paso 2. Ningún
*opcode* crece:

| Clave | *Opcode* | Paso 1 | Paso 2 |
|---|---|---|---|
| Dispositivo del anfitrión | 2556 | 2589 (3,4/s) | 943 (1,7/s) |
| | 2828 | 53 | 15 |
| | 3067 | 428 | 72 |
| | 2774 | 6 | 0 |
| Personaje del anfitrión | 2249 | 7636 (10,2/s) | 4455 (7,9/s) |
| | 2350 / 2380 / 2399 / 2612 / 2699 / 2990 / 2999 | 13 / 14 / 14 / 10 / 15 / 5 / 4 | 0 cada uno |

**Veredicto: se queda en 1.** Queda sin comprobar, de §8.5, la alineación de ±0,2 s entre los rechazos del paso 1 y
las 3082 del anfitrión.

## 4. `gimmick_event_dedup`: pasa

- **Paso 2:**
  - el invitado: `WITHHELD 5`;
  - el anfitrión: 2779 del cliente del anfitrión 5 y del usuario 2 0; `first 5`, `DUPLICATE created 0`, ninguna clave
    sin 2250.
- **Caso lejano (paso 1):** `keys 0, 2779 0`. Los jugadores llegaron a estar a 7 celdas, pero el usuario 2 solo mandó
  6 2779: no es una prueba fuerte.
- **Una 2779 del usuario 2 sin 2250 en el paso 1.** Era redundante: el anfitrión también la mandó, y su cliente recibió
  el `first`.

## 5. `tree_bend` (`gancho-cuerda.md` §14.7)

| Criterio | G1 | H1 | G2 | H2 |
|---|---|---|---|---|
| Tirones (`max − min > 0,2`) / tramos | 7 / 14 | 4 / 8 | 5 / 10 | 4 / 8 |
| Recibidos `new run`/`end` (`bad 0`) | 8/8 | 14/14 | 8/8 | 10/10 |
| `would apply` / `state != 3` | 1992 / 82 (96 %) | 2384 / 96 | 2033 / 74 | 1968 / 70 |
| FIGHTS / tabla llena | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `SetBend` nativo: dentro del tick / fuera | 0 / 3865 | 0 / 1073 | 0 / 2399 | 0 / 1184 |
| `SetBend` nativo en el hilo principal | 95 % | **77 %** | 95 % | **73 %** |
| Hilos del `SetBend` nativo / `in both` | 5 / 1 | 13 / 1 | 5 / 1 | 13 / 1 |
| M4 del remoto en el hilo principal | 100 % | 100 % | 100 % | 100 % |

- El hilo de cada `bend run END` es el del **primer** frame del tramo.
- Las cifras del anfitrión salen del resumen `(before a new connection …)`.

**Ghidra:**
- vf2 `0x117F7E0` no tiene `call` directo; solo aparece en la ranura de vtable `0x56FFB58` (despacho del *script*).
- **[V código, E en el juego]** `0x8A30C0` lleva una **copia en línea de `SetBend`**: escribe `+0x2A8`, `+0x2A4` y
  `+0x2B4`, llama a la vf4A8 y hace `+0x59 |= 8`. La llama `0x87A700+0xBC6`, que llama
  `MovementController_UpdateByMode` `0x88F4E0`. M3 no la ve y FIGHTS no la cuenta.

**Recomendación:**
- Por la regla escrita en §14.7, M4 **no pasa**: hay `OUTSIDE > 0` y los hilos no coinciden.
- El nativo, sin embargo, no tiene un contexto único: corre en trabajos de actualización de actores, en el hilo
  principal y en hasta 12 *workers*. M4 corre en el trabajo del remoto, siempre en el hilo principal.
- Propuesta: activar el paso 3 **cambiando el criterio** y con estas guardas:
  - aplicar solo en el hilo principal;
  - no aplicar si el jugador local tiene abierto un tramo sobre el mismo árbol;
  - SEH, y apagado al primer fallo;
  - un contador de solo lectura en `0x8A30C0`, por clave de personaje.

  La decisión es del usuario.

## 6. U-0b

- **«Mismo campo» funciona.** Juntos: `the same field` 34 y 31. Separados: `DIFFERENT fields` 3 y 3, sin ARM falso;
  `U would be OFF` en todo momento.
- **Stage en `+0xAC`: plausible.** Con la distancia a 0, 36 de las 41 stages del anfitrión quedan a 3 celdas o menos
  del usuario 2. Hay 5 lejanas, que dan el rango fijo `0..106`.
- **Logout con motivo 30 (127 en H1, 29 en H2): todos desde `exe+0xB6302A`.** Es `RemoveActorAck::vf2`, el manejador
  del 2516 en el **cliente**, en el hilo principal y a 0–1 celdas del anfitrión. El punto O mezcla el cliente y el
  servidor: falta saber quién manda en el servidor el 2516 con motivo 30.

## 7. Lo que sigue

- Comprobación c estructural (`reentrada.md` §6): implementada. **Necesita Shadow**: una reentrada con el anfitrión en
  el título y otra en el orden normal.
- F4=1, `docked_gimmicks_other=1` y `gimmick_event_dedup=1`: se quedan.
- `tree_bend`, paso 3: pendiente de la decisión sobre B1. **Necesita Shadow**.
- No cambia: `kControlHostReady` se manda con el anfitrión en el título. Retenerlo mandaría la conexión del invitado a
  local; es una decisión de experiencia de uso.
