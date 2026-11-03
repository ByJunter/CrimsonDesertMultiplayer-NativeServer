# P8 sesión 15: viaje rápido del usuario 2 (F1, F2, F3)

Build 1.0.0.2976, base `0x140000000`. Análisis estático con capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`,
sin abrir el juego (11-10). La causa raíz está en `p8-s14-teleport-cierre.md`. Marcas: **[V]** verificado (código o
captura); **[E]** estimación fuerte; **[H]** hipótesis.

Código:
- `src/mod/session2/fast_travel_logic.h`: lógica pura (tests en `tests/optd_fast_travel_tests.cpp`).
- `src/mod/session2/fast_travel.{h,cpp}`: F1 (invitado) y F2 (anfitrión).
- `src/mod/session2/user2_funnels.cpp`: F3, dentro del gancho vf48 que ya existía.
- `render/hud.{h,cpp}`: `hud_notice`, el aviso del HUD.
- `src/mod/session2/travel_probe.{h,cpp}` y `travel_probe_logic.h`: el alias del dueño en el 2643 y la sonda
  `user2_travel_probe` (§11).
- `src/mod/session2/observer_milestone_reload_logic.h` y `observer_milestone.{h,cpp}`: el 2322 del usuario 2 tras una
  recarga, `observer_milestone_reload` (§12, S16).

## 0. Resumen y recomendación para la sesión 15

| Pieza | Lado | Clave | Qué hace |
|---|---|---|---|
| **F3** | anfitrión | `user2_fast_travel = 0 \| probe \| 1` | En el 2283 del usuario 2, las dos vf48 (`0x2A5F7FC` y `0x2BC4C53`) devuelven **el gestor de secuenciadores del propio usuario 2**. Si no se puede leer, la primera devuelve un bloque con `+0x18` = su personaje. El destino se escribe en **su** `TransformSync+0x550`. `probe` solo comprueba que se instala: F2 sigue descartando. |
| **F2** | anfitrión | siempre, salvo que F3 esté vivo | El 2283 del usuario 2 se **descarta** antes de llegar al servidor si el usuario 2 no está en un campo propio. El `+0x550` del anfitrión nunca se toca. |
| **F1** | invitado | `guest_fast_travel_veto = 0 \| probe \| 1` | Con la conexión en remoto, la entrada `0x655110` devuelve 0 sin efecto y el HUD muestra un aviso. `probe` solo registra. |

**Recomendación:**
- Anfitrión: **`user2_fast_travel=1`**.
- Invitado: **`guest_fast_travel_veto=probe`**. En comportamiento es igual que 0, pero además registra los argumentos.

Ya están así en `cdmp.p8-s7-host.ini` y `cdmp.p8-noche-local.ini` (anfitrión), y en `cdmp.p8-s7-shadow.ini`,
`cdmp.p8-s7-shadow-fase2.ini` y `cdmp.p8-s12-shadow-seguro.ini` (invitado).

Por qué:
- La parte del **servidor** de F3 está entendida [V estático]:
  - `0x2896120` usa el gestor **solo** para leer `[gestor+0x18]`.
  - Todo lo que escribe después (§2.3) cae en objetos del viajero: su TransformSync, su usuario, sus compañeros/montura
    y, desde la revisión, el `+0x6A0` de su propio gestor (la segunda vf48, `0x2BC4C53`, también se redirige).
  - El 2437 posterior sigue el camino «mismo campo» (`SendMoveFieldAck 0x2BCA3D0`), que **salta** los pasos que son
    solo del dueño cuando el viajero no es el dueño del campo (§2.4).
- Lo que **no** está verificado es el cliente del invitado recargando dentro del campo compartido [H]. Es justo lo que
  hay que probar.
- Si falla, el resultado es el de la sesión 14 (el invitado sale al título), no algo peor. El anfitrión queda protegido
  en todos los casos: F3 redirige o F2 descarta.

Plan de aborto (§8): ante el primer fallo del invitado (logout, carga de más de 60 s o crash), pasar al invitado a
`guest_fast_travel_veto=1` para el resto de la sesión. El anfitrión puede seguir con `user2_fast_travel=1`: sin 2283
del invitado, no hace nada.

Si no se quiere probar F3 en la sesión 15: anfitrión `user2_fast_travel=0` (F2) e invitado `guest_fast_travel_veto=1`.

## 1. Lo que respondió el usuario sobre la sesión 14 (ajustes a `p8-s14-teleport-cierre.md`)

1. **Al aceptar el diálogo de error, el juego se cerró directamente; no se vio la pantalla de título.** Lo que el log
   llama «título» no lo vio el usuario: es el camino **automático** del cliente antes de `Terminate`.
   - el estado 0x13 a las 01:29:53.9;
   - la reconexión del título a las 01:30:06.9 (1001/2319/2490 al servidor local);
   - la 2001 local a las 01:30:10.3.

   Por tanto, el `Terminate 시작` de las 01:30:10.5 es la salida que provoca el diálogo de error crítico, no una salida
   pedida desde el título. Se corrige la lectura [E] del §0.1 y del §2 de aquel documento: «salida pedida desde el
   título» pasa a ser «salida del propio diálogo». La conclusión no cambia: ni crash ni cuelgue.
2. **El destino era el único punto de viaje rápido desbloqueado en esa partida: la propia ciudad de Hernand**, a unos
   263 m, en el mismo campo. Por eso el caso objetivo de F3 es una **recarga dentro del mismo campo**. El código
   confirma que el 2283 siempre va por ese camino (§2.4):
   - `0x2896120` construye el destino con `+0x18 = 0` y `+0x19 = 1` fijos;
   - con eso, `0x2BC9200` siempre llama a `SendMoveFieldAck 0x2BCA3D0` (sin `MoveFieldOnceTimer`, sin `AcquireField`,
     sin H1/H2 de E3b).

   Que el mundo abierto entero sea `FieldInfoKey 1` (el 2283 de la sesión 14 lleva 1) es [E]. Si es así, cualquier
   viaje rápido del mundo abierto es una recarga dentro del mismo campo.

## 2. F3: el gestor de secuenciadores del propio usuario 2

### 2.1 El manejador del 2283 en el servidor, `0x2A5F580` [V]

- Recibe `rcx = Troc`, `rdx = res` y `r8 = params`. Lee el paquete en este orden (`0x2A5F64B..0x2A5F77B`):
  1. `u32 a` → `[rbp-0x58]`;
  2. `u32 b` → `[rbp+0x98]`;
  3. `u32 c` → `[rbp-0x60]`;
  4. `float3` posición → `[rbp+0x24]`;
  5. `float4` rotación → `[rbp+0x14]` (`0x141FA90`);
  6. `float3` escala → `[rbp+8]`;
  7. `u8` → `[rbp+0x90]`.

  En total 53 B.
- `rsi = [params]`: el personaje remitente (el sujeto de ExecuteTroc). `0x2A5F7B7 call 0x17FA6C0` → su campo, en
  `[rbp-0x28]`.
- Llamada que se redirige:

  ```
  0x2A5F7F2  48 8B 4D D8        mov rcx,[rbp-0x28]       ; el campo
  0x2A5F7F6  48 8B 01           mov rax,[rcx]
  0x2A5F7F9  FF 50 48           call [rax+0x48]          ; ServerField::vf48 0x2B00CE0
  0x2A5F7FC  48 8D 8D 90 00..   lea rcx,[rbp+0x90]       ; <- dirección de retorno que filtra F3
  ...
  0x2A5F829  48 8B C8           mov rcx,rax              ; el gestor, primer argumento
  0x2A5F82C  E8 EF 68 E3 FF     call 0x2896120
  ```

  `0x2896120` recibe: `rcx` el gestor, `rdx = &res`, `r8 = &a`, `r9 = &b`, `[rsp+0x20] = &c`,
  `[rsp+0x28] = &transformación` y `[rsp+0x30] = &u8`.
- Si `res ≠ 0`, manda el Nak 1013 a la sesión del remitente.
- **No se parchea nada aquí.** F3 comprueba byte a byte `0x2A5F7F2..0x2A5F830` (63 B, con el `call 0x2896120`) y la
  cabecera de `0x2896120` hasta `mov r13,[rcx+0x18]` (`0x2896155`, 61 B). Si algo no coincide, F3 no se activa y F2
  descarta los 2283.

### 2.2 El mecanismo

- Se usa el gancho de entrada que `user2_funnels` ya tenía en `ServerField::vf48 0x2B00CE0`. Antes solo contaba; está
  verificado en `p8-impl-rango-embudos.md`. También se usa el sujeto de ExecuteTroc `0x2774B20`, ya existente.
- **No hay gancho nuevo en el código del juego.** Con `user2_fast_travel ≠ 0` se instalan vf48 y el sujeto aunque
  `quest_per_user` y `kill_loot_per_user` estén a 0.
- Condición (`ftl::decide_f3`):
  - hay sujeto, es decir, el `[params]` del Troc es el personaje principal del usuario 2 (clave, controlador +0x90 y
    vtable);
  - la dirección de retorno es exactamente `exe+0x2A5F7FC`.

  El opcode solo va al log: si un Troc está fuera de la tabla de manejadores, su opcode se lee como 0, y eso no debe
  dejar pasar el gestor del dueño.

  **Con `1` y esas dos condiciones, el 2283 del usuario 2 nunca recibe la vf48 original** (revisión 2a). Da igual el
  campo, las claves o la lectura de `[campo+0x180]`, que solo va al log. En el campo propio del usuario 2, la original
  sería su gestor de todos modos.
- Lo que devuelve con `1`: `[[[sujeto+0x68]+0xA8]+0x18]`, la misma cadena que vf48 sigue desde el personaje del dueño
  (`0x2B00D0D..0x2B00D24`). Se comprueba:
  - la vtable es `ServerSequencerStageManager` (`exe+0x5B17A58`; RTTI `.?AVServerSequencerStageManager@pa@@`
    comprobada en el exe);
  - **`[gestor+0x18] == sujeto`**, que es lo que lee `0x2896155`.

  En ese caso **no** se llama a la vf48 original: solo hace `FindUser`, la cadena y suelta el bloqueo, sin efectos
  secundarios.
- **Si la cadena falla (`SubstituteBlock`):** devuelve un bloque `thread_local` de 0x40 B con `+0x18 = sujeto`. Se
  registra con un WRN. Es seguro [V]:
  - `0x2896120` lee su gestor una sola vez, `mov r13,[rcx+0x18]` en `0x2896155`;
  - no guarda `rcx`, que se sobrescribe en `0x289615E`;
  - el manejador solo hace `mov rcx,rax` con el resultado (`0x2A5F7FC..0x2A5F82B`);
  - el sujeto es `[params]`, que es el `rsi` del manejador.

  El destino sigue yendo al TransformSync del usuario 2.
- **La segunda vf48 del mismo 2283 (revisión 1)** está dentro de `0x2BC4950`, llamada desde `0x2896974`:

  ```
  0x2BC4C49  48 8B 4D DF        mov rcx,[rbp-0x21]     ; el campo del field mover del VIAJERO (0x2BBF340(TS+0x4F0))
  0x2BC4C4D  48 8B 01           mov rax,[rcx]
  0x2BC4C50  FF 50 48           call [rax+0x48]        ; ServerField::vf48 -> el gestor del DUEÑO
  0x2BC4C53  48 8B D8           mov rbx,rax            ; <- segunda dirección de retorno que filtra F3
  0x2BC4C56  48 8D 50 08 ...    lea rdx,[rax+8] ; ... ; call 0x393650   ; referencia sobre [gestor+8]
  0x2BC4C80  C6 83 A0 06 00 00 00  mov byte [rbx+0x6A0],0
  ```

  Se ejecuta en **todo** 2283: `0x2896120` pone `dest+0x19 = 1` y el salto de `0x2BC4C23` entra aquí cuando no es 0.
  - Antes de esta revisión, F3 solo redirigía la primera vf48: cada viaje del usuario 2 ponía a 0 el `+0x6A0` del
    gestor **del anfitrión** y tomaba una referencia sobre su `[+8]`.
  - Ahora, cuando F3 devuelve el gestor real en `0x2A5F7FC`, lo guarda en el marco del sujeto (`Frame::ft_mgr`). El
    `Restore` de ExecuteTroc lo borra al salir del Troc.
  - La vf48 que vuelve a **`0x2BC4C53`** dentro de ese mismo Troc recibe el **mismo gestor real**. Ahí no puede ir el
    bloque: se usa `[gestor+8]` como objeto vivo, y un gestor nulo rompe en `0x2BC4C69`.
  - Si F3 tuvo que usar el bloque, esa segunda vf48 llama a la original: el `+0x6A0` del anfitrión se pone a 0 y queda
    un **ERR** en el log.
  - Los bytes `0x2BC4C49..0x2BC4C87` (62 B) se comprueban en `code_ready` y en `verify` antes de activar F3.
  - Cualquier otra vf48 que llegue bajo el mismo Troc armado se cuenta por llamador en el resumen («other vf48 while a
    2283 of user 2 is applied»), sin cambiarla.
- Registros y marco: el gancho está en la **entrada** de una función entera (una llamada virtual normal, ABI x64). El
  llamador solo usa `rax`: `mov rcx,rax` en `0x2A5F829` y `mov rbx,rax` en `0x2BC4C53`. No toca nada más del marco.
- **`probe` en el anfitrión no cambia nada y no prueba el viaje** (revisión 3). F2 descarta todos los 2283 con probe:
  solo sirve para ver en el log que F3 se instala (sitios verificados y sujeto). Para probar el viaje hace falta `1`.
- **ERR si un 2283 del usuario 2 llega a la vf48 original en `0x2A5F7FC`:** pasa con `0` o `probe` (instalado por
  otra clave), o con F3 sin verificar. El texto es: «a 2283 of user 2 reached the original vf48 …». F2 lo hace
  imposible salvo en una carrera.

### 2.3 Efectos secundarios de `0x2896120` → `0x2BC4950` (todos sobre el viajero tras F3)

`r13 = [gestor+0x18]`, el personaje. `[rsp+0x38]` es el objeto bloqueado desde `[r13+0xA0]`, que es su
**ServerUserActor** (`field_share.h kActorUser`).

| Dirección | Qué hace | De quién, con F3 | Marca |
|---|---|---|---|
| `0x2896159` `0x206AE30([r13+0xA0])` | bloquea al usuario del personaje (si falla, `res` = error y nada más) | usuario 2 | V |
| `0x2896209` `0x28FB8B0(r13)` | `GetField` del personaje (`salida-en-carga.md` §1.3): el campo compartido | — | V |
| `0x28962C8..0x289636E` | si `a` está en la tabla `exe+0x6D6C1F8`: `campo->vf40` (gestor de misiones) → `0x1AC2320` (un paso de misión). Los dos remitentes del cliente ponen `a = 0` | con `quest_per_user=1` es **el del usuario 2**: el retorno `0x2896347` se añadió a `vf40_caller_allowed` | V (código) / E (que `a` sea siempre 0) |
| `0x28963B5` | lee la palabra `[[usuario+0x68]+0x20]+0x30` (nivel) | usuario 2 | V |
| `0x28963C2` `b == 0` | recarga en el sitio (su propia transformación) | — | V |
| `0x28963F0` `c ≠ 0` | `0x2A827B0`: busca el actor `c` y viaja a su posición | — | V |
| `0x2896891` `0x2B97A20([[r13+0x68]+0x110], &dest, 0, &lista)` | componente `+0x110` del personaje. Usa `[[r15+0xA0]+0xD8]`, otro personaje del usuario: montura o compañero que viaja con él | usuario 2 | V (lectura) / E (semántica) |
| `0x28968A6..0x2896945` (si el `u8` del paquete ≠ 0; el cliente pone 1) | `0x222B4B0` y **`0x2228E40` = RideOff** (`caballo-coop.md`): desmonta al viajero | usuario 2 | V / E |
| `0x2896974` `0x2BC4950(TS, usuario, &dest)` | `TS+0x550 = destino` (`0x2BBF510`), `TS+0x591 = 1`, `[personaje+0xC3] = 0`, `[[usuario+0x68]+0x20]+0x398 \|= 4`. Después `[[usuario+0x68]+0x1C0]->vf170()` | usuario 2 | V (escrituras) / H (qué es vf170) |
| `0x2BC4C32..0x2BC4C91` (siempre: `dest+0x19 = 1`) | `0x2BBF340(TS+0x4F0)`: el campo del field mover del viajero, **el compartido**. Su **vf48** (`0x2BC4C50`, retorno `0x2BC4C53`) da un gestor; se toma una referencia sobre `[gestor+8]` (`0x393650`), se escribe **`[gestor+0x6A0] = 0`** (`0x2BC4C80`) y se suelta (`0x393C10`) | **sin la revisión, el del ANFITRIÓN**. Ahora es el del usuario 2 (el gestor real de F3); con el bloque, el del anfitrión (ERR) | V |

- Con F3 completo (gestor real), **ninguna escritura** cae sobre el anfitrión, su cámara, su UI ni su guardado.
  - **Excepción:** si F3 tuvo que usar el bloque, el `+0x6A0` del gestor de secuenciadores del anfitrión se pone a 0.
    Queda un ERR en el log. Qué significa ese byte es [H].
- **No se envía ningún paquete** desde `0x2896120`/`0x2BC4950`; la respuesta (2643 + 3066, §2.4 y §11) sale después,
  con el 2437.
- Lo compartido es el **campo** (`GetField`, solo lectura) y su vf48 de `0x2BC4C50`, que F3 redirige.
- `vf170` del componente `+0x1C0` del usuario es [H]. Si fuera un guardado, el guardián del anfitrión rechaza el del
  usuario 2 (`slot2_backup`, como en la sesión 14).

### 2.4 Lo que pasa después: el 2437 del invitado

- `0x29C59D0` → `0x2BC9200(TS del usuario 2)`. Ahora `[TS+0x550] ≠ 0` y sigue adelante. El código (`0x2BC9299..`):
  1. los compañeros que no están en la lista del destino se despiden (`[[pj+0x68]+0x110]->vf160`);
  2. `TS+0x568` (`dest+0x18 = 0`) → no `GameLoadingStart_Step`;
  3. `TS+0x569` (`dest+0x19 = 1`) → **`0x2BCA3D0(TS, res, 0)` = SendMoveFieldAck sin cambio de campo** [V].
- En `0x2BCA3D0`:
  - Hay dos `cmp edi, ebx` (`0x2BCA561`, `0x2BCA7AF`): clave del viajero contra la del personaje del dueño del campo.
  - Si **son el mismo**, llama a `campo->vf40 → 0x29228C0` y a `campo->vf48 → 0x287DA50`, y luego a
    `0x28D1380([comps+0x168])`: pasos de misión y de secuenciadores **del dueño**.
  - **Para el usuario 2 no son el mismo: se saltan** [V]. Por eso el viaje del invitado no toca el gestor de misiones
    ni el de secuenciadores del anfitrión.
  - **Corrección (§11, 11-10 noche):** lo que manda `0x2BCA3D0` **no es un 2174**. Llama a `0x299B520` en
    `0x2BCA79E`, que es el escritor del **LoadingFieldDataAck 2643** (`mov eax,0xA53` en `0x299B916`), y a `0x299A610`
    en `0x2BCA86C`, el del **MoveFieldCompletePostAck 3066** (`0xBFA`). El escritor del 2174 es `0x29997F0` (`0x87E`),
    y `0x2BCA3D0` no lo llama [V capstone; coincide con `owner_fanout_writers.h`, generado].
  - El 2643 lleva en su u32 `+9` el mismo `[campo+0x180]` (el dueño, el usuario del anfitrión) que el 2174.
- Por tanto, de las piezas del mod que existen, **ninguna de las de 2174 actúa en este viaje** (§11.2):
  - la reescritura `MoveFieldAck owner -> user 2` de `server.cpp` no ve ningún 2174: ahora hay una igual para el 2643;
  - la fase de movimiento se queda en MOVE_ASKED hasta el 2445/2762 de la fase 0x17 del cliente;
  - `observer_seq_copy`, `observer_milestone` y `load_defer` no ven un viaje nuevo (no hay 2174 ni 2549).

  No pasa por H1/H2 de E3b, porque no hay `AcquireField` [V].
- `TS+0x550` solo lo limpia `MoveFieldComplete` (`0x2BC6BB0` → `.xtls 0x106E7630` → `0x2BBF580`, que pone a cero
  `FM+0x60` = `TS+0x550`).
- [H] Sin verificar: cómo recarga el cliente del invitado dentro del campo compartido:
  - gestores X del Kliff del anfitrión (`guest_sched_route`, ya con la guarda «X GONE»);
  - copias de secuenciadores;
  - la cámara;
  - los NPC que ya tenía.

  Es lo que prueba la sesión 15.

## 3. F1: el veto en el invitado

- Se probaron dos candidatas [V]:
  - **`0xA9A860`** solo tiene un llamador en `.rsrc`: `0x6555C6`, dentro de `0x655110`. Es el envío del 2283 para un
    punto de viaje: llama a `0x778AB0` y después a `PostPacket 0xC04D20`.
  - **`0x655110`** es la entrada. Sus llamadores son los tres del viaje desde el mapa del mundo: `0xD9E7D8` (con
    `[rbx+0x960]`/`[rbx+0x964]`), `0xDBC73D` y `0xDBC768` (con `[rbx+0x968]`, el viaje a un actor). **Los tres ignoran
    el valor de retorno** y borran su petición pendiente de todos modos.
- Firma de `0x655110`: `al(rcx, edx = punto o -1, r8d = índice, r9d = actor)`.
  - `r9d ≠ 0`: viaje al actor.
  - Si no: el punto → `0xA9A860`.
  - Devuelve 1 cuando lo manda. Las salidas nativas sin efecto devuelven 0: `0x6553A8`, `0x6554F5` y `0x6555D2`
    (`edx == -1` o índice fuera de rango).
- **Gancho:** inline en la **entrada** `0x655110`. Se verifican 36 B:
  `48 89 5C 24 18 | 89 54 24 10 | 48 89 4C 24 08 | 55 56 57 41 56 41 57 | 48 8D AC 24 50 FE FF FF | 48 81 EC B0 02 00 00`.
  - Ninguna instrucción es relativa a `rip`.
  - Solo se acepta un parche que cubra instrucciones enteras.
  - También se verifican la cabecera de `0xA9A860` (20 B) y el `call` de `0x6555C6`.
  - `.rsrc`, nunca `.xtls`.
  - Se instala tras `WorldSystem` y con el jugador en el mundo 3 s.
- **Con `1` y `guest_remote_live()`:** devuelve 0 sin llamar a la original. No se ejecuta `0x778AB0`, así que no hay
  `[P+0x15C] = 1`, ni carga, ni 2283. Además pone `render::hud_notice(FastTravelVetoed, 6 s)`: dos `store` atómicos, sin
  lock ni asignación de memoria en el hilo del juego. El texto sale en el panel del overlay: «Viaje rapido no disponible
  en cooperativo (invitado): ve andando o a caballo».
  - Si el panel está oculto con F8, el aviso sale igual, en una ventanita propia arriba a la izquierda (revisión 5).
  - Con el overlay apagado (`[overlay] enabled=0`) no hay aviso visible; solo queda la línea WRN `GuestFastTravel …
    VETOED` del log.

  [H] Lo que hace la UI del mapa al no pasar nada, por ejemplo si se cierra o se queda en «teletransportando», se ve en
  la prueba. Por los llamadores, no queda ninguna petición pendiente.
- **`probe`:** llama a la original y registra el punto, el índice, el actor, el llamador y el valor devuelto, con una
  advertencia si es remoto.
- Con la partida local (sin conexión remota), siempre se comporta de forma nativa.

## 4. F2: el filtro del anfitrión

- Dónde: `session2.cpp offer()`, en el hilo de red, antes del filtro de `observer_seq_copy`.
  - `fast_travel_host_judge(opcode, payload)` lee y decide en cada oferta, sin contar ni escribir líneas.
  - `fast_travel_host_record(verdict, desenlace)` cuenta y escribe la línea **una sola vez**, en el desenlace final:
    descartado por F2, entregado al servidor, descartado por la política del túnel o inyección detenida.
  - Una Req retenida (`Hold`) se vuelve a ofrecer y se vuelve a juzgar, pero no se cuenta dos veces (revisión 4).
- Para un 2283 se lee con lecturas protegidas, en solo lectura:
  - usuario 2 → `[+0xD0]` personaje → `[[+0x68]+0x1A0]` TS → `[TS+0x500]` campo → `+0x180` dueño;
  - su cadena de gestor (§2.2), solo para la línea;
  - **la prueba del sujeto de ExecuteTroc sobre ese personaje** (`user2_funnels_subject_ready`, revisión 2b): las
    claves que publica `user2_funnels` están vivas y coinciden la clave, el controlador +0x90 y la clase. Es la misma
    `decide_subject` que se aplica en el hilo del servidor.
- Decisión (`ftl::decide_gate`):

  | Situación | Resultado |
  |---|---|
  | El usuario 2 está en un campo propio (dueño = usuario 2) | pasa (el código nativo ya es correcto) |
  | `user2_fast_travel=1`, F3 vivo y el sujeto reconoce al personaje del usuario 2 | pasa al servidor (F3 lo redirige; si su gestor no es legible, con el bloque) |
  | Cualquier otro caso, incluidos `probe` y «no se sabe dónde está» | **se descarta**, con una línea WRN (las primeras 16) y un contador |

- **Descartar no salva al invitado.** Su cliente ya está en 0x04, manda el 2437 y el servidor responde
  `eErrNoInvalidFieldInfoKey` → 2679. Para el invitado hace falta F1 (§3).
- Contador en `Session2: … | fast travel 2283 of user 2: N (to the server with F3 x, own field y, DROPPED z)`.

## 5. El 2283 y `observer_seq_copy` [V]

- Captura de la sesión 14 (t = 649.399, 53 B):

  ```
  00000000 01000000 00000000 | BC0926C6 A24D1B44 2C546EC5 | 00000000 4B13D63D 00000000 FC987E3F | 0000803F 0000803F ...
  ```

  Es decir: `a = 0`, `b = FieldInfoKey 1`, `c = 0`, posición (−10626,4; 621,2; −3813,3), cuaternión (0; 0,1045; 0;
  0,9945) ≈ 12° y escala 1. El log de la captura solo muestra 48 B; el `u8` final no se ve.
- La suposición de `observer_seq_copy_logic.h:74` («u64 seq @0 [E: not in a capture]») era falsa. Ese u64 es
  `FieldInfoKey << 32`, nunca un id de secuenciador. Hasta ahora no hacía daño: `0x100000000` no está en el libro.
  Pero un `a = id` con `b = 0` podría haberse descartado por error.
- Se **quitó** el 2283 del filtro de secuenciadores (`judge_req` y `observer_seq_copy_filter_req`) y del parseo de
  escenas de `scene_diag_logic.h`, que leía un id de escena falso.
- El test `test_seq_copy_ignores_2283` prueba que un 2283 cuyo u64 sería `0x1AB`, un id del libro, pasa.

## 6. El `+0x550` del Kliff del anfitrión en la sesión 14

- Con el fallo, el destino del invitado y todo lo de §2.3 cayeron sobre el **anfitrión** [V estático]:
  - `TS+0x550`;
  - `TS+0x591 = 1`;
  - `[pj+0xC3] = 0`;
  - el bit 4 de `[[usuario+0x68]+0x20]+0x398`;
  - `vf170`;
  - `[gestor+0x6A0] = 0` del gestor de secuenciadores del anfitrión (`0x2BC4C80`);
  - y, con el `u8 = 1` del cliente, **RideOff del anfitrión** (si iba montado) y `0x2B97A20` con **su** montura o
    compañero `+0xD8` hacia el destino del invitado [E].
- `TS+0x591` lo leen `0x2822E61` y `0x288B84C`. Mientras vale 1, **las dos funciones se saltan su trabajo** (`jne` al
  final). `0x288B84C` es la función con la puerta de dueño `0x288B87A` que mide `shop_probe` (escenas de tienda) [V].
  Así que, hasta que el anfitrión completara su propio cambio de campo, sus escenas de tienda en el servidor habrían
  quedado paradas [E].
- Nada de esto persiste: es memoria del proceso, y el anfitrión cerró el juego 47 s después.
  - Que `TS+0x550` no vaya al guardado es [E]: el guardado de posición usa `TS+0xB4`, no el destino pendiente.
  - En la partida siguiente no hay nada que reiniciar.
- Durante la misma partida, se limpia con el siguiente `MoveFieldComplete` del anfitrión: su propio viaje rápido, una
  muerte con reaparición o una carga. Su propio 2283 o la muerte (`0x2BC4950` desde `DeadActor`) escriben un destino
  nuevo antes de que se lea.
- **No se añade ningún reinicio:** F2 y F3 impiden que vuelva a pasar.
- Si alguna vez sale el ERR «a 2283 of user 2 reached the original vf48», el destino cayó en el anfitrión: debe hacer
  un viaje rápido propio o reiniciar el juego.
- El ERR de la segunda vf48 (`+0x6A0` del dueño, tras el bloque) no mueve a nadie: basta con anotarlo.

## 7. Inis

| Fichero | Clave | Valor |
|---|---|---|
| `cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini` | `user2_fast_travel` | `1` |
| `cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini`, `cdmp.p8-s12-shadow-seguro.ini` | `guest_fast_travel_veto` | `probe` |

- `quest_per_user=1` ya está en los inis del anfitrión, así que el paso de misión `0x2896347`, si llega a ocurrir, va al
  usuario 2.
- En `cdmp.p8-noche-local.ini`, con los guiones de login normales `cdmp_peer` no manda 2283: solo comprueba que F3 se
  instala y que el anfitrión no cambia. Con `login_e3lc00_travel_s14.troc` sí lo manda (§11.4).
- Desde §11 los dos inis del anfitrión llevan también `user2_loading_owner_alias=1` y `user2_travel_probe=1`.

## 8. Plan de pruebas

### 8.1 En un PC (anfitrión + `cdmp_peer`), sin Shadow

1. Despliega y arranca con `cdmp.p8-noche-local.ini`. En `cdmp.log` deben aparecer:
   - `User2Funnels: user2_fast_travel 1 LIVE: user 2's ReloadStageStartReq 2283 -> vf48 returning to exe+0x2A5F7FC
     answers user 2's own ServerSequencerStageManager …`;
   - `FastTravel F2 (host): user2_fast_travel=1: … handed to the server only while F3 is live and ExecuteTroc's subject
     recognises user 2's character …`;
   - ningún `REFUSED`, tampoco el de `0x2BC4950's vf48 call 0x2BC4C50`.
