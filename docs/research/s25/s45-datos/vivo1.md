# S45 — Paso 1 en vivo (probe): R-A, S-A…S-D, tienda y cárcel

Build del juego 1.0.0.2976 (PE `0x6AB28F00`). Mod `build 74EF2D22` (= HEAD `74ef2d2`, rama `opcion-d-escenas`).
Datos: `C:\Emulador CD\backups\p8-2inst-s45-probe\` (`guest.log`, `host.log`, `cdmp.guest.ini`, capturas
`cdmp_packets.{guest_20261016-071559,host_20261016-071542}.*`). Análisis de solo lectura; scripts en `s45\vivo1\`:
- `gsp.txt`: todas las líneas `GuestSeqPresent` del invitado, con su número de línea en `guest.log`;
- `wc.txt`: las 58 `WOULD CUT` (etapa, nombre, recurso);
- `names.py`: nombres completos desde `s44\inventario\names.json`;
- `caller.py`, `disp.py`, `evslots.py`, `wrap.py`: desensamblado del despachador de eventos y de las ranuras 97–103 de
  las seis clases de evento (usan `s45\ra-scripts\pe.py`, lectura del exe instalado).

Los números `L…` son líneas de `guest.log`. Las horas `~hh:mm:ss.mmm` son las del QPC (el momento real del evento); la
hora de escritura llega hasta 5 s más tarde.

Identidad: el invitado es el personaje **`0xA0100066`** (usuario `0x90100065`); en todas las líneas sale `this client
0xA0100066, M's 0xA0100066`. El anfitrión es `0xA0100001`, y su gestor X en el invitado es `0x5F3BB716600`.

Ventana del invitado: según `BgSpeed`, estuvo **en segundo plano** de 07:20:02 a 07:20:33 (tienda) y de 07:20:59 a
07:22:05 (cárcel). El usuario la estaba mirando, pero no tenía el foco. Por eso la falta de `MoveActorReq` propios
durante la cárcel (t 316–349 de la captura) **no dice nada sobre la entrada**.

Marcas Ctrl+F12, ancladas con lo que dijo el usuario:
- **MARK 1, 07:20:18.652** (L26083): la tienda. El anfitrión habla con el vendedor de armas.
- **MARK 2, 07:21:43.941** (L34953) y **MARK 3, 07:21:46.982** (L35425): la cárcel, dos marcas durante la cinemática.

En pantalla salió lo esperado en probe: la tienda vacía y la cinemática de la cárcel, igual que antes.

---

## 1. R-A en probe: criterios uno por uno

