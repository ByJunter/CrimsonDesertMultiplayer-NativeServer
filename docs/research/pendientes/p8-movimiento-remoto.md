# P8: movimiento de actores remotos (NPC, animales, caballo, jinete) en la sesión 7

Build **1.0.0.2976**, base `0x140000000`, direcciones en RVA. Trabajo de solo lectura: capturas de la
sesión 7 y capstone sobre el exe. No se tocó `src/` ni se abrió el juego.

- **[V]** = comprobado, con la evidencia al lado.
- **[E]** = deducción fuerte que falta medir en vivo.
- **[H]** = hipótesis.

## Fuentes

| Id | Fichero |
|---|---|
| HC | `C:\Emulador CD\backups\p8-sesion7-host\cdmp_packets_20261010-060302.bin`: cliente del anfitrión, 914 s, t0 = 06:03:02 |
| GC | `C:\Emulador CD\backups\p8-sesion7-shadow\cdmp_packets_20261010-050511.bin`: cliente del invitado, 621 s |
| GC2 | `…-051629.bin`: segunda entrada del invitado, 82 s |
| MRC | `docs/research/movimiento-remoto-cliente.md` (rama `main`): algoritmo del búfer de interpolación |
| CAB | `docs/research/caballo-coop.md` (`main`): capturas con monta |

Los scripts están en el scratchpad de la sesión (`…/scratchpad/mr7/`, no se conservan):
- `ext.py`: extrae del `.bin` los opcodes de movimiento;
- `ts1.py`: desfase entre `ts` y el reloj de la captura;
- `keys.py`: conteos por clave;
- `gaps.py` y `gapdist.py`: cadencia de las muestras aceptadas;
- `seq.py`: secuencia de una clave;
- `names.py` y `cat.py`: nombres de los registros 2005 y conteos por categoría;
- `fwd.py`: qué fracción de los Req llega al otro usuario;
- `par*.py`: campo padre (monta).

Para alinear las dos capturas se usa la `ts` de los paquetes: los dos clientes están en la misma base
de tiempo de servidor.

---

## 0. Resumen

**Causa raíz común de los síntomas 1, 2 y (en parte) 4 [V].** Todo actor movido por IA (claves `0xB…`:
NPC, animales, mascotas y caballo sin jinete) lo controla el cliente de su dueño. Ese cliente solo
envía su posición a **~0,9 Hz**:
- `MoveActorReq` cada ~1,1 s;
- `StartActionReq` al cambiar de acción.

El servidor lo reenvía al otro usuario 1:1, con la misma `ts` y la misma posición. En el cliente que
observa, el actor está en **modo 2** (remoto): solo interpola un búfer de muestras, con un retardo de
200 ms, sin extrapolar, y **vacía el búfer si entre dos muestras pasan más de 1000 ms**. En HC, el
**77 %** de los intervalos entre muestras aceptadas de actores `0xB` supera 1000 ms (mediana 1094 ms).

Por eso, en cada muestra el búfer se reinicia y el actor recorre el tramo de ~1 m en ≈180 ms; después
se queda quieto ~0,9 s. Es el "empieza a dar el paso y se teletransporta".

**Síntoma 3 [E fuerte].**
- El jinete manda su propio Move a ~36 Hz, con la posición de mundo, y el caballo solo a ~1 Hz (CAB).
- En modo 2 el padre del `Path` nunca se aplica y el vínculo jinete-montura no existe en el cliente
  que observa.
- Resultado: dos búferes independientes. El del jinete es denso y suave; el del caballo es escaso y va
  a saltos. El jinete va por delante del caballo.

**Arreglo propuesto, por fases:**
1. **F0:** un experimento de 3 escrituras para confirmar la causa.
2. **F1:** retardo de interpolación por actor, con 4 mid-hooks de 7 bytes verificados y 2 parches de
   inmediato.
3. **F2:** en el servidor del anfitrión, fabricar los `MoveActorAck` del caballo a partir de los del
   jinete.

---

## 1. Lo comprobado en la sesión 7

### 1.1 El reloj no es el problema [V]
`ts − t_captura` (ms) es estable dentro de cada cliente:
- **Anfitrión:** sus Req, mediana −34 328 164; los Ack que recibe, −34 328 224.
- **Invitado:** sus Req, −85 509; los `MoveActorAck` (2697) que recibe, −85 525. Los
  `StartActionAck` (2991), −85 524.

`ServerTimeInfoAck` (2188), cada 5 s, mantiene al invitado en la base del servidor (srv−t = −85 516
al principio y −85 511 al final). Los Ack llegan unos 16 ms después de su `ts`. **No hay muestras
"viejas" ni "futuras"**: no aplican S2, S3 ni S4 de MRC §2.