2. El **anfitrión** hace un viaje rápido propio con el usuario 2 conectado.
   - Esperado: viaje normal. En el resumen, `the 2283's vf48 under user 2: none yet` y `0x2BC4950's vf48: user 2's 0`:
     el 2283 del anfitrión no tiene sujeto.
   - En el resumen de `Session2`, `fast travel 2283 of user 2: 0`.

### 8.2 Dos PCs, sesión 15 (Shadow invitado)

Caso objetivo: **viaje rápido dentro de Hernand**, el mismo campo y el único punto desbloqueado.

Anfitrión:
1. `cdmp.p8-s7-host.ini` (`user2_fast_travel=1`). Antes de nada, comprueba las líneas del punto 1 de §8.1.
2. Espera a que el invitado esté dentro y estable. Quédate cerca, a menos de 100 m del punto de destino si se puede,
   para ver llegar al invitado.
3. Tras el viaje del invitado busca en el log:
   - `FastTravel F2: user 2's ReloadStageStartReq 2283 (FieldInfoKey 0x1, … ) | user 2 … is in the host's field … |
     ExecuteTroc's subject recognises it | its own sequencer manager readable | F3 1 (live) -> to the server (F3
     redirects its vf48) (handed to the server)`, una sola vez por viaje;
   - `User2Funnels fast travel (user2_fast_travel=1, live) … Req 2283 of user 2 … | user 2's manager | returned user
     2's ServerSequencerStageManager 0x… : the destination goes to ITS TransformSync+0x550`;
   - `User2Funnels fast travel … 0x2BC4950's vf48 from exe+0x2BC4C53 | returned user 2's manager 0x… (its +0x6A0 is
     zeroed, not the host's)`, con la misma dirección que la línea anterior;
   - en el resumen, `other vf48 while a 2283 of user 2 is applied (the owner's): none`. Si sale algún llamador, anótalo:
     es otra lectura del gestor del dueño durante el viaje;
   - **un 2643 y un 3066 para el usuario 2** (no un 2174: §11): `Session2: user 2's LoadingFieldDataAck 2643 names the
     field owner 0x… (the host); user 2's copy names user 2 …` y, con `user2_travel_probe=1`, las líneas `TravelAck`.
     La fase de movimiento pasa por `LOADING_SENT` (2445) y vuelve a `STABLE` (2762);
   - **ningún `Nak 1013`** y **ningún 2679** con `0A 63 19 8E`.
