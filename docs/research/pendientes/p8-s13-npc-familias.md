# P8 · Sesión 13: familias de NPC del invitado y la «media ciudad paralizada»

11 de octubre de 2026, rama `opcion-d`. Build del juego **1.0.0.2976**, direcciones en **RVA**.
Solo lectura: no se lanzó el juego, no se tocó código.

**[V]** = comprobado en los logs y capturas de la sesión 13. **[E]** = interpretación. **[H]** = pendiente de prueba.

Datos:
- Invitado (Shadow): `backups\p8-sesion13-shadow\cdmp_20261010-233225.log` y `cdmp_packets_20261010-233243.bin`.
- Anfitrión: `backups\p8-sesion13-host\cdmp_20261011-003019.log` y `cdmp_packets_20261011-003030.bin`.
- Relojes:
  - en el invitado, `qpc − 271,38 s = t` (`t` = tiempo de captura del invitado);
  - anfitrión → invitado: `t_invitado = t_anfitrión − 147,84 s`. Se alinearon 67 658 movimientos idénticos
    (`2556` del anfitrión ↔ `2697` recibido por el invitado); p10–p90 en ±0,02 s.
- Hitos del invitado: dos viajes (el 2549 en t 15,5 y en t 44,9), el 2144 en t 45,8 y el 2762 en t 59,6.
  La instantánea del campo del anfitrión llega en t 44,6–45,3.
- Ambos jugadores siguen juntos en Hernand (a menos de ~50 u el uno del otro):
  - plaza entre t 95 y 450 (`x −10720…−10640`, `z −3920…−3840`);
  - bajan hacia el sur entre t 470 y 650;
  - salen de la ciudad desde t ~655 (`z < −4050`).

## 0. Resumen

1. **Ningún límite del mod explica la media ciudad (§4) [V].**
   - Ningún tope se alcanzó, salvo el presupuesto de `guest_schedule_load`. Ese presupuesto afectó solo a
     13 animales al entrar.
   - La red entrega todo [V]. Para **todos** los NPC hay la misma presencia en los dos PC:
     - 0 claves anunciadas al anfitrión después de t 50 faltan en el invitado;
     - 0 claves con menos del 50 % de solape.
   - Movimientos, acciones, cambios de IA, 2335 y 2661 llegan 1:1. Las únicas excepciones son las acciones
     marcadas de secuenciador (familia d).
2. **La ciudad se divide en dos cohortes casi iguales [V]:**
   - **instantánea**: NPC que ya existían cuando entró el invitado;
   - **vivos**: NPC que aparecieron después.
   - Dentro de la ciudad (t 120–400), a menos de 80 u del invitado hay **~110 NPC vivos y 130–160 de
     instantánea**. Esa es la «mitad».
   - Fuera de la ciudad (t ≥ 600) **todos** son vivos, y fuera «todo está bien».
   - Las mismas escenas contienen NPC de las dos cohortes: control natural (§3).
3. **Qué distingue a las cohortes [V]:**

   | Cohorte | 2723 `StartSpawnActionAck` | Enlace | Estado al crearse |
   |---|---|---|---|
   | Vivos | **2549 de 2565** claves lo reciben, en el mismo envío que su 2005 | nativo (vf13 `0x9ED290`) | normal |
   | Instantánea | **2 de 540** | por el reintento, 3–7 s después (o nunca, en la familia d) | creados durante la carga, antes del 2144, con NPC y dueño aún sin `+0x5E & 0x10` |

4. **Lo comprobado como roto dentro de la instantánea:**
   - **Familia (d), secuenciador:** ~20 NPC en el centro de la plaza **sin un solo paquete** en toda la sesión
     (sus acciones están marcadas y no se relevan). 18 están sustituidos por la espera.
   - **Familia (b):** ~110–136 NPC con todo entregado y enlazado. No hay en el log una señal de qué les falta
     visualmente **[H]**.
5. **Ranking de causas (§6):**
   1. Cohorte de instantánea: falta de 2723 o inicialización con el dueño aún no resuelto.
   2. Familia (d).
   3. Presupuesto del cargador (13 animales).

   No hay ningún tope del mod.
6. **NPC que solo ve el anfitrión fuera de la ciudad [V]:** no es un problema de entrega. Todo NPC anunciado al
   anfitrión también se anunció al invitado, con la misma presencia. Es visibilidad o creación en el cliente del
   invitado **[H]**: hace falta la hora o el lugar para atarlo a una clave.

