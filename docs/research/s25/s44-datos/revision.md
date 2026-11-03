# Revisión adversarial S44 — `git diff 4ca7f44 dc30c87` (rama `opcion-d-escenas`)

Build 1.0.0.2976. Solo lectura del worktree. Desensamblado con capstone sobre el exe de Steam (`scratchpad\s44\revision\pe.py`),
decompilación Ghidra con `cdmp-query.ps1`. Test compilado aparte (`git archive dc30c87`, `cl /std:c++20 /W4 /WX`):
`optd_seq_foreign: OK`. Los 9 payloads 2661 de los tests están, byte a byte, en las capturas que citan (opcode 2661).

## Veredicto

- **`probe` (paso 1: `guest_seq_foreign_player=probe` + `guest_seq_foreign_probe=1`): SEGURO.** No escribe nada en datos del
  juego (solo `Applied` escribe, y exige `Mode::On && routed`). Las 4 sondas están en entradas verificadas o en un
  `create_mid` de instrucciones completas, sin RIP‑relativos ni saltos entrantes, con lecturas SEH, atómicos y anillo; no se
  llaman por fotograma.
- **`1` (paso 2): aceptable con condiciones.** No encuentro P0: la escritura es un byte en el vector propio del manejador,
  en un marco válido, entre su llenado y su único consumidor, y comprobado antes y después. Pero el tipo 0 **no es inerte**:
  en el motor significa «nodo no enlazado» y activa caminos nativos propios (spawn de actor cliente, resolución por datos
  del nodo). Además el dueño de muchas escenas no está en la ranura de jugador (tiempo y fauna en `0x20000001`). Antes de
  pasar a `1`, el paso 1 tiene que demostrar con P1 que el dueño está **siempre** en un nodo `CurrentControl` en las
  familias que se van a tocar (ver H‑1).

## Lo que he comprobado y está bien

1. **Disposición en `0xC0612A`** (`h_C05BB0.txt`): `rbp` = `lea rbp,[rsp-0xA0]` (0xC05BC0), no cambia en toda la función.
   - `[rbp-0x80]`/`[rbp-0x78]` se ponen a cero en 0xC05C79/7D (el qword de `-0x78` cubre también la capacidad `-0x74`).
   - Puntero y capacidad en 0xC05E44/48; número = `ebx` en 0xC05E84, **antes** del bucle de lectura.
   - A 0xC0607F solo se llega si `[rbp-0x54] == len` (0xC05FC2), es decir, con todas las entradas leídas; cualquier fallo
     de lectura va a 0xC06024 y libera el vector. En 0xC0612A todas las entradas `count` están rellenas.
   - Entre 0xC05E84 y 0xC0612A nadie escribe `[rbp-0x80..-0x71]` (0x8A67E0 usa `[rbp+0x70]`; 0x17FA6C0 usa `[rbp-0x48]..`).
   - `[rbp]` = datos de reproducción (0xC05D91 `lea rdx,[rbp] ; call 0x244BC50`), `+0` = dueño = `@5`.
   - Con `n == 0` (0xC05DE7) número y capacidad quedan a 0: el plan da `NoOwnerBind`.
2. **Memoria propia del manejador**: reservada en 0xC05E12/19, pasada por dirección en 0xC06179, copiada por `0xA9CA40`
   (0xA9CC28 `vmovsd [r14]` + 0xA9CC32 `mov eax,[r14+8]` → entrada de 0x30 B en `task+0xA8`), liberada en 0xC0620E/22.
   - El paquete crudo `[r12+0x18]` no se toca.
   - Un reintento (0xC05D49, «actor no listo») sale **antes** de reservar el vector, y la siguiente entrega construye uno
     nuevo y vuelve a pasar por el gancho.
   - `guest_stage_diag`, `g_seq_res.note`, `classify_2524`, `seq_actor_restore` y el ledger leen el paquete o sus propios
     datos, no este vector.
