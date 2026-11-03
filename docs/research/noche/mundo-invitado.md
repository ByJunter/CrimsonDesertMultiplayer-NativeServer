# Mundo alrededor del invitado (pendiente 4 de la opción D): por qué desaparecen los NPC a unos 250 m del anfitrión

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Investigación de solo lectura (2026-10-11, noche): no se abrió el juego, no se tocó el código del repositorio y no se
paró el servidor de Ghidra. Análisis estático con el servidor de consultas de Ghidra y capstone sobre el exe del disco.
Análisis de capturas con scripts propios en `%TEMP%\noche-mundo\` (lista en §9).

Marcas: **[V]** verificado (captura, log, bytes o desensamblado comprobado); **[E]** leído en el código (decompilado o
desensamblado) sin prueba en vivo; **[H]** hipótesis.

Datos usados: `C:\Emulador CD\backups\p8-sesion15-{host,shadow}`, `p8-sesion16-{host,shadow}` (las dos únicas
sesiones en las que el invitado y el anfitrión estuvieron a más de 130 m) y los logs de las sesiones 10–23.
Relojes: t del anfitrión = t del invitado + **129,14 s** en la S16 y + **382,6 s** en la S15 (medido con 6154 y 4217
`RemoveActorAck` idénticos en los dos lados) [V].

---

## 0. Resumen

**Causa raíz [E + V]: la vida de los actores del campo la decide un recolector del servidor que solo mira al
anfitrión.**

- El temporizador nativo `ActorGarbageCollectionRepeatTimer` pasa cada 5 s por todos los actores del servidor. Retira
  con `RemoveActorAck` motivo **25** (0x19) a todo NPC que esté más lejos que su radio de despawn de **dos anclas**:
  1. el personaje actual del **dueño del campo** (`[campo+0x180]` → usuario → `[usuario+0xD8]`), es decir, el anfitrión;
  2. el actor `[campo+0x2E8]`, pero solo si `[campo+0x2E4] ≠ 0`. Es el "observador de mundo" nativo, que fija
     `UpdateWorldObserverStateReq` 2245 y que **nunca se usa**: hay 0 en todas las capturas.
- El radio es un dato de cada spawn (`[[actor+0x68]+0x20]+0x468`). Medido: la mediana de distancia al anfitrión de
  las bajas por motivo 25 es **~200–230 m** en cada pasada.
- El invitado **no es ancla**. Cuando se aleja unos 200–250 m del anfitrión, los NPC que lo rodean superan su radio
  medido desde el anfitrión. En la siguiente pasada (≤ 5 s) el servidor los destruye y manda el 2516 a los dos
  usuarios.
- En la S16, con los dos a 180–199 m, las pasadas de motivo 25 retiraron animales a **92–130 m del invitado** que
  estaban a 200–240 m del anfitrión. La mediana fue ~200–215 m respecto del anfitrión y 318–412 m respecto del
  invitado [V].

**Lo que no es la causa [V]:**
- **No es un recorte por cliente.** El 100 % de las bajas de NPC del invitado son 2516 del servidor, idénticos y
  simultáneos a los del anfitrión: 6293/6293 en la S16 y 4262/4262 en la S15.
- **No es un área de interés por usuario.** En esta build el campo tiene **un solo sector de interés**: `0x2AF9A50`
  devuelve siempre el sector base. Por eso cada usuario recibe todos los actores del campo, estén donde estén [E +
  V: el 92 % de las altas llega a los dos con menos de 1 s de diferencia; el resto son la instantánea de entrada y las
  retenciones del mod].
- **No se paran las actualizaciones antes de la baja.** Los NPC que nacen junto al invitado los pide su propio
  cliente (2550 `AutoSpawnRoadReq`). El servidor los crea con el anfitrión como controlador (`0x90100000`), y el
  cliente del anfitrión les da IA (2223/2556/2734) **hasta al menos 215 m** de distancia [V].

**Arreglo raíz propuesto: «ancla de mundo del usuario 2» (`[session2] user2_world_anchor`).**
- El mod del anfitrión inyecta en el servidor, como sesión 2, la Req nativa `UpdateWorldObserverStateReq` 2245
  `{u8 1, u32 clave del personaje del usuario 2}` cada vez que el usuario 2 termina de cargar en el campo compartido.
- La quita (`{0, 0}`) justo antes de su 2437 (viaje o recarga) y de su 2001 (salida).
- Un vigilante de solo lectura comprueba `[campo+0x2E4/+0x2E8]` cada 5 s y la vuelve a poner si se ha perdido.
- Sin ganchos nuevos, sin escribir en la memoria del juego (salvo un borrado de respaldo de 1 byte), sin `.xtls`. Usa
  el manejador nativo, que toma el cerrojo del campo.
- El recolector conservará entonces la **unión** de los dos discos: los NPC alrededor del anfitrión y alrededor del
  invitado.

**Incógnitas principales:**
1. No hay ninguna captura de una separación de 250 m o más; el máximo guardado es 199 m (§3.4). El evento de 250 m es
   una extrapolación [E].
2. **IA y física de los NPC a más de ~250 m del cliente del anfitrión**: su controlador sigue siendo el anfitrión. Con
   el arreglo dejan de desaparecer, pero queda por medir si el cliente del anfitrión sigue moviéndolos o si se quedan
   quietos o caen. Se puede medir en un PC (§6).
3. El efecto colateral de `0x2836A20([campo+0x80])`, que el manejador llama al activar el observador.

---

## 1. Qué decide el servidor: población, interés y vida de los actores

### 1.1 Interés: un solo sector por campo [E + V]

- `ServerField::OnSetTransform` (`0x2AF8090`) calcula la celda de 64 m (`pos * 0.015625`) y la pasa a `0x2AF9A50`.
- **`0x2AF9A50`** solo comprueba que la celda esté dentro de los límites del nivel (`+0x30/+0x38` de la información del
  nivel). Después devuelve **siempre** `[campo+0x250] + [campo+0x268]*0x78`, que es el sector base. No usa la celda
  para elegir el sector [E, desensamblado `0x2AF9A50..0x2AF9ADE`].
- `GetNeighborSectors` (`0x2AFEDD0`) tiene una vecindad de 3×3, que queda vacía porque no se coloca ningún actor en
  otro sector. `ToNear` (`0x2BC4F70`) toma los observadores del sector del actor (`sector+0x68`).
  - Resultado: **todo usuario del campo es observador de todo actor del campo.**
- Confirmación en las capturas [V]:

  | | S16 | S15 |
  |---|---|---|
  | Bajas de NPC (`2516`, clave `0xB…`) del invitado que el anfitrión recibe a la vez (±1 s) | **6293 / 6293** | **4262 / 4262** |
  | Altas (`2005`) del invitado que el anfitrión recibe a la vez | 6400 (+548 «antes», +7 nunca) | 4320 (+524 «antes», +7 nunca) |

  - Las altas «antes» son la instantánea de entrada del invitado y las retenciones del mod (`observer_hold`,
    `observer_load_defer`).
  - Durante los 20 s con los dos a 199 m (S16), el anfitrión recibió los NPC nacidos junto al invitado y el invitado
    los nacidos junto al anfitrión (a 239 m de él) [V].
- **No hay ninguna baja «por salir de la vista»:** como solo hay un sector, ningún usuario sale nunca del interés de
  un actor.

### 1.2 Población: los spawns los pide cualquier cliente y los controla el dueño [E + V]

- `ActivateSpawningPoolReq` 2424 → `0x2C41AF0` → `SpawnManager_ActivateSpawningPool` `0x27EE3E0`. Recibe el campo
  **del remitente** y su personaje como solicitante.
  - Con `0x2AFFF50` y `SetFieldTime_PreCheck` (`0x206AE30`) obtiene el personaje del **dueño** del campo y se lo pasa a
    la creación (`0x27EA160`).
  - No encontré ninguna comprobación de distancia al dueño al crear [E]. `AutoSpawnRoadReq` 2550 va por
    `0x2C41E50`.
- Prueba de la S16 [V]:
  - En t = 186,24 el invitado (en (−10628, −3826), a 183 m del anfitrión) manda 14 `AutoSpawnRoadReq` 2550 con
    puntos en (−10699, −3792). El cliente del anfitrión no manda ninguna Req de spawn entre 184 y 187.
  - En 186,30 nacen `0xB0100704…0xB010070A`: el 2005 llega antes al anfitrión y a los 186,51 al invitado.
  - **Controlador `0x90100000`** (el usuario anfitrión) y dueño 0.
- **La IA la lleva el cliente del anfitrión** [V]. Su captura tiene C>S 2223/2556/2734 de `0xB0100704`, `…0707`,
  `…071A` y `…0723` desde su nacimiento. `0xB0100723` nació a 95 m del invitado y **215 m** del anfitrión.

### 1.3 Vida: `ActorGarbageCollectionRepeatTimer` [E + V]

Cadena [V bytes]:
- vtable `pa::TrocTrActorGarbageCollectionRepeatTimer` `0x5B35110`, vf2 en `0x2C372E0` (`E9 AB 11 C2 0D`, un `jmp`
  a `.xtls 0x10858490`);
- un contador global que solo deja pasar 1 de cada 10 llamadas;
- **`0x2C37340`** (en Ghidra, `cdmp_ServerDeadActorHide_caller`).

En las capturas las pasadas caen cada **5,0 s** exactos (S16: 100,1 / 105,1 / 110,1 …; S15: 154,5 / 159,5 …) [V].

Lógica de `0x2C37340` [E, decompilado y desensamblado]:

1. Recorre **todos** los actores del SAM (`SAM::GetActorList` `0x2A83CB0`). Se salta los de jugador (`vf(0x178)`): el
   personaje del usuario 2 nunca se recoge.
2. Para cada actor toma su campo (`0x28FB8B0` → `FieldMover` de `TS+0x4F0`) y su tipo de spawn
   (`[[[actor+0x68]+0x20]+0x2D0]`).
   - En casi todos los tipos, la **posición de referencia** es la del personaje del **dueño del campo**:
     `0x20DEE60(campo)` busca el usuario `[campo+0x180]` en el gestor de usuarios; `0x206AE30(usuario)` devuelve
     `[usuario+0xD8]`, el personaje actual (con la comprobación `vf(0xC0)(4,0x10)`); `0x17EB2C0` da la posición
     mundial (`sector*1000 + local`).
   - Algunos tipos usan otra ancla: el «dueño de spawn» `[[actor+0x68]+0x20]+0x278`, la clave de un componente
     (tipo 0x0E), etc.
3. **Ancla 1**, en `0x2C37D47`:
   - `0x28F9A20(actor, &ref, caja)` devuelve «fuera» cuando `d² ≥ r²`, con `r = [[actor+0x68]+0x20]+0x468`, o con una
     caja orientada si el actor la tiene.
   - Si está **dentro**, se conserva.
4. **Ancla 2**, en `0x2C38E0F…0x2C38EBC` [V bytes]:

   ```
   0x2C38E28  41 0F B6 9F E4 02 00 00   movzx ebx, byte [r15+0x2E4]     ; r15 = campo (con cerrojo compartido [campo+8])
   0x2C38E49  84 DB / 0F 84 77 00 00 00 ; si 0 -> 0x2C38EC8 (despawn)
   0x2C38E58  E8 33 D2 EC FF            call 0x2B06090                   ; clave = [campo+0x2E8]
   0x2C38E72  E8 39 99 E4 FF            call 0x2A827B0  (SAM::FindActor)
   0x2C38E90  E8 2B 24 BB FE            call 0x17EB2C0  (su posición)
   0x2C38EA2  E8 79 0B CC FF            call 0x28F9A20  (mismo radio del actor)
   0x2C38EA7  84 C0 / 75 11             ; dentro -> se conserva (0x2C38164)
   ```
5. Si no lo salva ninguna ancla:
   `0x2C38EE5 call 0x28F30C0` (¿se puede retirar?) → `ServerActor::Logout(actor, …, 0x19, …)` → `RemoveActorAck` con
   **motivo 25** por `ToNear`, es decir, a todos los usuarios del campo.
6. Otras ramas del mismo recolector también retiran con motivo 25:
   - caducidad por tiempo (`+0x428` frente a `_time64`);
   - dueño o usuario desaparecido;
   - una categoría aparte, los actores con banderas `+0x270/+0x273` del componente de spawn (`0x2945EE0`), que se
     recogen a más de **50 m** del personaje del dueño. Ese umbral es `GlobalVariableFloat` `0x6D09488`, iniciado a
     `0x42480000` = 50,0 en `0x232140`. **Esta rama no consulta el ancla 2.**

Valores medidos de las pasadas de motivo 25, solo NPC y animales (tipos 3 y 6) [V]:

| Tramo | Separación | Mediana d(NPC, anfitrión) | Mediana d(NPC, invitado) |
|---|---|---|---|
| S16 100–175 s, juntos | 1–2 m | 198–221 m | 198–222 m |
| **S16 185–205 s, separados** | **178–199 m** | **197–215 m** | **318–412 m** (mínimo 92–125 m) |
| S16 220–480 s, juntos | 1–35 m | 200–232 m | 196–229 m |
| S15 344–364 s (posición del invitado fiable) | ~132 m | 206–224 m | 130–257 m |

- Las bajas sueltas a menos de 100 m de los dos son las otras ramas del recolector (caducidad, motivo 25 sin
  distancia) o posiciones viejas.
- Por tipo de actor (S16) [V]: los NPC (3) y los animales (6) mueren casi siempre por motivo 25 (227 y 921) o 19
  (pantalla de carga). Los objetos (7) mueren por los motivos 21/66/57/20, que provocan las Req de escena y de pools
  de los clientes.

### 1.4 El observador de mundo nativo (`UpdateWorldObserverStateReq` 2245) [E + V]

- **Servidor**: manejador `0x29C2780` (vtable `0x5B245F8`).
  - Lee **`u8 activar`** y luego **`u32 clave`**. Exige una carga útil de exactamente 5 B (`min_size 5`; comprueba lo
    consumido frente a la longitud en `0x29C288D`).
  - Toma el campo **del actor remitente**: `[ctx]` con `0x28FB8B0`. **No comprueba que sea nulo**, así que hay que
    inyectar solo con el usuario 2 en el mundo.
  - `Actor_Lock(&l, [campo+8], 1)` en exclusiva y [V bytes]:
    ```
    0x29C2915  44 88 B6 E4 02 00 00   mov byte [rsi+0x2E4], r14b   ; activar
    0x29C291C  89 9E E8 02 00 00      mov dword [rsi+0x2E8], ebx   ; clave
    ```
    AOB `44 88 B6 E4 02 00 00 89 9E E8 02 00 00`, única.
  - Suelta el cerrojo. Si `activar ≠ 0`, llama a `0x2836A20([campo+0x80])`: vacía un conjunto `+0x278` de ese gestor y
    vuelve a meter en él las entradas de su tabla `+0x60` con `+0xB0 == −1` y (`+0x183` o `+0x187`). La semántica es
    desconocida [H]; solo la llama este manejador.
- **Ciclo de vida de la ranura**: el constructor `0x2AF6670` pone `+0x2E4 = 0` (`0x2AF69FD`) y `+0x2E8 = 0`
  (`0x2AF6A04`), y el reinicio del pool `0x2AF7360` pone `+0x2E4 = 0` (`0x2AF74B5`) [V bytes]. Solo hay **una**
  ranura por campo.
- **Cliente**: el emisor es `0xB7C6F0`, con la misma carga `u8, u32` y opcode `0x8C5`.
  - Lo llaman `0x969F00` y `0x96A490`, que cambian el personaje controlado al «WorldObserver» (índice estático
    `+0x10A`) y lo devuelven.
  - Clave enviada: `[usuario local+0x60]`.
  - Las dos están protegidas por el byte global `[0x6D20408]` [E]. **0 apariciones de 2245 o 3060** en las 49
    capturas revisadas (sesiones 3–22 de P8 y `captures/`) [V]: en la partida normal la ranura está libre.
- `SAM::FindActor` (`0x2A827B0`) busca por `clave & 0xFFFFF` y comprueba solo ese índice, **sin generación**. Una
  clave vieja puede acabar resolviéndose a otro actor que reutilice el índice [E].

---

## 2. Qué decide el cliente

- **Mantiene todo actor hasta que el servidor lo retira.** El manejador del 2516 es `0xB62C70` (`u8 04 | u32 | u8
  motivo | n × u32`). Se salta los actores de usuario y al jugador local salvo con motivo 7 [V, documentos previos].
  - En las capturas, toda baja de NPC del invitado es un 2516 del servidor idéntico al del anfitrión (§1.1) [V].
  - **No hay ningún recorte propio** por distancia al jugador local, al dueño ni a la cámara que explique el fallo.
- Los objetos de escena van aparte: `UnloadSyncSceneObjectAck` 2517 y las Req de nivel y pool de cada cliente
  (2834/2682/2119/2221). No influyen en los NPC.
- Cada cliente pide los spawns de su propio entorno (2424/2550/2928), las regiones (3067) y los candidatos de
  escenario (2100).
  - El invitado ya pide los suyos y el servidor los atiende (§1.2).
  - El **2100** se guarda **por campo** (`0x2A5E9D0` → `[campo]->vf(0x48)` `+0x640/+0x650`) y gana el último que
    escribe. Con `observer_seq_copy_2100=0` (por defecto), los dos clientes se lo pisan [E] (§5.4 R8).

---

## 3. Qué pasa a ~250 m

### 3.1 Lo que hay en los datos [V]

- **No hay ninguna captura ni ningún log con 250 m de separación o más.** Máximos (2D) de los logs guardados:
  - S15: 133 m;
  - S16: 199 m (invitado tras su viaje rápido, 12:45:00 en Shadow);
  - S23: 198 m;
  - el resto, < 100 m.
- En la S15 el invitado viajó a 132 m y se quedó atascado en la carga 0x11 (`p8-s15-viaje-tunel.md`). Lo observado en vivo (al alejarse más de 300 m, a los 250 m desaparecieron los NPC del invitado)
  corresponde a un tramo que no se
  guardó.
- El mejor tramo disponible es **S16 t = 185–208** (hora del invitado):
  - invitado en (−10615, −3808);
  - anfitrión quieto en (−10763, −3941), a **178–199 m**;
  - después viaja el anfitrión (t ≈ 209) y se juntan.

### 3.2 Secuencia observada en ese tramo [V]

| t (invitado) | Hecho |
|---|---|
| 186,24 | El invitado pide 14 `AutoSpawnRoadReq` 2550 a ~80 m de él |
| 186,30 / 186,51 | Nacen `0xB0100704…070A` (2005 al anfitrión y luego al invitado), controlador `0x90100000` |
| 186,5 → | El cliente del anfitrión les da IA: C>S 2223/2556/2734; el invitado los ve moverse (2697) |
| 185,16 | Pasada del recolector: 21 bajas de motivo 25. Ejemplos: `0xB0100627` a 100 m del invitado y **212 m** del anfitrión; `0xB0100624` 108/220; `0xB0100626` 106/216; `0xB0100628` 102/210 |
| 190,14 | 10 bajas; `0xB01002B8/B9` a 114 m del invitado y 155 m del anfitrión |
| 195,16 | 8 bajas; `0xB010063D/3E` a 92 m del invitado y 159–160 m del anfitrión |
| 200,17 | 8 bajas; `0xB0100641/42/43` a 127–130 m del invitado y **224–229 m** del anfitrión |
| 205,15 | 10 bajas; `0xB0100645/46/47` a 125–127 m del invitado y **209–213 m** del anfitrión |
| 208–212 | Viaja el anfitrión (motivos 19/30 al salir) y aparece junto al invitado |
| 215–225 | Mueren `0xB0100707` (a 167–187 m del anfitrión ya movido) y `0xB0100704` (217 m) |

- Las distancias usan la última posición conocida (2697 o 2005 con margen ≤ 3 s).
- Las bajas cercanas a 150–160 m del anfitrión son de spawns con radio menor o de otras ramas del recolector.

### 3.3 Interpretación

- A 180–200 m solo caen los NPC del lado del invitado más alejado del anfitrión.
- A **≈ 250 m** prácticamente todo lo que rodea al invitado (≤ 50–100 m de él) queda fuera del radio medido desde el
  anfitrión (~200–230 m). Se borra en la siguiente pasada (≤ 5 s): un 2516 de motivo 25 por pasada, con los mismos
  bytes en los dos clientes [E, extrapolación].
- Lo que el invitado pide después (2550/2424 cada ~1 s) nace y vuelve a morir en ≤ 5 s, o el pool se queda marcado como
  activo y no reaparece [H]. Por eso el invitado ve su entorno vacío o con apariciones que se van.
- **No es una parada de actualizaciones ni un recorte del cliente.** El juego no se cierra porque solo se retiran NPC:
  el personaje del usuario 2 es un actor de usuario (`vf(0x178)`) y el recolector se lo salta.

---

## 4. Por qué el invitado no tiene «su» mundo

| Pieza | Diseño nativo (un jugador) | Con el usuario 2 en el campo compartido |
|---|---|---|
| Interés (quién recibe) | Un sector por campo: todo a todos | Bien: el invitado recibe todo lo del campo [V] |
| Población (quién pide) | Cada cliente pide su entorno | Bien: el servidor atiende al invitado [V] |
| Control e IA | El dueño del campo (`[campo+0x180]`) | Todo lo controla el anfitrión; su cliente mueve los NPC del invitado al menos hasta 215 m [V]; más lejos, desconocido |
| **Vida (recolector)** | **Ancla = personaje del dueño (+ observador de mundo opcional)** | **El invitado no es ancla: lo suyo muere a ~200–250 m del anfitrión** [V/E] |

El único eslabón que rompe «ver el mundo alrededor del invitado» a esa distancia es la vida. El diseño nativo ya tiene
una segunda ancla por campo, libre en la partida normal.

---

## 5. Arreglo raíz: ancla de mundo del usuario 2 con la Req nativa 2245

### 5.1 Qué se hace

En el anfitrión, módulo nuevo `src/mod/session2/world_anchor.{h,cpp}` con la lógica pura en `world_anchor_logic.h`
para los tests. Interruptor `[session2] user2_world_anchor = 0 | probe | 1`.

- **Paquete** (cabecera Troc de 5 B + carga), inyectado como sesión 2 con `server_inject_ex` (`session2/server.cpp`):
  - poner: `C5 08 | 00 | 05 00 | 01 | k0 k1 k2 k3`;
  - quitar: `C5 08 | 00 | 05 00 | 00 | 00 00 00 00`.
  - **Ranura 0**: la del personaje principal, la misma que pone el cliente nativo con `actor->vf(0x168)` y la que el
    servidor resuelve con `ServerUserActor->vf(0xF8)(slot)`.
  - `k` = clave del personaje principal del usuario 2 (clase `0xA`). La fuente es la que ya guarda el mod (E4:
    `MainPlayerDataStartAck` +0), comprobada con `[[usuario2+0xD0]+0x60]` por lectura protegida.
- **Cuándo** (las mismas colas FIFO, así que el orden queda garantizado):
  1. **Poner**:
     - cuando el servidor manda al usuario 2 su **`LoadingCompleteAck` 2762** y su fase de movimiento termina; ya se
       detecta en el `Send` de la sesión 2 (`server.cpp`, bloque «user 2's move phase»);
     - y su personaje está en el campo del anfitrión (`FM2+0x10 == FM1+0x10`; las mismas lecturas que
       `field_share`/`fast_travel`).
     - Se marca «poner pendiente» y el hilo de red inyecta en su siguiente vuelta.
  2. **Quitar antes** de entregar al servidor el **2437** `GameLoadingStartReq` (viaje, recarga o reaparición), el
     **2283** si se entrega, y el **2001** de salida:
     - en `session2.cpp`, justo antes de `server_inject_ex(p)`, si el estado es «puesto»;
     - en la secuencia de salida del mod (`server.cpp`, `make_logout_session_req`), antes del 2001.
     - Así el manejador encuentra todavía el personaje en el campo compartido. Si no, escribiría en el campo nuevo, o
       `[ctx]` podría ser nulo.
  3. **Vigilante**, cada 5 s en el hilo de red (tick del informe):
     - lectura protegida (VirtualQuery/SEH) de `F = [FM1+0x10]`, `[F+0x4A]`, `[F+0x2C8]`, `[F+0x2E4]` y `[F+0x2E8]`;
     - con el usuario 2 en `F` y la ranura distinta de `{1, k}`: reinyectar (≤ 1 cada 5 s, contador y aviso con el
       valor encontrado: delataría un observador nativo del anfitrión).
  4. **Respaldo para quitar**: si ya no queda sesión 2 (corte sin 2001) y `F` sigue abierto (`+0x4A = 1`, misma
     generación `+0x2C8`) con `[F+0x2E8] == k`, escribir **1 byte** `[F+0x2E4] = 0` en `ServerRunThread`, dentro del
     gancho que ya existe en `OnRecvPacket` (`child_hold_server_tick`), con SEH.
     - Evita que una clave vieja ancle el mundo a otro actor que reutilice su índice del SAM (§1.4).
     - Si no se hace, el reinicio del pool limpia la ranura cuando el campo vuelve al pool.
- **`probe`**: no inyecta. Escribe la ranura, lo que *haría* y las evidencias del recolector:
  - con el grifo del sniffer, que ya ve todos los 2516 del anfitrión (un solo sector), y las posiciones de 2005/2697;
  - cuenta las bajas de motivo 25 de NPC a menos de 150 m del usuario 2 y su distancia al anfitrión.
  - Es la medida del fallo antes de activar nada.
- **Líneas de log**:
  - `WorldAnchor [set] key 0x… field 0x… gen 0x… slot was 00/00000000`;
  - `[verify] slot 01/0x… ok after N ms`;
  - `[clear] before 2437|2283|2001`;
  - `[lost] slot … while user 2 in the field -> reinject #n`;
  - `[fallback clear]`;
  - resumen por minuto con inyecciones, verificaciones, pérdidas y la evidencia del recolector.

