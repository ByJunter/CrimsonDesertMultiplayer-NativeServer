# Control de los NPC lejos del anfitrión (R3): quién los simula y cómo dárselos al invitado

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Investigación de solo lectura (2026-10-12): no se abrió el juego, no se tocó el código del repositorio, no se compiló y
no se paró el servidor de Ghidra. `.xtls` solo se leyó (Ghidra y capstone), nunca como sitio de gancho.

Marcas: **[V]** verificado (captura, log, bytes o desensamblado comprobado); **[E]** leído en el código (decompilado o
desensamblado) sin prueba en vivo; **[H]** hipótesis.

Datos:
- Capturas de dos PC `C:\Emulador CD\backups\p8-sesion15-*` … `p8-sesion22-*` (cliente del anfitrión y de Shadow). La S16
  (`p8-sesion16-host\cdmp_packets_20261011-133910.bin`) es la del invitado a 180–199 m.
- Capturas de un jugador `C:\Emulador CD\captures\cdmp_packets_20261003-200328.bin` y `…-112627.bin`.
- Punto de partida: `mundo-invitado.md` (ancla de mundo del usuario 2, implementada y comprobada en un PC). Este informe
  trata su riesgo **R3 / incógnita I2**.
- Scripts en §8 (scratchpad de la sesión, fuera del repositorio).

---

## 0. Resumen

**1. Quién controla un NPC [E + V].** El control (`ServerActor+0x90`, la clave de usuario que simula la IA) se decide
**una sola vez, al crear el actor**, y para el mundo es **siempre el dueño del campo**.
- Punto único: `ICreateServerActorDesc::vf3` `0x2791CD0`, bloque `0x2792A11…0x2792A7B`.
  - Si `+0x90` ya tiene un valor, lo conserva.
  - Si no: `[desc+0x40]+0x60` (actor explícito) → `[actor+0xA0]+0x60` (usuario padre) → `[campo+0x180]` (dueño del
    campo).
  - No mira la distancia ni quién pidió el spawn.
- Los spawns que pide el invitado (2424/2550) no rellenan `desc+0x40` (`0x27EA160`), así que salen con el anfitrión de
  controlador. En la S16, los 70 NPC nacidos a 150–250 m del anfitrión tenían todos `0x90100000` [V].
- **No hay reasignación nativa.**
  - De las 114 escrituras `mov dword [reg+0x90]` del rango servidor, solo `0x2792A7B` toca el controlador.
  - El recolector no lo lee.
  - `ChangeAiControlAck` 2508 sale solo del truco de GM 2442. Exige que quien lo pide ya sea el controlador y, en el
    cliente, solo enciende o apaga la IA.
  - `OverriedOwnershipStateTypeyAck` 2168 es el estado de propiedad de los gimmicks.
  - En S15–S22 hay 0 × 2508 y 10 × 2168, estos últimos de gimmicks tipo 7 durante la recarga del anfitrión [V].
- **No existe una regla «al más cercano» ni una Req con la que un cliente tome el control.** Solo hay dos palancas
  nativas:
  - el valor de `+0x90` en el momento de crear el actor;
  - el par de interés `NotifyDisappear` `0x2B00380` / `NotifyAppear` `0x2B000C0`, que hace que un cliente destruya y
    reconstruya un actor a partir de su 2005 actual.

**2. Qué hace el cliente del anfitrión con los NPC lejanos [V].** Simula **sin corte** los NPC que se alejan de él,
**al menos hasta ~640 m**.
- Animales que huyen, seguidos en las capturas:
  - S16: hasta 497 m;
  - S22: hasta 591 m, y luego vuelven;
  - un jugador (200328): hasta **643 m**, con la posición propia al día.
- A esa distancia siguen saliendo 2556/2223/2734/2405.
- Ritmo del 2556 de un mismo NPC corriendo: 1,10 s cerca y 1,20 s lejos, es decir, solo +5–10 %.
- En 79 trayectorias a 250–400 m y 13 a más de 400 m (puede haber NPC en los dos tramos) no hay ninguna caída
  (`dy/dt < −10 m/s`) ni ningún salto (> 25 m/s).
- **Lo que falta** son NPC que **nacen** lejos del controlador:
  - hay datos hasta 250 m: S16, 70 NPC, con IA a los 0,2–0,3 s;
  - no los hay más allá, que es justo el caso del invitado a 300–600 m;
  - y no hay datos de nada por encima de ~650 m.

**3. Diseño propuesto: «control para el jugador más cercano al nacer» con traspaso nativo a la salida.** Es un solo
diseño con tres piezas: A y B obligatorias, C opcional.
- **A, al nacer.** Gancho de entrada en `0x2791CD0`. Pone `+0x90` = clave del usuario 2 **antes** del original cuando se
  cumple todo esto:
  - es una Req de spawn del usuario 2 (sujeto de `ExecuteTroc`; opcodes 2424/2550, y después 2809/2327);
  - el descriptor es el genérico (vtable `0x5B04BE8`);
  - el actor es de tipo 3/6, sin padre y sin `desc+0x40`;
  - los dos jugadores están separados: puerta encendida a ≥ 300 m, apagada a ≤ 200 m.
  - La rama nativa conserva ese valor y el 2005 nace con el usuario 2 de controlador: el invitado lo simula y el
    anfitrión lo ve en modo 2.
  - Nunca cambia durante su vida: no hay vaivén ni doble simulación.
- **B, a la salida.** Antes del 2437/2283/2001 del usuario 2 (o si se corta), para sus NPC a menos de 300 m del anfitrión:
  - cerrojo del actor, `+0x90` = anfitrión, `NotifyDisappear([s1],[k],18)`;
  - ≥ 200 ms después, `NotifyAppear(0,[s1],[k])`;
  - el anfitrión reconstruye el actor como local;
  - el resto lo retira el recolector.
- **C, en vida (opcional).** Solo si en dos PC algo falla por encima de ~600 m. Usa la misma primitiva con histéresis:
  500/150 m, 10 s estable, 60 s por clave y fuera de combate.
- **Un PC** sirve para comprobar:
  - la fontanería, con un modo de prueba que asigna al peer los spawns del anfitrión;
  - el traspaso B;
  - la ida y vuelta de C;
  - el R3 de los NPC que se quedan atrás.
- **Dos PC** hacen falta para la IA del invitado, los NPC que nacen lejos, el combate y el aggro.

---

## 1. Pregunta 1: cómo asigna el servidor el control

### 1.1 Al crear: un solo punto [E + V]