4. Comprueba que tu Kliff no se ha movido, ni bajado del caballo, ni perdido su montura o compañero. Después haz **tú**
   un viaje rápido y comprueba que llegas a **tu** destino.

Invitado (Shadow):
1. `cdmp.p8-s7-shadow.ini` (`guest_fast_travel_veto=probe`). En el log: `GuestFastTravel: guest_fast_travel_veto=probe
   ON: exe+0x655110 hooked`.
2. Entra en el anfitrión y juega 1–2 min (estable). Abre el mapa (ESC → mapa) y elige **Hernand**.
3. Esperado: línea WRN `GuestFastTravel … 0x655110 to point … | connection REMOTE … | native (probe: logged only), it
   returned 1`; fundido y carga; apareces en el punto de Hernand; los NPC y el anfitrión siguen visibles.
4. Muévete 30 s, habla con un NPC y abre/cierra el menú. Haz **Ctrl+F12** si algo se ve raro.
5. Devuelve `cdmp.log` y `cdmp_packets_*.log` por la carpeta compartida.

**Prueba de F1, solo si F3 falla o al final de la sesión:**
1. Pasa el invitado a `guest_fast_travel_veto=1`, reinicia el juego y vuelve a entrar.
2. Repite el viaje.
3. Esperado: no hay carga, aparece el aviso amarillo en el panel del overlay durante 6 s, la línea WRN dice `VETOED`,
   el anfitrión no ve ningún 2283 y el invitado sigue jugando.