### 5.2 Hilos y cerrojos

| Paso | Hilo | Juego |
|---|---|---|
| Decidir «poner» al ver el 2762 | Hilo del servidor que manda (`Send` de la sesión 2) | Nada: solo un atómico |
| Inyectar 2245 | Hilo de red del mod (`server_inject_ex` → Q0) | La cola del servidor, como cualquier Req del invitado |
| Escribir la ranura | **`ServerRunThread`**, en el manejador nativo `0x29C2780` | Cerrojo **exclusivo** `[campo+8]` del propio juego |
| Leer la ranura (recolector) | Hilo del temporizador del servidor | Cerrojo **compartido** `[campo+8]` (`0x393DF0`) |
| Vigilante | Hilo de red, solo lectura protegida | Ninguno (lectura racy, sin efecto) |
| Respaldo para quitar | `ServerRunThread` (gancho existente), 1 byte | Sin cerrojo. Peor caso: una pasada lee 1 con la clave vieja → `FindActor` falla → como hoy |

**Ningún gancho nuevo**: no hay parches en `.rsrc` ni en `.xtls`. Se cumplen las reglas de CLAUDE.md, porque no hay
inline hooks a mitad de función y las lecturas están protegidas.

### 5.3 Por qué esto y no otra cosa

- **Gancho intermedio en el recolector** (`create_mid` en `0x2C37D5B`, `84 C0 0F 85 AC 10 00 00`, AOB única desde
  `0x2C37D47`: `4C 8D 85 40 02 00 00 48 8D 55 88 48 8B 4F 08 E8 C5 1C CC FF 84 C0 0F 85 AC 10 00 00`):
  - pondría AL = 0 si el actor está dentro de su radio respecto de cualquier otro jugador;
  - serviría para 3 o más jugadores, pero mete código del mod en un bucle de ~1000 actores cada 5 s, con cerrojos, y
    lee posiciones desde dentro;
  - **para dos jugadores no hace falta**. Queda como camino si un día hay más de 2 (§7, I7).
