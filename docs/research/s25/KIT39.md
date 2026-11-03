# Kit 39 (16-10): todo lo validado desde el kit 38

Punto de respaldo y de avance para probar con Shadow. Build del juego 1.0.0.2976.

## Base

- **Rama:** `opcion-d-2inst`, merge `90d1805`.
- **Compilación:** desde cero (`build\` borrado), 81/81 tests.
- **Kit 38:** `cdmp.asi` `ef23154f…`, código de `effae47`. Los inis del kit 38 eran exactamente `cdmp.2inst-host-carros-1.ini` y
  `cdmp.2inst-guest.ini` con la red de Steam.
- **Inis del kit 39:** se generan a partir de los inis **validados en vivo con dos copias**, con la misma red que el kit 38:
  - `cdmp.p8-s39-host.ini` = `cdmp.2inst-s43-activo-host.ini` + red del kit 38;
  - `cdmp.p8-s39-shadow.ini` = `cdmp.2inst-s45-activo2-guest.ini` + red del kit 38.
  
  Diferencias con el kit 38, comprobadas clave a clave:

  | Ini | Clave | Kit 38 | Kit 39 |
  |---|---|---|---|
  | Anfitrión | `scene_level_share` | — | `1` |
  | Anfitrión | `door_trace` | — | `1` (solo mide) |
  | Anfitrión | `enemy_damage_probe` | — | `1` (solo mide) |
  | Anfitrión | `npc_family_vehicles` | — | `1` (se degrada a probe, solo mide) |
  | Invitado | `guest_seq_foreign_player` | — | `1` |
  | Invitado | `guest_seq_foreign_present` | — | `1` |
  | Invitado | `guest_seq_foreign_timeline` | — | `1` |
  | Invitado | `guest_seq_foreign_probe` | — | `1` (sondas, solo leen) |
  | Invitado | `guest_seq_foreign_present_probe` | — | `1` (sondas, solo leen) |
  | Invitado | `door_trace` | — | `1` (solo mide) |
  | Invitado | `enemy_damage_probe` | — | `0` |
  | Invitado | `npc_family_vehicles` | — | `0` |

- **Plan B del invitado:** `cdmp.ini.planB` es el ini del kit 38. Con el binario nuevo, las claves nuevas quedan a 0 y todo
  se comporta como en el kit 38, salvo los cambios de código que no dependen de clave (ver abajo).

## Cambios de código desde el kit 38 (por sesión)

| Sesión | Qué | Clave | Validado |
|---|---|---|---|
| S42 | Sondas de solo lectura: familias con vehículo, inmunidad al daño, creación de puertas (`DoorTrace`) | `npc_family_vehicles`, `enemy_damage_probe`, `door_trace` | Sí: pasos probe 15-10, sin efectos |
| S43 | **Puertas.** El SceneLevel cargado por el invitado también queda «listo» (A, `0x283044D`), y no se borra un SceneLevel que el otro usuario sigue teniendo (B, `0x2829E60`, solo el 2834) | `scene_level_share` | Sí: la puerta de la taberna abre aunque el invitado cargue primero (15-10) |
| S43 | **Guarda de motivo 42 de U-e:** solo aborta ante repeticiones del mismo hueco (bucle S31), no ante bajas nativas sueltas | — (código) | Sí: sin abortos falsos desde el 15-10 |
| S44 | **Pose congelada / puntos rojos del anfitrión en el invitado** (recuerdos de jefe y escenas personales): se neutraliza el enlace del dueño en el nodo de jugador. Actúa desde la primera aparición; el mapa aprendido solo guarda excepciones | `guest_seq_foreign_player` | Sí: 15-10, Matthias |
| S44 | **Guarda de layout de `Config`:** un objeto compilado contra un `config.h` viejo hace que el mod no instale nada y lo diga, en vez de leer el ini desplazado | — (código) | Sí: compilación limpia + `cdmp_host` |
| S45 | **R-A.** En el invitado, el procesador de cada etapa ajena usa una tabla del mod en la que las funciones que encienden presentación (interfaz, paneles, entrada, cámara de etapa, carga, recompensa, minijuego…) no hacen nada, como en el servidor. Las de apagado quedan nativas | `guest_seq_foreign_present` | Sí: 16-10, pasos 2 y 3 (CUT 34/55, 0 fallos) |
| S45 | **R-B.** En el invitado, los inicios de cámara de la línea de tiempo de una etapa ajena (`CameraSelector`, `PlayerCameraPreset`, ranura 99) no se ejecutan; los fines quedan nativos | `guest_seq_foreign_timeline` | Sí: 16-10, paso 3 (12 saltos; ni tienda vacía ni cárcel) |
| S45 | Sondas S-A…S-D (solo lectura) | `guest_seq_foreign_present_probe` | Sí |

**No validado en vivo** (pasa sus tests y su revisión; si fallara, lo hace por el lado seguro: la cámara se vería como
antes):
- `9eae04a`: R-B con M, relectura del id y comprobación de la clase del evento.

## Riesgos y qué mirar con Shadow

- Es la primera vez **entre dos PC**. Hasta ahora todo se validó con dos copias en un PC.
- **Abortar el invitado (pasar a `cdmp.ini.planB`) si:**
  - pantalla negra, bandas o cámara pegadas;
  - el juego del invitado se cierra;
  - la copia del anfitrión o los NPC se quedan quietos.
- **En el log de Shadow:**
  - `GuestSeqPresent R-A summary` con `CUT`, sin `WRITE FAILED` ni `RESTORED`;
  - `R-B ... SKIPPED`, sin `NOT skipped` inesperados;
  - `GuestSeqForeign summary` sin `NOT player node`.
- **En el log del anfitrión:** `SceneLevelShare` y `DoorTrace` con éxito en la taberna.

## Fallos conocidos (no reportar)

- Enemigos con carro, que se comportan como civiles.
- Zona muerta de 4–9 celdas, sin enemigos hasta que llega el anfitrión.
- Tenderos.
- Objetos destruidos que no vuelven.
- Mascota en el hombro.

Ver `PENDIENTES.md`.