4. Anota qué hace la UI del mapa: si se cierra, si se queda abierta, si sale algún mensaje.

### 8.3 Criterios de aborto

| Señal | Acción |
|---|---|
| El invitado sale al título o aparece el diálogo «error crítico» (2679 con `0x8E19630A`) | invitado a `guest_fast_travel_veto=1`; anfitrión sin cambios; enviar logs |
| La carga del invitado pasa de 60 s, o `observer load defer … STUCK` > 0 | igual; el invitado cierra el juego (sin «Continuar») |
| Crash del invitado o del anfitrión, `GuestS3 … dumps > 0` o `FreezeWatch` fuera del apagado | parar la sesión; las dos claves a `0`/`1` (F2 + veto) |
| ERR `User2Funnels fast travel … a 2283 of user 2 reached the original vf48 …` | el destino cayó en el anfitrión: hace un viaje rápido propio o reinicia; `user2_fast_travel=0`; enviar logs |
| WRN `… user 2's manager NOT readable: a block …` y ERR `… 0x2BC4950's vf48 … the ORIGINAL …` | el invitado viajó, pero el `+0x6A0` del gestor del anfitrión se puso a 0: anotarlo y seguir; si se repite, `user2_fast_travel=0` |
| El Kliff del anfitrión aparece en el destino del invitado, o se baja del caballo solo | parar; `user2_fast_travel=0`; enviar logs |
| `User2Funnels: user2_fast_travel 1 NOT live` o cualquier `REFUSED` de F1/F3 | no pasa nada grave (F2 descarta); seguir con veto=1 en el invitado |

## 9. Riesgos

- **[H] Recarga del cliente del invitado dentro del campo compartido.** Es la incógnita principal (§2.4): estado del
  cliente (gestores X, copias de secuenciadores, NPC ya anunciados, cámara) al recargar sin cambiar de campo con el
  dueño reescrito. Si falla, el resultado esperado es el de la sesión 14 (logout) o una carga atascada; las guardas
  `STUCK` y `X GONE` ya existen.
- **[E] `a = 0` siempre.** Si algún otro remitente del 2283 pusiera `a ≠ 0`, el paso de misión del servidor iría al
  usuario 2 solo con `quest_per_user=1`. Con 0 iría al anfitrión. Los inis de la sesión 15 llevan 1.
- **[H] `vf170` del componente `+0x1C0` del usuario.** Puede ser un guardado. El guardián del anfitrión rechaza el del
  usuario 2.
- **[E] Montura/compañero (`0x2B97A20`) y RideOff:** se aplican al usuario 2. Su caballo debería viajar con él, como en
  un jugador. Si `horse_follow` o `attach_follow` del mod no lo esperan, se verá en la prueba.
- **Carrera F2 → F3:** F2 comprueba el sujeto en el hilo de red. Si las claves cambian antes de que el servidor ejecute
  el manejador (un relogin del usuario 2 en esos milisegundos), el sujeto no lo reconoce. Entonces el 2283 llega a la
  vf48 original y el destino cae en el anfitrión (§6), con ERR en el log.
  - Que el gestor del usuario 2 no se pueda leer ya **no** lleva al anfitrión: F3 usa el bloque. Solo afecta al
    `+0x6A0` de la segunda vf48, también con ERR.
- **F1:** el gancho de entrada de `0x655110` corre en el hilo de la UI del juego. Solo usa atómicos, la cola sin locks
  y dos `store`. Fuera del modo remoto siempre es nativo.

## 10. Tests

`tests/optd_fast_travel_tests.cpp` (ctest `optd_fast_travel`):
- los modos;
- el payload real de la sesión 14;
- el filtro de `observer_seq_copy` ignora el 2283 aunque su u64 sea un id del libro;
- dónde está el usuario 2;
- la puerta F2: descarta con F3 a 0 o probe, sin F3 vivo o sin sujeto reconocido; deja pasar en su propio campo o con
  F3 vivo y el sujeto reconocido;