Ruta de un spawn pedido por un cliente [E]:

```
2424 ActivateSpawningPoolReq → 0x2C41AF0 → .xtls 0x108797A0 → 0x27EE3E0 ─┐
2550 AutoSpawnRoadReq       → 0x2C41E50 → .xtls 0x10880460 → 0x27E8C90 ─┤
                                                                         ▼
0x27EA160 (núcleo de pools): por cada spawn, desc = new 0x4E8 B (ctor 0x278EA70, vtable
          pa::ICreateServerActorDesc 0x5B04BE8); rellena posición, radio, etc.; NUNCA desc+0x40
   → campo->vf17 0x2AF9AE0 (ServerField::CreateActors, vtable 0x5B29A60 slot 17)
       → .xtls 0x10649310 → desc->vf3 = 0x2791CD0 (por cada actor)
   → los descriptores se destruyen al volver (bucle de vf0(desc,1)): la creación es SÍNCRONA, dentro de la Req
```

Dentro de `0x2791CD0` (firma `(desc, &res, campo, actor)`):

```
0x2792A11  41 8B 86 90 00 00 00  mov eax,[r14+0x90]      ; r14 = actor
0x2792A18  85 C0 / 75 5F         test/jne 0x2792A7B      ; ya tenía controlador -> lo reescribe igual
0x2792A1C  48 8B 46 40 ...       desc+0x40 ? [desc+0x40]+0x60
0x2792A2A  ...                   actor+0xA0 ? [[actor+0xA0]+0x60]     (usuario padre: mascotas, caballos)
0x2792A70  41 8B 85 80 01 00 00  mov eax,[r13+0x180]     ; r13 = campo: el DUEÑO
0x2792A7B  41 89 86 90 00 00 00  mov [r14+0x90],eax
```

Comprobaciones:
- `0x2791CD0` es la vf3 de `ICreateServerActorDesc` (`0x5B04BE8`) y de `CreateServerActorDesc_DailyRoutine`
  (`0x5B04CF8`). La llaman directamente las vf3 derivadas `0x27A4A40` y `0x27A6160`. Por tanto **toda** creación desde
  descriptor pasa por este bloque [E].
- La secuencia de `0x2792A11` es única en el exe (16 B) [V bytes].
- `0x27EA160` no escribe `desc+0x40`, ni en el decompilado ni en sus campos `[8]` [E].
  - `0x27E8C90` (2550) lo llama con `param_9 = 0`.
  - La lista de descriptores va directa a `campo->vf17`.
- **En vivo [V]:**
  - S16 t = 186,24: el invitado manda 14 × 2550 y nacen `0xB0100704…070A` con controlador `0x90100000`
    (`mundo-invitado.md` §1.2).
  - 6B: 1 685 de 1 693 actores con `0x90100000`; los 8 restantes son gimmicks y el Kliff del usuario 2.
  - `bornfar.py` (§2.3), con todos los nacidos de tipo 3/6 de S15/S16/S22 y de los dos de un jugador: controlador =
    usuario local en todos menos uno por captura de dos PC (S16: 1 de 919; S15: 1 de 593, en el tramo de 50–100 m; no se
    examinó cuál).

Conclusión: **el que pide el spawn y el que está más cerca no cuentan.** El controlador de lo que nace en el campo
compartido es el anfitrión, salvo los hijos de un usuario (`+0xA0`).

### 1.2 Después de crear: no se reasigna [E + V]

| Pieza | Qué hace | Evidencia |
|---|---|---|
| Escrituras de `+0x90` en el servidor | De las 114 escrituras `mov dword [reg+0x90]` de `0x2780000–0x2D00000` (inventario de D0, `optd\srv_wr_90.txt`), 92 son variables de la pila (55 con `rsp` y 37 con `rbp` de puntero de marco). Las 22 restantes las clasifiqué una a una (`wr90.py`). 21 son de otro tipo: estructuras de la pila con `rbp` sin marco, inicializaciones a 0 de objetos recién creados (`0x27AB3ED`, `0x27AB612`, `0x2BB89A5`, `0x2CD65C4`, `0x2CD6BD9`, `0x2CD6EEF`, `0x287855A`), copias de estructuras (`0x28C1BA6`), acumuladores (`0x2C6388E`) o contadores de vector y estado (`0x2B7C0DA`, `0x2C6509E`, `0x2C5D80A`, `0x28792A2`). **Solo `0x2792A7B` es el controlador** | [E]. No barrí `.xtls` |
| Recolector `0x2C37340` | No lee `+0x90`: ni «controlador desaparecido» ni «controlador lejos» | [E] (decompilado completo) |
| Otros recorridos del SAM | Los 11 llamadores de `SAM::GetActorList` `0x2A83CB0` (temporizadores `0x2C3A1E0…0x2C3B520`, `0x2959540`, `0x287BCE0`…) no tocan `+0x90` | [E] |
| 2508 `ChangeAiControlAck` | Escritor `0x297B430`, solo desde el truco `AiControlChangeCheatReq` 2442 (`0x29BFB80` → `0x2C41360`). Pide permiso de GM y que quien lo pide **ya** sea el controlador (`0x2C414E4–0x2C41500`). La carga solo lleva la clave. En el cliente (`0xB681F0`), si `[actor+0x90]` es el usuario local, llama a `AiControlCheat_Step` (enciende o apaga la IA) y lanza una acción. **No transfiere** | [E]; 0 × 2508 y 0 × 2442 en las 16 capturas de S15–S22 [V] |
| 2168 `OverriedOwnershipStateTypeyAck` | `0x2A9B670` → escritor `0x29DD490`: byte `+0x61` de un componente de gimmick, por ToNear | [E]. S16 t = 500,2 (2283/2437 del anfitrión): 10 × 2168 `{k, k, 4}` de 10 gimmicks tipo 7, iguales en los dos clientes; 0 en las demás sesiones [V] |
| Validación de las Req de IA | `MoveActorReq` `0x2977E10`: si `sesión->vf170() != [actor+0x90]`, **se ignora en silencio** (resultado 0, sin Nak). `StartActionReq` `0x296CAA0` → `ServerStartActionLogic`; `DoFrameEventReq` exige que el atacante sea de la sesión | [E] |
| Cliente | `0x17EA310` copia el controlador del registro del 2005 en `actor+0x90`; el modo sale de `0x88E8E0` (1 local, 2 remoto, 3 simulado). El «cerebro» de IA (`[[ai+0x48]+8]`, comprobado por `0x9245B0`) solo existe en lo que el cliente controla desde la carga. Ningún paquete cambia el controlador de un actor ya creado | [E] (`movimiento-remoto-cliente.md` §1.1, `p8-arquitectura-observador.md` §5) |
| Grupo | `InvitePartyMemberReq` / `JoinPartyOfReq` exigen el **mismo** controlador (`0x2C3C6DA`, `0x2C3CBD4/0x2C3CC08`) | [E] (`vision-a1-mundo.md` §7.2) |

