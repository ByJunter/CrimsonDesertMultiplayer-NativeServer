# P8 · Arquitectura del observador: qué le falta al invitado y dónde se decide

10 de octubre de 2026, rama `opcion-d`. Build **1.0.0.2976**, direcciones en **RVA** (base `0x140000000`). Solo
lectura: no se tocó `src/`, no hubo commit y no se lanzó el juego. `.xtls` solo se leyó, sin parches ni ganchos.

**[V]** = comprobado: capturas o logs de la sesión 8, o instrucciones (capstone sobre
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` y, en `.xtls`, solo dentro de rangos con RUNTIME_FUNCTION).
**[E]** = inferencia con apoyo. **[H]** = hipótesis sin medir.

## 0. Datos y método

| Ref. | Archivo |
|---|---|
| HC | `C:\Emulador CD\backups\p8-sesion8-host\cdmp_packets_20261010-103235.bin` (cliente propio del anfitrión, 883 638 registros) |
| GC | `C:\Emulador CD\backups\p8-sesion8-shadow\cdmp_packets_20261010-093514.bin` (cliente real del invitado, 553 448 registros) |
| HL / GL | `cdmp.log` de cada carpeta |

**Reloj [V]:** `t(HC) = t(GC) + 174,72 s`. Es la mediana de 120 889 paquetes S>C idénticos byte a byte en las dos
capturas: 2642, 2665, 2516, 2005, 2981, 2335…, todos entre 174,70 y 174,72 s.

**Ventana:** el invitado está en el campo del anfitrión de HC 226,0 a 950,0 s, que equivale a GC 51,3–775,3 s. La puerta
de `observer_seq_copy` se abre en HC 225,72 s y el logout del invitado es a los 953,7 s.

**Formato del paquete [V]:**
- `u16 opcode | u8 0xFF | u16 longitud | payload`;
- en los Ack de actor, la clave va en `payload+0`;
- un 2005 es un registro: `u16 len | u8 tipo | u32 clave | …`, con el nombre en UTF-16 en el registro `+32`.

**Herramientas:**
- Scripts en el scratchpad de la sesión (no se conservan): lector CDPK, inventario por opcode con emparejamiento exacto
  por multiconjunto de bytes, recuentos C>S, claves de 2005 y de enlaces 2661, y un comprobador de escritores.
- `C:\Emulador CD\build\research\appearance\broadcast_scan.txt`: el buscador de destinatarios de cada escritor
  (731 sitios).
- `cdpe`/`xref` y `observador/scripts/pedis.py`.

**Ghidra:** no se abrió. No hizo falta, porque los cuatro sitios de `.xtls` que importaban (2195, 2335, 2981 y 2218)
están en rangos con RUNTIME_FUNCTION y se leyeron con capstone. No hay copia que borrar y el proyecto original quedó
intacto.

## 1. Resumen

1. **El conjunto de actores es el mismo [V].**
   - En la ventana, el anfitrión recibe 2005 de 4588 claves y el invitado de 4603; 4577 son comunes.
   - Las 11 claves que solo tiene el anfitrión llegan en el logout. Ninguna clave del viaje del invitado falta en el
     cliente del anfitrión.
   - Los relevos de IA y movimiento llegan 1:1: el invitado recibe 96 640 × 2697, 57 331 × 2991, 35 755 × 2404 y
     2017 × 2723.
   - **Lo que falta no son actores. Es el estado de los sistemas del mundo que el cliente dueño simula en el gestor de
     su personaje.**
2. **Lo que no llega por red [V].** Se reduce a cuatro grupos:
   - (a) las **creaciones previas** de escenas y stages del dueño. En su entrada, el anfitrión recibió 350 × 2195,
     43 × 2335, 36 079 × 2264, 147 × 2165, 18 × 2661 y 19 × 2981; el invitado, 0 de forma nativa;
   - (b) unos pocos **Ack ToTarget en vivo**: 2165 ×633, 2264 ×327, 2218 ×59, 2297 ×58, 2126 ×23, 2167 ×7 y 2909 ×2,
     además de 2661, 2810 y 2322 (los cubre ya la copia);
   - (c) lo que **nadie difunde**: 2921 acciones de NPC con `0x2D3 = 1` y 17 514 `RotateActorReq` de NPC;
   - (d) un **estado de proceso que no es de red**: los ficheros `NPCScheduleData_File`.
3. **Hallazgo nuevo: el cliente no carga los ficheros de horario [V estático + V log].**
   - El contenedor `pa::NPCScheduleManager` (`[exe+0x6C8CDA0]`, un solo objeto por proceso, ctor `0x1861F00` llamado
     una vez) solo lo llena el cargador `0x1864140` (`"sequencer/binary__"`). Sus **6 llamadores directos son todos código del
     servidor** (`0x27EA160`, `0x281B700`, `0x2848CE0`, `0x2849400`, `0x288A970`, `0x292A410`): pools, horarios,
     facción, stages y misiones. Los que llaman a estos también son del servidor.
   - El cliente solo busca: `0x1862A80`/`0x186A810`.
   - En el anfitrión el contenedor tiene 36 entradas. En el invitado está vacío toda la sesión, también después de
     recibir **36 `2335` nativos** (ToNear, taberna, GC 600,1 s): `G2 … empty container 25`, `G1 … FILE NOT LOADED`.
   - **El 2335 no carga el fichero**: `0xA9FEE0` exige que ya esté cargado. Esto corrige `p8-s1-escenas-npc.md` §1.2 y
     `p8-npc-tiendas-animales-escenas.md` §2.3 (§9).
4. **Servidor [V]: un solo punto decide los destinatarios de todo Ack.**
   - Los 478 escritores del barrido empiezan igual: `finder->vf1(finder, &array_TLS(gs:[0x58]→+0x250→+0x2B0))` y,
     después, unidifusión (`session->vf48`) o `0x2773870`, que tiene 509 llamadores.
   - Para la familia del dueño el buscador es `ToTarget([gestor+0x18])`. Su `vf1` es **`0x2A757F0`** (14 instrucciones,
     compartida con `ToControlUser`).
   - Esa `vf1` es el punto único donde añadir la sesión 2, eligiendo por la dirección de retorno, que identifica al
     escritor y por tanto el opcode. Los embudos vf40/vf48/`[field+0x180]` deciden **qué gestor**, no a quién se envía.
5. **Cliente [V]: el «es del dueño» está en un solo dato.**
   - El dato es `Q+0x180`, que escribe el estado 9 (`0x9A5EE2`) a partir del 2174.
   - Lo leen `ClientField::vf48 0x9A5BE0` (15 llamadas directas y, por la vtable, los manejadores de escena),
     `vf40 0x9A5B70` (3 + vtable), el tick `0x8B6150`/`0x8B2660` y el paso a 3 (`0x77D03C`).
   - Para actores sí existe un camino nativo de «no dueño»: el modo 2. Para escenas y stages no existe: el cliente da por
     hecho que el dueño es él.
   - 2508 es un truco de GM que activa o apaga la IA de un actor que ya controlas. 2168 es un estado de propiedad
     (crimen/objetos) **[E]**. Ninguno de los dos transfiere el control.
6. **Recomendación: B (puntos centrales), por fases y empezando por lo ya en curso.**
   - **Ya en curso (A, sin cambios):** el tick de X y su paso a 3 en el invitado. Validan la hipótesis de la que
     dependen B, C y D: que «X en el invitado se comporta como M en el anfitrión».
   - **Fase 1 de B:** el gancho `0x2A757F0` en modo sonda.
   - **Fase 2 de B:** el mismo gancho enviando, con lista de escritores, y la entrega por `take_frame` con la retención
     que ya existe.
   - **Fase 3 de B:** el enrutado del cliente con un solo criterio, la vida de las escenas (`0xAA3E70(X)`) y la carga de
     ficheros de horario en el invitado.
   - **C** solo como experimento corto. **D** solo si se juega con los jugadores separados.
   - La tabla está en §6 y el orden en §7.

## 2. Objetivo 1: inventario con datos (sesión 8)

### 2.1 S>C que recibe el anfitrión y no el invitado, en la ventana [V]

Emparejamiento exacto de bytes. Los recuentos son de la ventana HC 226–950 s / GC 51,3–775,3 s.

| Sistema | Opcode | Anfitrión | Invitado | Comunes | Buscador / escritor (sitio) | Comentario |
|---|---|---:|---:|---:|---|---|
| Secuenciadores | 2661 PrepareSequencer | 27 | 46 | 27 | ToTarget `0x2A4DEF0` (`0x287D003`) | Los 27 en vivo llegan por la copia (opción A); el invitado tiene además 18 de la instantánea y 1 suyo |
| | 2810 DeleteSequencer | 22 | 22 | 21 | ToTarget `0x2A4E4C0` (`0x28797D1`) | Copia A |
| | 2322 PlayAllPreLoad | 0 | 2 | — | ToTarget `0x2999D80` | Uno del invitado y uno copiado |
| | 2981 StageBranchState | 169 | 187 | **169** | **ToNear** `.xtls 0x103FB3CF` | Llega de forma nativa |
| | 2524 StageOnBranchEvent | 145 | 145 | **145** | **ToNear** `0x29346A3` | Nativo; en el invitado cae en M |
| | 2111 UpdateStageChartEvent | 388 | 388 | **388** | **ToNear** `0x2936BF3` | Nativo; en el invitado cae en M |
| | 2196 / 2561 | 10 / 10 | 10 / 10 | 10 / 10 | ToNear | Nativos |
| Horarios | **2335 CreateNPCScheduleStage** | 36 | 36 | **36** | **ToNear** `.xtls 0x1022C155` | Nativo (taberna, HC 774,8 s), **sin efecto** (§2.5) |
| | **2126 DeleteNPCScheduleStage** | 23 | 0 | 0 | **ToTarget** `0x2817CF5` (hay otro sitio ToNear, `0x28182C5`) | La creación es ToNear y el borrado ToTarget: asimétrico |
| | 2909 DeactivateMiseensceneScheduleEvadeObstacle | 2 | 0 | 0 | ToTarget `0x2815FD5` | |
| | 2352 ReserveSchedulePoint | 163 | 161 | 161 | ToNear (`.xtls 0x105DEC61`) | Nativo |
| Stages / misiones del dueño | 2165 AddStageRandomRate | 633 | 0 | 0 | ToTarget `0x2887047` | Payload `A0100001 + id 0xF42xx` **[E: stages de misión]** |
| | 2264 UpdateStageState | 327 | 0 | 0 | ToTarget `0x2886FD4` | Lo mismo |
| | 2203 / 3009 / 2808… | 176 / 162… | 0 / 160… | — | ToTarget | Interfaz de misiones de cada usuario: privada |
| IA (al controlador) | 2218 AiEvent | 59 | 0 | 0 | ToTarget `.xtls 0x105CA55C` | Va al cliente que controla |
| | 2167 PreemptActionPoint | 7 | 0 | 0 | ToTarget `0x2B0110F` | Respuesta a la 2382 del anfitrión |
| Gimmicks | 2297 UpdateGimmick | 58 | 0 | 0 | ToTarget `0x2AABC90` / `.xtls 0x1056B390` | Respuesta al que controla **[E]** |
| | 2900 / 2997 / 2145 / 2620 / 2852 / 2356 / 2665 / 2517 | 2548 / 2122 / 2747 / 99 / 13 / 10 / 33 448 / 24 885 | iguales o más | todos | ToField / ToNear / ToTarget por usuario | **Nativo y multiusuario**: el invitado recibe además su propia instantánea (6127 × 2665 de más) |
| Actores | 2005 / 2516 | 4588 / 4418 | 4603 / 4380 | 4577 claves / 4380 | ToNear del sector | Mismo conjunto. La mitad de los 2005 difiere en un bloque a 1100–1230 B del final (estado dinámico **[E]**) |
| | 2687 RotateActorAck | 129 | 44 | 0 | ToNearExceptOneControl (`.xtls 0x106F32C0`) | **Solo personajes** (`0xA010…`): 0 rotaciones de NPC |
| Tiempo | 2188 / 2093 | 145 / 145 | 143 / 144 | 143 / 144 | ToWorld / ToTarget | Nativo |

Resto:
- Los 1842 × 2953 y 514 × 2945 (Nak de gimmicks bajo demanda) son de cada usuario: el invitado tiene los suyos, 10 750 y
  404.
- 2948 (diálogo) es de cada usuario. 2642 (elemental) se comparte casi entero.

### 2.2 Instantánea de entrada: lo que el dueño recibe al entrar y el observador no [V]

| Opcode | Anfitrión (HC 55–85 s) | Invitado (GC 50,5–80,5 s, su entrada al campo del anfitrión) | Buscador |
|---|---:|---:|---|
| 2195 CreateFactionSpawnStage | **350** | 0 | ToTarget `.xtls 0x101EFD8B`, actor `[r15+0x18]` (actor del gestor) |
| 2335 CreateNPCScheduleStage | **43** | 0 | ToNear: el invitado no estaba |
| 2264 UpdateStageState | **36 079** | 0 | ToTarget `0x2885BD0` |
| 2165 AddStageRandomRate | 147 | 0 | ToTarget |
| 2724 UpdateStageList | 7 | 0 | ToTarget `0x2910A1D` / ToNear `0x291B183` |
| 2203 UpdateMissionUIStateList | 27 | 0 | ToTarget (privado) |
| 2661 / 2981 | 18 / 19 | 18 / 22 | Los del invitado son **la copia A** (snapshot) |
| 2665 / 2517 SyncSceneObject | 6746 / 9 | 6639 / 675 | Nativo **por usuario** |
| 2248 / 2997 / 2900 / 2145 | 39 / 50 / 231 / 109 | 14 / 21 / 41 / 53 | Nativo por usuario **[E: su propia entrada]** |

### 2.3 Lo que no reenvía nadie [V]

- **Acciones con bandera `0x2D3 = 1`:** 2921 `StartActionReq` de NPC del anfitrión en la ventana (más 5 de su
  personaje). El servidor las aplica y no las difunde (`0x2A78D0D`).
  - Son las `seq_bodyanim` que cada cliente reproduce **con su propia escena**.
  - **No se tocan.** Las generará el propio invitado cuando X reproduzca.
- **`RotateActorReq` de NPC:** el anfitrión manda 17 514 y el invitado recibe 0. La regla está en `.xtls 0x106F32C0`.
  Efecto menor: el giro llega dentro de Move y Start.
- **`UpdateAIDestinationReq` 2405 (20 968) y `StartAIFunctionReq` 2291 (8531):** solo existen como estado del servidor.

### 2.4 Estado de proceso, no de red: `NPCScheduleData_File` [V estático, V log]

| Hecho | Evidencia |
|---|---|
| Hay un solo `pa::NPCScheduleManager` por proceso | ctor `0x1861F00`, un llamador `0x93E216` ← `0xACE482` ← `0x616DFE` (arranque); escribe `[0x6C8CDA0]` |
| Los lectores del cliente solo buscan | `0x1869D70`, `0x1869EB0`, `0x15F1140`, `0x1862A80`: todos acaban en `0x186A810` (búsqueda en la tabla hash) |
| **El que carga es `0x1864140`** | construye la ruta `"sequencer/binary__"` (`0x1864288`), busca con `0x186A9A0` y, si no está, carga (`0x18643B0`) |
| **Sus 6 llamadores directos son del servidor** | pools `0x27EA160` (al que llaman `0x27EE3E0`, `0x27E8C90`, `0x27E81E0`, `0x27E9B40` y `0x27F29D0`); horarios `0x281B700` (desde `0x2925760`); facción `0x2848CE0`/`0x2849400` (desde `0x2849FC0`, `0x28431A0`, `0x2844000` y `0x2855C70`); stages `0x288A970` (desde `0x2885BD0`, el de 2264/2165); misiones `0x292A410` (desde `0x2929F20`) |
| El gestor que usa el servidor | `[[0x6D69518 \| 0x6D69520]+0x40]`, según la bandera TLS `+0x1EC` (`0x281B80D`–`0x281B836`). Que sea el mismo objeto que `[0x6C8CDA0]` es **[E fuerte]**, porque solo hay un ctor |
| Anfitrión | `SceneDiag [G2 host] … NPCScheduleManager (… buckets 2, used 36)`, con ficheros cargados |
| Invitado | `GuestScene [G2 summary] … empty container 25` toda la sesión. Ni el viaje a su campo ni los 36 × 2335 nativos lo llenan. 477 claves de NPC con recurso de horario en G1 y 0 resueltas |
| El 2335 no carga | manejador `0xC0AFC0` → `0x8A67E0` → `0x17FA6C0` → `vf48` → `0xA9FEE0` → `0x1862A80` (solo búsqueda) |

**Lectura [E fuerte]:** en un jugador, el cliente encuentra los horarios porque **su servidor local, en el mismo
proceso**, los cargó al crear los NPC. En el invitado el servidor local no simula el mundo del anfitrión, así que el
contenedor nunca se llena.

**Esto no lo arregla ninguna copia de red.** Hace falta que el proceso del invitado cargue esos ficheros, a partir del
recurso que traen el 2335 o el bloque de IA de cada 2005.

### 2.5 Lo que el invitado recibe y no aplica [V]

| Qué | Recuento | Por qué |
|---|---|---|
| 2335 nativos | 36 | Van a M (`vf48`) y `0xA9FEE0` no encuentra el fichero (§2.4). Si se aplicaran, nunca llegaría su 2126 (ToTarget) |
| 2524 / 2111 nativos | 145 / 388 | Van a M, que no tiene esos ids (`p8-s8-escenas.md` §2.2) |
| 2981 nativos | 168 | Enrutados a X (186 con la copia). Sin tick no tienen efecto |
| 2661 copiados | 45 | 43 creados en X y 2 rechazados (`0xA9CE81`, actor enlazado destruido) |
| Relevos de los 71 NPC enlazados a un 2661 | 870 × 2991, 213 × 2404, 16 × 2697 y 11 × 2723, en 39 claves | `+0xCF = 0`: la puerta `0x8B53C0` los deja en reintento y se instalan **0** (`p8-xtls-lectura.md` §2) |
| Login | 12 `login FAILED eErrNoInvalidActionChart` y 24 `RestartLayerAction … NOT FOUND` | Restauración de `seq_bodyanim` sin el gráfico dinámico |
| Enlace de NPC (`vf13 0x9ED290`) | 540 NPC, los 540 salen en `local_res8` | Falta el fichero (§2.4) |

### 2.6 Lo que manda el invitado sobre el mundo del anfitrión (C>S, ventana) [V]

| Req | Invitado (claves `0xB`) | Lectura |
|---|---|---|
| 2556 Move / 2828 Rotate / 3067 RegionInOut / 2758 UpdateGimmick | 14 457 / 17 142 / 15 997 / 2387 | **Casi todo de gimmicks de tipo 7** («Game Mechanic»): 14 207 / 17 124 / 15 967 / 2387. El invitado los simula también. El servidor solo acepta los del controlador **[E]**; el anfitrión manda los mismos tipos |
| 2223 / 2734 de NPC | 601 / 569 | Solo su mascota `B0100334` |
| 2857 SetQuestDialog | 782 sobre 508 NPC | Diálogo por usuario. Ya era así |
| 2100 SyncStageCandidatePostion | 250 (el anfitrión 251) | **Dos clientes alimentan las posiciones candidatas del gestor del dueño** (`vf48`) **[E]** |
| 2591 / 2436 / 2173 | 10 / 1 / 7 | Solo su escena propia |
| 2424 / 2119 / 2682 / 2550 (pools y carreteras) | 121 / 115 / 90 / 1887 | El servidor crea con controlador `[field+0x180]` = anfitrión (`0x27EED63`/`0x27EA44B`) **[E]** |

### 2.7 Alcance por sistema (clases de NPC en el invitado, sesión 8)

| Clase | NPC | Qué les falta | Dónde se arregla |
|---|---:|---|---|
| Fauna, genéricos y gimmicks (sin escena) | ~1900 claves (G1 sin recurso) | Nada de red. Solo la suavidad del modo 2 (`p8-movimiento-remoto.md` F1) | Interpolación |
| **Enlazados a un secuenciador** del dueño (tenderos, taberna, mesa, Damiane…) | **71** | 2661, 2981, 2810 y 2322 (cubiertos), tick de X y paso a 3, 2524 y 2111 a X, `0xAA3E70(X)`, complemento `seq_bodyanim` | A en curso, o B |
| **Horario** (criadas, niños, mercaderes, guardias de patrulla) | **≥477** | **Fichero en el proceso**, las 2335 de antes de la entrada, 2126/2958/2909/2499/2127/2839 en vivo, enrutado a X y tick de horario de X | B (con A sería otra cadena entera) |
| **Facción** (stages `0x4000xxxx`) | ¿10? **[E]** | 2195 (350) y 2130 | B + instantánea |
| Misiones del dueño | — | 2264, 2165, 2724: privadas del anfitrión. Solo cuentan si `0x1AC2830` (escena «del dueño») las consulta en X | No, salvo en C |

## 3. Objetivo 2: puntos centrales del servidor

### 3.1 Cómo decide un Ack sus destinatarios [V]

1. Cada Ack tiene **un escritor** propio: 478 funciones distintas en el barrido.
2. **Las 478** cargan el array del TLS (`gs:[0x58]` → `+0x250` → `+0x2B0`) y llaman a `finder->vf1(finder, &array)`
   dentro de sus primeras ~40 instrucciones. Sin excepción en el barrido de este informe.
3. Después envían:
   - si hay una sola sesión, directamente a ella (`session->vf48` = Send, `kSessionVfSend = 9`);
   - si hay varias, con `0x2773870(ServerNet, opcode, paquete, tamaño, &array)`, que tiene 509 llamadores y recorre el
     array.

   Que se elija por el número de sesiones es **[E]**.
4. **La sesión 2 tiene la vtable del mod.** Todo lo que se le envía pasa por `session2_send` → `take_frame`, donde ya
   viven `observer_hold`, `observer_hold_acks` y los libros.

| Buscador | vtable | `vf1` | Sitios en el barrido | Para qué |
|---|---|---|---:|---|
| ToTarget | `0x5B25438` | **`0x2A757F0`** (= ToControlUser) | 536 (46 por el ctor `0x2A757A0`; el resto en línea) | Una sesión: `actor->vfE8()` |
| ToNear | `0x5B25418` | `0x2A75860` | 152 | Observadores del sector del actor |
| ToNearExceptOneControl / Self | `0x5B25358` / `0x5B25378` | `0x2A759B0` / `0x2A75880` | 9 / 6 | Relevos (2991, 2404, 2697, 2723…). El mod ya los engancha (`relay_fix`) |
| ToField | `0x5B25398` | `0x2A75EF0` → `0x2AFFBF0` | 9 | Todos los usuarios del campo (`field+0x2A0`) |
| ToWorld | `0x5B25318` | `.xtls` | 8 | Eventos globales y hora |
| **ToStage** | `0x5B253B8` | `0x2A75D20` | 2 | **Participantes de la stage**: lista de claves de usuario en `stage+0xA0/+0xA8` → `FindUser` → sesión. Solo lo usa 2253 |
| ToSessionList / ToNearAndParty / ToControlUser | — | — | 3 / 4 / 2 | — |

### 3.2 Quién elige al dueño y quién al destinatario [V]

- Los escritores de la familia del mundo reciben el **actor dueño del gestor** y construyen `ToTarget(actor)`:
  - 2661 `0x287C200`, 2810 `0x2879440` (`[r12+0x18]`), 2264 y 2165 `0x2885BD0` (`[r15+0x18]`);
  - **2195** en `.xtls 0x101EFB30`: `mov rcx,[r15+0x18]` → `vfE8` → `+0x30` del buscador (`0x101EFD76`–`0x101EFDAA`).
- El gestor sale de los embudos: `vf48 0x2B00CE0`, `vf40 0x2B00C70` o las 13 copias en línea de
  `FindUser([field+0x180])` (`p8-sistemas-juego.md` §11.7).
- Por tanto el «dueño» se decide **antes**, al elegir el gestor. El destinatario es siempre «el personaje dueño del
  gestor». **No existe un ayudante "enviar al dueño del campo"**: cada escritor arma su ToTarget.
- No hay ninguna rama nativa de «más observadores» para los gestores de un personaje. ToStage, que la tiene, no lo usa
  ninguno de los escritores de esta familia.
- Los de **ToNear** (2981, 2524, 2111, 2335, 2352, 2411, 2196, 2266, 2892/2359, 2561, 2736, gimmicks…) ya llegan al
  invitado si está en el sector [V §2.1].

### 3.3 Puntos candidatos

| Punto | ¿Central? | Qué ve | Veredicto |
|---|---|---|---|
| **`ToTarget::vf1 0x2A757F0`** | **Sí**: lo pasan los 536 sitios ToTarget, en `.rsrc` y en `.xtls` | El buscador (`+0x10` = sesión destino); la **dirección de retorno** dentro del escritor da el opcode; el array del TLS | **Recomendado.** Un gancho de entrada o de salida al estilo de `relay_fix` |
| Ctor `0x2A757A0` | No: 46 de 536 | Actor destino | — |
| `0x2773870` | No: solo el camino de varias sesiones | Opcode y array | — |
| Embudos vf40 / vf48 / `[field+0x180]` | Deciden el **gestor** | — | Para las Req del invitado (§11.7), no para observar |
| `0x2BC7670` (rama de no dueño `0x2BC7885` / `0x2BC8470`) | Entrada | — | Ahí no hay instantánea nativa (`p8-observador-escenas.md` §5). La instantánea sigue saliendo del libro (sniffer) |
| `0x2A78D0D` (bandera `0x2D3`) | — | — | **No tocar** (§2.3) |

### 3.4 Diseño de la extensión «observador» en `0x2A757F0` (B-servidor) [E]

**Regla.** Después del original:
1. `[finder] == 0x5B25438`: es ToTarget, no ToControlUser.
2. La sesión empujada es s1, la del anfitrión.
3. El retorno pertenece a un escritor de la **lista**.
4. El usuario 2 está en el **mismo** `ServerField` que el actor destino (viaje de observador, época viva).
5. s2 no está ya en el array.

Si todo se cumple, se empuja s2 con el mismo crecimiento nativo (`0x35E3F0`). Gestión de fallos como `finder_fault`.

**Lista de escritores (ToTarget; sitio entre paréntesis):**

| Familia | Opcode → escritor |
|---|---|
| Secuenciadores | 2661 → `0x2A4DEF0` (`0x287D003`), 2810 → `0x2A4E4C0` (`0x28797D1`), 2322 → `0x2999D80` (`0x287F1A5`), 2797 → `0x2A4EE30`, 2224 → `0x2A55EA0`, 2226 → `0x2A58820`, 2140 → `0x2A52150`, 2675 → `0x2A58CE0`, 2222 → `0x2A45600`, 2918/2416 (`0x293801D` / `0x2939C12`) |
| Horarios | 2126 → `0x2A53EB0`, 2958 → `0x2A54CD0`, 2909 → `0x2A552A0`, 2499 → `0x2A54300`, 2127 → `0x2A54800`, 2839 → `0x2A463A0` |
| Facción | 2195 → `0x2A52BC0`, 2130 → `0x2A53060` |
| Dudosos (misiones del dueño) | 2264 → `0x2A56BC0`, 2165 → `0x2A423D0`, 2724 → `0x2A40B30`. Fuera al principio: son 327 + 633 en la ventana y 36 k en la entrada |

**Excluidos:**
- interfaz de misiones e inventario (2203, 3009, 2808, 2635, 2102, 2426…);
- respuestas al que pidió (2297, 2167, 2663, 2916, 2948);
- IA del controlador (2218, 2273, 2933): solo para D.

**Orden:** el paquete añadido entra en `take_frame` de la sesión 2. Si nombra NPC cuyo 2005 está retenido, se le aplica la
misma retención multiclave que hoy se aplica a la copia del 2661.

**Lo que aporta frente a A:**
- los mismos bytes en el mismo hilo y en el mismo orden nativo que al dueño;
- cubre de una vez las ~25 familias, sin libro en vivo ni sniffer;
- el sniffer queda solo para la **instantánea de entrada**: 2661+2981 (hoy), y 2335, 2195 y el último 2126 por escena
  (nuevos).

**Prerrequisito, ya existente:** el filtro de C>S del invitado sobre stages del anfitrión, ampliado con 2276 y 2105
(`p8-xtls-lectura.md` R1).

**Alternativa sin filtro [E]:** con `vf48` por sujeto (§11.7 rango 3) las Req del invitado irían a **su** gestor del
servidor, donde no existen los ids del anfitrión, y no tendrían efecto. Además sus escenas propias (ESC) tendrían su
2810 de forma nativa.

## 4. Objetivo 3: puntos centrales del cliente

### 4.1 Dónde decide el cliente «esto es del dueño» [V]

| Pieza | Qué hace | Consumidores |
|---|---|---|
| `Q+0x180` (`ClientField`) | Clave del usuario dueño. La escribe el **estado 9** `0x77AE70` → `0x9A5D20` (`0x9A5EE2`) desde el 2174 (`P+0x12C`) | `0x9A5BE0` y `0x9A5B70` (búsqueda en cada llamada) |
| `ClientField::vf48 0x9A5BE0` (ranura 9 de `0x55BCEA0`) | `FindUser(Q+0x180)` → personaje → `[[+0x68]+0xA8]+0x18` | 15 llamadas directas: estados de carga `0x779E43`, `0x77A2B5`, `0x77AA6C`, `0x77AE9A`, `0x77D03C`, `0x77D364`, `0x77E08F`, `0x77FD8E`; tick de horario `0x8B268E`; `0x6E3648`/`0x6EC281` (actualización por fotograma `0xA8ACD0`); `0x6F00EC`; `0x952807`; `.xtls 0x9407274`, `0x940B210`. **Más las virtuales** `[rax+0x48]` de los manejadores 2661 `0xC06120`, 2981 `0xC07746`, 2810 `0xC0659B`, 2264 `0xC0E236`, 2322 `0xB87A64`, 2524 `0xC08175`, 2111 (cerca de `0xC08970`), 2335 `0xC0B5BE`, 2195/2126/2958… y el tick `0x8B6102` |
| `ClientField::vf40 0x9A5B70` | Gestor de misiones del dueño | `0x9AD96D`, `0x9ADAB2`, `.xtls 0x99287E7`, virtuales; `.xtls 0x9F8FCE0` (paso a 3, `0x1AC2830`) |
| `0x17FA6C0` → `0x9F8560` | **Siempre** el campo actual del cliente | 622 llamadores. No mira el actor |
| Tick | `0x8B6150` (`0xA953C0`) y `0x8B2660` (`0xAA3350`/`0xAA3E70`): solo el gestor que da `vf48` | — |
| Paso a 3 | Estado `0x14` (`0x77D03C`) → `0xA97B30` → `.xtls 0x9F8FCE0`: solo ese gestor | — |

**Para actores sí hay un «no dueño» nativo [V]:**
- con `actor+0x90 ≠` usuario local va en modo 2 (`0x88E8E0`): interpola el búfer de red;
- los relevos del controlador llegan por ToNearExceptOneControl.

**Para escenas y stages no hay** camino de observador. El gestor de cada personaje existe (X está en el invitado), pero
solo se alimenta, actualiza y reproduce el del dueño del campo.

**Mensajes de control [V]:**

| Mensaje | Qué es | Conclusión |
|---|---|---|
| 2508 ChangeAiControlAck | Solo lo emite `0x2C41360`, la lógica de `AiControlChangeCheatReq 2442`. Exige permiso de GM (componente `+0x130` vf140(3)), que el objetivo no sea un usuario (`+0xA0 = 0`) y que su controlador **sea ya** el usuario de la sesión (`0x2C414E4`–`0x2C41500`). Activa o apaga la IA (`0x206EED0`) y difunde por ToNear | Herramienta de desarrollo: no transfiere |
| 2168 OverriedOwnershipStateTypeyAck | ToNear `0x2A9B670`. Por las cadenas `CheckOwnershipState` es la propiedad de objetos o el crimen **[E]** | No es control |
| 2473 ChangePlayerbleCharacterAck | ToTarget desde misiones (`0x29181B0`) | Cambio de personaje jugable del propio usuario |

**El controlador de un NPC** lo fija el servidor al crearlo (`[field+0x180]` en los pools; `0x2791CD0` en los atados a
stages) y el cliente lo aprende **solo** del 2005.

### 4.2 ¿Puede el invitado no ser dueño (2174 nativo) y cargar? [V/E]

| Paso | Con el 2174 nativo (dueño `0x90100000`) |
|---|---|
| Estado 9 | `Q+0x180 = 0x90100000` |
| **Estado `0x0A`** | `.xtls 0x940B170` → `0x9A5BE0(Q)`. **Cierre** si el usuario `0x90100000` (con `+0x5E & 4`) y su personaje aún no existen en el cliente (`p8-cierre-campo.md`). Sus 2005 llegan **después** del 2549, así que hace falta **adelantarlos**: copiar al invitado el `[U][A0100001]` antes del 2174. No es nativo **[E: alto riesgo]** |
| Nodo `P+0x200` | Creado en el estado 4 con el contexto local. Se buscaría en X (`0x1F6C190`) y probablemente no estaría. Efecto desconocido **[E]** |
| Estado `0x11` | Esperaría `X+0x231 ≥ 2`. El 2322 **propio** del invitado se enrutaría de forma nativa a X (`vf48`). Funcionaría **[E]** |
| Estado `0x14` | `0xA97B30(X)` → `X+0x231 = 3` y la primera ronda. **Sería nativo** **[E]** |
| Después | Tick `0x8B6150(X)`, `0x8B2660(X)` y todos los manejadores a X, **nativos** |

**Qué rompería [E]:**
1. **Las escenas propias del invitado** (ESC «ver objeto», `UseItemReq`):
   - sus 2661/2810/2111 caerían en X;
   - sus NPC buscan en M (`comp+0x2D8` = su personaje);
   - M dejaría de tener tick.
2. **La interfaz de misiones del invitado:** `vf40` le daría las misiones del Kliff del anfitrión en su cliente, que
   están vacías porque los 2724 no le llegan. Además `0x1AC2830` decidiría «reproducir ya» con esas misiones.
3. **Conductor doble:** el invitado mandaría 2767/2591/2436/2100/2105/2276 de las escenas de X. Hace falta el filtro
   del §3.4.
4. **Reconexión, salida y viajes:** dependen de mantener el orden de los 2005 adelantados.

**Variante C′ [H]:** dejar el 2174 reescrito para la carga (S2, que ya funciona) y, ya en el mundo (estado `0x18`),
escribir una vez `Q+0x180 = 0x90100000`. Restaurarlo antes de cualquier viaje o logout (el estado 9 lo reescribe con el
siguiente 2174).
- **Gana, de golpe:** el enrutado, el tick, el tick de horario y `0xAA3E70` sobre X.
- **No da:**
  - el paso a 3 (`0x77D03C` ya pasó);
  - los ficheros de horario (§2.4);
  - los datos (siguen haciendo falta B-servidor o A).
- **Rompe lo mismo que en los puntos 1–3.**
- Para no romper las escenas propias habría que **enrutar al revés**: los paquetes con el actor propio, a M. Son los
  mismos puntos que el enrutado de B, así que **no ahorra ganchos**, y añade un estado global del cliente cambiado.

### 4.3 Punto central del cliente en B [E]

Hoy hay 4 ganchos intermedios por manejador (`guest_seq_route`). Con el criterio de B serían:

1. **Entrada de `0x9A5BE0`**, por la dirección de retorno: la tabla de `call [rax+0x48]` de los manejadores de la familia,
   unos 15 sitios.
2. **El actor del paquete** guardado en una variable del hilo:
   - por un gancho de entrada en `0x8A67E0` (`ClientActorContainer`, que todos esos manejadores llaman antes de `vf48`
     con la clave del payload);
   - o, como hoy, leído en cada sitio.

Si el actor es un personaje distinto del dueño y tiene `ClientSequencerStageManager`, se devuelve X. Los estados de carga
y los ticks no están en la tabla y siguen viendo M.

**Tick (un punto, `0x8B6144`, ya planeado):**
- `0xA953C0(X)`;
- el paso a 3 de X;
- en la fase 2, `0xAA3E70(X, dt)`;
- **no** `0xAA3350(X)`, que duplicaría el 2100.

## 5. Objetivo 4: reparto del control de la IA (D)

- **No hay mecanismo nativo** para pasar el control de un NPC a otro usuario. 2508 no lo hace (§4.1).
  - El servidor fija `actor+0x90` al crear el NPC.
  - El cliente decide el modo con `0x88E8E0` a partir del 2005.
  - La IA local exige el «cerebro» (`0x9245B0` sale con `[[ai+0x48]+8] = 0`), que existe para lo que el cliente controla
    desde su carga (la mascota del invitado: modo 1, IA local).
- **Forma «casi nativa» [E]:** que lo que **crea una Req del invitado** salga con controlador = usuario 2. Bastaría el
  cambio temporal de dueño de §11.7 rango 4 durante sus 2424/2550: 121 + 1887 Req en la ventana.
  - Pero los NPC de stages del dueño (los 13 cálculos en línea de `vf48` en pools y en `0x2791CD0`) seguirían atados a
    las escenas del anfitrión.
- **Forzar el control de NPC ya creados:**
  - escribir `actor+0x90` en el servidor;
  - volver a anunciarlos (2516 + 2005) a los dos clientes para que cambien de modo;
  - redirigir 2218 y 2273 (ToTarget al controlador).

  Es un estado inventado: riesgo alto.
- **Implicaciones:**
  - el anfitrión los vería en modo 2, con los saltos de 1 Hz si no está hecha la interpolación F1;
  - la IA del invitado necesita **lo mismo que el observador**: los ficheros de horario (§2.4) y las escenas en X. **D
    depende de B**, no lo sustituye;
  - acciones `0x2D3`: el invitado como controlador las mandaría y el anfitrión tendría que reproducir la escena (en
    espejo).
- **Única ventaja:** jugadores separados.
  - Hoy cada NPC lo simula el cliente del anfitrión, también si está solo cerca del invitado. Que el cliente del
    anfitrión deje de simular los NPC lejanos es **[H]**.
  - En la sesión 8 estuvieron siempre cerca: **ninguna** clave del viaje del invitado faltó en el cliente del anfitrión
    [V]. Sin medida.

## 6. Objetivo 5: estrategias

Puntos de intervención = ganchos y escrituras nuevos o ampliados.

| | A: pieza a pieza (actual) | **B: puntos centrales** | C: invitado no dueño | D: reparto de IA |
|---|---|---|---|---|
| **Servidor** | Libro por familia (sniffer) + copia en `take_frame`. Hecho para 2661/2981/2810/2322; faltan 2335 (instantánea), 2126/2958/2909/2499/2127/2839, 2195/2130 y 2224/2226/2140/2675… (~6–8 bloques nuevos) | **1 gancho** (`0x2A757F0`) + lista + instantánea por el libro (ampliar a 2335/2195) + filtro C>S (existe; ampliar) | Igual que B (o que A) **más** el adelanto del `[U][A0100001]` si se usa el 2174 nativo | B + escritura de `actor+0x90` + reanuncio + 2218/2273 |
| **Cliente** | Un gancho por manejador (4 hechos + ~10) + tick + 3 + tick de horario + ficheros + complemento | Enrutado (1–2) + tick y 3 (1) + `0xAA3E70` (en el mismo) + ficheros (1) + complemento (1) | 1 escritura (`Q+0x180`) + enrutado inverso (1–2) + 3 + ficheros + complemento | Lo de B + modo y cerebro en el invitado + F1 en el anfitrión |
| **Puntos totales** | **~25–35** | **~7–9** | ~7–9 (C′) / +3 (nativo) | B + ≥4 |
| **Cobertura** | Secuenciadores: sí, con tick y 3. Horarios y facción: tantas cadenas como familias. Lo que no se copie no existe | Todo lo ToTarget de la lista en vivo, nativo y en orden. ToNear ya llega. Instantánea por el libro. Ficheros por el cargador. **Toda la tabla de §2.7** | Lo mismo que B en datos. Rompe las escenas propias y las misiones del invitado | Solo los NPC reasignados. No arregla escenas ni horarios por sí solo |
| **Riesgo** | Medio por pieza, **alto acumulado**: el sniffer va en otro hilo, hay N formatos y deriva entre el libro y el estado real | **Medio:** `vf1` es diminuta y pura; el riesgo está en el cliente (X a 3, conductor doble) y en el cargador de ficheros | **Alto:** la máquina de carga (`0x940B170`), estado global del cliente, misiones del invitado | **Alto:** estado inventado y efecto espejo en el anfitrión |
| **Orden** | En curso: tick y 3 | **Siguiente** | Experimento corto, opcional | Último, si hace falta |

## 7. Recomendación y orden

1. **(Shadow, ya preparado) Tick de X + paso a 3** (`p8-xtls-lectura.md` §5) con la sonda G5 de `+0x3D8`/`+0xCF`.
   - Es la prueba de que X puede ser una réplica de M.
   - Si falla, ni B, ni C ni D sirven tal como están.
2. **B-servidor en modo sonda.** Gancho de salida en `0x2A757F0` que solo **cuente**, por escritor, las veces que «se
   añadiría s2»:
   - con la lista del §3.4 y los dudosos aparte;
   - sin enviar nada;
   - se puede probar en un PC con `cdmp_peer`.
3. **B-servidor enviando.** Sustituye las copias **en vivo** de A; el libro queda solo para la instantánea, ampliada a 2335
   y 2195. Antes, el filtro C>S con 2276 y 2105.
4. **B-cliente:**
   - enrutado por un solo criterio (retorno de `vf48` + actor del paquete) para toda la familia: 2524, 2111, 2335, 2126,
     2195, 2958…;
   - `0xAA3E70(X)`;
   - complemento `seq_bodyanim` (mesa de pulsos y grupo de Damiane).
5. **Ficheros de horario en el invitado:** el grupo más grande, ≥477 NPC.
   - Primero una sonda: confirmar en el anfitrión que `[0x6C8CDA0] == [[0x6D69520]+0x40]` (o `0x518`) y quién llama a
     `0x1864140` en vivo.
   - Después, cargar en el invitado los recursos que nombran los 2335 y los bloques de IA, con `0x1864140` o su camino de
     carga asíncrona.
   - Pide visto bueno: ejecuta código de carga del servidor en el proceso del invitado.
6. **Interpolación F1:** independiente, para la fauna.
7. **C′:** solo como experimento de una sesión, para medir si el enrutado y el tick nativos sobre X dan lo mismo que B.
   **D:** solo si se juega separado y se ven NPC congelados lejos del anfitrión.

**Necesita Shadow:** los pasos 1, 3 (validación), 4 y 5. El paso 2 no.

## 8. Riesgos y abiertos

| Id | Riesgo / abierto | Nota |
|---|---|---|
| R1 | **Conductor doble:** con X reproduciendo, el invitado manda C>S de las escenas del anfitrión, que el servidor aplica en el gestor del dueño | Filtro (existe; añadir 2276 y 2105) o `vf48` por sujeto (§3.4) |
| R2 | **2100 de los dos clientes** sobre las posiciones candidatas del gestor del dueño (251 frente a 250) | **[E]** Puede hacer que el servidor prepare y borre escenas según quién habló el último. Medir: ¿2661/2810 en vaivén? |
| R3 | **Asimetría 2335 ToNear / 2126 ToTarget:** con B, el 2126 llega. Con A, hay que copiarlo | Si no, las escenas de horario quedarían huérfanas en X |
| R4 | `0x2A757F0` también es de ToControlUser | Comprobar la vtable `0x5B25438` en el gancho |
| R5 | Orden 2661 frente al 2005 retenido | `take_frame` ya lo resuelve para la copia: reutilizar la retención multiclave |
| R6 | Las escenas siguen la **posición del dueño**: con los jugadores separados, lo que rodea al invitado no se prepara | **[H]**: entonces haría falta D o `vf48` por sujeto |
| R7 | El cargador de ficheros es código del servidor (gestor elegido por la bandera TLS `+0x1EC`) | Sonda antes de llamarlo en el invitado |
| A1 | ¿Qué hacen en el cliente los 2195 (stages de facción) y qué NPC dependen de ellos? | **[E]** Los «10 sin identificar» de la sesión 6 |
| A2 | ¿Hacen falta 2264/2165 (stages de misión del anfitrión) para que X decida reproducir (`0x1AC2830` lee las misiones del **dueño del campo**, que en el invitado es él mismo)? | Lo decide el paso 1 |

## 9. Correcciones a informes anteriores

| Afirmación | Ahora |
|---|---|
| `p8-s8-escenas.md` §2.2: «2335 / 2264 / 2195 / 2165: 0 del anfitrión» | **36 × 2335 nativos** del anfitrión (actor `A0100001`, ToNear `.xtls 0x1022C155`) llegaron al invitado en GC 600,1–600,5 s, byte a byte iguales [V]. 2264/2195/2165, 0 |
| `p8-s1-escenas-npc.md` §1.2 y §7: «el 2335 o el 2661 provocan la carga del fichero [E fuerte]» | **Falso para el 2335 [V]:** `0xA9FEE0` solo busca. El cargador `0x1864140` solo tiene llamadores del servidor, y el invitado sigue con el contenedor vacío tras 36 × 2335 |
| `p8-npc-tiendas-animales-escenas.md` §2.3: «el 2335 es lo que hace falta para cargar el fichero» | Lo mismo. Hace falta el 2335 (escena en X) **y**, por separado, el fichero en el proceso |
| `p8-sistemas-juego.md` §2.3: `ActivateSpawningPoolReq`/`AutoSpawnRoadReq` «dueño único [E fuerte]» | El invitado manda 121 + 1887 en la ventana [V]. El efecto (controlador anfitrión) sigue **[E]** |
| `p8-observador-escenas.md` §2.3: «buscador de proximidad [E]» para 2981/2524/2111 | **[V]:** ToNear (`.xtls 0x103FB3CF`, `0x29346A3`, `0x2936BF3`). Coinciden 169/169, 145/145 y 388/388 |