| Criterio | Resultado | Líneas |
|---|---|---|
| Línea `ARMED` con 25 cambiadas, 2 de mundo, 9 nativas y 80 copiadas | **Cumple.** «24 slots that turn presentation ON to the common's ret 0x359B50 … 1 to the common's own: +0x288 (0x204DDD0); kept native as world effects: +0x1A0 +0x1A8; kept native as they turn it OFF …: +0x130 +0x1B8 +0x1C8 +0x1D0 +0x1D8 +0x1E8 +0x210 +0x228 +0x250; the other 80 copied». 24 + 1 = 25; 25 + 2 + 9 = 36 decisiones; 25 + 80 = 105 ranuras. Verificado: base y PE, tres COL, 36 decisiones, 36 trampolines, ranura 0 y 4 *thunks*. La tabla está en `0x150C0008`. La clasificación cruda coincide: 32 de solo cliente, 3 de cliente y servidor, 1 distinta en el común y 33 fuera de rango | L130, L131 |
| Todos los `WOULD CUT` sobre etapas ajenas con dueño anfitrión y G = X | **Cumple.** Hay **58** líneas `[R-A WOULD CUT presentation]`; el resumen dice `WOULD CUT (probe) x58`. El grep da 59 porque la línea `ARMED` cita el texto. En las 58: `owner 0xA0100001 (this client 0xA0100066)`, `G != M, known X of the owner: yes`, vtable `exe+0x558C080 (client)`, `started +0x28B 0` e hilo de P1 = hilo de fotograma (20128 = 20128). En las 58 `S-A P1 FOREIGN` el G es el mismo, `0x5F3BB716600` (X), con `[proc+0x10] == the stage` y un id de línea de tiempo válido | `wc.txt`; L3305 (la primera), L44837 (resumen) |
| «G = M» en 0 | **Cumple.** `foreign: [proc+8] != G 0, [proc+0x10] != stage 0, G = M 0` | L44836 |
| Ningún `NOT cut` (hilo, `[proc+0x10]`, vtable, M aún no visto, X desconocido) | **Cumple.** 0 líneas `NOT cut`, 0 `RESTORED`, 0 `WRITE FAILED`. `P1 hooked calls 60: not foreign x2; WOULD CUT (probe) x58`. Las 2 «not foreign» son escenas **propias**: `cd_common_abyssoneloading`, `0xA010006600000001/02`, de la carga y el viaje del invitado, 07:17:52–07:18:44 | L44837 |
| S-A2 / P2-1: activaciones desde etapas **no ajenas** de X y «antes de P1» | **Cumple: 0 y 0.** `before their P1 0 (R-A's gap), after it 101`; `the same handlers from X's NOT foreign stages: none`. De las ranuras que se quedan nativas solo se llamó `SequencerCamera (+0x1C8) foreign before P1 0 / after 1, not foreign 0`. Matiz: en esta sesión **no existe ninguna etapa de X que no sea ajena** (60 P1 = 58 ajenas + 2 propias en M). Las tres fuentes de un encendido sin cortar (etapa de mundo de X, hueco antes de P1, etapa rechazada) valen 0. Las 101 son InputBlock 5 + UIControl 4 + ChangePhasePanelTag 2 + UIShowMinimap 1 + UIStageIcon 1 + UIStageGuide 88 (cuadra con `GuestXVeto summary`, L44840) | L44836, L44837, L44840 |
| Coste | Ver §1.1 | |

### 1.1 Coste de cada sonda (resumen de las 07:22:48, L44836)

| Sonda | Llamadas | Coste |
|---|---|---|
| S-A + juicio de R-A (P1) | 60 en ~7 min | **No se mide** (no hay QPC en P1). Son unas pocas lecturas SEH por inicio de etapa: despreciable por frecuencia |
| S-A2 (dentro de los manejadores de `guest_x_veto`) | 273 | No se mide aparte |
| S-B `CameraSelectorEventData` | 11 | media 1,9 µs, máximo 2,9 µs, total 0,02 ms |
| S-B `SequencerPlayerCameraPreset` | 4 | media 1,9 µs, máximo 3,1 µs, total 0,01 ms |
| S-B `LetterBoxControl` / `PostProcess` / `AIDialog` | **0** | — |
| S-C `StartLoadingSequencer` | 10 (+1 a las 07:22:57, al cerrar) | media 3,3 µs, máximo 9,7 µs, total 0,03 ms |
| S-D | **0: no se instaló** (§4.1) | — |

Las entradas de evento son **frías**: 15 eventos de presentación en 7 minutos. No hay problema de coste para R-B.
Además: `lines limited 0, ring lost 0, over budget 0`, y `GuestSeqPlay` hizo 13 177 fotogramas sin fallos.

### 1.2 Veredicto del paso 2

**Sí, es seguro pasar a `guest_seq_foreign_present=1`.** Todos los criterios de la revisión se cumplen, sin ningún caso
dudoso. Condiciones:
- **copia de las partidas antes**: el paso 2 escribe el vptr del procesador;
- criterios de aborto de §3.1.8;
- en el paso 2, mirar que los minijuegos (`seotda`, `rockpaperscissors`, `armwrest`, `timeattack`), `reward_spear_base`
  y las tiendas siguen animados. `ActivateMiniGame` `+0x128`, `Reward` `+0x258` y `HireMercenary` `+0x200` **no tienen
  gancho de S-A2**, así que no sé si se llamaron. En esta sesión su paso por R-A es [H].

Dos salvedades que no bloquean:
1. La métrica P2-1 es 0 porque X solo tuvo etapas del anfitrión. Si en otra sesión X recibe etapas con otro dueño, habrá
   que volver a mirarla. Con el diseño «solo se cortan encendidos», un encendido que se escape sigue encontrando su
   apagado nativo.