**Respuesta:**
- No hay una regla nativa «dar el control al usuario más cercano» que se pueda activar.
- Un cliente no puede pedir el control: 2442 es de GM y solo actúa sobre lo que ya controlas.
- Las dos palancas nativas son:
  1. **El valor de `+0x90` cuando corre `0x2792A11`.** Si ya es distinto de 0, el juego lo conserva: es la rama
     `jne 0x2792A7B`.
  2. **El par de interés del campo:**
     - `NotifyDisappear(rcx = &{sesiones}, rdx = &{claves}, r8b = motivo)` `0x2B00380` (en `.rsrc` es un `jmp` a
       `.xtls 0x10650890` → `RemoveActorAckWriter 0x29756F0`). Motivo `0x12` = salir del sector
       (`0x2AF94A4`, `0x2AF97B7`).
     - `NotifyAppear(cl = flag, rdx = &{sesiones}, r8 = &{claves})` `0x2B000C0`: el **único** emisor de 2005, con el
       estado actual del actor.
     - Es lo que hace el juego cuando un actor entra en o sale del interés de un usuario
       (`ServerField::OnActorSectorChanged` `0x2AF8AF0`). El mod ya llama a `NotifyAppear` (`child_hold` / `child_settle`).

---

## 2. Pregunta 2: el cliente del anfitrión con NPC lejos de él

### 2.1 Reqs por distancia (S16, cliente del anfitrión) [V]

`farai.py`: cada C>S de IA o movimiento de un NPC tipo 3/6 contra la distancia 2D al personaje propio en ese momento.
Las posiciones de los NPC salen de su propio 2556. El «hueco» es el tiempo entre dos 2556 seguidos del mismo NPC.

| Distancia (m) | 2556 | 2223 | 2734 | 2405 | NPC | Mediana del hueco (s) | p90 |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0–25 | 4 124 | 3 538 | 4 041 | 377 | 430 | 1,07 | 1,64 |
| 50–75 | 14 613 | 12 562 | 7 461 | 2 667 | 1 655 | 1,12 | 1,78 |
| 150–175 | 3 385 | 1 995 | 300 | 1 322 | 1 170 | 1,13 | 1,78 |
| 200–225 | 1 860 | 1 032 | 245 | 621 | 899 | 1,13 | 1,86 |
| 225–250 | 391 | 207 | 79 | 124 | 225 | 1,15 | 1,89 |
| 250–300 | 181 | 117 | 54 | 75 | 78 | 1,20–1,25 | 1,90 |
| 300–400 | 113 | 164 | 85 | 66 | 43 | 1,26–1,77 | 1,96–2,09 |
| 400–500 | 38 | 317 | 180 | 22 | 14 | 1,68–1,86 | 1,92–1,97 |

- **Actividad:** cada 5 s, de los NPC vivos que controla el anfitrión, cuántos tuvieron ≥ 1 Req en los 5 s anteriores.
  - 70–81 % a menos de 125 m: hay NPC quietos que no mandan nada.
  - **92–100 % a 125–500 m**: lo lejano que existe, se mueve.
- Otras capturas, 2556 a más de 250 m:
  - un jugador 200328: 2556 hasta **643 m** (huecos de 1,55–2,09 s); a menos de 250 m, 1,19–1,24 s;
  - S22: hasta 591 m (1,74–1,83 s frente a 1,02–1,18 s);
  - S15: hasta 496 m.
- **El ritmo baja poco con la distancia [V].** `gapsame.py` compara el **mismo** NPC corriendo (> 3 m/s), cerca
  (< 200 m) y lejos (> 250 m):

  | Captura | NPC | Mediana del hueco cerca | Lejos |
  |---|---:|---:|---:|
  | S16 | 23 | 1,10 s | 1,20 s |
  | S22 | 3 | 1,67 s | 1,82 s |
  | 200328 | 3 | 1,75 s | 1,89 s |
  | S15 | 3 | 1,15 s | 1,17 s |

  - Lo que manda en el ritmo es el animal: unos van a 1,1 s y otros a 1,7 s.
  - La distancia solo añade un 5–10 %. **No hay un corte de LOD hasta ~640 m.** Si el +5–10 % es un LOD suave o el
    efecto de correr en línea recta es [H].

### 2.2 Trayectorias concretas [V]

`farnpc.py`; «edad» = antigüedad de la posición propia (el anfitrión estaba quieto o la tenía al día).

| Captura | NPC | Recorrido (distancia al anfitrión) | Qué pasa al final |
|---|---|---|---|
| S16 | `0xB01003A6` (tipo 6) | Nace a 17 m; huye a 8,3–9,0 m/s: 26 → 199 → 302 → 430 m (t 158 → 214) | Lo retira el recolector por motivo 25 **a 430 m**: su radio `+0x468` es mayor que la mediana de ~210 m |
| S16 | `0xB0100AFC` (tipo 6) | 28 → 497 m a 7,5 m/s; la Y sube y baja con el terreno (+19,8, +22,2, −16,8, −23,1…) | 2556 hasta t 497,4. A los 498,2 el anfitrión manda 2283 y a los 500,2 su 2437 (recarga propia): la simulación se corta por su carga, no por la distancia |
| S16 | `0xB01006E2` | 244 → 389 m, 2556 cada ~1 s | Motivo 25 a 389 m |
| S22 | `0xB01004B6` (tipo 6) | 13 → 591 m y **vuelve** a 314 m, a 7,2–7,5 m/s, con el anfitrión quieto | Motivo 25 a 314 m |
| S22 | `0xB01004D5` (tipo 6) | 13 → 446 → 19 → 144 m | Motivo 25 |
| 200328 (un jugador) | `0xB0100969` (tipo 6) | 25 → **643 m** a 8,2–9,3 m/s, con la posición propia al día (edad 0,0 s) | Fin de la captura |
| 200328 | `0xB0100AB5` (tipo 6) | 14 → 404 → 54 → 565 m | — |

**Física [V], `vz.py`.** Pares de 2556 seguidos de NPC 3/6 (0,2 s < Δt < 5 s):