3. **Retenciones**: `0xA9CA40` solo toma un attacher (`0x8AF170`) para los tipos 4/5 (0xA9CB87). Las entradas guardan el
   flag «tiene valor» en `+0x20`, y su destrucción lo comprueba (patrón 0xA9CD49/0xA9CD54). Con 0 no se toma ni se suelta
   nada: no hay desequilibrio.
   - El 2810 (`0xA9D950`), el 2981 (`0xA9E100`) y el 2524 (`0xA9E480`) buscan la escena por id.
   - `0x602940` desenlaza (`0x9EE710`) comparando `seqcomp+0x2E0` con el id de la etapa, no por tipo. Como la copia nunca
     se enlaza, nunca coincide.
   - **Efecto lateral (favorable):** con 4/5, si el actor falta, `0xA9CA40` descarta la escena entera (0xA9CC13 →
     0xA9CE81); con 0, ya no depende de que la copia exista. En la práctica no cambia nada, porque `Route` ya exige X =
     gestor del actor del paquete.
4. **Lectores del tipo en `.rsrc`**:
   - `0x205DC30` ignora el 0: 0x205DD50 `test bl,bl ; je`; cuenta todo lo que no es 0 (`inc r15w`) y solo resuelve el 5
     (0x205DDA1).
   - `0x602940` solo quita `0x200` con tipo 5 (0x602C69).
   - `0x205F7F0` y `0x20521E0` exigen 5 (0x205FB38, 0x2052DA8).
   - `0x6067C0` trata el {0,1,2,3,6} como «no aplicar» (0x606ABF).
5. **Sondas**:
   - **Firmas y patches:** firmas de 40 B únicas en `.rsrc`, `.pdata` exacta y patches de 5/5/5/7 B de instrucciones
     completas (verificación propia: no hay saltos a 0x602941‑44, 0x601471‑74, 0x601C11‑14 ni 0x6034FB‑0x603500).
   - **Registros:** `r15` = etapa en todo `0x6030D0` (solo 0x603120 lo escribe). `rbx` = entrada+8 (0x603368/0x603538),
     con camino lineal hasta 0x6034FA.
   - **Rechazo:** `p8_install_mid` rechaza el fallback de 14 B y cualquier byte distinto, y no instala la sonda si algo
     falla.
   - **Datos de P2:** `R8` = descripción en vf21/vf22 (`mov r15,r8`, y `[r15+8]+0x30/+0x38` antes de `call 0x205DC30`
     en 0x60160B/0x601DAF). La réplica de P2 coincide con `0x205DC30` (nodo `+0x30` o `+0x34`, primer id que coincide,
     0 no cuenta, solo 5 resuelve).
   - **vf178:** `0x4FE260` usa rip → `0x6D69190` = `kWorldSystemSlot`.
   - **Coste:**
     - `0x602940` y `0x6030D0`: una vez por inicio de escena (único llamador `0xA9D91A`);
     - vf21/vf22: por evento SequencerControl (vtable en `.xdata` 0x558BE28/30);
     - nada por fotograma;
     - en el gancho, como mucho 64 entradas × 6 lecturas SEH, sin CRT, formato, reservas ni cerrojos.
6. **Modo 0 igual que hoy**:
   - `guest_seq_foreign_start` no crea hilo ni ganchos con `0` y sin probe;
   - `guest_seq_foreign_2661` sale en la primera línea (una carga atómica);
   - `guest_seq_foreign_tick` sale enseguida;
   - el único cambio en `guest_seq_route.cpp` es la llamada (líneas 1437‑1443), antes de `ctx.rax = f.x`, sin tocar
     RAX/RBX.
7. **Inis**: los dos s44 = `cdmp.2inst-s43-activo-guest.ini` más la cabecera, el bloque S44 dentro de `[session2]`
   (líneas 252‑266) y las dos claves. `guest_seq_route_deny=8CD771B1` es igual que en la base: el `9586FFA4` estaba en
   `s43-pose`, no en `s43-activo`. Entre `probe` y `activo` solo cambian las líneas 1‑2 y 265.
