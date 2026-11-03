# Compañeros: dónde publica el servidor un actor nuevo a los demás (D-53)

Fecha: 2026-10-09. Build 1.0.0.2976, base `0x140000000`, direcciones RVA. Solo lectura:
- Ghidra: servidor propio, instancia `f19a08ca1a45498d8e58c0bd7ab0bc97`, pid Java 30948. Parado con
  `server-stop.ps1 -Instance …` → `PARADO=si`, 0 procesos de la instancia, puerto 18490 sin escucha
  (solo conexiones en `TIME_WAIT`).
- Capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (PE leído a mano con `struct`).
  Scripts en `%TEMP%\claude\publica\` (`cdis.py`, `opscan.py`, `w130.py`, `req2223.py`, `ccrec.py`,
  `scanhatch.py`, `acks.py`, `diffdog.py`), ejecutados con `python -I -B`.
- Capturas: `D:\cdmp-grabaciones\c1g.bin` (sesión 2), `C:\Emulador CD\captures\cdmp_packets_20261003-112627.bin`
  y las 32 capturas del anfitrión `D:\cdmp-grabaciones\cdmp_packets_20261003-*.bin`.

No se ejecutó el juego. No se propone enganchar `.xtls`: las direcciones `.xtls` se leyeron como bytes.

Etiquetas: **[E]** código, **[V]** captura o log, **[S]** inferencia.

## Resumen

1. **[E] Todo `ActorLoadDataAck` 2005 sale por una sola función `.rsrc`: `NotifyAppear` `0x2B000C0`.**
   - Recibe `(u8 sin uso, vec<NwSession*>* destinatarios, vec<u32>* claves)`.
   - Solo la llaman los dos sitios de `OnActorSectorChanged` `0x2AF8AF0`.
   - Es el único escritor del opcode 0x7D5 en el ejecutable.
2. **[E] No hay decisión nativa por pareja (actor, observador)**, ni refresco periódico del área de
   interés.
   - El único filtro es por actor y afecta a todos los observadores: una bandera del usuario dueño,
     `[[user+0x68]+0x130]+0x18`.
   - «Aparecer» solo ocurre cuando cambia el conjunto de observadores: alta en sector, cambio de
     sector o `MoveFieldComplete`.
   - Un actor retenido **no se vuelve a anunciar solo**.
3. **[E] El servidor lee el byte de restauración en `[[[[actor+0x68]+0x40]+0x40]+0x30]`.**
4. **[S] Retención propuesta: envolver la entrada de `0x2B000C0`.**
   - Si la lista de claves contiene un hijo del usuario de opción D con byte 0, se llama al original
     dos veces:
     - con todos los destinatarios y las demás claves;
     - solo con la sesión dueña y las claves retenidas.
   - Se anota `(hijo, observador)`.
   - Al aceptarse su primera acción (byte 1), se llama una vez a `0x2B000C0([sesión del observador],
     [hijo])`. Es la misma llamada que hace el motor.
   - Los 2005 mixtos no son problema: se filtra **antes** de serializar.
5. **[V] Primera Req2223 del hijo tipo 6 (perro) en 112627:** `#3449`, 0,372 s después de `#1517`.
   El payload completo está en §4.

## 1. El camino «aparecer» en el servidor

### 1.1 Cadena [E]

```
alta de un actor en el campo (hijo, NPC o acompañante)
  0x2AFDE70 (lote de apariciones)  /  0x2BC1AB0 (personaje + acompañantes de FM+0x88)
    → 0x2AF7DC0  jmp → .xtls 0x106482B0  «colocar en sector»            (solo leído)
        0x1064833C call 0x2AF9A50             sector de la posición
        0x10648385 call [rax+0x178]           ¿lo controla un usuario? → bpl
        0x106483C1 call 0x20D21A0             el actor entra en la lista del sector
        0x10648425 mov [TS+0x540], r15        sector actual
        0x1064853A call 0x2AF8AF0 (campo, flag, actor, usuario|0, 0, bpl, viejo=0, nuevo)
movimiento:  0x2BCD8A0 → 0x2AF8090 (solo si cambia de sector) → 0x2AF8AF0
MoveFieldComplete: 0x28F93C7 → 0x2AF86E0 → 0x2AF8AF0
```

