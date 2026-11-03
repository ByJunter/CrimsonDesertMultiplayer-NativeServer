# P8 · Sesión 7: escenas de NPC (S1), NPC-tienda ausentes y animales quietos

10 de octubre de 2026, rama `opcion-d`. Build **1.0.0.2976**, direcciones en RVA (base `0x140000000`).
Solo lectura: no se tocó `src/`, no hubo commit y no se lanzó el juego. Los scripts (lector CDPK, inventario de
actores por captura, simulador del búfer de interpolación) estaban en el scratchpad de la sesión, en
`npcshop_s7/`, y no se conservan.

**[V]** = comprobado (log, captura o instrucción). **[E]** = hipótesis o inferencia.

## 0. Datos

| Ref. | Archivo |
|---|---|
| HL | `C:\Emulador CD\backups\p8-sesion7-host\cdmp.1.log` (06:02–06:18; `cdmp.log` es un segundo arranque, 06:24–06:31, sin invitado) |
| HC | `…\p8-sesion7-host\cdmp_packets_20261010-060302.bin` (cliente del anfitrión) |
| GL / GL2 | `…\p8-sesion7-shadow\cdmp.1.log` (05:04–05:16) y `cdmp.log` (05:16–05:18, segunda entrada) |
| GC / GC2 | `…\p8-sesion7-shadow\cdmp_packets_20261010-050511.bin` / `-051629.bin` (cliente real del invitado) |

- **Reloj [V]:** `t(HC) ≈ t(GC) + 143,2 s`. Es la mediana de 5051 claves de NPC que aparecen en las dos capturas (rango 133–147).
- **Estancia del invitado en el campo del anfitrión:** viaje 2 (HC t≈190–770) y viaje 4 (t≈858–918), con el personaje `A01002E9` y, en la reconexión, `A0101847`.
- **Distancia invitado–anfitrión:** entre 1 y 75 m en todo el viaje 2 (posiciones de los 2556/2697). Estaban en el mismo pueblo, alrededor de (−10700, 615, −3880).

## 1. Resumen

| Síntoma | Causa raíz | Confianza | ¿Depende de la opción A? |
|---|---|---|---|
| 1. Escenas de NPC (barrer, charlar) | Al invitado le faltan las escenas del dueño. Las de **secuenciador** (2661/2981) no le llegan, y las de **horario** no tienen su `NPCScheduleData_File` (contenedor vacío). Las acciones con bandera `0x2D3=1` nunca se reenvían. 21 NPC de secuenciador fallan el login (`eErrNoInvalidActionChart`) | Alta [V] | Sí: parte de secuenciador + complemento de restauración |
| 2. NPC-tienda ausentes | **No es red ni retención.** Su 2005 llega al invitado a la vez que al anfitrión y ningún hold los retiene. Los tenderos con nombre tienen **un secuenciador propio del Kliff del anfitrión** (p. ej. `0x1AB` Grover, `0x1AD` Dahlia), que el invitado no recibe. El cliente del invitado **no inicializa el diálogo de ningún NPC de secuenciador**: 0 de 40 `SetQuestDialogReq`, frente a 40 de 40 en el anfitrión | Alta en el hecho [V], media en el efecto visual [E] | Sí: parte de **secuenciador**, con un orden distinto al previsto (§5.2) |
| 3. Animales quietos | **Interpolación del modo 2.** Todos los NPC del invitado tienen controlador `0x90100000`, así que van en modo 2 (solo interpolan). El anfitrión envía el `MoveActorReq` de cada NPC cada **~1,1 s** (mediana 1,114 s; p10 1,012 s), y el búfer se **reinicia** cuando entre dos muestras pasan más de 1000 ms (`0x11B2073 cmp r8,0x3E8`). El animal queda parado unos 0,9 s y luego salta unos 9 m en 200 ms. Las mascotas del invitado van en modo 1 (control local) y por eso se mueven bien | Alta en el mecanismo [V], alta en el efecto [E fuerte] | **No** |