2. **Recomiendo arreglar S-D antes del paso 2** (§4.1, un cambio de una línea en la verificación). Así la misma sesión
   también mide la cámara para R-B. No afecta a R-A.

---

## 2. Tienda y cárcel

### 2.1 Qué escenas fueron

| Caso | Etapa en X | Recurso y nombre completo (`names.json`) | P1 / R-A | Fin |
|---|---|---|---|---|
| **Tienda** (MARK 1) | `0x2E4`, línea de tiempo id **245** | `0xA7132561` `cd_seq_funcnpc_smithy` | P1 a las 07:19:31.530 (L14936), `WOULD CUT` L14937. La escena del herrero ya estaba viva, en reposo; la tienda son sus ramas 2981: estados 1 (07:20:16.371), 2 (07:20:17.371), 7 (07:20:21.482) y 0 (07:20:24.455) | sigue viva |
| Previa a la cárcel | `0x339`, id 316 | `0xC542144F` `cd_seq_quest_crime_police_hernand_0300` | 07:21:21.107, L30624/L30625 | 2810 a las 07:21:36.221 |
| **Cárcel** (MARK 2 y 3) | `0x33A`, id **318** | `0xE5B490F1` `cd_seq_quest_crime_police_wakeup_prison_hernand_0100` | 2661 a las 07:21:40.852; P1 a las **07:21:42.098** (L34743/L34744) | 2524 + 2810 a las **07:21:59.419** (L36255; 07:22:03 escrita) |
| Junto con la cárcel | `0x434`, id 326 | `0x0E61D7F9` `cd_seq_quest_progression_call_0100` | 07:21:42.326 | `SequencerCamera` de X al borrarse, 07:22:33.367 (L43588) |
| Matthias | `0x32A`, `0x338`, `0x4A1`, `0x4B2`; `0x498` | `0x9586FFA4` `cd_seq_boss_rematch_matthias`; `0xF2A314F0` `cd_seq_memory_hernandgibbet_0001` (07:22:27.7) | todas `WOULD CUT` | — |

### 2.2 Qué ejecutó en el cliente del invitado y qué habría cortado R-A

**Tienda (`0x2E4`), de 07:20:16.37 a 07:20:24.46:**

| ~Hora | Vía A (ranura del procesador) | Vía B (evento de la línea de tiempo) | Vía C |
|---|---|---|---|
| 16.372 | `InputBlock` VETADO | **`SequencerPlayerCameraPreset`** (L25959, FOREIGN, id 245) | `SetInteraction remove` ×3 (permitido), `SetQuestDialogAICategory` (probe, corrió) |
| 17.371 | `InputBlock` ×2 y `UIControl` ×3 VETADOS; `ChangePhasePanelTag` (probe, **corrió**) | **`SequencerPlayerCameraPreset`** (L25960) | — |
| 21.482 | `UIControl` e `InputBlock` ×2 VETADOS; `ChangePhasePanelTag` (corrió) | **`SequencerPlayerCameraPreset`** (L26666) | `BlockInteraction on` (corrió) |
| 24.455 | — | — | `BlockInteraction off`, `SetQuestDialogAICategory`, `SetInteraction add` ×3 |

- **R-A las habría cortado todas en la vía A:** `InputBlock` `+0x150`, `UIControl` `+0x158` y `ChangePhasePanelTag`
  `+0x160`. Hoy `ChangePhasePanelTag` corre: el panel es lo único de la vía A que aún llega a la pantalla del invitado.
- **Fuera de R-A:** los 3 `PlayerCameraPreset` (vía B) y la vía C (`SetInteraction`, `BlockInteraction`,
  `SetQuestDialogAICategory`).
- En la tienda **no hubo ningún `CameraSelectorEventData`**.

**Cárcel (`0x33A`), de 07:21:42.10 a 07:21:59.42:**

