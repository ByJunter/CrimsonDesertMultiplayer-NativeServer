# P8 · Sesión 12: familias de NPC del invitado

10 de octubre de 2026, rama `opcion-d`. Build del juego **1.0.0.2976** (PE `0x6AB28F00`), direcciones en **RVA**.
Solo lectura: no se lanzó el juego, no se tocó código.

**[V]** = comprobado en los logs y capturas de la sesión 12. **[E]** = interpretación. **[H]** = pendiente de prueba.

Datos:
- Invitado (Shadow): `backups\p8-sesion12-shadow\cdmp_20261010-215406.log` y `cdmp_packets_20261010-215423.bin`.
- Anfitrión: `backups\p8-sesion12-host\cdmp_20261010-225143.log` y `cdmp_packets_20261010-225154.bin`.
- Relojes:
  - en el invitado, `qpc − 383,22 s = capture t`;
  - la entrada del invitado es el 2549 en t 44,45, el 2144 en t 45,34 y el 2762 en t 59,68;
  - el anfitrión manda la instantánea en su 22:55:39 (`[sched gate]`), que el invitado recibe en t 44,56.

## 0. Resumen

Lo que el usuario ve encaja con cuatro familias. Cada una tiene un fallo distinto.

| Familia | Qué es | NPC tipo 3 (sesión / presentes al final) | Fauna tipo 6 (sesión / presentes) | ¿Se ve bien? | Qué le falta |
|---|---|---:|---:|---|---|
| **(a)** | Horario con la etapa en X y **enlazado** (vf13 nativa) | 160 / **114** | 6 / 4 | sí: taberna sentados y apoyados, «cesta» | nada |
| **(b)** | Horario con la etapa en X y **sin enlazar** (los 196) | 183 / **134** | 13 / 2 | no: plaza de Hernand en pose A, guardias | la búsqueda `0x8AE1A0` falla durante la carga y nadie reintenta |
| **(c)** | Horario cuya **etapa nunca se creó** (los 21 «no stage in X») | 4 / 2 | 29 / 1 (rebaño) | no | el fichero `.paschedule` se descargó entre su carga y el 2335 |
| **(d)** | **Secuenciador** (2661) con acciones `seq_bodyanim` marcadas (`2D3=1`) | 42 / 24 (27 con acciones marcadas) | — | no: de pie (espera), «el agachado» | gráfico dinámico `CC+0x70/0x78` (eventos de la escena) |
| **(e)** | Sin clave de escena: solo acciones relevadas | 133 / 23 | 1324 / 105 (aves) | sí (no hay quejas) | nada |

«Presentes al final» = anunciados con un 2005 y sin 2516 en t 598, el momento del último censo (22:04:52). El servidor
solo anuncia lo cercano, así que es lo que había alrededor del invitado.

Datos clave:
1. **(b) [V]:** los 196 son exactamente los NPC cuyo 2005 se desempaquetó en t 44,58–44,88, **durante la carga del campo**
   (entre el 2549 y el 2144).
   - En su vf13 la búsqueda del dueño (`0x8AE1A0`, Kliff `0xA0100001`) **no dio nada**: no hay línea
     `GuestStage [NPC owner 0x9ED453]` para ninguno, y la etapa ni se buscó.
   - En t 47,80, `guest_sched_bind` tampoco obtuvo el NPC con `0x8AE1A0`: 196/196 `not called`.
   - Todos los NPC posteriores (t ≥ 82) sí encontraron al dueño: 167/167.
   - **No es un fallo de orden 2335/2005:** aunque la etapa hubiera existido, la vf13 habría fallado igual.
2. **(c) [V]:** los 21 «NO stage» son los 21 fallos de `0x1862A80` del `GuestSchedLoad summary` (`missing after our load 21`).
   - El cargador los cargó en t 44,56 en el hilo de red y soltó su referencia en el acto (`0x38AD90`).
   - El 2335 se manejó 3,2 s después.
   - El contenedor pasó de 35 ficheros a **15** en ese intervalo.
   - Sobrevivieron exactamente:
     - los 12 recursos que nombraban NPC ya presentes (las 12 escenas de (b));
     - los 3 cargados 0,24 s más tarde.
   - **Expulsión por inactividad**: el riesgo S9-1 de `p8-s9-horarios.md`.
