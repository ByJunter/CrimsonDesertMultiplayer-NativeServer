# P8 · S1 opción A, tramo de secuenciadores: diseño e implementación

10 de octubre de 2026, rama `opcion-d` (sobre `36b89cb`). Build del juego **1.0.0.2976**, direcciones en **RVA**
(base `0x140000000`). Aprobada por el usuario la «opción A» de `p8-s1-escenas-npc.md` §4 con las correcciones de
`p8-npc-tiendas-animales-escenas.md` §5.2. No se lanzó el juego ni se desplegó nada.

**[V]** = comprobado (captura, log, instrucciones con capstone sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, compilación, pruebas o reproducción de las capturas).
**[E]** = hipótesis pendiente de la prueba en el juego.

Claves nuevas en `[session2]`:

| Clave | PC | Valores | Qué hace |
|---|---|---|---|
| `observer_seq_copy` | anfitrión | `0` \| `probe` \| `1` | copia al usuario 2 los secuenciadores del Kliff del anfitrión (§3–§6) y filtra sus C>S (§7). `probe`: decide y escribe las líneas, no envía ni descarta nada |
| `observer_seq_copy_only` | anfitrión | ids hex de secuenciador y/o claves `B…` enlazadas; vacío = todos | solo esos secuenciadores |
| `observer_seq_copy_2322` | anfitrión | `1` (defecto) \| `0` | el 2322 del anfitrión una vez por viaje (§4.3) |
| `observer_seq_copy_2100` | anfitrión | `0` (defecto) \| `1` | descartar los 2100 del usuario 2 mientras observa (§7) |
| `guest_seq_route` | invitado | `0` \| `probe` \| `1` | los 2661/2981/2810/2322 que nombran a OTRO personaje van al gestor de ese personaje (X) (§8) |
| `guest_seq_watch` | invitado | claves hex | G5: estado de esos actores al cambiar (§8.4) |

## 0. Evidencia

| Ref. | Archivo |
|---|---|
| HC | `C:\Emulador CD\backups\p8-sesion7-host\cdmp_packets_20261010-060302.bin` (cliente propio del anfitrión) |
| GC | `C:\Emulador CD\backups\p8-sesion7-shadow\cdmp_packets_20261010-050511.bin` (cliente del invitado, viajes 1–2) |
| GL | `…\p8-sesion7-shadow\cdmp.1.log` (`GuestStage`, `GuestScene`) |

Reloj: `t(HC) = t(GC) + 143,2 s` (`p8-npc-tiendas-animales-escenas.md` §0). Los scripts y la herramienta de
reproducción (§10) están en el scratchpad de la sesión y no se conservan; el lector es el de
`observador/scripts/cap.py`.

## 1. Resumen

1. **Qué se copia [V por capturas]:** el **2661** de cada secuenciador del Kliff del anfitrión (33 en HC, **0** en GC);
   su **2981** solo si el usuario 2 no recibió ninguno de ese secuenciador en el viaje (los de los secuenciadores
   preparados con el invitado cerca llegan solos, byte a byte; los anteriores a su entrada no); el **2810** de uno ya
   copiado (11 en HC, **0** en GC); y, una vez por viaje, el **2322** del anfitrión (§4.3, [E]). **No** se copian
   2335/2264/2195/2165 (escenas de horario: §4.4) ni 2524/2111 (llegan casi todos solos: §2).
2. **Cuándo:** instantánea al entrar el usuario 2 en el campo del anfitrión (su `MoveFieldAck` 2174 con el dueño que
   escribió el servidor ≠ el usuario 2) y, durante la estancia, cada 2661 nuevo del anfitrión.
3. **Orden:** cada copia espera a que **todos** los actores de tipo 4/5 de su 2661 hayan salido hacia el usuario 2
   en un 2005 de este viaje (y no se hayan retirado con un 2516), y a que salga el 2322 propio del usuario 2 (S2), o
   30 s después de su 2144. Es una espera de varias claves propia (`osq::Copier`), no `observer_hold_acks` (§5).
4. **Bytes:** los del cliente del anfitrión tal cual (el sniffer, hilo escritor del mod), **sin reescribir nada**:
   el invitado los enruta por el actor del paquete (§6).
5. **Corrección al encargo [V]:** en el invitado el cliente **no** enruta al gestor X del Kliff del anfitrión sino al
   **M del propio usuario 2** (el 2174 reescrito lo hace dueño). Sesión 7, GL: `GuestStage [2981 route 0xC07749] …
   manager … of actor 0xA01002E9 … sequencer owner 0xA0100001 -> ANOTHER character's manager`. Por eso hace falta la
   mitad del invitado (paso A.4: `guest_seq_route`); sin ella las copias caerían en M, lo que §5.5 de
   `p8-s1-escenas-npc.md` prohíbe, y una **puerta de entrega** las descarta (§8.3).