| ~Hora | Vía A | Vía B |
|---|---|---|
| 42.098 | P1 (`WOULD CUT`) | — |
| 42.120 (+22 ms) | `UIStageIcon` (probe, **corrió**), `UIStageGuide` VETADO | **`CameraSelectorEventData`** (L34745, FOREIGN, id 318, `playing +0xDC 1`: la línea de tiempo acaba de arrancar), `ev+0x38 0x6463000000000106` |
| 50.249 (+8,2 s) | — | **`SequencerPlayerCameraPreset`** (L35673, FOREIGN, id 318) |
| 59.419 | `UIStageGuide` VETADO (al borrarse) | — |

- **R-A cortaría las dos de la vía A** (`+0x1F8`, `+0x238`).
- **Fuera de R-A:** el `CameraSelector` del arranque y el `PlayerCameraPreset` de los 8 s.
- **No hay ningún evento de bandas, posproceso ni diálogo de IA, ni en la cárcel ni en toda la sesión.**

**Otras escenas.** El único `SequencerCamera` de X (ranura `+0x1C8`, nativa en R-A porque lleva la dirección en los
datos) es de `quest_progression_call_0100`, al borrarse (07:22:33.367). Es lo mismo que en la regresión de S44.

**Escenas propias del invitado.** Los 9 `CameraSelector` y los 6 `StartLoadingSequencer` de `cd_common_abyssoneloading`
salen `timeline of an own stage`. El juicio separa bien lo propio de lo ajeno también en la vía B.

### 2.3 El vínculo evento → etapa: **demostrado [V]**

En las 15 líneas S-B y S-C con etapa:
- la «raíz» de `[ctx+8]` está a **profundidad 0**: `[ctx+8]` **es** el objeto de línea de tiempo (vtable
  `exe+0x5C75F30`), sin recorrer `+0x98`;
- su `+0x1D8` es exactamente el `stage+0x298` que P1 anotó: tienda 245 = `0x2E4`; cárcel 318 = `0x33A`; propias 10 y 146
  = `0xA0100066…01/02`;
- `+0x1D1 = 1` (listada) y `+0xDC` = 1 al arrancar y 2 en marcha.

Los 5 casos `timeline of no stage P1 saw` (ids 1 y 8 en la carga, 500 al cerrar) son líneas de tiempo de carga sin etapa
de secuenciador. Ninguno es de X.

**Llamador [V en log y código].** Todos los S-B y casi todos los S-C vienen de `exe+0x36574CD`: el retorno del
`call [rax+0x318]` (ranura 99, inicio) en `0x36574C7`. El resto viene de `0x36575D6`: el retorno de la ranura 99 en
`0x36575D0`, seguida de inmediato por la ranura 101 (fin) en `0x36575E6`.
- Las dos llamadas están en el fragmento con `.pdata` `0x36573B5–0x3657600`, en `.rsrc`.
- **No son** los sitios `0x377FC03…` que proponía `nativo.md` §3.2. Ese despacho genérico no apareció.
- No hubo ningún llamador distinto en `LetterBoxControl`. B1 (ranura 67) no se pudo comprobar porque esa sonda no tuvo
  llamadas.

### 2.4 ¿Por qué vía se encendió la cámara?

- **Ni vía A ni otra ranura de etapa** (las dos escenas):
  - en la cárcel, ningún manejador de cámara de etapa;
  - en la tienda, ninguno de cámara (sí `InputBlock`, `UIControl` y el panel);
  - `guest_x_camera_veto` solo vio el `SequencerCamera` de `0x434` al borrarse.
- **Por la línea de tiempo (vía B) [V en eventos, E en el efecto]:**
  - **cárcel:** `CameraSelectorEventData` 22 ms después del P1, cuando la línea de tiempo arranca (`+0xDC 1`), y un
    `PlayerCameraPreset` a los 8 s;
  - **tienda:** solo `SequencerPlayerCameraPreset` ×3, uno por cada rama 1/2/7 del herrero.
- **No demostrado:**
  - que esos eventos **sean** los que cambian la cámara del invitado. S-D no corrió, y no se puede descartar que el
    sistema de cámara lea directamente la pista `CameraComponent` de la línea de tiempo activa;
  - el `.paseq` del herrero tiene además `CameraSetFollowTargetEventData`, y el de la cárcel
    `CameraParameterPresetEventData` y `CameraEffectComponent`. Ninguna de esas clases tiene sonda.