## 2. Sondas S1 (H1–H3, G1–G4) y ObserverSceneCopy: qué confirman

Todas las sondas se instalaron: anfitrión `hooked 9/9 | REFUSED: none`, invitado `hooked 2/2`. Ninguna perdió datos (`ring dropped 0`).

### 2.1 H1: el filtro del servidor es la bandera `0x2D3` (confirmado en vivo) [V]

- **Recuentos** (último `[H1 summary]`, 06:18:03):
  - acciones con bandera: **3559** (514 con el usuario 2 ausente y 3045 dentro), aplicadas y **nunca difundidas**;
  - sin bandera: 67 880, de las que se difunden 67 851, 2 se quedan por la bandera de salida y 27 se paran antes.
- **Por clave:** los 23 NPC con acciones con bandera tienen `B0` (cero difusiones de esas acciones).
  - Las acciones sin bandera de la misma clave sí se difunden. Ejemplo: `B010007C`, paloma mensajera: `F2/0 P15/70 B85`.
- **Por hash:** el único hash de NPC con bandera es `0x35DD392F` (`F470/3012`). El 2223 propio del cliente del anfitrión (sniffer) da 3533 con bandera, casi lo mismo que el servidor.
- **Conclusión:** el §3 de `p8-s1-escenas-npc.md` queda confirmado. No hay que tocar la bandera.

### 2.2 H2: cómo se enlaza cada NPC en el anfitrión [V]. Refuta la hipótesis §2.3

- **Lectura de bloques:**
  - bloques de IA leídos: 2494;
  - con escena: 471, **todos válidos**;
  - con recurso y "fichero no cargado": 0. Los 2020 "not loaded" son bloques sin escena: recurso 0 y nodo −1.
- **vf13 `0x9ED401`:**
  - `passes` 473, igual que los 473 `P8Probe [binding 0x9ED431]`;
  - `EXITS` 50, y **los 50 son NPC de secuenciador**: `6D, 71–80, 82, 14C–14F, 156–15B, 211, 212, 6C8, B02, BF2, C08–C12, C51–C54, CC9, E0C, E0D`.
- **Reenlace:** el reenlace por la IA (vf107) se dio **0 veces**, y por 2874 también 0. `0x9F2FA0` tuvo 0 hits.
- **La hipótesis §2.3 cae:** "Damiane se enlaza más tarde por la vf107" es **falso**. En esta sesión, el bloque de IA de los NPC de secuenciador no trae escena **ni en el anfitrión**: `B010006D`: `resource 0 node −1 scene 0` (HL 06:04:22.986).
- **Cómo se enlazan entonces:** por la ruta propia del 2661 (`SequencerLoadingTask`), a partir de la lista de enlaces del paquete. `comp+0x2E0` lleva la clave del secuenciador (`0x13`…) desde el 2005.
  - Indicio [E fuerte]: `BodyAnim 0xA79770` corre en el anfitrión justo para esos NPC: 18 llamadas, `71–80, 82, 158, C07, C0A, C0B…`. En el invitado corre 0 veces para NPC.
- **Consecuencia:** a los NPC de secuenciador **no les hace falta** `NPCScheduleData_File` ni retener su 2005. Les hacen falta el **2661 (+2981)** y que sus actores existan antes (§5.2).

### 2.3 G1 + G2: al invitado le falta el fichero, no el dato [V]

- **G1:** 1806 bloques de IA leídos, **0 válidos**. Los 466 con recurso dan `FILE NOT LOADED` (en GL2, 248 de 248).
  - Ejemplos: `B01000A3` Peddler, recurso `0xAC00B3BB` nodo `0x20` escena `0x20000014`; `B01000DE` Goldleaf Merchant, `0x6FF7E5A6`/`0x34`/`0x2000001B`.
- **G2:** el contenedor `NPCScheduleManager` del invitado está **vacío** (`buckets 0, used 0`) para los 26 recursos (GL) y los 21 (GL2).
  - En el anfitrión, los mismos recursos (`0xAC00B3BB`, `0x5FB728ED`…) están `file loaded and node found`. Allí hay 18–39 ficheros cargados.