6. **Reproducción de la sesión 7 [V]** con la lógica real (§10): con `observer_seq_copy_only=1AB,1AD,1A9,19E` saldrían
   `2981+2661 0x19E` + el 2322 del anfitrión al abrirse la puerta (t=198,35; 8,7 s tras el 2174), y `2661` de `0x1A9`,
   `0x1AB` y `0x1AD` 0,18–0,20 s después del 2661 del anfitrión, cada uno tras el 2005 de su tendero; y el 2810 de
   `0x19E` a t=452,48.
7. Build y pruebas: `.\scripts\build.ps1` sin avisos, **31/31** (nueva `optd_seq_copy`).

## 2. Lo que dicen las capturas [V]

### 2.1 Paquetes de secuenciador del anfitrión y si llegan al invitado

| Opcode | HC (cliente del anfitrión) | Llegan a GC (byte a byte) |
|---|---|---|
| 2661 `PrepareSequencerAck` | 33, todos a `A0100001`, dueño `A0100001` | **0** (GC solo tiene 1, el suyo `0x1A3`) |
| 2981 de secuenciadores preparados **antes** de la entrada (`0x13`…`0x1A2`, t ≤ 70) | 1 por secuenciador | **0** |
| 2981 de secuenciadores preparados **durante** la estancia (`0x1A4`…`0x1D6`) | 1 por secuenciador | **todos** (mismo instante: buscador cercano, no ToTarget) |
| 2981 periódicos de `0x164`–`0x167`, `0x1D5` | 12–50 | los de la estancia (34/47, …) |
| 2810 `DeleteSequencerAck` | 11 | **0** |
| 2524 / 2111 | 164 / 364 | casi todos (p. ej. 2111 de `0x1A9` 2/2); no los de t < 190 |
| 2322 | 2 (los suyos) | — (GC: 2, los del usuario 2) |

Conclusión: al usuario 2 le faltan **el 2661 siempre**, el **2981 de lo preparado antes de que llegara** y el
**2810**. 2524/2111 ya llegan; hoy caen en M y no encuentran el id (como en la sesión 5).

### 2.2 Orden 2005 → 2661 (por secuenciador, en HC y GC)

- En el anfitrión, el 2005 de cada NPC enlazado llega **justo antes** (−0,000 a −0,088 s) del par 2981/2661.
- En el invitado, el 2005 del mismo NPC llega **+0,18 s** después del 2661 del anfitrión en los tenderos de la
  estancia (`B0100C0F` Grover +0,184; `B0100C12` Dahlia +0,177; `B0100C09` posadero +0,200): es la espera de
  `observer_hold` (sesión 7: media 205 ms). Un 2661 copiado en el acto llegaría **antes** que el NPC y `0xA9CE81` lo
  descartaría. Por eso la copia espera a los 2005 (§5).
- El Kliff del anfitrión (`A0100001`, tipo 1) se anuncia al invitado una vez, en la ráfaga de entrada (GC t=54,331 →
  HC 197,53). Está en los enlaces de casi todos los secuenciadores (`0x20000002 -> A0100001`, tipo 5).
- Cuando un NPC enlazado sale de la vista del invitado (2516), el anfitrión borra su secuenciador **60–75 ms después**
  (2810 con u8 7): `0x19E` (2516 de `157/158` t=452,409, 2810 t=452,482), `0x1A2`, `0x73`, `0x147`, `0x14D`, `0x1CB`.

### 2.3 C>S del invitado (formatos)

| Opcode | Tamaño | Campo | Invitado (GC) |
|---|---|---|---|
| 2767 | 18 | u64 secuenciador @0 | 0 |
| 2591 | 17 | u64 @0 | 10, todos `0x1A3` (su escena ESC) |
| 2436 | 14 | u64 @0 | 1, `0x1A3` |
| 2173 | 15 | u64 @0 **o** `u32 1 | u32 actor` | 12: 10 de su personaje, 2 de `0x1A3` |
| 2547 | 4 | u32 actor (`B…`) | 1 |
| 2100 | 1549 | posiciones (sin id de escena) | **210**, todos con el invitado dentro del campo del anfitrión |

El manejador del servidor del 2100 (`0x2A5E9D0`) llama a `0x17FA6C0` y a `vf48` del campo [V instrucciones]: aplica
la posición al gestor del **dueño del campo** (el del anfitrión) [E fuerte].

## 3. El libro del anfitrión (fuente de los bytes)

- **Fuente:** el *tap* del sniffer (`net::sniffer_set_tap`), en el **hilo escritor** del sniffer (hilo del mod). Ahora
  es un reparto (`sniffer_fanout` en `session2.cpp`) para `host_scene_diag` y `observer_seq_copy`.
- **Por qué no el serializador nativo** (como `observer_scene_copy` con el 2810): el 2661 solo sale de `0x287C200` con
  argumentos que no existen fuera de esa preparación (`p8-observador-escenas.md` §5) y el 2981 solo desde `.xtls`. Para
  la instantánea hacen falta los **bytes** de algo emitido minutos antes, cuando no había sesión 2: solo los conserva
  el cliente propio del anfitrión. No se engancha la `Send` de la sesión 1 (D-7: la ruta caliente no pasa por el mod).
