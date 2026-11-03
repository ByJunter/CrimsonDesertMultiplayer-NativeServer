# Saltos pequeños de la copia durante el movimiento normal (prueba con dos PC, 5 oct 2026)

Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Mod `6318627D`, protocolo 6.
Análisis de solo lectura de los registros y las capturas. No se cambió código.

**Datos:**
- Anfitrión (PC del usuario): `bin64\cdmp.log` y `cdmp_packets_20261005-204149.bin`.
- Invitado (Shadow): `cdmp-shadow\logs\20261005\cdmp.log` y `cdmp_packets_20261005-202422.bin`.
- Todas las horas de este documento están en **hora del anfitrión**. Shadow va una hora por detrás.

**Scripts** en `build/research/saltos/`:
- `extract.py` y `extract_q.py` leen el `.bin` en streaming y escriben un CSV de Move, StartAction y Rotate (Req y Ack).
- `load.py`: utilidades comunes.
- `a1.py` y `a7.py`: reloj del juego frente a QPC, y pausas.
- `a2.py` y `a3.py`: el adelanto de las muestras y una réplica del búfer de interpolación (sin excluir nada).
- `a9.py`: lo mismo solo en juego normal, excluyendo pausas y recargas; añade categorías, StartAction y Rotate.
- `a8.py`: huecos del emisor y espera de inyección.
- `a10.py`: retraso de llegada.

Nota sobre el tiempo de las capturas: `t_us` usa el QPC de cada PC. Para emparejar los dos PC uso
cada `MoveActorReq` del emisor con el `MoveActorAck` inyectado en el receptor que tiene **la misma
posición exacta** (en total se emparejaron 32 658 de 33 016 en el invitado).

---

## 0. Resumen

1. **El retardo de 300 ms del mod no amortigua el jitter.** El mod hace dos cosas con esos 300 ms:
   - retiene cada evento 300 ms desde que llega (`enqueue_at(now + delay_ms)`);
   - suma esos mismos 300 ms a su marca de tiempo (`ts_shift = offset + delay_ms`).

   Las dos cosas se anulan. Cuando el paquete entra en el cliente, lo que queda de margen es solo
   el retardo de interpolación del propio juego (200 ms) **menos lo que el paquete llegó tarde**.
   Lo medido en juego normal:
   - el adelanto de la muestra al inyectarla tiene una mediana de **−97 ms** en el invitado y **−105 ms** en el anfitrión;
   - el margen típico que queda es de unos 100 ms.

   Cuando un paquete llega más de ~185 ms tarde respecto al más rápido, el cliente se queda sin
   muestras. Como el juego **no extrapola**, la copia se para y después da un salto hacia delante.
   Ese es el "tirón" pequeño.
2. **El retraso viene de dos fuentes:**
   - **La red de Steam.** Por encima del paquete más rápido, el retraso tiene p50 de unos 56 ms,
     p90 de 105 ms y p99 de 184 ms (invitado). En el anfitrión es p99 de 233 ms.
   - **La inyección.** El mod solo puede inyectar cuando llega un paquete S>C real al hook de
     `OnRecvPacket`, y esos paquetes llegan a ráfagas:
     - la espera media es de 28 a 32 ms;
     - el 10 % del tiempo estamos dentro de huecos de 96 ms o más (129 ms en el anfitrión);
     - los Move se entregan en grupos de 3 a 12.
3. **El efecto medido** en juego normal (réplica exacta del búfer del cliente, sin pausas ni
   recargas):
   - en el invitado, la copia del anfitrión se queda sin muestras el **2,4 %** del tiempo que se mueve: unas 21 paradas de ≥ 33 ms por minuto;
   - en el anfitrión, la copia del invitado, el **8,1 %**.

   A caballo (8 a 13 m/s), una parada de 33 a 100 ms es un salto de **0,3 a 1,3 m**. Andando es de 5 a 30 cm.