8. **Clave propia**:
   - `g_own_key` sale del 2756 con su época (`guest_seq_route.cpp:1637-1641`, `:1792`);
   - la compuerta (`gsr::gate`) no entrega ningún 2661 de un personaje mientras la clave propia es 0;
   - `plan` también excluye la clave de M (leída en cada llamada);
   - tras un rejoin, la época nueva da 0 hasta el 2756 nuevo, y mientras tanto la compuerta descarta.
   - Las escenas propias del invitado (tienda 0x143, `use_sealedartifact` 0x1A3) tienen `actor @1` = la propia
     (`OwnCharacter`): no llegan al gancho y, aunque llegaran, dan `OwnScene`.
   - En el inventario (1 635), dueño = actor `@1` siempre, y ninguna escena enlaza a la vez H y G.

## Hallazgos

### P0
Ninguno.

### P1
Ninguno confirmado. H‑1 puede llegar a P1 según lo que mida P1 en el paso 1.

### P2

**H‑1. El tipo 0 es el estado nativo «nodo sin enlazar», no un «ignorar». El arreglo lo pone sin saber el tipo de nodo.**
- **Spawn de actor cliente (`0x5EB310`):** si la entrada del nodo tiene tipo ∈ {1,2,4,5} → error (0x5EB41F
  `movzx eax,[rdx+0xC] ; cmp al,7 ; bt 0xC9 ; jb`, y si no, 0x5EB431 `*res = error`). Con {0,3,6,7} sigue y construye un
  `CreateClientActorDesc_Sequencer` (decomp líneas 128‑140): crea un actor de cliente para el nodo, con la apariencia
  sacada del actor que devuelve `0x15A77B0(stage)`. Hoy ese comando falla en un nodo enlazado al anfitrión; con 0 **se
  ejecutaría**, y podría salir un «doble» en el invitado.
- **Resolución por datos del nodo (`0x205E520`):** tiene una rama explícita para el 0 (0x205E6AF `test al,al ; jne`). Con
  datos de tipo `SequencerGamePlayData_GimmickActor` (`0x525520`), resuelve el actor por ids o nombre del nodo y lo cuenta.
- **Dónde está el dueño:** `guest_seq_foreign_logic.h:142-146` decide solo por `actor == dueño`, y el vector de 12 B no
  lleva el tipo de nodo. El inventario pone al dueño en `0x20000001` en 331 escenas de tiempo y 41 de fauna (`spawn_auto`,
  10/1, `AutoSpawnOwnerData`), y en `0x20000003` en `memory` y en 23 de `quest`. Que sean `CurrentControl` es [H]; el
  propio `nativo.md` §6(a) dice «P1 tiene que confirmar». En fauna y tiempo el dueño puede ser un PivotActor: sin él, las
  colocaciones relativas se pierden y un spawn podría desbloquearse.
- **Arreglo propuesto:**
  1. En el paso 1, que el resumen de P1 desglose `OwnerOtherNode` y `OwnerActive` **por recurso o familia**; hoy solo
     hay conteos globales (`summary()`, `guest_seq_foreign.cpp:393-396`).
  2. Pasar a `1` solo si `OwnerOtherNode == 0`. Si no, limitar el arreglo con un mapa aprendido (recurso, nodo) → tipo de
     nodo, escrito por el hook P1 en un anillo y consultado por el hook 2661 con atómicos: neutralizar solo si se vio
     `+0xAC == 0`. La primera aparición quedaría «would», que se mide, no se rechaza a ciegas.
  3. Corregir `s44-escenas-del-anfitrion.md` §2.2 y `nativo.md` §6: «todos los lectores aceptan valores ≠ 4/5» es cierto,
     pero el 0 tiene semántica propia (`0x5EB310`, `0x205E520`).