- **Qué guarda** (`osq::Ledger`): por secuenciador, el 2661 entero (si `dueño == actor`, `kind 4`), sus actores
  requeridos (enlaces de tipo 4/5), el **último 2981** (un 2981 anterior a su 2661, el orden nativo, se adjunta al
  llegar este); el 2810 lo borra; el 2174 del propio anfitrión vacía los vivos (campo nuevo); el último 2322 del
  anfitrión (su personaje). Cotas: 1024 vivos, 64 2981 en espera, 8192 ids conocidos.
- **Requisito:** `[sniffer] enabled=1` (sin él la clave avisa y queda apagada). Si el sniffer pierde paquetes
  (`# DROPPED`), el libro queda incompleto.

## 4. Qué opcodes y cuándo

### 4.1 Instantánea y en vivo

- **Inicio del viaje** (take_frame, tras el *push* del 2174 al usuario 2): observador si el dueño que escribió el
  servidor (`milestone_value`, antes de reescribirlo) es un usuario `0x9…` distinto del usuario 2. Todo secuenciador
  vivo del libro que pase `observer_seq_copy_only` queda **pendiente**. Viaje a su propio campo: nada.
- **En vivo:** cada 2661 que recibe el cliente del anfitrión durante un viaje observador queda pendiente.
- **2810:** si su copia ya salió en este viaje, se copia el 2810 del anfitrión (bytes tal cual, actor `A0100001`); si
  estaba pendiente, se cancela.

### 4.2 El 2981

Con cada 2661 se envía el último 2981 del libro **solo si** el último 2981 de ese secuenciador que salió hacia el
usuario 2 en este viaje es **otro estado** (hash FNV-1a del payload; take_frame los anota). Así: instantánea =
`2981 + 2661` (orden nativo); en vivo = solo `2661` (el 2981 ya le llegó por el buscador cercano, §2.1, con los mismos
bytes). Un 2981 posterior del anfitrión de un secuenciador ya copiado se copia solo si no le llegó al usuario 2 (otro
hash): take_frame ve el nativo antes que el sniffer (el servidor envía a la sesión 2 dentro de la misma emisión; el
cliente del anfitrión lo lee después). En la sesión 7 [V]: de los 136 2981 de secuenciadores del anfitrión durante la
estancia, 4 no llegaron al invitado (`0x164`–`0x167`, t≈195, durante su carga, antes de abrirse la puerta: los cubre
la instantánea); después de la puerta llegaron todos, así que la copia en vivo del 2981 es solo una red de seguridad
(la reproducción con la lógica nueva no copia ninguno mientras el invitado está dentro). No hay dos 2981 idénticos de
un mismo secuenciador en HC (0 repeticiones), así que el hash distingue estados.

### 4.3 El 2322 del anfitrión [E, con base estática]

- G3 (sesión 7): X tiene `+0x231 = 1` en el invitado. M del propio usuario 2 está en 3.
- `0xA97300` (actualización del gestor) [V instrucciones, E semántica]: con `+0x231` 1 o 2, un secuenciador cargado va
  a la lista `+0x1C0` (`0xA977B6`–`0xA977CF`, `call 0x40B9C0`); con 3 se procesan (`0xA97885`); con 2 y la lista vacía
  llama a `0x3D3080` (`0xA97488`). El único escritor de 2 es el manejador del 2322 (`0xB87A67`).
- Lectura: en X los secuenciadores copiados quedarían **precargados y sin reproducirse** mientras `+0x231 = 1`. En el
  dueño nativo, el 2322 llega tras su preparación y los «reproduce todos».
- Por eso, una vez por viaje observador, al abrirse la puerta y después de los pares listos, sale el **último 2322 del
  anfitrión** (`1209ff04 00 010010a0`, su personaje), que el invitado enruta a X (`guest_seq_route`, punto 2322).
  Clave aparte (`observer_seq_copy_2322`) para poder quitarlo solo.

### 4.4 Lo que no se copia y por qué

- **2335/2126/2411/2264/2195/2165 (escenas de horario):** sus NPC ya abren diálogo en el invitado (419/422) y se ven;
  el síntoma de los tenderos es de secuenciador. Además el 2335 tiene que ir **antes** de la ráfaga de 2005 (inicio de
  `0x2BC7670`, H3), lo que pide un gancho nuevo en el anfitrión y un punto de enrutado del 2335 en el invitado
  (`0xC0AFC0`) que no está localizado. Siguiente tramo, fuera de este cambio.
- **2524/2111:** llegan casi todos (§2.1). No se enrutan a X en este tramo (siguen cayendo en M sin efecto, como hoy);
  de los cuatro tenderos de la prueba, solo el posadero `0x1A9` tiene 2111 (2). Riesgo en §11.

## 5. El orden respecto a los 2005