- **Cambiar `[campo+0x180]` al usuario 2**: rompe todo lo «solo dueño» del anfitrión (hora, stages, guardado). No.
- **Ignorar en el cliente del invitado los 2516 de motivo 25**: el actor ya no existe en el servidor y quedarían
  fantasmas sin actualizaciones. No.
- **Subir el radio `+0x468`**: global, dispara la población del anfitrión y no sigue al invitado. No.
- **Que el cliente del invitado mande él mismo el 2245**, inyectado en su `ClientNet`: equivale al diseño elegido,
  pero el anfitrión controla mejor el orden respecto de 2437/2001, el respaldo y la verificación. Se descarta como vía
  principal.

### 5.4 Riesgos

| # | Riesgo | Valoración | Mitigación |
|---|---|---|---|
| R1 | Una sola ranura por campo, compartida con el «WorldObserver» nativo: si el anfitrión lo usa, la pisa al activarlo y la vacía al salir | Baja: protegido por `[0x6D20408]`, 0 de 49 capturas [V] | El vigilante reinyecta y avisa con el valor encontrado |
| R2 | Clave vieja tras la salida del usuario 2: `FindActor` sin generación puede resolverla a otro actor con el mismo índice | Media sin respaldo | Quitar antes de 2437/2283/2001 y respaldo de 1 byte (§5.1) |
| R3 | **IA y física de los NPC lejanos.** El controlador sigue siendo el anfitrión: más allá de ~250 m, su cliente quizá no los mueve (LOD de IA) o no tiene terreno ni colisión cargados y caen. En el invitado, los actores remotos toman la Y exacta del `MoveActorAck` (`pendiente-copia-se-hunde.md`) | **Desconocida.** Llega a 215 m [V] | Se mide en un PC (§6.2). Si falla: fase 2, control para el usuario 2 de lo que él pide (`actor+0x90 = clave del usuario 2`; P5 ya enruta por controlador). Riesgo nº 1 de `vision-sintesis.md` |
| R4 | Más población y tráfico: la unión de dos discos. Los dos clientes reciben todo (un sector) y el anfitrión carga con los NPC del invitado (CPU y red) | Media | Medir NPC vivos, `SAM+0x9C`, tráfico del túnel y FPS del anfitrión en la prueba de dos PC. El SAM admite 15 501 claves; hoy hay ~600–800 vivas |
| R5 | El efecto colateral `0x2836A20([campo+0x80])` del manejador nativo | Desconocida [H] | En la prueba de un PC, comparar el tráfico S→C en los 2 s siguientes a la inyección. Alternativa: escritura directa en `ServerRunThread` (8 B bajo el cerrojo del juego), sin efecto colateral |
| R6 | Lo que no protege el ancla 2: la rama de 50 m (`0x6D09488`, banderas `+0x270/+0x273`), las anclas propias de algunos tipos, la caducidad | Baja | Contar en `probe` las bajas de motivo 25 cerca del usuario 2 con el ancla puesta |
| R7 | Objetos (tipo 7) que retira el **otro** cliente con sus Req de pool o nivel (2119/2682/2834/2221): los pools no llevan cuenta por solicitante | Media; es el pendiente 6 «objetos que van y vienen» | Fuera de este arreglo |
| R8 | Candidatos de escenario 2100 por campo: gana el último que escribe | Media en ciudades, para los horarios | Fuera de este arreglo; `observer_seq_copy_2100` ya existe como palanca |
| R9 | Viaje del anfitrión (campo nuevo) con el invitado en el viejo (C1 de `opcion-d-campo-compartido.md`) | Igual que hoy | La ranura es del campo viejo y se queda con el invitado. El campo nuevo nace con la ranura a 0 (`0x2AF74B5`) y se vuelve a poner tras el 2762 del invitado en él |
| R10 | `[ctx]` nulo en el manejador si se inyecta sin personaje en ranura 0 | Bajo con las condiciones de §5.1 | Inyectar solo tras su 2762, con el personaje leído en el campo, y quitar antes de 2437/2001. Nunca durante una fase de carga |