- la decisión de F3: dirección de retorno, sujeto, probe/0 (original) y, con 1, nunca la original (gestor real o
  bloque);
- la segunda vf48 (`decide_apply`): el gestor real si F3 lo armó; la original tras el bloque; nada fuera de
  `0x2BC4C53` o sin armar;
- la cadena del gestor sobre memoria falsa: vtable, puntero de vuelta y cada eslabón;
- la decisión de F1: local siempre nativo.

`optd_user2_funnels` comprueba además el nuevo llamador permitido `0x2896347`.

## 11. Contraste con el informe externo y prueba en un PC

Informe independiente del 11-10 (`<codex-workspace>\teleport-s14-20261011-work\INFORME.txt`,
§5 E–H, §8, §10, y `review\peer-plan.txt`). Analizó las fuentes de S14 (antes de 71fee89). Todo lo de aquí está
comprobado de nuevo con capstone sobre el exe de la build 2976.

### 11.1 Qué coincide y qué es nuevo

| Punto | Nosotros (§2–§6) | Informe | Estado |
|---|---|---|---|
| Causa: el 2283 usa la vf48 del dueño, el 2437 consume el destino del remitente | F3 | igual | coincide |
| Segunda vf48 `0x2BC4C53` dentro de `0x2BC4950` y `[gestor+0x6A0]=0` | revisión 1 (71fee89) | igual (su lectura de la versión concurrente no la veía aún) | coincide |
| vf40 `0x2896347` (WakeSpot) | en `vf40_caller_allowed` con `quest_per_user=1` | igual | coincide |
| 2679 = `eErrNoInvalidFieldInfoKey` (`0x8E19630A`) por `TS+0x550` vacío en `0x2BC9256` | sí | igual, con el hash reproducido | coincide |
| **Respuesta al 2437 del usuario 2** | decíamos «2174 vía `0x2BC96C0`/`0x299B520`» | **2643 + 3066** | **nos equivocábamos**: corregido en §2.4 |
| **El 2643 lleva el dueño del campo** | no lo sabíamos | u32 `+9` → `P+0x12C` del cliente | **nuevo: alias en la copia del usuario 2** (§11.3) |
| Sondas TravelBegin…TravelAck | no había | contrato propuesto | **hechas**, sin ganchos nuevos (§11.3) |
| Si una guarda falla, no delegar en silencio al gestor del anfitrión | la 1.ª vf48 nunca devuelve el del dueño con `1` (bloque); la 2.ª, tras el bloque, sí llama a la original (ERR en el log) | — | diferencia residual, ruidosa (ERR), solo si el gestor del usuario 2 no se puede leer |

[V] Los prefijos de §6 del informe, otra vez con capstone (todos en `.rsrc`, dentro de `[0x1000,0x51EB000)`):
`0x2A5F580 48 89 5C 24 08`, `0x2896120 48 89 5C 24 10`, `0x2BC4950 48 8B C4 48 89 58 10`, `0x29C59D0 48 89 5C 24 08`,
`0x2BC9200 48 89 5C 24 10`, `0x2774B20 48 89 4C 24 08`, `0x2B00CE0 40 53 48 83 EC 40`. No hizo falta ninguno nuevo: la
sonda reutiliza ExecuteTroc y la vf48 de `user2_funnels`.

### 11.2 Los Acks de la recarga y el dueño [V estático]

`0x29C59D0` (2437) → `0x2BC9200` → con `TS+0x569 = 1` → `0x2BCA3D0` (termina en `0x2BCA9B9`). Dentro:

| Ack | Escritor (llamada) | Payload | ¿Lleva el dueño? | Qué hace el mod |
|---|---|---|---|---|
| **2643 LoadingFieldDataAck** | `0x299B520` (`0x2BCA79E`), `mov eax,0xA53` | u8 `@0` (`4` en este camino, `0x299B689`), u32 actor `@1`, u32 FieldInfoKey `@5`, **u32 dueño `@9`** (= r9 = `[campo+0x180]`, `0x2BCA72B..0x2BCA78F`), u8 `@13`, u8 `@14`, u32 `@15`, u8 append `@19`, registros de 48 B desde `+20` (u32, u32, float3, float4, float3: `0x299B810..0x299B8E2`) | **sí** | alias en la copia del usuario 2 (nuevo) |
| **3066 MoveFieldCompletePostAck** | `0x299A610` (`0x2BCA86C`), `mov word [r14],0xBFA` | u32 `[personaje+0x60]` (la clave de actor **del viajero**, `0x299A6F9`/`0x299A807`) + u8 + u8 | no | nada |
| 2174 MoveFieldAck | `0x29997F0` (`0x87E`) | — | sí | **no lo manda `0x2BCA3D0`** |

- La tabla generada `owner_fanout_writers.h` da los mismos escritores (2643 = `0x299B520`, 3066 = `0x299A610`,
  2174 = `0x29997F0`).
- `0x2BCA3D0` tiene otra variante a partir de `0x2BCA9C0` con los mismos dos escritores (`0x2BCAD65`, `0x2BCADA6`): el
  alias va por opcode, así que la cubre igual.
- **2873** no es un Ack: es una Req del cliente (fase D, si `P+0x163 ≠ 0`; informe §10.1). El servidor no escribe un
  dueño ahí.
- 2445/2762 (fase 0x17) ya existían en la entrada y no se tocan. Para cualquier otro paquete del viaje que lleve la
  clave del anfitrión, el resumen de `Session2` ya tiene el contador `to user 2 with the host's key: OPxN`.
- Consecuencias en el mod: no hay 2174 ni 2549, así que `observer_seq_copy`, `observer_milestone` y `load_defer` no ven
  un viaje nuevo; la fase de movimiento queda en `MOVE_ASKED` hasta el 2445 (→ `LOADING_SENT`, retiene un instante) y
  el 2762 (→ `STABLE`). `observer_milestone` cuenta el 3066 suelto como «no sigue a un 2144» (solo diagnóstico). Si el
  usuario 2 se va en `MOVE_ASKED`, la secuencia de cierre espera 3 s a un 2174 que no llegará y manda el 2001 (como
  hoy).

### 11.3 Qué se ha implementado

**Alias del dueño en el 2643** (`server.cpp take_frame`, lógica pura `travel_probe_logic.h`):
- `[session2] user2_loading_owner_alias = 1` (por defecto) | `0`. Solo anfitrión.
- Solo la copia del usuario 2, solo el u32 `+9`, solo si `size >= 20 && (size-20) % 48 == 0`, solo si `+9` es una
  clave de usuario (`0x9…`) distinta de la del usuario 2. Igual que el 2174, pero con su propia validación (no la
  17 + 48n del 2174).
- El campo del servidor (`+0x180`), la copia del anfitrión y los módulos observadores conservan el dueño real (la
  sonda recibe el original).
- Líneas: `Session2: user 2's LoadingFieldDataAck 2643 names the field owner 0x… (the host); user 2's copy names user 2
  (0x…)` (las 8 primeras); WRN si el formato no es el conocido (las 4 primeras, sin escribir nada). En el resumen de
  `Session2`: `LoadingFieldDataAck 2643 owner -> user 2: N (unknown layout M, left as written K)`.
- La reescritura del 2174 no cambia de comportamiento: ahora usa la misma función (`tpl::alias_owner`), siempre
  activa.

**Sonda `[session2] user2_travel_probe = 0 | 1`** (`travel_probe.{h,cpp}`). Solo anfitrión, solo lectura, sin ganchos
nuevos; las líneas del servidor necesitan `user2_fast_travel=1` (instala el sujeto y la vf48). Los hilos del juego solo
empujan eventos de tamaño fijo a una cola sin locks; su propio hilo los escribe. Límites: 16 TravelBegin, 32 por tipo,
24 Acks por ventana de 20 s, 8 2679/Nak.

| Línea | Dónde | Qué dice |
|---|---|---|
| `TravelBegin` | hilo de red, al decidir F2 | WakeSpot (u32 a), FieldInfoKey, selector de actor, posición, rotación, escala, flag; usuario/personaje; dónde está; desenlace de F2/F3 |
| `TravelContext trip N` | vf48 `0x2A5F7FC` y `0x2BC4C53` | dirección de retorno, el gestor del dueño (leído por la cadena del Kliff del anfitrión, nunca llamado) y su `+0x18`, el devuelto y su `+0x18`; `user 2's own (ok)` o WRN `NOT user 2's own` |
| `TravelPrepared trip N` | ExecuteTroc del 2283 (antes/después) | `TS_user2` y `TS_host` `+0x550/+0x568/+0x569/+0x591`, `+0x6A0` del gestor del anfitrión, setter `0x2BC4950` visto (con F3), resultado de ExecuteTroc → `PREPARED` / WRN `NOT prepared` / **ERR `HOST TOUCHED`** |
| `TravelConsume trip N` | ExecuteTroc del 2437 | `TS_user2` antes/después, rama de `0x2BC9200` (`+0x568`/`+0x569`), resultado → `destination pending` / WRN `EMPTY` |
| `TravelAck trip N … #k +ms` | `take_frame`, cola del usuario 2 | opcode, tamaño, dueño escrito / dueño en la copia (2174/2643), campos del 2643; 2679 con actor, motivo y error |
| `TravelAck trip N after user 2's 2437: …` | al cerrar la ventana | clase de respuesta: `reload in the same field (2643/3066)`, `field change (2174)`, `REFUSED (2679)`, `Nak 1013`, `no answer yet` |

Límite conocido: el resultado interno de `0x2896120` (su `res`) no se lee; se deduce de «setter visto», `TS_user2`
después y un Nak 1013 en `TravelAck`. Si hiciera falta, el siguiente paso es una sonda de entrada en `0x2896120`
(prefijo verificado arriba).