## 1. Método (reproducible)

Scripts en el scratchpad de la sesión (`…\scratchpad\s13\`), Python 3.14 `-I`:

| Script | Qué hace |
|---|---|
| `mvall.py` | Recorre el `.bin` y guarda por clave: 2005 (tipo, nombres, posición en `len−208/−210/−166/−168`, 97/102 aciertos), 2516, 2556/2697 (`xyz` en `+4`), 2223/2991, 2404/2734, 2723, 2828, 2888 |
| `acts.py` | Hashes de 2991/2223/2723 (`+4/+8/+73/+77`, `b[0x2D3]`) |
| `align.py` | Alineación de relojes |
| `fam.py` | Familias por la cadena de la vf13; cuenta por distancia y tiempo |
| `rate.py` | Clases caminante / solo acciones / estático (t 150–450, ≤ 100 u) |
| `del.py` | 2335/2126 frente a la presencia de sus NPC |
| `honly.py` | NPC solo en el anfitrión |
| `npc.py` (S12) | Líneas `GuestStage [NPC vf13/res8/owner/instance]` |

Familias, por la vf13 de cada NPC (un NPC por clave):

| Familia | Clave | Cadena de la vf13 | NPC en S13 |
|---|---|---|---:|
| (a) | `0x2000xxxx` | `instance FOUND` | 482 |
| (b) | `0x2000xxxx` | `res8 passes` y sin dueño → reintento | 181 |
| (s) | `0x2000xxxx` | `res8 EXITS` (nueva) | 13 |
| (d) | de secuenciador | — | 88 + 1 |
| (e) | sin clave de escena | — | 12 887 llamadas |

## 2. Familias en S13

**[V]** Cifras de `GuestSchedBind retry summary`, `GuestStage summary` y `SceneDiag [H1 summary]`.

| Familia | NPC | Cohorte | Estado en el invitado |
|---|---:|---|---|
| (a) enlace nativo | 482 | vivos (t ≥ 100) | enlazada; 2723 + 2991 |
| (b) reintento | 181 (190 en la cola) | instantánea (t 44,6–45,3) | **177 BOUND**, 1 desaparecido, 13 «still 0» (= la familia s); 2991 relevadas 1:1 |
| (s) `res8 EXITS` | 13 | instantánea | sin enlace: su fichero no estaba cargado cuando el juego leyó el bloque de IA (§4.4) |
| (d) secuenciador | 89 | 30 instantánea, el resto vivos | 32 claves con acciones marcadas en el anfitrión (no relevadas); 18 sustituidas por la espera (`GuestSeqRestore`) |
| (e) sin escena | — | ambas | solo 2991 relevadas; fuera de la ciudad, fauna y enemigos |

Cerca del invitado (≤ 80 u):

| t | Vivos (a / d / e) | Instantánea (b / d / e) |
|---:|---|---|
| 60 | 0 NPC | **151 / 18 / 6** |
| 120 | 94 / 11 / 4 | **136 / 20 / 3** |
| 250 | 93 / 13 / 4 | **123 / 21 / 0** |
| 400 | 92 / 13 / 5 | **110 / 20 / 0** |
| 500 | 52 / 1 / 8 | 54 / 2 / 0 |
| 600 | 58 / 1 / 2 | 3 / 0 / 0 |
| 700–800 | 24–32 / 0 / 0–10 | **0** |

La instantánea desaparece en t 557,7. Ese momento el anfitrión borra con 2126 sus etapas
(`0x20000014` con 54 NPC, `0x2000001B` con 14, `0x20000181`) y el servidor da de baja a los NPC (t 558). Pasa
igual en los dos PC.

## 3. Mapa de la ciudad (t 150–450, ≤ 100 u del invitado)

Clases (posiciones `x`/`z` del 2005 o del último 2697):
- **caminante**: recorre más de 5 u;
- **acciones**: quieto, recibe 2991;
- **estático**: sin ningún paquete.

| Familia | Escena | NPC | Rango x | Rango z | Nombres más comunes |
|---|---|---:|---|---|---|
| (a) | `0x20000188` | 54 | −10673…−10612 | −3911…−3815 | Stranger 29, Wandering Freesword 10, Beggar 8 |
| (a) | `0x20000187` | 12 | −10704…−10677 | −3830…−3796 | Noble 5, Hernandian Guard 3 |
| (a) | `0x200001A5` y otras 10 | 21 | — | — | — |
| (b) | `0x20000014` (`splithorn_boss_0100_schedule` en S12) | 54 | **−10711…−10676** | −3895…−3838 | Stranger 24, Wandering Freesword 10, Hernandian Guard 4, Noble 4 |
| (b) | `0x20000188` | **28** | −10681…−10643 | −3936…−3856 | Stranger 18 |
| (b) | `0x20000187` | **15** | −10730…−10701 | −3914…−3838 | Maid 7, Hernandian Guard 4 |
| (b) | `0x2000001B` (espectadores de minijuego) | 13 | −10668…−10659 | −3840…−3831 | Stranger 11 |
| (d) | 18 escenas | 33 | — | — | ver §5 |

Clases por familia:

| Familia | Caminantes | Solo acciones | Estáticos sin paquetes |
|---|---:|---:|---:|
| (a) | 36 | 51 | 0 |
| (b) | 26 | 86 | 0 |
| (d) | 5 | 9 | **19** |
| (e) | 0 | 4 | 0 |

**Control natural [V]:** `0x20000188` (54 vivos y 28 de instantánea) y `0x20000187` (12 y 15) mezclan las dos
cohortes en la misma escena, con el mismo recurso y la misma etapa en X.
- Si en vídeo se paraliza la mitad de **un mismo grupo**, la causa es la cohorte, no la escena.
- Geografía: la instantánea ocupa el oeste y el centro (donde aparecieron los dos jugadores, −10717, −3915).
  Los vivos ocupan el este.

## 4. Límites del mod: todos revisados

| Límite | Valor en S13 | ¿Se alcanzó? |
|---|---|---|
| reenlace: 64 por 2335 | `over the cap 0` | no |
| reintento: 32 por pasada, 120 s, 3 intentos | cola 191, `full 0`, espera máxima 7,5 s; 177 BOUND, 13 «still 0» tras 3 intentos (216 = 177 + 13 × 3) | solo los 13 de §4.4 |
| `guest_seq_route`, libro 512 | `limited 0 lost 0`; `watched 64` es solo diagnóstico | no |
| `guest_schedule_load`, tope de 256 llamadas | 85 de 256 | no |
| `guest_schedule_load`, presupuesto por paquete | `budget (kept for a retry) x956`, `retried 945`, retry set 0 al final | **sí, en t 45,0** (§4.4) |
| `guest_schedule_load`, libro 4096 | 69 de 4096, `overflow 0` | no |
| `observer_sched_copy`, libro 256 | `live 59 of 256`, `full 0` | no |
| `observer_sched_copy`, 6 seguimientos | `kept 15 (dropped 2)` | [E] irrelevante: los seguimientos en vivo los copia el anfitrión (2958 ×4, 2909 ×5) |
| 2335 entregados | anfitrión: 106 nativos + 46 de la instantánea = **152**; invitado: `routed 152` | no se perdió ninguno |
| 2661 / 2810 | copiados 50 / 45; el invitado enruta 50 / 45 | no |
| cola de entrada de red | `full 0`, `capped 0`, pico 593 | no |
| RemoteInterp, tabla de 8192 | usada 1604, `full 0` (`another actor 997` = reutilización normal) | no |
| guardias GuestDiag / S3 | retienen **1** actor (`0xB010025E`, «Baby Deer Carcass») | no |

### 4.1 Entrega por NPC [V]

Comparación por clave, en el tiempo común, entre lo que el anfitrión envía (C>S) y lo que el invitado recibe (S>C):

| Mensaje | Anfitrión → invitado |
|---|---|
| Movimientos (2556 → 2697) | iguales |
| Acciones (2223 → 2991) | iguales, salvo las de `B+0x281` |
| Estado de IA (2734 → 2404) | iguales (995/995 en los caminantes de a) |

- Cadencia de movimiento de los caminantes, p50 / p90:

  | Familia | p50 | p90 |
  |---|---:|---:|
  | (a) | 1,12 s | 1,53 s |
  | (b) | 1,14 s | 1,46 s |
  | (d) | 1,15 s | 1,95 s |

- Ninguna clase recibe menos que otra.
- **No es un cuello de botella de red ni del relé.**

### 4.2 Lo que el invitado aplica [V]

- G5 sobre los NPC vigilados de (b), por ejemplo `0xB010009C`: `L0 current 0xA0C332BC`, `chart yes` durante 8 minutos.
  Es el mismo hash que repite el anfitrión cada ~9 s.
- En el anfitrión, la sonda `P8Probe [Restart 0x86392A]` resuelve esos hashes (`0xA0C332BC`, `0xB6DA5B4E`,
  `0x537FFD08`) en el gráfico **normal** de la capa (`[layer+0x110]`, 27 173 entradas). No necesitan `CC+0x70`.
- **[E]** La acción está instalada y su gráfico existe. Si estos NPC están paralizados, lo que falla no es la acción
  recibida, sino algo del estado del actor de la instantánea.

### 4.3 Escenas borradas [V]

- 107 borrados (2126) antes del final.
- Solo 5 dejan NPC presentes, y son los mismos en los dos PC: `0x20000072`, y en t 557,7 `0x20000014`,
  `0x2000001B` y `0x20000181`, más `0x200001D6`.
- No se recreó ninguna. Los NPC se dan de baja a la vez en los dos PC.

### 4.4 El único límite que se alcanzó: el presupuesto del cargador [V]

- En t 45,0, `GuestSchedLoad [not loaded] … budget spent: kept in the retry set` aplazó `0xF56759DA` y
  `0x268D4087`.
- El juego leyó en t 45,19 el bloque de IA de sus 13 NPC: `INVALID: FILE NOT LOADED`, con escena y recurso no nulos.
  Son los únicos 13 de 664 bloques con escena.
- Por eso `0x9F2FA0` no resolvió el nodo: son los **13 «still 0»**.
- Afectados: todo fauna, fuera de la plaza:

  | Escena | Claves | Animales | Posición | Baja |
  |---|---|---|---|---|
  | `0x2000018B` | `0xB010013D`–`142` | 6 ovejas | (−10710, −3978) | t 111 |
  | `0x20000192` | `0xB0100143`–`149` | 7 aves | (−10735, −3946) | t 111–116 |

- No explica la media ciudad.
- **Arreglo:** si el bloque de IA ya se leyó sin el fichero, el reintento debe:
  - forzar la carga en el hilo principal;
  - y releer el nodo, o llamar al camino que relee `[[actor+0x68]+0x58]+0x90`.
  
  O bien no aplicar el presupuesto a recursos nombrados por 2005 del mismo lote.

## 5. Familia (d): estáticos comprobados [V]

Sin un solo 2991 ni 2697 en toda su presencia. Las acciones marcadas del anfitrión (`SceneDiag [H1 summary]`,
32 claves) no se relevan.

| Clave | Nombre | Escena | Posición | Cohorte | Qué hace el anfitrión |
|---|---|---|---|---|---|
| `0xB010006D`, `0071`–`0076` | Damiane, Matthias, 3 × Wandering Freesword, Barden Middler, Stranger | `0x13` | (−10682, −3845), centro de la plaza | instantánea | marcadas F11/72 (`0x71`–`74`); silencio (`0x6D`, `75`, `76`) |
| `0xB0100077` / `78` | Stranger ×2 | `0x19` | (−10706, −3845) | instantánea | F54/417 cada uno |
| `0xB010007E` / `7F` / `80` | Hernandian Guard ×3 | `0x16C` | (−10735, −3858) | instantánea | F37/277 |
| `0xB0100082` | House Roberts's Servant | `0x198` | (−10680, −3875) | instantánea | F37/278 |
| `0xB01005FE` | Pedro | `0x1A1` | (−10745, −3828) | vivo (t 106) | F59 |
| `0xB0100601`–`605` | 5 × Wandering Freesword | `0x1A4` | (−10662, −3832) | vivo (t 106) | F179 (`601`/`602`); silencio el resto |

- `GuestSeqRestore summary`: 18 sustituidas por la espera (`0x26107D3C`), entre ellas `0x6D`, `0x71`–`76` y
  `0x601`–`605`.
- `GuestBodyAnim`: `CC+0x70/+0x78 = 0` en todos los vigilados (`BodyAnim seen for 11 … without CC+0x70 now`).
- 2524: `guest_seq_route_2524=probe`, `WOULD route` ×2; 133 se quedan nativos (clima / personales).
- **[E]** Este es el grupo de «paralizados» seguro. Son pocos (~20 de ~250 en la plaza), pero están en el centro:
  la tropa de `0x13`, guardias y espectadores.
- Hay (d) en las **dos** cohortes (`0x1A4` es vivo). Su fallo no depende de la cohorte.

## 6. Causas candidatas, ordenadas

1. **Cohorte de instantánea [V la división; H el mecanismo].**
   - Por qué es la primera:
     - explica la «mitad» en número;
     - explica la geografía (oeste y centro de la plaza);
     - explica que fuera de la ciudad todo esté bien: allí todo es vivo.
   - Su (b) tiene todo lo verificable en el log igual que los vivos:
     - paquetes;
     - enlace a la etapa (`a node carries the NPC 179`);
     - acción instalada.
   - Diferencias comprobadas con los vivos:
     - **(i)** no reciben **2723** `StartSpawnActionAck`. El servidor lo genera con la primera acción del anfitrión
       tras aparecer (2223 con `+4 = 0`; mismo hash que las 2991 siguientes). La instantánea solo tiene el estado
       del 2005 (`restore` `0x8646FC`) y luego 2991 de reinicio;
     - **(ii)** se crean durante la carga, con el NPC y su dueño (`0xA0100001`) aún sin `+0x5E & 0x10`.
       `0x8AE1A0` los rechaza: el reintento lo arregló para el **componente de secuenciador**, pero cualquier otro
       paso de inicialización que busque al dueño o controlador con `0x8AE1A0` pudo fallar igual sin que nadie lo
       reintente **[H]**;
     - **(iii)** su enlace llega 3–7 s después, ya con la IA arrancada.
   - Para separar (i), (ii) y (iii) en la próxima sesión de dos PC:
     - sonda G5 en **pares de la misma escena** (`0x20000188` y `0x20000187`, un vivo y uno de instantánea):
       - `+0x5E`, `+0xC2`, `+0xCF`;
       - capa `+0x10` (acción) y `+0x18` (reproducción);
       - **avance del tiempo de reproducción** entre dos lecturas;
       - número de llamadas a la actualización de capa `0x2329700` por actor;
       - modo de IA;
     - vídeo de esos pares.
   - Arreglos candidatos, por coste:
     - **(a)** que el anfitrión mande a la cohorte de instantánea su acción actual como **2723**, con la cola de
       `observer_sched_copy` tras el 2762 del invitado;
     - **(b)** que el invitado retenga los 2005 de NPC de la instantánea hasta tener al dueño (`+0x5E & 0x10`) y
       `M+0x231 == 3`, para que sigan el camino de los vivos (vf13 nativa);
     - **(c)** pedir al anfitrión que **reanuncie** (2516 + 2005 + 2723) los NPC de la instantánea una vez dentro el
       invitado, como `child_reannounce`.
2. **Familia (d) [V].**
   - ~20 estáticos en el centro de la plaza; 32 claves en total.
   - Siguiente paso, el de `p8-s12-npc-familias.md` §5:
     - 2524 → X (`guest_seq_route_2524=1`) solo para las escenas de mundo;
     - luego volver a mandar la última acción marcada.
3. **Presupuesto de `guest_schedule_load` [V].** 13 animales, fuera de la plaza (§4.4).
4. **No son causa [V]:**
   - topes del reenlace, del libro de `guest_seq_route`, del libro y los seguimientos de `observer_sched_copy`, y
     las 256 llamadas del cargador;
   - 2335 o 2661 sin entregar;
   - la red o RemoteInterp;
   - las guardias.

## 7. NPC que solo ve el anfitrión fuera de la ciudad

- **[V]** No hay ninguna clave anunciada (2005) al anfitrión después de t 50 que falte en el invitado.
- **[V]** No hay ninguna con presencia distinta (0 claves con menos del 50 % de solape).
- **[V]** Lo único asimétrico:
  - el invitado recibe 7435 `SpawnOnDemandGimmickNak` (el anfitrión 1636);
  - el invitado envía 10 776 `SceneLevelOnDemandGimmickSpawnReq` (el anfitrión 3329).
- **[E]** Si los 2–3 NPC son objetos o personajes de *gimmicks* bajo demanda o de `AutoSpawnRoad` (carros,
  viajeros), el invitado los pide y el servidor los rechaza.
- **[H]** Para atarlo hace falta la hora aproximada o una captura con el lugar.
- Siguiente prueba: un censo del invitado cada 5 s de «2005 entregado y actor ausente de la tabla del cliente».