---

## 6. Qué se verifica en un PC y qué necesita dos

### 6.1 Ya hecho, estático y con capturas

- Cadena del recolector, anclas, radio y motivo [E].
- Un sector por campo [E + V].
- Bajas globales e idénticas [V].
- IA del anfitrión sobre los NPC del invitado hasta 215 m [V].
- Formato y efecto del 2245 [E].

### 6.2 Un PC (anfitrión con el juego + `cdmp_peer` de usuario 2)

En el recolector solo cuenta la distancia entre ancla y actor, así que **mover al anfitrión equivale a mover al
invitado**. No hace falta mover al peer.

1. **Regresión de la Req** (`user2_world_anchor=1`, guion E3 con despedida `login_e3lc00bye_*.troc`):
   - `WorldAnchor [set]` tras el 2762 del peer y `[verify] slot 01/<clave del peer>`;
   - ningún 1013 (Nak) por el 2245;
   - `[clear] before 2001` en la despedida y la ranura de nuevo a `00`;
   - el campo sigue abierto (`+0x4A = 1`), sin fallos y sin `FAULTED`.
   - Repetir con el corte a mitad de carga (`…cut8…`) para el respaldo.
2. **Efecto en el recolector**, en dos pasadas, A con `probe` y B con `1`:
   - el peer entra y se queda en el punto de partida;
   - el anfitrión (el usuario, o yo con control del escritorio si se acuerda) se aleja a pie o a caballo **400–600 m**,
     espera 60 s y vuelve.
   - Se mide en la captura del anfitrión (recibe todos los 2516) y en el log del mod:
     - A: bajas de motivo 25 de NPC a menos de 150 m del peer en cuanto el anfitrión pasa de ~200 m. **Reproduce el
       fallo en un PC.**
     - B: ninguna de esas bajas, salvo las otras ramas del recolector. Las bajas de motivo 25 quedan a ~200 m del ancla
       más cercana.
     - **R3**: con el anfitrión a 300–600 m, si su cliente sigue mandando 2556/2223/2734 de los NPC junto al peer, y si
       su Y baja (caen).
   - Script: `%TEMP%\noche-mundo\gcbound.py` adaptado a «distancia al ancla más cercana».
