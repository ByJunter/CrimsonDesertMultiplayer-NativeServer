# P8 · Sesiones 13 y 14: los viajeros que el invitado no ve

11 de octubre de 2026, rama `opcion-d`. Build del juego **1.0.0.2976**, direcciones en **RVA**. Solo lectura: no se
lanzó el juego, no se tocó código y no hubo commit. `.xtls` no se leyó (no hizo falta).

**[V]** = comprobado en capturas, logs o instrucciones (capstone sobre el exe). **[E]** = inferencia con apoyo.
**[H]** = hipótesis sin medir.

Datos:
- Invitado (S14): `backups\p8-sesion14-shadow\cdmp_20261011-011822.log` y `cdmp_packets_20261011-011835.bin`.
- Anfitrión (S14): `backups\p8-sesion14-host\cdmp_20261011-021131.log` y `cdmp_packets_20261011-021141.bin`.
- S13: `backups\p8-sesion13-{shadow,host}` (mismo análisis, §2.3).
- Control de un jugador: `backups\capturas\cdmp_packets_20261008-223758.bin` (prueba a4, sin usuario 2).
- **Relojes [V]:** con `t` = segundos desde el primer registro de cada captura, `t_invitado = t_anfitrión − 425,0 s`
  (50 865 movimientos idénticos `2556`→`2697`; p10–p90 entre −425,02 y −424,96). Todas las horas de este informe
  son de la captura del invitado. En S13, `t_invitado = t_anfitrión − 147,84 s` (`p8-s13-npc-familias.md`).
- Recorrido S14: plaza de Hernand hasta t ≈ 360; bajan al sur (`z ≈ −3970`) entre 380 y 500; salen hacia
  `(−10809, −4003)` entre 520 y 660. Los dos jugadores, a menos de ~50 u el uno del otro.

## 0. Resumen

1. **Cómo nacen los viajeros [V].**
   - Los pide **el cliente** con `AutoSpawnRoadReq` 2550: una lista de puntos de la carretera alrededor de su
     jugador, por clase.
   - El servidor crea el actor **exactamente en el punto pedido** 10–20 ms después (más el túnel, si lo pidió el
     invitado). **No hay Ack ni Nak** para la 2550.
   - Los **dos** clientes piden y el servidor atiende a los **dos**:
     - en S14, 40 peatones de carretera los pidió el invitado y 23 el anfitrión;
     - pájaros: 579 el invitado y 664 el anfitrión.
   - El 2005 de cada uno lleva la clave del personaje que lo pidió (`0xA0100A4C` o `0xA0100001`). El controlador es
     siempre el anfitrión (`0x90100000`).
   - **El Nak no decide nada de los viajeros.** Los 2953 `SpawnOnDemandGimmickNak` responden a otra cosa: la
     `SceneLevelOnDemandGimmickSpawnReq` 2928, para objetos de nivel (tipo 7). El 65 % de los Nak del invitado son
     de objetos que **ya había creado el anfitrión** (§1.3).
2. **¿Se anuncian al invitado? Sí, todos [V].**
   - Cada viajero visto por el anfitrión le llega al invitado: 2005, 2697, 2991, 2723, 2404 y 2516, 1:1.
   - **La única diferencia es el orden.** El invitado recibe **las piezas antes que el NPC que las lleva**. Las piezas
     son actores tipo 7 «Game Mechanic» enganchados a su padre: cestas, la carga del buey, la carga del carro,
     la cuerda del que guía.
   - Cifras del orden invertido (pieza antes que padre):

     | Sesión | Invitado | Anfitrión | Padres |
     |---|---:|---:|---|
     | S14 | **66** | 0 | casi todos viajeros |
     | S13 | **114** | 0 | casi todos viajeros |

   - Ejemplos de padres: Stranger de carretera, Loaded Bull, Fisherman, Peddler, los carros (Narrow Grain /
     Material Wagon, Small Cargo Wagon, Desert Freight Wagon) y el Priden Trade Goods Packhorse. Es el retrato de
     «llevan una cesta o guían un animal de carga».
   - **Causa [V]:** `observer_hold` (S3) del anfitrión.
     - Retiene el 2005 del **NPC** hasta su primera acción, unos 0,1–0,5 s, y lo reanuncia.
     - **No** retiene sus piezas: no tienen CC (`NoCc`, `child_hold.cpp:125`). Las piezas salen al instante.
   - Lo que hace el cliente nativo con un hijo cuyo padre aún no existe es el camino «sin padre» de la jerarquía
     (`0x15ECA10` → `0x439AB0` → `0x439BB8`). Que eso deje **invisible al NPC** cuando llega después es
     **[E] fuerte por correlación**, sin medir en el cliente (§2.4).
3. **Arreglo mínimo [E]:** que `observer_hold` trate las piezas como dependientes de su padre: mismas retenciones,
   y salen **justo detrás** del 2005 del padre.
   - El sitio más barato es `observer_hold_acks_route` (`take_frame` del anfitrión), que ya hace eso mismo con las
     Ack de cada NPC.
   - Ni desviar la 2550 ni suprimirla lo arregla: hay padres invertidos pedidos por el anfitrión (§3).

## 1. Cómo nacen los viajeros (pregunta 1)

### 1.1 `AutoSpawnRoadReq` 2550 [V]

Formato de la carga útil, tras la cabecera de 5 B:

```
u8 04 | f32 1.0 | n × { u32 id_clase | u16 cupo | u8 a | u8 tramo | f32 x | f32 y | f32 z | f32 giro }   (24 B)
```

Clases en S14, por lo que crea el servidor **[E]** (los nombres de clase del enum del juego son `AutoSpawn_Bird`,
`_SidWalk` y `_Wagon`, en `0x15BA43C…`):

| `id_clase` | Qué aparece en ese punto | Peticiones del invitado | Peticiones del anfitrión |
|---|---|---:|---:|
| `0x1F5` | bandadas de pájaros (`cupo` 25) | 1176 entradas | 1724 |
| `0x3FD` | peatones: Stranger + Loaded Bull, Meat Porter, Guide, Mushroom Specialist, Fisherman… (`cupo` 3–10, `tramo` 2–18) | 574 | 455 |
| `0x7D1` | jinete o guía con caballo/animal de carga (Herspia / Priden / Packhorse + Wandering Freesword) | 117 | 23 |
| `0xFA1` | carro: Wagon + Wagon Driver + Wagon Horse | 93 | 8 |

**Correlación exacta:** cada grupo de viajeros aparece en la posición de una entrada de la 2550 (±2 u). Esa
2550 se envió 0,01–0,03 s antes (la del anfitrión) o 0,1–0,2 s antes (la del invitado, por el túnel).