**H‑2. El dueño sigue siendo alcanzable por `stage+0x98` (`0x205D7C0`), sin pasar por los enlaces.**
- `0x205D7C0` resuelve `[stage+0x98]` por clave (decomp) y tiene 19 llamadores. Entre ellos:
  - vf21 en 0x6014A9 → 0x6014C6 `mov [r15],rax`: el actor dueño va a `desc+0`, que consume `.xtls`;
  - vf22 en 0x601C4D → 0x601C6D, igual;
  - `0xA9E100` (2981) y `0xA9E480` (2524);
  - `0x6021F0`, `0x602250` y `0x6022B0` → `0xC03D50`, `0xC03F10` y `0xC040D0`: envían C>S `0x966`, `0xB98` y `0x915`
    «desde» el dueño (preexistente, no lo cambia el arreglo).
- `s44-escenas-del-anfitrion.md` §0.1/§2 afirma que todo pasa por `0x205DC30`: **es exagerado**. Si con `1` la pose sigue y
  P2/P3 ya no muestran la clave del anfitrión, el sospechoso es `desc+0` en el control de `.xtls`.
- **Arreglo:** añadir a P2 la clave de `[stage+0x98]` y marcar «control con `desc+0` = otro jugador» (ya se tiene el
  dueño; basta con contarlo cuando `foreign`). Documentarlo como hueco conocido.

**H‑3. Bit `0x200` (ocultación) de la copia, sin medir.**
- **Quién lo quita:** al empezar la escena, solo con tipo 5 (0x602C69‑0x602CA3, `0x1795490(Status,0x200,0,…)`);
  también `0x205F7F0` (tipo 5 y nodo 1/2/3), `0x9F05D0` (en una vtable de `.xdata` 0x55BF020) y `0xB435D0`.
- **Quién lo pone (`r8b = 1`):**
  - `0x5E9C40`, `0x5EF730` y `0x5F7B20` (órdenes de etapa: pasan por `0x205DC30`, así que el arreglo también las corta);
  - `0x8D8940` (desconocida);
  - el login del CC (`copias-grupo-invisibles.md` §2.4, si `seqcomp+0x320`).
- **Riesgo:** si la copia entra con `0x200` (el anfitrión en una escena al conectar, o `0x8D8940`), ya no hay escena que
  la «destape» en el invitado y se queda invisible (criterio de aborto 4).
- **Arreglo barato:** en `guest_seq_foreign_2661`, que ya tiene `[r12]` = la copia (0xC060C2 `mov [r12],rbx`), leer con
  SEH `u32 [[[[r12]]+0x68]+0x20]+0x390` y meter el bit en la línea `[foreign]`/`[would foreign]`. Y/o en el tick, 5 s
  después, por clave (`guest_seq_route_find_actor`).

**H‑4. Pérdida de referencias para los NPC.** Al quitar al anfitrión de la escena en X, los NPC que lo miran o hablan con
él (`LookAt 0x5F3600`, `Talk_TwoTarget`, Pivot, posiciones relativas) se quedan sin destino.
- Es cosmético; Matthias, los tenderos y el gimmick siguen enlazados.
- El criterio de aborto 5 lo cubre solo en parte («dejan de animarse»): añadir «miran al vacío o se colocan en otro
  sitio».

### P3

- **H‑5.** `tests/optd_seq_foreign_tests.cpp:225-231` da `Applied` para `kLoading` (`0x8CD771B1`), pero en vivo está
  denegado (`guest_seq_route_deny`) y nunca llega al gancho. Poner un comentario o quitarlo de ese bucle.
- **H‑6. Huecos de los tests:**
  - el dueño `A?` (0xA0100363: `spawn_auto_animal_bush_bird`, `use_sealedartifact`);
  - una escena de fauna o de tiempo con el dueño en `0x20000001` y varios NPC (es justo la familia de H‑1);
  - la integración con `guest_seq_route_hook`, que solo llama con `raw && resource_known` y `Route`/`WouldRoute`.
- **H‑7.** `guest_seq_foreign.cpp:244-245`: con más de 16 ids de destino, `nid = 0` y P2 sale «NothingResolved».
  Truncar a 16 y marcar «truncado».