3. **R5**: tráfico S→C en los 2 s siguientes a cada 2245 (`analyze_capture.py phases`).

Limitación del PC único: el peer no pide spawns. Que los NPC **nuevos** alrededor del invitado sobrevivan y se muevan
solo se ve con un cliente real.

### 6.3 Dos PC (Shadow de invitado)

1. Anfitrión quieto. El invitado camina y luego cabalga **300–600 m** hasta una zona que el anfitrión no ha visitado
   (campo abierto y una aldea); se queda 2 min y vuelve.
   - Se espera que los NPC y animales alrededor del invitado aparezcan y no desaparezcan a 250 m.
   - Queda por ver si se mueven (R3).
   - El anfitrión no debe notar nada salvo carga.
2. El invitado hace un viaje rápido a otra zona del mismo nivel y vuelve: `[clear] before 2437` y `[set]` al llegar.
3. El invitado sale al título: `[clear] before 2001`.
4. Datos: logs y capturas de los dos lados, FPS del anfitrión, `SAM+0x9C` (actores) y tráfico del túnel.

---

## 7. Incógnitas

| # | Incógnita | Cómo cerrarla |
|---|---|---|
| I1 | No hay captura de la separación de 250 m; el umbral exacto depende de `+0x468` de cada spawn (medido ~200–230 m) | §6.2 A (anfitrión a 400–600 m) |
| I2 | **IA y física de NPC a más de 250 m del cliente controlador** (el anfitrión) | §6.2 B, punto R3. Si fallan, la fase 2 es el control al usuario 2 |
| I3 | Semántica de `0x2836A20` y del gestor `[campo+0x80]` | §6.2 punto 3; o la alternativa de escritura directa |
| I4 | Si el despachador Troc rechaza el 2245 sin actor en la ranura 0 (el manejador no comprueba `[ctx]`) | No hace falta saberlo con las condiciones de §5.1 |
| I5 | `[usuario+0xD8]` (ancla 1 del recolector) frente a `+0xD0` (personaje principal que usa el mod): en una partida normal deberían ser el mismo Kliff [H] | Leerlos en la regresión de un PC |
| I6 | Si un pool que el anfitrión ya activó y el recolector vació vuelve a nacer cuando lo pide el invitado (idempotencia del gestor de spawn) | Prueba de dos PC |
| I7 | Más de 2 jugadores: una sola ranura | Gancho intermedio del recolector (§5.3) |

---

## 8. Direcciones (build 2976)

