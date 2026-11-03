# P8: validación de S1/S2/S3

Implementación: [p8-implementacion.md](p8-implementacion.md). Leyenda de resultados:
- **OK**: comprobado en la prueba indicada.
- **Pendiente**: falta Shadow, o no se ha probado.
- **Contención**: se evita el síntoma, pero no está arreglado.

## 1. Fuera del juego

- `.\scripts\build.ps1` (RelWithDebInfo): **17/17 pruebas OK** (`6b0ae94`).
- Alcance: lógica pura y "costuras" falsas, sin el motor. Cubren la tabla de retenciones con
  `record()`+`sweep()` (liberación dirigida, periódica, vencimiento, duplicados, tabla llena, salida del
  usuario 2), el análisis de 2005 con registros de la sesión 4, los anillos y conjuntos llenos, la
  puerta de captura de cierres y la lógica de las sondas S1/S2.

## 2. En el juego, un PC (anfitrión + `cdmp_peer`)

| Caso | Prueba | Resultado |
|---|---|---|
| Instalación de los 10 puntos del anfitrión | `cdmp.p8-local.ini` | **OK**, `REFUSED: none` |
| Anfitrión sin invitado | arranque y título | **OK** |
| Entrada en el campo privado y después en el compartido | peer, sesión 4 de guion | **OK**, rama de dueño y luego de no dueño [V] |
| Aviso del anfitrión abierto | peer entra y sale con el aviso abierto | **OK**, sin cuarentena |
| Aviso cerrado: ráfaga de NPC con restauración 0 | peer soak 300 s | **OK**: 730 retenidos, 730 liberados (media 214 ms, máximo 375 ms), 0 vencimientos, 0 sin entrada |
| NPC con dueño (compañero) | D-53 | **OK**, sin cambios |
| Salida del invitado y limpieza | soak → logout | **OK**, tabla vacía, logout completo, 0 ERR |
| Reconexión con claves reutilizadas | épocas 1→2 de esta tarde | **OK** |
| Coste en el hilo del servidor | describe | **OK**: 10 µs de media y 276 µs de máximo por pasada |
| Primera pasada (antes de los arreglos) | — | **Fallo** B1 visto en vivo y corregido |

## 3. En el juego, un PC (lado invitado, sin red, modo observe)

| Caso | Resultado |
|---|---|
| Instalación de todas las piezas del invitado | **OK**: S3 (6 puntos), GuestDiag, P8Probe invitado (12 puntos), sin rechazos |
| Línea base nativa de S2 (un jugador) | **OK**: cada viaje recibe 2322 y `M+0x231` pasa a 2; `0xA97D60` puede bloquear un instante (entrada `+0x28C == 0`); `0x11` sale en 0–0,486 s |
| Falsos positivos de la captura de cierres | **OK**: 0 registros y 0 dumps en 4 min |
| Guardas sin actores remotos | **OK**: 0 saltos |

## 4. Pendiente de Shadow (sesión P8 completa)

| Problema | Qué hay que observar | Criterio |
|---|---|---|
| S1 | Para un NPC con `seq_bodyanim_action_xxx`: en el anfitrión, instalación en `CC+0x70`, búsqueda encontrada en el gráfico de 2 entradas y binding; en el invitado, lo mismo para la misma clave | Primera divergencia identificada (no basta el error numérico) |
| S2 | En el invitado, línea `P8Probe [loading 0x779E77]` del segundo viaje: `M+0x231` y el primer bloqueo de `0xA97D60`; `GuestDiag loading transition` | Saber qué predicado bloquea y si sale de `0x11`; correlación con el aviso del anfitrión |
| S3 | En el invitado: historial por actor de los NPC liberados por `observer_hold` (2005 con restauración 1 → restauración OK → acción instalada) y de los anunciados por vencimiento | Ningún actor llega a `0x232D5C0` con acción nula; `guard skips` cerca de 0 y ninguno atascado |
| Estabilidad | Que no haya cierre; si lo hay, `cdmp_crash_*.dmp` más el historial | Cierre localizado por actor y capa |
| Regresiones | Identidad F002, reloj, transporte de Steam, salida y reconexión | Igual que en las sesiones anteriores |

**Contenciones activas en esta prueba:** `guest_update_guard` y `guest_layer_guard`.