- `osq::Copier` lleva, por viaje, el conjunto de claves **anunciadas** al usuario 2: las de cada 2005 que take_frame
  **ya metió** en la cola de la sesión 2 (tipo ≠ 0), menos las de cada 2516. Una copia sale cuando todas sus claves
  requeridas están en ese conjunto. Como la cola es FIFO y el cliente procesa en orden, el 2005 se procesa antes que
  la copia (`0xA9CA40` encuentra los actores con `0x8AF170`).
- La comprobación se hace en take_frame **justo después** del *push* de cada 2005 (también en la ruta `Pushed` de
  `observer_hold_acks`: después del 2005 y de sus paquetes retenidos), en el hilo del sniffer al llegar un 2661 cuyas
  claves ya estén y, como reintento, en el *tick* del hilo de red (cada 100 ms).
- **Coste en take_frame:** como `observer_milestone` y `observer_hold_acks`, se toma un candado hoja y se asigna en el
  montón del mod (vectores de claves): nada del juego, ninguna línea de log bajo el candado (solo el anillo sin
  candado). Acotado: la lista de pendientes se mantiene ordenada al insertar (sin ordenar en cada 2005), como mucho
  1024 entradas; `release` sale enseguida si no hay nada pendiente o la puerta está cerrada.
- **Por qué no `observer_hold_acks`:** retiene paquetes **de una clave** que pasan por take_frame y su vida la marca
  `child_hold`; aquí el paquete no pasa por take_frame (viene de otro hilo), espera a **varias** claves y vive lo que
  dura el viaje (un NPC puede entrar en la vista minutos después). Mezclarlo cambiaría un módulo validado (6965/6965).
- **Puerta:** nada sale antes de que el 2322 propio del usuario 2 (S2, `observer_milestone`) haya entrado en su cola
  (30 s después de su 2144 si no llega). La carga del invitado (S2) queda exactamente como está.

## 6. Construcción, heap y campos

- **Sin heap del juego:** los bytes se copian en el hilo del sniffer a `std::vector` del mod; se empujan con
  `server_push_extra` (= `push_outgoing_extra`: cola `g_outgoing` del mod, solo si la sesión 2 viva existe y la cola
  está por debajo de la mitad de su presupuesto). No se llama a ninguna función del juego; no hay ganchos nuevos en el
  anfitrión. Si la cola se niega, la copia sigue pendiente y se reintenta (`queue refused`).
- **Hilos:** take_frame (hilos del servidor), escritor del sniffer, hilo de red (filtro y *tick*). Un candado hoja
  (`g_mu`); dentro solo se toma el de `g_outgoing`. Las líneas: anillo sin candado, se escriben en el *tick*.
- **Sin reescritura de campos:** el invitado elige el gestor por el **actor del paquete** (§8). Si se cambiara
  `A0100001` por el personaje del usuario 2, la escena caería en M y su Kliff haría de «jugador» de las cinemáticas
  del anfitrión (`p8-observador-escenas.md` §14.7). Los enlaces mantienen `0x20000002 -> A0100001`, que en el invitado
  es la marioneta del Kliff del anfitrión.

## 7. Filtro invitado → servidor

En `offer()` (hilo de red), antes de inyectar:

- **2767, 2591, 2436, 2138, 2283, 2173** cuyo u64 @0 es un secuenciador que el servidor dio al cliente del anfitrión
  (ids únicos del servidor; las escenas propias del usuario 2, p. ej. `0x1A3`, nunca están): **descartadas**. El 2173
  `u32 1 | u32 actor` no coincide nunca con un id.
- **2100**: opcional, **apagado por defecto** (`observer_seq_copy_2100=0`, también en los inis). Con `1`, se descarta
  mientras el usuario 2 está en un viaje observador (en la sesión 7 serían los 210). Lo que se pudo comprobar [V]: en
  el campo del anfitrión el usuario 2 **no** tiene escenas propias (GC viaje 2: 0 × 2195/2264/2165 y los 36 × 2335 van
  a `A0100001`, no a su personaje; tras la reconexión, ninguno). Lo que no se pudo: si el servidor usa las posiciones
  del 2100 del usuario 2 para algo del campo (p. ej. generar escenas de facción cerca de él). Sin esa prueba se deja
  como estaba (pasan, como en las sesiones 6 y 7).
- **Ids propios primero:** los 2661 que van a la cola del usuario 2 con su propio personaje (su ESC) marcan su id
  como propio, y sus Req sobre él pasan siempre. Los ids son un contador del servidor (sesión 7: `0x13`…`0x1D7` del
  anfitrión con `0x1A3` del invitado intercalado, sin choques); un **servidor nuevo** (el anfitrión vuelve al título)
  vacía el libro, los ids conocidos y los propios (`observer_seq_copy_on_server_replaced`).
- `probe`: solo cuenta («WOULD BE DROPPED»). 2547 (u32 actor) y 3020 no se tocan.

## 8. Lado del invitado: `guest_seq_route`

### 8.1 Puntos [V]