4. **Por qué se nota más en el invitado** aunque las cifras sean parecidas o peores en el
   anfitrión (§2.7):
   - el anfitrión **montó mucho más** a caballo (17 290 Move derivados del caballo en el invitado, frente a 6 768) y se movió más rápido;
   - tuvo **más pausas de reloj** (12, algunas cortas de 0,6 a 2 s);
   - el invitado sufrió **5 recargas por desmontar** con saltos de 1 a 7 m;
   - lo que ve el invitado se mira a través del streaming de Shadow, a 50–79 fps.

   El mecanismo es el mismo en las dos direcciones.
5. **Lo que no es la causa:**
   - **Las líneas "physics basis not a rotation"** son de cuando se va **a caballo**
     (21:29–21:35), no nadando. El juego sigue enviando `MoveActorReq` a 67 Hz, así que lo que se
     envía no cambia; solo afecta a los Move sintetizados (que ahí no hacen falta) y al
     `PlayerState`.
   - **Las posiciones de StartAction** coinciden con la trayectoria de los Move (p90 < 0,15 m).
   - **Los gap-fill no son más en el invitado.** El registro entero tiene 14 en el invitado y 13 en
     el anfitrión (no 3), y la mayoría son escaladas cortas y recargas.
6. **Arreglo principal (§4, P1):** separar la retención (`hold`, unos 100 ms o programada por
   marca de tiempo) del desplazamiento de la marca de tiempo (D = 300 ms). Así el margen pasa de
   unos 185 ms a unos 485 ms, **sin añadir latencia**. Hace falta además:
   - recortar los Move a unos 40 Hz (P2), para no superar las 30 muestras del búfer del cliente;
   - una guardia de marcas de tiempo crecientes (P3);
   - no rellenar huecos a través de una recarga (P4).

---

## 1. Por qué los 300 ms de "playout" no dan margen

Repaso de `movimiento-remoto-cliente.md` §2:
- el cliente pinta la copia en `T = reloj_juego − 200 ms`, interpolando un búfer de muestras ordenadas por `ts`;
- no extrapola: si `T` pasa la última muestra, el actor se queda quieto (S6);
- cuando llega una muestra nueva, salta a donde "debería" estar (S2).

Lo que hace el mod (`coop.cpp:378`, `remote_puppet.cpp:1130` y `:1292`):

```
ts_reb  = ts_emisor + offset + 300                 // offset = min(reloj_local_llegada − ts) en 4 s
entrega = llegada + 300 + w                        // w = espera hasta el siguiente S>C real
adelanto al inyectar = ts_reb − reloj_local(entrega) = −(J + w)
          (J = retraso de ese paquete sobre el más rápido de los últimos 4 s)
margen frente al punto de pintado = 200 − J − w
```

Así que los 300 ms solo **retrasan** la copia (unos 500 ms detrás del jugador real) y no absorben
nada. La copia se queda sin muestras cuando `J + w > 200 − Δts`, que es unos 185 ms a 68 Hz.

Comprobación directa (`a2.py`): el adelanto medido de los Move inyectados tiene una mediana de
−81 ms (invitado) y −93 ms (anfitrión), con toda la sesión. Si la retención amortiguara, sería de
unos +200 ms.

Lo mismo vale para la retención de las acciones del caballo (`ride_horse` y `horse_event`).

---

## 2. Medidas

### 2.1 Cadencia del emisor (`a8.py`, `a5.py`)

| | Anfitrión → invitado | Invitado → anfitrión |
|---|---|---|
| `MoveActorReq` enviados | 34 491 | 23 944 |
| Intervalo p50 / p90 / p99 | 14,4 / 19,8 / 114 ms (unos **68 Hz**) | 15,9 / 20,5 / 130 ms |
| Huecos ≥ 150 ms **moviéndose** | 16, todos remates de escalada ("up", con 2–4 StartAction) | 11, igual |
| Huecos ≥ 150 ms quieto | 222 (128 s) | 180 (111 s) |
| Move sintetizados o gap-fill vistos por el receptor | 358 | 414 |
| Pares consecutivos con la **misma ts** | 294 (casi todos montando) | 155 |