- **H3:** el registro que recibe el usuario 2 lleva el mismo bloque que leyó el anfitrión: "the record carries the host's block 199 / 246".
- **§1.1–§1.2 confirmados:** el 2335 es lo que hace falta para cargar el fichero. Ningún 2335 del anfitrión llega al invitado: los 36 que recibe son de su propio campo.

### 2.4 H3: dónde iría la instantánea [V]

En los 4 viajes, el primer 2005 de NPC al usuario 2 cae **entre el 2549 y el 2144**: viaje 2, 527 registros en esa ventana y 63 después; viaje 4, 616 y 151.

| Viaje | Instantánea prevista | Dónde |
|---|---|---|
| 2 | 49 × 2335 y 18 × (2981+2661) | antes del paquete #254 |
| 4 | 59 × 2335 y 24 × (2981+2661) | — |

El punto para los **2335** es el inicio de `MoveFieldCompleteCore 0x2BC7670`, como se previó. **Para los 2661 vale lo contrario** (§5.2).

### 2.5 G3 y G4: líneas base [V]

- **G3:**
  - en el invitado existe X = `0x56F6B330600` (`ClientSequencerStageManager` del `A0100001`);
  - `X+0x148` tiene 0 escenas, `X+0x174 = 0` y `X+0x231 = 1`;
  - el gestor del Kliff propio (`A01002E9`) tiene `+0x231 = 3`.
- **G4** (C>S del invitado):
  - `2591` ×10, todos de su escena propia `0x1A3`;
  - `2436` ×1, `2547` ×1, `2173` ×12 (su Kliff), `2100` ×187 (id 0) y `2223` ×1088;
  - de las "2223 for NPC keys 438", **491 son de su mascota `B01002EA` (Abyss Heuklang)**, no de NPC del anfitrión. G4 las clasifica por la clase de clave; es una etiqueta engañosa.
  - Ningún C>S toca escenas del anfitrión.
  - **Riesgo a vigilar:** los `SyncStageCandidatePostionReq` 2100 del invitado llegan al servidor (220).

### 2.6 ObserverSceneCopy [V]

- `COPY x1` y 9 casos "not user 2's scene"; `copies 2` en el estado y `LOST 0`.
- La escena propia del usuario 2 (ESC/inspección, `0x1A3`/`0x1D7`) recibe su 2810. Funciona como se diseñó y no afecta a los síntomas.

## 3. Síntoma 2: NPC-tienda ausentes

### 3.1 La red y los holds quedan descartados [V]

- **Mismo conjunto de 2005 [V]:** de las 5051 claves que el anfitrión recibió con el invitado dentro, al invitado le faltan **0**. Las 7 claves que solo tiene el invitado son gimmicks de su propio campo.
  - Hay copia del 2005 para el usuario 2 de toda la interest del anfitrión (`finders control … 213688 relayed`).
- **Mismo ciclo de vida [V]:** las altas y bajas (2516, motivo 25) de cada tendero coinciden en las dos capturas.

| Clave | Nombre | 2005 HC / GC | Baja HC / GC |
|---|---|---|---|
| `B01000A3` | Peddler | 67,5 / 54,4 | — |
| `B01000DE` | Goldleaf Merchant | 67,5 / 54,4 | — |
| `B010011D` | Apothecary | 67,5 / 54,5 | 469,5 / 326,2 |
| `B0100157` | Street Vendor | 68,4 / 54,5 | 452,5 / 309,2 |
| `B0100C0F` / `C11` / `C12` | Grover / Tina / Dahlia | 452,6 / 309,6 | — |
| `B0100C53` / `C54` | Haldwin / Bran | 453,1 / 310,1 | — |
| `B01005CD` | Leather Peddler | 282,9 / 139,8 | 519,5 / 376,2 |