| Punto | Lectura | Contexto comprobado (bytes = PE) |
|---|---|---|
| `0xC0612A` 2661 (`cmp bl,4 ; je`), 5 B | RAX = M; BL = kind; `[R12]` actor resuelto; `[R12+0x18]` paquete | `0xC05BC0`, `0xC05BCF` (`mov r12,r8`), `0xC060C2` (`mov [r12],rbx`), `0xC060C6`, `0xC06120` |
| `0xC07749` 2981, 9 B | RAX; `[R15]` actor; `[R15+0x18]` paquete | `0xC073BC` (`mov r15,r8`), `0xC076E6` (`mov [r15],rdi`), `0xC0773F` |
| `0xC0659E` 2810, 8 B | RAX; `[RBP-8]` actor de `0x8A67E0` si `byte [RBP]`; u32 `[RBP+0x78]` clave del paquete; u64 `[RBP-0x58]` | `0xC06340` (`lea rbp,[rsp-0x20]`), `0xC06505`–`0xC0651F` (la búsqueda y `mov rsi,[rbp-8]`), `0xC06594`, `0xC065AA` |
| `0xB87A67` 2322 (`mov byte [rax+0x231],2`), 7 B | RAX = M; R14 = paquete (clave en +5); el actor por clave (réplica de solo lectura de la tabla de actores) | `0xB878AC` (`mov r14,[r8+0x18]`), `0xB87A5D` |

- Capstone: entre la definición y el punto no se escriben `r12`/`[r12]`, `r15`/`[r15]`, `[rbp-8]`/`[rbp]`/`[rbp+0x78]`
  ni `r14`; RAX llega intacto a su consumidor (`0xC06185`, `0xC0776E`, `0xC065B2` `mov rcx,rax`; `0xB87A67`).
- Los cuatro puntos son los mismos que ya verificaron `guest_stage_diag` (2661/2981/2810) y `P8Probe` (2322): mismas
  instrucciones enteras, sin saltos dentro ni operandos relativos a RIP. Todo en `.rsrc`.

### 8.2 Un gancho por dirección

Si `guest_stage_diag=1` / `load_diag=1`, el gancho de esa dirección es **del otro módulo**, y su *callback* llama primero
a `guest_seq_route_hook` (puede cambiar RAX) y luego registra: sus líneas (`GuestStage [2661 route …]`, `P8Probe [2322
write …]`) muestran ya el gestor que usa el manejador. Si el otro módulo no está, `guest_seq_route` engancha la
dirección él mismo con los mismos bytes. Su hilo de instalación **espera** a que los otros terminen (`*_settled()`, hasta
5 min) y nunca pone un segundo gancho en la misma dirección.

### 8.3 Decisión y puerta

- `gsr::decide`: se enruta (RAX := X) solo si el actor del paquete es un **personaje** (`0xA…`), no es el personaje
  de M, coincide con la clave del paquete, tiene gestor X con la vtable `pa::ClientSequencerStageManager`
  (`exe+0x55C9720`), `[X+0x18]` es él y X ≠ M. Para el 2661, solo `kind 4`. `probe`: misma decisión, no escribe.
- Las escenas propias del usuario 2 (ESC `0x1A3`, su personaje) y todo lo de NPC/usuarios quedan nativos.
- **Puerta de entrega** (`deliver_all`, siempre activa en el invitado): un 2661/2810/2322 que nombra a otro personaje
  no llega al cliente salvo que su punto esté ON y enganchado. Mientras no se conoce el personaje propio **de esta
  conexión** (su 2756; se reaprende en cada conexión, la reconexión de la sesión 7 entró con `A0101847`), cualquier
  2661/2810/2322 que nombre a un personaje se descarta (falla cerrada) [V: en las dos capturas del invitado ninguno
  llega antes del 2756, t=8,905 y 4,663]. Hoy no llega ninguno ajeno de forma nativa [V GC]. El 2981 no pasa por la
  puerta: los 2981 del anfitrión llegan solos (129 en la sesión 7, a M, donde no encuentran el id) y uno copiado es
  el mismo tipo de paquete; filtrarlos cambiaría ese camino validado.
- **Rechazo con el enrutado ON:** un 2661 o 2322 de otro personaje que no se puede llevar a X (sin actor, sin X,
  vtable o dueño de X distintos) no toca M: el 2661 pasa con `kind = 0` (la rama nativa de `kind != 4`: una línea de
  error y nada creado) y la escritura del 2322 cae en un sumidero del mod (RAX no se vuelve a leer: `0xB87A6E` lo
  recarga). Nunca para el personaje propio (el 2322 de S2).
- **Hilo y vida de X:** el actor es siempre el que el propio manejador acaba de resolver con la búsqueda del juego
  (`0x8A67E0 → 0x8B3C00`, con el candado del gestor de actores en `+0x10`), en la misma llamada y el mismo hilo, y el
  manejador lo sigue usando; X se lee de sus componentes. Para el 2322 es su búsqueda `0xB879DD` (`[rbp-0x10]` si
  `byte [rbp-8]`; bytes `0xB879D6`–`0xB879EC` comprobados, y no se escriben antes de `0xB87A67`). Ya no hay recorrido
  propio de la tabla de actores dentro de un gancho (solo en el hilo de diagnóstico, para G5).
