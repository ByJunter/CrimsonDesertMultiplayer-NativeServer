# Plan de lo que queda por resolver (4 de octubre de 2026)

Sale de los 10 informes `docs/research/pendiente-*.md` y del trabajo de la pieza 4
(`pieza4-*.md`). Build del juego: 1.0.0.2976.

Cada paso se cierra con una prueba en el juego (un PC con `cdmp_peer`, salvo que se diga otra
cosa), un commit y, si toca, la actualización del informe. No se empieza el siguiente sin cerrar
el anterior, salvo los pasos marcados como independientes.

---

## Lista de lo que queda por resolver

| # | Problema | Informe | Tamaño |
|---|---|---|---|
| 1 | **Prueba pendiente:** golpes del invitado a las copias (daño, muerte, botín, tambaleo) | `pieza4-tambaleo.md` | En prueba |
| 2 | Las muertes por truco (`KillCheatReq`, estado "fatal") no dan puntos de habilidad ni cuentan para desafíos | `pendiente-recompensas-progreso.md` | Pequeño |
| 3 | 18 comprobaciones de claves dejan de funcionar a los 30-60 min (claves `0xB02…`); `0xA0100001` fijo en el código; `net_id` sin límite | `pendiente-rango-claves.md` | Pequeño |
| 4 | Daño de flechas mal calculado en las dos direcciones (accessor de tipo 1 mal leído) | `pendiente-proyectiles.md` | Pequeño |
| 5 | El modo automático copia animales; jinetes y torres sin un rechazo claro | `pendiente-jinetes-animales.md` | Pequeño |
| 6 | Los enemigos pueden seguir atacando a una copia muerta; `PlayerHit` se envía con la copia muerta | `pendiente-reaccion-muerte-invitado.md`, `pendiente-aggro-y-respuesta-golpes.md` | Pequeño |
| 7 | El golpe del invitado se aplica en el anfitrión ~300 ms antes de que se vea su espadazo | `pendiente-aggro-y-respuesta-golpes.md` | Pequeño |
| 8 | **Sin control de versión:** el kit del amigo es viejo; con Ctrl+F7 se satura el enlace; si no coinciden las versiones, se buscan para siempre | `pendiente-robustez.md` | Medio |
| 9 | En el PC del invitado las copias de enemigos apuntan a la copia del anfitrión (`0xA010F001` sin traducir) | `pendiente-proyectiles.md`, `pendiente-aggro-y-respuesta-golpes.md` | Pequeño (cambia el protocolo) |
| 10 | Canal fiable: un mensaje puede quedar incompleto para siempre; mensajes de enemigos desordenados; tamaños sin validar | `pendiente-robustez.md` | Medio |
| 11 | Relevo de enemigos sin ciclo de vida: reconexión, pantallas de carga, parpadeos de "fuera del mundo", paquetes viejos tras un menú | `pendiente-robustez.md` | Medio |
| 12 | El log se borra al arrancar; faltan datos para depurar con el amigo; un `.ini` de anfitrión y otro de invitado | `pendiente-robustez.md` | Pequeño |
| 13 | El invitado no reacciona a los golpes de los enemigos del anfitrión y nunca muere por ellos | `pendiente-reaccion-muerte-invitado.md` | Medio |
| 14 | Reparto de enemigos entre los dos jugadores (hoy casi todos van al anfitrión) | `pendiente-aggro-y-respuesta-golpes.md` | Medio |
| 15 | Respuesta visual del golpe del invitado: efecto de impacto en el anfitrión, números de daño en el invitado, menos retraso | `pendiente-aggro-y-respuesta-golpes.md` | Medio |
| 16 | El invitado ve sus propios enemigos además de las copias (duplicados) | `pendiente-3g-suprimir-enemigos-invitado.md` | Grande |
| 17 | El invitado no recibe botín, experiencia ni progreso de misiones | `pendiente-recompensas-progreso.md` | Medio (depende del 16) |
| 18 | Jefes: el remate por truco puede romper misiones, se puede herir en invulnerabilidad, no hay barra para el invitado | `pendiente-jefes.md` | Medio → grande |
| 19 | Las flechas del invitado no se ven en el PC del anfitrión | `pendiente-proyectiles.md` | Medio |
| 20 | Jinetes y monturas sin copiar | `pendiente-jinetes-animales.md` | Medio → grande |
| 21 | Arqueros y mosqueteros en torres sin copiar | `pendiente-jinetes-animales.md` | Grande |
| 22 | La copia se hunde o flota en las pruebas de un PC | `pendiente-copia-se-hunde.md` | Ninguno: es un efecto del desplazamiento lateral; solo documentarlo |

---

## Plan, paso a paso

### Fase 0 — cerrar la pieza 4 (donde lo dejamos)