- **Holds [V]** (estado final del anfitrión, HL 06:18:22):
  - `observer hold`: 1748 retenidos y **1748 liberados** (media 205 ms, máximo 10,06 s). Solo 2 se liberan por vencimiento con restauración 0: los "cadáveres" `B01005CC` Baby Deer Carcass y `B010138E` Small Wolf Carcass, que en el invitado quedan `STUCK` en la guarda de capas.
  - `observer hold acks`: 6965 paquetes, todos después de su 2005.
  - `child hold`: 2 retenidos y 2 liberados.
  - `character hold`: 0.
  - Ningún tendero aparece en una línea de retención.
- **En el invitado se crean y actúan [V]:**
  - `GuestS3 … StartAction 0xB01000A3 … action INSTALLED`;
  - `RestartLayerAction` sin fallo para `B01000DE`, `B0100157` y `B010011D`;
  - los tenderos **no** están entre los 21 `login FAILED`;
  - sus posiciones (2991 `+0x8F`) están en el pueblo, a 20–80 m del invitado. Ejemplo: Goldleaf Merchant en (−10667,5, 620,7, −3835,2).

### 3.2 Lo que sí distingue a los tenderos: el secuenciador del dueño [V]

- **Los tenderos con nombre están enlazados a un secuenciador propio** del Kliff del anfitrión (2661 de HL, `ObserverStage`).

| Secuenciador | Enlaces |
|---|---|
| `0x19D` | Renee |
| `0x19E` | Street Vendor + Elantrima Packhorse |
| `0x19F` | Delkin |
| `0x1A0` | Rhett |
| `0x1A1` | Alden |
| `0x1A4` | Turnali |
| `0x1A9` | Host (posadero) + gimmick `C08` + 5 Wandering Freesword |
| `0x1AB` | Grover |
| `0x1AC` | Tina + gimmick `C10` |
| `0x1AD` | Dahlia |
| `0x1B9` | Haldwin |
| `0x1BA` | Bran |

  - Todos tienen además el rol `0x20000002 → 0xA0100001`.
- **El diálogo del NPC (`SetQuestDialogReq` 2857) [V]:** se cruzó qué NPC recibe un 2857 de cada cliente.

| Clase del NPC | Con 2857 del anfitrión | Con 2857 del invitado |
|---|---|---|
| Enlazado a un **secuenciador** (2661) | 40 | **0** |
| Enlazado a un **horario** (`0x2000xxxx`) | 422 | 419 |
| Sin enlace conocido | 146 | 91 |

  - Los 45 NPC de secuenciador presentes en el invitado: **0** con 2857 del invitado.
  - Entre ellos están todos los tenderos de la tabla anterior y los 21 del síntoma 1.
  - Los tenderos de horario (Peddler `A3`/`A7`, Goldleaf Merchant, Apothecary `11D`) **sí** reciben el 2857 del invitado, y el servidor contesta igual que al anfitrión (`2948` con la misma cola, p. ej. `01000000c40d0000`).
  - Los 55 "sin enlace" sin 2857 no se explican por distancia: mediana de 92 m al invitado frente a 90 m al anfitrión. Queda abierto [E].
- **Interpretación [E fuerte]:** el papel de tienda (diálogo y punto de servicio) de un tendero de secuenciador lo pone su secuenciador en el cliente que lo reproduce. Hoy ese cliente es solo el del dueño (`2661` ToTarget del dueño). En el invitado el NPC existe, pero **sin rol**: no hay diálogo ni interacción de tienda.
- **Pendiente [E]:** si además **no se dibuja** (lo que vio el usuario). En la sesión 6, los NPC de secuenciador (`0x13`) sí se veían, en A-pose. Eso apunta a "presente pero sin tienda", o a que el tendero esté donde lo deja el servidor y no donde lo coloca la escena. Lo decide la medida de §6 (G5 y confirmar con el usuario qué tres tiendas miró).
- **Tienda, misión y stage:** no depende de una misión ni de un `stage`/`schedule` del dueño. Depende del **secuenciador del dueño**. Los datos de tienda (`StoreDataListAck` 2146 ×675) llegan al invitado con su propia clave `A01002E9` [V].