- **Modo cinemática/exclusivo** (`Timeline.set_cutScene/set_exclusive` del `.paseqc`): no hay sonda. No hay dato sobre
  la entrada (la ventana no tenía el foco).

### 2.5 Qué queda fuera de R-A (resumen)

- **Vía B:**
  - `CameraSelectorEventData` (cárcel);
  - `SequencerPlayerCameraPreset` (tienda ×3, cárcel ×1);
  - lo que no tiene sonda: las pistas de cámara, `CameraSetFollowTarget`, `CameraParameterPreset`, el modo cinemática;
  - `StartLoadingSequencer` de etapas ajenas: **0** (solo propias o de carga). Con `deny=8CD771B1` no se probó la carga
    de facción.
- **Vía C:** `SetInteraction` (permitida), `BlockInteraction` (5 on / 5 off) y `SetQuestDialogAICategory` (38), con los
  vetos actuales.
- **Ranuras nativas a propósito** (P2-1): `SequencerCamera` (1 llamada, al borrarse `0x434`), `FadeInOut` y los
  apagados.

---

## 3. R-B: diseño propuesto con los datos medidos

### 3.1 Qué está medido y qué falta

- **Medido:**
  - el vínculo `[ctx+8]` = línea de tiempo y `+0x1D8` = `stage+0x298`, en el mismo hilo principal (T20128) que P1;
  - las únicas clases de presentación que dispararon en escenas ajenas: `CameraSelectorEventData` y
    `SequencerPlayerCameraPreset`, ambas en la ranura 99 (inicio);
  - el coste: menos de 3 µs por evento, 15 eventos en 7 minutos.
- **Falta:**
  - S-D, para saber si saltar esos inicios basta para que la cámara no cambie;
  - las clases sin sonda;
  - la ranura 100.

### 3.2 Puntos y forma de «no hacer nada» [V en código]

Las ranuras 97 y 98 de las seis vtables de evento son `0x359B50` = `ret 0`, el cuerpo vacío `void` de la clase base
(`evslots.py`). Son las mismas que usa R-A. Por tanto, **saltar la ranura 99 en su entrada** (`rip = [rsp]`,
`rsp += 8`, sin tocar nada más) equivale a la implementación base. Los dos llamadores vistos (`0x36574C7` → `jmp`
epílogo, y `0x36575D0` → recarga `rcx` y llama al fin) **ignoran `rax`** [V].

| Punto (ya enganchado por S-B) | Ranura | Tipo | En 1 |
|---|---|---|---|
| `0x34CFC20` `CameraSelectorEventData` | 99 (inicio) | *thunk* hoja de 5 B (`jmp .xtls`), sin `.pdata` | si la etapa es ajena: retorno en el mismo marco (`rip = [rsp]`, `rsp += 8`) |
| `0xB27880` `SequencerPlayerCameraPreset` | 99 | entrada con `.pdata` (`0xB27880–0xB27DDE`); en el primer byte aún no se ha guardado nada (el `mov [rsp+0x10],rbx` escribe en el *home space*) | ídem: retorno en la entrada, sin ejecutar el prólogo |

**El juicio**, idéntico al de R-A más el enlace medido:
1. `root = [rdx+8]` con SEH, y su vtable == `exe+0x5C75F30`;
2. `id = [root+0x1D8]`;
3. etapa = la de la tabla de P1 con ese id. Hoy la tabla es por `seq`: hace falta un índice id → entrada, o un recorrido
   acotado, aceptable con menos de 1 evento/s;
4. la etapa tiene que tener **veredicto R-A = Cut/Would** (dueño personaje ajeno, M conocido, G ≠ M, G = X conocido del
   dueño);
5. hilo = el hilo de fotograma de `guest_seq_play`.

Si algo falla, el evento corre nativo y sale una línea `NOT skipped` con el motivo.

Clave nueva `guest_seq_foreign_timeline = 0 | probe | 1`:
- en probe, línea `[R-B WOULD SKIP]`;
- en 1, `[R-B SKIPPED]`.

### 3.3 Qué se corta y qué no

- **Se corta:** solo la ranura 99 de `CameraSelectorEventData` y de `SequencerPlayerCameraPreset` en líneas de tiempo
  de etapas ajenas de X.