3. **(d) [V]:** los 27 NPC con acciones marcadas del anfitrión (`SceneDiag [H1 summary]`: `NPC keys with flagged actions 27`)
   son todos de secuenciador (2661 enrutado a X).
   - 24 no recibieron **ninguna** 2991.
   - 19 pasaron por `GuestSeqRestore` → **espera `0x26107D3C`** (de pie), porque ni el gráfico de su capa ni
     `[CC+0x70/+0x78]` tienen el hash `seq_bodyanim`.
4. **Inversión de orden en el invitado [V]:**
   - el anfitrión puso los 43 2335 en la cola **antes** que los 2005 de NPC (`NPC 2005 records already queued to user 2: 3`);
   - el invitado los recibió así (carga en t 44,56);
   - pero el juego ejecutó el manejador del 2335 (y los 2661) en t 47,80, **después** de las vf13 de los 2005 (t 44,58).
   - **[E]** El cliente aplaza los Ack de escena hasta acabar de restaurar los actores del campo. Durante la entrada, la
     instantánea nunca puede llegar antes que la vf13: el reenlace es imprescindible y tiene que correr **después** de la
     carga.

## 1. Método (reproducible)

Scripts en el scratchpad de la sesión (`…\scratchpad\`), con Python 3.14 `-I`:

| Script | Qué hace |
|---|---|
| `cap.py` | Lee el `.bin` del invitado en streaming. Saca: registros 0xB de 2005 (tipo `r[0]`, clave `@1`, cadenas UTF-16), 2516 (bajas), 2991/2223 de 725 B (hashes `+4/+8/+73/+77`, `b[0x2D3]`), 2661 (lista nodo/actor), 2335/2126 |
| `npc.py` | Por NPC del log del invitado: `GuestStage [NPC vf13/res8/owner/instance]` (clave `comp+0x2E0`, dueño `comp+0x2D8`, resultado), líneas `GuestSchedBind [0x9F2FA0]`, `GuestSeqRestore` |
| `q1.py`…`q7.py` | Presencia, familias, hashes y ejemplos |
| `route.py` | Línea de tiempo de `GuestSchedRoute`/`GuestSchedBind` |

Mismo formato que el informe S11 (`npc-raiz-s11-20261010-work\inventory\inventory.py` y `analyse.py`).

Clasificación por la cadena de la vf13 (`0x9ED290`), un NPC por clave (cada clave se anuncia una sola vez en toda la
sesión, [V]):
- clave `0x2000xxxx` y `instance FOUND` → (a);
- clave `0x2000xxxx`, `res8 passes` y sin `owner` → (b);
- clave `0x2000xxxx`, `owner found` y sin instancia → (c);
- clave de secuenciador (pequeña, de 2661) → (d);
- tipo 3/6 sin clave de escena en la vf13 → (e).

Tipos del registro 2005 (`r[0]`):

| Tipo | Registros 0xB | Qué es |
|---:|---:|---|
| 7 | 5831 | — |
| 6 | 1372 | fauna |
| 3 | 522 | NPC |
| 5 | 25 | — |
| 4 | 12 | NPC con nombre del secuenciador (Damiane, Matthias…) |

## 2. Familia (a): horario enlazado — correcta

- **[V]** 166 NPC (160 tipo 3, 6 fauna). Todos con la vf13 en t ≥ 82, casi todos en la segunda ola: t 277–290, ~21:59:32,
  cuando el invitado entró en la zona de la taberna. La vf13 nativa encontró al dueño y la etapa (`instance … FOUND`).
- Escenas:

  | Escena | Recurso | NPC |
  |---|---|---:|
  | `0x20000185` | `npc_city_00_0001_02` | 67 |
  | `0x20000184` | `_01` | 39 |
  | `0x200001BC` | `city_0003` | 18 |
  | `0x200001AB` | `castle` | 10 |
  | `0x2000016D` | `miseenscene` | 8 |
  | `0x200001A3` | `cd_her_minigame_pub_armwrest_spectator` | 8 |
  | `0x200001A6`–`0x200001B9` | `cd_seq_spawn_docu_nhm_1_sit_lean_*` | — |

  Los `sit_lean` se crearon en la segunda tanda de 2335, en t 282.
- Ejemplos:

  | NPC | Escena |
  |---|---|
  | `0xB0100C4C` / `0xB0100C4B` / `0xB0100C49` («Stranger») | `0x200001A3` |
  | `0xB0100ADE` / `0xB0100B29` | `0x20000185` |
  | `0xB0100BE8` | `0x20000184` |

- G5: 14/14 NPC de horario vigilados `bound … manager +0x28 = X`, `active +0x178 1`.
- **Coincide con el usuario:** dentro y fuera de la taberna, sentados, apoyados y con animaciones variadas.
- **Control natural [V]:** la escena `0x20000185` tiene **36 NPC sin enlazar en (b)** (los de la entrada) y **67 enlazados
  en (a)** (los que llegaron después). Mismo recurso; solo cambia si la vf13 encontró al dueño.

## 3. Familia (b): horario con la etapa en X, sin enlazar — los 196

**[V]**
- 196 NPC: 183 tipo 3 y 13 fauna. 134 tipo 3 y 2 de fauna presentes al final.
- vf13 entre t 44,58 y 44,88: la entrada al campo. El 2144 llega en t 45,34 y el 2762 en t 59,68.
- Escenas y ejemplos:

  | Escena | Recurso | NPC | Ejemplos |
  |---|---|---:|---|
  | `0x20000014` | `cd_seq_quest_splithorn_boss_0100_schedule` | 54 | `0xB010009B` «Wandering Freesword», `0xB010009C` «Stranger» |
  | `0x20000184` | `npc_city_00_0001_01` | 43 | `0xB010008B`/`8D`/`8E` «Maid» |
  | `0x20000185` | `npc_city_00_0001_02` | 36 | `0xB010011C` «Manor Guard» |
  | `0x20000074` | `hernand_slum_ii_drug` | 19 | `0xB01000E6` «Beggar Boy» |
  | `0x2000001B` | `cd_minigame_seotda_base_hernand_spectator` | 14 | — |
  | `0x20000188` | `npc_farm_00_0090` | 9 | perro y ovejas |
  | `0x20000170` | — | 7 | — |
  | `0x20000072` | — | 6 | — |
  | `0x2000016D` | — | 3 | — |
  | `0x20000148` | — | 2 | — |
  | `0x20000191` | — | 2 | arrendajos |
  | `0x20000059` | — | 1 | «Bianca» |

- Nombres: Stranger 62, **Hernandian Guard 29**, Wandering Freesword 20, Beggar 15, Maid 13, Noble 9, Boy 9…
- Cadena de la vf13: `res8 passes` y **sin** línea `owner found`. La vf13 sale en `0x9F30FB`-equivalente: `0x8AE1A0` no da
  el dueño.
- Resultado: `comp+0x318 = 0` para toda la sesión. Cada NPC se anuncia una sola vez, así que no hay otra vf13.
- `GuestSchedBind` en t 47,80 (2335 de la instantánea):
  - `its NPCs: 54 / 14 / 1 / 6 / 19 / 2 / 3 / 7 / 43 / 36 / 9 with comp+0x318 0`;
  - `0 already bound`;
  - `not called: the game's lookup 0x8AE1A0 did not give this actor` × 196;
  - `AI mode [X1+0x66] 2`: la vf13 no llegó a tocarlo; un reenlace posterior lo deja en el valor normal.
- Los 21 2335 de la segunda tanda (t 282) no añadieron candidatos (`candidate 196` sin cambiar, `table walks 43`): sus
  NPC llegaron después y los enlazó la vf13.
- **Acciones:** el anfitrión las manda **sin marcar** (`2D3 = 0`; ninguna de sus claves está entre las 27 marcadas) y el
  servidor las releva: 11 539 × 2991 recibidas.
  - 80 de los 134 presentes reciben al menos un hash de capa 0 que no usa **ningún** NPC de (a) ni de (e), con capa 1 =
    espera `0x26107D3C`.
  - Ejemplos: `0x56F06B41` (Maid de `0x20000184`), `0x220FA215` (Beggar Boy), `0x8AA3FE54` (Manor Guard).
  - 51 solo reciben hashes de ese tipo.
  - Los otros 54 solo reciben hashes genéricos (andar o esperar, los mismos que usan (a) y (e)).

**Qué falta:**
- El **enlace** a la etapa (`0x43B4B0`: nodo `+0x28` y referencias del nodo `+0x120` → `0xA53CC0([[actor+0x68]+0x40]+0xE8)`).
- **[E]** Sin esas referencias, la acción de escena que llega relevada (agacharse, barrer, apoyarse…) no tiene con qué
  resolverse: pose A o de pie.

**Siguiente arreglo:**
- **Reintentar `guest_sched_bind` después de la carga, no solo en el 2335.**
  - Guardar en una cola acotada (≤ 512) los candidatos `not called` y las claves de escena en X con NPC sin enlazar.
  - Reintentarlos desde un punto del hilo principal ya usado por el mod para llamar al juego: el gancho por fotograma
    `0x8B6144` de `guest_seq_play` (antes de `0xA953C0`).
  - Puerta: `M+0x231 == 3`. Su primer tick fue en t 53,70; mejor tras el 2762 del invitado (t 59,68).
  - Presupuesto: ≤ 16 llamadas por fotograma.
  - El mismo contrato que hoy: `0x8AE1A0` → nueva comprobación → `0x9F2FA0(comp, &clave)` → `0x1434880`.
- Lo que está arreglando el otro agente (la búsqueda de `0x8AE1A0`) **no basta solo:**
  - `0x9F2FA0` vuelve a llamar a `0x8AE1A0` para el **dueño** (sale en `0x9F30FB` si falla);
  - en t 44–48 esa búsqueda falla también para el Kliff.
  - Hay que hacerlo cuando el cliente ya resuelve actores, es decir después de la carga.
- **[E]** Hipótesis de por qué falla: `0x8AE1A0` exige un estado del actor (`actor+0x5E & 0x10`, cerrojo del gestor) que
  los actores del campo aún no tienen mientras se restauran. Para confirmarlo: registrar `+0x5E` de los candidatos en
  la línea `not called`.
- Medida en la prueba: `GuestSchedBind summary` con `BOUND` ≈ 134+ y `still 0` ≈ 0.

**Fracción rota [E]:**
- 51–80 de los 134 presentes (40–60 %) reciben poses de escena que, sin enlace, no se ven como en el anfitrión.
- Los otros ~54 caminan o esperan y pueden parecer normales.
- Es la familia que mejor explica «guardias y gente en pose A en la plaza de Hernand»: los 29 «Hernandian Guard» y los 62
  «Stranger» de la entrada están aquí, y la plaza es donde empezaron los dos jugadores.

## 4. Familia (c): la etapa nunca se creó — los 21 «no stage in X»

**[V]**
- Los 21 2335 de la instantánea con `NO stage with that key in X+0x148 after 0xA9FEE0`:

  | Escenas | Recursos |
  |---|---|
  | `0x20000018` | `farm_0007_01` |
  | `0x20000168`/`169`/`16A`/`16B` | `monster_creature_region` |
  | `0x2000016E` | `miseenscene_0002` |
  | `0x20000172` | `npc_small_0015` |
  | `0x20000175` | `shepherd_city_00_0001_01` |
  | `0x2000017A`/`0x20000182` | `npc_small_0019` |
  | `0x2000017B`–`0x20000181` | 7 × `sit_lean` |
  | `0x20000183` | `farm_0007_02` |
  | `0x20000186`/`187` | `farm_0005/0006` |
  | `0x2000018A` | `hedgehog` |

- **No es la deduplicación por *wake spot*:** `GuestSchedLoad summary`: `0x1862A80 … found 32, missing 21`,
  `VERIFY: missing after our load 21`, y las líneas `[lookup miss] … found no file (… its state: LOADED)` de esos mismos
  recursos.
- Cronología:
  - carga de 35 ficheros en t 44,560–44,575 (hilo de red, `0x1864140` y en el acto `0x38AD90`);
  - manejador del 2335 en t 47,80 (hilo principal);
  - `[state]` en 21:55:42 → **`container (files 15)`**.
- Sobreviven:
  - los 12 recursos nombrados por NPC ya presentes (cuyos manejadores de IA retienen el fichero): `AC00B3BB`, `6FF7E5A6`,
    `DA8B15D0`, `A6BD9C7B`, `AD5D3465`, `6C4DE020`, `B9F63F22`, `57360156`, `79AF6B66`, `5FB728ED`, `F56759DA`, `268D4087`;
  - los 3 cargados en t 44,81 (`8FADCDF8`, `54AC45AA`, `B1AB5208`).
- El resto lo expulsa el barrido de inactividad (**[E]** a los ~3 s sin referencia).
- NPC afectados (33: 4 tipo 3, 29 fauna):
  - escena `0x20000175`, 30 NPC: «Herder», 18 «Sheep», 10 «Goat»;
    - `0xB0100B2B`–`B39` en t 278;
    - `0xB0101DB7`–`DC5` en t 584;
  - `0x2000017A`: 2;
  - `0x2000017D`: 1 (`0xB0100C3F`).
- Su vf13 sí encontró al dueño, pero no la etapa (`owner found`, sin `instance`).
- El 2335 de esas claves no se volvió a mandar. El recurso del pastor se recargó en t 277 al nombrarlo un 2005, pero la
  **etapa** sigue sin existir.
- Las escenas `sit_lean` y `npc_small_0019` se crearon más tarde con **otras claves** (segunda tanda, t 282, ya con el
  fichero presente). Por eso la taberna salió bien.
- Los 2126 de la segunda tanda no tocan estas claves.

**Qué falta:**
- Que el fichero siga vivo hasta que corra el 2335 que lo nombró.
- Un reintento de la creación si aun así falta.

**Siguiente arreglo (`guest_schedule_load`):**
1. **Retener el handle:**
   - no llamar a `0x38AD90` al cargar por un 2335;
   - guardar el handle en una tabla acotada (≤ 64) con el recurso y la clave de escena;
   - soltarlo en el hilo principal en el punto de después `0xC0B626` (ya instalado por `guest_sched_bind`), cuando pase
     el 2335 con ese recurso, o a los 30 s si nunca pasa.
   - `0x38AD90` es atómico (`+0x10`/`+0x14`): soltar desde el hilo principal es seguro.
   - La etapa creada retiene su fichero (el contenedor se quedó en 15 de forma estable de 21:55 a 21:59).
2. **Red de seguridad:** el gancho `0xC0B5C1` ya ve `resource`. Si `0x186A810` dice «ausente» justo antes de `0xA9FEE0`,
   contarlo y anotarlo (`would miss`). Si hiciera falta, cargar ahí en el hilo principal (el cuerpo ya está verificado;
   0,04–2 ms por fichero, como en el hilo de red).
3. **[E] Alternativa del anfitrión:** si el invitado dice «NO stage», volver a mandar ese 2335 del libro de
   `observer_sched_copy`. Requiere un aviso del invitado al anfitrión: es más caro. Preferir 1.

**Fracción rota [E]:**
- 100 % de los afectados: sin etapa, el rebaño y el pastor no tienen su rutina.
- Al final del log quedaban 3 presentes (el rebaño de t 278 salió en t 400). No hay queja concreta del usuario sobre él.
- Puede explicar «tareas tipo barrendero» si alguna de `farm_0005/0006/0007` estaba a la vista.

## 5. Familia (d): secuenciador (2661) con `seq_bodyanim` marcadas

**[V]**
- 58 NPC con clave de secuenciador en la vf13:
  - 42 tipo 3, 12 tipo 4, 1 tipo 5 (caballo de carga), 3 tipo 7;
  - 57 con `res8 → EXITS (no stage node of its own)`, más 1 (`0xB0100C0C` «Pedro», `0x19F`) que sí encuentra al dueño.
- Al final quedan 24 tipo 3 presentes.
- Del anfitrión (`SceneDiag [H1 summary]`): 3534 acciones marcadas (`B+0x281`/`2D3 != 0` → aplicadas en `0x232FF90` y
  **no** relevadas) en **27 claves NPC, todas de esta familia**. Hashes `0x35DD392F`/`0x837487D4`.

| Clave | Nombre | Escena 2661 | Marcadas (antes/con el invitado) | 2991 recibidas | `GuestSeqRestore` |
|---|---|---|---|---:|---|
| `0xB0100077` / `78` | Stranger ×2 | `0x19` | 72/449 cada uno | 0 | `77`: espera; `78`: no |
| `0xB010007E` / `7F` / `80` | Hernandian Guard ×3 | `0x16C` | 49/299, 13/75 | 0 | espera |
| `0xB0100082` | House Roberts's Servant | `0x195` | 49/298 | 0 | no |
| `0xB0100071`–`76`, `0xB010006D` | Wandering Freesword, Matthias, Damiane… | `0x13` | 14/78 | 0 | espera |
| `0xB0100C0F`–`C13` | Wandering Freesword ×5 | `0x1A2` | 0/159 | 0 | espera |
| `0xB0100DF4`/`DF5`/`DF8`/`DF9` | Hernandian Guard ×4 | `0x1BD`/`0x1BE` | 0/135 | 0 | espera (3) |
| `0xB0100C0C` | Pedro | `0x19F` | 0/53 | 0 | no |
| `0xB0100211`/`212` | Servant, Maid | `0x19C` | 2/0 | 0 | no |
| `0xB010007C` | Homing Pigeon | `0x163` | 2/0 (y 115 sin marcar) | 88 | no |

- `GuestSeqRestore summary`: 19 actores `SUBSTITUTED (idle wait)`. Con `L0 0x837487D4`/`L1 0x35DD392F` ni el gráfico
  de su capa ni `[CC+0x70]`/`[CC+0x78]` (0) tienen el hash. Restaurar fue en t 47,6, antes del 2661 (t 47,80) y del
  primer tick de X (t 53,70).
- El secuenciador sí está en X:
  - 34 × 2661 `ROUTED`, 135 × 2981, 8 × 2810;
  - `X+0x231 2 → 3`, `started ~34`;
  - pero `GuestSeqPlay … live calls 0` (`guest_seq_play_live=0`);
  - y los **117 × 2524 `StageOnBranchEventAck`** que recibió el invitado (claves `0x164`…) siguen nativos (`not wanted`).
- Según `npc-raiz-s11…\bodyanim\informe.txt`, el gráfico `SeqBodyAnim` lo instala `0xA79770` en `CC+0x70/+0x78` desde el
  evento de escena: `0xADED90 → 0xADF1A2 → 0xA79770` (o `0xA085100 → 0xA0852F4`), con el recurso, el vínculo y el tiempo
  del evento. Los dos hashes solos no llevan el clip.

**Qué falta:**
- Que en el invitado corra el **evento de la escena** que instala el gráfico dinámico en `CC+0x70/+0x78` de cada NPC
  vinculado.
- Mientras tanto, la acción del anfitrión (marcada, no relevada) tampoco llega.
- Resultado:
  - de pie, en espera (sustituido);
  - o, en los 5 no sustituidos sin ninguna acción (`0xB0100078`, `0xB0100082`, `0xB0100C0C`, `0xB0100211`/`212`),
    **[E]** pose A o la pose del 2005.

**Siguiente arreglo:**
1. **Diagnóstico primero [H]** (sin escribir nada):
   - extender G5 a los vigilados de 2661 para leer `[CC+0x70]`, `[CC+0x78]` y el hash actual de las dos capas cada 5 s;
   - con eso se ve si algún camino del invitado instala el gráfico tras el 2661/2981;
   - mid hooks propuestos en el informe S11: `0xA7987C` (llamada a `0xA7D550` dentro de `0xA79770`) y `0xA79F4C` (escritura
     de `CC+0x78`), contando por actor.
2. **Si nunca se instala:** activar el camino de eventos de X, `guest_seq_play_live=1` (2524 `0xC08178` → X), **solo para
   las escenas de mundo** de esta tabla (`0x13`, `0x19`, `0x16C`, `0x195`, `0x1A2`, `0x1BD`, `0x1BE`, `0x19C`, `0x19F`).
   - Lista blanca por recurso del 2661: así se dejan fuera cámara, interfaz e interacciones personales, como pide S11.
   - Así es el propio `0xADED90` el que llama a `0xA79770` con su recurso y tiempo.
3. **Después, re-solicitar la acción actual:**
   - una vez instalado el gráfico, el anfitrión copia al invitado la última acción marcada de esos 27 NPC como 2991
     (`observer_seq_copy`, la misma cola que los 2335);
   - o el invitado repite la restauración con el gráfico ya presente.
   - Relevar los hashes **sin** el gráfico no sirve: el invitado los volvería a sustituir.

**Fracción rota [E]:**
- ~100 % de los 24 presentes cuando el anfitrión les pone una animación de secuenciador (17 de 24 no recibieron ni una
  2991 en toda la sesión).
- **«El hombre agachado en el anfitrión y de pie en el invitado»** encaja con esta familia: `0xB0100077`/`78` con 449
  acciones marcadas cada uno, sustituido por espera.
- Los guardias `0x7E`/`7F`/`80` y `DF4`–`DF9` también explican guardias quietos en la plaza.
- La «mujer del balcón» puede ser la Maid `0xB0100212` (`0x19C`) o una Maid de (b). **[H]** Sin vídeo no se puede atar.

## 6. Familia (e): sin clave de escena

**[V]**
- 133 tipo 3 (23 presentes al final): Stranger 13, Fisherman 3, Punter 2, Wandering Freesword 2, Gambling Den Guard,
  Wagon Driver…
- 1324 fauna (105 presentes): Grey Parrot 29, Pigeon 18, Sparrow 14, Crow 11…
- Su vf13 no tiene clave de secuenciador (6389 llamadas `without a sequencer key`).
- Solo acciones relevadas (2991), con los hashes más comunes (`0xE851DFDF`, `0xE3FDDDEB`, `0x1DF97512`…).

**Qué falta:** nada visible. El usuario no se queja de ellos.

## 7. Cruce con lo que vio el usuario

| Observación | Familia más probable | Base |
|---|---|---|
| Mujer con cesta que camina: bien | (a), o (b) con hashes genéricos | los (b) genéricos andan bien; sin vídeo no se ata la clave |
| Taberna, sentados, apoyados y variados: bien | **(a)** | `sit_lean` y `armwrest_spectator` creados en t 282 y enlazados por la vf13 nativa |
| Hombre agachado → de pie | **(d)**, si no (b) | `0xB0100077`/`78` (`0x19`), 449 `seq_bodyanim` marcadas, sustituidos por espera |
| Guardias y gente en pose A en la plaza de Hernand | **(b)**, y (d) para algunos guardias | 29 Hernandian Guard + 62 Stranger de la entrada sin enlazar; guardias `0x16C`/`0x1BD`/`0x1BE` en (d) |
| Mujer en un balcón | (b) Maid de `0x20000184`, o (d) Maid `0x212` | — |
| Tareas tipo barrendero | (b) con hash de escena; posible (c) de `farm_*` | 80/134 de (b) con poses de escena |

## 8. Orden de los arreglos

1. **(b) Reintento del enlace tras la carga.**
   - Es la familia más grande (134 presentes) y lo que se ve en la plaza.
   - Va sobre el arreglo de `0x8AE1A0` del otro agente, que no basta en t 47,8: el dueño tampoco se resuelve.
   - Un reintento acotado desde `0x8B6144` con la puerta `M+0x231 == 3` / 2762.
2. **(c) Retener el handle del cargador hasta `0xC0B626`.** Es un cambio pequeño en `guest_sched_load` y evita los 21
   «NO stage». Sin él, el reintento de (b) tampoco ayuda a las escenas sin etapa.
3. **(d) Diagnóstico de `CC+0x70/+0x78`** y luego 2524 → X con lista blanca de escenas de mundo. Es el más caro y el de
   más riesgo (S11 §4: efectos personales).
4. Medida de éxito en la siguiente prueba de dos PC:
   - `GuestSchedBind summary`: `BOUND` cerca de los candidatos y `still 0` ≈ 0;
   - `GuestSchedLoad summary`: `missing after our load 0`;
   - `GuestSchedRoute [census]`: `bound (comp+0x318)` de los vigilados cerca de los de horario;
   - G5 de (d) con `CC+0x70 != 0` tras su 2661;
   - y en vídeo, la plaza de Hernand comparada con el anfitrión.