- **Alcance de la búsqueda del 2810 (y del 2322):** es la misma función y la misma forma de llamada que en 2661/2981
  (primer argumento el gestor de actores del cliente, tabla con candado), que en la sesión 7 resolvió `0xA0100001`
  (GL: `addressed actor 0x56F6B304C40 (0xA0100001)`): cubre a los otros personajes.
- `load_trip` (S2) ya no cuenta como «2322 del viaje» un 2322 de otro personaje.

### 8.4 G5 (sonda de solo lectura, hilo de diagnóstico)

Cada 5 s, al cambiar (y cada 5 min aunque no cambie): para los actores de `guest_seq_watch` y los de cada 2661
enrutado: existe, `+0x5E`, `+0xC2`, `+0xCF`, componente `ClientSequencerPlayDataActorComponent` (`[[a+0x68]+0xB0]`,
vtable `exe+0x55BEE78`) con `+0x2D8` y `+0x2E0`; y para cada personaje enrutado su X: `+0x231`, instancias `+0x174`,
creados `+0x17C`, lista de precarga `+0x1C0` y lista `+0x1B0`. No cubre aún modo `0x88E8E0`, búfer de interpolación
ni posición (resto de G5 en `p8-npc-tiendas-animales-escenas.md` §6.2).

## 9. Implementación

| Archivo | Contenido |
|---|---|
| `src/mod/session2/observer_seq_copy_logic.h` (nuevo) | Lógica pura: modo, `OnlyFilter`, analizadores, `Ledger`, `Copier` (viaje, pendientes, anunciados, puerta, `release`, 2810, filtro C>S) |
| `src/mod/session2/observer_seq_copy.{h,cpp}` (nuevos) | Tap del sniffer, take_frame, filtro, *tick*, líneas y resumen de 60 s |
| `src/mod/session2/guest_seq_route_logic.h` (nuevo) | Decisión de enrutado, puerta, estado G5 |
| `src/mod/session2/guest_seq_route.{h,cpp}` (nuevos) | Puntos, instalación cooperativa, puerta, G5, líneas |
| `server.{h,cpp}` | `server_push_extra`; take_frame llama a `observer_seq_copy_on_out` (y `to_live` lo incluye); época y servidor sustituido |
| `session2.cpp` | Clave, `sniffer_fanout`, filtro en `offer`, *tick*, describe |
| `guest.cpp`, `guest_diag_worker.cpp` | Arranque, puerta en `deliver_all`, *tick*, describe |
| `guest_stage_diag.{h,cpp}`, `p8_probes.{h,cpp}` | `*_settled()`, `*_point_hooked()`; sus *callbacks* de esos puntos llaman antes a `guest_seq_route_hook` |
| `load_trip.cpp` | Ignora el 2322 de otro personaje |
| `config.{h,cpp}`, `CMakeLists.txt` | Claves; fuentes; prueba `optd_seq_copy` |
| `tests/optd_seq_copy_tests.cpp` (nuevo) | Lógica con paquetes reales de HC/GC (§12) |
| `docs/research/ini/cdmp.p8-s7-host.ini`, `cdmp.p8-noche-local.ini`, `cdmp.p8-s7-shadow.ini` | Claves nuevas |

Comprobado en el `cdmp.asi` compilado (`dumpbin /disasm`): `guest_seq_route_hook` y `guest_seq_route_gate` solo llaman
a las lecturas SEH propias (`rdv`, `rdp`, `key_of`, `manager_of`, `find_actor`, `mgr_state`), `emit`, auxiliares
atómicos y `__security_check_cookie`; ninguna instrucción VEX, ni `memcpy`/`memset`.

## 10. Prueba sin juego: reproducción de la sesión 7 [V]

Herramienta del scratchpad (`s7replay.cpp`, compila contra `observer_seq_copy_logic.h`): HC alimenta el libro (como el
sniffer) y GC hace de take_frame (S>C) y de `offer` (C>S), en orden de tiempo (`GC + 143,2 s`). El viaje observador se
decide por el campo del 2174 (en GC el dueño ya viene reescrito).

Con `observer_seq_copy_only=1AB,1AD,1A9,19E`:

```
  189.605 user 2 2174 (field 1) observer=1 snapshot 1 of live 18
  198.352 user 2's own 2322: gate open (pending 1)
  198.352 COPY 2981+2661 0x19E (snapshot, waited 8.746 s, 3 required)
  198.352 COPY host 2322
  452.409 bound actor of delivered 0x19E removed for user 2   (x2: 157 y 158)
  452.482 host 2810 0x19E -> COPY 2810
  452.565/581/588 live pending 0x1A9 / 0x1AB / 0x1AD
  452.765 COPY 2661 0x1AD (live, waited 0.177 s) / 0x1AB (0.184 s) / 0x1A9 (0.200 s, 8 required)
guest C>S DROP 2100: 210; host sequencer: 0; pass: 2173 x12, 2436 x1, 2591 x10
```