- **Se queda nativo:**
  - **la ranura 101 (fin)** de las dos (`0x34CFF80` y `0xB27E40`, *thunks* a `.xtls`; corrección posterior: su código
    se lee y tolera un inicio saltado, ver `s45-escenas-ajenas.md` §3.2.3 punto 5);
  - **la ranura 100** de las dos:
    - `0x34CFDE0` sube por `+0x98` hasta `+0x1E0`;
    - `0xB27DE0` toma la entrada actual de la cámara `[[cw+0x118]+0x40]-0x28` → `0xAAFC00` → `+0x340` → `vf 0xA0`
      sin argumentos, que parece un reinicio o apagado [E];
  - la ranura 103.
- **No se toca:**
  - **`LetterBoxControl`, `PostProcess`, `AIDialog` ni `StartLoadingSequencer`:** 0 llamadas en escenas ajenas. Sin
    datos, no se engancha. Si hace falta, `LetterBox` es como `FadeInOut`:
    - la ranura 99 `0xC38F30` lleva la dirección en `[ev+0x51]`;
    - la ranura 101 `0xC38F40` es `xor edx,edx` = apagado;
    - solo se cortaría la 99 cuando `[ev+0x51] != 0`.
  - **el despachador `0x36573B5`:** es un fragmento a mitad de función, compartido por todas las líneas de tiempo
    (CLAUDE.md: nada de ganchos a mitad de función).

### 3.4 Cómo no dejar nada pegado

- **Solo se cortan los inicios; todos los fines y la ranura 100 quedan nativos.**
- **El motor ya llama al fin sin el inicio [V en código]:**
  - `0x365740D` → `0x36575D6`: con `r15b == 3` y sin la bandera 8, solo la ranura 101;
  - `0x36574DA` → `0x36575D6`: con `[ctx+8] == 0` y la bandera 8, ídem.

  Los fines toleran un inicio que no corrió: **[V]** tras leer su código (corrección posterior, revisión de R-B P3-3:
  `s45-escenas-ajenas.md` §3.2.3 punto 5). Ninguno de los dos es opaco.
- En probe de R-B, contar por evento **inicio saltado → fin visto**. Para eso habría que enganchar la ranura 101 en modo
  conteo; las dos son *thunks* de 5 B: hay que verificarlas como `0x34CFC20`.
- **Criterio de aborto:** cámara del invitado pegada, pantalla negra o bandas tras una escena ajena; un fin sin
  inicio que deje la cámara en otro preajuste.

### 3.5 Qué no está demostrado (y cómo cerrarlo antes de escribir R-B)

1. **[H] Que saltar esos dos inicios quite la cámara.** La pista `CameraComponent` puede aplicarse sin eventos.
   → Arreglar S-D (§4.1) y repetir la tienda y la cárcel: ver si la entrada de la cámara `[[cw+0x118]+0x40]` cambia
   con el `CameraSelector` (22 ms después de P1) o sin él.
2. **[H] Clases sin sonda:**
   - `CameraSetFollowTargetEventData` (herrero);
   - `CameraParameterPresetEventData` y `CameraEffectComponent` (cárcel);
   - `SubtitleGroup`, `ChangeFocusActor`.

   → Añadir sondas de la ranura 99 (y 100) con la misma verificación (firma única, COL y ranura 99 → entrada).
3. **[H] La ranura 100** de `CameraSelector` y `PlayerCameraPreset`: ¿se llama en escenas ajenas y enciende algo? →
   Sonda de conteo.
4. **[H] El modo cinemática/exclusivo y el bloqueo de entrada:** no hay campo identificado. En la próxima prueba, con
   la ventana del invitado **con el foco** durante la cárcel, intentar moverse: así se separa «sin entrada» de «sin
   pulsar».
5. **[E] El tipo de retorno de la ranura 99:** se deduce del `ret 0` de la base y de los dos llamadores vistos. Los
   envoltorios `0x37808C0` y `0x3780A20` (eventos diferidos, `vf298 & 1/2`) no llaman a esas ranuras directamente
   (`wrap.py`).
