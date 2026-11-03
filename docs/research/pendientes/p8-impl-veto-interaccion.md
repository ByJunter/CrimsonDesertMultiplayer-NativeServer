# P8: SetInteraction deja de vetarse en X (`guest_x_ui_veto_allow`)

Build del juego 1.0.0.2976. Fecha: 2026-10-10. Sin juego: lectura estática con capstone sobre el PE de solo lectura
(`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, el de `pedis.py`), los registros de la sesión 11 y el informe
independiente `npc-raiz-s11-20261010-work\INFORME.txt` §6 y `personal\informe.txt`.

Convenciones: **[V]** comprobado (código leído o dato contado), **[E]** interpretación fundada, **[H]** sin demostrar.
X = el gestor de escenas del Kliff del anfitrión replicado en el invitado; M = el del Kliff propio del invitado.

## 1. Resumen

- **Síntoma (sesión 11):** el invitado ve la escena del tendero pero no le sale el menú. Manda 2857
  (`SetQuestDialogReq`) a Renee (`B0100153`) y a Delkin (`B0100156`), pero ningún 2720 (`InteractionDoStepReq`).
- **Causa probable [E]:** `guest_x_ui_veto=1` vetaba `SetInteraction` en X. Esa función no es interfaz: da de alta o
  de baja las **interacciones dinámicas de los NPC** que enlaza la escena (hablar, tienda). Las escenas 196/198 del
  anfitrión, copiadas a X, registran las de Renee y Delkin al crearse, y el veto se las quitaba al invitado.
- **Cambio:** clave nueva `[session2] guest_x_ui_veto_allow`, por defecto `SetInteraction`. Los manejadores de esa
  lista nunca se vetan para X: siguen enganchados y contados, y su línea dice `ALLOWED`. SetInteraction pasa entera
  (alta y baja). InputBlock, UIControl, UIShowMinimap, UIStageGuide y TimelineEvent siguen vetados; las cámaras
  siguen en probe.
- **Sin cambio de protocolo.** 40/40 pruebas.

## 2. Evidencia

### 2.1 SetInteraction `0x2369360` [V]

Envolturas vf8/vf9 `0x235E0F0` / `0x235E100`: `mov r8b,1` / `xor r8d,r8d` y salto. `rcx` = la función de etapa,
`rdx` = el contexto (`[rdx]` = la etapa, `[[rdx]+0x58]` = el procesador), `r8b` = 1 alta / 0 baja.

```
0x23693BD  call 0x2049B90(proc, func+0x20, [ctx+0x48], &lista)   ; resolver actores; si falla: aviso y salida
0x236951F  call 0x393DF0 ...                                         ; cerrojo de la etapa
0x2369543  if func.byte3B: etapa.vf120(...) (alta) / etapa.vf128 (baja)
0x2369590  por cada actor de la lista (entradas de 0x20, actor en +8):
             comp = [[actor+0x68]+0x98]
             0x393650: cerrojo del actor
             alta:  0x2124370 -> .xtls 0xE402CC0(comp, func.u16[0x38], func.byte[0x3C], [etapa+0x30],
                                                 dato de etapa, func.byte[0x3B], condición func+0x30 o una por defecto)
             baja:  id = 0x3888B0(func+0x38); 0x21245B0 -> .xtls 0xE404A10(comp, id)
0x23697F1  liberar la lista; epílogo 0x23698C6
```

- **Qué actores.** `0x2049B90` → `.xtls 0xE0314C0` → `0x255A030` (busca por nombre el selector `func+0x20` en los
  enlaces de la etapa; un nombre especial da `0x1FFFFFFF`) → `0x205DC30`:
  - `0x1FFFFFFF` → `0x205EFB0` → `0x1F6C540([etapa+0x28])`: el actor dueño del gestor. En X es el Kliff del
    anfitrión (`A0100001`), no el del invitado [E: `[etapa+0x28]` = G por analogía con UIFindPath, que llama a
    `0x1F6C540(G)`].
  - Enlaces `[etapa+0x120]` (paso 0x28) de tipo 5 → el actor por clave en el gestor global de actores. En las escenas
    de tienda: el NPC (`B0100153`, `B0100156`).
  - Por tanto: **los NPC de la escena y, como mucho, el personaje del anfitrión**. Nunca el personaje propio del
    invitado, salvo que un enlace lo nombre, y las escenas del anfitrión no lo hacen [E].
- **Qué escribe.** Solo el componente de interacción de cada actor (`comp+0x88` lista de entradas de 0x30, `+0x90`
  cantidad) y, con `byte3B`, la propia etapa:
  - `vf120` (`0x605A40`, vtable `pa::ClientSequencerStage` `0x558BD80`): arma registros de 0x2C bytes (posiciones e
    ids) y los guarda en `etapa+0x400` / `+0x408` bajo el cerrojo de la etapa. `vf128` es un thunk `.xtls`
    (`0x8F7A6A0`). Es estado de la etapa de X. Quién lo consume: [H].
  - `E404A10` (baja) busca la **primera** entrada cuyo `0x3888B0(entrada+0x10)` sea el id, la cambia por la última,
    resta 1 a `+0x90` y llama a `0x2124730` → `.xtls 0xE411950` (actualizar). Una baja quita **una** entrada.
- **Sin efecto personal directo.** Las únicas llamadas del cuerpo son: el resolvedor, el aviso de fallo
  (`0x2066090` / `0x609990`), cerrojos (`0x393DF0`, `0x393650`), las dos vf de la etapa, una condición por defecto
  (`0x14203F0`), alta/baja y liberaciones por refcount. Ninguna toca cámara, entrada, paneles ni diálogo.
  Lo que haga `E411950` con la lista nueva (por ejemplo, refrescar el aviso de interacción del NPC) es [H], pero es
  del NPC, no del jugador.

**Clasificación:** estado del mundo (qué interacciones ofrece cada NPC). De forma nativa, en la partida del anfitrión, esas
entradas existen para el jugador que se acerque al NPC. Vetarlas en el invitado le quita al invitado la tienda.

### 2.2 BlockInteraction `0x23698F0` → `.xtls 0xEFE0C60` [V]

- Mismo resolvedor `0x2049B90`. Por actor, bajo su cerrojo: `[[actor+0x68]+0xB0]+0x180 = [etapa+0x30]` (la clave de
  la etapa) al activar; al desactivar se borra.
- Es la marca «NPC ocupado por esta escena». En la partida del anfitrión el NPC queda bloqueado para todos mientras
  el anfitrión habla con él, y el servidor también bloquea por objetivo (2671/2720, `p8-sistemas-juego.md`).
- **Decisión:** se queda como está. Nunca se vetó (es un thunk `.xtls`, solo probe), así que ya corre entero en X:
  activar y desactivar. Es coherente con el juego nativo: mientras el anfitrión habla con Renee, el invitado no
  puede. El resumen ahora cuenta `on` / `off` de X para ver que se emparejan.

### 2.3 Cómo envía el invitado su 2720 [V salvo lo marcado]

- **Escritor de 2720:** `.xtls 0xA44B3D0` (thunk `.rsrc 0xBB3D40`). El opcode no está literal:
  `mov ecx,[0xAEFAD07] ; add ecx,0xA98`, que con el valor del fichero da `0xAA0` = 2720 (el valor en ejecución del
  global: [H]). Formato igual que el de 2671 (`.xtls 0xA44A8D0`, thunk `0xBB3AB0`, `mov ecx,0xA6F`).
- **Único llamador:** `.xtls 0x9BC7720`, dentro de `0x9BC7640` (thunk `.rsrc 0x9B56D0`) = la ranura `+0x188` de
  `pa::ClientInteractionActorComponent` (vtable `0x55BDB30`). La ranura `+0x180` (`0x9B5EB0`) es la que manda el 2671
  (`InteractionPreemptionStepReq`).
- **Qué comprueba `0x9BC7640`** sobre el componente del **jugador** (`self`, `[self+8]` = su actor):
  - `[self+0x2A] < 5` (estado del paso);
  - `[self+0x28]` (u16) `!= 0xFFFF`: el índice de la interacción. Lo pasa a clave con `0x3888B0`, **la misma
    conversión** que usan la baja de SetInteraction y `E404A10` sobre `entrada+0x10`;
  - `[self+0x20] != 0`: la clave del objetivo;
  - y la conexión (`0x8B44E0`). Envía (clave propia, objetivo, clave de interacción, `[self+0x2C]`).
- **`0x9B5EB0` (2671)** exige `[self+0x2A] == 0`, `[self+0xD8] == 0` y un tiempo de espera (`[self+0x158]`); recibe
  el actor objetivo y el índice de interacción como argumentos.
- **Conclusión:** el 2720 no lee la lista `+0x88` del NPC: manda la interacción que el componente del jugador ya
  tiene elegida. Quién la elige (el aviso de interacción en pantalla) y si recorre `+0x88` del NPC es [H]. Lo
  apoyan [E]: el mismo tipo de índice (`0x3888B0`) y los datos de la sesión 11:
  - el anfitrión habló con Renee y Delkin con la interacción `0x000F4321` (H1 R323683, R345172, R412479);
  - el invitado mandó 19 2720 a otros objetivos (interacciones `0x2B2F`, `0xF4274`, `0x272C`, `0x35`, `0xF428C`) y
    recibió sus 19 2916: su ruta de interacción funciona;
  - con Renee y Delkin, 2 2857 a cada uno y 0 2720 (`personal/interaction_packets.csv`);
  - los registros de esas escenas en X se vetaron antes de la charla del anfitrión (G1 L2127–2129, L2135–2137).
- **Que `0xF4321` sea justo la entrada que añade SetInteraction:** [H]. El A/B de §5 lo decide.

### 2.4 Cifras de la sesión 11 [V, `personal/informe.txt` §3]

Últimos resúmenes: G1 `SetInteraction X116 / VETOED116`, G2 `X39 / VETOED39`; entre las etapas, butcher, seatdealer
y grocery. InputBlock X18 VETOED18 y UIControl X21 VETOED21 (G1) siguen siendo personales y siguen vetados.

## 3. Cambio

| Fichero | Qué |
|---|---|
| `guest_x_veto_logic.h` | `Act::Allow`; `FnSet` (máscara de `gxv::Fn`), `parse_allow` (nombres de `fn_info`, sin distinguir mayúsculas, separados por comas, punto y coma o blancos; `""`, `0`, `none` = ninguno; nombres desconocidos contados), `fn_act` (un manejador permitido da `Allow` donde daría `Would`/`Veto`), `fn_mode`/`fn_held_as_probe` con `allowed` (un permitido nunca llega a `On`, así que no necesita su salida verificada), `fn_dir`/`dir_name` (`r8b` de SetInteraction y BlockInteraction: add/remove, on/off) |
| `guest_x_veto.cpp` | `on_fn`: si el manejador está permitido, cuenta `ALLOWED` y no toca el contexto (corre nativo). Cuenta por dirección las llamadas de X. Línea `ALLOWED (guest_x_ui_veto_allow: runs natively) SetInteraction add …` o `… remove …` (INF, dentro del cupo de líneas). Resumen con `(add a / remove r)` y `ALLOWED n`. Línea de instalación con `allowed (…): SetInteraction`. El resto (veto forzado, `replay_way_out`, comprobación de bytes y FNV de los tramos) sin cambios |
| `guest_x_veto.h`, `config.{h,cpp}`, `mod.cpp`, `guest.cpp` | Clave `guest_x_ui_veto_allow`, por defecto `SetInteraction`, impresa en `Config: P8 X veto: …` y pasada a `guest_x_veto_start` |
| `tests/optd_x_veto_tests.cpp` | `test_allow`: análisis de la lista, modos, la charla de la sesión 11 (SetInteraction `Allow`; InputBlock, UIControl, UIShowMinimap, UIStageGuide, TimelineEvent `Veto`), las dos direcciones con la misma decisión, M nunca, lista vacía = sesión 11 |
| `docs/research/ini/cdmp.p8-s7-shadow.ini`, `-fase2.ini` | `guest_x_ui_veto_allow=SetInteraction` con su bloque. Fuera de comentarios siguen difiriendo solo en `guest_seq_play_live` |

**Emparejamiento.** La decisión es por función, nunca por dirección:
- SetInteraction hace alta y baja en la misma función y el mismo gancho: permitirla deja pasar las dos.
- InputBlock (bloquear / desbloquear) y UIControl siguen vetados en las dos direcciones, como antes.
- TimelineEvent sigue vetado al empezar; su final (vf9, `.xtls`) nunca se vetó (asimetría ya conocida,
  `p8-s10.md` §4.5). La lista no crea asimetrías nuevas: solo quita vetos de funciones enteras.
- Duplicados M + X [E]: si el invitado también tiene la escena del tendero en M, cada alta añade una entrada y cada
  baja quita una (`E404A10` quita la primera que coincide). Las cuentas cuadran; una baja de X puede quitar la entrada
  de M y dejar la de X (ver riesgos).

**Cómo volver atrás:** `guest_x_ui_veto_allow=` (vacío), `0` o `none` = comportamiento de la sesión 11.

## 4. Qué mirar en el próximo log del invitado

- **Arranque:**
  - `Config: P8 X veto: … guest_x_ui_veto '1' guest_x_ui_veto_allow 'SetInteraction' …`;
  - `GuestXVeto: guest_x_ui_veto_allow='SetInteraction': run natively for X (never vetoed, both directions): SetInteraction`;
  - `GuestXVeto (…): … handlers: 28 of 28 handlers hooked | veto: TimelineEvent … InputBlock … UIControl …
    UIShowMinimap … UIStageGuide … | allowed (…): SetInteraction | probe: …`. SetInteraction ya **no** aparece en
    `veto:`.
- **Al entrar en el mundo, cerca de la carnicería / tienda (las escenas 196/198 o las que toquen):**
  `GuestXVeto [dialogue] … ALLOWED (guest_x_ui_veto_allow: runs natively) SetInteraction add of manager 0x… (character
  0xA0100001; own 0xA01003xx …): another character's manager (X) | stage 'butcher…'` donde antes salía `VETOED`.
- **Resumen cada 60 s:** `… (5 vetoing, 1 allowed) … SetInteraction X n (add a / remove r) / M m / c ALLOWED n;`
  y `BlockInteraction X n (on a / off b) …`. Con la escena terminada, `add` y `remove` deberían quedar parejos por
  escena; un `remove` mayor que `add` también es posible (bajas de altas hechas en M).
- **La prueba:**
  1. El invitado se acerca a Renee / Delkin **sin** que el anfitrión hable con ellos: debe salir el aviso de hablar, y
     al pulsar, 2671 → 2663 → **2720 con la clave del invitado** → 2916, y el menú de la tienda en el invitado.
  2. El anfitrión habla con Renee con el invitado delante: el invitado no recibe menú, ni pierde cámara o movimiento
     (InputBlock / UIControl siguen `VETOED`). Mientras dure, el invitado no puede hablar con Renee (BlockInteraction
     `on`): es lo nativo. Al acabar, `BlockInteraction … off` y el invitado puede volver a hablar.
- **Si sigue sin 2720:** mirar en el log si hay `ALLOWED SetInteraction add` para la etapa del tendero **antes** de
  intentar hablar. Si lo hay y no sale el aviso, la causa está en otro sitio (la elección de interacción del cliente,
  §2.3 [H], o el servidor del anfitrión para el usuario 2).

## 5. Criterios de aborto

- **Vuelve el menú de diálogo o tienda del anfitrión en el invitado**, o el invitado pierde cámara / entrada durante la
  charla del anfitrión → `guest_x_ui_veto_allow=` (vacío) y mirar qué manejador corre con X en ese momento.
- **Una línea `ALLOWED` con `character` igual al propio del invitado** → error del predicado; `guest_x_ui_veto=probe`.
- **Cierre del juego cerca de una línea `SetInteraction`** (el aviso de crash nombra `0xE402CC0`, `0xE404A10` o
  `0xE411950`) → `guest_x_ui_veto_allow=` (vacío).
- **Renee / Delkin bloqueados para siempre** (BlockInteraction `on` sin `off` tras la charla) → no lo arregla esta
  clave; anotar y seguir (es del ciclo de vida de las escenas de X).
- **Aviso de hablar sobre el Kliff del anfitrión** en el invitado (SetInteraction resuelve el dueño de X) → anotar;
  si molesta, volver a vacío.

## 6. Riesgos

- **[H] Duplicados M + X.** Si el invitado tiene la misma escena en M y en X, el NPC puede tener la entrada dos
  veces (¿dos avisos?). Y como `E404A10` quita la primera coincidencia, una baja de X puede dejar la entrada de X
  (con la etapa y la condición del anfitrión) en lugar de la de M.
- **[H] La entrada que queda puede apuntar a la etapa de X** (`entrada+0x20` = `[etapa+0x30]` de X). Si la charla
  del invitado usa esa etapa, podría arrancar en la escena del anfitrión en vez de en la suya.
- **[H] Altas sin baja si X desaparece sin pasar por la baja** (salida del invitado, borrado de la escena). Las
  referencias son con refcount (no hay puntero colgante), pero el NPC conservaría la interacción.
- **[E] El dueño de X recibe interacciones.** El selector especial resuelve el Kliff del anfitrión; una escena que se
  las ponga al dueño se las pondría al anfitrión remoto en el invitado.
- **[V] TimelineEvent sigue vetado entero.** Según el informe, una de sus ramas manda acciones a la IA de los NPC
  (mundo). No se toca aquí: no hubo llamadas en X en la sesión 11. Si aparecen, se puede probar
  `guest_x_ui_veto_allow=SetInteraction,TimelineEvent` (su final ya corre siempre).
- No hay ganchos nuevos ni bytes nuevos: los 28 puntos y sus comprobaciones son los de `p8-s10.md` §4. Todo dentro
  de `[0x1000, 0x51EB000)`; `.xtls` solo se leyó.

## 7. Compilación y pruebas

- `.\scripts\build.ps1`: compila sin avisos y **40/40 pruebas**; `optd_x_veto` añade `test_allow`.
- No se lanzó el juego. Necesita Shadow (P8) para el A/B de §4.