| Captura | < 250 m | 250–400 m | > 400 m | Caídas (`dy/dt < −10 m/s`) | Saltos (> 25 m/s) lejos |
|---|---|---|---|---|---|
| S16 | 58 278 pares | 290 pares / 47 NPC | 38 / 5 | **0** (mínimo −7,7 m/s lejos) | **0** |
| S22 | 47 996 | 96 / 8 | 55 / 3 | 0 | 0 |
| 200328 | 29 567 | 249 / 17 | 109 / 4 | 0 | 0 |
| S15 | 63 966 | 69 / 7 | 18 / 1 | 0 | 0 |

- La Y lejos se comporta igual que cerca: sube y baja con el terreno y no cae en picado. **El anfitrión tiene suelo
  (colisión o malla de navegación) donde están sus NPC, al menos hasta ~640 m** [V para la Y; de qué sistema sale es [H]].
- Varios animales dan media vuelta a 429–643 m (`0xB0100BB0`, `0xB0100BDD`, `0xB01004B6`, `0xB01004D5`). No se distingue
  entre la correa de la IA de huida (la distancia a su spawn coincide con la distancia al anfitrión) y el borde de la
  malla cargada por el anfitrión [H].

### 2.3 Los que nacen lejos del controlador [V]

`bornfar.py`: NPC 3/6 controlados por el cliente, agrupados por la distancia entre su posición de spawn (2005) y el
personaje propio al nacer. Se cuentan sus Req en los 30 s siguientes.

| Captura | Nacidos a 150–250 m | Con IA (≥ 1 Req) | Se movieron (2556) | Mediana de la 1.ª Req | Más de 250 m |
|---|---:|---:|---:|---:|---|
| S16 (invitado a 180–199 m) | **70** | **70** | 51 | 0,19–0,33 s | 2 (uno con la posición mal) |
| S15 | 13 | 13 | 7 | 0,28–0,30 s | 0 |
| Un jugador (200328, 112627; posición propia ≤ 2 s) | 1 / 0 | — | — | — | 0 |

- En un jugador todo nace a **< 150 m** del jugador, que es su radio de streaming de spawns [V]. Los «nacidos a
  500–1000 m» del primer recuento eran posiciones propias viejas (a caballo); desaparecen al exigir que la posición
  propia tenga ≤ 2 s.
- **Por tanto:**
  - hasta 250 m de nacimiento: comprobado (S16) [V];
  - de 250 a 640 m: solo se sabe para NPC que se **alejaron** después de nacer cerca [V];
  - **para NPC que nacen a 300–600 m del controlador (el invitado lejos) no hay ningún dato.** Es justo cuando el
    anfitrión tiene que crear el modelo, la física y el cerebro en una zona que **nunca** ha cargado [H: que falle o no].

### 2.4 Conclusión para R3

| Caso | Riesgo con el anfitrión de controlador | Base |
|---|---|---|
| NPC que nació junto al anfitrión o al invitado a ≤ 250 m y después se alejó (o se alejó el anfitrión) hasta ~640 m | **Bajo**: IA, 2556 y Y correctos | [V] (§2.1, §2.2) |
| NPC que nace a 250–600 m del anfitrión (lo que pide el invitado lejos) | **Desconocido** | Sin datos (§2.3) |
| Cualquier NPC a más de ~650 m del anfitrión | **Desconocido** | Sin datos |
| Lo que pide el invitado a 1–2 km (mundo abierto) | **Alto** [H] | El anfitrión no ha cargado nada allí |

Con el ancla de mundo puesta, el invitado puede estar a cualquier distancia. Para cubrir lo desconocido y lo alto, los
NPC del invitado lejano los tiene que simular su propio cliente.

---

## 3. Pregunta 3: diseño. Control para el jugador más cercano **al nacer** y traspaso nativo **a la salida**

### 3.0 Principios

1. **Juntos no cambia nada.** Con los dos a menos de 300 m, todo sigue igual que hoy, que es lo probado.
2. **Se decide al nacer y no se cambia en vida.** Un 2005 que ya nace con el usuario 2 de controlador es el camino nativo
   de los dos clientes (el modo y el cerebro se deciden al cargar). No hay reconstrucción, ni salto visual, ni vaivén.
3. **Solo para lo que pide el invitado y cuya IA puede correr en su cliente:** animales y NPC genéricos de pools y
   carreteras. Quedan fuera los horarios, las escenas y los stages del anfitrión, que el invitado no tiene
   (`p8-arquitectura-observador.md` §2.4).
4. **Ningún NPC se queda sin controlador:** antes de que salga el usuario 2, lo suyo vuelve al anfitrión (pieza B).
5. Reglas de CLAUDE.md:
   - un solo gancho nuevo, en la **entrada** de una función de `.rsrc`;
   - nada en `.xtls`;
   - lecturas con SEH;
   - identidad solo por clave completa (`+0x60`), nunca por dirección.

Interruptor propuesto: `[session2] npc_control = 0 | probe | 1`, solo en el anfitrión, con `create_session=1`,
`user2_world_anchor=1` y `relay_fix=1`. Umbrales en el ini: `npc_control_on_m = 300`, `npc_control_off_m = 200`,
`npc_control_handover_m = 300`.

### 3.1 Pieza A: asignación al nacer (servidor)

**Gancho:** `safetyhook` inline en la entrada de `ICreateServerActorDesc::vf3` **`0x2791CD0`**.
- Prólogo `48 8B C4 48 89 58 18 48 89 50 10 55 56 57 41 54 41 55 41 56 41 57 48 8D 68 A1 48 81 EC D0 00 00`.
  - Esos 32 B aparecen 3 veces en el exe, así que se comprueban **en esa RVA** y no por AOB.
  - Además, la secuencia de `0x2792A11` (`41 8B 86 90 00 00 00 85 C0 75 5F 48 8B 46 40 48`, única) tiene que coincidir.
- Las dos primeras instrucciones (`mov rax,rsp`, `mov [rax+0x18],rbx`, 7 B) no usan RIP: se pueden reubicar.
- Argumentos: `rcx = desc`, `rdx = &resultado`, `r8 = campo`, `r9 = actor`.

**Condición** (todas; si alguna lectura falla, el original sin tocar):
1. **Sujeto = personaje principal del usuario 2.**
   - Es el `thread_local` que ya pone el desvío de `ExecuteTroc` `0x2774B20` (`user2_funnels`: clave, `+0x90` y clase
     comprobados).
   - Opcode de la Troc en curso ∈ {2424, 2550}. 2809 y 2327, primero solo en `probe`.
   - Requiere que `user2_funnels` instale el sujeto aunque sus otros embudos estén a 0.
