# P8 · Sesión 8: secuenciadores copiados (opción A) y lo que sigue faltando

10 de octubre de 2026, rama `opcion-d` (HEAD `16bf71f`). Build **1.0.0.2976**, direcciones en **RVA** (base
`0x140000000`). Solo lectura: no se tocó `src/`, no hubo commit y no se lanzó el juego. Nada de `.xtls`: donde una
cadena entra en `.xtls` se dice y se para ahí.

**[V]** = comprobado (log, captura o instrucciones con capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`).
**[E]** = hipótesis o inferencia.

## 0. Datos

| Ref. | Archivo |
|---|---|
| HL | `C:\Emulador CD\backups\p8-sesion8-host\cdmp.log` (10:32–10:51; `ObserverSeqCopy`, `SceneDiag` H1–H3) |
| HC | `…\p8-sesion8-host\cdmp_packets_20261010-103235.bin` (cliente propio del anfitrión) |
| GL | `…\p8-sesion8-shadow\cdmp.log` (09:34–09:51; `GuestSeqRoute`, `GuestStage`, `[G5 …]`, G1–G4, `P8Probe`, `GuestS3`) |
| GC | `…\p8-sesion8-shadow\cdmp_packets_20261010-093514.bin` (cliente real del invitado) |

- **Reloj [V]:** `t(HC) ≈ t(GC) + 174,5 s` (2005 de Dahlia: HC 774,838 / GC ≈600,36; de Tina: 833,888 / ≈659,36).
- **Nombres [V]:** salen del propio 2005 (registro `u16 len | u8 tipo | u32 clave | …`, nombre UTF-16 con su `u32`
  de longitud en el registro +30). 6338 claves con nombre en HC.
- **Viaje observador:** HC t=217,5 (2174 del usuario 2) → puerta abierta t=225,7 → fin t=953,7 (logout).
  Filtro `observer_seq_copy_only` vacío (`only: every sequencer`).
- Scripts (lector CDPK, nombres, enlaces por secuenciador, trazas por clave, distancias, barridos capstone): scratchpad
  de la sesión, no se conservan. Lector = `observador/scripts/cap.py`; desensamblador = `observador/scripts/pedis.py`.

## 1. Resumen

1. **La parte de transporte y enrutado de la opción A funciona [V].** Anfitrión: **45** 2661 copiados (18 de la
   instantánea con su 2981 + 27 en vivo), **1** 2322, **21** 2810; `queue refused 0`, `ring lost 0`. Invitado: llegan
   los 45/1/21; **45/45** 2661 `ROUTED` a X, **43 created** y **2 rechazados** (`0xA9CE81`, actor enlazado ausente);
   186 2981 y 21 2810 enrutados; 2322 `ROUTED`, `X+0x231 1 → 2`; `gate dropped 0`, `refused 0`; ningún C>S del
   invitado sobre secuenciadores del anfitrión (G4).
2. **Nada se reprodujo [V].** X se queda en `+0x231 = 2` los 15 minutos, `+0x174` vuelve a 0 (carga terminada) y las
   instancias se acumulan en la lista `+0x1B0` (17 → 22); la lista de precarga `+0x1C0` siempre 0. `BodyAnim
   0xA79770` en el invitado: 4 llamadas, **todas de su propio Kliff**; 0 de NPC.
3. **Causa raíz nueva [V estático + log]: X no tiene «tick».** La única actualización de un
   `ClientSequencerStageManager` (`0xA97300`, la que pasa `+0x1B0 → +0x1C0` y llama a la reproducción `0xA9D860`) se
   llama solo desde `0xA953C0`, y este solo desde `0x8B6150`, con el gestor que devuelve la **vf48 del campo**
   (`0x9A5BE0` = M, el del dueño del campo, que en el invitado es el usuario 2). X nunca se actualiza. Además, el paso
   `2 → 3` de `+0x231` no está en `.rsrc`: lo hace la llamada `0x77D061 call 0xA97B30` (thunk a `.xtls`) del estado
   de carga `0x14`, también solo con M.
4. **Consecuencia en los NPC [V]:** los 71 NPC enlazados a un secuenciador tienen `+0xCF = 0` (login sin terminar)
   todo el rato (G5) y **0 de 71** instalaron una sola acción (`GuestS3`), frente a 2244 claves de otros NPC que sí.
   La puerta «no listo» `0x8B53C0` deja en reintento todos sus Move/StartAction. Que el login de un NPC de
   secuenciador espere a que su secuenciador se reproduzca es **[E fuerte]** (lo que lo decide está en `.xtls`).
5. **Por síntoma:**
   - **Dahlia y la taberna:** existen (G5 `exists`), con su instancia en X (`0x1B3`), pero sin reproducir y con
     `+0xCF 0`: quietas, sin acciones.
   - **Mesa de pulsos:** es el secuenciador **`0x1B0`**: Kliff, el gimmick «Game Mechanic» `B0100F34` (la mesa), el
     posadero `B0100F35` y 5 «Wandering Freesword» `F36–F3A`. Los cinco hacían `seq_bodyanim` al anunciarse: su
     restauración da `NOT FOUND`, el login falla (`eErrNoInvalidActionChart`) y el actor se destruye. 0,03–0,1 s
     después llega la copia, y `0xA9CE81` rechaza el secuenciador entero.
   - **Tina, Turnali y Rhett:** son de **secuenciador**, no de horario (bloque de IA con escena 0, `comp+0x2E0` =
     `0x1D0`, `0x1CF` y `0x19A`). Sus 2661 se copiaron y se crearon en X. Existen en el invitado, en la misma posición
     que en el anfitrión. Por qué no se ven, si están en el cliente, sigue **abierto [E]**.
   - **Niños en A-pose:** en la taberna, `B0100F3E`/`F3F` (secuenciador `0x1BC`, 0/36 acciones aplicadas). Los otros
     15 niños del pueblo son de **horario** (`0x2000xxxx`, fichero sin cargar, sin 2335) y sí instalan sus acciones.
6. **Siguiente paso:** dar «tick» a X en el invitado (`0xA953C0(X, Q, modo)` junto a la llamada nativa de
   `0x8B6150`), medir `+0x1B0 → +0x1C0`, y luego el paso a 3 de X (con visto bueno, §6). Después, el complemento de
   restauración `seq_bodyanim` (o adelantar la copia a la restauración) para `0x13` y `0x1B0`, el enrutado de
   2524/2111 y el tramo de horarios (2335).

## 2. Recuentos de extremo a extremo [V]

### 2.1 Anfitrión (HL, último resumen 10:51:35)

`ledger … prepared 47, deleted 23 | trips 2 (observer 1) | copied 45 (with their 2981 18), 2322 1, 2810 copied 21,
cancelled 0, vanished 0, filtered out 0, pending table full 0, delivered marks evicted 0, queue refused 0 | bound actor
removed 32 | C>S dropped: host sequencer 0, 2100 0 | sniffed 319 | ring lost 0`.

| Tramo | Detalle |
|---|---|
| Instantánea | 18 secuenciadores vivos (`0x13`…`0x19C`) pendientes en t=217,50; salen juntos en t=225,72 con el 2322 del anfitrión (puerta: el 2322 propio del usuario 2), 8,2 s después |
| En vivo | 27 2661: los de paso Servant+Maid (`0x19D`…`0x1AB`, recurso `72A00680`, uno cada ~20 s mientras el anfitrión anda), la taberna (`0x1AC`…`0x1C1`, t=774,8) y la herrería/sastrería (`0x1CF`/`0x1D0`, t=833,9); espera media 0,18–0,25 s a los 2005 |
| 2810 | 21: cada vez que los enlazados salen de la vista del usuario 2 (2516), el anfitrión borra el secuenciador 1–8 ms después (`[bound actor removed]` + `[2810] SENT`) |

### 2.2 Invitado (GC + GL)

| Paquete con actor `A0100001` | Llegan (GC) | Enrutado (GL `GuestSeqRoute summary`) | Resultado |
|---|---|---|---|
| 2661 | 45 | `ROUTED … x45`, `foreign delivered 45` | 43 `created`, 2 `REJECTED` (§4.2) |
| 2981 | 186 (18 copiados + 168 nativos) | `ROUTED x186` | — |
| 2810 | 21 | `ROUTED x21` | — |
| 2322 | 1 | `ROUTED x1` | `X+0x231 1 → 2` |
| 2524 / 2111 | 144 / 388 (nativos) | **no se enrutan** (fuera de `guest_seq_route`) | caen en M sin efecto |
| 2335 / 2264 / 2195 / 2165 | 0 del anfitrión | — | — |

- G4: C>S del invitado `2591 x10`, `2436 x1`, solo de su `0x1A3`. `2100 x252` (pasan, `observer_seq_copy_2100=0`).
- Sin cierre ni aviso de cámara o control. El final del log es el logout normal.

## 3. X por dentro: por qué no reproduce

### 3.1 Estado de X (`0x488A52C8100`, gestor del Kliff del anfitrión en el invitado) [V]

| Momento (GC) | `+0x231` | `+0x174` (cargas en curso) | `+0x17C` (creados) | `+0x1C0` (precarga) | `+0x1B0` (pendientes) |
|---|---|---|---|---|---|
| 53,7099, 1.er 2661 | 1 | 0 | 0 | 0 | 0 |
| 53,7101, 2322 copiado | 1 → 2 | 17 | 17 | 0 | 0 |
| 09:36:38 (G5) | 2 | 0 | 34 | 0 | **17** |
| 320–753 (cada 2661 en vivo) | 2 | 0 → 1 → 0 | +2 | 0 | +1 (y −1 con cada 2810) |
| final, 09:50:07 | **2** | 0 | 86 | **0** | **22** |

M del usuario 2 (`0x4887B77BD00`): `+0x231 3` (G3), con su `0x1A3` creado en t=533,5.

Lectura: la `SequencerLoadingTask` termina (`+0x174` vuelve a 0) y la instancia pasa a la lista de pendientes
`+0x1B0` **[V]**. Desde ahí nadie la consume: ni pasa a la precarga `+0x1C0` ni se reproduce.

### 3.2 La actualización del gestor solo corre para M [V estático]

```
0xAD2380 → 0x8B6060 (único llamador directo: 0xAD277F)
  0x8B60C6  call 0x17FA6C0             ; campo actual → [rbp-0x41] = Q
  0x8B6102  mov rcx,[rbp-0x41] ; call [rax+0x48]   ; vf48 del campo = 0x9A5BE0 → gestor del DUEÑO del campo (M)
  0x8B613D  mov r8,[rdi+0x80] ; movzx r8d,byte [r8+0x50] ; mov rdx,[rbp-0x41] ; mov rcx,rax
  0x8B6150  call 0xA953C0(M, Q, modo)
0xA953C0: si modo != 2 → 0xA97300(M, Q); luego bucle de reproducciones vivas (+0x1F0/+0x1F8, 0x1F6C190/0x1F6D320)
0xA97300 (actualización del gestor):
  0xA97366  si +0x174 != 0 → no hace nada (cargas en curso)
  0xA973C8  toma la lista +0x1D0 si no está vacía; si no, toma +0x1B0 (y la vacía)
  por entrada: comprobaciones (+0x441, +0x28C, +0x70, 0x602490, recurso +0x298 en [0x6D691E0]+0x120, +0x286)
    no lista → vuelve a su lista (0xA979E0/0xA97A00)
    lista y +0x231 ∈ {1,2} y lista +0x1B0 → push a +0x1C0 (0xA977C5)          ; precarga
    lista y (+0x231 = 3 o lista +0x1D0) → 0xA9D860 (reproducir)               ; 0xA977E7
  0xA97885  si +0x231 = 3 → reproduce (0xA9D860) todo +0x1C0 y lo vacía
```

- Llamadores (barrido de `E8`/`E9` en `.rsrc` y de punteros en todas las secciones): `0xA97300` ← solo `0xA953EA`;
  `0xA953C0` ← solo `0x8B6150`; `0xA9D860` ← solo `0xA977E7` y `0xA97919`. No hay punteros de vtable a
  `0xA97300` ni a `0xA953C0` **[V]**.
- La lista la elige `0xA95940` al terminar la carga: `+0x1D0` si `[[play]+0x60] == 3`, si no `+0x1B0` **[V]**
  (`0xA95A6D`). Los 43 de esta sesión están en `+0x1B0`.
- Otra actualización por fotograma, también solo para M: `0x8B268E` → `0xAA3350(M)` y `0xAA3E70(M, dt)` **[V]** (para
  las escenas de horario de `+0x148` **[E]**).

### 3.3 `+0x231 = 3` [V/E]

- Escrituras de `+0x231` en `.rsrc` **[V]**: `0x77AA8B` (0), `0x77B0CE` (1), `0xB87A67` (2, el 2322). **Ninguna
  escribe 3** en el gestor. Las otras escrituras con desplazamiento `0x231` son de otros objetos (`0x4DA8EE` es una
  copia de estructura; `0x10B6F82`, `0x31A8E45`…).
- El estado `0x14` de la carga (`0x7797F0`, `0x779F6F`) llama a `0x77CFB0`. Este hace `0x77D03C call 0x9A5BE0`
  (= M) y `0x77D061 call 0xA97B30(M, &{[P+0x150], [P+0x158]})`. `0xA97B30` es un `jmp` a `.xtls`: ahí acaba el
  análisis.
- **[E fuerte]:** «reproducir todo lo precargado» (`+0x231 = 3`) lo hace ese `.xtls` y solo sobre M. X se queda en 2
  aunque reciba el 2322. Con 2 y sin tick, aunque X se actualizara, lo listo iría a `+0x1C0` y esperaría al 3.

## 4. Por síntoma

### 4.1 «Muchos NPC sin animación; los de secuenciador, quietos» [V]

- **Bloque 1 (X sin tick):** §3.
- **Bloque 2, login [V log, E causa]:**
  - G5 sigue 64 claves. Todas las enlazadas a un secuenciador del anfitrión que existen tienen `+0x5E 0x10, +0xC2 1,
    +0xCF 0` de principio a fin. Ejemplos: Strangers `77/78`, guardias `7E–80`, Renee `13F`, Delkin `142`, Rhett
    `143`, Alden `144`, `F1E`, Pedro `F33`, mesa `F34`, posadero `F35`, Grover `F3B`, Dahlia `F3C`.
  - El único NPC vigilado sin secuenciador (`B0100157`, «Game Mechanic») tiene `+0xCF 1`.
  - Puerta `0x8B53C0` (Move/Rotate/StartAction; `optD-mundo.md`, `mirror-ack-formats.md`): sin `+0xCF`, el Ack vuelve
    con «reintentar». Resultado [V]: de las 71 claves enlazadas a algún 2661 del anfitrión, **0** tienen un
    `GuestS3 … StartAction … INSTALLED`. Las otras 2244 claves sí lo tienen.
  - Por ejemplo, Rhett recibe 79 `StartActionAck` y no aplica ninguno.
- **Bloque 3 (H1, este viaje):** 4332 acciones con bandera `0x2D3` aplicadas y **no difundidas**, 3148 de ellas con el
  usuario 2 dentro, en 33 claves.
  - Hash `0x35DD392F` (`seq_bodyanim_action_loop`): F1138/3101.
  - Por clave: Strangers `77/78` 584 cada uno; guardias `7E/7F/82` ~388; Freeswords de la mesa `F36/F37` 87 y 90; Pedro
    `F33` 30; cada pareja Servant/Maid 2 por clave.
  - Por diseño solo las reproduce un secuenciador local (bloque 1).
- **Mujeres que barren [E]:** no se identificaron por clave.
  - Las Maid/Servant **de secuenciador** (`0x19D`…`0x1AB`, recurso `72A00680`) en el invitado solo reciben su 2005,
    5 × 2734 y el 2516: no se mueven. No encajan con «caminan bien».
  - Las Maid **de horario** (p. ej. `B010008B`, escena `0x20000187`, fichero `0x79AF6B66` sin cargar) sí reciben
    Move y StartAction (73/138) y los instalan, y además `ReserveSchedulePointAck` ×13. Encajan con «caminan bien».
  - Su «acción concreta» sería la del punto de horario, cuyo `NPCScheduleData_File` falta: 2335 no copiado (G1
    `FILE NOT LOADED`). Pendiente de confirmar con la clave exacta.

### 4.2 Taberna y mesa de pulsos [V]

| Secuenciador (HC t) | Enlaces (nombre del 2005) | En el invitado |
|---|---|---|
| `0x1AC` (774,80) | «Game Mechanic» `F1E` | created |
| `0x1AD` (774,81) | Kliff, Pedro `F33` | created |
| `0x1AF` (774,81) | ninguno | created |
| **`0x1B0`** (774,82, recurso `94FEA6AE`) | Kliff, **«Game Mechanic» `F34`** (en (−10662,8; 617,9; −3831,1), a 1,4 m del posadero), **Host `F35`**, **Wandering Freesword `F36`–`F3A`** | **REJECTED** |
| `0x1B2` / `0x1B3` | Kliff + Grover `F3B` / Kliff + **Dahlia `F3C`** | created |
| `0x1BC` | Kliff, **Boy `F3E`, Girl `F3F`** | created |
| `0x1BE` / `0x1C0` / `0x1C1` | — / Haldwin `F40` / Bran `F41` | created |

- **La mesa de pulsos es `0x1B0` [V en la composición, E en «mesa»].** Equivale al `0x1A9` de la sesión 7 (posadero +
  gimmick + 5 Freeswords). El gimmick `F34` existe en el invitado (`exists`, `+0xCF 0`), pero el secuenciador que
  le da la escena no.
- **Cronología en GC [V]:**
  1. 2005 de `F34`–`F3A` en t=600,1.
  2. t=600,25–600,32: `RestartLayerAction` de `F36`–`F3A` con `0x837487D4` (`seq_bodyanim_action`) y `0x35DD392F` →
     `NOT FOUND (no fallback graph)`.
  3. 09:45:43,77–43,85: `login FAILED … eErrNoInvalidActionChart` de `F36`–`F3A`; los actores desaparecen.
  4. t=600,357: llega la copia del 2661 `0x1B0`. `GuestStage [2661 missing actor] … bound actor 0xB0100F36 … NOT on
     this client` y `0xA9CE81` lo rechaza.
- **Lo mismo con `0x13`** (Damiane, Matthias, 3 Freeswords, Barden, Stranger): 2005 en t≈51,0, `login FAILED` 7 de 7
  en t≈53,5, copia en t=53,71 (instantánea retenida por la puerta) → rechazada.
- **Por qué solo esos:** en su 2005 la acción en curso es `seq_bodyanim_*`, que solo se puede restaurar con el
  gráfico dinámico `CC+0x70/+0x78`. Ese gráfico lo instala `BodyAnim 0xA79770` cuando la escena se reproduce. En el
  invitado eso no ocurre nunca (§3).
  - En el anfitrión el orden nativo es 2005 → 2661 (0–90 ms) → reproducción, y la restauración llega después **[E
    fuerte]**.
  - Los demás de la taberna (posadero, Dahlia, Grover, Pedro, niños) se anunciaron con acciones «other»
    (`0x26107D3C`, `0x1BEBE8DC`): restauran bien y se quedan con `+0xCF 0` (§4.1).
- **Dahlia «aparece, antes no» [E]:** en la sesión 7 su 2005 también llegó al invitado (GC2 309,6). El cambio no se
  explica por la opción A. Lo probable es el momento o el sitio de la visita. Ahora: `exists`, instancia `0x1B3` en
  X sin reproducir, 0 acciones aplicadas. Ese es el «completamente bugeada».

### 4.3 Tina, Turnali y Rhett [V salvo la visibilidad]

| NPC | Clave | Secuenciador | Enlaces | 2005 HC / GC | Posición del 2005 (= la del anfitrión) |
|---|---|---|---|---|---|
| Rhett (Quartermaster) | `B0100143` | `0x19A` (instantánea) | Kliff + Rhett | 56,97 / 51,15 | (−10636,5; 613,4; −3848,3) |
| Turnali (Smithy) | `B0101108` | `0x1CF` (en vivo) | Kliff + Turnali | 833,88 / 659,36 | (−10634,9; 613,6; −3841,4) |
| Tina (Tailor's Shop) | `B010110A` | `0x1D0` (en vivo) | Kliff + «Game Mechanic» `B0101109` + Tina | 833,89 / 659,36 | (−10640,9; 614,4; −3835,4) |

- **No son de horario:** en G1, su bloque de IA es `resource 0, node −1, scene key 0`, y `comp+0x2E0` es su
  secuenciador. El 2335 no tiene nada que ver con ellos.
- En el invitado [V]:
  - 2661 `ROUTED` y `created` en X;
  - restauración `0x26107D3C` completa;
  - sin 2516;
  - Rhett en G5 `exists … +0xCF 0` (Tina y Turnali fuera de la lista G5, misma clase);
  - 0 acciones aplicadas (79/36/14 `StartActionAck` en reintento).
- El invitado estuvo a **7, 14 y 19 m** de Rhett, Turnali y Tina entre GC 686 y 811 s, cuando los tres existían en
  su cliente.
- **Por qué no se ven: abierto [E].** Dahlia y el posadero están en el mismo estado y sí se ven. Candidatos:
  - que el secuenciador del puesto los muestre o coloque (pista de visibilidad o anclaje al mostrador), y que sin
    reproducción queden ocultos;
  - que estén dentro del mostrador o tapados sin su pose de escena.
  - Lo decide la prueba de §6, paso 2, y una captura de pantalla en el sitio.
- **Sobre el 2335:** no se copia (§4.4 de `p8-impl-opcion-a.md`). Afecta a los NPC y niños de horario, no a estos
  tres.

### 4.4 Niños en A-pose

| Clase | Claves | Qué tienen en el invitado |
|---|---|---|
| **Secuenciador** (`0x1BC`, taberna) | Boy `B0100F3E`, Girl `B0100F3F` | instancia en X sin reproducir, `+0xCF 0`, **0/36** StartAction aplicadas → A-pose / pose de entrada **[V estado, E pose]** |
| **Horario** | 15 de 17: Boy `CC`/`D2`/`F9`/`118`/`F61`/`FF3`, Noble Boy `CD`, Girl `CE`/`CF`, Beggar Boy `E4`/`E5`/`E7`/`EA`/`EF`/`113`/`7F1` | escenas `0x20000014`, `0x20000072`/`74`, `0x20000184`/`188`, `0x200001BD`; G1 `FILE NOT LOADED`, sin 2335. Reciben y **instalan** sus acciones (p. ej. `B01000CC` 488 StartActionAck, `INSTALLED`) |

- `B010012B` (Boy, `0x20000188`) no tiene ningún `INSTALLED`.
- Si los niños en A-pose que vio el usuario no son los de la taberna, la causa es la de horario: su acción depende de
  la escena de horario, cuyo fichero no está cargado **[E]**. Hace falta la clave (G5) o la ubicación.

## 5. Qué funcionó de la opción A [V]

- **Libro:** el sniffer y su *tap* llevan 47 preparados y 23 borrados sin pérdidas (`ring lost 0`).
- **Viaje:** decisión de observador, instantánea de 18, puerta y copias en vivo correctas.
- **Orden:** ningún 2661 llegó antes del 2005 de sus actores. Los 2 rechazos son actores que se destruyeron tras
  llegar (login fallido), no un problema de orden.
- **2810:** sincronizado con los 2516; `bound actor removed 32`, todos seguidos de su 2810.
- **2322:** copiado una vez y enrutado (`X+0x231 1→2`). La carga S2 del invitado no cambió (`GuestLoad`, 2 × 2322
  propios nativos).
- **Invitado:**
  - 4 puntos enganchados (`REFUSED: none`);
  - 45/45 al gestor X correcto, vtable comprobada;
  - puerta sin caídas;
  - `SequencerLoadingTask` completa en X (43 instancias);
  - el filtro C>S no tuvo nada que descartar.
- **Estabilidad:** sin cierres. La cámara y el control del invitado sin quejas.

## 6. Siguiente paso concreto (en orden)

1. **Tick de X (invitado; clave nueva, p. ej. `guest_seq_tick`).**
   - Gancho intermedio en `0x8B6144` (`45 0F B6 40 50`, `movzx r8d, byte [r8+0x50]`; sin RIP; comprobar contexto
     `0x8B613D` y `0x8B6149`/`0x8B614D`). Ahí: `rax` = M, `[rbp-0x41]` = Q, `r8` = `[rdi+0x80]`.
   - En el mismo hilo y fotograma, para cada X de personaje remoto con instancias enrutadas (los X que ya conoce
     `guest_seq_route`, vtable `exe+0x55C9720`, `X ≠ M`), llamar `0xA953C0(X, Q, [[rdi+0x80]+0x50])`.
   - Primero solo con `+0x231 = 2`.
   - **Éxito:** `[G5 X]` `+0x1B0` baja y `+0x1C0` sube; sin `0xA95530` (borrado con código 6); sin cierres.
   - Medir también `0xA97300` sobre X (contador) y el tiempo por fotograma.
2. **Reproducir: X a 3.**
   - El camino nativo (`0xA97B30`) es un thunk a `.xtls`: no se usa.
   - Alternativa: escribir `X+0x231 = 3` una vez, cuando X tenga `+0x174 = 0` y el 2322 copiado ya se haya aplicado.
     En `.rsrc`, sus únicos lectores son `0xA97488`, `0xA97503`, `0xA977B6`, `0xA97885`, `0xA97DDB` y `0x779E48`, y
     este último lee M.
   - **Pide visto bueno:** es forzar un estado, aunque no el de M.
   - **Éxito:**
     - `+0x1C0 → 0`;
     - `P8Probe [BodyAnim 0xA79770]` con claves `0xB…`;
     - `+0xCF → 1` en los NPC de G5;
     - `GuestS3 StartAction … INSTALLED` para Rhett, Dahlia o Grover;
     - a la vista: tenderos en pose y la mesa de pulsos.
3. **Enrutar 2524 y 2111 a X:** puntos `0xC08175` (2524) y el de 2111 cerca de `0xC08970`; verificar como en §8.1 de
   `p8-impl-opcion-a.md`. Ya llegan 144 y 388 de forma nativa a M. Con escenas vivas, son sus eventos de rama y carta.
4. **Los que fallan el login con `seq_bodyanim` (`0x13`, `0x1B0`):**
   - (a) el complemento de restauración (`p8-actores-anfitrion.md` §4.1 (ii));
   - o (b) con el paso 1 y 2 hechos, que la copia llegue y se reproduzca antes de la restauración. Hoy la restauración
     corre 0,15–0,25 s después del 2005 y la copia en vivo llega a los 0,22–0,25 s. La instantánea llega 2,7 s tarde
     por la puerta S2.
   - Lo más seguro es (a). Sin él, la mesa de pulsos y el grupo de Damiane no se arreglan.
5. **Tramo de horarios (2335)** para los niños y las criadas de horario y los 199 de horario: el que ya estaba
   previsto (`p8-impl-opcion-a.md` §4.4). Más el tick de las escenas de horario (`0xAA3350`/`0xAA3E70` de
   `0x8B268E`), que también es solo de M.
6. **Medidas para la próxima sesión:**
   - G5 en el **anfitrión** con las mismas claves: ¿`+0xCF 1` en Rhett y Dahlia? Confirma §4.1 bloque 2.
   - G5 en el invitado con Tina y Turnali;
   - captura de pantalla de los dos lados en el puesto de Rhett;
   - la clave de las «mujeres que barren» y de los niños en A-pose.

## 7. Riesgos

- **Tick de X:** llamar a la actualización de un gestor que el juego nunca actualiza.
  - `0xA97300` llama a `0xA9D4B0` (carga) y a `0xA95530` (borrado con error 6) si una entrada no es válida.
  - Su `r13 = [X+8]` (contexto) debe ser válido. Leerlo antes y no llamar si falta.
  - Coste: un recorrido de ~20 entradas por fotograma.
- **X a 3:** reproduce de golpe lo precargado (hasta 22 escenas), con el Kliff del anfitrión (`0x20000002/3`) como
  «jugador». Vigilar la cámara, la entrada y la interfaz del invitado (criterio de aborto ya definido).
  - Puede que `0xA97B30` haga más cosas que el 3 (tiempos `+0x2D0/+0x2D4`), que no tendremos.
- **Escenas de secuenciador del anfitrión que ya terminaron:** entrada tardía; la escena empieza desde el principio.
- **Login:** si `+0xCF` no depende de la reproducción, los NPC seguirán sin aplicar Move/StartAction. Lo decide el paso 2.
- **El 2100 del invitado** sigue llegando al servidor (252); sin efecto visible en esta sesión.

## 8. Correcciones y abiertos

| Afirmación anterior | Ahora |
|---|---|
| `p8-impl-opcion-a.md` §4.3 [E]: «el 2322 en X los reproduce todos» | **Falso [V]:** el 2322 deja X en 2; el 3 y la reproducción necesitan la actualización del gestor (`0xA97300`), que solo corre para M, y el paso a 3 (`.xtls`, estado `0x14`, solo M) |
| §11 riesgo 1 «instancias `+0x174` suben; `+0x1C0` se vacía tras el 2322» | `+0x174` son cargas en curso (17 → 0); las instancias quedan en `+0x1B0`; `+0x1C0` nunca se llena (sin tick) |
| `p8-npc-tiendas-animales-escenas.md` §3.2: secuenciadores `0x1A0` Rhett, `0x1A4` Turnali, `0x1AC` Tina, `0x1A9` posadero | Los ids cambian entre sesiones: en la 8 son `0x19A`, `0x1CF`, `0x1D0` y `0x1B0` |

Abiertos:
- por qué Tina, Turnali y Rhett no se ven estando en el cliente;
- la causa exacta de `+0xCF 0` (en `.xtls`);
- qué hace `0xA97B30` aparte del 3;
- la identidad de las «mujeres que barren».