## 4. Síntoma 3: animales quietos

### 4.1 La red está completa [V]

En la ventana del viaje 2 (HC 210–760 s, GC 67–617 s):

| Clase | El anfitrión envía (C>S) | El invitado recibe (S>C) |
|---|---|---|
| Fauna (tipo 6, 1827 claves) | 2556 ×26 936, 2223 ×20 582, 2734 ×23 517, 2405 ×12 112 | 2697 ×26 935, 2991 ×19 400, 2404 ×23 593, 2723 ×1210 |
| Monturas y carros (tipo 5) | 2556 ×1893 | 2697 ×1895 |
| Humanos (tipo 3) | 2556 ×32 689 | 2697 ×32 593 |

- Los recorridos coinciden (p. ej. `B0100A0B` Black-Naped Oriole: 165/165 muestras y 496 m en los dos lados).
- 2405 y 2291 no tienen Ack (son estado de servidor).
- **No es un hold ni una IA que falte en la red.**
- **Marcas de tiempo [V]:** el `ts` de los 2697 (`+0xD5`) llega igual al reloj del invitado (`ts − t` mediana 159 589 frente a 159 586 de su propio 2556). Están sincronizados y no hay desfase de reloj.

### 4.2 El mecanismo: modo 2 + muestras cada 1,1 s + reinicio a 1000 ms [V]

1. Los 1789 actores no propios que registra `GuestS3` tienen `ctrl 0x90100000`. Entre ellos, 1278 de fauna. El modo es `0x88E8E0`, regla 3: `actor+0x90 ≠ 0` → **modo 2** (`movimiento-remoto-cliente.md` §1.1).
   - En modo 2 el actor solo interpola el búfer de red: sin IA, sin root motion y sin extrapolación.
2. Cadencia de envío del controlador:

| Emisor | Hueco entre `MoveActorReq` con movimiento |
|---|---|
| NPC del anfitrión | p1 0,56 s; **p10 1,012 s; p50 1,114 s**; p90 1,74 s |
| Kliff del anfitrión | p50 0,029 s |
| Kliff del invitado | p50 0,017 s |

3. `setInterpolatePositionData 0x11B1FD0` vacía el búfer cuando `ts_nueva − ts_última > 1000` (`0x11B2073 cmp r8,0x3E8; jbe`, verificado con capstone).
   - Después parte de la posición visible y alcanza la nueva en `ts − (now − 200 ms)`, unos 200 ms.
   - El retardo de 200 ms es la global `exe+0x6CB9548` (la lee `0x11B200E`).
   - Sin muestra nueva, el actor se queda quieto.
4. **Simulación del búfer** con todas las muestras que recibió el invitado (2697 y el MoveData del 2991):

| Clase | Muestras aceptadas | Reinicios | Reinicios con salto > 0,5 m | Salto (mediana) |
|---|---:|---:|---:|---|
| Fauna | 43 504 | **30 369 (70 %)** | **26 853** | 9,3 m |
| Humanos | 55 343 | 43 590 (79 %) | 14 594 | 0,2 m |

   - Fauna: un animal en marcha queda unos 0,9 s en pose de reposo, con velocidad 0, y luego "salta" 9 m en 200 ms (unos 45 m/s, que el suavizado de `0x9FF500` apenas deja ver como locomoción). Es lo que se percibe como "quietos".
   - Humanos: la mayoría está parada o anda despacio, y el tirón es poco visible.
5. **Mascotas del invitado [V]:** las controla él (`B01002EA`: 2556 ×195 y 2223 ×491 desde su cliente) → modo 1, simulación local, y se mueven bien.
   - En el anfitrión, la mascota del invitado está en modo 2 y le pasará lo mismo [E].
   - Los caballos del anfitrión: [E] mismo caso.
6. **No es el mecanismo de S1:** no tiene que ver con el bloque de IA ni con el secuenciador, ni con un hold.

## 5. Diseño de arreglo

### 5.1 Animales y NPC en movimiento (no depende de la opción A)