### 1.2 Reenvío 1:1 y cadencia de origen [V]
- **Reenvío.** De los Req `0xB` del anfitrión dentro de la ventana del invitado, **94,1 %**
  (126 513/134 508) le llegan como Ack con la **misma `ts`**. El resto son claves con un solo
  Move (gimmicks).
- **Ejemplo**, NPC `0xB010014D` caminando:
  - HC, 300,6–311,7 s: `MoveActorReq` cada 1086–1149 ms, con un avance de 0,77–1,48 m por muestra
    y `+0x74` (velocidad) ≈ (−0,99, 0, −0,23).
  - GC, 157,2–168,4 s: las mismas `ts` (317045…328182) y las mismas posiciones, como `MoveActorAck`.
  - Los `StartActionAck` del mismo NPC traen la misma `ts` y la misma posición, así que se
    descartan como duplicados.
- **Distribución** (HC, Move y StartAction de claves `0xB` juntos, solo las muestras que el búfer
  aceptaría): n = 151 287, p10 370 ms, **p50 1094 ms**, p90 1149 ms; **77 % > 1000 ms**.
  - Por actor: 2231 de 2314 tienen la mediana entre 1000 y 1200 ms.
  - **Ningún** actor `0xB` tiene la mediana por debajo de 300 ms.
- **Los personajes `0xA…` en cambio:**
  - el del anfitrión, mediana 29 ms;
  - el del invitado, 10 048 `MoveActorReq` en 621 s.
- **Velocidad en `+0x74` [V].** Es la velocidad de mundo. Un cuervo que vuela da |v| = 8,6 m/s
  y 8,66 m en 1004 ms.

### 1.3 Por qué eso produce teletransportes (código de MRC, ya verificado en `main`)
Datos del código:
- `setInterpolatePositionData` (`0x11B1FD0`): reinicio en `0x11B2073 cmp r8,0x3E8` (bytes
  `49 81 F8 E8 03 00 00`). En la rotación, `0x11B26E0`, el reinicio está en `0x11B2775` (mismos
  bytes).
- El retardo es la `GlobalVariableInt` con el valor en `0x6CB9548` (200). Se lee en cuatro sitios
  (los bytes están verificados en §3).
- `updateInterpolatePositionData` (`0x11B2420`) es pura: no recorta muestras; el recorte solo pasa
  al insertar, con un tope de 30.

Secuencia en el cliente que observa, para un actor con muestras cada ~1,1 s:
1. Llega la muestra k+1 (`ts ≈ now − 16`). El hueco con k es de más de 1000 ms, así que hay
   **reinicio**. El búfer queda `[sintética(posición mostrada, now−200), k+1]`.
2. `T = now − 200` alcanza `ts_{k+1}` en unos 184 ms. El actor recorre ~1 m (andando) o ~8,7 m
   (cuervo) a 5–45 m/s.
3. Durante unos 900 ms `T` ya ha pasado la última muestra. No hay extrapolación: el actor se queda
   parado.
4. La velocidad derivada (Δpos/dt) da un pico y luego cero. La animación de locomoción arranca
   ("mueve un poco el pie") y se corta.

**Aunque no hubiera reinicio, con 200 ms de retardo seguiría el salto** (la llegada cae al 83 % del
tramo). Hacen falta las dos cosas: un retardo mayor que el intervalo **y** quitar o neutralizar el
reinicio.

### 1.4 Los NPC están en modo 2 en el invitado [E fuerte]
- `0x88E8E0(mc)`: si `u32[actor+0x90] != 0` (control), devuelve **2**. Los NPC del anfitrión llegan
  con control `0x90100000` (`p8-actores-anfitrion.md`; el log de HC da `controller 0x90100000`).
- Además `actor+0xC8` se fuerza a 0 cuando el control no es el usuario local (optD-mundo §2.2), así
  que el Ack se aplica.
- El invitado **no** envía Req de estos NPC (por ejemplo `0xB0100135`: recibe 540 Move y 33 Start;
  envía 0). Solo corre la IA de su mascota `0xB01002EA` (502 `ChangeAIStateReq` y 120
  `UpdateAIDestinationReq`).

### 1.5 Mascotas: lo mismo en los dos sentidos [V]
- La mascota del anfitrión `0xB0100002` (Abyss Heuklang, tipo 6) y la del invitado `0xB01002EA`
  tienen una mediana de intervalo de 695–734 ms, p90 1,87–1,9 s, y 160 de 514 huecos de más de 1 s.