Sin filtro: 18 de instantánea + 15 en vivo = 33 2661 (18 con su 2981), 1 2322 y 9 2810; ninguno queda pendiente al
final. **Un PC con el juego** (`cdmp.p8-noche-local.ini` + `cdmp_peer`, guion `login_e3lc00soak_child_112627.troc`)
probaría además take_frame, la cola y el sniffer en vivo: en la grabación del peer, cada 2661 de `A0100001` tras el
2005 de sus NPC (`analyze_capture.py … scenes A0100001`). No se hizo: el juego lo lanza el usuario.

## 11. Riesgos [E]

1. **X en el invitado:** que su actualización corra para un personaje remoto y que la `SequencerLoadingTask` termine
   (§4.3). Lo dirá `GuestSeqRoute [G5 X]`: instancias `+0x174` suben; lista `+0x1C0` se vacía tras el 2322.
2. **El Kliff del anfitrión como «jugador» de la escena en el invitado** (rol `0x20000002`): cámara, entrada o
   interfaz del invitado. Criterio de aborto.
3. **El 2322 copiado** pone X en 2 (luego 3): todos los secuenciadores precargados de X se reproducen. Si algo raro
   ocurre al llegar, `observer_seq_copy_2322=0`.
4. **2810 en el invitado** aplica `vf150(0)` al componente de transformación del actor del paquete (`0xC0653C`): la
   marioneta del Kliff del anfitrión, como en el cliente del anfitrión con su propio Kliff.
5. **NPC fuera de la vista solo del invitado** con su secuenciador vivo en el anfitrión: la instancia de X enlaza un
   actor borrado sin 2810. En la sesión 7 cada 2516 de un enlazado fue seguido del 2810 del anfitrión en < 80 ms (§2.2);
   se cuenta y se escribe (`bound actor removed`).
6. **Entrada tardía:** el 2661 de la instantánea reproduce la escena desde su preparación.
7. **2100 descartado:** si el servidor usa las posiciones del invitado para algo suyo (generar escenas de facción
   cerca de él), dejaría de hacerlo. `observer_seq_copy_2100=0` lo devuelve.
8. **2524/2111** de los secuenciadores copiados siguen yendo a M (no se encuentran): eventos de rama/carta que X no
   recibe (el posadero `0x1A9` tiene 2 × 2111).
9. **Ids que cambian entre sesiones:** `observer_seq_copy_only` admite las claves de los NPC; la línea `[ledger]` de cada
   2661 del anfitrión da id y enlaces para ajustarla.
10. El libro depende del sniffer (pérdidas = copias que faltan).

## 12. Pruebas [V]

`optd_seq_copy` (0,05 s), con los paquetes reales de HC/GC: valores de las claves; lista `only` (ids, claves, basura);
analizadores (2661 de 8 enlaces, 2981, 2810, 2516, 2005 sin el registro de usuario, dueño del 2174 del anfitrión y de
la copia reescrita del invitado); libro (2981 antes de su 2661, reemplazo que conserva el 2981, borrado, 2322, 2661 no
propio, campo nuevo, cotas); viaje con instantánea (`0x19E`) y en vivo (`0x1AB` sin segundo 2981), puerta por el 2322
propio y por tiempo, 2322 una sola vez, filtro `only`, 2810 copiado y cancelado, 2516, cola que se niega y reintento en
orden, cotas; filtro C>S (escena propia `0x1A3` y 2173 de personaje pasan; 2100 con y sin la subclave); decisión del
invitado razón por razón y la puerta. No prueban ganchos, hilos, take_frame en vivo ni el cliente.

## 13. Qué mirar en la próxima prueba

**Anfitrión** (`cdmp.p8-s7-host.ini`):

- `ObserverSeqCopy ON (observer_seq_copy=1) … only: seq 0x1AB seq 0x1AD seq 0x1A9 seq 0x19E …`.
- `ObserverSeqCopy [ledger] … 2661 prepared of sequencer 0x… (actor 0xA0100001 …) | bindings …`: comprobar que Grover,
  Dahlia, posadero y Street Vendor siguen teniendo esos ids; si no, cambiar `observer_seq_copy_only` (claves).
- `[trip] … OBSERVER trip | snapshot: N …`, `[pending] …`, `[gate] … user 2's own 2322 went …`.
- `[copy] sequencer 0x1AB (live): SENT to user 2's queue 2661 … after the 2005 of its 2 bound actor(s), 0.18 s …`.
- `[2322] the host's 2322 …: SENT`; `[2810] … SENT`; ningún `[2981] … did not reach user 2 natively` mientras el
  invitado está cerca (si sale, el buscador cercano no le llegó); ningún aviso `pending table full` ni `delivered
  mark(s) evicted`.
- `ObserverSeqCopy summary` cada 60 s: `queue refused 0`, `pending` sin claves eternas, `ring lost 0`.