### 1.2 `OnActorSectorChanged` `0x2AF8AF0` (decompilado)

1. Calcula los observadores del sector viejo y del nuevo con `0x2AF2E60`:
   - `sector+0x68` = `{ServerUserActor*, refcount}`;
   - el número está en `+0x70`.
2. Saca los que entran y los que salen.
3. **Primera llamada, `0x2AF90EB`:**
   - destinatarios = la sesión de cada observador nuevo (`user->vf0xE8`);
   - claves = `[actor+0x60]`.
4. **Segunda llamada, `0x2AF92D1`:** solo si el actor que se mueve es de un usuario.
   - destinatario = su propia sesión;
   - claves = todos los actores de los sectores en que entra (`sector+0x18`, número en `+0x20`).
5. Las bajas van por `0x2B00380`, un `jmp` a `.xtls 0x10650890`. **No se toca.**

ABI en la llamada (`0x2AF90D5–0x2AF90EB`):

```
lea r8,[rsp+0x50]           ; {u32* claves, u32 n}
lea rdx,[rsp+0x20]          ; {NwSession** ses, u32 n}
movzx ecx, r12b ; call 0x2B000C0
```

El `ecx` no se guarda en el prólogo: no se usa.

### 1.3 `NotifyAppear` `0x2B000C0`

**[E]** Es una función real de `.rsrc`: `.pdata` `0x2B000C0–0x2B00372`, entrada
`48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 54 …`. Hace esto:

1. Copia las sesiones no nulas.
2. `0x2B060E0`: por cada clave, busca el actor en el SAM.
   - Lo **descarta** si su usuario dueño (`vf160`) tiene `[[u+0x68]+0x130]+0x18 ≠ 0`.
   - El mismo filtro se repite en `.xtls 0x106570BC`.
3. `0x2B06B60` → `.xtls 0x10657040`: serializa cada actor con `actor->vf148`.
   - El byte de restauración se lee **en ese momento**.
4. `0x2B06330` → `.xtls 0x10651260`:
   - empaqueta el 2005 (`mov word [rcx],0x7D5` en `0x106512EF` y `0x106513AA`);
   - antepone a cada actor de usuario el registro de su usuario, de tipo 0 y 9 bytes;
   - lo manda a todas las sesiones con `ServerNet::SendPacket` `0x2773870`, los mismos bytes para
     cada una.

**[E] Comprobaciones:**
- `opscan.py` (inmediatos 0x7D5 en todas las secciones ejecutables): solo `0x106512EF` y
  `0x106513AA`.
- `0x2B060E0`, `0x2B06B60` y `0x2B06330` tienen un único llamador, `0x2B000C0`.
- `0x2B000C0` solo tiene esos dos llamadores (barrido de `call`/`jmp` en todas las secciones).

### 1.4 ¿Hay un «todavía no» nativo?

**[E] No hay ninguno por observador.** El único filtro es la bandera del usuario dueño
`[[u+0x68]+0x130]+0x18` (`0x2B06185 call [rax+0x160]` ; `0x2B06194 mov rcx,[rax+0x130]` ; `0x2B0619B cmp byte [rcx+0x18],0`; y `0x106570BC`).
- Oculta **todos** los actores de ese usuario a **todos**, incluido el dueño (y también corta las
  bajas, en `0x2AF8AF0`).
- **[S]** Es inservible para D-53: dejaría al usuario 2 sin su hijo y sin su propio Kliff.
- No se encontró quién la escribe (`w130.py`: ningún `mov byte [x+0x18]` tras `[...+0x130]` en
  `.rsrc`). Su significado es **[S]**.

**[E] No hay reevaluación.**
- No existe un temporizador de interés: `0x2B000C0` solo se llama dentro de `0x2AF8AF0`.
- Las 2723/2991 de relevo solo actualizan actores que ya se conocen. No hay «puesta al día»
  (`opcion-d-accion-usuario2.md` §1.4).
- Un hijo retenido para el anfitrión solo reaparecería si el anfitrión entrase en nuevos sectores
  que lo contengan, y entonces llegaría **con 0**. El reanuncio tiene que ser explícito.

### 1.5 Evidencia en C1g [V]

Lectura propia de CDPK (`req2223.py pkt`):

