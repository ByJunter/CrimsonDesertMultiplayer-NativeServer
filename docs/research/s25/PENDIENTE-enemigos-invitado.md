# PENDIENTE (muy importante, siguiente después de los tenderos): los enemigos del invitado se comportan como civiles

Anotado el 15-10 a pedido del usuario. **No se ha investigado ni tocado nada todavía.**

## Lo que vio el usuario (dos copias en un PC, el invitado lejos del anfitrión)

Build de la rama `opcion-d-2inst` (`57d340f`/`13aceab`, `cdmp.asi` `ef23154f…`), ini del anfitrión
`docs/research/ini/cdmp.2inst-host-carros-1.ini` (fases A, E, B=1, C=1 con `user2_road_carts=1`, D=1).

Los **enemigos alrededor del invitado** (bandidos y posiblemente cualquier otro tipo) se comportan como NPC normales:
- no atacan al invitado y no salen en el minimapa como enemigos;
- si el invitado los ataca, **nunca mueren**;
- reaccionan un momento como enemigos (como si un segundo se les activara que es su enemigo), pero enseguida dejan de
  atacar y les cuesta muchísimo atacar;
- al matar a varios, al invitado **le sale que ha cometido un delito**: el juego los trata como civiles.

## Datos guardados (no borrar)

`C:\Emulador CD\backups\p8-enemigos-invitado-20261015\`:
- `host.log`, `guest.log`: los dos logs de la sesión.
- `cdmp_packets.host_20261015-123008.bin/.log`, `cdmp_packets.guest_20261015-123024.bin/.log`: las capturas.
- Marcas Ctrl+F12, en los dos logs: **12:33:41, 12:34:13, 12:40:17, 12:40:32, 12:41:17** (las del final, durante lo de
  los enemigos).

## Pistas sin verificar, para orientar la investigación (no son conclusiones)

- Desde S33-S34 el mod da el control de NPC al cliente del invitado:
  - U-e: NPC de horario;
  - fase B: sus familias;
  - fase C: viajeros de camino, ahora también jinetes y carros.
  
  Si los bandidos salen de esas mismas rutas (stages o AutoSpawnRoadReq 2550), su IA, su facción y su relación de
  hostilidad pueden depender del controlador o de datos de cliente que no se rehacen al cambiar de dueño.
- La revisión de la fase C ya avisó: «viajeros hostiles: no hay ninguno en la muestra; habría que auditar
  `combat_follow` antes de ampliar». Ver `docs/research/s25/s34-investigaciones/3-viajeros.md` (riesgos) y la revisión
  de la fase C.
- Comparar con una sesión sin las fases B y C (claves a 0) y con una en la que el invitado esté cerca del anfitrión.
- Mirar `combat_follow`, la puerta de publicación (restore 0), `remote_interp`, y el estado de facción y hostilidad que
  envía el servidor en el 2005 de esos actores.

## Confirmado con Shadow (kit 38, 15-10)

Shadow vio lo mismo entre dos PC reales. Logs en `C:\Emulador CD\backups\p8-sesion38\` (2 marcas en cada log). En la
misma sesión el usuario vio otra vez **puertas que no se abren** (lo apuntado el 15-10: con el anfitrión solo sí abrían;
el fallo es intermitente). Investigar las puertas junto con esto o justo después.