2. `[desc]` = `exe+0x5B04BE8` (descriptor genérico; excluye `DailyRoutine` `0x5B04CF8` y los derivados).
3. `[actor+0x90] == 0`, `[desc+0x40] == 0` y `[actor+0xA0] == 0` (ni explícito ni hijo).
4. Tipo `[[actor+0x88]+1]` ∈ {3, 6}. `vf3` lo lee en sus primeras ramas (los `switch` sobre ese byte, antes de fijar
   el controlador), así que ya es válido en la entrada [E].
5. `campo` = el campo del usuario 2, con `[campo+0x180]` = la clave del anfitrión, abierto (`+0x4A = 1`) y con el ancla
   puesta (`+0x2E4 = 1`, `+0x2E8` = personaje del usuario 2).
6. **Puerta de separación encendida.**
   - Distancia 2D entre el personaje del anfitrión y el del usuario 2, de la misma lectura que ya hace `world_anchor`
     (gestor de actores del cliente del anfitrión), publicada como atómico cada ≤ 1 s.
   - Se enciende con ≥ `on_m` (300 m) mantenida 5 s y se apaga con ≤ `off_m` (200 m).
   - La histéresis está en la puerta, no en el NPC: un NPC decide una vez.

**Acción:** `[actor+0x90] = clave del usuario 2` (u32: la `+0x60` de su `ServerUserActor`, la misma que devuelve el
`vf170` de su sesión). Después, el original.
- La rama `0x2792A1A jne` conserva ese valor y no pasa por `FieldInterestNode_GetSessionList` (`0x4A2010`), igual que
  las ramas nativas de `desc+0x40` y del padre [E].
- No hace falta cerrojo: el actor aún no se ha publicado y `vf3` escribe `+0x90` sin cerrojo (`0x2792A7B`) [E].

**Hilo:** el de la Req del usuario 2 (ámbito servidor, TLS `+0x1EC = 1`).
- Ese hilo ya ejecuta `ExecuteTroc` → manejador → `0x27EA160` → `campo->vf17` → `.xtls 0x10649310` → `vf3`, de forma
  síncrona [E].
- El desvío: sin sujeto, vuelve al original en unas pocas instrucciones. Solo atómicos y lecturas con SEH; ninguna
  llamada al juego.

**`probe`:**
- no escribe; cuenta por opcode, tipo y separación;
- deja en un anillo sin bloqueo `would assign key/type/op/sep`;
- el hilo del mod escribe las líneas.

Verificación en `NotifyAppear`: el desvío de `child_hold` ya existe. Ahí se comprueba que cada clave asignada sale con
`+0x90` = usuario 2. Línea: `NpcControl [announce] … controller user 2 ok / MISMATCH`.

**Qué recibe cada cliente:**

| Momento | Anfitrión (s1) | Invitado (s2) |
|---|---|---|
| Nacimiento | 2005 con controlador = usuario 2 → **modo 2** (remoto), sin cerebro, interpola | El mismo 2005 → controlador = su usuario → **local** (modo 3 y cerebro), como en un jugador [E] |
| Vida | Relevos `2697/2991/2404/2723` de las Req del invitado (ToNearExceptOneControl, que esta build trae vacío: `relay_fix` ya excluye la sesión del controlador cuando es `key2`, `server.cpp` `finder_common`) | Manda `2556/2223/2734/2405/2291/2828/2380` del NPC; el servidor las acepta (controlador = su sesión). Recibe los Ack ToTarget del controlador (`2218` AiEvent, `2167` PreemptActionPoint, `2273`, `2933`) por `take_frame` |
| Combate | Sus golpes (`DoFrameEventReq` 2380 con objetivo de otro controlador): válidos. Ve la reacción por relevos | El NPC reacciona en su propio cliente, **sin el retardo de ida y vuelta** de `combate-retardo.md` |

Volumen [E, estimado con la S16]:
- por NPC recién nacido, ~43 Req en 30 s (16,9 × 2556 de 514 B, 11,8 × 2223 de 725 B, 8,3 × 2734 de 672 B, 6,5 × 2405
  de 57 B), unos 0,8 KB/s;
- 30 NPC activos son ~15–25 KB/s del invitado al anfitrión por el túnel;
- los relevos de esos NPC cambian de sentido (ya no van del anfitrión al invitado).

**Alternativa A′ (sin gancho nuevo):** escribir `+0x90` en el desvío de entrada de `NotifyAppear` (`child_hold`) para
las claves que se anuncian por primera vez dentro de una Req de spawn del usuario 2.
- Ventaja: no añade ganchos.
- Inconveniente: la creación (`vf3` y sus componentes) ya habría visto al anfitrión de controlador.
- Solo como plan B si el prólogo de `0x2791CD0` no coincide.

### 3.2 Pieza B: traspaso al anfitrión a la salida (obligatoria con A)

**Por qué:**
- Ni el servidor ni el recolector reasignan (§1.2).
- Un NPC del usuario 2 que siga vivo después de su salida (dentro del radio del anfitrión) quedaría en modo 2 en el
  anfitrión, **congelado**.
- Las Req del anfitrión sobre él se ignorarían en silencio (`0x2977E10`).
- Además, una clave de usuario reutilizada podría «adoptarlo».

**Cuándo:**
- en la **entrada** de `ExecuteTroc` del 2437 / 2283 / 2001 del usuario 2 (desvío de `user2_funnels`, hilo del
  manejador), antes del original. Son los mismos tres momentos en que `world_anchor` quita la ranura;
- y en el respaldo de «usuario 2 desaparecido» de `world_anchor` (corte sin 2001).

**Qué NPC:**
- El libro de A: claves asignadas en esta época del invitado.
- Más cualquier 2005 que `take_frame` haya visto con controlador = `key2`.
- Cada uno se comprueba con `SAM::FindActor 0x2A827B0` → `[actor+0x60]` = clave completa (generación incluida),
  `[actor+0x90]` = `key2` y `[actor+0xA0]` = 0.
- Se traspasan los que están a **< `handover_m` (300 m) del anfitrión**.
- Los demás los retira el recolector: con el ancla quitada, la siguiente pasada (≤ 5 s) borra lo que esté fuera del radio
  del anfitrión. Una segunda barrida a los 6 s traspasa los que sobrevivan, porque su radio era mayor.