Tests (`optd_fast_travel`): el alias del 2643 (solo `+9`, el resto de bytes igual; apagado; formato malo: 19, 67, 65,
21 B; sin clave; ya del usuario 2; no es un usuario; 3066 nunca), el del 2174 con su formato, el 2679 de S14, el
empaquetado de TS, los veredictos Prepared/Consume/Context, la clase de respuesta y la ventana. `optd` reproduce el
guion de §11.4 contra un servidor falso (orden, payload exacto de 53 B, 5 s tras el segundo 2762 y 1953 ms entre 2283
y 2437).

### 11.4 Prueba en un PC (anfitrión + `cdmp_peer`)

Guion: `docs/research/data/login_e3lc00_travel_s14.troc` = `login_e3lc00_112627.troc` sin cambios + el sufijo B del
informe tras su **segundo** 2762: `expect_learned USER_KEY/CHARACTER_NO/PLAYER_KEY`, `wait 5000`, el 2283 de S14
(53 B exactos), `wait 1953`, `send 2437 0 -`, `wait 2000`, `end`. No manda 2549/2445 a ciegas. Es a mano:
`check_troc_scripts.py` no lo compara; `optd` lo analiza y lo reproduce.

El destino (−10626, 621, −3813) está **en Hernand** (FieldInfoKey 1): el anfitrión tiene que estar en Hernand.

Preparación:
1. `cdmp.ini` = `docs/research/ini/cdmp.p8-noche-local.ini` (UDP a `127.0.0.1:37015`, `user2_fast_travel=1`,
   `user2_loading_owner_alias=1`, `user2_travel_probe=1`).
2. Arranca el juego, carga la partida y entra en **Hernand**. Quédate quieto, sin viajar, sin guardar, sin morir.
3. En `cdmp.log`, antes de lanzar el peer:
   - `Travel (host): user2_loading_owner_alias=1 … | user2_travel_probe=1: …`;
   - `User2Funnels: user2_fast_travel 1 LIVE: …` y ningún `REFUSED`;
   - el slot: la línea periódica `Session2: save guard …` contiene `| host save slot N`. Ese `N` es `SAVE_SLOT`. Si es 3
     (CrossSave) o 206, usa una ranura normal (el anfitrión rechaza esas dos para el usuario 2).
4. Desde la raíz del repo:

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_travel_s14.troc --var ACCOUNT=1 --var SAVE_SLOT=N --record D:\cdmp-grabaciones\viaje-s14-1.bin --start-after 3000 --linger 30000 --seconds 240
```

5. Espera a `script done` y a que el peer cierre (30 s de linger). Sigue quieto hasta que el log diga que la sesión 2
   se ha cerrado (C4). Luego haz **tú** un viaje rápido y comprueba que llegas a tu destino.

**Éxito** (en este orden, en `cdmp.log`):
- `TravelBegin …: user 2's ReloadStageStartReq 2283 (user 0x9…, character 0xA…) | WakeSpot u32 0 | FieldInfoKey 0x1 |
  actor selector none | position (-10626.434, 621.213, -3813.261) … | in the host's field (shared) … | F2/F3: to the
  server (F3 redirects its vf48) (handed to the server)`.
- `TravelContext trip 1 …: vf48 returning to exe+0x2A5F7FC | … | returned 0x… (its +0x18 <personaje del usuario 2>) |
  first (the 2283 handler) -> user 2's own (ok)` y otra igual con `exe+0x2BC4C53 … second (0x2BC4950)`. El gestor del
  dueño tiene `+0x18` = el Kliff del anfitrión.
- `TravelPrepared trip 1 …: TS_user2 before [+0x550 0x0 … +0x591 0] after [+0x550 0x… (≠ 0) … +0x569 1 +0x591 1] |
  TS_host before [X] after [X] (iguales) | host manager +0x6A0 a -> a | setter 0x2BC4950 seen yes | F3 real manager … ->
  PREPARED: user 2's TS set, the host's untouched`.
- `TravelConsume trip 1 …: user 2's 2437 … | 0x2BC9200: destination pending (+0x569 set: 0x2BCA3D0 …)`.
- `Session2: user 2's LoadingFieldDataAck 2643 names the field owner 0x9010… (the host); user 2's copy names user 2`.
- `TravelAck trip 1 … 2643 LoadingFieldDataAck (20+48n bytes) | owner as written 0x9010… / user 2's copy 0x9… (user 2's
  copy names user 2) | … FieldInfoKey 0x1 …` y `… 3066 MoveFieldCompletePostAck (6 bytes)`.
- A los ~20 s: `TravelAck trip 1 after user 2's 2437: reload in the same field (2643/3066) | 2643 1 3066 1 2174 0 … 2679
  0 Nak 0`.
- **Ningún 2679**, ningún `Nak 1013`, ningún ERR (`HOST TOUCHED`, `reached the original vf48`), y tu Kliff no se ha
  movido ni bajado del caballo. En `viaje-s14-1.bin` están el 2643 (con el dueño = usuario 2) y el 3066.

**Fallo** (para y guarda `cdmp.log`, `cdmp_packets_*` y el `.bin`):

| Señal | Qué significa |
|---|---|
| El peer no llega al segundo 2762 (`FAILED` antes) | el login no terminó: la prueba no ha empezado |
| `TravelBegin … DROPPED (F2)` | F3 no está vivo o el sujeto no reconoce al usuario 2 (`User2Funnels … NOT live` / `REFUSED`) |
| `TravelPrepared … NOT prepared` + `TravelConsume … EMPTY` + `TravelAck … 2679 … error 0x8E19630A` | se reproduce S14 aunque F3 redirigió: `0x2896120` sale antes del setter. Siguiente paso: sonda en `0x2896120` |
| `TravelAck … Nak` tras el 2283 | `0x2896120` devolvió un error con Nak; anota el código |
| **ERR `TravelPrepared … HOST TOUCHED`** o `TravelContext … NOT user 2's own` | **abortar**: el destino o el `+0x6A0` cayeron en el anfitrión; haz un viaje rápido propio o reinicia el juego |
| `TravelAck … field change (2174)` | el servidor eligió la rama `+0x568` (cambio de campo): anótalo, no es el caso de Hernand |
| `no answer yet` | no hubo respuesta: mira la fase en la línea `Session2` y el `.bin` |

Lo que esta prueba **no** valida: el peer no carga nada. No prueba el cliente del invitado (fundido, carga, actor
controlado, NPC, gestores X/M). Eso sigue siendo la prueba con Shadow (§8.2).

## 12. El 2322 del usuario 2 tras una recarga: `observer_milestone_reload` (S16, 11-10)

Causa raíz en `p8-s15-viaje-tunel.md` §0–§2: tras la recarga en el mismo campo, el cliente del invitado manda
`PreLoadSequencerReq` 2873 (fase 0x0D) y espera en el estado 0x11 un 2322 con **su** personaje (`M+0x231` 1 → 2 en
`0xB87A67`). En el servidor, el 2873 pasa por el embudo de dueño único (vf48 con retorno `.xtls 0x104D801E`) y
`0x2895EF0 → 0x287EF60` vuelve a preparar los secuenciadores **del anfitrión** y le manda **a él** el 2322
(`0x287F1D4`). Build 1.0.0.2976, capstone sobre el PE de solo lectura; no se abrió el juego.

Código:
- `src/mod/session2/observer_milestone_reload_logic.h`: lógica pura (`omr::Ledger`; tests en
  `tests/optd_observer_milestone_tests.cpp`).
- `observer_milestone.{h,cpp}`: el estado, las líneas y la emisión (reutiliza `read_facts`, `call_native` y el anillo de
  `observer_milestone`).
- `user2_funnels.cpp` (`troc_detour`): el 2437 antes de ejecutarse, el 2283 aceptado por F3 y la salida de cada Req
  del usuario 2.
- `session2.cpp` (`offer`): el 2873 en el hilo de red. `server.cpp` (`take_frame`): 2174/2643/3066/2322/2762/2679.
- `travel_probe.{h,cpp}` y `travel_probe_logic.h` (`HostBurst`): el efecto en el anfitrión en `TravelAck`.
- `fast_travel.{h,cpp}`: `fast_travel_user2_where()`, la lectura de F2 de dónde está el usuario 2.

### 12.1 Elección: variante B (A no está limpia)

**A** (la nativa): en el desvío de vf48, con retorno `exe+0x104D801E` y el sujeto = el usuario 2, devolver su
`ServerSequencerStageManager`, como F3. Lo que haría el código nativo con ese gestor [V capstone]:

| Paso | Qué hace | Con A |
|---|---|---|
| `0x2895EF0` | `0x1F6C540(gestor)`: bloquea el campo de `[gestor+0x18]` (`0x17FA6C0`) | campo del usuario 2 = el compartido |
| `0x2895F5F` | `0x206AE30([rem+0xA0])`: bloquea el **usuario del remitente** | usuario 2 (hoy también) |
| `0x2896035` | `0x2BD1920(TS del remitente, campo, rem, usuario, 1)` → `.xtls 0x1071C6E0`: `campo->vf60`, recorre la lista global `[[g+8]+0x388]` y llama a `0x2BD1D00` | del remitente (hoy también) |
| `0x2896049` | `0x287EF60(gestor, campo, r8 = [gestor+0x18], r9 = usuario del remitente)` | usuario 2 / su personaje |
| `0x287EF8D` | `[gestor+0x689] = 1` … `0` al final; `0x2880BC0`, `0x287FDD0(gestor)` | su gestor |
| si `0x287FDD0` = 1 | `[gestor+0x68A] = 0`; `0x287F450`, `0x2896C60`, `0x287F7D0`, `0x287FA30`, **`0x288B7A0`** (la función de la puerta de dueño `0x288B87A`, `shop_probe`); temporizador 0x7FB (`0x2774430`, `[gestor+0x18]`); **2322** a r8 (`0x287F1D4`); **`0x28431A0([[[pj+0x68]+0x188]+0x18])`** | la primera vez que corre sobre el gestor de un no dueño [H] |
| si no | `[gestor+0x68A] = 1` y temporizador 0xA | — |
| dentro de `0x28431A0` (profundidad 2–3) | **vf48 del campo** en `0x2849490` (retorno `0x2849493`, en `0x2849400`, tras `0x17FA6C0`; lee `[gestor+0x148]`) y en `0x284A08E` (retorno `0x284A091`, en `0x2849FC0` desde `0x2844000`) | **devuelven el gestor del ANFITRIÓN** mientras se trabaja con el componente `+0x188` del personaje del usuario 2: mezcla |

