# Pose congelada del anfitrión en la pantalla del invitado (junto al «Beggar»): causa raíz

Build 1.0.0.2976. Solo lectura: no se compiló, no se desplegó ni se abrió el juego. Scripts en
`scratchpad\pose\` (`actor_tl.py`, `find_name.py`, `rec2005.py`, `dumprec.py`), venv `-I -B`.
Etiquetas: **[V]** verificado en logs/capturas, **[E]** inferencia fuerte, **[H]** hipótesis.

Sesiones:
- **HOY** = `backups\p8-2inst-s43-activo` (marca 20:10:24.238). Captura del invitado t0 = 20:06:32.114; del anfitrión t0 = 20:06:15.194.
- **MAÑANA** = `backups\p8-2inst-pose-anfitrion` (marcas 08:37:19.9, 08:51:50.4 y 08:54:13.9; la 1 se pulsó con el
  invitado aún cargando, las útiles son la 2 y la 3). Captura del invitado t0 = 08:35:37.628; del anfitrión t0 = 08:35:19.968.

---

## 0. Resumen

1. **Las posiciones del anfitrión llegan y se aplican [V].** El sondeo de divergencia del invitado (memoria del actor
   frente al canal) solo se dispara durante los dos saltos del anfitrión a la posada (9,6 s y 5,1 s). Después queda
   por debajo de 1,5 m hasta el final. En la marca, el actor está donde está el anfitrión de verdad: a 1,5 m del
   invitado. Lo que falla es la **pose**, no la posición.
2. **La UPDATE GUARD de 0x232D5C0 y Puppet no intervienen [V].** En las dos sesiones: `null actions 0 (remote 0) |
   guard skips 0 keys 0`, ninguna línea `update guard: holding`, `remote update guard: update skipped 0`, `animation
   skipped 0`, `empty layer guarded 0`. La guarda sí se aplicaría a personajes de jugador (solo exige
   `control != usuario local`), pero no se activó ni una vez.
3. **La entrada simultánea no es la causa común [V].** Hoy los dos entraron a la vez (20:07:22.659 y 20:07:22.721) y
   el invitado recibió del anfitrión un 2005 «aparcado» en (0, 1000, 0) a las 20:06:55. Pero ese actor se borró
   (2516 a las 20:07:17.13) y se volvió a crear con un 2005 nuevo y correcto a las 20:07:25.77. Por la mañana el
   invitado entró 95 s después que el anfitrión, y el síntoma salió igual.
4. **Lo que sí comparten los dos casos, y solo ellos [V correlación]:** la escena **`cd_seq_boss_rematch_matt`**
   (recurso **`0x9586FFA4`**). Es la escena de la revancha contra Matthias, que en esas dos sesiones estaba en su
   sitio de la taberna, en (-10680.44, 616.68, -3846.53), entre los dos mendigos.
   - El servidor la prepara (2661) para el Kliff del anfitrión. El invitado la enruta al gestor X de ese Kliff
     (`guest_seq_route=1`) y la reproduce (`guest_seq_play=1`: tick de X y X+0x231 = 3). El Kliff del anfitrión queda
     enlazado como «jugador» de la escena (rol `0x20000002`).
   - En la marca la escena sigue viva en el invitado (hoy `0x464`; por la mañana `0x440`).
   - Este recurso aparece en las 2 sesiones con el síntoma y en ninguna de las otras 5 capturas de dos copias. En
     `s43-probe` el anfitrión estuvo 3 min a menos de 15 m del mismo sitio, pero Matthias estaba en otra parte
     (-10592.6, 637.9, -3744.3) y la escena no se creó.
   - Es exactamente el riesgo que ya está escrito en `p8-impl-opcion-a.md` §11.2 y `p8-s8-escenas.md` §7: «el Kliff
     del anfitrión como jugador de la escena en el invitado».
5. **Solo por la mañana hay además un «mendigo» de verdad [V].** El anfitrión **sí interactuó con un mendigo**
   (`B0100314`, «Beggar», en (-10674.58, 616.54, -3846.47)):
   - 2671 `InteractionPreemptionStepReq`, 2720 `InteractionDoStepReq` (id `0xAA`) y 2916 `InteractionDoStepSuccessAck`
     a las 08:36:46.4, 52 s antes de que entrara el invitado;
   - el único 2005 del anfitrión que recibió el invitado (08:37:38.6) lleva un bloque opcional con la clave de ese
     mendigo, que no aparece en ningún 2005 de hoy.

   Hoy no hay ningún paquete de interacción (2671/2720/2663/2916/3000) en ninguna de las dos capturas.
6. **Causa raíz más probable [E]:** el invitado ejecuta escenas personales del anfitrión (la revancha de Matthias)
   sobre el Kliff remoto del anfitrión. De forma nativa, un tercer cliente nunca ejecuta las escenas del gestor de otro
   personaje: el mod lo fuerza (opción A + `guest_seq_play`). El mecanismo exacto de la pose (qué pista la pone) **no
   está medido**: ninguna sonda lee el estado de animación del Kliff remoto en la marca. Primero hay que medirlo con la
   prueba A/B y la sonda del §5. El bloque de interacción del 2005 de la mañana queda como causa secundaria [H].

---

## 1. Cronología

### 1.1 HOY (s43-activo)

| Hora | Lado | Hecho |
|---|---|---|
| 20:06:44.4 / 20:06:47.1 | anf. / inv. | Los dos en carga, aparcados en (0, 1000, 0) |
| 20:06:55.038 | inv. | **2005 de A0100001 con pos (0, 1000, 0)** (`Capture: ignoring the default character load`), 2723 A0 0 / A1 `26107D3C` (wait_default) en (0, 1000, 0), 2322 |
| 20:06:57.5 | inv. | 2991 A1 `26107D3C` en (0, 1000, 0); ChangeAIState ×2 |
| 20:07:17.130 | inv. | **2516 RemoveActorAck A0100001**, después 2174 MoveFieldAck del propio invitado (entra en el campo del anfitrión). `GuestSeqPlay`: «X's character is not in the client actor table any more» |
| 20:07:22.659 / .721 | anf. / inv. | **«Local player: in world» en los dos (entrada simultánea)** |
| 20:07:25.772 | inv. | **2005 nuevo de A0100001 en (-6726.45, 477.77, -3046.45)** (la partida del anfitrión); restore OK (L0/L1 `26107D3C`); 2661 de 0x115/0x116/0x117/0x13A |
| 20:07:29.814 | inv. | `GuestSeqPlay [3]`: **X+0x231 2 → 3** (reproduce lo precargado con el Kliff del anfitrión como jugador) |
| 20:07:32.7–20:08:07 | inv. | 2991 del anfitrión sentado (padre B010018D) y caminando por (-6724, -3059) |
| ~20:08:05 | anf. | Salto a (-10626.43, 621.26, -3813.26) (posada; padre B01008A3: levantarse) |
| 20:08:08–20:08:17.9 | inv. | Divergencia #1: memoria (-6725.11, …) frente a canal (-10626.43, …), 3976 m durante 9,6 s; luego se corrige |
| 20:08:25 | inv. | 2661 0x2F1 **recurso 9586FFA4** (primera pasada junto a Matthias); 2810 a los 11 s |
| ~20:08:36 | anf. | Segundo salto a (-10626.43, 621.26, -3813.26) |
| 20:08:37.2–20:08:42.3 | inv. | Divergencia #2: 64,8 m durante 5,1 s; luego se corrige |
| 20:08:54.486–.507 | inv. | **2981 + 2661 0x464, recurso 9586FFA4, bindings `0x20000004→B0101214` (Game Mechanic), `0x20000002→A0100001`, `0x20000006→B0101313` (Matthias)**: «created», enrutado a X |
| 20:08:54.999 | inv. | `GuestXVeto`: stage **'cd_seq_boss_rematch_matt'** (key 0x464): BlockInteraction on (would), UIStageGuide VETOED |
| 20:08:55.8–20:08:59.0 | inv. | 2991 del anfitrión: A1 `00F967ED`, A0 `25DC7FB9`, A1 `26107D3C`, A1 `333C8CC8`, **A1 `F070444E` (la aplica: L1 cambia)**, A0 `25DC7FB9` |
| 20:08:58.839 | inv. | **Último 2697 MoveActorAck**: (-10678.34, 616.62, -3848.16). El anfitrión se queda quieto |
| 20:09:02–20:10:30 | inv. | Solo acciones de espera: A0 `25DC7FB9`, `874F689B`, y **`ECF9B7D2` + A1 `26107D3C` cada ~11,5 s** (esperas de inactividad); pos (-10678.35, 616.62, -3848.20) |
| 20:10:24.238 | ambos | **MARCA 1.** Anfitrión (-10678.35, 616.67, -3848.20); invitado (-10679.85, 616.74, -3848.54) = 1,5 m; Matthias a 2,6 m del anfitrión; Beggar B01003A2 (-10674.58, 616.54, -3846.47) a 4,2 m. Canal del invitado: «last remote (-10678.35, 616.67, -3848.20)». La escena 0x464 sigue viva (sin 2810) |

### 1.2 MAÑANA (pose-anfitrion)

| Hora | Lado | Hecho |
|---|---|---|
| 08:36:03.97 | anf. | En el mundo (2005 propio en (-6726.45, 477.77, -3046.45)) |
| **08:36:46.42–.49** | anf. | **2671 → 2663, 2720 → 2916: interacción id `0xAA` con B0100314 «Beggar»** (Give Coin con toda probabilidad) |
| 08:37:38.6 | inv. | En el mundo (95 s después del anfitrión: **no simultáneo**) |
| 08:37:38.623 | inv. | **Único 2005 de A0100001 en toda la sesión**, en (-10671.34, 616.40, -3850.29). **Lleva la clave B0100314** (record +0x2D84). El mendigo B0100314 no llega al invitado hasta las 08:37:50.770 |
| 08:37:51.02 | inv. | 2661 0x2B1, **recurso 9586FFA4**, `0x20000002→A0100001`, `0x20000006→B010069A` (Matthias en (-10680.44, 616.68, -3846.53)) |
| 08:37:51.662 | inv. | `GuestSeqPlay [3]`: X+0x231 2 → 3, +0x1B0 = 14 escenas precargadas |
| 08:37:53.9 | inv. | 'cd_seq_boss_rematch_matt' (0x2B1): BlockInteraction on / UIStageGuide VETOED |
| 08:38:13.7 | inv. | 2991 del anfitrión junto a los mendigos (-10673.7, -3848.6): A0 `25DC7FB9`/A1 `B441F461`, rotaciones, A1 `CE35CA5D` |
| 08:38:21.5–08:38:26.9 | inv. | BlockInteraction off; salto del anfitrión a la posada (-10626.43, …); divergencia #1 57,9 m durante 4,6 s y se corrige |
| 08:38:34.46 | inv. | 2661 **0x440, recurso 9586FFA4**, Matthias B01010C9 en el mismo sitio: **viva hasta el final** |
| 08:44–08:54 | inv. | Anfitrión quieto en (-10675.01, 616.54, -3851.37): solo `ECF9B7D2` + A1 `26107D3C` cada 11,5–18 s; 2697 con velocidad 0 |
| 08:51:50 / 08:54:13 | ambos | **MARCAS 2 y 3.** Anfitrión (-10675.01, 616.59, -3851.37); invitado (-10677.26, 616.56, -3852.71) = 2,6 m; sin divergencia |

Las acciones `25DC7FB9`/`ECF9B7D2`/`874F689B`/`26107D3C` aparecen en las 15 sesiones de dos copias (ECF9B7D2: en 11
logs del invitado, en carros, mov y tenderos) sin que se viera este síntoma. **No son de interacción con un mendigo**.
Ninguna acción del flujo 2991 del anfitrión tiene objetivo (+0x16D = 0 en todas).

---

## 2. Respuestas a lo pedido

### 2.1 Qué recibió el invitado sobre 0xA0100001 y dónde está [V]

- **2005:** hoy 2 (aparcado a las 20:06:55, borrado por 2516, y el real a las 20:07:25 en -6726); por la mañana 1 (a
  las 08:37:38, en el sitio). No hubo ningún 2005 más del anfitrión después, ni tras los saltos a la posada.
- **2723:** uno solo, hoy, con el 2005 aparcado (wait_default en (0, 1000, 0)).
- **2991 / 2697 / 2687:** llegan sin interrupción. Hoy hay 133 2991 y 1395 2697 en total; el último 2697 es de las
  20:08:58.839, cuando se para. Las posiciones de los 2991/2697 coinciden con el `Local: pos` del anfitrión (último
  (-10678.35, 616.62, -3848.20) frente a (-10678.35, 616.67, -3848.20)).
- **Se aplican:** las divergencias solo se ven en los saltos y se corrigen (`END … back within the threshold`), y el
  cambio de L1 a `F070444E` se ve aplicado a las 20:08:59 (sonda de MountSync).
- **Acción en la marca:** el anfitrión envía esperas (A0 `ECF9B7D2`, A1 `26107D3C`). Ninguna es de interacción y ninguna
  tiene objetivo. **No hay ningún hash de interacción con el mendigo en el flujo de acciones.**
- **El NPC:** «Beggar» B01003A2 (hoy) y B0100314/B0100315 (mañana), en (-10674.58, 616.5, -3846.47) y
  (-10675.50, …, -3846.36). Matthias está en (-10680.44, 616.68, -3846.53), a 6 m de ellos; el anfitrión está entre
  medias.

### 2.2 Qué módulo o camino deja al actor congelado

- `GuestS3` UPDATE GUARD (`src/mod/session2/guest_s3.cpp:212-284`, decisión `guest_s3_logic.h:377-390`): **sí se
  aplica a personajes de jugador**, porque `remote_controlled = local_user != 0 && control != local_user`, y el Kliff
  del anfitrión tiene control 0x90100000. **Pero 0 activaciones en las dos sesiones [V]:** `GuestS3 diag off guard ON
  … null actions 0 (remote 0) | guard skips 0 keys 0 recovered 0 stuck 0`. `guard_report()` registraría cualquier
  clave retenida aunque el diagnóstico esté apagado, y no hay ninguna.
- `guest_remote_update_guard`: `update skipped 0 post-update 0` [V].
- `Puppet` (solo en la build de la mañana): instalado; `animation skipped 0`, `empty layer guarded 0` [V].
- `RemoteInterp`: los 0xA usan el retardo del juego (200 ms). No retiene nada [V].
- `RemoteMotion`: divergencias solo en los saltos. Lo de escalada (`climb FORCED`) son 4 entradas a las 20:08:48–50,
  lejos de la marca [V].
- `GuestBodyAnim`: las llamadas a BodyAnim sobre actores que no son NPC (10 por sesión) se dan todas antes de que el
  anfitrión llegue junto a Matthias. Hoy hay 0 llamadas nuevas desde las 20:08:04 [V]. La lista de vigilancia solo
  admite claves 0xB (`guest_seq_route.cpp:488`), así que **no se sabe** a qué actor fueron esas 10. En el invitado los
  2524 de X van a M (`guest_seq_route_2524=probe`), y por ese camino no se construye BodyAnim (`p8-impl-secuenciador-bodyanim.md` §0).
- **Camino del mod que sí interviene en los dos casos [V]:** `GuestSeqRoute` (2661 → «ROUTED to the actor's own
  manager X») y `GuestSeqPlay`:
  - `ticks 7191 … slot 0 character 0xA0100001`;
  - `3 written x1` a las 20:07:29.8 y a las 08:37:51.7.

  Así se ejecuta en el invitado `cd_seq_boss_rematch_matt` con el Kliff remoto como rol `0x20000002`. `GuestXVeto`
  veta UIStageGuide pero deja pasar `SetInteraction` (`guest_x_ui_veto_allow=SetInteraction`), y no tiene ningún
  manejador para lo que la escena haga sobre el cuerpo del jugador enlazado.

### 2.3 ¿Por qué el mendigo?

- **Mañana [V]:** el anfitrión interactuó de verdad con el mendigo B0100314 (2671/2720 id `0xAA` → 2916) a las
  08:36:46, antes de que el invitado entrara. El 2005 que recibió el invitado lleva un bloque opcional con esa clave.
  En el 2005 de hoy el mismo hueco está vacío:
  - mañana, record +0x2D81: `ff 01 00 | 14 03 10 b0 | ff ff ff ff | 00 00 00 00 | ff×8 | 00 00 00 00 | 01 00 10 a0`;
  - hoy, record +0x2D7A: `ff 00 00 | 00 00 00 | ff×8 | 00 00 00 00 | 01 00 10 a0`.

  [E] Es el estado de interacción, o la «última interacción», del personaje que guarda el servidor. Que el cliente
  del invitado lo restaure al crear la copia y la deje en la pose de «Give Coin» es [H]: no hay ninguna lectura del
  componente de interacción de la copia. El usuario no lo recuerda, pero la captura del anfitrión lo muestra (con
  toda probabilidad pulsó interactuar al pasar).
- **Hoy:** no hubo interacción ni bloque. El «mendigo» es la ubicación: el Kliff queda congelado entre los mendigos y
  Matthias, cuya escena de revancha (personal del anfitrión) se está reproduciendo en el invitado con ese Kliff como
  jugador [E].
- **Descartado [V]:**
  - acción de un NPC aplicada al actor equivocado: todos los 2991/2723 con clave A0100001 son del anfitrión, y no hay
    ninguna clave cruzada;
  - partida guardada del anfitrión: el 2005 de carga de hoy (en -6726) no lleva el bloque.

### 2.4 La pista del usuario (entrada simultánea)

- Hoy fue simultánea y el invitado recibió la instantánea del anfitrión mientras cargaba: (0, 1000, 0), 2723
  wait_default y 2991 en (0, 1000, 0) [V].
- **No sobrevivió:** el 2516 de las 20:07:17.13 borró ese actor (el invitado cambió de campo con su 2174) y el 2005 de
  las 20:07:25.77 lo recreó con datos reales; la restauración fue correcta [V].
- Por la mañana el invitado entró 95 s después [V]. La simultaneidad no es la causa común. Solo cambia cuándo se
  fuerza X a 3 y qué escenas se reproducen de golpe.

---

## 3. Causa raíz

| # | Afirmación | Nivel |
|---|---|---|
| 1 | Las posiciones llegan y se aplican; el actor no está «anclado» en otra posición | V |
| 2 | La UPDATE GUARD, la guarda de update remoto y Puppet no actuaron | V |
| 3 | El estado aparcado del 2005 de la carga simultánea no persistió | V |
| 4 | En las 2 sesiones con el síntoma, y solo en ellas (0 de 5 capturas más), el invitado ejecuta `cd_seq_boss_rematch_matt` (9586FFA4) sobre X del Kliff del anfitrión, con ese Kliff como rol 0x20000002, viva en la marca | V |
| 5 | Lo que congela la pose es esa escena personal ejecutada en un tercer cliente (lo fuerzan `guest_seq_route=1` + `guest_seq_play=1`), algo que el juego nativo nunca hace con el gestor de otro personaje | **E** (correlación 2/2 frente a 0/5, y riesgo ya documentado; falta el mecanismo) |
| 6 | Qué pista de la escena pone la pose (acción de inicio, IK/look-at, gráfico en CC+0x70, alineación) | H |
| 7 | Solo mañana: el bloque de interacción (mendigo B0100314) del 2005 contribuye | H |
| 8 | Las esperas `ECF9B7D2` se quedan atascadas por sí solas | H débil (aparecen en 11 sesiones sin el síntoma) |

---

## 4. Arreglo de raíz propuesto

### 4.1 Medir primero (A/B, sin código)

En el ini del invitado (`[session2]`), la lista de exclusión que ya existe
(`guest_seq_route.cpp:1249`, `DenyList<32>::parse`):

```
guest_seq_route_deny=8CD771B1,9586FFA4
```

Hace lo mismo que con la cinemática del campamento: el 2661 de esa escena no se crea en ningún gestor del invitado,
y sus 2981/2524 se quedan en M. Si con esto la pose no aparece y sin esto sí, la afirmación 5 queda verificada.

Plan B, más amplio: `guest_seq_play=probe` (X queda en 2 y no se reproduce nada; se pierden las escenas de NPC del
anfitrión).

### 4.2 Sonda (solo lectura) para el mecanismo (afirmación 6)

1. `src/mod/session2/guest_seq_route.cpp:487-488` (`watch()`): admitir una clave 0xA **solo si viene en el ini**
   (`guest_seq_watch=A0100001`). Ahora exige `(key >> 28) == 0xB`. Con eso, `GuestBodyAnim [G5 body] 0xA0100001` sale
   cada 5 s cuando cambia, con CC+0x70/+0x78, +0x23A y las acciones de las capas. Así se ve si una escena le monta un
   gráfico propio, o si L0/L1 se quedan fijos mientras llegan 2991. El ini tiene que ponerla primero: hoy la lista
   se llenó (64).
2. `src/mod/session2/guest_bodyanim_diag.cpp:126-136` (`on_bodyanim`, entrada 0xA79770 ya verificada): para claves que
   no son 0xB, registrar la clave y el clip en las primeras N llamadas. Ahora solo se cuentan. Así se sabe de quién son
   las 10 llamadas que no son de NPC.
3. Al pulsar Ctrl+F12, una línea con el estado de la copia de cada jugador remoto:
   - L0/L1 (hash, cur/play), CC+0x70/+0x78 y método;
   - TS frente al canal;
   - lista de escenas vivas de X que la enlazan (id, recurso, rol).
4. Solo para la causa 7: en `guest_s3_on_host_packet` (que ya recorre los records del 2005), contar los records 0xA
   de otro usuario con la bandera de interacción a 1 y su clave. Antes hay que localizar en Ghidra el serializador
   de ese bloque del record de jugador (hueco +0x2D7A/+0x2D81 en esta build). No hay que reescribir nada sin el campo
   mapeado.

### 4.3 Arreglo (cuando la sonda confirme la afirmación 5)

- **R1 (inmediato):** dejar `9586FFA4` en `guest_seq_route_deny` en los inis del invitado (kit). Cubre esta escena.
- **R2 (general, código):** en el punto de enrutado del 2661 (gancho existente del manejador 0xC0612A en
  `guest_seq_route.cpp`), no enrutar a X las escenas **personales del anfitrión**: las que enlazan su Kliff
  (A0100001) en rol de jugador (`0x20000001/2`) y además no hacen falta para el mundo del invitado (revanchas,
  minijuegos, recompensas, misiones: los prefijos `cd_seq_boss_rematch_`, `cd_minigame_`, `cd_seq_reward_` y
  `cd_seq_quest_` aparecen todos en los logs de X). Nueva clave `guest_seq_route_personal=0|probe|1`:
  - probe cuenta y escribe `would keep in M` con el recurso;
  - el criterio debe ser por recurso o por clase medida en el 2661, no por nombre de etapa (el nombre solo se conoce
    tras crearla).

  Mantener las escenas de NPC (`cd_seq_funcnpc_*`, `cd_seq_contents_weather_*`), que son el objetivo de la opción A.
- **R3 (solo si la causa 7 se confirma):** con el campo del 2005 mapeado, que el relevo hacia el invitado ponga a 0 el
  bloque de interacción de los records 0xA de otro usuario. Ya hay precedente: `DockedGimmicks` reescribe +0x12 en
  2005. Medirlo antes con el modo probe.

---

## 5. Cómo probarlo con dos copias en un PC

1. Copia de seguridad de `save` y `save_cdmp_guest` antes de empezar. Las dos copias como el 14-10 (variables de
   Steam y papeles e inis separados). Inis de `s43-activo` con la sonda §4.2 (`guest_seq_watch=A0100001,…`).
2. El anfitrión va a la taberna de Hernand cuando Matthias está en (-10680, 617, -3847), entre los mendigos (en las
   dos sesiones con el síntoma estaba ahí; en `s43-probe` no). Se queda quieto a 2–3 m de él al menos 2 min.
   Confirmar en `guest.log` la línea `GuestXVeto … 'cd_seq_boss_rematch_matt'`.
3. Variantes, con una marca Ctrl+F12 al ver la pose y captura de las dos ventanas (CopyFromScreen, 2560×1440):
   - **A:** inis actuales, invitado entra después (como por la mañana) → se espera la pose;
   - **B:** igual con `guest_seq_route_deny=8CD771B1,9586FFA4` → se espera que no haya pose;
   - **C:** B, y además el anfitrión usa «Give Coin» con el mendigo antes de que entre el invitado → aísla la causa 7;
   - **D:** A con entrada simultánea → confirma que la simultaneidad no importa.
4. Leer:
   - `GuestBodyAnim [G5 body] 0xA0100001` (¿CC+0x70 ≠ 0 o L0/L1 fijos?);
   - `analyze_capture.py <guest.bin> scenes A0100001` (¿0x9586FFA4 vivo?);
   - `RemoteMotion divergence`;
   - y, en C, el contador de records 0xA con bandera de interacción.
5. Criterio: la pose aparece en A y en D y no en B, y en A el `[G5 body]` de A0100001 cambia al crearse la escena
   9586FFA4 → afirmación 5 verificada y R1/R2 justificados. Si aparece también en B → volver a las afirmaciones 7 y 8.