**Pasos:**
1. **Cambio en el servidor:**
   - `Actor_Lock(&h, [actor+8], 1)` `0x393650`; es el cerrojo que toma `MoveActorReq`. El mod usaría la variante sin
     espera (`TryLock` vf5), como `world_anchor`.
   - Volver a comprobar el estado, escribir `[actor+0x90] = key1` y soltar el cerrojo.
2. `NotifyDisappear(&{s1}, &{claves}, 0x12)` `0x2B00380`, en el mismo hilo (ámbito servidor). El anfitrión recibe
   `RemoveActorAck` 2516 de motivo 18 y destruye el actor.
3. **≥ 200 ms después:** `NotifyAppear(0, &{s1}, &{claves})` `0x2B000C0`.
   - Va por la liberación diferida de `child_settle`, que ya llama a `NotifyAppear` solo en hilos donde el juego lo hace.
   - El anfitrión recibe el 2005 **actual**: posición, PV y byte de restauración de la acción (`opcion-d-accion-usuario2.md`
     §1.2), con controlador = él.
   - Lo crea **local** (modo 3 y cerebro) y retoma la IA.
4. El invitado no recibe nada:
   - con 2001 se va;
   - con 2437 o 2283, al volver (2762) recibe la instantánea del campo con controlador = anfitrión.

**Riesgo propio:**
- Entre el 2516 y el 2005 el cliente podría tener todavía la clave registrada. En ese caso el 2005 fallaría con
  `eErrNoDuplicateCreateActor` (`0xA1B3B447`) o «Actor 생성에 실패했습니다» (`0x8BDC4A`, `c1-actor-rechazado.md`).
- Por eso la espera. Además, `game_errors` debe contar esos códigos.
- **Respaldo:** si el anfitrión no vuelve a mandar Req del NPC en 3 s, retirarlo con `ServerActor::Logout 0x2068340(actor,
  out, 0x19, err)`, la misma llamada del recolector (motivo 25). La reaparición por los pools del anfitrión es I6
  (`mundo-invitado.md`).

### 3.3 Pieza C (opcional): traspaso en vida con histéresis

Solo si la prueba de dos PC muestra NPC mal simulados por un controlador lejano (más de ~600 m), en cualquiera de los dos
sentidos.

- **Regla.** El NPC K (tipo 3/6, sin padre, clave completa comprobada) cambia de controlador C a O si:
  - d(K, personaje de C) > **500 m** y d(K, personaje de O) < **150 m** durante **10 s seguidos** (dos vigilancias de
    5 s);
  - K lleva ≥ **60 s** sin traspaso;
  - K no está en combate: ningún 2380 suyo ni contra él, y ningún `VaryHPAck` 2147 suyo en 15 s, por el grifo del
    sniffer.
- **Por qué no hay vaivén.** Para volver, K tendría que pasar de estar a < 150 m de uno a > 500 m de ese mismo jugador y a
  < 150 m del otro: una separación ≥ 350 m que no se recorre por oscilación. A eso se suma la espera por clave.
- **Primitiva:** la de B, con `{s1, s2}` en `NotifyDisappear` y `NotifyAppear`.
  - El cliente que deja de controlar lo recrea en modo 2.
  - El nuevo lo recrea local.
  - Coste: un parpadeo de reconstrucción en los dos. Si está lejos de uno de ellos, ese no lo ve.
  - La IA del nuevo controlador empieza de cero (aggro y objetivo se pierden), y por eso se exige estar fuera de combate.
- **Hilo:**
  - la selección va en la vigilancia de 5 s de `world_anchor` (solo lecturas);
  - la ejecución, en el próximo hilo del servidor que valga para `NotifyAppear`, como B.

### 3.4 Qué queda fuera de A, y por qué

| Fuera | Motivo |
|---|---|
| NPC de horarios y rutinas (`CreateServerActorDesc_DailyRoutine`, vtable `0x5B04CF8`) | El invitado no carga `NPCScheduleData_File` (`p8-arquitectura-observador.md` §2.4): su IA fallaría |
| NPC creados por stages o escenas del anfitrión (sin sujeto del usuario 2) | Atados al gestor del dueño (vf48); el invitado no los tiene |
| Hijos de un usuario (`+0xA0`) y actores con `desc+0x40` | Ya tienen su controlador nativo |
| Tipo 7 (gimmicks, objetos) | Van por 2758/2974/2573 y los pools de escena de cada cliente (R7 de `mundo-invitado.md`); no son IA |
| Tipos 4/5/8/9 | Primero en `probe`, para ver si los hay en las Req del invitado |

### 3.5 Riesgos

| # | Riesgo | Valoración | Mitigación |
|---|---|---|---|
| C1 | **Doble simulación** | Nula por diseño: un controlador por NPC desde el 2005. Las Req del que no controla se ignoran en silencio (`0x2977E10`) [E] | Contar en `probe`/1 los 2556 de claves de A que lleguen del anfitrión (deberían ser 0) |
| C2 | **Vaivén de autoridad** | Ninguno en A (una sola decisión). En C, histéresis 500/150 m + 10 s + 60 s | Línea por traspaso; contador de traspasos por clave |
| C3 | **Aggro y combate asimétricos**. Un enemigo del invitado aplica `AddAggro` en el cliente del invitado (exige control local). La lista de rango (`AICondition_CheckAIBattleType`, `.xtls 0xC5DC280`) solo se llena con objetivos locales, así que atacará poco al anfitrión: es el espejo del problema actual (`p8-sistemas-juego.md` §3.2) | Media | Juntos (< 200 m) no se asigna nada nuevo. Si molesta: el espejo de `rank_permit` en el cliente del invitado |
| C4 | El NPC necesita algo que el invitado no tiene (horario, escena, stage) | Media para tipo 3 de carretera [H]; baja para animales | Filtro de vtable y tipo; `probe` con nombres y tipos antes de activar |
| C5 | Huérfanos tras la salida del invitado | Alta sin B | Pieza B + barrida a los 6 s + respaldo con `Logout` |
| C6 | Doble creación al reconstruir (`eErrNoDuplicateCreateActor`) | Desconocida [H] | Espera ≥ 200 ms; `game_errors`; si falla, `Logout` |
| C7 | El anfitrión ve en modo 2 (interpolación a ~1 Hz) los NPC del invitado cuando se juntan | Media (estética) | La interpolación de `remote_interp` ya existe para el modo 2; o C en sentido inverso cuando el invitado se aleja |
| C8 | Módulos del invitado que dan por hecho que «todo 0xB es remoto» (`remote_interp`, `remote_motion`, `combat_follow`, `horse_follow`) | Baja: lo local no usa el búfer de red [E] | Revisarlos: deben mirar `actor+0x90 == usuario local` |
| C9 | Ack ToTarget del controlador (2218…) hacia s2 durante sus retenciones (`observer_hold_acks`) | Baja | Comprobar en `take_frame` que no se retienen sin 2005 previo |
| C10 | Carga de red y CPU del invitado (Shadow) | ~0,8 KB/s por NPC nuevo | Medir el túnel en la prueba de dos PC |
| C11 | Viaje del anfitrión con el invitado en el campo viejo (R9): los NPC del **anfitrión** junto al invitado se congelan y el ancla los mantiene vivos | **Existe ya**, independiente de A | La primitiva de B en sentido inverso (anfitrión → usuario 2) en el 2437 del anfitrión. Fuera de este diseño |
| C12 | `.xtls` sin barrer: una reasignación nativa escondida | Baja [H] | Si existiera, la contaría la verificación en `NotifyAppear` (controlador distinto del esperado) |