**Paso 0.1 — Prueba pendiente** (#1). **Hecho (4 oct):** daño, muerte y botín funcionan; el tambaleo funciona tras quitar el `AttackTargetAck` de los golpes que tambalean (`pieza4-tambaleo.md` §8-9). El usuario pega a las copias de enemigos (Ctrl+F7,
`cdmp.ini.prueba-pieza4b`). Revisar en el log y la captura:
- `VaryStatCheatReq`/`KillCheatReq` con daño acorde a la tabla;
- el lote ATA + AAR + buff y el `StartActionReq` de tambaleo del enemigo en ≤30 ms;
- muerte y botín.

Si no hay tambaleo, aplicar las pruebas R1c/R1d de `pieza4-tambaleo.md` §5.2.

**Paso 0.2 — Remate con experiencia** (#2). En `guest_hits.cpp`, un golpe mortal pasa a ser:
`VaryStatCheatReq` hasta 1 HP → `AttackByCollisionReq(E, jugador, 0xF2EE2678)` (como X2, que dio
motivo 8, experiencia y botín). `KillCheatReq` queda como reserva a los ~500 ms. Nunca un
`VaryStatCheatReq` que deje 0 HP.
- **Prueba:** `AddSubLevelExperienceAck(521, 15)` tras la muerte (prueba T1 del informe).

### Fase 1 — arreglos pequeños, todos se prueban con un PC

**Hecha el 4 de octubre** (commits `b34f559`, `41dfa34`, `8628d5e`). Prueba en vivo: caballo bien; 8 copias, todas
de bandidos en combate y ninguna de animales; los originales reaccionan a los golpes de arma y de arco
a sus copias (flecha `E912512D/769` = 17 HP); al morir, los enemigos dejaron de atacar.
- Consecuencia de la 1.3: Ctrl+F7 solo copia a los enemigos que ya están en combate (cuando te
  detectan o les pegas). Para que el invitado vea a los enemigos **antes** del combate, la fase 5
  (zona alrededor del anfitrión, enemigos hostiles por su facción) los enviará a todos.

Independientes entre sí; un commit por paso.

- **1.1 Claves de cualquier vuelta** (#3): sustituir las 18 comprobaciones `>> 20 == 0xB01/0xA01/0x901` por
  comprobaciones de clase (bits 28-29); quitar `0xA0100001` fijo; acotar `net_id`; un contador
  compartido de ids de instancia; y un "disyuntor" que apague las copias si una clave del juego cae
  en nuestro rango.
- **1.2 Daño de flechas** (#4): leer el accessor de tipo 1 de 24 B (clave del proyectil) en
  `player_hits.cpp` y `guest_hits.cpp`. Semillas: 28/17 (jugador), 24-30 (arqueros), 6 (mosquetes).
- **1.3 Selección de enemigos** (#5): copiar solo los que mandan `ChangeBattleStateReq` ≠ 0 o
  tienen un golpe real. Rechazar jinetes y torres, con la categoría en el log.
- **1.4 Copia muerta** (#6): `copy_state_normal()` comprueba `Status+0x273`; no enviar `PlayerHit`
  ni forzar objetivos con la copia muerta.
- **1.5 Sincronía del golpe** (#7): aplicar el `GuestHit` con el mismo retraso de reproducción que
  la copia, para que el enemigo reaccione cuando se ve el espadazo.
- **1.6 Documentar el hundimiento** (#22): una nota en `PRUEBAS-COOP.md` y en los `.ini` de prueba
  (modo "detrás de ti": `offset_m=0.01`, `delay_ms` 1500-3000).

### Fase 2 — protocolo 6 y robustez (antes de jugar con el amigo)

**Hecha el 4 de octubre** (commits `b2f2249`, `bc78dd8`, `873c534`): 2.1-2.4 y 2.6. Prueba en vivo con
`cdmp_peer --loss 5 --jitter 40 --delay 60`: todo bien, 0 avisos ni errores en el log; reconexión (peer
parado 10 s) recuperada sola en 1,5 s; alejarse del combate quita las copias y vuelven al reanudarlo.
Arreglado: un parpadeo de la copia justo tras reconectar. La 2.5 (ampliar `GuestHit`)
pasa a la fase 3, donde se usa. Falta la prueba en el juego con `cdmp_peer --loss 5 --jitter 40 --delay 60`
(P0 n.º 10 de `pendiente-robustez.md`) y reescribir el `LEEME` para el zip nuevo (fase 4).

Todos los cambios de formato juntos, para subir `kVersion` una sola vez.

- **2.1 Versión** (#8): `kVersion` 6, `kModBuild` = hash de git, aviso en el log y en pantalla si no
  coinciden; confirmar los tipos de mensaje desconocidos y limitar los reenvíos.
- **2.2 Traducción de claves entre PCs** (#9): intercambiar `0xA010F001` ↔ jugador local en todos los
  campos de los mensajes de enemigos.
- **2.3 Canal fiable y orden** (#10): ventana de envío, expulsar mensajes parciales por antigüedad,
  entrega en orden de los mensajes de enemigos, validar tamaños antes de inyectar.
- **2.4 Ciclo de vida** (#11): reiniciar el relevo al cambiar de sesión o de compañero; esperar a
  estar en el mundo antes de crear copias (con antirrebote); descartar los paquetes viejos tras un
  menú; vaciar la cola C>S al cambiar de mundo.
- **2.5 Ampliar `GuestHit`**: clave de flecha, punto de impacto, id de golpe y reloj (lo necesitan
  la 1.2 y la fase 3).
- **2.6 Diagnóstico** (#12): logs rotados, volcado de la configuración al arrancar, una tecla de
  "marca" para sincronizar, estadísticas cada 5 s; `.ini` de anfitrión y de invitado separados.
- **Prueba:** todo con un PC (eco), forzando reconexiones y pantallas de carga.

### Fase 3 — el combate se siente bien en los dos lados

**Estado (4 de octubre):**
- 3.1 reacción del invitado: **hecha y probada** (commits `8044f68`, `c820aae`, siguiente): 3 de 3 golpes cuerpo a
  cuerpo a la copia hicieron tambalearse al jugador; las flechas, arregladas después de la prueba.
- 3.2 muerte del invitado: **programada** (`f3e1284`): 1 HP + colisión propia, muerte normal (D1/D2 probados);
  falta verla con golpes reales a la copia (los enemigos atacan poco a la copia en un PC).
- 3.3 reparto: el usuario lo ve bien en la práctica; queda lo hecho en la fase 1 (copia muerta) y la
  ausencia (la copia se retira). Sin más cambios por ahora.
- 3.4 números de daño: **descartado** (el juego no los muestra). Efecto de impacto (`+0x81 = 0`): aplazado.
- Extra: recarga automática de una copia atascada; la clave de usuario solo en campos alineados.

- **3.1 Reacción del invitado** (#13): el anfitrión aprende el lote ATA + AAR + buff de los golpes
  enemigos a su propio jugador y lo manda en `PlayerHit`; el invitado lo inyecta sobre sí mismo
  desde la copia del enemigo (ids `0x8000E000+n`) y sigue perdiendo la vida como hoy.
- **3.2 Muerte del invitado** (#13). **Decisión pendiente:** `KillCheatReq` sobre sí mismo funciona,
  pero la telemetría de la muerte diría "DevCheat" (`pendiente-recompensas-progreso.md`). Antes de
  implementarlo, probar con un PC la vía por colisión (`AttackByCollisionReq` sobre el propio
  jugador) y elegir.
- **3.3 Reparto de enemigos** (#14): elegibilidad en `IsTargetable` (vivo, presente, ≤40 m); cambios
  forzados con `CurrentTargetChangedAck` cuando el invitado golpea; filtro en `SetCurrentTarget`
  para repartir; límite de ~6 enemigos sobre el invitado; histéresis.
- **3.4 Respuesta visual** (#15): `+0x81 = 0` en el ATA inyectado (efecto de impacto en el
  anfitrión); `OnPostBuffDamagedAck` en el invitado (números de daño); un retraso más corto para
  las copias de enemigos.

### Fase 4 — primera prueba seria con el amigo (dos PCs)

- Zip nuevo con los `.ini` separados y las opciones nuevas apagadas por defecto, salvo lo ya probado.
- Seguir el protocolo de `pendiente-robustez.md`: qué hace cada uno, qué se graba y qué se envía.
- Arreglar lo que salga antes de seguir.

### Fase 5 — un solo mundo de enemigos

- **5.1 Quitar los enemigos propios del invitado** (#16): primero el modo "solo registrar", luego el
  modo laboratorio con zona manual (pruebas L0-L4), luego la zona de 150 m alrededor del
  anfitrión. Solo el anfitrión envía enemigos; el daño espejo, apagado.
- **5.2 Recompensas para el invitado** (#17): remate del gemelo oculto en su servidor (1 HP +
  colisión), una vez por enemigo y solo si el invitado participó.

### Fase 6 — más tipos de enemigos y contenido

- **6.1 Jefes, modo seguro** (#18): lista de jefes desde los datos del juego; sin remates por truco,
  sin tambaleo inyectado, nunca por debajo de 1 HP, daño del invitado a la mitad, sin golpes
  durante cinemáticas. Antes, grabar un combate real contra un jefe. Después, una barra de jefe en
  el overlay del invitado.
- **6.2 Flechas del invitado visibles en el anfitrión** (#19), detrás de un interruptor.
- **6.3 Jinetes** (#20): primero jinete y montura por separado (visual) y luego enlazados (experimental).
- **6.4 Torres** (#21): más adelante.

### Más adelante / opcional

- Que la muerte por el remate del invitado (colisión) parezca un espadazo: probar otras claves de
  colisión, el tambaleo antes del remate, o la dirección y la fuerza del impacto.

- Jefes en los dos mundos a la vez (la única forma de que los dos avancen la misión del jefe).
- Ajustar la copia al suelo con un rayo del juego, si con dos PCs hiciera falta.
- Investigar si el servidor interno del juego podría aceptar a un segundo jugador (modo MMO).