6. **[H] La carga de facción** (`StartLoadingSequencer` de etapa ajena): no se probó (deny `8CD771B1`).

---

## 4. Otras anomalías

### 4.1 S-D no se instaló (defecto del mod, no del juego)

- **Síntoma:** `WRN GuestSeqPresent: S-D NOT active: the code at exe+0x5E4E6E (7 bytes) is not build 2976's` (L138).
  El resumen da `S-D OFF: frames 0`.
- **Causa [V]:**
  - `kSdCode` (`guest_seq_present.cpp:106-108`) verifica los bytes originales de `0x5E4E6E` (`kBlockCall`, la llamada
    de `InputBlock`);
  - pero `guest_x_ui_veto=1` ya había puesto **su** gancho *mid* de 7 B justo ahí (`guest_x_veto.cpp:179`; instalado a
    las 07:15:54.522, L136, antes de la verificación de S-D a las 07:15:54.984).
- **Arreglo:**
  - quitar `{0x5E4E6E, kBlockCall}` de `kSdCode`: solo documenta que `InputBlock` llega a `0xA98AD0`; el offset que S-D
    lee, `G+0x230`, ya lo verifica `{0xA98D3E, kBlockSet}`;
  - o comparar con los bytes originales que conserva el gancho de `guest_x_veto`.

  Pasará lo mismo en el paso 2, que lleva los mismos vetos.

### 4.2 Fin de la sesión: FreezeWatch al cerrar el juego (benigno)

- **Invitado:** `Game error: "Terminate 시작."` a las 07:22:59.289. Después, FreezeWatch #1.1–1.4 (328–546 ms, de
  L45472 a L45498).
- **Anfitrión:** lo mismo a las 07:23:05.788 (L66420), FreezeWatch a las 07:23:06.
- Las dos pilas son idénticas (`exe+0x13656D3, exe+0xB5AB56D…`).
- Antes: `TravelAck … 2679 LogoutCharacterToFieldServerAck` (anfitrión, 07:22:59.095) y el S-C de la línea de tiempo
  500 a las 07:22:57.966 (pantalla de carga de la salida).
- Es el cierre normal, no un cuelgue durante la prueba.

### 4.3 Resto

- **0 ERR** en los dos logs.
- **WRN del invitado:**
  - 95 `GuestXVeto [interface]` VETOED y 1 `[dialogue]` (`TimelineEvent` VETOED en `reward_spear_base`, 07:18:31.597);
  - 14 `RemoteMotion` divergence: la red y los saltos del anfitrión al ir a la cárcel, máximo 3 980 m en la #1 a las
    07:18 (viaje);
  - los de arranque habituales: redirección de partidas, `GuestS3` filtro de crashpad reencadenado, `WeatherSync X
    CHANGED` a la misma dirección;
  - `GuestFastTravel` (probe, 07:18:38.748): el invitado viajó al punto 6. Es su propio viaje, el de las escenas
    `abyssoneloading`.
- **WRN del anfitrión:** los de siempre: partidas, `NpcFamilyVehicles` en probe, `User2Stages` sin armar, 4 `family
  order ORPHAN` (07:18:18) y 1 `User2Road [viajero partido]`. Nada nuevo de S45: el anfitrión no lleva ninguna clave
  de S45.
- **`GuestXVeto summary`** (L44840):
  - `SetInteraction` X 121 = **add 78 / remove 43**: desequilibrado, pero es vía C permitida y de mundo; no lo toca
    S45;
  - `BlockInteraction` 5 on / 5 off: equilibrado;
  - `V1 Loading`: 0 llamadas.
- **`GuestSeqForeign`** (L44835): `NEUTRALIZED x54`, `write failures 0`, `OTHER NODE 0`. S44 sigue limpio.
- **Las líneas `S-A` llevan `capture t=-` hasta conocer la captura** (L185, la primera de S-C): cosmético.
- **El nombre de la etapa se corta a 24 caracteres** (`'cd_seq_quest_crime_polic'`): las dos etapas de la cárcel
  (`0x339` y `0x33A`) solo se distinguen por el recurso. Conviene que R-A y S-A impriman el nombre completo o el
  recurso con su nombre.