Lo que **no cambia**:
- el ancla 2 y el recolector, que protegen igual los NPC del invitado;
- la rama de 50 m (`0x2945EE0`, sin ancla 2; parecen cuerpos o restos que se esconden);
- la confianza en el 2573 con la ranura puesta;
- los pools que apaga el otro cliente (R7).

---

## 4. Verificación

### 4.1 Un PC (anfitrión con el juego + `cdmp_peer` de usuario 2)

El peer **no simula IA**: lo que controle se queda quieto. Tampoco pide spawns: `--capture` reenvía las Req tal cual, sin
reasignar la clave del personaje. Aun así se puede comprobar casi toda la fontanería:

| Paso | Configuración | Qué se mide | Esperado |
|---|---|---|---|
| 0. R3 de los NPC que se quedan atrás (ya posible con la build actual) | `user2_world_anchor=1`; el anfitrión se aleja 300 → 600 m del peer, 2 min, y vuelve | `farai.py`/`vz.py` sobre la captura del anfitrión: 2556/2223/2734 de los NPC junto al peer y su Y | Igual que §2: ritmo +5–10 %, sin caídas. **Si a 600 m se cortan las Req o la Y cae, la puerta de A se baja** |
| 1. A, modo de prueba (`npc_control_test=host_spawns`) | Mismas condiciones, pero el sujeto es el **anfitrión**: sus 2424/2550 asignan al peer los NPC que nacen | 2005 con `+0x12` = clave del peer; el anfitrión **no** manda 2556/2223 de esas claves (modo 2); el peer recibe 2218/2167 de ellas (su `--record`); `NpcControl [announce] ok` | Los NPC asignados se quedan quietos en el anfitrión (esperado: el peer no los simula). Ningún `FAULTED` |
| 2. B | Tras el paso 1, el peer se despide (guion `…bye`) y luego corta a mitad (`…cut8…`) | `[handover] N keys to the host`; 2516 de motivo 18 + 2005 con controlador = anfitrión en su captura; **el anfitrión vuelve a mandar 2556/2223 de esas claves en ≤ 1 s** | 0 × `eErrNoDuplicateCreateActor`, 0 × «Actor 생성에 실패»; los NPC vuelven a moverse en pantalla (captura con `CopyFromScreen`) |
| 3. C, ida y vuelta (orden de depuración) | Un ciervo junto al anfitrión → peer → anfitrión | Dos parejas 2516/2005; el anfitrión deja y retoma sus Req | Un parpadeo visible por traspaso (medirlo) |
| 4. `probe` de A real | `npc_control=probe` con el peer quieto | 0 asignaciones (el peer no pide spawns) | Comprueba que la puerta no salta por error |

### 4.2 Dos PC (Shadow de invitado), **necesita Shadow**

| Paso | Qué hace el usuario | Qué se mide |
|---|---|---|
| T1 (`npc_control=probe`, ancla = 1) | Anfitrión quieto; el invitado cabalga 300 → 600 m a una zona no visitada, 2 min | **El R3 de los NPC que nacen lejos**: los que nacen junto al invitado, ¿los mueve el anfitrión (2556, Y) y el invitado los ve moverse? Líneas `would assign` por tipo y nombre. Decide el umbral |
| T2 (`npc_control=1`) | Lo mismo | En la captura del invitado aparecen C>S 2556/2223/2734 de claves `0xB` (hoy solo las de su mascota); el anfitrión recibe sus relevos; en Shadow los NPC se mueven con suavidad. Combate con ellos y comparación del retardo con `combate-retardo.md` |
| T3 | El invitado vuelve junto al anfitrión y sale al título con NPC suyos cerca | B: el anfitrión los recibe y los mueve; ninguno congelado |
| T4 | El anfitrión viaja junto al invitado (mezcla de controladores) | Aggro asimétrico (C3), aspecto en modo 2 (C7) |
| T5 (si se hace C) | Separarse > 600 m dejando NPC atrás | Traspasos sin vaivén; parpadeo |

Datos: logs y capturas de los dos lados, FPS, `SAM+0x9C` y tráfico del túnel.

### 4.3 Líneas de log propuestas

- `NpcControl ON (probe|1): gate on ≥300 m / off ≤200 m, ops 2424,2550, types 3,6`
- `NpcControl [gate] on|off sep 312 m`
- `NpcControl [assign] key 0xB… type 6 op 2550 sep 412 m -> user 2 0x9010…`
- `NpcControl [announce] key … controller ok|MISMATCH 0x…`
- `NpcControl [handover] before 2001|2437|2283|lost: N keys to the host (M left to the collector)`
- `NpcControl [rebuild] key … host resumed after X ms | NO REQ -> logout`
- Cada 60 s: `NpcControl [minute] assigned A, alive B, handed over C, host Req on user 2's keys D (expected 0), duplicate-create E`

---

## 5. Incógnitas

| # | Incógnita | Cómo cerrarla |
|---|---|---|
| N1 | Si el anfitrión simula bien NPC que **nacen** a 300–600 m de él | T1 (dos PC) |
| N2 | Si el cliente del invitado ejecuta bien la IA de NPC de pools y carreteras con controlador = él (cerebro, malla de navegación, acciones) | T2 |
| N3 | Si un 2005 justo después de un 2516 de motivo 18 crea el actor sin duplicado; la espera mínima | Paso 2 de un PC |
| N4 | Qué es el +5–10 % de ritmo lejos (LOD suave o línea recta) y por qué los animales dan media vuelta a 429–643 m (correa o borde de la malla) | Paso 0 con un NPC quieto lejos |
| N5 | Si en `.xtls` (cuerpo de `ServerField::CreateActors 0x10649310`, salida de usuario) hay algo que use o reasigne `+0x90` | Verificación en `NotifyAppear` durante T2/T3 |
| N6 | Si las Req 2809/2327 del invitado pasan por el mismo descriptor genérico | `probe` en T1 |
| N7 | Comportamiento del anfitrión a más de ~650 m (ningún dato) | Paso 0 con 800–1000 m |

