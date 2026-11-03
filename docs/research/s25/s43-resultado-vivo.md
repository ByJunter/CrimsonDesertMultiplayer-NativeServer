# S43 en vivo: puertas arregladas (15-10, dos copias en un PC, binario `e36ad277…`, `d884648`)

Logs, capturas e inis:
- `C:\Emulador CD\backups\p8-2inst-s43-probe\` (paso probe, 19:54–20:04);
- `C:\Emulador CD\backups\p8-2inst-s43-activo\` (paso 1, 20:05–20:10).

En las dos copias y en los dos pasos: 0 ERR, 0 cierres.

## Paso probe: la causa, confirmada en vivo

- Instalación: `SceneLevelShare ON (scene_level_share=probe)`, con A y B enganchados y el sujeto de ExecuteTroc enganchado.
- Hash: `[hash] primera coincidencia nombre -> hash del 2834`, y 134 coincidencias frente a 0 distintos en el modo 1.
- 19:57:41: `[would ready] nivel 'sector_-42_-15' hash 0xDE7B52AA ... actor 0xA0100066 ... opcode 2598`. Es el SceneLevel de la
  puerta de la taberna de Hernand.
- 19:57–20:01: DoorTrace da `recurso-no-listo` para los dos sujetos (invitado y anfitrión), unos 60 intentos.
- 20:01:48: el invitado se va. Su 2834 lleva al borrado nativo porque la entrada no estaba lista. Después, la carga del
  anfitrión registra la entrada como dueño: **DoorTrace `exito`** para el anfitrión a las 20:02 y a las 20:03.
- El usuario lo confirma: si el invitado carga la zona, las puertas no abren hasta que hace un viaje rápido, que lo
  descarga todo.

## Paso 1 (`scene_level_share=1`): la puerta abre

- 20:07:26.305: `[ready for user 2] nivel 'sector_-42_-15' hash 0xDE7B52AA` (el invitado cargó primero).
- 20:07:28: DoorTrace `exito`, sujeto `0xA0100066`.
- En vivo: las puertas funcionaron bien.
- Resumen de las 20:10:44:
  - A: `ready for user 2 109`, lecturas fallidas 0;
  - B: 2834 = 134 (`kept 6`), `entrada-no-lista 15`; `0x2836110` (carga del dueño) = 219, de ellos `would keep 173` (solo
    observa).
- **Hueco que queda (P2, conocido):**
  - la carga nativa del anfitrión con DL≠0 borra los niveles listos que el invitado sigue teniendo cargados: 173 en esta
    sesión;
  - el usuario no notó ningún efecto en la puerta, pero puede romper otras puertas o gimmicks del invitado hasta que
    recargue;
  - siguiente paso: demostrar si conservar en `0x2836110` es seguro, o hacer que el invitado vuelva a cargar esas zonas
    (Resync).
- Motivo 42: no hubo aborto de U-e en ninguna de las dos sesiones.

## Prueba de la pose (20:39–20:50, `C:\Emulador CD\backups\p8-2inst-s43-pose\`)

Inis: `cdmp.2inst-s43-activo-host.ini` + `cdmp.2inst-s43-pose-guest.ini` (`guest_seq_route_deny=8CD771B1,9586FFA4`).

- **Pose arreglada:**
  - la escena `cd_seq_boss_rematch_matt` (`0x9586FFA4`) llegó 15 veces al invitado (20:43–20:44, secuenciadores `0x301`–`0x308`…);
  - las 15 salieron `DENIED ... REFUSED: kind set to 0, nothing created in M`;
  - el usuario no vio la pose congelada (solo comentó lo de la puerta);
  - 0 ERR en las dos copias.
- **Puerta, error nuevo y distinto** (no lo causa la exclusión, que solo afecta a esa escena):
  - el invitado da `creacion-fallida-6CF7028` en todas sus 2928: 12 entre 20:42:06 y 20:43:09, y 6 entre 20:49:23 y 20:49:50;
  - el recurso sí estaba listo (`[ready for user 2] sector_-42_-15` a las 20:42:04);
  - el anfitrión obtiene `exito` a las 20:43:12 y a las 20:49:51, y desde ahí el invitado ve la puerta abierta;
  - en `0x282DC80` es la rama de `FUN_1428308e0(mgr, actor)` tras leer la posición del actor, y `DAT_146cf7028`;
  - en `s43-activo` el invitado sí obtuvo `exito` (20:07:28).
  
  **Pendiente** (aplazado): qué comprueba `0x28308E0` sobre el actor del invitado, y por qué
  falla en esta sesión y no en la anterior.