| t | Grupo | Lo pidió |
|---:|---|---|
| 80,3 | Stranger + Loaded Bull `0xB0100C1E/1F` | invitado (`0x3FD`, tramo 17) |
| 239,3 | Narrow Material Wagon `0xB010151D–1F` | anfitrión (`0xFA1`) |
| 377,6 | Narrow Grain Wagon `0xB0101DB7–B9` | anfitrión (`0xFA1`) |
| 439,3 | Stranger + Loaded Bull, Guide + 2 Meat Porter, 2 Mushroom Specialist (`0xB0101F34–46`) | invitado (`0x3FD`, tramos 4, 11 y 17) |
| 541,3 | Small Cargo Wagon `0xB010231A–1C` | invitado (`0xFA1`) |
| 632,6 | Stranger + Loaded Bull `0xB010259B–9E` | anfitrión (`0x3FD`, tramo 18) |

El 2005 de un actor de carretera lleva un bloque de origen tras el nombre:

```
u32 personaje que pidió | u8 | u8 1 | u8 1 | u8 tramo | u32 id_clase | 3 × i32 0x7FFFFFFF | u8 1
```

Por ejemplo, el Loaded Bull `0xB0101F38` lleva `4C 0A 10 A0 00 01 01 11 FD 03 00 00` (lo pidió el invitado, tramo
17, clase `0x3FD`). El controlador (`+18`) es `0x90100000` en todos. Recuento de los 2005 de S14 con ese bloque
(t 60–670):

| Lo pidió | `0x1F5` (tipo 6) | `0x3FD` (tipo 3 / 6) | `0x7D1` (tipo 3 / 5) | `0xFA1` (tipo 3 / 5) |
|---|---:|---:|---:|---:|
| invitado `0xA0100A4C` | 579 | 40 / 4 | 7 / 7 | 1 / 2 |
| anfitrión `0xA0100001` | 664 | 23 / 2 | 5 / 5 | 3 / 6 |

Consecuencias:
- **No hay Ack ni Nak de la 2550**: no existe en la tabla de mensajes. No hay ninguna comprobación de dueño de campo
  visible en la red: el servidor atiende las peticiones del usuario 2 [V].
- **[E]** La densidad de viajeros y pájaros es aproximadamente el doble de la nativa, porque piden los dos
  clientes. Es un efecto aparte. No es la causa de los invisibles, porque todo lo creado llega a los dos.
- Servidor:
  - manejador de la Req `0x29C4930`;
  - núcleo `0x2C41E50`, que es un `jmp` a `.xtls 0x10880460` (virtualizado);
  - la creación en el campo es `0x27E8C90`, llamada desde `.xtls 0x10880515` [V].
  - Que el controlador salga de `[field+0x180]` viene de `p8-sistemas-juego.md` §2.3 [E]. Lo confirma el
    `+18 = 0x90100000` de todos los 2005 [V].

### 1.2 ¿Lo pide el cliente «cuando necesita viajeros alrededor del jugador»? [V]

Sí.
- Cada cliente manda la 2550 con los puntos de **su** jugador.
- El invitado manda 1536 en S14 (1887 en S8). El anfitrión, 2641.
- El servidor crea con la clave del que pidió en el 2005 y con el anfitrión de controlador. El cliente del
  anfitrión les da la IA, como a todos los NPC: `2223`, `2556` y `2734` del anfitrión para `0xB0101F37/38`, etc.

### 1.3 Lo que sí son los Nak asimétricos (2953 y 2829) [V]

**2953 `SpawnOnDemandGimmickNak`** responde a **2928 `SceneLevelOnDemandGimmickSpawnReq`**:
- Formatos:
  - la Req: `04 | u32 n | n × { u32 hash | 16 B id de objeto de escena }`;
  - el Nak: `04 | actor que pidió | 16 B id | u32`, **sin código de error**.
- Son objetos de nivel (árboles, cajas…), no personajes. El 2145 `UpdateOnDemandGimmickAck` (ToNear) dice quién lo
  creó o lo quitó: `04 | actor | u8 estado | id`.
- En S14, t > 60:

  | Cliente | 2928 | 2953 | Nak con el objeto ya creado por el anfitrión | … ya creado por el invitado | quitado | sin estado conocido |
  |---|---:|---:|---:|---:|---:|---:|
  | Invitado | 7580 | 6504 | **4199 (65 %)** | 308 | 480 | 1517 |
  | Anfitrión | 2977 | 2073 | 787 | 36 | 95 | 1155 |

- **[E]** El servidor niega porque el objeto ya existe. El cliente del invitado no lo cuenta como suyo y lo vuelve
  a pedir. Es inocuo para los viajeros.
- Efecto aparte **[V]**: el invitado también **quita** objetos de nivel con 2221 (1631). Por ejemplo, en t 300,20,
  `2145 … 4C0A10A0/0` y el anfitrión lo vuelve a pedir en 300,30. Es un ping-pong de objetos de escena entre los dos
  clientes; queda como pendiente aparte.

**2829 `UpdateOnDemandEquipDockingNak`** = `eErrNoFailedToSpawnDockingActor` (`0x95D8E760`; hash `hashlittle`
comprobado con `eErrNoInvalidFieldInfoKey`):
- Responde a 2249 `OnDemandEquipDockingGimmickSpawnReq`.
- Camino en el servidor: `0x2970790` → `0x28AD170`. Por cada ranura pedida, si la ranura ya tiene actor la salta.
  Si no creó ninguno, escribe el error en `0x28AD627` y lo manda con `0x2969FE0`.
- **Es simétrico**: los dos clientes repiten la 2249 de las cestas de los Mushroom Specialist / Herbalist / Stranger
  (ranura `0x0E`) cientos de veces.
- La primera vez **sí** se crea: `0xB0101F47` llega 17 ms después de la 2249 del anfitrión para `0xB0101F45`. Las
  siguientes reciben el Nak porque la ranura ya está ocupada.
- En la captura de un jugador (a4) cada NPC manda una sola 2249 y no hay ningún 2829.
- **[H]** El bucle es otra anomalía de la opción D (los dos PC), pendiente aparte. No explica la asimetría.

## 2. ¿Llegan al invitado y por qué no se ven? (pregunta 2)

### 2.1 Llegan todos [V]

- Ningún 2005 que el anfitrión recibe después de t 60 falta en el invitado (0 claves).
- Para los viajeros nacidos con el invitado dentro (`trav.py`, 107 claves) coinciden 2697, 2991, 2723, 2404 y
  2516: la diferencia es de 0 o 1 paquete, salvo al final de la captura del invitado. Cada baja 2516 tiene su par en
  el invitado con < 0,3 s de diferencia.

Ejemplos cerca del invitado:

| t | Clave | Distancia al invitado | 2005 G/H | 2697 G/H | 2991 G / 2223 H | 2723 G |
|---:|---|---:|---|---:|---:|---:|
| 439,3 | Loaded Bull `0xB0101F38` | 72 u | 1/1 | 69/69 | 71/72 | 1 |
| 439,3 | Stranger `0xB0101F37` (guía al buey) | 72 u | 1/1 | 79/79 | 89/90 | 1 |
| 541,3 | Wagon Driver `0xB010231B` | 60 u | 1/1 | 99/102 (fin de captura) | 3/4 | 1 |
| 632,6 | Loaded Bull `0xB010259E` | 71 u | 1/1 | 10/34 (fin de captura) | 12/38 | 1 |

En el invitado también se crean: cada uno tiene su línea `GuestScene [G1 AI block]`, la lectura del bloque de IA al
construirse el actor. Las líneas G1 cubren el 100 % de los tipos 3/4/5/6 nacidos después de t 60.

### 2.2 La asimetría: el invitado recibe las piezas antes que el padre [V]

Grupo de t 439,3 (lo pidió el invitado):

| Anfitrión (t) | Invitado (t) |
|---|---|
| 439,279 Stranger `F34` → Sheep `F35` → pieza `F36` (padre `F34`) | 439,266 **piezas** `F36`, `F39`, `F3A`, `F3B`, `F3E`, `F3F` (sus padres aún no existen) |
| 439,285 Stranger `F37` → Loaded Bull `F38` → piezas `F39`, `F3A` (padre `F38`), `F3B` (padre `F37`) | 439,475 Guide `F42`, Meat Porter `F43/44`, Mushroom Specialist `F45/46` (sin piezas en su 2005) |
| 439,286–287 Stranger `F3C`, `F3D` → piezas `F3E` (padre `F3D`), `F3F` (padre `F3C`) | 439,541 `F3C`, `F3D`, `F34`, `F35` (reanuncio de `observer_hold`) |
| 439,289–290 `F40`, `F41`, `F42`, `F43`, `F44`, `F45`, `F46` | 439,767 **`F37`, `F38`** (0,5 s después de sus piezas) |

- Enlaces en el 2005 de la pieza tipo 7 [V]:
  - con el nombre «Game Mechanic» (13 caracteres), la clave del padre va en el registro `+232` y se repite en `+270`;
  - en general va a `fin_del_nombre + 172`, con el nombre UTF-16 en `+34` y su longitud en caracteres en el `u16`
    de `+30`;
  - en el 2005 del padre, sus piezas van en la lista del final, por ejemplo `F37` en `+5655`, `+6784` y `+7348` →
    `F3B`.
- `+232` y `+270` coinciden en 633 de 814 registros tipo 7 con un `0xB` en `+232`.

Recuento de todas las piezas tipo 7 con padre conocido, padre nacido en t 60–670 (S14):

| Padre | Llega tarde al invitado (retenido) | Pieza antes que padre en el invitado | Casos |
|---|---|---|---:|
| tipo 3 (personas) | sí | **sí** | **56** |
| tipo 3 | sí | no (la pieza se creó después, p. ej. la 2249) | 76 |
| tipo 5 (carros, animales de carga) | sí | **sí** | **10** |
| tipo 5 | sí / no | no | 3 / 66 |
| tipo 6 (ovejas, cabras guiadas) | sí | no (padre e hijo se retienen y salen juntos) | 345 |

Los 66 padres invertidos de S14, por nombre:

| Nombre | Casos |
|---|---:|
| Stranger | 23 (casi todos clase `0x3FD`, de carretera) |
| Wandering Freesword | 10 |
| Loaded Bull | 8 |
| Fisherman | 8 (`0x3FD`) |
| carros `0xFA1` | 9 |
| Peddler | 3 |
| Hernandian Guard | 2 |
| Apothecary | 2 |
| Priden Trade Goods Packhorse | 1 |

En el anfitrión el orden es siempre padre → piezas (0 inversiones). El hueco es de 0,12–0,50 s.

### 2.3 S13 [V]

El mismo análisis sobre S13 (`t_invitado = t_anfitrión − 147,84`) da **114** piezas antes que su padre en el
invitado y **0** en el anfitrión. Padres por nombre:

| Nombre | Casos |
|---|---:|
| Fisherman | 40 |
| Stranger | 33 |
| Loaded Bull | 14 |
| Desert Freight Wagon | 12 |
| Wandering Freesword | 7 |
| Narrow Grain Wagon | 3 |
| Hernandian Guard | 3 |
| Porter | 1 |
| Peddler | 1 |

S11–S14 tienen `observer hold true` y `child hold true` en el anfitrión.

### 2.4 Mecanismo y grado de certeza

- **[V] Por qué se invierte.**
  - `observer_hold` (S3) retiene del usuario 2 todo NPC sin dueño, controlado por el anfitrión y con byte de
    restauración 0. Lo reanuncia con un 2005 nuevo cuando el anfitrión le da su primera acción.
    - Resumen del anfitrión: `NPCs held 2931 | released 2890 … dropped 41`.
    - El reanuncio del grupo `F3C/F3D/F34` va a 439,541, justo tras sus 2223 del anfitrión (439,538).
  - Las piezas tipo 7 no tienen CC y `npc_verdict_with` las deja pasar: `HoldWhy::NoCc`, `child_hold.cpp:125`.
  - El reparto (`child_hold.cpp` ~1866: «a mixed 2005 keeps its parent -> child order») conserva el orden **solo
    dentro de lo que pasa**. Si el padre se retiene, la pieza sale sola antes.
- **[V código, E conducta] Qué hace el cliente.**
  - La pieza busca a su padre por clave en cada actualización de `ClientGimmickActorComponent` (vf+0xC0 `0x8D8940`
    → `0x15ECA10`).
  - Sin padre, `0x15ECA10` pasa `rdx = 0` y `ClientHierarchyActorComponent` vf150 `0x439AB0` toma `0x439BB8`, que
    usa el propio actor (`p8-crash-esc-1831598.md` §5.2).
  - Ese estado no existe en el juego nativo: allí la pieza siempre llega después que su padre, en el mismo 2005.
- **[E fuerte, por correlación] Que esto sea lo que deja invisible al viajero.**
  - Es la **única** diferencia medible entre los dos PC para estos NPC: entrega, acciones, movimientos, IA, 2723 y
    bajas son 1:1.
  - El conjunto afectado coincide con lo que describió el usuario: llevan una pieza (cesta, carga, cuerda) o guían
    un animal con carga.
  - El resto de retenidos sin piezas, que también llegan 0,2 s tarde, **no** cambia de orden. Por ejemplo, el Guide y
    los Meat Porter `F42–F44`, y los Mushroom Specialist `F45/46`, cuya cesta se pide después con la 2249.