No hay huecos de envío al nadar, planear o correr: el juego manda `MoveActorReq` a cada frame. Los
únicos huecos en movimiento son los remates de escalada (165–490 ms, y 1,9–3 s en los más altos).

### 2.2 Retraso de llegada sobre el más rápido (red + inyección), juego normal (`a10.py`)

| Receptor | p50 | p90 | p95 | p99 | p99,9 | > 150 ms | > 200 ms |
|---|---|---|---|---|---|---|---|
| Invitado (copia del anfitrión) | 56 | 105 | 126 | 184 | 283 | 2,4 % | 0,65 % |
| Anfitrión (copia del invitado) | 60 | 117 | 146 | 233 | 1254 | 4,7 % | 1,7 % |

RTT de la sesión: unos 56 ms de media, con un máximo de 95 ms. No hubo pérdidas apreciables.

### 2.3 Espera de inyección: solo en S>C reales (`a8.py`)

| | S>C reales/s | Espera media | 10 % del tiempo en huecos ≥ | 5 % ≥ | Move por ráfaga p50/p90/p99 |
|---|---|---|---|---|---|
| Invitado | 83 | 27,5 ms | 96 ms | 144 ms | 3 / 5 / 11 |
| Anfitrión | 76 | 31,7 ms | 129 ms | 186 ms | 3 / 6 / 12 |

`troc_sniffer.cpp:on_recv_packet` vacía la cola solo cuando llega un paquete real. Esta espera se
suma a J.

### 2.4 Paradas y saltos en la réplica del búfer, juego normal (`a9.py`)

La réplica es exacta: reglas de `setInterpolatePositionData` y `update…`, con un retardo de 200 ms,
30 muestras como máximo y reinicio a los 1000 ms. Usa el reloj de juego real de cada PC, obtenido
de sus propios Req (`ts − t`, constante entre pausas). Se excluyen las pausas de reloj de los dos
PC (−3 s a +15 s) y las recargas de la copia (−3 s a +6 s).

| Copia vista en | Categoría | Moviéndose | Sin muestras | Paradas ≥ 33 ms/min | ≥ 100 ms/min | Adelanto p50 / p5 / p1 |
|---|---|---|---|---|---|---|
| Invitado | a caballo | 1,4 min | 1,4 % | 12,9 | 0 | −108 / −168 / −207 ms |
| Invitado | andando (< 3 m/s) | 1,2 min | 5,1 % | 40,8 | 4,9 | −97 / −191 / −265 |
| Invitado | rápido (> 6 m/s, caída o planeo) | 0,7 min | 0,2–1,7 % | 3,5–13 | 0 | −97 / −160 / −200 |
| Invitado | **total** | 4,1 min | **2,4 %** | 21 | 1,5 | |
| Anfitrión | andando | 1,5 min | 8,6 % | 50 | 10 | −105 / −222 / −297 |
| Anfitrión | a caballo | 0,4 min | 8,6 % | 65 | 13 | −111 / −225 / −305 |
| Anfitrión | **total** | 3,4 min | **8,1 %** | 51 | 10 | |

Sin excluir nada (`a3.py`), los saltos visibles (paso de un frame mayor que el ideal + 0,15 m y
mayor que 2,5 veces el ideal) son:
- **invitado:** 36 eventos; 24 de más de 0,3 m y 5 de más de 1 m. Los mayores son las recargas
  por desmontar: 7,1 m a las 21:34:53, 4,9 m a las 21:31:37 y 1,1 m a las 21:49:55 y a las
  21:32:39. Después vienen paradas yendo rápido o en caída (21:54:12–29, 0,4–0,75 m) y corriendo
  (21:46:31–34, 0,4–0,65 m).
- **anfitrión:** 98 eventos.

### 2.5 Otras fuentes revisadas

- **Posición de StartAction frente a la trayectoria de los Move en su `ts`:** p50 de 0,00–0,03 m
  y p90 por debajo de 0,15 m en todas las categorías. No producen saltos.

  Una primera medida daba 5–19 m al ir a caballo. Era un artefacto: tras una pausa, los rangos de
  `ts` rebasada de épocas distintas se solapan (ver §2.6).