| RVA | Qué es | Notas |
|---|---|---|
| `0x5B35110` | vtable `pa::TrocTrActorGarbageCollectionRepeatTimer` | vf2 = `0x2C372E0` |
| `0x2C372E0` | vf2: `E9 AB 11 C2 0D` → `.xtls 0x10858490` (1 de cada 10 → `0x2C37340`) | No tocar (`.xtls`) |
| `0x2C37340` | Recolector de actores (motivo 25) | `.rsrc`, 0x2DE8 B |
| `0x2C37D47..0x2C37D63` | Ancla 1: `call 0x28F9A20`; `test al,al; jne 0x2C38E0F` | AOB única (§5.3) |
| `0x2C38E28` | Ancla 2: `41 0F B6 9F E4 02 00 00` (`[campo+0x2E4]`) | AOB única `41 0F B6 9F E4 02 00 00 66 C7 85 A0 11 01 00 01 00` |
| `0x2C38E58` / `0x2C38EA2` | `0x2B06090` (`[campo+0x2E8]`) / `0x28F9A20` del ancla 2 | |
| `0x2C38EE5` | `call 0x28F30C0` (¿se puede retirar?) antes del `Logout(…,0x19,…)` | AOB única `48 8B 4F 08 E8 D6 A1 CB FF 90 48 8D 4D 70 84 C0 75 0A` |
| `0x28F9A20` | «Fuera del radio»: `r = [[actor+0x68]+0x20]+0x468` (o caja) | |
| `0x20DEE60` / `0x206AE30` | Usuario de `[campo+0x180]` / personaje `[usuario+0xD8]` | |
| `0x17EB2C0` | Posición mundial de un actor (`sector*1000 + local`) | |
| `0x2A827B0` | `SAM::FindActor` (índice `clave & 0xFFFFF`, sin generación) | |
| `0x2945EE0` / `0x6D09488` | Categoría de 50 m / `GlobalVariableFloat` 50,0 (`0x232140`) | Sin ancla 2 |
| `0x29C2780` | Manejador de `UpdateWorldObserverStateReq` 2245 | Su prólogo de 40 B aparece 2 veces: no engancharlo |
| `0x29C2915` | `mov [rsi+0x2E4], r14b ; mov [rsi+0x2E8], ebx` | AOB única |
| `0x2836A20` | Efecto colateral del 2245 activo (`[campo+0x80]`) | Solo lo llama `0x29C2780` |
| `0xB7C6F0` | Emisor cliente del 2245 (`u8, u32`, opcode `0x8C5`) | Lo llaman `0x969F00` / `0x96A490`, protegidos por `[0x6D20408]` |
| `0x2AF6670` (`0x2AF69FD`, `0x2AF6A04`) / `0x2AF7360` (`0x2AF74B5`) | Constructor y reinicio del campo: ranura a 0 | |
| `0x2AF8090` / `0x2AF9A50` | `OnSetTransform` / sector: devuelve siempre el sector base | Un sector por campo |
| `0x2AF8AF0` / `0x2AFEDD0` | Cambio de sector y diferencia de observadores / vecinos 3×3 | |
| `0x2C41AF0` → `0x27EE3E0` | `ActivateSpawningPool` (campo y personaje del remitente, dueño = controlador) | |
| `0x2C41E50` | `AutoSpawnRoad` | |
| `0x2A5E9D0` | `SyncStageCandidatePostionReq` 2100: candidatos por campo | R8 |

---

## 9. Scripts (`%TEMP%\noche-mundo\`, fuera del repositorio)

| Script | Qué hace |
|---|---|
| `cap.py` | Lector de `.bin`, registros del 2005 y posición del 2005 (padre − 160 B, contrastada con el primer 2697 en 1350/1818 casos) |
| `timeline.py` | Línea de tiempo por ventana: posiciones, altas y bajas por motivo, Req de spawn |
| `events.py`, `rmsame.py`, `addsame.py` | Coincidencia de bajas y altas entre las dos capturas |
| `gcbound.py` | Pasadas de motivo 25: distancias al anfitrión y al invitado |
| `newkeys.py`, `track.py`, `ctl.py`, `hostmoves.py`, `reqwin.py` | Claves nuevas, trayectorias, controlador, IA del anfitrión y Req de spawn por ventana |
| `rtyp.py`, `count_ops.py`, `offset.py`, `dist.py` | Motivos por tipo, búsqueda de 2245/3060, desfase de relojes y separación máxima por log |
| `dis.py`, `fdis.py`, `disp.py`, `imm.py`, `ptrscan.py`, `rdf.py` | Capstone: desensamblado, desplazamientos, inmediatos, punteros y globales |
| `d_2c37340.c`, `d_27ee3e0.c`, `d_27ea160.c`, … | Decompilados guardados |
| `sigs.txt`, `sigs2.txt` | Firmas comprobadas con `tools/scripts/aob_scan_file.py` |

---

## Implementado (2026-10-12): `[session2] user2_world_anchor = 0 | probe | 1`

Build **1.0.0.2976**. Solo anfitrión y con `create_session=1`. Por defecto vale 0. Código:
`src/mod/session2/world_anchor.{h,cpp}`; la lógica pura está en `world_anchor_logic.h` y sus tests en
`tests/optd_world_anchor_tests.cpp` (`optd_world_anchor`, 22 casos). No se ha probado en el juego.

### Decisión: escribir la ranura directamente, sin la Req nativa 2245

**La incógnita I3 / R5 queda cerrada: `0x2836A20` tiene un efecto colateral persistente y de uso desconocido.** Por
eso se aplica la alternativa de R5.

Qué hace `0x2836A20` [E, decompilado y desensamblado]:
- Recibe `[campo+0x80]`, que es el **`pa::ServerSyncSceneObjectManager`** del campo (vtable `0x5B16A40`). El
  constructor `0x2AF6670` lo crea y `0x20D3910` lo guarda en `+0x80` (`param_1[0x10]`).
- Toma su cerrojo `[gestor+8]` en exclusiva y vacía el conjunto `gestor+0x278` (`0x40B7B0`).
- Después recorre su tabla `+0x60` y vuelve a meter en el conjunto la clave de 16 B de cada objeto de escena con
  `+0xB0 == −1` y `+0x183` o `+0x187` distinto de 0 (hash `0x3D6210`, inserción `0x7650E0`).

Quién vacía ese conjunto [E]:
- `WorldObserverDisableLoadCompleteReq` 3060 (manejador `0x29C2AB0`), solo si `[campo+0x2E4] == 0`;
- y el destructor del gestor (`0x2825550`).
- El 2245 con `{0, 0}` **no** lo vacía. Quitar el ancla con la Req nativa dejaría el conjunto lleno hasta un 3060.

Quién lo lee: **ningún lector encontrado** [E]. Busqué todos los accesos `+0x278` del exe (2263 sitios), filtrados
por los que llaman al hash o a la inserción de 16 B y por las funciones del gestor (`0x2800000…0x2860000`). Solo
aparecen el destructor, `0x2836A20` y `0x29C2AB0`.

Conclusión:
- Por el nombre, el conjunto parece «objetos de escena que esperan a que el observador termine de cargar». Su
  consumidor no se conoce [H].
- Inyectar el 2245 tras cada 2762 lo rellenaría una y otra vez y lo dejaría lleno. No se usa.

**La escritura directa** hace lo mismo que el manejador (`0x29C2915`) y nada más:
- Se ejecuta en **ServerRunThread**, en el gancho `OnRecvPacket` que ya existe (`on_server_recv`, tras
  `child_hold_server_tick`), antes de que el juego trate ese paquete.
- Va bajo el **cerrojo del propio campo**: `[campo+8]` → `campo+0x1B8`, un `pa::WindowsRWLock<4,1>` (vtable
  `0x57A30B8`).
  - El manejador usa vf1 `0x1371410` (exclusivo y espera) y vf2 `0x1371460`. El recolector lo toma compartido con
    `0x393DF0` → vf3 `0x1371AD0(1)`.
  - El mod usa **vf5 `0x1371BA0(cerrojo, 0)`**, que es `TryAcquireSRWLockExclusive` (con dueño reentrante
    `+0x28/+0x2C`), y **vf4 `0x1371B70(cerrojo, 0)`** para soltarlo. Es el mismo SRW, así que hay exclusión con el
    recolector y con el manejador, y **nunca espera**: si está ocupado, lo reintenta en el paquete siguiente.
  - Para el «quitar» de una salida se reintenta durante un máximo de **5 ms**, con `SwitchToThread` entre intentos.
    El recolector tiene el cerrojo compartido durante toda su pasada, cada 5 s.
  - Si aun así sigue ocupado, el manejador de esa salida se ejecuta con la ranura puesta. En ese caso el «quitar»
    queda pendiente y se reintenta con el mismo margen en cada paquete siguiente (también los del anfitrión) hasta
    conseguirlo. Se registran:
    - la línea `[clear] before N: the field's lock stayed busy…` y, cuando por fin se quita,
      `[clear] after N (k packets later…)`;
    - en la línea de estado, `missed at its packet / cleared later / ONE STILL PENDING`.