| Paquete | Registros |
|---|---|
| `#944` (2,115061 s, a la sesión 2) | tipo 7 `B010006C`, usuario `90100000` (tipo 0), anfitrión `A0100001`, usuario `90100000`, hijo `B0100002` (restauración 1) |
| `#7431` (2,131281 s) | usuario `9010052B` (tipo 0) + hijo `B010052D` (tipo 6, ranura 1, restauración 0) |

- `#944` es la segunda llamada: la sesión del usuario 2 entra en los sectores del anfitrión.
- `#7431` es la primera llamada al colocar al hijo: `S` = observadores de su sector, `K = [hijo]`.
- **[S fuerte]** En ese mismo `S` estaba la sesión 1, que creó el hijo con 0 (log de C1g,
  companeros.md §1). La grabación de la sesión 2 no contiene la copia de la sesión 1.

## 2. El byte de restauración en el servidor

**[E] Cómo se llega al byte:**
- `0x296CED7` `mov rcx,[rax+0x68]` … `0x296CEFE` `mov rcx,[rcx+0x40]` ; `call 0x2A78CA0`: el CC del
  servidor es `[[actor+0x68]+0x40]` (`r14`, con `[CC+8]` = actor).
- Se lee en `0x2A78DAF` `mov rax,[r14+0x40]` ; `cmp byte [rax+0x30],r11b(0)` → bandera 0x40,
  «de aparición».
- Se escribe en `0x2328890` `mov rax,[r14+0x40]` ; `0x2328894` `mov byte [rax+0x30],1`.
  - En el hilo del servidor, solo si `TLS+0x1EC ≠ 0` y la acción no tiene el bit 0 de `+0x50` ni el
    de `+0xC8`.
  - Es la única escritura de ese byte (D-48).
- El empaquetador lo copia tal cual al registro (`0x231FC90`).

**Predicado legible:** `b = [[[[actor+0x68]+0x40]+0x40]+0x30]`, con cada puntero comprobado.

**[E] Otros campos que usa la retención:**

| Campo | Qué es | Fuente |
|---|---|---|
| `[actor+0x60]` | clave | `0x2AF9062` |
| `[actor+0xA0]` | usuario dueño | `vf160` = `0x50E6B0`; `AddActor` |
| `[[actor+0x88]+1]` | tipo de actor (= tipo del registro: 6 = mascota) | — |
| `[[actor+0x68]+0x1A0]+0x540` | sector actual | `0x10648425` |
| `user->vf0xE8` | su sesión | — |

## 3. Retención mínima propuesta (D-53)

**[S] Un único wrapper en la entrada `.rsrc` `0x2B000C0`.**
- Comprobar antes los bytes de la entrada.
- Activo solo con `share_field`, en el hilo del servidor (el mismo de `on_server_recv`).

**Paso 1, por cada clave `k` de la lista:**
- Resolver el actor: SAM `0x2A827B0`, contexto `[cdmp_g_ServerFieldContext+0x48]`, y soltar el
  attacher con `0x1434880`, como `0x2BC1AB0`.
- La clave queda **retenida** si se cumplen las cuatro condiciones:
  - `[actor+0xA0]` es el `ServerUserActor` del usuario de opción D;
  - el tipo `[[actor+0x88]+1]` ∈ {4, 5, 6};
  - la cadena del CC es válida;
  - `b == 0`.

**Paso 2, si no queda ninguna clave retenida:** se llama al original sin cambios. Es el caso de
todos los actores del anfitrión y de cualquier 2005 que no lleve hijos del usuario 2.

**Paso 3, si hay alguna:** se construyen en la pila vectores propios `{ptr, n}`. El original solo
los lee y no los libera.
1. `orig(S, K\H)`, si `K\H` no está vacío. Así un 2005 mixto como `#944` conserva al anfitrión, a
   sus hijos y el orden padre→hijo.
2. `orig(S∩{sesión del usuario 2}, H)`. El dueño recibe su hijo **ya**, en un 2005 aparte.
3. Por cada `s` de `S` distinta de la sesión del dueño, se anota `(clave, usuario observador, época,
   t0)`.

Esto cubre también la segunda llamada: el anfitrión que entra en sectores donde está el hijo.

**Liberación**, en dos puntos del hilo del servidor:
- tras volver `StartActionReq::vf2` `0x296CAA0` (función `.rsrc`, `.pdata` `0x296CAA0–0x296D128`);
- como respaldo, en cada `on_server_recv`.