- En HC llegan 192 Move y 492 Start de `0xB01002EA`: el reenvío al anfitrión también es 1:1.

### 1.6 RotateActorAck no se reenvía para actores `0xB` [V, causa H]
- **De NPC:** el anfitrión envía 1517 `RotateActorReq`; el invitado recibe **0** `RotateActorAck`.
- **Del personaje del anfitrión:** se envían 15 y se reciben 15.
- Efecto menor: los giros en el sitio solo llegan por el quat de Move y Start.
- La regla de reenvío está en el escritor `0x106F32C0` (en `.xtls`) y no se puede tocar. **[H]** El
  servidor solo difunde el Rotate de un actor con usuario dueño.

### 1.7 En la sesión 7 no hubo monta [V]
- 0 `MoveActorReq`/`Ack` de personaje con padre (`+0x8C`) distinto de −1. El único caso es
  `0xB01001F2`, 16 Move en 0,5 s, que es un asiento.
- Ningún 2005 de un hijo de usuario de tipo 5 (caballo) en HC ni en GC. Ningún
  `CallVehicleMercenaryAck`.
- Los síntomas 2 y 3, por tanto, **no se pueden medir con estas capturas**. Su análisis se apoya en
  CAB y en lo medido aquí con las mascotas.

### 1.8 Animales (síntoma 4) [V parcial]
Por categoría de nombre (registros 2005 del invitado):
- **Aves** (cuervo 303, loro 274+245, paloma 240…): Move a ~1 Hz y saltos de ~8,7 m en vuelo.
  Llegan al invitado 1:1, con el mismo patrón de saltos.
- **Gatos** `0xB010012B`/`012E`: en HC, 0–1 Move y 104–142 Start en 15 min, con la posición
  **idéntica** durante minutos (sentados, con una acción de reposo cada ~5 s). **También están
  quietos en el anfitrión.**
- **Ovejas** `0xB010013B…`: 1–6 Move en 15 min. Cuando se mueven lo hacen por acciones con root
  motion (69–74 s: 2,4 m con Start cada 1,5–2,1 s).
  - En modo 2 el root motion no se aplica (MRC §5): la oveja anima en el sitio y salta en el
    siguiente Start.
- **Distancia entre los jugadores:** 1–90 m durante toda la sesión (`pp.py`). No se probó un
  NPC cerca del invitado y lejos del anfitrión.

---

## 2. Causa raíz por síntoma

| # | Síntoma | Causa raíz | Confianza |
|---|---|---|---|
| 1 | NPC: empiezan la animación y se teletransportan ~1 m | Origen a ~0,9 Hz (77 % de los huecos > 1 s) + búfer remoto de 200 ms sin extrapolación + reinicio a los 1000 ms (§1.2–1.4) | **Alta** [V cadencia y reenvío; código de MRC] |
| 2 | Caballo del otro a pequeños teleports (en los dos sentidos) | El mismo mecanismo: el caballo también es un actor con IA del cliente dueño, a ~1 Hz (CAB: 1015 ms montado; 195,09/196,25/197,27 s caminando). Medido aquí con las mascotas en los dos sentidos (§1.5) | **Media-alta** [E: no hubo caballo en la sesión 7] |
| 3 | Jinete por delante del caballo | El jinete manda Move a ~36 Hz con su posición de mundo; el caballo, a ~1 Hz. En el remoto (modo 2) el `Path` (padre `+0x8C`, mundo `+0x94`/`+0xA0`) solo se guarda, no se aplica, y el vínculo de vehículo no se crea (lo escribe `0x222AFD0`, mutado). Dos búferes independientes: el jinete suave al día y el caballo atrasado y a tirones; a galope (8–12 m/s) puede llegar a ~10 m | **Media-alta** [E] |
| 4 | Animales salvajes quietos | (a) Muchos están quietos de verdad también en el anfitrión [V]. (b) Los que se mueven lo hacen con Move a ~1 Hz (aves) o solo por acciones con root motion (ovejas), que el modo 2 no aplica [V código / E visual]. (c) **[H]** IA con nivel de detalle: el anfitrión no simula animales lejos de él. Las mascotas propias se ven bien porque las controla el cliente local (modo 1/3) | **Media-baja** |

---

## 3. Arreglo

### F0. Experimento de confirmación (sin hooks; en los dos clientes; reversible)

1. Escribir **1300** en el int `exe+0x6CB9548` (valor actual de la `GlobalVariableInt` del retardo;
   el objeto es `0x6CB9510`, el valor por defecto 200).