- Bajo el cerrojo, el mod vuelve a comprobar el estado del campo y después escribe:
  - **poner**: exige el campo abierto (`+0x4A = 1`) y la misma generación (`+0x2C8`). Escribe `+0x2E8 = clave` y
    `+0x2E4 = 1`, y nunca pisa una clave ajena;
  - **quitar**: exige la misma generación (abierto o ya cerrado) y la ranura todavía en `{1, nuestra clave}`. Escribe
    `+0x2E4 = 0` y `+0x2E8 = 0`.
- Excepciones:
  - Las lecturas y escrituras del mod van con SEH y no bloquean el mod.
  - Una excepción dentro de las dos funciones del cerrojo **no se trata**: pasa por `game_fault_continue`.
- No hay ganchos nuevos ni nada en `.xtls`, y no se inyecta ninguna Req.

### Otro lector de la ranura (vale para el 2245 y para la escritura directa)

Barrido de los accesos de byte a `+0x2E4`. En el servidor, además del recolector (`0x2C38E28`), del manejador y del
3060, aparecen dos lectores [E]:

- **`0x2B06040`** es la vf31 (`+0xF8`) de `pa::ServerField`: devuelve `+0x2E4` bajo el cerrojo compartido. No se
  conocen sus llamadores virtuales.
- **`0x2826F70`**, del `ServerSyncSceneObjectManager`, lee `+0x2E4` en `0x2827094`.
  - Con la ranura encendida y su 5.º argumento a 1, devuelve «fuera de los límites del nivel» sin hacer su propia
    comprobación de la caja del nivel (`0x184F360`). Es decir, el servidor **se fía del cliente**.
  - Ese argumento lo pone a 1 el camino de `UpdateSyncSceneObjectOutOfLevelBoundaryReq` **2573** (`0x29C31C0` →
    `0x282EE90` → `0x28256F0(…, 0, 0, 1)`). La vf2 del gestor (`0x2829E60`) lo pasa desde sus propios argumentos.
  - Efecto posible: con el ancla puesta, si un cliente informa de que un objeto de escena (tipo 7) se ha salido de
    su nivel, el servidor lo acepta. Por ejemplo, el cliente del anfitrión, sin terreno cargado junto al invitado
    (R3).
  - La medida cuenta los 2573 del anfitrión y del usuario 2, con la ranura apagada y encendida. Va en la línea de
    estado: `OutOfLevelBoundaryReq 2573 host/user 2: slot off a/b, on c/d`.

### Direcciones y bytes comprobados en el exe del disco

| RVA | Bytes / valor | Qué es |
|---|---|---|
| `0x29C2915` | `44 88 B6 E4 02 00 00 89 9E E8 02 00 00` (AOB única) | Escritura del manejador 2245; se comprueba al activar |
| `0x2C38E28` | `41 0F B6 9F E4 02 00 00` (AOB única con `66 C7 85 A0 11 01 00 01 00`) | Lectura del ancla 2 en el recolector; se comprueba al activar |
| `0x29C2905…0x29C2914` | `41 B0 01 48 8B 12 48 8D 4D F0 E8 3C 0D 9D FD 90` | `Actor_Lock(&g, [campo+8], 1)` del manejador |
| `0x57A30B8` | vf0 `0x475C90`, vf1 `0x1371410`, vf2 `0x1371460`, vf3 `0x1371AD0`, vf4 `0x1371B70`, vf5 `0x1371BA0` | `pa::WindowsRWLock<4,1>`; vf4/vf5 se comprueban antes de cada llamada |
| `0x5B29A60` | vf0 `0x2AF6A40` | `pa::ServerField`; se comprueba en cada lectura y escritura |
| `0x2AF69FD` / `0x2AF6A04` / `0x2AF74B5` | `C6 87 E4 02 00 00 00` / `44 89 B7 E8 02 00 00` / `C6 83 E4 02 00 00 00` | Constructor y reinicio del campo: la ranura empieza en 0 |
| `0x2836A20` | `48 89 5C 24 18 48 89 6C 24 20 …` | Efecto colateral del 2245; **no se llama** |

Si cualquiera de las dos secuencias de código no coincide, la opción queda apagada. Si el cerrojo o la vtable del
campo no son los esperados, no se escribe nada (línea de error, una vez).

### Cuándo se pone, se quita y se vigila

Todo ocurre en ServerRunThread. Mientras no hay nada puesto, las lecturas van cada 0,5 s. Con la ranura puesta, el
vigilante lee cada 5 s.

- **Poner** `{1, k}`, si se cumplen todas estas condiciones:
  - el usuario 2 existe y no se ha pedido su salida;
  - su fase de movimiento es `Stable`, es decir, el servidor ya le escribió su `LoadingCompleteAck` 2762;
  - ese 2762 es más nuevo que su última salida;
  - `k = [[usuario2+0xD0]+0x60]`, de clase `0xA` e igual a la clave de su `MainPlayerDataStartAck`;
  - el campo de ese personaje (`[[[pj+0x68]+0x1A0]+0x500]`) está abierto y **su dueño es el anfitrión**
    (`[campo+0x180]` = clave de usuario del anfitrión).
  - Esta última condición amplía el «FM2 == FM1» del §5.1: también cubre R9. Si el anfitrión viaja y el invitado se
    queda en el campo viejo, el ancla sigue allí.
- **Quitar antes de una salida**: el 2437, el 2283 o el 2001 del usuario 2 llegan a su `OnRecvPacket`. Ahí, antes de su
  manejador, se quita la ranura. El siguiente «poner» espera a un 2762 nuevo, y tanto el viaje como la recarga
  terminan con uno.
- **Quitar** también cuando dejan de cumplirse las condiciones: salida pedida, carga, otro campo, o un campo que ya no
  es del anfitrión.
  - **Respaldo**: si el usuario 2 desaparece sin que su 2001 haya quitado la ranura (corte del enlace, inyección
    parada), se hace un borrado completo `{0, 0}` bajo el cerrojo, solo si la ranura aún tiene nuestra clave y el
    campo la misma generación.
- **Vigilante**:
  - si la ranura sigue igual: `[verify]` una vez;
  - si se ha perdido: se vuelve a poner, como mucho 1 vez cada 5 s;
  - si tiene una clave ajena (por ejemplo, un observador nativo del anfitrión): se deja y se avisa una vez por
    valor. **No se pisa**;
  - si el campo se ha cerrado (misma generación): se quita como cualquier otro «quitar»;
  - si se ha reutilizado (otra generación): se olvida, porque el reinicio del pool ya pone la ranura a 0.
- **Épocas**: con un invitado nuevo, sus 2762 vuelven a contar desde 0. Si se sustituye la instancia del servidor
  (vuelta al título), se olvida todo y no se toca ningún campo del servidor viejo.
- **`probe`**: toma las mismas decisiones y escribe las líneas `[probe] would set|clear …`. Solo lee la ranura.

**Cuándo se ejecuta (revisión del 2026-10-12)**: solo cuando ServerRunThread recibe un paquete.
- **No hay otro punto periódico** del mod en ese hilo:
  - su bucle (`0x27734C4`) hace `pop(Q0, 1000 ms)` y llama a `OnRecvPacket` por paquete;
  - los temporizadores (entre ellos el recolector) van en otro hilo.
- `OnRecvPacket` recibe también los paquetes de la **sesión del anfitrión**: su gancho ya los usa para el espejo E4 y
  para la ranura del 3093.
- Medido en las capturas del cliente del anfitrión [V]:
  - S16 (`cdmp_packets_20261011-133910.bin`, 630 s): tras el login, los huecos entre sus C>S son ≤ 1,8 s;
  - S22 (`…-205758.bin`): ≤ 5,0 s, con un 2469 cada 5 s mientras está parado;
  - solo sus propias pantallas de carga dejan huecos más largos (12–14 s).