- **RotateActorAck:** aplica la rotación **al instante** (`SetRotation` de tipo 5), por delante
  de la posición interpolada.
  - En juego normal: en el invitado 27 de 235 giran la cara más de 20° respecto a la que se
    muestra, y 2 más de 45°. En el anfitrión, 111 de 312 y 50.
  - Puede verse como un tic de orientación de un frame. Es una hipótesis, de prioridad baja.
- **ts iguales consecutivas** (294 y 155) y **Move reales después de un sintetizado con `ts`
  menor:** el cliente descarta la segunda muestra (unas 60 por lado). El efecto es muy pequeño.
- **"physics basis not a rotation" en el anfitrión:** 204 líneas, alternando con "in world" cada
  60–100 ms.
  - Ocurre entre 21:29 y 21:35 y en 21:32–21:33, siempre **a caballo**: el `parent` del Move es el
    caballo `0xB0100A20`.
  - Entre 21:31:26 y 21:32:50 el anfitrión envió 5 123 Move, unos 67 por segundo, y los únicos
    huecos > 100 ms son una escalada.
  - **No afecta a lo que se envía.** Afecta a `capture_synth` (que ahí no hace falta), al
    `local_pos` (tiene 500 ms de gracia) y al `PlayerState`, que se usa como posición de recarga.
  - La tolerancia de `math::is_orthonormal` (0,02) probablemente es demasiado estricta para la
    base del jinete.
- **Gap-fill:**
  - **invitado:** 14 en total. 5 son de 1,5 a 3 s "down" justo después de "spawn queued", es
    decir, recargas por desmontar (§3, C4). Los otros 9 son remates de escalada de 0,17 a 0,5 s.
  - **anfitrión:** 13 en total, 6 por recarga y 7 por escalada. Los "3" del anfitrión eran solo
    la primera parte del registro.

### 2.6 Pausas de reloj (para coordinar con el agente de menús; `a7.py`)

| PC | Pausas de reloj | Más ejemplos |
|---|---|---|
| Anfitrión | 12: entre 0,6 y 32 s | Pausas cortas a las 21:38:53, 21:46:00, 21:46:23 y 21:51:45 (0,6–2 s) |
| Invitado | 11 | Además, 4 tirones de 116–196 ms (Shadow) |

Después de cada pausa, `ClockMapper` salta (39 y 37 saltos de más de 250 ms en los informes cada
5 s). Mientras tanto:
- las muestras llegan ya pasadas, lo que provoca un salto;
- o llegan con la `ts` en el futuro: la copia anda en el sitio o se pierden muestras por desborde.

También aparecen **rangos de `ts` rebasada que se solapan** entre épocas. Es el problema de
"camina en el sitio tras el menú", que estudia otro agente. Lo dejo fuera, salvo que en el
invitado las pausas **del anfitrión** también se ven como saltos de su copia.

Además, el `ClockMapper` se desplaza **1 ms por muestra**, es decir, unos 68 ms/s. Mientras
converge (|objetivo − offset| tiene una media de unos 20 ms, y en el 10 % de los informes es de más
de 50 ms), la copia anda un **±7 %** más rápida o más lenta. Es un efecto de goma, no un salto.

### 2.7 ¿Por qué se ve peor en el invitado?

Con las mismas reglas, la copia del invitado en el anfitrión se queda sin muestras más a menudo.
La diferencia que se percibe sale de:
1. **El tipo de movimiento del anfitrión.**
   - Montó mucho más: 17 290 Move de su caballo en el invitado, frente a 6 768.
   - Corrió, cayó y planeó más rápido.
   - La misma parada de 50 ms es de unos 0,5 m a caballo y de unos 0,1 m andando.
2. **Más pausas de reloj del anfitrión** (cortas y largas) y **5 recargas por desmontar** en el
   invitado, que dejan los saltos más grandes (§3, C4).
3. **El invitado se mira por streaming** (Shadow, a 50–79 fps), que añade su propio judder. No se
   puede medir desde aquí.