2. Parchear el inmediato del reinicio a **2500 ms** en dos sitios:

   | Dirección | Bytes actuales | Bytes nuevos |
   |---|---|---|
   | `0x11B2073` | `49 81 F8 E8 03 00 00` | `49 81 F8 C4 09 00 00` |
   | `0x11B2775` | `49 81 F8 E8 03 00 00` | `49 81 F8 C4 09 00 00` |

**Resultado esperado:**
- NPC, mascotas y caballo sin jinete se mueven de forma continua, 1,3 s atrasados.
- El otro jugador también va 1,3 s atrasado.
- Al cabalgar, jinete y caballo con el mismo retardo; el caballo sigue cuerdas de ~1 s.

En un jugador solo usan el modo 2 los clones (MRC §6), así que no afecta al resto del juego.
Sirve **solo para confirmar** la causa; no es la solución final.

### F1. Retardo por actor en el receptor (ambos clientes)

Mid-hook en las cuatro lecturas del retardo (todas de 7 bytes, `movsxd reg,[rip→0x6CB9548]`,
verificadas en el exe):

| Sitio | Función | Bytes | Registro destino | Búfer |
|---|---|---|---|---|
| `0x11B200E` | set pos | `4C 63 15 33 75 B0 05` | r10 | `rdi` (= mc+0x3D0) |
| `0x11B2730` | set rot | `4C 63 1D 11 6E B0 05` | r11 | `rdi` (= mc+0x488) |
| `0x11B249A` | update pos | `48 63 05 A7 70 B0 05` | rax | `rdi` (= mc+0x3D0) |
| `0x11B2B62` | update rot | `48 63 05 DF 69 B0 05` | rax | `rdi` (= mc+0x488) |

Todos caen en el rango permitido `[0x1000, 0x51EB000)`, no en `.xtls`. Las funciones se llaman por
punteros globales (`exe+0x6C8CC38/C40/C98/CA0`), así que cambiar esos punteros es una alternativa
sin tocar código.

**Lógica del stub** (estado por `mc`, compartido por posición y rotación):
1. `mc` = `rdi − 0x3D0` (posición) o `rdi − 0x488` (rotación).
2. **Objetivo** = `clamp(max(últimos 3 huecos de ts del búfer de posición) + 150, 200, 1800)`.
   - Personajes: el objetivo se queda en 200.
   - NPC a ~1,1 s: ≈1300.
   - Caballo montado con F2: 200.
3. El D aplicado se acerca al objetivo **sin saltos**:
   - si sube, como mucho 0,5 ms por ms (reproduce a 0,5× unos 2 s);
   - si baja, 0,25 ms por ms.
4. Se devuelve D en el registro destino.
5. Los sets usan el mismo D. Así, si hay reinicio, la muestra sintética queda en el instante mostrado
   (`now − D`) y no a `now − 200`.
6. Además, el parche de reinicio de F0 a 2500 ms, para que se use el tramo real entre muestras.

**Riesgos:**
- Hay que **confirmar que los cuatro sitios se ejecutan en el mismo hilo** que las pausas del Ack
  (registrar el TID en la primera prueba).
- Tope de 30 muestras: con D = 1800 y ≥ 17 Hz se perdería historial. No pasa con los actores `0xB`
  (todos escasos), y los densos se quedan en 200.
- Latencia visual de los NPC: ~1,3 s. Aceptable para el ambiente.
- En combate, el golpe del invitado se calcula contra su vista atrasada. **[H]** Medir si la IA
  en combate envía más a menudo (eso bajaría D sola). Si no, valorar F1-bis: extrapolar con
  `Path+0x28` (= `Ack+0x74`, velocidad), que está en la muestra en `+0x38`, en la rama "T pasó la
  última" (`0x11B2519`–`0x11B252A`), con un horizonte de ≤ 1,2 s.
  - Coste: rebasar el sitio al pararse (hasta 1 m andando, ~9 m un ave).

### F2. Jinete y caballo (en el servidor del anfitrión; cubre los dos sentidos)

En la interceptación de envíos por sesión que ya existe (la de `observer_hold` y `child hold`, en
las dos sesiones), por cada `MoveActorAck` (2697, 235 B) cuyo padre en `+0x8C` sea ≠ `0xFFFFFFFF`
y sea un vehículo (hijo tipo 5 de un usuario):
1. Encolar detrás un 2697 **sintético** para el padre, copiando el del jinete y cambiando:
   - `+0x00` = clave del caballo;
   - `+0x04` = `Ack[+0x94..+0xA0)`, la posición de mundo del caballo;
   - `+0x10` = `Ack[+0xA0..+0xB0)`, el quat del caballo;
   - `+0x8C` = −1;
   - la `ts` (`+0xD5`) y la cola (`+0xDE…`) se dejan igual.