- Por tanto, el vigilante y el «quitar» de respaldo esperan ≤ ~5 s en juego, no al tráfico del usuario 2.
- El juego nunca se toca desde el hilo de red del mod.

**Vida de los campos [E]**:
- Los `ServerField` van en un pool y se reutilizan en el sitio. `Pool<ServerField>::Acquire` (`.xtls 0x1066CDB0`) saca
  uno de la lista libre (o lo construye) y llama siempre a `ServerField::Reset` `0x2AF7360` (vf4).
- `Reset` sube la generación bajo el cerrojo del campo, con `0x1434990` → vf7, y luego pone `+0x2E4 = 0` en
  `0x2AF74B5`. Su `r8 = 0` no vuelve a abrir el campo: el cierre (`0x2AFEA00`) dejó `+0x4A = 0`.
- **Esto ocurre fuera de ServerRunThread**, en el hilo que mueve al usuario. Aun así es seguro:
  - la única liberación es el destructor con borrado vf0 `0x2AF6A40`, que no tiene llamadores directos (es el
    desmontaje del pool junto con el servidor);
  - `[campo+8] = campo+0x1B8` se escribe una sola vez en el constructor (`0x1434920`) y `Reset` no lo toca;
  - leer el puntero del cerrojo y llamar a su TryLock es seguro en cualquier generación. Abierto y generación se
    comprueban bajo el cerrojo, así que un «poner» nunca cae en un campo que se está reutilizando.

Medida de solo lectura (con `probe` y con `1`):
- En `take_frame`, cada `RemoveActorAck` 2516 de motivo 25 de un actor `0xB` que recibe el usuario 2. Se leen todas
  sus claves.
- Las posiciones salen del gestor de actores del cliente del anfitrión: el actor todavía está ahí, porque su propio
  2516 se procesa después. También se leen las posiciones de los dos personajes.
- Distancias en 2D (x, z) al anfitrión y al usuario 2.
- Como mucho 64 búsquedas por segundo.

### Líneas de `cdmp.log`

- `WorldAnchor ON (probe|1): …` al arrancar; `Config: P8 world around user 2: user2_world_anchor '…'`.
- `WorldAnchor [set] key 0xA… field 0x… gen 0x… slot was 00/00000000 (user 2's current character 0x…; the host's own
  field: the same|another)`. El personaje actual (`+0xD8`) sirve para I5.
- `WorldAnchor [verify] slot 01/A… ok after N ms (field … gen …)`.
- `WorldAnchor [clear] before 2437|2283|2001 (…)`, `WorldAnchor [clear] <motivo> (…)`, `WorldAnchor [fallback clear] …`.
- `WorldAnchor [lost] slot XX/XXXXXXXX of field … while user 2 is in it -> set again #n`, `WorldAnchor [busy] …`,
  `WorldAnchor [forget] …`, `WorldAnchor [leave] user 2's 2437 reaches the server: nothing set …`.
- `WorldAnchor [probe] would set …` / `would clear …`.
- Cada 60 s: `WorldAnchor [minute] user2_world_anchor <modo>: …`, con estos datos:
  - estado: campo, ranura, 2762 vistos, motivo actual, contadores, 2573;
  - `collector reason 25 to user 2: N removals in P passes (position unknown U, over budget B), near user 2 (< 150 m,
    2D) K, of them beyond 150 m from the host F: d(host) min/median/max, d(user 2) max`;
  - mediana de `d(host)` de todas las bajas;
  - otros motivos;
  - distancia entre el anfitrión y el usuario 2 ahora y la máxima con baja.
- El mismo estado sale también en la línea `Session2: …`.

### Prueba en un PC (anfitrión + `cdmp_peer` como usuario 2)

`cdmp.p8-noche-local.ini` ya trae `user2_world_anchor=probe`.

1. Arranca `cdmp_peer --mode replay` con el guion E3 y lanza el juego. El peer entra y se queda quieto donde aparece.
2. **Pasada A (`probe`)**:
   - Tras el 2762 del peer sale `WorldAnchor [probe] would set key 0xA… field … (the host's own field: the same)`.
   - El anfitrión se aleja **400–600 m** a pie o a caballo, espera 60–120 s y vuelve.
   - Esperado: en `WorldAnchor [minute]` aparecen `near user 2 … K > 0` y `beyond 150 m from the host F > 0`
     (`d(host)` ~200–250 m). Esto reproduce el fallo en un PC.
3. **Pasada B (`1`)**: `user2_world_anchor=1` y el mismo recorrido.
   - Esperado: `[set]` y, a los 5 s, `[verify] … ok`.
   - `F ≈ 0`; solo quedan las otras ramas del recolector: rama de 50 m, caducidad.
   - Sin `FAULTED`, sin `GAME FAULTS passed on`, sin `[lost]` ni `[busy]`.
   - Mirar también el contador 2573 `on c/d`.
   - **R3**: mirar si los NPC junto al peer siguen moviéndose. En la captura del anfitrión, que siga enviando
     2556/2223/2734 suyos con el anfitrión a 300–600 m.
4. **Salidas**:
   - despedida del guion (`login_e3lc00bye_*.troc`) → `[clear] before 2001`;
   - corte a mitad (`…cut8…`) → `[fallback clear]` o `[clear] user 2 is gone`;
   - en ambos casos, la ranura vuelve a `00/00000000` en la línea de estado.

Limitación (§6.2): el peer no pide spawns, así que la supervivencia de NPC **nuevos** junto al invitado solo se ve con
un cliente real.

### Qué necesita dos PC (Shadow de invitado)

- El caso real del §6.3:
  - con el anfitrión quieto, el invitado recorre 300–600 m hasta una zona no visitada;
  - sus NPC y animales nacen, siguen vivos y se mueven (R3/I2);
  - prueba también el viaje rápido del invitado: `[clear] before 2283`/`2437`, luego `[set]` tras su 2762;
  - y la salida al título: `[clear] before 2001`.
- Comparar los FPS del anfitrión, `SAM+0x9C`, el tráfico del túnel (R4) y los 2573 con la ranura encendida.
- Empezar con `user2_world_anchor=probe` en `cdmp.p8-s7-host.ini`, como está ahora, y pasar a 1 cuando la pasada B
  en un PC salga limpia.

### Riesgos que quedan

- **R3** (IA y física lejos del cliente controlador): sin cambios, se mide en la pasada B y con dos PC.
- **Los otros lectores de `+0x2E4`**:
  - La aceptación de los 2573 (arriba) existe con cualquier forma de poner el ancla.
  - La vf31 `0x2B06040` tiene llamadores virtuales desconocidos.
  - El conjunto `+0x278` del 2245 no se toca.
- **Sin paquetes no hay vigilante**: el gancho solo se ejecuta cuando ServerRunThread recibe un paquete. En juego,
  el anfitrión manda uno cada ≤ ~5 s (medido arriba). Durante su propia pantalla de carga el retraso es mayor, pero
  entonces su campo cambia de todos modos.
- **Cerrojo ocupado al quitar antes de una salida**: tras 5 ms el «quitar» queda pendiente y su manejador se ejecuta
  con la ranura puesta. Con un 2001, la clave vieja seguiría hasta el siguiente paquete en que el cerrojo esté
  libre. Como `FindActor` no comprueba la generación, podría anclar a otro actor durante ≤ 1 pasada, lo que solo
  mantiene vivos unos NPC 5 s más. Se cuenta.
- **Una sola ranura (I7)**: con más de 2 jugadores haría falta el gancho intermedio del §5.3.