- **[H] Lo que falta medir.** El estado del actor padre en el cliente del invitado:
  - si existe en la tabla de actores;
  - el enlace de jerarquía de la pieza;
  - su visibilidad o render.

  Predicción comprobable: en el grupo de t 439, `F34`, `F37`, `F38`, `F3C` y `F3D` faltan en el invitado, y `F42–F46`
  se ven.

### 2.5 Descartados para esta asimetría [V]

| Candidato | Por qué no |
|---|---|
| Nak de 2550 o comprobación de dueño | No existe Nak. Los viajeros pedidos por el invitado se crean y le llegan |
| 2953 / 2928 | Objetos de nivel, no personajes (§1.3) |
| Bucle 2249 / 2829 (cestas) | Igual en los dos PC (§1.3) |
| Quién lo pidió (`0xA0100A4C` o `0xA0100001`) | Hay padres invertidos de los dos: `0xB010151D`, `1DB7` y `259B/9E` los pidió el anfitrión |
| D-53 `child_hold` (hijos de usuario) | Solo retuvo 2 (`held 2 … released 2`): los viajeros no tienen dueño (`+0xA0 = 0`) |
| `child_settle`, `attach_follow` (sonda), `ride_filter`, `RemoteInterp` | Actúan sobre hijos de usuario o claves `0xA`; ninguna línea toca estas claves |
| 2395 `ChangedAutoSpawnCloseFilterStateAck` | Llega igual a los dos (el del anfitrión; ToNear). El invitado no recibe uno propio en el campo compartido, pero eso afecta a dónde pide, no a lo que ve **[E]** |
| Tipo 4/5 en sí | Los tipo 5 sin piezas (Herspia, Priden Packhorse con jinete) llegan en orden |

## 3. Arreglo mínimo (pregunta 3)

### A. Recomendado [E]: piezas dependientes en `observer_hold_acks` (anfitrión, `take_frame`)

`observer_hold_acks_route` (`src/mod/session2/observer_hold_acks.h`) ya:
- retiene en `take_frame` lo que va a la sesión 2 de una clave que `observer_hold` retiene;
- lo suelta «right after its 2005», con límites;
- lo descarta si se cae la retención.

Ampliación:
1. **Separar las piezas.** En cada 2005 hacia la sesión 2 (carga = secuencia `u16 len | registro`), un registro
   cuya clave padre (`fin_del_nombre + 172`, con `+270` como comprobación) esté retenida
   (`observer_hold_acks_holding(padre)`):
   - se quita de ese 2005, rehaciendo la longitud de la cabecera;
   - se guarda como un 2005 de un solo registro en la cola de la clave del padre;
   - se emite así justo detrás del reanuncio del padre y antes de sus 2723 y 2404.
   Si el 2005 se queda vacío, no se envía.
2. **Alias.** La clave de la pieza pasa a ser alias de la retención del padre. Sus paquetes por clave (`+0`: 2697,
   2642…; además 2356 `GimmickCatcherUpdateAck` y 2516, este con la clave en `+6`) esperan igual.
   - Hoy la 2697 de `F39/F3A` llega antes del padre (27 en el invitado).
   - Si llega un 2516 de la pieza mientras espera, se descarta todo lo suyo.
3. **Caída de la retención** (despawn, fuera de rango, época): se descartan las piezas, como hoy los paquetes por
   clave.
   - **[H]** Si el padre se da de baja pero la pieza sigue viva en el servidor, el invitado no la vería. Alternativa
     prudente: soltarla sola, como ahora.
4. **Límites:** un registro tipo 7 mide ~6,3–6,7 KB y un carro tiene 3 piezas (~20 KB). El tope actual por clave
   es de 32 KB y 32 paquetes. Si se pasa, sale todo en orden al momento (lo de hoy).

Riesgos:
- reescribir un 2005, que hoy solo se encola entero;
- el formato del registro, verificado solo para «Game Mechanic» en 2976;
- un 2005 dentro de un `ScopeSenderAck` 2042: en S14, `delivered 2042 0`. Si aparece, se deja pasar sin tocar.
- No toca el servidor ni el juego del invitado. Va detrás de una clave nueva (p. ej.
  `[session2] observer_hold_pieces=1`), apagada por defecto.

### B. Alternativa en el reparto del servidor (`child_hold`, gancho de NotifyAppear `0x2B000C0`) [H]

En `prepare`/`plan_split`, una clave `NoCc` cuya **jerarquía en el servidor** apunte a un NPC retenido para el mismo
observador:
- se retiene también;
- su liberación va en el mismo `NotifyAppear([observador], [padre, piezas…])`.

Es más limpio (sin reescribir paquetes), pero falta localizar en el actor del servidor el campo del padre que se
serializa en el registro `+232`. **Hecho después:** el campo está en la jerarquía `[[actor+0x68]+0x1B8]+0x1C`; es
`[session2] observer_hold_pieces` (§6).

### C. Alternativa en el invitado [E]

En `guest_deliver`, retener los registros tipo 7 cuya clave padre aún no esté en la tabla de actores del cliente
hasta que llegue el 2005 del padre (tope de ~3 s y entrega nativa después), con sus paquetes por clave.
- Es independiente del anfitrión.
- Pero el invitado no sabe si el padre vendrá, y hay que reordenar también 2697 y 2356. Es más código que A.

### No recomendados

- **Desviar la 2550 del invitado por el contexto del anfitrión, o suprimirla.** No arregla nada: hay padres
  invertidos pedidos por el anfitrión.
  - Suprimirla solo devolvería la densidad nativa (§1.1). Si se quiere, es una clave aparte en la entrada del túnel
    del anfitrión, para las Req 2550 de la sesión 2.
  - **[H]** Riesgo: menos viajeros alrededor del invitado si se separa del anfitrión.
- **Soltar sin retener a los NPC que llevan piezas.** Vuelve el byte de restauración 0 en el observador, el motivo
  de S3 (seis lectores nativos cerraban el juego).

## 4. Prueba discriminante (próxima sesión con Shadow)

1. **Sin código nuevo** (se puede aprovechar cualquier sesión):
   - grabar vídeo o capturas del invitado y del anfitrión en carretera, anotando la hora;
   - después, con `order.py`, cruzar los padres invertidos de esa ventana con los que faltan en el vídeo.

   Predicción: faltan los invertidos (Stranger + Loaded Bull, carros, Fisherman) y se ven el Guide, los Meat
   Porter, los Mushroom Specialist y los jinetes.
2. **Con sonda** (solo lectura, invitado): al entregar un 2005 de un tipo 7 cuya clave padre aún no está en el
   cliente, una línea `child before parent`. Al llegar el padre y cada 5 s:
   - si el padre está en la tabla;
   - `[[pieza+0x68]+0x1B8]+0x1C` (la clave padre de la jerarquía);
   - la posición del padre frente a la de su último 2697.
3. **Con A activada:** 0 inversiones en `order.py` y los mismos grupos visibles en el invitado.