---

## 6. Direcciones (build 2976)

| RVA | Qué es | Notas |
|---|---|---|
| `0x2791CD0` | `ICreateServerActorDesc::vf3` (también de `DailyRoutine`): crea componentes y fija el controlador | **Gancho de A** (entrada). Prólogo de 32 B en §3.1 (3 apariciones en el exe: comprobar en la RVA) |
| `0x2792A11` / `0x2792A1A` / `0x2792A70` / `0x2792A7B` | Lee `+0x90` / `jne` «conservar» / `[campo+0x180]` / escritura | `41 8B 86 90 00 00 00 85 C0 75 5F 48 8B 46 40 48`, única |
| `0x5B04BE8` / `0x5B04CF8` | vtables `ICreateServerActorDesc` / `CreateServerActorDesc_DailyRoutine` (slot 3 = `0x2791CD0`) | Filtro de A |
| `0x278EA70` | Constructor del descriptor (0x4E8 B), 28 llamadores | |
| `0x27EA160` | Núcleo de pools: construye descriptores y llama a `campo->vf17` | No pone `desc+0x40` |
| `0x27EE3E0` / `0x27E8C90` | `ActivateSpawningPool` / `AutoSpawnRoad` | Entradas desde 2424 / 2550 |
| `0x2AF9AE0` | `ServerField::vf17 CreateActors` (vtable `0x5B29A60` + 0x88) → `.xtls 0x10649310` | Síncrono |
| `0x2774B20` | `ServerNet::ExecuteTroc` (llamadas `0x27742A9` síncrona y `0x2774A08` diferida) | Sujeto de `user2_funnels` |
| `0x2977E10` | `MoveActorReq::vf2`: `vf170 == [actor+0x90]` o se ignora sin Nak | |
| `0x296CAA0` | `StartActionReq::vf2` → `ServerStartActionLogic` | |
| `0x2B000C0` | `NotifyAppear(u8, &sesiones, &claves)`: único emisor de 2005 | Ya enganchado por `child_hold` |
| `0x2B00380` | `NotifyDisappear(&sesiones, &claves, u8 motivo)` → `.xtls 0x10650890` → `0x29756F0` | Motivo `0x12` en `0x2AF94A4` / `0x2AF97B7`; variable en `LeaveField 0x2BBEE4F` |
| `0x393650` | `Actor_Lock(&h, [actor+8], 1)` | Cerrojo de `MoveActorReq` |
| `0x2A827B0` | `SAM::FindActor` (índice, sin generación) | Comprobar `+0x60` |
| `0x2068340` | `ServerActor::Logout(actor, out, motivo, err)` → `.xtls 0xE0D9AF0` | Respaldo de B (`0x19`) |
| `0x2C37340` | Recolector | No lee `+0x90` |
| `0x297B430` / `0x2C41360` / `0x29BFB80` / `0xB681F0` | 2508: escritor / lógica del truco / 2442 vf2 / manejador del cliente | |
| `0x2A9B670` / `0x29DD490` / `0xBB1640` | 2168: estado del componente `+0x61` / escritor / manejador del cliente | |
| `0x17EA310` / `0x88E8E0` / `0x9245B0` | Cliente: copia de `+0x90` desde el 2005 / modo de control / IA sin cerebro | |
| `0x2A759B0` | `ToNearExceptOneControl::vf1` (vacío en esta build; `relay_fix`) | Ya enrutado por controlador |

---

## 7. Respuestas cortas

1. **Asignación:** al crear, en `0x2792A11–0x2792A7B`. Orden: `desc+0x40` → padre → **dueño del campo**. No influyen ni
   el que pide ni el más cercano.
   - No hay reasignación nativa: 2508 es un truco que exige ser ya el controlador y 2168 es de gimmicks.
   - No hay regla «al más cercano» ni Req para tomar el control.
   - Palancas: `+0x90` previo a la creación, y `NotifyDisappear` / `NotifyAppear`.
2. **El cliente del anfitrión** mueve sus NPC hasta al menos ~640 m: ritmo +5–10 %, sin caídas ni saltos, con IA (2223/2734)
   [V].
   - Sin datos para NPC nacidos a más de 250 m de él ni para más de ~650 m.
   - En la S16 mandó 2556 de NPC a 200–250 m con huecos de 1,13–1,15 s (p90 1,9 s) y actividad del 92–98 %.
3. **Diseño:** A (gancho de entrada en `0x2791CD0`, controlador = usuario 2 para sus spawns cuando están separados ≥ 300 m)
   + B (traspaso nativo al anfitrión antes de su salida) + C opcional (traspaso en vida con histéresis).
   - Un PC valida la fontanería y B/C.
   - Dos PC validan la IA del invitado, los NPC que nacen lejos y el combate.

---

## 8. Scripts (scratchpad de la sesión, `…\scratchpad\ctrl\`, fuera del repositorio)

| Script | Qué hace |
|---|---|
| `cap.py` | Lector CDPK (copia de `%TEMP%\noche-mundo\cap.py`) |
| `farai.py` | Req de IA y movimiento de los NPC del cliente por distancia al personaje propio; huecos; actividad cada 5 s |
| `farnpc.py` | Trayectorias de los NPC movidos a más de N m: posiciones, velocidad, Y, baja |
| `bornfar.py` | NPC agrupados por la distancia al nacer: IA en los primeros 30 s |
| `vz.py` | Velocidad vertical y horizontal de los 2556 por distancia (caídas y saltos) |
| `gapsame.py` | Hueco del 2556 del mismo NPC corriendo, cerca frente a lejos |
| `ops.py` / `o2168.py` | Recuento de 2508/2168/2442/2245/3060 y detalle de los 2168 |
| `win.py` | Ventana de tiempo de una captura (eventos de carga) |
| `wr90.py` | Clasificación de las escrituras `[reg+0x90]` del servidor |
| `aob.py` / `xd.py` | Unicidad de secuencias de bytes / desensamblado capstone (`.xtls` incluido) |