**Invitado** (`cdmp.p8-s7-shadow.ini`):

- `GuestSeqRoute ON … points: 2661 … (guest_stage_diag's hook) … 2322 … (P8Probe's hook) | REFUSED: none`.
- `GuestSeqRoute [2661 PrepareSequencerAck 0xC0612A] … corr=2661:A0100001:0x1AB:0x2 … X 0x… : +0x231 1 … | ROUTED …`,
  y `GuestStage [2661 route 0xC0612A] … manager 0x<X> … of actor 0xA0100001` con `[2661 result 0xC061C1] created`.
- `GuestSeqRoute [2322 …] ROUTED`; luego `[G5 X] character 0xA0100001: … +0x231 3, instances N` (sube).
- `[G5 actor] 0xB0100C0F: exists … sequencer component: owner 0xA0100001, scene 0x1AB` (antes `scene 0`).
- `GuestSeqRoute [gate]` **no** debe salir; `refused 0` en el resumen; `GuestLoad` del viaje: «one 2322, after its 2144» (sin DUPLICATE).
- **Juego:** Grover, Dahlia, el posadero y el Street Vendor visibles en el invitado; diálogo y tienda (un 2857 del
  invitado a esas claves y su 2948); la cámara y el control del invitado no cambian.

**Abortar** (poner `observer_seq_copy=0` o `guest_seq_route=0`): cualquier cierre, salto de cámara o pérdida de
control del invitado, `GuestSeqRoute … NotStageManager/XNotItsOwn`, `queue refused` creciente, o C>S del invitado con
ids del anfitrión que no se descarten.

## 14. Comprobado y pendiente

| | Estado |
|---|---|
| Qué falta en el invitado (2661 siempre, 2981 de antes, 2810) y que 2524/2111 llegan | [V] capturas |
| Orden 2005 (+0,18 s por `observer_hold`) frente al 2661 del anfitrión | [V] capturas |
| El invitado enruta a M, no a X | [V] GL |
| Puntos, registros y bytes del lado invitado | [V] capstone + PE |
| Secuencia de copias en la sesión 7 | [V] reproducción con la lógica real |
| Que X cree la instancia, la cargue y la reproduzca tras el 2322 | [E] |
| Que el tendero se vea y abra tienda en el invitado | [E] |
| Efecto del 2100 descartado | [E] |

## 15. Revisión de `fc49a0d` (mismo día)

| # | Hallazgo | Resultado |
|---|---|---|
| 1 | Un secuenciador copiado y preparado otra vez perdía su marca «entregado»: su 2810 se cancelaba y X conservaba la instancia vieja | **Corregido.** La marca se mantiene; al borrarse, se copia el 2810 si alguna copia salió en el viaje y se cancela la pendiente. Prueba |
| 2 | La puerta deja pasar todo 2981 | **Se mantiene** (§8.3): los 2981 ajenos ya llegan de forma nativa y caen en M sin efecto; uno copiado es igual. Comentario aclarado en `guest_seq_route.h` |
| 3 | Puerta abierta sin clave propia; la clave no se reinicia | **Corregido:** falla cerrada (ninguno llega antes del 2756 en la sesión 7) y la clave vale solo en su conexión. Prueba |
| 4 | `branch_seen_` no se invalida; los 2981 posteriores no se copian | **Corregido** con hash del payload en lugar de «visto»: no hace falta invalidar; y copia en vivo de un 2981 que no llegó (§4.2, con la evidencia de la captura). Prueba |
| 5 | Pendientes 512 frente a libro 1024 | **Corregido:** 1024 los dos; si está llena, se purgan las que salieron del libro; contado y aviso en el log. Prueba |
| 6 | Marcas de entregado a 1024 sin aviso | **Corregido:** se expulsa la más antigua, contado y aviso. Prueba |
| 7 | El 2322 del anfitrión sobrevivía a su cambio de campo | **Corregido:** el 2174 propio del anfitrión lo borra. Prueba |
| 8 | 2100 descartado | **Apagado por defecto** (`observer_seq_copy_2100=0`, inis también): §7 |
| 9 | RAX := X tras un recorrido sin candado | **Corregido:** el 2322 usa el actor que resolvió su manejador; ningún recorrido en ganchos (§8.3) |
| 10 | Alcance de `0x8A67E0` en el 2810 | **Confirmado** sin cambio (§8.3) |
| 11 | `known_` nunca se vacía; ids propios | **Corregido:** ids propios primero; servidor nuevo vacía libro, ids conocidos y propios. Prueba |
| 12 | Candado y asignaciones en take_frame | **Documentado** (§5); lista ordenada al insertar y salida rápida |

Además: rechazo del 2661/2322 ajeno no enrutable con el enrutado ON (§8.3). `optd_seq_copy` cubre cada punto; 31/31.
La reproducción de la sesión 7 da el mismo resultado que en §10 (ningún 2981 en vivo mientras el invitado estaba
dentro).