Además, la medida de S15 da **vf48 ×143 y vf40 ×13 004** bajo ese 2873 (`p8-s15-viaje-tunel.md` §5); A dejaría todas
salvo la primera con el gestor del dueño. Conclusión: **A no está limpia** (mezcla de gestores y código de dueño sobre
un no dueño). Se implementa **B**, sin variante A.

**B**: durante un viaje de recarga, el 2873 del usuario 2 se **descarta** en `offer` (como F2 con el 2283) y el 2322
para su personaje se emite **una vez** en el hilo del servidor, a la salida de su siguiente `ExecuteTroc`, con el emisor
ya verificado de `observer_milestone` (`ToTarget 0x2A757A0(finder, personaje)` + `0x2999D80(personaje, finder)`). Lo
que se pierde frente al nativo es solo `0x2BD1920` del remitente (riesgo §12.4).

### 12.2 Funcionamiento

`[session2] observer_milestone_reload = 0 | probe | 1`, solo anfitrión. Necesita `observer_milestone=1` (su emisor
verificado y su hilo de líneas; con `_send=1` para el modo 1) y `user2_fast_travel=1` (F3 y el sujeto de ExecuteTroc).
Sin `observer_milestone` se queda en 0 (WRN); en modo 1 sin `_send=1` no descarta nada (puerta `emitter off`).

Viaje de recarga `R<n>` (`omr::Ledger`):
1. **Inicio:** el `ExecuteTroc` del 2283 del usuario 2 vuelve con F3 armado (`ft_mgr` o bloque) → fase `Prepared`.
2. **2437:** antes de ejecutarse (su 2643/3066 salen dentro) → `Asked`.
3. **`take_frame`** (solo la sesión 2 viva): 2643 o 3066 → **`Reload`**; 2174 → `FieldChange` (de
   `observer_milestone`, nunca nuestro); 2679 → `Refused`; un 2322 nativo con el personaje del usuario 2 tras el 2437
   → no se emite el nuestro; 2762 → fin normal (línea).
4. **2873 en `offer`** (hilo de red; se juzga en cada oferta y se registra una vez):

   | Situación | Puerta | Efecto |
   |---|---|---|
   | sin viaje / viaje sin 2643-3066 / 2174 / 2679 | `no trip` / `not a reload` | pasa (nativo) |
   | usuario 2 en un campo propio / desconocido / de otro | `own field` / `not shared` | pasa |
   | modo 1 sin emisor SEND verificado | `emitter off` | pasa, **no** se descarta |
   | probe | `WOULD DROP` | pasa; se arma la decisión en seco |
   | modo 1 | **`DROPPED`** | no llega al servidor; se arma la emisión (el primer 2873 del viaje) |

5. **Salida de un `ExecuteTroc` del usuario 2** (el más externo del hilo; prueba atómica si no hay nada armado), al menos
   **300 ms** después del descarte (nativo: +460–501 ms). Puertas, con los mismos `read_facts` que la entrada:
   - finales: emisor con fallo previo, nueva época, logout, 2322 nativo ya enviado, 15 s sin Req apta, personaje no es el
     del usuario 2 / no es el principal, sesión ≠ la sesión 2 viva, su campo es suyo, el dueño no es el anfitrión, el
     campo no es el actual del anfitrión;
   - transitorias (se reintenta en la siguiente Req; cuenta como mucho un intento cada 50 ms, así que manda el límite
     de 15 s, §12.8): ilegible, dentro de `take_frame`, hilo sin reino de
     servidor (TLS `+0x1EC`), sin contexto de envío (`+0x250`), *scope sender* abierto (`+0x2AC/+0x2A0`).
6. **Emisión:** `call_native` (constructor + emisor, mismas comprobaciones de `finder`: vtable, sesión = la viva) con
   la marca `t_r_emitting`: el 2322 que pasa por `take_frame` en ese hilo es «nuestro» y **no** entra en el libro de
   viajes de `observer_milestone`. Una sola vez por viaje.

Líneas (`ObserverMilestone [reload] …`, formateadas por el hilo de `observer_stage`):
- `trip Rn begins …`; `trip Rn: RELOAD (2643/3066, no 2174) after 2643 to user 2 (+x s after its 2437)`;
- `user 2's PreLoadSequencerReq 2873 #1 (…; user 2 in the host's field (shared)) -> DROPPED … | NOT handed to the server`;
- `trip Rn: EMIT NOW …` / `WOULD EMIT …` / `NO 2322 FROM US: <motivo>`, con `+x s after the 2873`, actor, dueño, sesiones
  y estado del hilo;
- `trip Rn: 2322 EMITTED to user 2 after its PreLoadSequencerReq 2873: IS in user 2's queue (x1)`;
- `trip Rn: LoadingCompleteAck 2762 to user 2 …` y `trip Rn ends: <resultado> | 2873 seen/dropped | 2437 -> 2873 | 2873
  -> our 2322 | our 2322 -> 2762 | native 2322 …`;
- en el resumen de 60 s de `ObserverMilestone`: `|| reload (…): trips … | 2873 of user 2 …: DROPPED …`.

### 12.3 `TravelAck`: el 2322 y el efecto en el anfitrión

- La línea final de la ventana (`TravelAck trip N after user 2's 2437: …`) añade `2322 to user 2 N` (cualquier 2322 que
  vaya a la sesión 2 viva en la ventana de 20 s: el nuestro o uno nativo).
- `host side effect:` con el sniffer (`[sniffer] enabled=1`; si no, WRN y sin cuenta): tras el primer 2873 del usuario
  2 de la ventana, durante 3 s, los 2661/2981/2322 que recibe el **cliente del anfitrión**:
  - `SIDE EFFECT: the host's client re-prepared its sequencers for user 2's 2873` (WRN) si el 2873 fue al servidor y
    llegó un 2661 o un 2322 (lo de S15: 21 + 21 + 1);
  - `clean` si no hubo ráfaga (lo esperado con el modo 1: `was DROPPED`);
  - `ambiguous` si el propio cliente del anfitrión mandó un 2873 entre 1 s antes y 3 s después (su propia recarga).

### 12.4 Riesgos

- **[H] `0x2BD1920` del remitente no se ejecuta** (se descarta el 2873 entero). Es el único paso del manejador que
  depende del remitente (su TS, su usuario): `campo->vf60` y un recorrido por la lista global `[[g+8]+0x388]` →
  `0x2BD1D00` (cuerpo en `.xtls`, ofuscado). Qué hace es desconocido. Si el invitado muestra algo raro tras la recarga
  (estado de campo que no se refresca), es el primer sospechoso; la alternativa sería dejar pasar el 2873 y emitir
  igual (R1 de `p8-s15-viaje-tunel.md` §6), con el efecto en el anfitrión.
- **[H] El contexto de la salida de `ExecuteTroc`.** Es el mismo hilo que ejecuta el manejador del 2873 (donde sale el
  2322 nativo), pero después de volver. Si allí hubiera siempre un *scope sender* abierto o faltara el contexto de envío,
  la decisión acaba en `NO 2322 FROM US: …` a los 15 s y el invitado sigue como en S15: nada peor. Con
  `probe` se ve en las líneas `not now (…)`.
- **[E] Un 2322 antes que el nativo** (≥ 300 ms frente a 460–501 ms): el manejador `0xB87890` escribe `+0x231 = 2` sin
  condición (`0xB87A67`, tras `0x8A67E0`/`0x17FA6C0` y la vf48 del cliente), y entre la fase 0x0D y el estado 0x11 nadie
  lo vuelve a poner a 0 (`0x77AA8B` está en la fase 7, `0x77AA40`; el 1 en `0x77B0CE`, estado 9).
- **[E fuerte] Después viene el estado 0x12** (`p8-s15-viaje-tunel.md` §3): lo trabaja otro agente en el lado del
  invitado; este cambio no toca módulos del invitado.
- **Del anfitrión desaparece** la ráfaga 2661/2981/2322 y los 2547 posteriores (S15 §5). `observer_seq_copy` deja de
  copiar esos 41 2661 al usuario 2 (no hay ráfaga que copiar).
- **Carrera:** si el viaje cambia (2174, 2679, época) entre el juicio y el registro del 2873, el registro usa la puerta
  ya decidida; como mucho se descarta un 2873 de un viaje que acaba de terminar, sin emitir nada (el armado exige
  `Reload` en el juicio y la emisión vuelve a comprobarlo todo).

### 12.5 Inis

| Fichero | Clave | Valor |
|---|---|---|
| `cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini` | `observer_milestone_reload` | **`1`** |