**"Retardo de interpolación por actor"** en el invitado (y en el anfitrión, para las mascotas y monturas del invitado). Todo en `.rsrc`.

**Ganchos** (bytes por verificar con capstone antes de implementar, como en §9.1 del informe de S1):
- **`0x11B200E` / la lectura del retardo en `updateInterpolatePositionData 0x11B2420`**, y las equivalentes de rotación `0x11B26E0`/`0x11B2AF0`:
  - si el búfer (`mc+0x3D0`/`mc+0x488`; `mc = buf − off`, actor `mc+0x68`) es de un actor con clave `0xB…`, sin dueño usuario y con `+0x90` ≠ usuario local, se usa `retardo_actor`;
  - `retardo_actor = clamp(hueco_último + 150 ms, 200, 1600)`;
  - a los jugadores (tipo 1) no se les cambia nada.
- **`0x11B2073`:** para esos mismos actores, umbral de reinicio de 2500 ms en lugar de 1000. El retardo, por sí solo, ya suaviza (el reinicio sigue partiendo de la posición visible), pero sin el umbral cada muestra crea un punto de quiebre.

**Experimento previo, barato y reversible** (solo el invitado, sin código):
- escribir `exe+0x6CB9548 = 1300` en el mundo y mirar la fauna;
- se espera movimiento continuo con 1,3 s de retraso;
- efecto colateral aceptable en la prueba: el Kliff del anfitrión también va 1,3 s tarde.
- Si pasa, se implementa la versión por actor.

**Riesgos:**
- NPC enemigos hasta 1,3 s por detrás en combate. Se puede limitar el retardo a la fauna (tipo 6) y a los NPC que no estén en combate.
- La global la leen solo las cuatro funciones de interpolación (§6 de `movimiento-remoto-cliente.md`).
- No se tocan la red ni el servidor.

**Alternativa, en el anfitrión:** subir la cadencia de envío de los NPC que observa el usuario 2. Sigue haciendo falta un retardo mayor que el hueco, así que no basta sola.

### 5.2 Tiendas y escenas: la opción A, con secuenciadores primero y un orden distinto

La opción A de `p8-s1-escenas-npc.md` §4 sigue siendo la raíz para los síntomas 1 y 2. Esta sesión obliga a tres cambios.

1. **Prioridad.** Los tenderos y los NPC de "charla" o "barrido" son de **secuenciador** (H2: los 50 que dan `EXITS`). Los de horario (199–246) solo necesitan el fichero.
   - Propuesta: hacer **primero el tramo de secuenciador**, empezando por **un tendero aislado sin acciones con bandera**: `0x1AB` (Kliff + Grover) o `0x1AD` (Dahlia).
   - Son más simples que `0x13` (8 enlaces y acciones `seq_bodyanim` en bucle).
2. **Orden del 2661: al revés que el 2335.**
   - `0xA9CA40` descarta el secuenciador si **algún actor enlazado no existe todavía** (`0xA9CE81`).
   - En el anfitrión el 2005 del NPC llega **antes** que el 2661 (Damiane, t=67,066 frente al 2661 en 67,151).
   - Por tanto, en el invitado el 2661 copiado (con su 2981) tiene que salir **después** del 2005 de todos sus enlaces.
   - El 2005 lo retrasa a su vez `observer_hold`: media 205 ms, máximo 10 s.
   - **Mecanismo propuesto:** encolar la copia del 2661 en `observer_hold_acks`, como un paquete retenido de **varias** claves (todas las `0xB…` de su lista de enlaces). Ese módulo ya garantiza "después de su 2005" (6965 de 6965). El rol `0xA0100001` ya existe.
   - Los 2335, en cambio, van **antes** de la ráfaga de 2005, al inicio de `0x2BC7670` (H3).
3. **Enrutado a X** (paso 4 de la opción A) para 2661, 2981, 2810 y 2524, con el actor `A0100001` del paquete.
   - **Filtro de autoridad:** descartar los C>S 2767/2591/2436/2100 del invitado que nombren un secuenciador del libro del dueño. El 2100 ya llega hoy, ×220.