Por cada entrada:
1. Volver a resolver el actor. Si ya no existe, o cambió la época o el campo, se descarta.
2. Si `b == 1`, comprobar que el observador sigue observando el sector del hijo: `0x2AF2E60` sobre
   `[[[hijo+0x68]+0x1A0]+0x540]`.
3. En ese caso, `orig([user->vf0xE8()], [clave])` una vez, y borrar la entrada.

**[E/S] Por qué es coherente con el motor:**
- `0x2B000C0` se llama siempre sin locks del llamador. Tanto `0x2AF8090` como `0x106482B0` sueltan
  el lock del actor antes.
- El 2005 se serializa en la liberación y saldrá con 1 y la acción aplicada, como Kliff en D-48.

**Mensajes mientras dura la retención:**
- `ToNear` sigue entregando al anfitrión mensajes del hijo, porque el anfitrión es observador del
  sector.
- **[E]** Los clientes de 3095 (`0xB74AB0`) y 2723 (`0xB57BC0`) hacen `FindByKey` y no hacen nada si
  no encuentran el actor.
- Los demás manejadores son **[S]**: contarlos en la prueba.
- La 2723 de P5 llega antes que el 2005 liberado y se ignora. Después, el 2005 ya lleva la acción.

**Plazo y salvaguardas [S]:**
- A los 3 s sin primera acción: registrar `CompHold timeout` y **seguir reteniendo**. Equivale al
  hijo «rechazado» de antes, que era inocuo. Nunca se libera con 0.
- Tope de 64 entradas. Si se desborda, se retiene sin anotar (no reaparece) y se registra.
- La salida o época nueva del usuario 2 vacía la tabla.
- Si no se puede evaluar el predicado (punteros nulos, ABI inesperada), se llama al original. Ese
  es el caso de hoy, y D-52 sigue como red.
- Riesgo **[S]**: que el servidor rechace la acción del hijo en el campo compartido (#9 de
  `opcion-d-accion-usuario2.md` §2.4). Entonces el hijo no se verá nunca en el anfitrión, pero sin
  cierre.

Líneas de log sugeridas:
- `CompHold key=… obs=… reason=restore0`
- `CompRelease key=… obs=… ageMs=… restore=1`
- `CompHold timeout/drop`

**Alternativas, peores:**
- La barrera de cliente en `0x35D2A0` (companeros.md §A): tiene que partir 2005 ya serializados
  (`#944`) y no cubre 2042.
- La bandera de usuario de §1.4: oculta también al dueño.

## 4. Primera Req2223 del hijo tipo 6 y cómo la reproduce el peer

### 4.1 La captura 112627 [V]

| Paquete | Qué es |
|---|---|
| `#1517` | 42,911983 s, tipo 6, `B0100004`, ranura 3, aspecto `cd_m0002_00_dog` |
| `#3449` | 43,284124 s, primera 2223, Δ 0,372141 s, 730 B; cabecera `af 08 00 d5 02` (op 2223, u16 longitud 725) |

En 112627, `B0100002` es Damian (tipo 4) y `B0100003` el caballo (tipo 5).

Payload completo (+0x000…+0x2D4):

```
+000 040010b00000000050f1e7230000ffffffffffff0001000000000000ffffffff
+020 ffff0000898808bd00000000000000000000803f575700000000030001010000
+040 00000000000000000000000000000000000000ffffffffffff00ffffffffffff
+060 00ffffffffffff0002000080bf00000000000000000000803f57570000000003
+080 0000000000000000000000000000fe9b22c65da112449c835ac5000000006156
+0A0 9e3d00000000d73b7f3f00000000000000000000000000000000000000000000
+0C0 0000000000000000803f0000803f0000803f0000803f00000000000000000000
+0E0 00000000000000000000000000000000803f0000803f0000803f0000803f0000
+100 00000000000000000000000000000000000000000000ffffffffffffffff0000
+120 000000000000000000000000000000000000000000000000803f0000803f0000
+140 803f0000803f00000000000000000000000000000000000000000000000000f4
+160 0101000000000000548cb06b010010a00000000000000000000000008b5b94bd
+180 00000000c0a5dd3d000000000000000000000000000000000000000000000000
+1A0..+2BF  ceros
+2C0 00000000000000000000f401010000000000000000
```