## 5. Método (reproducible)

Scripts en el scratchpad de la sesión (`…\scratchpad\s14v\`), Python `-I`:

| Script | Qué hace |
|---|---|
| `../s13/mvall.py`, `../s13/align.py` | Datos por clave y reloj (de S13) |
| `cnt.py` | Recuento por opcode, dirección y sesión |
| `r2550.py`, `corr.py` | Decodifica la 2550 y la cruza con la posición de cada viajero |
| `rec2005.py`, `refs.py` | Registros 2005 crudos y referencias entre claves (padre/piezas) |
| `order.py`, `order2.py` | Orden pieza/padre en los dos PC y clasificación |
| `req.py` | Quién pidió cada actor de carretera (bloque de origen del 2005) |
| `od.py` | 2928/2953/2145: estado del objeto al recibir el Nak |
| `dock.py`, `dock2.py` | 2249/2829 por actor |
| `err.py` | `hashlittle(nombre, 0xC5EDE)` de los 1502 `eErrNo*` del exe |
| `d.py`, `fn.py`, `callers.py`, `sref.py`, `gref.py` | Desensamblado, límites de función (`.pdata`) y referencias |

## 6. Implementación: `[session2] observer_hold_pieces` (alternativa B, anfitrión)

11 de octubre de 2026, rama `p8-travelers-2` (desde `opcion-d`). Build **1.0.0.2976**. Sin juego: solo tests.

Se hizo la **B** de §3 y no la A: el campo del padre en el servidor sí está localizado, y así no se reescribe
ningún 2005.

### 6.1 El enlace pieza → padre en el servidor [V]

- Componente de jerarquía, el mismo en servidor y cliente (`CommonHierarchyActorComponent`): `[[actor+0x68]+0x1B8]`.
  - Su propio actor está en `+8`. Se comprueba siempre (`== actor`); si no coincide o no se puede leer, la pieza
    queda nativa y se cuenta como **unlinked**.
  - La clave del padre está en `+0x1C`.
- Servidor: `0x279688C mov rax,[rbx+0x68]` ; `0x2796890 mov rbx,[rax+0x1B8]` ; `0x2796897 mov ebx,[rbx+0x1C]` →
  búsqueda por clave (`0x27968B0`).
- Cliente: `0x15ECA10` busca al padre por esa misma `+0x1C` (§2.4).
- Solo cuenta como padre una clave `0xB` distinta de la propia. Sin componente (`[comps+0x1B8] == 0`): sin padre.
- Lecturas solo con SEH (`game::FastReads`), en el hilo del reparto. No se llama a nada del juego.

### 6.2 Qué hace (`child_hold.cpp`, ganchos de `NotifyAppear` y `StartActionReq::vf2`)

Con `observer_hold=1` y `observer_hold_pieces=1`:

1. **Reparto (`prepare_pieces`).** Una clave con veredicto `NoCc` de `observer_hold` (sin dueño, controlador = un
   usuario conocido) cuyo padre P está retenido por `observer_hold`:
   - P en esta misma llamada con `hold && npc`; o, si P no va en la llamada, entradas de P en la tabla de NPC.
   - **Todos** los destinatarios que no son del controlador de P tienen que estar entre los que retienen a P
     (`piece_follows_parent`). Si no: queda nativa y se cuenta como **partial**.
   - Se retiene igual que P: el controlador la recibe ya (en el mismo grupo, en el orden de la llamada) y los demás
     esperan.
   - Su sitio se reserva bajo el candado en el propio reparto (`g_piece_reserved`), como el de los NPC. Tabla de
     256 (pieza, observador); llena → nativa, contada (**table full**). Nunca se retiene sin entrada.
2. **Liberación.** Cuando sale P hacia un observador (restauración 1, control, timeout o «nativo en vez de
   descartado»), sus piezas de ese observador salen **en el mismo `NotifyAppear`**, detrás de todos los NPC
   (`std::stable_sort`: padres primero). Una pieza que ya no existe por clave se descarta.
3. **Descarte.** Si se descarta P (fuera de rango, despawn, época, observador ido), se descartan sus piezas.
   Si se va el usuario 2, también.
4. **Pasada periódica** (cada 100 ms, en un hilo que puede llamar a `NotifyAppear`):
   - pieza cuyo padre ya **no tiene entrada** para ese observador (p. ej. el motor anunció al padre por su cuenta)
     → se anuncia sola, de forma nativa (**orphan**, línea INF);
   - pieza retenida más de 30 s (`kPieceMaxHoldMs`) → nativa (**AT THE MAX WAIT**, línea WRN);
   - otra época, despawn, observador ido o fuera de rango → descartada.
   - El hilo de recepción solo descarta; lo que tenga que salir espera (contado como *deferred*).
5. **`observer_hold_acks`.** Las claves de las piezas retenidas al usuario 2 se registran como las de los NPC: sus
   paquetes por clave esperan a su 2005. Se añadió **2356 `GimmickCatcherUpdateAck`** (clave en `+0`) a los
   opcodes retenidos. Si la pieza se descarta, sus paquetes también. (No se toca 2516.)
6. **`probe`:** misma decisión, pero solo cuenta (**WOULD be withheld**) y escribe líneas; no cambia nada.

Configuración: `observer_hold_pieces = 0 | probe | 1` (por defecto 0). Necesita `observer_hold=1` y
`create_session=1`; si no, se queda apagada con un WRN. Inis del anfitrión (`docs/research/ini/cdmp.p8-s7-host.ini`
y `cdmp.p8-noche-local.ini`): `probe`.

Tests (`tests/optd_s3_tests.cpp`, `test_piece_logic`, `test_hold_pieces`, `test_hold_pieces_acks`): misma llamada y
llamada posterior (también con la pieza antes que el padre en la lista) → `{padre, pieza}` en un 2005; descarte con
el padre; pieza desaparecida antes del reanuncio; huérfana anunciada sola; la pasada del hilo de recepción no la
suelta; `probe` no cambia nada; apagada = como antes; tabla llena → nativa; partial; unlinked; padre no retenido;
salida del usuario 2; con `observer_hold_acks` los 2697/2356 de la pieza esperan y salen tras el 2005
`[padre, pieza]`.

### 6.3 Qué mirar en la próxima sesión

En el resumen `observer hold pieces (...)` del log del anfitrión y en las capturas:

- **`probe`** (primera sesión):
  - `WOULD be withheld` cerca de las **66** de S14 (o las 114 de S13) por cada 600 s en carretera.
  - `UNLINKED 0`. Si sale alto, el enlace `+0x1B8 / +8` no vale para estas piezas: **no** pasar a 1.
  - `partial` bajo (solo con un tercer destinatario o un padre retenido a medias).
- **`1`**:
  - `order.py` sobre las dos capturas: **0 piezas antes que su padre** en el invitado.
  - `held` ≈ el `WOULD` del probe; `released` ≈ `held`; `orphans` pocos; `AT THE MAX WAIT 0`; `WITHOUT an entry 0`.
  - Visual: los Stranger + Loaded Bull, carros, Fisherman y Peddler se ven en el invitado (predicción de §4).

### 6.4 Cuándo abortar (volver a `probe` o `0`)

- Cualquier WRN `... WITHOUT an entry` (una pieza retenida sin entrada: no debería pasar, su sitio se reserva).
- Piezas soltadas de forma nativa en cantidad (`AT THE MAX WAIT` > 0, u `orphans` del orden de `held`): el padre no
  se reanuncia con ellas.
- Un cierre en el invitado cerca de `0x15ECA10` o `0x439AB0` (camino de jerarquía de la pieza).
- `unlinked` alto con la clave a 1 (no se retiene nada, pero indica un enlace mal leído).

### 6.5 Riesgos

- **[E]** El orden dentro del 2005 de la liberación es el de la lista de claves que se pasa a `NotifyAppear`
  (padres y luego piezas). Es lo que muestra el anfitrión nativo (padre → piezas en el mismo 2005), pero no se ha
  comprobado que `0x2B060E0` nunca reordene.
- **[H]** Piezas cuyo controlador no sea un usuario conocido (`CtrlNotUser`) no se siguen: el análisis de S14 dice
  que llegan a `NoCc`, así que deberían estar cubiertas. El `seen` del probe lo confirmará.
- Pieza huérfana tras un anuncio nativo del padre: sale sola en ≤ 100 ms, después del padre (orden correcto).
- Coste: unas pocas lecturas SEH por clave `NoCc` en cada reparto con NPC retenidos, y en la pasada periódica un
  recorrido (piezas × entradas NPC) bajo el candado, acotado por 256 × 1024.

## 7. `[session2] observer_hold_family`: el orden de familias completo (anfitrión, P8 S15/S16)

11 de octubre de 2026, rama `p8-family-order` (desde `opcion-d` `acc48c8`). Build **1.0.0.2976**. Sin juego: solo
tests. Sustituye a `observer_hold_pieces` (§6), que queda como **alias** de la clave.

### 7.1 Por qué §6 no bastaba [V]

`p8-s15-viaje-tunel.md` §8.1: el cliente del invitado destruye todo registro de un 2005 cuyo padre (`+0xAC`, el
final del registro) no tiene todavía (`0x8B7D78`), y el jefe que llega después no forma nunca su grupo de login
(`+5E 8` para siempre: bloquea la recarga en 0x12 y deja invisibles a los viajeros). La entrega nativa no manda
ninguno (captura del anfitrión S15: **0**); con el mod el invitado recibió **75** (S15), **106** y **60** (S14). La
condición de §6 («la pieza sin CC cuyo padre está retenido *ahora* en la tabla de NPC o en la misma llamada») vio 205
piezas y no retuvo ninguna, porque:

- la pieza llega en un `NotifyAppear` **anterior** al de su padre (el padre aún no está retenido ni anunciado:
  `B01000A4` 10,9 s antes que `B01000A3`; `141B`/`14E8` 0,7 s antes);
- los **hijos con CC** (miembros de grupo, `B010012E` del jefe `B010012D`) no eran candidatos;
- `observer_load_defer` y `observer_hold` sueltan por **clave**, no por familia (44 huérfanos en la ráfaga tras el
  2762 del invitado).

### 7.2 La regla

Para **cada clave `0xB`** de un `NotifyAppear` que ninguna otra regla retenga y cuyo componente de jerarquía nombre un
padre P (`[[actor+0x68]+0x1B8]`, `+8` = el propio actor, `+0x1C` = P; §6.1), y para cada sesión destinataria S:

1. **P va en la misma llamada y está retenido para alguien** (cualquier retención: `observer_hold`, `load_defer`,
   D-53, otro miembro de familia): el miembro sigue a P **exactamente** (mismo dueño, mismo grupo, en el orden de la
   llamada).
2. **P no va en la llamada y tiene una entrada retenida para S** (tablas de NPC, de familia, de D-53 o de
   `child_settle`): retenido para S.
3. **S es la sesión 2 viva del usuario 2 y P no le ha llegado todavía**: retenido para S. «Le ha llegado» = el
   conjunto de claves que se le anunciaron en esta época de invitado:
   - se añaden las de cada `NotifyAppear` que lo incluye como destinatario (nativo, pase o grupo de un reparto,
     liberación de una retención) y las de cada 2005 que `take_frame` le encola;
   - se quitan las de cada `RemoveActorAck` 2516 que se le encola;
   - es un conjunto fijo sin asignaciones (16 384 huecos, sondeo lineal con borrado por desplazamiento). Lleno →
     responde «llegó» para todo hasta la época siguiente (como sin la clave) con un WRN.

Quién lo recibe ahora (el «dueño» de `plan_split`): el usuario de los destinatarios que no lo necesitan; si todos lo
necesitan, el otro usuario. Si un mismo usuario lo recibiría en una sesión y no en otra, o ambos usuarios tienen que
esperar → nativo (**partial**). Un miembro de un miembro de la misma llamada se decide después de su padre (hasta 8
niveles).

El sitio se reserva bajo el candado en el propio reparto (tabla de **1024** (miembro, sesión)). Llena → nativo,
contado. Jerarquía ilegible o de otro actor → nativo (**unlinked**).

### 7.3 Liberación (siempre detrás del padre)

- **Con la liberación del padre:** cuando una pasada suelta a P hacia S, sus miembros de S salen en **el mismo
  `NotifyAppear`**, y los miembros de esos miembros también, a cualquier profundidad. El lote se ordena por
  profundidad (`std::stable_sort`: padres primero).
- **Justo después del anuncio nativo del padre:** si el motor (o el pase de un reparto) anuncia P al usuario 2, en
  la cola del propio `NotifyAppear` (hilo nativo) se suelta lo que esperaba a P con un `NotifyAppear([sesión 2],
  [miembros por profundidad])` más. Si en ese momento corre una pasada en otro hilo, lo suelta la siguiente pasada
  («su padre ya llegó»).
- **La puerta de las liberaciones de `observer_hold` / `observer_load_defer`:** un NPC cuya liberación toca hacia la
  sesión 2 y cuyo padre ni le ha llegado ni sale en esa misma pasada **no sale**. Pasa a la tabla de familia
  (`ReleaseBlocked`, `npc_origin`) y sale con su padre o justo después de él. Sus contadores de NPC y de `load_defer`
  cuentan cuando por fin sale. Si la tabla está llena, sale como antes (WRN).
- **Caída:** se descarta junto con su padre (fuera de rango, despawn, época, observador ido), y con él toda su
  descendencia.
- **Tope (nativo, como hoy):**
  - el padre no está retenido y no llega en **10 s** (`kFamilyOrphanMs`) → se anuncia solo: es un huérfano, WRN
    `ITS PARENT NEVER CAME`;
  - el padre sigue retenido tras **90 s** (`kFamilyMaxHoldMs` = 60 s de `load_defer` + 3 × 10 s) → nativo, WRN;
  - en la sesión del anfitrión, que no tiene conjunto de «llegó» (su cliente lo tiene todo desde antes del gancho),
    un miembro cuyo padre ya no está retenido sale enseguida (**orphan**, la regla de §6).
- **Válvula:** si salen por el tope de 10 s al menos 16 miembros y más de 1 de cada 4 de los que esperaron por un
  padre no anunciado, la regla 3 y la puerta se **apagan solas** hasta el final del proceso (WRN `rule is OFF`).
  Protege del caso en que la jerarquía nombre padres que el motor no manda (cada miembro llegaría 10 s tarde). Las
  reglas 1 y 2 siguen.
- **Lo inverso:** si el usuario 2 pierde al padre (2516) y conserva al hijo, cuando el hijo vuelve a anunciarse solo
  espera al padre (regla 3). Si el padre vuelve retenido (restauración 0), el hijo queda detrás de él (regla 1 o 2).

`observer_hold_acks`: las claves de los miembros retenidos al usuario 2 (salvo las de `load_defer`) se registran como
las de los NPC (sus 2697/2356… esperan a su 2005). Si un miembro se descarta, sus paquetes también.

### 7.4 El recuento en `take_frame` (con `observer_hold=1`, tenga la clave el valor que tenga)

`child_hold_on_user2_out` replica `analyze_capture.py <bin> orphans` sobre lo que se encola para la sesión 2 viva, en
orden y después del enrutado de `observer_hold_acks` (ni el 2005 ni el 2516 se retienen allí):

- **2005:** cada registro de tipo ≠ 0 cuya cola `padre | n | hijos` nombre un padre que no está vivo en ese cliente
  (ningún 2005 anterior desde su último 2516) ni en el mismo 2005 es un **huérfano**. Se cuenta y se escribe una
  línea WRN `ORPHAN queued for user 2` (las 40 primeras);
- **2516:** quita sus claves.

El resumen periódico lleva `family order: 2005 to user 2 N (M records), 2516 K | ORPHANS queued for user 2 X`. **X = 0
es el orden nativo.** Con la clave a 0 o `probe` mide lo de hoy (la línea base).

### 7.5 Sonda de la fuente [H]

La regla usa la jerarquía (+0x1C), como pide el análisis de S15. No está verificado que en el servidor sea siempre el
mismo valor que la cola `+0xAC` del registro, sobre todo en los miembros con CC. Por eso cada candidato lee también el
`+0xAC` del actor del **servidor**: lectura SEH, solo cuenta, nunca decide. En el resumen sale `source probe:
hierarchy parent == server +0xAC N, DIFFERENT M, only +0xAC K`, más 12 líneas con ejemplos. Si `DIFFERENT` u `only
+0xAC` salen altos, la fuente es la otra (siguiente paso: decidir con `+0xAC`).

### 7.6 Configuración e inis

- `[session2] observer_hold_family = 0 | probe | 1`. Sin la clave, vale lo que diga `observer_hold_pieces` (alias).
  Si están las dos, manda `observer_hold_family` (línea INF). Necesita `observer_hold=1` y `create_session=1`.
- `probe`: misma decisión, solo cuenta: `WOULD be withheld` en los repartos y `WOULD wait` en las liberaciones.
- Inis del anfitrión `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini`: **`observer_hold_family=1`**. Hay red de
  seguridad (topes nativos, válvula, recuento de huérfanos) y el `probe` de §6 ya no medía nada útil.

### 7.7 Tests (`tests/optd_s3_tests.cpp`)

- `test_piece_logic`: `fam::family_owner`, `family_release_step`.
- `test_family_logic`: `KeySet` contra un conjunto de referencia (4000 operaciones con colisiones), desbordamiento;
  colas `padre | n | hijos`; `OrphanCounter` con padre en el mismo 2005, 2516 y registros de tipo 0.
- `test_family_cc_child`: jefe retenido y miembro con CC listado antes → `{jefe, miembro}` en un 2005.
- `test_family_parent_later`: pieza antes que su padre → sale justo después del anuncio nativo del padre.
- `test_family_load_defer`: jefe y miembro de `load_defer` y pieza del jefe → tras el 2762, un 2005
  `{jefe, miembro, pieza}`.
- `test_family_multilevel`: montura → jinete → bolsa (tres niveles) en un 2005, y caída de toda la familia.
- `test_family_orphan_timeout`: un padre que no llega nunca → nativo a los 10 s, WRN, el recuento lo ve.
- `test_family_release_gate`: un NPC retenido cuyo jefe no llegó → espera como familia y sale tras el jefe.
- `test_family_reverse`: 2516 del jefe, el miembro vuelve solo y el jefe vuelve retenido → padre primero.
- `test_family_valve_and_source`: la válvula y la sonda `+0xAC`.
- `test_family_off_and_probe`: con 0 y `probe`, como antes (el recuento cuenta el huérfano).
- `test_hold_pieces` y `test_hold_pieces_acks` (los de §6) siguen pasando con la nueva regla.

En todas las de familia, el `NotifyAppear` falso pasa cada 2005 de la sesión 2 por el gancho de `take_frame` con los
padres de sus registros. Se comprueba **0 huérfanos** salvo donde se espera uno.

### 7.8 Qué mirar en la próxima sesión

1. Al arrancar: `observer_hold_family ON` y `family order: every 2005 / 2516 queued for user 2 is checked`.
2. En el resumen del anfitrión:
   - **`ORPHANS queued for user 2 0`**, o casi: compáralo con `analyze_capture.py <captura del invitado> orphans`.
     Deberían coincidir; si no, es que algo llega a la sesión 2 por otra vía;
   - `held` repartido entre `parent withheld in the same call`, `parent withheld` y `parent not with user 2 yet`;
   - `NPC releases that waited for their parent` > 0 tras cada 2762 (la ráfaga de S15);
   - `PARENT NEVER CAME` y `AT THE MAX WAIT` bajos, `"not announced yet" rule on`;
   - `source probe`: `DIFFERENT` ≈ 0.
3. En el invitado: sin `NOT READY` de larga duración en `GuestReloadWait [trip N begins]`, sin `[0x12 DROPPED]` en las
   recargas, y los viajeros (Stranger + buey, carros) visibles.

### 7.9 Riesgos

- **[H]** La jerarquía como fuente del padre (§7.5): si no coincide con `+0xAC` en algún tipo, esos registros siguen
  saliendo huérfanos (el recuento lo dice) o esperan 10 s (la válvula corta si son muchos).
- **[E]** Un miembro controlado por el usuario 2 cuyo padre no le ha llegado también se le retiene. El controlador
  siempre recibía al instante; un grupo con control mixto es raro.
- **[E]** El conjunto de «llegó» se alimenta de `NotifyAppear` y de `take_frame`. Una carrera de microsegundos entre un
  2516 y la anotación posterior a la llamada puede dejar un padre como «llegado» de más (sale el hijo, como hoy).
- **[E]** Un miembro que espera a un padre de otro sector que el usuario 2 no observa sale a los 10 s como huérfano.
  Es lo de hoy, con retraso; nativamente no pasa (0 huérfanos).
- Orden dentro de la liberación: el de la lista que se pasa a `NotifyAppear` (padres primero). Sigue sin comprobarse
  que `0x2B060E0` no reordene (§6.5), pero el padre en el mismo 2005 basta para el cliente.
- Coste:
  - en cada reparto con la clave puesta, unas pocas lecturas SEH por clave `0xB` no retenida, y por candidato un recorrido
    de las tablas bajo el candado (≤ 1024 + 1024 + 96);
  - en la pasada periódica, un índice ordenado de todas las retenciones (≤ 2144) mientras haya familia retenida;
  - en `take_frame`, un recorrido de cada 2005 / 2516 de la sesión 2 bajo su propio candado.

### 7.10 Regresión en un PC (11-10, 13:07) y corrección: la fuente es `+0xAC` (rama `p8-family-order-2`)

Prueba: anfitrión con `cdmp_peer` (`login_e3lc00soak_child_112627.troc`) e ini `cdmp.p8-noche-local.ini` con
`observer_hold_family=1`. Log: `backups\regresion-family-20261011-130754.log`.

Resultado **[V]**:
- El recuento funciona: 619 2005 al usuario 2 (1018 registros), 489 2516, **65 ORPHANS**.
- La regla no actuó: `seen 140 | held 0`.
- `source probe`: jerarquía == `+0xAC` del servidor **146**, distinta **0**, solo `+0xAC` **41**.

Por qué:
1. **La jerarquía no es la fuente.** La mayoría de los actores (piezas de tipo 7 y NPC de tipo 3 y 6 con CC) no tienen
   componente de jerarquía (`[comps+0x1B8] = 0`, padre 0), pero su `+0xAC` en el servidor nombra al padre. Los padres
   de todos los huérfanos con línea de sonda coinciden con ese `+0xAC`: `7D→7C`, `A3→A2`, `25F→25E`. Ninguno era
   candidato, así que ninguna regla podía actuar. Los 140 candidatos de la jerarquía tenían a su padre en la llamada
   o ya entregado: la regla 3 no tenía nada que hacer con ellos.
2. **Ninguna vía rodea la clasificación.** Los huérfanos salen por los caminos que el diseño ya cubre:
   - 13:09:54, la instantánea del viaje observador del usuario 2 (`MoveFieldAck` → carga, `load_defer`): las piezas,
     solas en 2005 de 5 registros, mientras su padre se retiene;
   - 13:09:59, la liberación periódica de `observer_hold`: un `NotifyAppear` de **256 claves** que el motor parte en
     2005 de 3 a 5 registros grandes. El miembro iba antes que su jefe en la lista (orden de la tabla) y quedaba en
     un 2005 anterior (`300→2FC`, `263→262`, `12D→12B`). Sin padre conocido, la profundidad era 0 para todos;
   - 13:10:53 y 13:13:09, piezas de un registro anunciadas antes que su padre.
   
   El conjunto «llegó» no marca nada antes de tiempo: un padre retenido por `load_defer` no va en el pase y no se
   anota.

Corrección: `family_parent_with` lee primero el `u32 +0xAC` del actor del servidor (`hold::kActorParent`, una clave
`0xB` distinta de la propia) y solo si no es legible usa la jerarquía. Se usa en el reparto y en la puerta y la
profundidad de las liberaciones. Con el padre conocido:
- la pieza de la instantánea sigue a su padre retenido (regla 1);
- el lote de 256 claves se ordena con los padres primero, así que cada jefe queda en un 2005 anterior al de sus
  miembros aunque el motor lo trocee;
- la pieza suelta espera a su padre (regla 3).

«UNLINKED» pasa a significar que no se puede leer ninguna de las dos fuentes.

Test nuevo `test_family_ac_source`, que reproduce los tres casos con actores sin jerarquía y solo `+0xAC`. El
`NotifyAppear` falso trocea en 2005 de un registro. Sin la corrección fallan 8 comprobaciones; con ella hay 0
huérfanos.

### 7.11 Revisión independiente de `d823732` (corregido en `p8-family-order-2`)

- **4 (cambio de campo).** El conjunto «llegó» y el del recuento solo se vaciaban al cambiar de sesión o de época.
  Ahora un `MoveFieldAck` 2174 encolado para la sesión 2 viva vacía los dos. Si no, tras un cambio de campo sin
  2516 los padres seguirían «llegados» y el conjunto crecería hasta llenarse, y lleno deja pasar todo.
  - En las capturas de S14 y S15 (anfitrión y Shadow) vaciar en el 2174 no cambia ningún recuento: 0 y 0 nativos,
    75 y 106 en el invitado. El servidor vuelve a anunciar a los padres antes que a los hijos.
  - `analyze_capture.py orphans` hace ahora lo mismo.
  - Pendiente comprobar con una captura del invitado que su cliente, de verdad, se vacía en el 2174.
- **5 (coste bajo `g_mu`).**
  - El reparto copia una vez las retenciones hacia los destinatarios de la llamada en un índice ordenado y hace
    búsquedas binarias. Antes recorría las cuatro tablas por candidato y sesión.
  - Busca al padre en la llamada con la lista de claves ordenada.
  - Sale enseguida si el usuario 2 no recibe la llamada, no se retiene nada en ella y todas las tablas están vacías:
    las llamadas del anfitrión no leen nada.
  - Los arrastres (liberación detrás del padre, caída con él y lo que espera a un padre anunciado) recorren la tabla
    de familia una vez por nivel contra una lista ordenada, en vez de una vez por padre.
- **6.** `take_frame` solo pasa un 2005, 2516 o 2174 al gancho cuando se ha encolado de verdad (`pushed`). Antes se
  pasaba antes de encolar, y un empuje fallido dejaba a un padre «llegado».
- **8.** `analyze_capture.py orphans` quitaba solo la primera clave de un 2516; ahora quita todas, como el
  recuento C++. En las capturas todos los 2516 llevan una clave.

Tests: `test_family_field_change_and_gate` (el 2174 vacía los dos conjuntos; la llamada del anfitrión sin nada
retenido no se mira).