Por qué 1 y no probe: el caso que corrige (el 2873 de una recarga en el campo compartido) hoy ya deja al invitado
colgado y toca al anfitrión; con 1, en el peor caso (emisión rechazada) el invitado queda igual que en S15 y el
anfitrión deja de recibir la ráfaga. La emisión es la misma llamada verificada que `observer_milestone` ya hace en cada
entrada. Antes de Shadow se valida en un PC (§12.6). Para volver al comportamiento de S15: `observer_milestone_reload=0`.

### 12.6 Prueba en un PC (anfitrión + `cdmp_peer`)

Guion: `docs/research/data/login_e3lc00_travel_s14_b.troc` = el de §11.4 + una cola:
- tras el 2437: `expect 2643 10000`, `expect 3066 10000`, `wait 2300` (el hueco 2437 → 2873 del invitado en S15:
  t=176,060 → 178,369);
- `send S15_PreLoadSequencerReq 2873 0 -` (el único 2873 de la captura del invitado `cdmp_packets_20261011-103850.bin`:
  t=178,369, slot 0, **0 bytes**; el manejador rechaza cualquier carga);
- `wait 400` y `send S16_RegionInOutReq_carrier 3067 0 0000000001000100` (solo hace de **portador**: el peer no manda
  nada más y la emisión ocurre a la salida de la siguiente Req del usuario 2; el invitado real manda cientos de 3067
  durante la carga. Actor 0: nadie);
- `expect 2322 5000`, `wait 2000`, `end`. Sin el 2322 el peer termina en `FAILED … no Ack 2322` (lo esperado con 0 o
  probe).

Preparación: igual que §11.4 (`cdmp.ini` = `cdmp.p8-noche-local.ini`, el anfitrión en **Hernand**, quieto; comprobar
además `ObserverMilestone reload: observer_milestone_reload=1 (drop the 2873, emit the 2322) …` y
`ObserverMilestone ON (SEND) …` en `cdmp.log`). Desde la raíz del repo:

```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00_travel_s14_b.troc --var ACCOUNT=1 --var SAVE_SLOT=N --record D:\cdmp-grabaciones\viaje-s16-1.bin --start-after 3000 --linger 30000 --seconds 240
```

**Éxito** (en `cdmp.log`, en este orden), además de lo de §11.4:
- `ObserverMilestone [reload] … trip R1 begins: user 2's ReloadStageStartReq 2283 was accepted by F3 …`;
- `… trip R1: RELOAD (2643/3066, no 2174) after 2643 to user 2 …`;
- `… user 2's PreLoadSequencerReq 2873 #1 (RELOAD …; user 2 in the host's field (shared)) -> DROPPED … | NOT handed to
  the server`;
- `… trip R1: EMIT NOW: 0x2A757A0 + 0x2999D80 … | +0.4xx s after the 2873, attempt 1 …`, seguida de `OUR 2322 reached
  take_frame … -> queued` y `2322 EMITTED to user 2 after its PreLoadSequencerReq 2873: IS in user 2's queue (x1)`;
- `TravelAck trip 1 after user 2's 2437: reload in the same field (2643/3066) | … | 2322 to user 2 1 | host side effect:
  user 2's 2873 was DROPPED (observer_milestone_reload): the host's client got 2661 x0 2981 x0 2322 x0 within 3 s ->
  clean …`;
- el peer: `script done`; en `viaje-s16-1.bin`, un 2322 con el `PLAYER_KEY` del peer.

**Fallo:**

| Señal | Qué significa |
|---|---|
| `-> no reload trip of user 2` / `not a reload` | F3 no aceptó el 2283 o no llegó 2643/3066 (mira §11.4) |
| `-> mode 1 but observer_milestone's emitter is not verified/SEND` | falta `observer_milestone_send=1` o su verificación (`REFUSED` de `ObserverMilestone`) |
| `not now (a native scope sender is open …)` repetido y `NO 2322 FROM US: …` | la salida de `ExecuteTroc` no es un sitio apto (§12.4): anotar y pasar a `probe` |
| `NO 2322 FROM US: the Req's actor is not user 2's character` | el sujeto del 3067 no es el personaje del peer: anotar |
| `host side effect: … SIDE EFFECT` con el modo 1 | el 2873 pasó: mirar la línea de la puerta |
| ERR/crash | parar; `observer_milestone_reload=0` |

**Shadow (después):** el mismo viaje a Hernand. Éxito por etapas: en el invitado `M+0x231 1 -> 2` y `0x11 -> 0x12`
(después, el bloqueo de 0x12 es cosa de R2, en el invitado); en el anfitrión las líneas de arriba, sin `SIDE EFFECT` y
sin ráfaga de 2547.

### 12.7 Tests

`tests/optd_observer_milestone_tests.cpp` (ctest `optd_observer_milestone`), con la secuencia de S15:
- los modos (`0`, vacío, `probe`, `1`, desconocido) y que con 0 nada es relevante;
- recarga completa en modo 1: descarte, nada antes de 300 ms, una emisión, el 2322 propio no cuenta como nativo,
  resultado `Emitted`, un segundo 2873 tras la decisión **pasa** (`over`) sin rearmar, una sola decisión, el 2762
  cierra el viaje (`Loaded`) y la línea de fin;
- viaje terminado (§12.8): 2762 antes de cualquier 2873 → pasa; 2873 a más de 45 s del 2437 → pasa;
- lo que no es nuestro: sin viaje, 2174 tras el 2437 (y un 2643 posterior no lo convierte), sin respuesta aún, 2643 a
  otra sesión o sin 2437, 2679, campo propio / desconocido / de otro, modo 1 sin emisor (nunca se descarta);
- probe: `WOULD DROP`, se arma solo al registrarlo como entregado, decisión en seco sin llamada;
- puertas: transitorias con reintento (scope abierto, sin reino), una ráfaga de 500 Req en < 50 ms cuenta un intento,
  y con una Req cada 10 ms acaba en `Timeout` a los 15 s (no por el presupuesto de intentos); un 2174 / 2679 / 2762
  entre el descarte y la salida → `the trip changed`, sin emisión; cada puerta final; 2322
  nativo del usuario 2 (y uno del anfitrión que no cuenta); emisor con fallo previo; 15 s;
- cancelaciones (época, logout) y fallos de la llamada (perdido en la cola, no visto, destinatario equivocado), `No2873`
  y `Passed`.

`tests/optd_fast_travel_tests.cpp`: el 2322 en `ReplyCounts` y `HostBurst` con la ráfaga de S15 (21 + 21 + 1 →
`SIDE EFFECT`), descartado → `clean`, el 2873 propio del anfitrión justo antes → `ambiguous`, uno de 30 s antes no.

`tests/optd_tests.cpp` (`test_travel_script_b_flow`): el guion `_b` contra un servidor falso: orden de las 14 Req,
2873 vacío en el slot 0 a 2300 ms del 2437, el portador a 400 ms; termina con el 2322 y falla sin él.

### 12.8 Revisión independiente (S16, tras la prueba en un PC de 6f4d3f4)

6f4d3f4 se verificó en un PC (`2322 EMITTED to user 2`, `host side effect: clean`). La revisión encontró tres fallos de
la lógica; corregidos en `observer_milestone_reload_logic.h`:

1. **(media-alta) El viaje no se cerraba.** Tras la decisión y el 2762 el viaje seguía en `Reload` hasta otro 2283 con
   F3, un 2174, un 2679 o una época: `judge` seguía diciendo `Drop` y `record_offer` no rearmaba, así que cualquier 2873
   posterior del usuario 2 (una reaparición u otro camino sin 2283 nuevo ni 2174) se tragaba **sin 2322** → el
   invitado se quedaría en 0x11. Ahora:
   - el 2762 a la sesión 2 viva cierra el viaje (fase `Loaded`);
   - `judge` devuelve la puerta nueva **`over`** (pasa, nativo) si el viaje está decidido, cargado o el 2873 llega más de
     **45 s** (`kMaxAgeUs`) después de su 2437. Solo se descarta la primera ventana de 2873 del viaje, mientras la
     emisión está pendiente.
2. **(media) Sin recomprobar la fase al emitir.** Un 2174/2679 entre el descarte y la salida de la siguiente Req
   (≥ 300 ms) dejaba emitir un 2322 en mitad de una entrada (doble con `observer_milestone`). Ahora `at_exit` empieza
   comprobando la fase: si ya no es `Reload` (2174, 2679 o 2762), decisión final **`the trip changed after the drop`**,
   sin emisión.
3. **(media) El presupuesto de reintentos se gastaba en < 1 s.** Cada salida de Req contaba un intento y el invitado
   manda cientos de 3067 durante la carga. Ahora un rechazo transitorio cuenta como mucho **uno cada 50 ms**
   (`kAttemptGapUs`) y el tope sube a 400 (> 15 s / 50 ms): manda el límite de **15 s**.
   - **Reinyectar el 2873 descartado tras un rechazo final: no se hace.** Sería posible (guardar el paquete y que el
     hilo de red lo pase por `server_inject_ex`), pero no ayuda al invitado: el 2873 nativo sigue resolviendo el gestor
     del dueño, así que su 2322 iría otra vez al **anfitrión** (la causa raíz) junto con la ráfaga 2661/2981 en su
     cliente, y llegaría hasta 15 s tarde y desordenado respecto a las Req posteriores del usuario 2. Lo único que
     recuperaría es `0x2BD1920` del remitente (§12.4, [H]). Si una prueba muestra rechazos finales, la salida es
     `observer_milestone_reload=probe` o `0` (comportamiento de S15) mientras se estudia el sitio de emisión.

Líneas nuevas: la puerta `its reload trip is over (decided, loaded or > 45 s after its 2437): passes (native)` en
`ObserverMilestone [reload] … PreLoadSequencerReq 2873 …`, el motivo `the trip changed after the drop (2174 / 2679 /
2762)` y `over N` en el resumen de 60 s. Tests: §12.7.