### 4.2 Campos

Fuentes: deserializadores `0x2442610` (A, 69 B) y `0x2442BF0` (B, 582 B), MoveData de
`mirror-ack-formats.md` y `diffdog.py` sobre las 32 capturas del anfitrión.

| Offset | Campo | En las 32 muestras |
|---|---|---|
| `+0x000` | **clave del actor** (la única) | — |
| `+0x004` | A[0]: u32 0, **hash `0x23E7F150`** en `+0x008`, …, `+0x22`=0 | constante (31 de 32) |
| `+0x049` | A[1]: hash 0, `+0x22` = 2 (capa vacía, cumple la #3) | constante |
| `+0x08E` | B. Posición vec3 (−10407, 586,5, −3496,2) | **varía** |
| `+0x09A` | quat (0, 0,077, 0, 0,997) | **varía** |
| `+0x0AA` | u32 de MoveData | 0 |
| `+0x0D6` | Path: Transform local | una variante |
| `+0x116` | padre | `FF…FF` (a pie) |
| `+0x15F` | **u64 reloj del cliente** (ms) | **varía** |
| `+0x168` | u32 opaco | **32 distintos** |
| `+0x16C` | **u32 clave del personaje del dueño** (`A0100001`) | — |
| `+0x17C`, `+0x184` | f32 de dirección | **varían** |
| `+0x2CA` | u64 reloj (= `+0x15F`) | **varía** |
| `+0x2D2`, `+0x2D3` | u8 | 0 |
| `+0x2D4` | u8 final | **0**: el servidor no aplica el movimiento (D-48 §2.2) |

- En 32 de 32 capturas el hash es el mismo y Δ = 0,29–0,55 s.
- `+0x16C` vale lo mismo en la 2991 de la mascota del anfitrión en C1g (`#8165`) [V]. **[S]** Es a
  quién sigue la mascota.

### 4.3 Qué sustituir para el hijo del usuario 2

| Campo | Valor |
|---|---|
| `+0x000` | clave del hijo |
| `+0x16C` | clave del personaje del usuario 2 |
| `+0x08E…+0x0A9` | posición y quat del usuario 2 (o del hijo). Distinta de (0,0,0), por la #10 |
| `+0x15F` y `+0x2CA` | reloj del peer |
| `+0x168`, `+0x17C…+0x187` | los capturados, opacos [S] |

**Atención [V]:** la mascota de la partida del 10-09 **no es el perro**. Es `cd_m0002_00_hatch`
(`#7431`, y el hijo del anfitrión en `#944`).
- No hay ninguna Req2223 inicial del hatch en las capturas.
- El hash del perro `0x23E7F150` puede no existir en su gráfico. Fallaría en silencio, con
  severidad 2.

Candidatos para el hash, por orden:
1. **[S]** El hash que el propio servidor puso en la capa 0 del registro que recibe el peer: hoy
   `0x866C7489`, igual para el perro y el hatch. Es el patrón de Kliff, cuya primera Req usó la
   espera por defecto.
2. **[V]** `0x94D333D6`, acción aceptada del hatch del anfitrión (`#944` y la 2991 `#8165`).
3. `0x23E7F150`, solo para el perro.

La prueba debe registrar `0x2328894` y el byte del siguiente registro.

### 4.4 Cómo aprende el peer la clave del hijo [V]

En cada 2005 que recibe:
1. Recorrer `u16 longitud + registro` desde `+5`.
2. Los registros de 9 B son de tipo 0 (usuario). Por ejemplo, `00 2b051090 00000000` en `#7431`.
3. Los demás: `u8 tipo @0`, `u32 clave @1`, `u32 dueño @5`, `u8 ranura @9`, `u64 @0xA`.
4. **Hijo** = dueño == la clave de usuario propia (de 2646, o del dueño de su propio registro de
   tipo 1), tipo ∈ {4, 5, 6}, `clave>>28 == 0xB`, y primera vez que se ve esa clave.
5. El tipo y la ranura eligen la plantilla. El byte de restauración (gramática del CC, como en
   `companeros_data.py`) confirma que vale 0.

Se envía con un retraso de unos 0,35 s, o en cuanto llegue el registro. Siempre **después** de él:
la #5 exige que el actor exista.