2. Va solo a las sesiones a las que va el Ack del jinete, nunca a la de su dueño.
3. El 2697 propio del caballo (~1 Hz) lleva la misma `ts` que un Move del jinete (CAB), así que el
   búfer lo descarta (`ts ≤ última`). Coherente.

**Resultado:** las muestras del caballo pasan a ~36 Hz con la misma `ts` que las del jinete. Con F1,
los dos quedan en D = 200, sincronizados.

**Riesgos:**
- Durante la animación de montar (~2 s sin Move del jinete) el caballo vuelve a sus propias
  muestras.
- La longitud del Ack tiene que ser exacta (235 B; si no, se descarta en silencio, `mirror-ack-formats.md`, regla 1).
- Sigue sin existir el vínculo real de vehículo. Lo visual basta; lo físico, como colisiones del
  caballo, no se toca.

### F3. Alternativa en el emisor (no localizada)
Subir la frecuencia de `MoveActorReq` de la IA en el cliente dueño, de ~1 Hz a ~10 Hz, mientras haya
otro usuario.
- **Ventajas:** latencia baja y trayectoria exacta.
- **Inconvenientes:**
  - más tráfico: ~10× los Move de `0xB` (hoy 134/s en HC, ×235 B por Ack);
  - más carga del servidor;
  - **no localicé el regulador del envío**. Revisé dos `GlobalVariableInt` de 1000 cerca del
    movimiento (`0x6CBB178` y `0x6CBB1C8`, usadas en `0x8DF411` y `0x8DF69C`); son temporizadores de
    otro componente.

Queda abierto.

---

## 4. Qué medir en la próxima prueba (P8, sesión 8)

1. **M1, F0 A/B en el invitado** (y en el anfitrión para ver su mascota): un vídeo de NPC caminando
   con y sin el F0. Además, con una sonda de lectura, el `count` en `mc+0x3D0+0x10` y la última `ts`
   de 3 NPC cercanos cada frame. **Esperado sin F0:** el contador cae a 1–2 una vez por segundo
   (reinicio S1). **Con F0:** sin caídas.
2. **M2, modo de control:** en el invitado, para esos NPC, `0x88E8E0(mc)` (o leer `+0x90`, `+0xC8` y
   `[[CC+0x60]+0x10]`). Esperado: modo 2, `+0x90 = 0x90100000`, `+0xC8 = 0`.
3. **M3, monta en los dos sentidos:** captura de ambos clientes mientras cada jugador cabalga 30 s.
   Comprobar:
   - Ack del jinete con `+0x8C` = caballo y `+0x94` = posición del caballo;
   - cadencia del Move propio del caballo;
   - separación visual jinete-caballo en el remoto (`TS+0xB4` de los dos).
4. **M4, animales:**
   - (a) con un gato o una oveja en reposo cerca de los dos, ¿la animación de reposo se ve en el
     invitado? Registrar el hash de acción aplicado.
   - (b) **Nivel de detalle de la IA:** el anfitrión a más de 300 m del invitado. ¿Siguen saliendo
     `MoveActorReq` del anfitrión para los NPC que rodean al invitado (claves con 2005 en GC)?
5. **M5, hilos:** el TID de las llamadas a `0x11B2420` y `0x11B1FD0` frente al hilo del Ack
   (`0x35D0C0`). Si es el mismo, F1 puede guardar estado sin locks.
6. **M6, combate:** cadencia de `MoveActorReq` de un enemigo persiguiendo a un jugador. Decide si
   hace falta F1-bis.

## 5. Observaciones laterales (fuera de alcance)
- **El invitado también simula objetos del anfitrión.** Envía `MoveActorReq` y `RotateActorReq`
  para 3446 claves `0xB` que también simula el anfitrión:
  - gimmicks (`0xB0100160`: 749 Rotate);
  - los hijos acoplados del personaje del anfitrión `0xB0100150–155` (~483 Move cada uno).
  
  Son simulaciones dobles. Hoy el servidor no las reenvía al anfitrión (0 Ack en HC para esas
  claves), pero es un riesgo de divergencia si alguna se acepta.
- El cliente del anfitrión recibió 9960 `MoveActorAck` del personaje del invitado, que manda a
  ~30 ms. Por eso el personaje del otro se ve bien y solo fallan los actores con IA.