---

## 3. Causas, por orden de impacto en el movimiento normal

| # | Causa | Evidencia | Dónde |
|---|---|---|---|
| **C1** | La retención y el desplazamiento de `ts` son el mismo número. El margen real es solo los 200 ms del cliente, ya gastado a medias (adelanto p50 de unos −100 ms). | §1, §2.4 | `coop.cpp:378`; `remote_puppet.cpp` `puppet_event` (`now + delay_ms` y `g_ts_shift`), `ride_horse`, `horse_event`, `fill_gap_before` |
| **C2** | Inyección solo a lomos de los S>C reales: espera media de unos 30 ms, colas de 100–190 ms y entrega en ráfagas. | §2.3 | `troc_sniffer.cpp:on_recv_packet` |
| **C3** | Jitter de Steam: p99 de 184–233 ms sobre el más rápido. El `offset` usa el **mínimo** de 4 s, así que cualquier paquete más lento cuenta como retraso. | §2.2 | `clock_mapper.h` |
| **C4** | Gap-fill **a través de una recarga**: `g_last_move` sobrevive a `puppet_remove` y al nuevo spawn. Rellena 1,5–3 s con muestras "down" que ya vencieron, y la copia recién cargada salta de 1 a 7 m. | §2.4 y log del invitado 20:31:37, 20:32:39, 20:34:53, 20:49:55, 20:52:10 (hora Shadow) | `remote_puppet.cpp:fill_gap_before` / `puppet_remove` |
| **C5** | El `ClockMapper` se desplaza por muestra: ±7 % de velocidad mientras converge, y no hay cota de `ts` creciente por actor. | §2.5, §2.6 | `clock_mapper.h`, `puppet_event` |
| C6 | `ts` iguales del emisor, o un Move real con `ts` menor que uno sintetizado: se descartan muestras. | §2.5 | `local_capture.cpp:capture_synth` y `puppet_event` |
| C7 (hipótesis) | `RotateActorAck` fija la orientación al instante, por delante de la posición. | §2.5 | `rotate_ack` |

---

## 4. Propuestas de arreglo

### P1. Separar la retención (H) del desplazamiento de marca de tiempo (D): la principal

Objetivo: que el margen frente al punto de pintado pase de `200 − J − w` a `D + 200 − max(H, J) − w`
**sin cambiar la latencia** (la copia sigue unos `D + 200 ≈ 500 ms` por detrás).

**Opción recomendada: entrega programada por marca de tiempo.**
- En `CoopLink::on_event`, para eventos con `ts`:
  - `objetivo_local = ts + offset + H`;
  - `hold = clamp(objetivo_local − capture_game_clock_ms(), 0, D)`;
  - llamar a `puppet_event(ev, offset + D, hold)`.
- Un paquete que llega tarde se inyecta enseguida y no se queda retenido 300 ms más.
- Valores: `H ≈ 100 ms` y `D = coop.delay_ms` (300).

**Variante sencilla:** `puppet_event(ev, offset + D, H)` con H fijo en 100. El margen pasa a
`D + 200 − H − J − w` (unos 385 ms).

**Cosas que dependen hoy de la retención de 300 ms y hay que mantener:**
1. **Re-path de StartAction en el remate de escalada** (`fill_gap_before` recorre la cola). Con
   H = 100, las StartAction del congelado ya se habrían entregado.

   Retención condicional: mientras el emisor está congelado (`now − g_last_move_ms ≥
   kMoveSilenceMs`), StartAction y Rotate usan `hold = D`. Así siguen en la cola cuando llega el
   Move que reanuda. Las StartAction posteriores a ese Move ya las corrige `g_recent_gap`.
2. **WallUp** tiene que llegar justo antes de su StartAction (`kLead = 40`). Debe usar la retención
   vigente (la última aplicada a una StartAction o Move, guardada en una variable del hilo de
   eventos) menos 40, no `delay_ms − 40`.