- **H‑8.** `guest_seq_foreign.cpp:333-334`: la línea `[foreign]` reconstruye el nodo como `0x20000000 | byte bajo`, que es
  falso para nodos ≥ `0x20000100`. Guardar el nodo completo (hay sitio en `v[8..11]`).
- **H‑9.** P1/P2 consideran `foreign` todo lo que no es `own_key`. Si la clave propia es 0 (justo tras reconectar), todas
  las escenas, incluidas las del invitado, salen como FOREIGN en las líneas. Solo es diagnóstico; usar también la clave de
  M o ignorar con `own == 0`.
- **H‑10.** El principio no se aplica a M. Una escena **propia** del invitado (`OwnCharacter`, va a M) que enlazara al
  anfitrión no se neutraliza, y tampoco un 2661 del anfitrión que enlazara al invitado como no dueño (`judge_bind` →
  `Keep`). No se ha visto (0 de 1 635); dejarlo anotado.
- **H‑11.** P1 y P2 leen `stage+0x120` sin el cerrojo `[stage+0x20]` (el juego lo toma en 0x602A15 y 0x6014E8). SEH lo
  hace seguro frente a fallos, pero los datos pueden estar desfasados. Además `rd()` captura cualquier excepción (también
  una página de guarda); es el patrón del resto del mod y el riesgo es bajísimo.

## Punto 7: ¿el principio falla en algún caso?

- **Montar o compartir montura y vehículos:** no son escenas 2661 en lo visto. Si algún embarque lo fuera (nodo
  ConnectActor), la copia dejaría de ir «enganchada» por la escena en el invitado, y la posición seguiría llegando por red.
  Es cosmético. Hoy, en cambio, se congelaría.
- **Escenas que animan al anfitrión** (sentarse a una mesa de minijuego, gestos de tienda, revancha): en el invitado la
  copia queda con lo que llega por red (2991/2697), que no lleva las acciones del grafo de la escena. Se cambia «pose
  congelada» por «idle de red». Es el compromiso buscado, pero no es una reproducción fiel.
- **Cinemáticas compartidas:** de forma nativa no existen (ToTarget). En el invitado, la copia sigue visible donde diga la
  red mientras el anfitrión ve su cinemática. Está bien. El riesgo real es H‑3 (oculta) y H‑1 (un doble por spawn).
- **El anfitrión enlazado en una escena del invitado:** no lo cubre (H‑10).

## Qué mirar en vivo

**Paso 1 (probe):**
- **P1:** `OwnerOtherNode` tiene que ser 0. Anotar, por recurso o familia, el tipo de nodo del dueño (tiempo y fauna en
  `0x20000001`, `memory`/`quest` en `0x20000003`, tiendas y revancha en `0x20000002`). Si alguna familia sale con tipo
  ≠ 0, no pasar a `1` sin el mapa aprendido de H‑1.
- **P2 y P3:** confirmar `ANOTHER player's character` con `A0100001` en la revancha (el mecanismo). Anotar si el
  `desc+0` (H‑2) también es el anfitrión.
- **Status `+0x390`** de la copia (si se añade H‑3): ver si alguna vez tiene `0x200` antes de una escena.

**Paso 2 (1):**
- Un `[foreign]` con `written 1/1` y después un P1 de la misma secuencia con `link 0` (o `no binding to the owner`).
  **Nunca** `link 5`.
- Ningún actor nuevo o «doble» del anfitrión junto a escenas de fauna, tiempo o tiendas (H‑1).
- La copia, visible: alfa o `0x200` (H‑3).
- Los NPC de la escena siguen animados y no cambian de orientación o sitio respecto al paso 1 (H‑4).
- `GuestStage [2661 result 0xC061C1]` = 0 en todas las secuencias `[foreign]`.
- Si la pose sigue sin P2/P3 sobre `A0100001`: mirar `desc+0` (H‑2).