**Criterios de éxito del tramo mínimo:**
- en el invitado, un `SetQuestDialogReq` 2857 de Grover o Dahlia, seguido del 2948;
- la tienda se abre desde el invitado;
- `X+0x170/+0x148` cuenta un secuenciador más;
- no hay C>S del invitado con `0x1AB`;
- la cámara y la entrada del invitado no cambian (el rol `0x20000002` es el Kliff del anfitrión).

**Complemento para los 21 `login FAILED`** (solo síntoma 1): la restauración de un hash `seq_bodyanim` en un actor remoto no debe dar error (`p8-actores-anfitrion.md` §4.1 (ii)). Los tenderos no lo necesitan: su restauración ya funciona.

**Riesgos:**
- enlazar el Kliff remoto en una escena local del invitado;
- una entrada tardía reproduce la escena desde su preparación;
- tráfico de 2981/2264;
- `SyncStageCandidatePostionReq` del invitado sobre los stages del anfitrión.

## 6. Qué medir en la próxima prueba

1. **Antes, al usuario:** nombre y lugar de las 3 tiendas que miró, y si "no aparecen" quiere decir que no se ven o que no ofrecen tienda.
   - En la prueba: anfitrión e invitado juntos delante de Grover, Dahlia, el "Host" (posadero) y el Goldleaf Merchant (horario), con captura de pantalla de los dos.
2. **G5 (sonda nueva, solo lectura, invitado), por lista de claves:**
   - existencia, `+0x5E`, `+0xC2/+0xCF`;
   - modo `0x88E8E0`;
   - `mc+0xF0`;
   - `count` del búfer `mc+0x3D0+0x10`;
   - posición de `TransformSync` frente a la red (`+0x534`);
   - `comp+0x2D8/+0x2E0`;
   - si ya salió su 2857.
   - Decide "no se dibuja" frente a "sin tienda".
3. **G6 (invitado):** contador de reinicios en `0x11B2080` (rama `> 1000 ms`) por clase de clave, y el modo de 20 animales.
   - Se espera más de un 70 % de reinicios en la fauna y modo 2.
   - Después, el experimento `exe+0x6CB9548 = 1300` (§5.1), con observación visual de la fauna.
4. **Recuentos a repetir:**
   - 2857 del invitado por clase (secuenciador, horario, otros);
   - qué son los 55 "sin enlace" que no reciben 2857;
   - en el anfitrión, cadencia del 2556 de la mascota del invitado y aspecto en pantalla (el mismo problema en espejo).
5. **Si se aprueba el tramo mínimo de secuenciador:** los criterios de §5.2 y una sola escena (`0x1AB`).

## 7. Correcciones a informes anteriores

| Afirmación | Corrección |
|---|---|
| `p8-s1-escenas-npc.md` §2.3 [E]: "Damiane se enlaza en el anfitrión más tarde por la vf107 de la IA" | **Falso [V, H2]:** 0 reenlaces por vf107 o 2874. En la sesión 7, el bloque de IA de los NPC de secuenciador no trae escena en ninguno de los dos clientes y su vf13 sale también en el anfitrión. El enlace es por la ruta del 2661 |
| `p8-s1-escenas-npc.md` §4 A.2/A.5: "la instantánea del libro, antes de la ráfaga de 2005; retener los 2005 hasta que el fichero esté cargado" | Correcto **solo para los 2335**. Los 2661 tienen que ir **después** de los 2005 de sus enlaces (`0xA9CE81`). Retener los 2005 no ayuda a los secuenciadores |
| `p8-s1-escenas-npc.md` §2.2: "235 NPC; 225 con recurso de 2335/2661" | En la sesión 7, los recursos del bloque de IA son todos de **horario** (466 en G1). Los de secuenciador se identifican por `comp+0x2E0` y por la lista de enlaces del 2661 (45 NPC) |
| G4 `2223 for NPC keys` | Incluye la mascota propia del invitado (`B01002EA`, 491). No son Req sobre NPC del anfitrión |