3. **Docking, Equip y Relay** (sin `ts`): pueden seguir con `hold = D`. Llegar después de su acción
   no es un problema; llegar antes sí, y con D ≥ la retención de la acción no ocurre.
4. **Gap-fill**: los `due` de las muestras sintetizadas (`g_last_move_ms + k·50 + delay`) deben
   usar la misma retención. Si ya vencieron, se entregan en orden antes del Move que reanuda, que
   es lo correcto.
5. **Caballo** (`ride_horse` y `horse_event`): la misma retención que el Move del jinete del que
   sale, para que interpolen juntos.
6. **Configuración:** añadir `[coop] hold_ms=100` y mantener `delay_ms=300` como D.

### P2. Recortar los Move a unos 40 Hz en el receptor, o subir el tope del búfer del cliente

Con P1, al inyectar quedan unos `D − H + 200 = 400 ms` de muestras futuras. A 68 Hz son unas 27, y
las StartAction comparten búfer. Eso roza el tope de 30 (`exe+0x6CB9598`, build 2976).

Si se supera, el cliente tira la muestra más antigua. Si todas son futuras, `T ≤ ts[0]` y la copia
se para (S4).

- **Recomendado, en `puppet_event`, caso Move del jugador:** descartar un Move si `ts −
  ts_del_último_Move_encolado < 22 ms`.
  - No descartar nunca un Move cuyo `parent` (`+0x8C`) cambie, ni el primero tras un hueco.
  - Las StartAction no se descartan.
  - `ride_horse` se deriva solo de los Move que se conservan.
  - El documento del cliente ya dice que 25–40 Hz bastan, porque la velocidad se deriva de la
    interpolación.
- **Alternativa** (escribe en la memoria del juego; copia de saves antes): subir
  `GlobalVariableInt exe+0x6CB9598` (valor en `obj+0x38`, build 2976) de 30 a 64. Solo lo usan
  los actores en modo 2, es decir, nuestras copias.

### P3. Guardia de `ts` estrictamente creciente por actor (Move + StartAction + gap-fill)

En `puppet_event`, tras rebasar la `ts`: `if (ts ≤ g_last_injected_ts[key]) ts = g_last_injected_ts[key] + 1`.

- Hay una variable por clave de copia (jugador y caballo).
- Así se evitan los descartes por `ts` iguales del emisor (C6) y por la convergencia del offset.
- Al hacerlo, desplazar también `g_last_move_ts` del relleno.
- **No** aplicarla cuando el offset salta por una pausa (más de 250 ms). Eso lo resuelve el agente
  de menús. Por eso conviene coordinar el cambio.

### P4. No rellenar huecos a través de una recarga, ni con muestras ya vencidas

En `puppet_remove` y `puppet_forget`, invalidar la referencia del relleno, por ejemplo con
`g_last_move_ts = 0` y `g_gap_ref_valid = false`.

**No vaciar `g_last_move`:** `puppet_enemy_move` y `puppet_spawn_enemy` lo usan como plantilla.

En `fill_gap_before`:
- salir si la referencia no es válida;
- salir si `g_last_move_ms` es anterior al último `puppet_spawn`;
- saltarse las muestras cuyo `due` ya pasó de largo (`due + 200 < now`), porque se aplicarían de
  golpe.

Esto quita los 5 saltos de 1 a 7 m tras desmontar en el invitado y 6 en el anfitrión. No toca el
problema de la ropa al desmontar, que estudia otro agente.

### P5. ClockMapper: corrección por tiempo, no por muestra (opcional: D adaptativo)

En `clock_mapper.h`:
- limitar la corrección a unos 20 ms por segundo (2 %), en vez de 1 ms por muestra (7 % a 68 Hz);
- añadir tests en `tests/tests.cpp`.

**Opcional, D adaptativo:** la ventana ya guarda las muestras de 4 s.
- `J95 = p95(sample) − min(sample)`;
- `D = clamp(J95 + 60, 150, 400)`, suavizado;
- D solo baja despacio, para no hacer retroceder las `ts`.

Con P1 y D = 300 fijo, el margen (unos 485 ms) ya cubre el p99,9 medido (283 ms en el invitado), así
que esto es secundario. Serviría para **bajar** la latencia cuando la red es buena.

### P6 (baja prioridad)

- **`is_orthonormal`:** subir la tolerancia (por ejemplo a 0,05) o reortonormalizar la base antes
  de rechazarla (`player_reader.cpp:88`). Así el `PlayerState` y la posición de recarga no se
  quedan viejos al ir a caballo.
- **`RotateActorAck`:** probar si el tic de orientación se ve. Si se ve, no inyectar Rotate
  mientras lleguen Move, porque el Move ya trae el quaternion en `+0x10`.
- **No extrapolar desde el mod:** una muestra predicha con `ts` mayor hace que el cliente descarte
  las reales posteriores (`ts ≤ última`), y los fallos de predicción serían saltos. Es mejor dar
  margen (P1).

---

## 5. Plan de pruebas

1. **Validación sin tocar código** (escribe en la memoria; copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` antes):
   - subir `GlobalVariableInt exe+0x6CB9548` (retardo de interpolación del cliente, build 2976)
     de 200 a 400 solo en el invitado;
   - repetir el recorrido de la prueba.

   Si los tirones desaparecen, C1 queda confirmado. A cambio, la copia va 200 ms más tarde.
2. **Prueba unitaria y local:** P3, P4 y P5 tienen lógica pura. Hay que añadir casos a `tests/tests.cpp`:
   - **ClockMapper:** la corrección por tiempo, y que no se salta con jitter de 100 ms;
   - **guardia de `ts`:** se cumple con Move y StartAction intercalados y con `ts` iguales;
   - **`fill_gap_before`:** no hace nada tras un `puppet_remove` seguido de un `puppet_spawn`.
3. **Loopback con `cdmp_peer`.** El mod tiene `dev_mirror_delay_ms`, pero el jitter de Steam no
   se reproduce en loopback. Usar `cdmp_peer` con un retardo aleatorio, si se le añade un modo
   jitter (p50 de 60 ms y cola de 250 ms), para medir con `a9.py` antes y después.
4. **Prueba con los dos PC (Shadow como invitado), con P1 a P4 activos.** Un recorrido de unos 10
   minutos en el anfitrión, sin abrir menús:
   - andar;
   - correr;
   - montar a galope en línea recta y en círculos;
   - 3 remates de escalada;
   - una escalada libre;
   - planear o caer;
   - nadar;
   - desmontar 2 veces.

   Marcar Ctrl+F12 en los dos PC al empezar cada tramo.
5. **Métricas** (los scripts sirven tal cual):
   - `a9.py`:
     - menos del 0,3 % del tiempo sin muestras en todas las categorías;
     - 0 paradas de 100 ms o más;
     - adelanto p1 > −100 ms.
   - `a3.py`: 0 saltos de más de 0,3 m fuera de pausas y recargas.
   - Tras desmontar: ningún "filled a ≥1000 ms gap" y ningún salto de más de 1 m.
   - Escaladas: el log sigue mostrando "re-pathed N queued StartAction positions" con N > 0
     (comprueba que la retención condicional funciona) y no hay vuelta al pie del muro.
   - Revisar que no hay overflow (P2): con la réplica, el número de muestras futuras al inyectar
     debe ser de 25 o menos.
6. **Visual:** capturas con `CopyFromScreen` en el invitado durante el galope (antes y después).
   Mejor aún, una grabación corta por el lado de Shadow, aunque pasa por el streaming.

---

## 6. Límites del análisis

- **El reloj de juego de cada PC** se reconstruye con sus propios Req. Cuando el jugador local
  está quieto mucho rato, no hay referencia. Por eso solo se analizaron con categoría unos 4
  minutos de movimiento por lado, después de excluir pausas y recargas.
- **La réplica asume 60 fps.** En el invitado son 50–79.
- **La categoría** (a caballo, andando…) se deduce de la velocidad y del `parent` de los Move. No
  se distingue nadar de andar.
- **El tic de `RotateActorAck`** no está verificado en el juego.
