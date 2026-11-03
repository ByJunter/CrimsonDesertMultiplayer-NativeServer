# Quién controla los *gimmicks* y cómo dar al usuario 2 los suyos

12 de octubre de 2026 (noche), rama `opcion-d`. Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`),
direcciones en RVA. Investigación de solo lectura: no se tocó `src/`, no se instaló nada y no se abrió el juego.
Parte de `gancho-cuerda.md` §12.

- **[V]** comprobado (captura, log del mod o decompilado/desensamblado de esta build, citado al lado).
- **[E]** deducción fuerte que falta medir en vivo.
- **[H]** hipótesis.

## Fuentes y relojes

| Id | Fichero | Qué |
|---|---|---|
| G27-1 | `…\scratchpad\s27-shadow\cdmp_packets_20261012-210724\` | invitado, paso 1 (`0xA010033E`, usuario `0x9010033D`) |
| G27-2 | `…\scratchpad\s27-shadow\cdmp_packets_20261012-213205\` | invitado, paso 2 (`0xA0100377`, usuario `0x90100376`) |
| H27-1 / H27-2 | `C:\Emulador CD\backups\p8-sesion27-host\cdmp_packets_20261012-220503.*` / `…222919.*` | anfitrión (`0xA0100001`, usuario `0x90100000`) |

- Relojes [V]: t(G27-1) = 0 a las 21:07:54,882 (log del invitado, «capture t=10,882066» a las 21:08:05,764); t(G27-2) = 0 a
  las 21:32:30,625 (dos anclas, 95,238 y 799,738, dan lo mismo a 1 ms). t(H27-2) = t(G27-2) + 171,7 s (§12; lo vuelven a
  confirmar los ocho `RemoveProjectileAck` de §2.4).
- Scripts nuevos en `…\scratchpad\gimmicks\` (Python `-I`, entrada no fiable, solo datos): §5.

---

## 0. Resumen

1. **Quién manda en un *gimmick* [V código].** El servidor fija el **controlador** (`actor+0x90`) una sola vez, al
   crearlo (`0x2791CD0`, sitios `0x2792A11–0x2792A7B`). Si no hay otro dato, usa **el dueño del campo**
   (`[campo+0x180]`). Así que, en el campo del anfitrión, **todo** *gimmick* sale con el controlador `0x90100000`,
   incluido el dispositivo acoplado al personaje del invitado, y da igual quién lo pidiera. En el mundo propio del
   invitado, sus 8 dispositivos salían con su usuario. El cliente recibe ese valor en el 2005 (`rec+0x12`, el «+0x14» de
   §12) y lo guarda en su `actor+0x90`.
2. **La comprobación del cliente [V código].** Un cliente solo pide o aplica eventos de un *gimmick* si
   `[[jugador local]+0xA0]+0x60` (su usuario) `== [gimmick]+0x90`: `0x8E59D0` (vf `+0x740`, también la usa el 2297),
   `0x8E5F60` y `0x9065A0`. Si no lo es, devuelve `eErrNoActorNotExist` (`0xD65F8D70`, sitio `0x8E5A49`). El servidor
   comprueba lo mismo: `0x2AC1270` (2758/`UpdateGimmick` en `0x2AC0790`, 3082/`ExecuteFrameEvent`) ignora en silencio
   las peticiones que no son del controlador.
3. **Por eso [V datos]:**
   - el invitado manda **0** `GimmickExecuteFrameEventReq` 3082 y **0** `GimmickBranchStateReq` 2609 para los 5184
     *gimmicks* del anfitrión, también para su propio dispositivo (`0xB01004A1`);
   - el anfitrión manda 48 014 y 5301, y entre ellos los del dispositivo del invitado (208 y 69).

   Sí manda lo que no pasa esa comprobación: `MoveActorReq`, `RotateActorReq` y `RegionInOutReq`, y un 2758 por
   *gimmick* al crearlo (4882). El servidor rechaza esos 2758 **sin ningún Nak ni error de juego** en ninguno de los dos
   PC.
4. **Qué explica:**
   - **La cuerda propia invisible:** el dispositivo del invitado (`B010049B–B01004A2`) tiene como controlador al
     anfitrión. El cliente del invitado nunca ejecuta su máquina de estados.
   - **`Gimmick RelaseCatch … 실패`:** el código `0xD65F8D70` del log es exactamente el rechazo de `0x8E59D0`. En las 8
     ventanas comprobadas llega con un `GimmickCatcherUpdateAck` 2356 de un *gimmick* recogedor del anfitrión y un NPC
     «Stranger».
   - **`Projectile을 생성하는데 실패`:** es el proyectil de ese mismo «Stranger», que el cliente del anfitrión crea y
     borra en 0,2 s. El `OnFailedToCreateProjectileReq` 2935 del invitado llega cuando ya se había borrado, así que no
     cambia nada.

   Estos dos errores son ruido de observador. No es la cuerda.
5. **Objetos que van y vienen [V datos, E código]:** es un ping-pong. Cualquier cliente quita un objeto de nivel con
   `SceneLevelOnDemandGimmickUnspawnReq` 2221, y el servidor lo quita **para los dos** (`0x29C2200` → `0x282E6A0` no mira
   quién lo pidió ni si el otro está cerca). El otro lo vuelve a pedir con una clave nueva:
   - S27-2: **1406 veces en 13 min**; mediana 0,27–0,34 s entre la baja y el alta;
   - S27-1: 2373 veces.
6. **Arreglo de raíz (§3).** Clave `[session2] docked_gimmicks = 0 | probe | 1 | 2`, en fases:
   - **probe:** solo log, en los dos PC.
   - **1, «manejo local»:** cada cliente maneja **en local** los *gimmicks* acoplados a un personaje de jugador que no
     controla. Para ello reescribe `rec+0x12` del 2005 antes de entregarlo y **tira** sus Req de estado para esas
     claves, de modo que el servidor nunca vea dos controladores. Con esto el invitado ve su dispositivo y su cuerda (y
     los del anfitrión), y el anfitrión no cambia.
   - **2, autoridad en el servidor:** los acoplados heredan el controlador de su padre: el usuario 2 para los suyos.
     Gancho en la entrada de `0x28AD170`/`0x28ADE40` y `create_mid` en `0x2792A7B`. El anfitrión pasa a manejarlos en
     local con la misma lógica de la fase 1.

   El ping-pong lleva su propio arreglo, un filtro de bajas en el servidor (§3.6).

---

## 1. Cómo decide el juego el controlador (pregunta 1)

### 1.1 Servidor: el controlador se fija al crear y no cambia [V]

`ICreateServerActorDesc::vf3` `0x2791CD0`, desensamblado de `0x2792A11–0x2792A7B` (`r14` = actor nuevo, `rsi` =
descriptor, `r13` = campo):

```
0x2792A11  mov eax,[r14+0x90] ; test ; jnz 0x2792A7B      ya tiene controlador: se queda
0x2792A1C  rax=[rsi+0x40]  -> eax=[rax+0x60]               actor/usuario que indica el descriptor
0x2792A2A  rax=[r14+0xA0]  -> eax=[rax+0x60]               usuario dueño del actor
0x2792A70  eax=[r13+0x180]                                 DUEÑO DEL CAMPO
0x2792A7B  mov [r14+0x90],eax                              escritura del controlador
```

- **No hay ninguna reasignación para *gimmicks*.**
  - `ChangeAiControlAck` 2508 solo lo emite un truco; en el cliente (`0xB681F0`) cambia el grafo de IA y no toca
    `+0x90`.
  - `OverriedOwnershipStateTypeyAck` 2168 cambia el byte `comp+0x61` (estado de propiedad), no el controlador, y sale 0
    veces en S27.
- **Equipo acoplado** (`OnDemandEquipDockingGimmickSpawnReq` 2249):
  1. `0x2970790` busca el actor de la clave pedida y llama a `0x28AD170([[actor+0x68]+0x170] = componente de acople,
     …)`.
  2. Para cada hueco, `0x28AEC70` prepara un descriptor de 0x5A0 B (constructor `.xtls 0x1500F8790`, `cdmp_GimmickPrepare`
     `0x278F490`).
  3. Al final crea todos los del lote con la vf `+0x88` del campo del personaje (`local_98`, de `0x28FB8B0`), que acaba
     en la vf3.
  4. El descriptor no lleva `+0x40` y el *gimmick* no tiene dueño (`rec+5 = 0` en los 2005), así que toca **el dueño
     del campo**.

  El otro camino de acople (2973) es `0x29711B0` → `0x28ADE40`. `0x28AD170` tiene además un llamador en la tabla
  `.trace` (`0x17204EE8`, virtualizado).
- **Da igual quién lo pida [V].** El dispositivo del invitado `B01004A1` (H27-2 t 237,087) lo creó la 2249 **del
  anfitrión** para `0xA0100377` (t 236,506). Cada cliente pide el acople de todos los personajes que ve (§2.6).
- **Datos [V]** (`who.py`, primeras apariciones de tipo 7):

  | Captura | Controlador del anfitrión | Controlador del invitado |
  |---|---|---|
  | G27-1 | 6726 | 9 (su mundo propio) |
  | G27-2 | 5184 | 9 (su mundo propio) |
  | H27-1 / H27-2 | 6802 / 5272 | 0 |

  El dueño (`rec+5`) es 0 en todos. En ninguna captura cambia el controlador de una clave entre dos 2005.
- **Validación en el servidor [V código]:**

  | Req | Lógica | Comprobación | Si no es el controlador |
  |---|---|---|---|
  | 3082 `GimmickExecuteFrameEventReq` | `0x29E0970` → `0x2AC1270` | `[gimmick+0x90] == [[sesión+0xA0]+0x60]` | `return` mudo |
  | 2758 `UpdateGimmickReq` | `0x29DE610` → `0x2AC0790` | la misma | código `eErrNoActorNotExist`. En el cable no sale nada: 0 Nak en las cuatro capturas y ninguna línea de error en el log del anfitrión por los ~4900 2758 rechazados del invitado en cada sesión [V] |
  | 2609 `GimmickBranchStateReq` | `0x29E0610` | — | el servidor no aplica nada (cuerpo vacío) |
  | 2774 / 3089 (`BodyMotion` / `Constraint`) | `0x29E5050` / `0x29E1E00` → `0x2A987C0` | **ninguna** (§6.1) | se aplican: 2774 escribe `comp+0x42D` |
  | 2779 `CreateProjectileFromGimmickEventReq` | `0x2A3E370` | ninguna en el primer nivel (§6.1) | crearía el proyectil |

  `UpdateGimmickAck` 2297 va `ToTarget` al controlador (`p8-arquitectura-observador.md`): 154 en H27-2 y 0 en G27-2.

### 1.2 Cliente: «¿lo controlo yo?» [V código]

- El cliente copia `rec+0x12` del 2005 a su `actor+0x90` al crear el actor (`mirror-ack-formats.md`;
  `guest_s3_logic.h` lo lee así).
- **`byte actor+0xC8` no sirve para esto.** `0x4FA530` pone 3 a todo actor de tipo 7 y sale **antes** de mirar
  `+0x90`. `cdmp_Actor_IsLocallyControlled` `0x17F2A50` devuelve falso para el tipo 7.
- **La comprobación de verdad**, la misma en los tres sitios:

  ```
  jugador = cdmp_ClientActorManager_GetLocalPlayer(WorldSystem+0x30)   (0x8B44E0)
  mio     = [[jugador+0xA0]+0x60]                                       (su usuario: 0x90100376 en el invitado)
  if (mio != [gimmick+0x90]) -> error eErrNoActorNotExist               (0x8E5A41 cmp / 0x8E5A49)
  ```

  | Función | Qué es | Llamadores |
  |---|---|---|
  | `0x8E59D0` | vf `+0x740` de `ClientGimmickActorComponent` (vtable `0x55B7898`): copia el evento de 0xE8 B, `actor+0x60` en `+0x20`, y lo encola en `comp+0x468` o `WorldSystem+0x20 +0xF60` | la vf148 `0x90D9B0` → `.xtls 0x9A06500` (evento de suelta o agarre), el manejador de `UpdateGimmickAck` `0xBAD340` |
  | `0x8E5F60` | variante con cola `comp+0x478` | `0x909DC0` (vf `+0x7E0`), `0x8EC720` |
  | `0x9065A0` | aviso con la clave del *gimmick* (`thunk .xtls 0xA42E8F0`) | `0x909620` (vf `+0x7D0`) |

- El emisor del 3082 es `0xBAAD90` (`PostPacket` con opcode `0xC0A`). Solo se llega a él por la tabla `.trace`
  (`0x17132468`, Denuvo), así que no lo he podido seguir más arriba.
- **El código de error lo confirma** (`hl.py`, `hashlittle(nombre, 0xC5EDE)`): el global `0x6CF6B68` (mal nombrado
  `g_Err_SaveSlotOwnerNull`) se registra en `0x21FC0F9` como **`eErrNoActorNotExist` = `0xD65F8D70`**, y ese es el
  `code 3596586352` de cada «Gimmick RelaseCatch» del invitado.
- **Lo que manda cada cliente [V]** (`byop.py`, G27-2 / H27-2; Req / claves):

  | Req | Invitado, *gimmicks* del anfitrión | Invitado, sus 9 del mundo propio | Anfitrión, todos |
  |---|---|---|---|
  | 3082 `GimmickExecuteFrameEvent` | **0** | 90 / 9 | 48 014 / 5055 |
  | 2609 `GimmickBranchState` | **0** | 10 / 9 | 5301 / 5060 |
  | 2758 `UpdateGimmick` | 4882 / 4882 (uno al crearlo) | 12 / 9 | 10 717 / 5060 |
  | 2774 / 3089 | 548 / 288 | 8 / 0 | 493 / 373 |
  | 2556 / 2828 / 3067 (transformada) | 24 545 / 14 083 / 31 368 | 1 / 0 / 0 | 28 071 / 20 611 / 31 687 |

  Los Req de **estado** solo salen del controlador. Los de **transformada** y el 2758 de creación salen de todos.

**Corrección a §12.2 de `gancho-cuerda.md`.** El invitado **sí** tiene el dispositivo del anfitrión:
`B0100151–B0100156`, en el 2005 de G27-2 t 64,4, con controlador `0x90100000`. No lo maneja: solo manda 520
`MoveActorReq` y 45 `RegionInOutReq` de `B0100156`, como de cualquier *gimmick*. La predicción de que «ni siquiera lo
tiene» era falsa. Que en su pantalla se vea quieto sigue siendo [H].

---

## 2. Qué *gimmicks* toca el invitado y quién los maneja (pregunta 2)

### 2.1 Tabla [V datos salvo marca]

| Clase | Ejemplo (S27-2) | Controlador en el campo del anfitrión | Quién ejecuta su máquina de estados | Síntoma en el invitado |
|---|---|---|---|---|
| **Equipo acoplado a su personaje** («Game Mechanic», sockets `RemoteCatchPivot`…) | 8: `B010049B–B01004A2` (en su mundo, `B0100438–3F`, controlador suyo) | anfitrión | el **anfitrión** (de `B01004A1`: 208 × 3082, 69 × 2609, 82 × 2758); el invitado, nada | Su lanzador no se despliega y la cuerda no se ve [E visual; informe del usuario] |
| Equipo acoplado al personaje del anfitrión | `B0100151–56` (+ `B0101CC7/CEB/CEC` a los 661–678 s) | anfitrión | el anfitrión | [H] quieto en el invitado (§3.7, comprobación 2) |
| Acoplados y recogedores de NPC («Stranger», «Mushroom Specialist», «Herbalist», hueco `0x0E`) | `B01005A3/A4/A7/A8` con los Stranger `B01005A1/A2/A5/A6` | anfitrión | el anfitrión (15 × 2826 `GimmickCatcherUpdateReq`) | Los errores de §2.4 |
| Objetos de nivel bajo demanda (árboles, cajas…) | 2928 / 2221 / 2145 | anfitrión (aunque los pida el invitado) | el anfitrión | Van y vienen (§2.5). El doblado del árbol es otra cosa: solo de cliente (§12.3) |
| Interacciones (cofres, mesas, botín…) | 16 `InteractionPreemptionStepReq` / `DoStepReq` del invitado → 16 / 16 éxitos (2663 / 2916) | servidor (por usuario) + estado guardado `SyncFieldGimmickSavedByGimmickStateAck` 2900 ToNear | servidor; la animación, el controlador | Funcionan [V]; si la animación se ve en el invitado es [H] (lo mide la sonda, §3.4) |
| Proyectiles de NPC | `CreateProjectileAck` 2250 de un Stranger | la IA de su NPC (anfitrión) | el anfitrión | «Projectile … 실패» (§2.4) |

### 2.2 La cuerda propia invisible [V cadena; E pantalla]

1. El dispositivo del invitado se crea en el campo del anfitrión con su controlador (§1.1).
2. En el cliente del invitado, `0x8E59D0`/`0x8E5F60` rechazan cualquier evento de ese dispositivo. No sale ningún 3082
   ni 2609, y ni el despliegue (`003ECC59`) ni el enganche (`DA667CC9`) se ejecutan en su pantalla.
3. La cuerda del componente `RemoteCatch` (`remote_rope`, §12.1) sí se engancha, pero sale de un `RemoteCatchPivot`
   que no se ha desplegado.
4. En el anfitrión se ve porque **su** cliente maneja ese dispositivo a partir de las acciones que le llegan del
   invitado (13 despliegues, 8 enganches y 8 sueltas en S27-2, §12.2).

Es la misma relación que en el mundo propio del invitado, pero al revés. Allí su usuario era el controlador y sí lo
manejaba (90 × 3082 en t 35,4–36).

### 2.3 Acople en bucle (2249 / 2829) [V datos; H causa]

- Cada cliente repite sin parar la 2249 del personaje del **otro** y recibe el Nak 2829 (`eErrNoFailedToSpawnDockingActor`).
  Para su propio personaje no la repite.

  | Sesión | Invitado → personaje del anfitrión | Anfitrión → personaje del invitado |
  |---|---|---|
  | S27-2 | 6770 veces (t 64,7–797,3, huecos 2/15/20) | 861 en 51 s |
  | S27-1 | 12 619 | 1404 en 20 min |

- Los dos repiten también la de los NPC de cesta (hueco `0x0E`), y eso ya pasaba en una partida de un jugador sin el
  mod.
- **[H]** El hueco del personaje remoto no se da por lleno porque el cliente no controla el *gimmick* acoplado. La fase 1
  lo pone a prueba: si es así, el invitado debería dejar de repetir la 2249 de `A0100001`.

### 2.4 Los errores de juego del invitado [V]

| Error | G27-1 / G27-2 | Lo que hay en la captura (`win2.py`) |
|---|---|---|
| `Gimmick RelaseCatch 시점의 기믹 이벤트 호출에 실패` (`exe+0x20AD762`, código `0xD65F8D70`) | 9 / 9 | En las 8 ventanas comprobadas (G27-1 137,9 / 347,0 / 584,4 / 884,1 / 1232,0; G27-2 87,2 / 320,0 / 385,2 / 451,5) hay, a menos de 0,1 s, un **`GimmickCatcherUpdateAck` 2356** de un «Game Mechanic» del anfitrión con un **Stranger**, por ejemplo `B01005A4 ← B01005A1`. Al aplicarlo, la suelta del agarre (`0x20AD400`) llama a la vf148 del *gimmick* → `0x8E59D0` → rechazo por no ser el controlador |
| `Projectile을 생성하는데 실패` (`exe+0x21DF65A` en `0x21DF510`) | 6 / 8 | Llega un `CreateProjectileAck` 2250 de un Stranger; `0x21DF510` no obtiene objeto de la vf `+0x188` del componente y el invitado manda **`OnFailedToCreateProjectileReq` 2935** |

- **Lo que hace el anfitrión** con esos proyectiles: los crea, los mete en el recogedor (2826), los termina
  (`OnAirFinish` 2699) y los quita (2399 → 2751), todo en 0,15–0,25 s.
- **Los 2935 del invitado no tienen efecto.** En los 8 casos (S27-2: 87,24 ×4; 319,98 ×2; 451,46; 747,35) el borrado
  del anfitrión ya había salido o sale a la vez, dentro de ±0,05 s del ajuste de relojes. Por ejemplo, `B01005A6`: el
  2751 del anfitrión a los 258,71 y el 2935 del invitado a los 258,94 (hora del anfitrión).
- **Por qué falla el proyectil:** es [H]; quizá el componente de proyectiles de un NPC remoto no tenga el tipo
  necesario.

Ninguno de los dos errores viene de los acoplados del invitado. Son lo normal en un cliente que **no** controla al
NPC ni a su recogedor: el juego lo anota y sigue, sin ningún paquete. **No hay que arreglarlos cambiando quién manda;**
como mucho, contarlos.

### 2.5 Objetos que van y vienen (2928 / 2221) [V datos; E código]

`pingpong.py` sigue los `UpdateOnDemandGimmickAck` 2145 (`04 | actor | u8 estado | id de escena`):

| Sesión | Uno lo quita, el otro lo había creado | El otro lo vuelve a crear | Mediana hasta crearlo de nuevo |
|---|---|---|---|
| S27-2: el anfitrión quita / el invitado crea | 1175 | 1094 | 0,27 s |
| S27-2: el invitado quita / el anfitrión crea | 311 | 312 | 0,34 s |
| S27-1: el anfitrión quita / el invitado crea | 1721 | 1689 | 0,27 s |
| S27-1: el invitado quita / el anfitrión crea | 681 | 684 | 0,22 s |

- **El código [E primer nivel].**
  - `0x29C2200` → `0x282E6A0` solo comprueba que quien pide está en el juego (vf178) y, por cada clave, que es tipo 7,
    que no está ocupado (`0x210D730`) y que tiene uuid.
  - Después la saca: `ServerActor__Logout`, con su `RemoveActorAck` a los dos.
  - No mira quién la creó, quién la controla ni si el otro usuario sigue cerca.
  - Una alta repetida solo recibe «ya existe» (2953). No hay ningún recuento de quién lo quiere.
- **Efecto.** Cada vuelta destruye el objeto y lo vuelve a crear con una **clave nueva** en los dos clientes. Quien
  sigue cerca lo ve desaparecer unos 0,3 s y se pierde lo que hubiera sobre él (por ejemplo, el árbol que tiene
  enganchado: `interacciones-mundo.md`, intento 1). Que quite más el anfitrión que el invitado encaja con lo que se ve:
  descarga lo que está fuera de **su** radio, que suele ser lo que tiene cerca el invitado.

---

## 3. Arreglo de raíz (pregunta 3)

### 3.1 Idea

Hay que separar dos cosas que hoy van juntas:
- **autoridad**: el controlador en el servidor, que decide qué peticiones se aceptan;
- **manejo local**: qué cliente ejecuta la máquina de estados para dibujarla.

El juego no tiene ningún relevo de observador para el estado de un *gimmick*. 3082 y 2758 no se reenvían, 2297 va solo
al controlador y `0x8E59D0` rechaza aplicarlo a quien no controla. Con dos jugadores, quien no controla no ve nada.
Hoy ya hay un caso que funciona: **el anfitrión maneja el dispositivo del invitado a partir de las acciones que le
llegan de él**. La propuesta es generalizarlo.

- **Regla de manejo local (fase 1, los dos PC).** Un *gimmick* acoplado a un **personaje de jugador** lo maneja en local
  todo cliente que lo dibuja. Las Req de estado de quien no tiene la autoridad no salen de su PC. Así el servidor nunca
  recibe dos controladores.
- **Regla de autoridad (fase 2, solo en el anfitrión).** Un *gimmick* acoplado hereda el controlador de su padre. Es la
  relación nativa del mundo propio del invitado y la única en la que los efectos de servidor del equipo del usuario 2
  (cooldowns, consumo, eventos) los decide el cliente de quien juega.
- **Fuera del alcance:** dar al invitado la autoridad sobre los *gimmicks* del mundo con los que interactúa (puertas,
  cofres, recogedores de NPC). No hay ningún traspaso nativo: el controlador solo se escribe al crear, en el servidor y
  en el cliente. Las interacciones ya son del servidor y funcionan (16 / 16). Se decide con datos de la sonda (§3.4).

### 3.2 Fase 1: `docked_gimmicks = 1`, manejo local (los dos PC; el anfitrión no cambia nada)

**Al recibir un 2005** (invitado: `guest.cpp`, bucle de entrega, antes de `guarded_on_recv`, ~l. 858; el `item.packet`
del lote se puede modificar). Para cada registro:
- tipo `== 7`;
- nombre de longitud `n = u32 rec+0x1E` (`< 64`); el padre está en `ne+0xAC` **y** en `ne+0xC6`, con
  `ne = 0x22 + 2n`;
- las dos claves son iguales y la clave es de un actor de **tipo 1** que este cliente ya recibió;
- `rec+0x12` es distinto del usuario local.

Entonces:
1. Se escribe el usuario local en `rec+0x12` (mismo tamaño; el resto del 2005 queda igual).
2. La clave pasa al conjunto **«solo local»**.

Comprobación del desplazamiento [V] (`docked.py`, `parent7.py`):
- con «Game Mechanic» (`n = 13`) el padre cae en `+0xE8`/`+0x102`;
- G27-2: 8 + 8 registros con padre `A0100377` y 9 con `A0100001`; H27-2: 8 + 15;
- los mismos desplazamientos dan la clave del NPC en 255 registros acoplados a NPC;
- los registros no acoplados tienen 0 o claves distintas en esos dos sitios.

La sonda comprueba en vivo que ese padre es `comp+0x2B4` del componente cliente (`0x8D8940` lo usa como clave de acople).

**Al enviar.** Se tiran (no se encolan en `g_to_host`; en el anfitrión, §3.3) las Req de estado cuya clave en `+0`
está en el conjunto «solo local»:
- 3082, 2758, 2609, 2774, 3089, 2779, 2826 y 2618 (lista final y su justificación en §6.1).

`MoveActorReq` 2556, `RotateActorReq` 2828, `RegionInOutReq` 3067 y `GamePlayTriggerInOutReq` 3068 siguen como hoy:
hoy ya salen de los dos clientes para todos los *gimmicks*.

**Qué se espera:**

| | Antes | Con la fase 1 |
|---|---|---|
| Dispositivo y cuerda del invitado, en su pantalla | Quietos | Se despliegan con sus acciones, igual que en su mundo |
| Dispositivo del anfitrión, en el invitado | No se maneja | Se maneja con las acciones del anfitrión que le llegan, igual que hace hoy el anfitrión con el del invitado |
| Servidor | Autoridad: el anfitrión | Sin cambios: sigue siendo el anfitrión, que sigue manejándolos |
| Peticiones nuevas en el cable | — | Ninguna |
| Bucle de 2249 | Sí | [H] puede desaparecer (§2.3) |

**¿Por qué reescribir el 2005 y no `actor+0x90` en caliente?** Al crearse un *gimmick* controlado, el cliente lo
inicializa: un 2609, unos 8 × 3082 y un 2758 (`opcion-d-login.md`, F4). Cambiarlo después dejaría la máquina sin
inicializar **[E]**. El 2005 se reescribe antes de que el juego lo lea.

### 3.3 Fase 2: `docked_gimmicks = 2`, autoridad del usuario 2 (solo el anfitrión; el invitado en 1)

- **Gancho A**, entrada (`safetyhook::create_inline`) de `0x28AD170` y de `0x28ADE40`.
  - Prólogos sin direccionamiento relativo a `rip`: `48 89 5C 24 18 | 48 89 54 24 10 | 48 89 4C 24 08 | 55 | 56 | 57`.
  - Corren en el hilo del servidor.
  - Lecturas protegidas: `padre = [rcx+8]`, `ctl = [padre+0x90]`, tipo `[[padre+0x88]+1]`.
  - Si el tipo es 1 (o 5/6 con controlador del usuario 2: monturas y mascotas, que ya controla él) y `ctl` es la
    clave del usuario 2, se apunta en una variable por hilo `{padre, ctl}`. Se llama al original y se borra.
- **Gancho B**, `create_mid` en **`0x2792A7B`** (`41 89 86 90 00 00 00`, una sola instrucción de 7 B; es destino de
  `0x2792A1A` y la siguiente, `0x2792A82`, también, así que no toca otro destino).
  - Si hay contexto, `eax == [r13+0x180]` (o sea, se iba a poner el dueño del campo) y `[[r14+0x88]+1] == 7`, entonces
    `rax = ctx.ctl`.
  - El primer `cmp` es la variable por hilo, para no frenar el resto de creaciones.
- **Resultado esperado.** El 2005 de esos *gimmicks* sale ya con el controlador `0x90100376` hacia los dos clientes. En
  H27-2, el 2005 salió 0,58 s después de la 2249 que lo creó (t 236,506 → 237,087), así que no hace falta volver a
  anunciar nada.
  - **El invitado** recibe sus acoplados como suyos y los maneja de forma nativa. El servidor acepta sus 3082 y 2758
    (misma comprobación que en §1.1, ahora a favor) y le manda sus 2297 por la sesión 2.
  - **El anfitrión** deja de controlarlos. La regla de la fase 1 en su cliente los reescribe a `0x90100000` y los
    marca «solo local». Hay que ponerla en el camino de entrada del cliente del anfitrión: el `OnRecvPacket` del
    sniffer.
  - Las Req del anfitrión para esas claves se tiran. Si eso no se puede hacer limpio en `PostPacket`, se dejan llegar:
    el servidor las rechaza **en silencio** ([V] para 3082/2758; para 2774/3089, §3.5 R2).
- **Lo que no se toca:** los *gimmicks* acoplados a NPC y los de mundo siguen con el dueño del campo.

### 3.4 `probe` (solo log, los dos PC): lo que hay que medir antes de 1 y de 2

Nada de esto rechaza ni cambia nada. Se mide primero.

| Dónde | Qué registra | Para qué |
|---|---|---|
| Invitado y anfitrión: entrega del 2005 | Por cada registro tipo 7 acoplado a un tipo 1: clave, padre, `rec+0x12`, usuario local y «reescribiría». 1 s después, con lectura protegida en el cliente: `actor+0x90` y `[[actor+0x68]+0x30]+0x2B4` | Valida el desplazamiento del padre y que `+0x90` es el del 2005 |
| Los dos: entrada de `0x8E59D0` y `0x8E5F60` (prólogos `48 89 5C 24 10 | 4C 89 44 24 18 | 55 56 57` / `48 89 5C 24 10 | 4C 89 4C 24 20 | 4C 89 44 24 18 | 48 89 4C 24 08`) | Clave, `controlado = (mio == +0x90)`, hash del evento (`u32` de `r8`), clase (acoplado propio / remoto / mundo / NPC). Contadores por clase y hash, con resumen cada 30 s | Comprueba **[V en vivo]** que el invitado intenta los eventos de su dispositivo (`003ECC59`, `DA667CC9`…) y que se le rechazan. Mide en qué otros *gimmicks* le pasa: puertas, recogedores… |
| Anfitrión: gancho A + `0x2792A7B` | Padre, su controlador, el dueño del campo, cada actor creado en el contexto (clave, tipo, `eax`) y «pondría X» | Confirma que el acople pasa por la vf3, y qué claves y cuántas (8 por personaje) |
| Anfitrión: entrada de `0x2AC1270` y `0x2AC0790`, y de los manejadores 2774 `0x29E5050` y 3089 `0x29E1E00` | Usuario que pide, controlador y aceptado o rechazado, por clase | Cuánto se rechaza hoy; si 2774/3089 se aceptan de quien no controla (R2) |
| Anfitrión: entrada de `0x29C2200` | Quien pide, las claves, si el otro usuario lo creó o lo pidió (2928/2145) en los últimos N s, y la distancia de cada personaje al objeto | Prepara §3.6 |
| Invitado: contadores | 2249 por clave de personaje y 2829 recibidos | Contrasta §2.3 en la fase 1 |

### 3.5 Riesgos

- **R1, dos clientes manejando un *gimmick*.** En el servidor no ocurre: las Req de quien no tiene la autoridad no
  salen de su PC. En pantalla, cada uno lo maneja con lo que ve de las acciones del padre, así que pueden diferir
  unas décimas. Hoy ya pasa con el anfitrión y el dispositivo del invitado.
- **R2, validación en el servidor.**
  - 3082 y 2758 exigen controlador [V].
  - 2774, 3089 y 2779 **no la exigen** (§6.1). Hoy el invitado ya manda 548 y 288 de los dos primeros sobre *gimmicks* ajenos.
  - Con la regla de tirar, las claves nuevas no añaden ninguno. La sonda dice si hay que tirar también los de los
    *gimmicks* de mundo.
- **R3, efectos de cliente del manejo local.** Un evento del dispositivo podría lanzar algo de cliente que no sea
  una Req (sonido, efecto, un `CreateProjectileFromGimmickEventReq` 2779, que se tira). Vigilar errores de juego
  nuevos y el recuento de 2779 en el paso 2.
- **R4, momento de creación.** La fase 2 solo afecta a lo que se crea después. Un acoplado creado antes del cambio
  conserva su controlador. Con la clave en el `.ini` desde el arranque, no pasa.
- **R5, salida del usuario 2.** Los acoplados con controlador el usuario 2 deberían irse con su personaje (2451 /
  cierre de sesión) [H]. La sonda, al cerrar la sesión 2, lista los actores con `+0x90` = usuario 2 que queden.
- **R6, un falso positivo del padre.** Se exigen las dos copias iguales y que sea un tipo 1 conocido. La sonda lo
  valida contra `comp+0x2B4`.
- **R7, gancho en una función muy usada** (`0x2792A7B` crea todos los actores). La primera instrucción es una
  comparación con la variable por hilo. Todas las lecturas van protegidas.
- **R8, informes a Pearl Abyss.** Ningún fallo esperado. Se instala como `remote_rope`: hilo propio, 3 s en el mundo,
  comprobación de bytes y `REFUSED` si no cuadran.

### 3.6 Objetos que van y vienen: filtro de bajas en el servidor (anfitrión)

En la entrada del servidor (el gancho de `OnRecvPacket 0x2773C20` que ya ven las dos sesiones), un registro de quién
quiere cada objeto de nivel:
- usuario U lo quiere tras su 2928 del id S;
- la clave K se asocia a S por el uuid del servidor (`[[K+0x68]+0x30]+0x30`, el mismo que usa `0x282E6A0`) o por el
  2145 de alta;
- U deja de quererlo tras su 2221 de K.

Una 2221 de U se tira si el **otro** usuario aún lo quiere; si no, sigue. Es el recuento de referencias que le falta
al servidor de un solo jugador.
- El cliente que pidió la baja no la echa en falta: hoy ya convive con objetos que creó el otro (el 65 % de los Nak
  2953 del invitado son de eso, `p8-s14-viajeros.md` §1.3), y si vuelve a acercarse recibirá un 2953.
- Antes, `probe`: con §3.4 se mide cuántas 2221 se habrían tirado y si alguna dejaría vivo un objeto que nadie quiere.
- No hace falta ningún umbral de distancia.

### 3.7 Verificación

**En un PC con `cdmp_peer` (anfitrión real + invitado sintético):**
- Se usa el guion `grapple_s25_g1_walk_138_217.troc` (`gancho-cuerda.md` §8): el invitado anda y lanza dos ganchos.
  El cliente del anfitrión pide el acople del personaje sintético (2249) y se crean sus 8 *gimmicks*.
- `probe`: el log del gancho A y de `0x2792A7B` lista esas 8 claves con «pondría `0x9010xxxx`».
- `2`:
  - en la captura de la sesión 2, el 2005 de esas claves lleva el controlador del usuario 2;
  - el cliente del anfitrión las reescribe y las marca «solo local»;
  - el anfitrión sigue mandando o manejando el despliegue y el enganche de ese dispositivo (contadores de
    `0x8E5F60` con `controlado = 1` tras reescribir);
  - el servidor no acepta ninguna Req del anfitrión para ellas;
  - en una captura de pantalla (`CopyFromScreen`), la cuerda del sintético se sigue viendo como en S27.
- **Límites.** `cdmp_peer` no es un cliente:
  - no ejecuta *gimmicks*;
  - no puede comprobar la pantalla del invitado ni que su cliente maneje su dispositivo;
  - la aceptación de un 3082 real del usuario 2 se podría forzar con un 3082 del mundo propio de G27-2 (t 35,4),
    con la clave cambiada a la nueva. Opcional.

  La fase 1 en el invitado solo se puede probar con Shadow. Los tests unitarios de la reescritura usan registros
  reales: el 2005 de `B01004A1`, que `recdump.py` saca de G27-2.

**Con Shadow (sesión 28; el anfitrión es el PC del usuario):**
1. **Paso 1, `probe` en los dos.** Las tres miradas de §12.2:
   - ¿se despliega el lanzador del invitado en su pantalla?
   - ¿ve el invitado la cuerda y el lanzador del anfitrión?
   - ¿ve el anfitrión el lanzador del invitado?

   Más la sonda de §3.4.
2. **Paso 2, invitado `1` y anfitrión `probe`.**
   - El invitado ve su lanzador y su cuerda.
   - No hay errores de juego nuevos: los 9 RelaseCatch y 6–8 Projectile son la base.
   - En el **anfitrión**, el contador «user 2 listed requests» de las claves acopladas al personaje del usuario 2 es
     0. La captura del invitado no sirve para esto: su *sniffer* la toma en `PostPacket`, antes de que `guest_send`
     retenga nada (§6.6).
   - Recuento de 2249.
3. **Paso 3, los dos en `2`.**
   - Las dos pantallas como en el paso 2.
   - La sonda del servidor: 3082 aceptados del usuario 2 para sus acoplados > 0 y 0 rechazados.
   - 2297 hacia el invitado > 0.
   - El anfitrión sigue viendo el lanzador del invitado.

**Criterio de aborto:** el de §6.3. En resumen:
- un cierre;
- un error de juego nuevo en cualquiera de los dos;
- que el anfitrión deje de ver algo que ya veía;
- el contador del anfitrión de §6.6 mayor que 0.

---

## 4. Lo que queda abierto

- El emisor del 3082 y su origen en `.trace` (no seguido). La sonda de `0x8E59D0`/`0x8E5F60` basta para saber si el
  rechazo es lo único que falta.
- La validación de 2774 / 3089 en el servidor.
- Por qué falla el proyectil del Stranger en el invitado (`0x21DF510`, vf `+0x188` nula) [H].
- La causa del bucle de 2249 [H, §2.3].
- `TransformSyncSceneObjectReq` 2974 (invitado 202, anfitrión 757) y `BreakSceneObjectAck` 2746 (anfitrión 4 / invitado
  16): objetos de escena con física. No son *gimmicks* de tipo 7 y no los he mirado.

## 5. Scripts y direcciones

Scripts en `…\scratchpad\gimmicks\` (Python `-I`; los de capturas leen datos no fiables y no ejecutan nada):

| Script | Qué hace |
|---|---|
| `gidx.py CAP.bin OUT.pkl` | Una pasada: todos los registros 2005 (tipo, clave, dueño, controlador, nombre) y las ops de *gimmick*, proyectil y agarre (`g1/g2/h1/h2.pkl`) |
| `who.py` / `byop.py` | Controlador por tipo; Req de manejo por clase de controlador y opcode |
| `win.py` / `win2.py` | Ventanas de tiempo decodificadas (`win2.py`: proyectiles y agarres alrededor de los errores) |
| `pingpong.py` | Bajas y altas cruzadas de objetos de nivel (2145) |
| `docked.py` / `parent7.py` / `recdump.py` | Acoplados a personajes (padre en `ne+0xAC`/`ne+0xC6`), clases de esos campos, volcado de registros |
| `ctlscan.py` / `ctlscan2.py` / `callscan.py` | Sitios del exe que comparan el usuario local con `+0x90` |
| `hl.py` | `hashlittle(nombre, 0xC5EDE)` de los códigos de error |

| RVA | Qué |
|---|---|
| `0x2791CD0`, `0x2792A11–0x2792A7B` | Servidor: controlador al crear (existente → `desc+0x40` → `+0xA0` → `[campo+0x180]`); escritura en `0x2792A7B` |
| `0x2970790` → `0x28AD170` → `0x28AEC70` / `0x278F490` | 2249: acople de equipo; descriptor; creación con la vf `+0x88` del campo |
| `0x29711B0` → `0x28ADE40` | 2973: acople no de equipo |
| `0x29E0970` → `0x2AC1270` | 3082 en el servidor; rechazo mudo si quien pide no es el controlador |
| `0x29DE610` → `0x2AC0790` | 2758 en el servidor; `eErrNoActorNotExist` si quien pide no es el controlador |
| `0x29E0610` | 2609 en el servidor: vacío |
| `0x29C2200` → `0x282E6A0` | 2221: baja sin mirar dueño ni otros usuarios |
| `0x4FA530` | `C8 = 3` para tipo 7 antes de mirar `+0x90` |
| `0x8E59D0` (vf `+0x740`), `0x8E5F60`, `0x9065A0` | Cliente: «¿lo controlo?» = `[[jugador local+0xA0]+0x60] == [gimmick+0x90]`; rechazo en `0x8E5A49` |
| `0xBAD340` | Manejador de `UpdateGimmickAck` 2297 → `0x8E59D0` |
| `0x9A06500` (`.xtls`, vf148 `0x90D9B0`) | Evento de agarre o suelta del *gimmick* → vf `+0x740` |
| `0xBAAD90` | Emisor del 3082 (opcode `0xC0A`), por `.trace 0x17132468` |
| `0x6CF6B68` | `eErrNoActorNotExist` = `0xD65F8D70` (registrado en `0x21FC0F9`) |
| `0xBB0960` | Manejador de `GimmickCatcherUpdateAck` 2356 (→ `.xtls 0xE364C00` en el componente del *gimmick*) |
| `0x21DF510` | Creación de proyectil en el cliente; error en `0x21DF65A` |
| `0xB681F0` / `0xBB1640` | Manejadores de 2508 y 2168: ninguno cambia `+0x90` |

---

## 6. Implementado: fases `probe` y `1` (13 de octubre de 2026; compilado y con tests, sin probar en el juego)

Rama `gimmicks-1` (worktree `D:\cdmp-wt\gimmicks`), sobre `opcion-d` `c4367f8`. **La fase 2 (autoridad en el
servidor) no está.** Clave `[session2] docked_gimmicks = 0 | probe | 1`, en los dos PC.

| Fichero | Qué |
|---|---|
| `src/mod/session2/docked_gimmicks_logic.h` | Lógica pura: registros del 2005, padre acoplado, decisión y reescritura, Req retenidas, tablas de la sonda, bytes verificados |
| `src/mod/session2/docked_gimmicks{.cpp,.h}` | Módulo: entrega y envío del invitado, *tap* del *sniffer* y entrada del servidor en el anfitrión, ganchos de sonda, hilo propio |
| `guest.cpp` | 2756 → personaje propio; antes de `guarded_on_recv`, el 2005; en `guest_send`, la retención (como `mount_sync`); `guest_conversation_begin` → reinicio |
| `server.cpp` | En `on_server_recv`: `docked_gimmicks_server_ingress` (solo cuenta) |
| `session2.cpp` | *Tap* del *sniffer* (`sniffer_tap_docked_gimmicks`) |
| `config`, `mod.cpp`, `CMakeLists.txt` | Clave, arranque en los dos papeles, test `optd_docked_gimmicks` |
| `tests/optd_docked_gimmicks_tests.cpp` | Sobre los primeros bytes reales de seis registros de G27-2 (usuario `0x90100376`, personaje `0xA0100377`, dispositivos `B01004A1` y `B0100156`, acoplado a NPC `B010006E`, *gimmick* de mundo `B010006C`) |

### 6.1 Qué hace

**Invitado, `1` (manejo local; solo paquetes, sin gancho):**
- Personaje propio: el `MainPlayerDataStartAck` 2756. Usuario propio: el `+0x12` del registro tipo 1 de ese personaje
  (en S27, `0x90100376`, llega en el 2005 de t 64,38, antes que sus acoplados).
- Para cada registro tipo 7 que cumple todo esto se escribe su usuario en `+0x12` (4 bytes, mismo tamaño) y la clave
  pasa a «solo local» (64 entradas):
  - su padre (`ne+0xAC` == `ne+0xC6`) **es su personaje**;
  - se llama «Game Mechanic»;
  - su `+0x12` es una clave de usuario (`0x9…`) y no la suya;
  - el paquete está bien formado.
- Se pasa sin tocar y se cuenta cuando:
  - falta el personaje o el usuario propio;
  - el registro está acoplado al otro jugador, a un NPC o no está acoplado;
  - el registro tiene otro nombre (`not rewritten (name)`, con el nombre en el log) o su `+0x12` no es un usuario;
  - **cualquier** registro del paquete está mal formado: el paquete entero pasa tal cual.
- En `guest_send` se **retienen** (no salen hacia el anfitrión, como las de `mount_sync`) las Req de esas claves.
  Lista confirmada en el código del servidor:

  | Req | Manejador | ¿Exige ser el controlador? | Efecto de retenerla |
  |---|---|---|---|
  | 3082 | `0x2AC1270` | Sí, rechazo mudo | Ninguno |
  | 2758 | `0x2AC0790` | Sí, `eErrNoActorNotExist` | Ninguno |
  | 2609 | `0x29E0610` | Cuerpo vacío | Ninguno |
  | 2774 | `0x29E5050` | No: escribe `comp+0x42D` | El anfitrión sigue siendo el único que escribe |
  | 3089 | `0x2A987C0` | No se encontró comprobación | Ídem |
  | 2779 | `0x2A3E370` | No se encontró comprobación (crearía un proyectil) | Ídem |
  | 2826 | — | Sin decodificar | Ídem |
  | 2618 | — | Sin decodificar; el anfitrión la manda para el dispositivo (36 en H27-2) | Ídem |

  `MoveActorReq`, `RotateActorReq`, `RegionInOutReq` y 3068 siguen como hoy.
- **Hallazgo lateral [V datos]:** el invitado ya manda hoy `CreateProjectileFromGimmickEventReq` 2779 para *gimmicks*
  del anfitrión, con las mismas claves que el anfitrión (G27-1 157,1–161,7 = H27-1 312,3–316,8). Si el servidor no
  comprueba el controlador, eso duplica proyectiles. No se toca aquí: solo se mide con la sonda en una sesión futura.

**Anfitrión, `probe` o `1`:** nunca reescribe nada. Sus acoplados ya son suyos y los del usuario 2 no se toman nunca,
así que `1` equivale a `probe`. Los 2005 de su cliente se clasifican desde el *sniffer*: hace falta
`[sniffer] enabled=1`, que ya está en los .ini de la sesión.

**Sondas (`probe` y `1`, solo log).** Con el mundo cargado (6 × 0,5 s), en entradas verificadas con
`hook_prepare` (salto de 5 B sobre una instrucción completa; si cualquier byte no cuadra, `REFUSED` y no se escribe
nada):

| Dónde | Gancho | Qué cuenta |
|---|---|---|
| Los dos | `0x8E59D0` (comprueba también que la ranura `+0x740` de la vtable `0x55B7898` apunta ahí) y `0x8E5F60` | Por clave: llamadas, rechazos (el código del juego leído de `0x6CF6B68`, esperado `0xD65F8D70`), hashes, `comp+0x2B4`. Una línea solo para los acoplados a un jugador |
| Anfitrión | Entradas de `0x28AD170` y `0x28ADE40` (variable por hilo con el padre) + `create_mid` en `0x2792A7B` (lectura de `rax`, `[r13+0x180]`, `[r14+0x60]`) | La línea sale solo si se creó algo (el bucle de 2249 no gasta líneas) |
| Anfitrión | Entradas de `0x2AC1270` y `0x2AC0790` | Aceptado/rechazado por quien pide (cliente del dueño del campo u otro) y por clave. Línea para el primer rechazo de un acoplado a un jugador |
| Anfitrión | `on_server_recv` | Por clave, último uso de cada usuario (2556/2828/3067/2758/3082/2609/2774/3089). Ante cada 2221, la edad del último uso **del otro** en tramos `<0,5 s`, `<2 s`, `<10 s`, `<60 s`, `más`, `nunca`; 2928 por usuario |
| Los dos | Hilo del módulo, 1,5 s después de cada 2005 acoplado a un jugador | `find_actor`, `actor+0x90` y el `+0x2B4` del componente comparados con el registro: `MATCH` / `DIFFERENT` |

2774 y 3089 no tienen gancho propio: medir quién manda qué es lo mismo que el recuento de la captura y no salía
barato.

### 6.2 Líneas del log

- Arranque: `DockedGimmicks ON (guest|host, docked_gimmicks=probe|1): …`.
- Ganchos:
  - `DockedGimmicks: client probe hooked at the entries of 0x8E59D0 … and 0x8E5F60`;
  - en el anfitrión, `… server probe hooked: …`;
  - si algo no cuadra, `… REFUSED, nothing written`.
- `DockedGimmicks #n own character …: 0xA01…` y `… own user learned …: character 0x… is controlled by user 0x…`.
- `DockedGimmicks #n 2005 into this client …: gimmick 0xB01… 'Game Mechanic' docked to character 0xA01… | controller
  +0x12 0x90100000 -> 0x9010… | REWRITTEN (local drive)`. Las otras acciones posibles:
  - `would rewrite (probe)`;
  - `already this client's`;
  - `another player's: unchanged`;
  - `own user unknown: unchanged` y `own character unknown: unchanged`;
  - `not rewritten (name)`, `not rewritten (controller is not a user)` y `not rewritten (malformed packet)`.
- `DockedGimmicks #n local-only key 0x… dropped …`, cuando una clave sale de «solo local» (§6.6).
- `DockedGimmicks summary, user 2 listed requests at the server's receive (…): TOTAL for its own N | …`, en el
  anfitrión cada 60 s, y las primeras 20 como `DockedGimmicks #n user 2 SENT request …`.
- `DockedGimmicks check of 0xB01…: client actor +0x90 0x… (the 2005 had 0x…, written 0x…) | … +0x2B4 0xA01… -> MATCH`.
- `DockedGimmicks #n WITHHELD (never sent to the host) …: request 3082 of local-only gimmick 0x…`; en `probe`,
  `would withhold (probe; sent)`.
- `DockedGimmicks #n client request 0x8E59D0 …: gimmick 0x… docked to THIS client's character … | event 0x003ECC59
  (deploy 003ECC59) | accepted here|REFUSED (…)`.
- Anfitrión:
  - `… server created 0x… (type 7) inside the docking spawn for 0xA01…: controller written 0x90100000 (field owner …)
    | the character's controller 0x9010… -> phase 2 would write …`;
  - `… server REJECTED …`;
  - `… 2221 unspawn from …`.
- Cada 60 s, `DockedGimmicks summary (…)`, `… summary, client requests …` y, en el anfitrión, `… summary, server …`.

### 6.3 Prueba con Shadow (sesión 28)

**Paso 1 (anfitrión `cdmp.p8-s27-host-paso1.ini`, invitado `cdmp.p8-s26-shadow.ini`: los dos en `probe`).** Las tres
comprobaciones en pantalla (captura `CopyFromScreen`, recortada):
1. En la pantalla del **invitado**, cuando lanza el gancho: ¿se despliega su lanzador del brazo? ¿se ve su cuerda?
2. En la pantalla del **invitado**, cuando lanza el **anfitrión**: ¿se ven su lanzador y su cuerda?
3. En la pantalla del **anfitrión**, cuando lanza el invitado: ¿se ve el lanzador del invitado?

Y en el log:
- el invitado, `would rewrite (probe)` para sus 8 acoplados y `another player's` para los del anfitrión;
- `check … MATCH`;
- en la sonda del cliente, el despliegue y el enganche de **su** dispositivo `REFUSED` en el invitado (la causa en
  vivo) y `accepted here` en el anfitrión;
- en el anfitrión, `server created … controller written 0x90100000 (…, the field owner) | the character's controller
  0x9010…` para los acoplados del invitado;
- los tramos de 2221.

**Paso 2 (invitado `cdmp.p8-s26-shadow-paso2.ini`: `1`; el anfitrión, `cdmp.p8-s25-host-activo.ini`, sigue en
`probe`).**
- **Pasa si:**
  - comprobación 1: el invitado ve desplegarse su lanzador y ve su cuerda;
  - la 3 sigue como en el paso 1;
  - en el invitado: `REWRITTEN` × 8 por cada aparición de su personaje, `check … MATCH` con `actor+0x90 = su
    usuario`, y su sonda con `accepted here` para su dispositivo;
  - `WITHHELD` > 0 en el log del invitado;
  - **en el anfitrión, `user 2 listed requests … TOTAL for its own` = 0.** En el paso 1 se esperan unas 16: el 2758 y
    el 2774 que el cliente del invitado manda al crear cada uno de sus 8 acoplados. La captura del invitado **sí**
    mostrará esas Req, porque su *sniffer* captura en `PostPacket`, antes de que `guest_send` las retenga; también
    las cuenta `note_out`. La línea `WITHHELD` lo recuerda;
  - errores de juego no nuevos: base S27, 9 RelaseCatch y 6–8 Projectile.
- **Se aborta (el invitado vuelve a `docked_gimmicks=probe`) si:**
  - se cierra o se cuelga el juego de cualquiera de los dos;
  - aparece un error de juego nuevo respecto a la base S27 (9 RelaseCatch, 6–8 Projectile);
  - el anfitrión deja de ver el lanzador o la cuerda del invitado;
  - sale algún `DIFFERENT` en las claves propias, o algún `REWRITTEN` con un nombre que no sea «Game Mechanic»;
  - el contador del anfitrión `user 2 listed requests … TOTAL for its own` es mayor que 0;
  - las Req con la clave del personaje (2387, 2380, 2504, 3010, 2287) crecen entre el paso 1 y el 2 con un uso
    comparable del gancho.

**Ojo con las herramientas de captura.** En el paso 2, la captura del invitado ve los 2005 ya reescritos. Así,
`who.py` y la parte del dispositivo de `rope_probe` lo mostrarán como controlador de sus acoplados, y eso es lo
esperado. Lo que de verdad ve el servidor está en la captura del anfitrión y en sus contadores.

### 6.4 Riesgos de esta fase

- **El invitado ejecuta la máquina de estados de su lanzador sin que el servidor la acepte.** Todo lo que pide se
  queda en su PC. Un efecto de cliente del dispositivo (sonido, efecto, proyectil local) solo existe en su pantalla.
  - Su mundo propio era así de nativo, pero allí el servidor aceptaba las Req. Si algún paso necesitara respuesta del
    servidor, el lanzador se quedaría a medias **[H]**.
  - En el mundo propio no se recibió ningún 2297 (0 en G27-2), así que no parece que la necesite.
- **El estado del dispositivo en el servidor lo sigue decidiendo el cliente del anfitrión,** a partir de lo que ve.
  Cooldowns o consumo del equipo del usuario 2 que pasen por el dispositivo: igual que hoy. Eso lo arregla la fase 2.
- **El registro reescrito es el que el juego guarda en el cliente del invitado.** Si una ruta futura reenviara ese
  2005, no se reenvía nada de él: la entrega solo lo pasa al juego.
- **Desplazamiento del padre:** está validado en 290 registros S27 y en los tests. La comprobación en vivo es
  `check … MATCH`. Si no cuadra, nada cambia: solo se reescribe cuando las dos copias coinciden **y** son el
  personaje propio.
- **Sondas en caliente:** `0x8E59D0`/`0x8E5F60` y `0x2792A7B` corren en hilos del juego.
  - Lecturas `fast_read`, SEH solo para la pila del llamador, contadores atómicos y un anillo sin asignación.
  - `0x2792A7B` está en todas las creaciones de actores: la primera comprobación es la variable por hilo.

### 6.5 Un PC

Los tests (`optd_docked_gimmicks`, en verde con el resto: 60/60 tras la revisión de §6.6) cubren:
- la reescritura con los bytes reales;
- que no cambia nada fuera de los acoplados propios;
- probe y 0;
- desconocidos, malformados, la lista retenida y las tablas.

Con `cdmp_peer` + `grapple_s25_g1_walk_138_217.troc` se pueden ver las sondas del anfitrión: creación dentro del
acople, rechazos y 2221. **No se puede ver la fase 1:** `cdmp_peer` no es un cliente y no ejecuta *gimmicks*; la
reescritura solo actúa en la entrega del invitado real. No hubo prueba en el juego: no se instaló nada ni se abrió
el juego.

### 6.6 Revisión independiente (13 de octubre de 2026)

Correcciones sobre `920b375` (fusionado en `2888da9`):

1. **Cómo comprobar la retención.** La captura del invitado no lo demuestra: su *sniffer* graba en `PostPacket`
   (`troc_sniffer.cpp`, `on_post_packet`) antes de que `guest_send` retenga, y `note_out` también cuenta antes.
   - Ahora el anfitrión cuenta, en `docked_gimmicks_server_ingress`, las Req de la lista que manda el usuario 2 para
     claves acopladas a un jugador. El padre de cada clave sale de los 2005 que ve su cliente.
   - Las separa entre «acoplado al personaje del usuario 2», «al del anfitrión» y «el personaje del anfitrión aún
     desconocido», y lo da en una línea del resumen.
   - La línea `WITHHELD` del invitado avisa de que su captura sigue viéndolas.
   - Los criterios de §3.7 y §6.3 están reescritos con esto.
2. **Paquete mal formado.** Si alguna longitud del paquete no cuadra, no se escribe nada aunque el modo sea 1
   (acción `not rewritten (malformed packet)`, la clave no pasa a «solo local»). El test se cambió en consecuencia.
3. **Hilos.** El 2756 se lee ahora en `docked_gimmicks_guest_deliver`, en el mismo hilo y en el mismo orden que los
   2005; antes se leía en el hilo de red del mod.
   - `docked_gimmicks_guest_reset` borra también el personaje propio, y lo llama además `end_remote`.
   - La clave del personaje se repite entre mundos: `A0100377` en el suyo y en el del anfitrión.
4. **Filtro.** Solo se reescribe un «Game Mechanic» cuyo `+0x12` sea una clave de usuario. Cualquier otro registro
   acoplado al personaje propio se registra con su nombre y no se toca. El invitado manda
   `AttachDockingChildActorReq` 2287 con su personaje, así que un objeto del mundo acoplado no debe pasar a ser suyo.
5. **Tablas de la sonda.** Como mucho se miran 64 posiciones por clave; si no hay hueco, se cuenta en `full`.
6. **Claves que salen de «solo local».** Una clave sale cuando un 2005 la anuncia con otro padre o sin acoplar, o
   cuando llega un `RemoveActorAck` 2516 con ella (clave en `+6`). Ambos casos se cuentan en el resumen.
7. **Rendimiento y log.**
   - `0x2792A7B` mira primero la variable por hilo; fuera de un acople no hace nada más. Desaparece el recuento de
     todas las creaciones.
   - Un 2221 con más de 16 claves se mide en sus 16 primeras y cuenta las demás («keys past 16 not measured»).
   - Las líneas `REWRITTEN`, `WITHHELD` y de claves que salen tienen su propio cupo (200), aparte de las 400
     generales.

---

## 7. Proyectiles de *gimmick* duplicados (2779): `gimmick_event_dedup` (13 de octubre de 2026)

Solo `probe` en los .ini de la sesión 28. **El modo 1 no se activa hasta cumplir §7.4.**

### 7.1 Evidencia

**Código [V]** (investigación de solo lectura, `scratchpad\proj_dup\`):
- `CreateProjectileFromGimmickEventReq` 2779 → `0x2A3E370` → `0x2BDD1D0(componente, …)` **no recibe a quien pide**
  ni compara el `+0x90` del *gimmick*: no exige ser el controlador.
- 2399, 2202, 2699, 2612, 2999 y 2387 sí lo exigen. Sus duplicados no tienen efecto y se quedan como están.

**Datos [V]** (2250 = `CreateProjectileAck`: `+0` el dueño, `+0x1D` el dueño otra vez, `+0x21` u32 el índice del
proyectil de ese dueño):

| Sesión | Qué pasó |
|---|---|
| S27-1 | Los dos clientes mandaron la 2779 de 8 *gimmicks* (`B010087B` … `B01008F4`, H27-1 t 312–317). El servidor creó **dos** proyectiles de cada uno: 2250 con índice 1 (la del invitado llegó antes, p. ej. 312,196) y 2 (la del anfitrión: 2779 a 312,288 → índice 2 a 312,296). Los dos PC recibieron los dos |
| S27-1 | `B010014C` (dispositivo del anfitrión): índice 1 a H 50,117 y, cuando entró el invitado, índice 2 a H 214,899 |
| S27-2 | El invitado repitió la 2779 de `B010015E` al cargar el mundo (G 65,19 ≈ H 236,9). El servidor creó el índice 2 a H 239,03, 2,1 s después; la 2779 del anfitrión era de H 69,38, 170 s antes |

En total, **10 proyectiles de más en S27**. Los 8 errores «Projectile 생성 실패» del invitado no tienen que ver con
esto: son sus 2935 para *gimmicks* del anfitrión (§2.4).

### 7.2 Qué hace (`[session2] gimmick_event_dedup = 0 | probe | 1`, los dos PC)

**Invitado.**
- En la entrega, **antes** de que `docked_gimmicks` reescriba nada, guarda el controlador (`+0x12`) del último 2005 de
  cada *gimmick* tal como lo mandó el servidor (tabla de 16 384 claves). Se borra al empezar cada conversación.
- En `guest_send`, cada 2779 que sale se decide:
  - el controlador es conocido, es un usuario y **no** es el suyo: `would withhold` (probe) o `WITHHELD` (1);
  - controlador desconocido, o un valor que no es un usuario: se envía (`controller unknown: sent`);
  - aún no conoce su usuario: se envía (`own user unknown: sent`);
  - el controlador es él: se envía.
- Si la regla de claves propias de `docked_gimmicks` ya la retiene (`docked_gimmicks=1`), la línea dice
  `docked_gimmicks' own-key rule matched` y no se cuenta otra vez.

**Anfitrión, solo log en cualquier modo.**
- En la entrada del servidor (`on_server_recv`) cuenta cada 2779 por origen (su cliente o el usuario 2) y por
  *gimmick*.
- Con el *sniffer* (necesita `[sniffer] enabled=1`) clasifica cada 2250 de un *gimmick* que haya recibido alguna
  2779:
  - `first`: índice 1.
  - `DUPLICATE created`: índice > 1, una 2779 que llegó como mucho 5 s antes lo causó, y **el otro origen** también
    había mandado una 2779 de ese *gimmick*, cuando fuera. La línea dice qué origen lo causó y cuántos ms antes.
  - `index > 1 without 2779 from both sides`: índice > 1 sin lo anterior. Tiene otra causa; solo se cuenta.
- Sobre la ventana: el encargo pedía «< 1 s después de 2779 de los dos lados». El caso de S27-2 (2,1 s, y la otra
  2779 de 170 s antes) no entraría, así que se pide la del otro origen en cualquier momento.

Lógica pura en `src/mod/session2/gimmick_event_dedup_logic.h`; la ejecución está en `docked_gimmicks.cpp`, en los
mismos puntos de paquetes y en el mismo hilo. Tests en `tests/optd_gimmick_event_dedup_tests.cpp`, con los 2250 y la
2779 reales de S27 y los dos casos de duplicado.

### 7.3 Líneas del log

| Dónde | Qué buscar |
|---|---|
| Arranque | `GimmickEventDedup ON (guest|host, gimmick_event_dedup=probe)` |
| Invitado (las primeras 200) | `GimmickEventDedup [probe] #n 2779 for gimmick 0x… …: controller (last 2005 +0x12) 0x90100000 (the other user's) | own user 0x9010… | controller is the other user -> would withhold (probe; sent)`; en las demás, `-> sent` con la regla correspondiente |
| Invitado, cada 60 s | `GimmickEventDedup summary (guest, …): outgoing 2779: <regla>: sent N, would withhold N, WITHHELD N | … | controllers known N (table full N)` |
| Anfitrión (los primeros 100 que no son `first`) | `GimmickEventDedup host #n 2250 for gimmick 0x… index 2 …: DUPLICATE created | last 2779 within 5000 ms: user 2's (2132 ms before) | 2779 so far: the host's client 1, user 2 1` |
| Anfitrión, cada 60 s | `GimmickEventDedup summary (host, …): 2779 at the server's receive: the host's client N, user 2 N | 2250 (sniffer): first N, DUPLICATE created N, index > 1 without both N | keys whose last 2779 got no 2250 within 5000 ms: the host's client's N, user 2's N | …` |

### 7.4 Plan de medida y criterios para activar el modo 1

**Medida (sesión 28, `probe` en los dos):** el invitado lanza el gancho, entra y sale del mundo, y se acerca a los
*gimmicks* del anfitrión; el anfitrión hace lo mismo cerca del invitado.

**Se puede poner `gimmick_event_dedup=1` en el invitado cuando se cumpla todo esto en una sesión:**
1. Cada `would withhold` del invitado (por *gimmick* y momento, con el ajuste de relojes de la sesión) tiene su
   `DUPLICATE created` en el anfitrión con `user 2's` como origen, **uno a uno**:
   - ningún `would withhold` sin duplicado;
   - ningún duplicado causado por el usuario 2 sin su `would withhold`.

   Los duplicados causados por el cliente del anfitrión, cuando la 2779 del invitado llegó primero, se emparejan con
   el `would withhold` de esa 2779.
2. Ningún caso en que el invitado habría retenido una 2779 cuyo proyectil no llegó por otro lado. En probe se ve así:
   para cada `would withhold`, el anfitrión tiene un 2250 de ese *gimmick* (índice 1 o el de su propia 2779) dentro
   de la ventana. En el resumen del anfitrión, `keys whose last 2779 got no 2250 … the host's client's` debe ser 0.
3. Sin `controller unknown` en *gimmicks* que luego dan duplicado. Si los hay, la tabla de controladores no es fiable.

**Comprobación con el modo 1 puesto:**
- en el anfitrión, `DUPLICATE created` = 0;
- `the host's client's … got no 2250` = 0;
- `user 2` en `2779 at the server's receive` baja exactamente en lo que el invitado cuenta como `WITHHELD`.

**Si no:** volver a `probe`.

---

## 8. Sesión 28: resultados, y `docked_gimmicks_other` para la sesión 29 (13 de octubre de 2026)

Análisis de solo lectura de la sesión 28 (Shadow invitado, PC del usuario anfitrión). Scripts y salidas:
`…\scratchpad\s28_gimmicks\` (`scan.py` lee cada captura una vez y deja un `.pkl`; `an1`–`an7.py`, `xf.py`, `rope.py`,
`rec.py`).

| Id | Fichero | Configuración |
|---|---|---|
| H28-1 | `C:\Emulador CD\backups\p8-sesion28-host\cdmp_20261013-053042.log`, `cdmp_packets_20261013-053053.*` | anfitrión: `docked_gimmicks=probe`, `gimmick_event_dedup=probe` |
| H28-2 | `…\cdmp_20261013-055640.log`, `cdmp_packets_20261013-055652.*` | anfitrión: lo mismo |
| G28-1 | `…\scratchpad\s28-shadow-p1\cdmp_20261013-043247.log`, `cdmp_packets_20261013-043307.*` | invitado: `docked_gimmicks=probe` |
| G28-2 | `…\scratchpad\s28-shadow-p2\cdmp_20261013-045951.log`, `cdmp_packets_20261013-050005.*` | invitado: `docked_gimmicks=1` |

- **Relojes [V].** El reloj de Shadow va una hora por detrás. Los alinean las 2779 de §8.2:
  - paso 1: t(H) = t(G) + 146,32 s (14 de 15 parejas entre 146,31 y 146,42 s; la primera, en la carga del mundo, 147,45 s);
  - paso 2: t(H) = t(G) + 202,775 s (las tres parejas, con 14 ms de margen).
- **Mundo del anfitrión en la captura del invitado:**
  - G28-1: t 50–684;
  - G28-2: t 49,8–481,6 y, tras volver a entrar, 550–650.
- **Lo que vio el usuario:**
  - **Paso 1:** el invitado no ve ninguna de las dos cuerdas; el anfitrión ve las dos.
  - **Paso 2:** el invitado **ve su cuerda**, pero no la del anfitrión. El anfitrión sigue viendo la del invitado.
  - **En los dos pasos:** los árboles no se doblan. En el paso 2, revivir y volver a entrar funcionan. Ningún cierre.

### 8.1 La fase 1 contra los criterios de aborto de §6.3 [V]

| Criterio | Paso 1 | Paso 2 | ¿Pasa? |
|---|---|---|---|
| `user 2 listed … TOTAL for its own` (anfitrión) | 16 (2758 ×8 + 2774 ×8: lo esperado) | **0** en toda la sesión, también tras volver a entrar | Sí |
| `WITHHELD` del invitado | — | 3082 406, 2758 99, 2609 74, 2774 16 | Sí |
| `REWRITTEN` con otro nombre / `not rewritten (name)` | — | 0 / 0 (16 `REWRITTEN`, todos «Game Mechanic») | Sí |
| Paquetes mal formados | 0 | 0 | Sí |
| Claves retiradas (2005 / 2516) | 0 / 0 | 0 / 0 (se vacían en cada cambio de mundo: `resets 3`) | Sí |
| `DIFFERENT` | 14 de 14 | 30 de 30 | Salta, pero es un fallo de la sonda (abajo) |
| Errores de juego nuevos | — | Ninguno de tipo nuevo (abajo) | Sí |
| Req con la clave del personaje (2387/2380/2504/3010/2287) | — | Sin crecimiento atribuible (abajo) | Sí |

- **La retención, comprobada [V].** Los contadores `WITHHELD` del invitado coinciden exactamente con lo que muestra su captura (`PostPacket`):

  | Req | Retenidas | Primer tramo + tras volver a entrar |
  |---|---|---|
  | 3082 | 406 | 324 + 82 |
  | 2758 | 99 | 86 + 13 |
  | 2609 | 74 | 65 + 9 |
  | 2774 | 16 | 8 + 8 |

  Al servidor no llegó ninguna: el contador del anfitrión es 0.
- **`DIFFERENT` es un fallo de la sonda, no de la reescritura.**
  - En las comprobaciones de los cuatro logs (80 de 80), `+0x90` coincide siempre con lo que escribió el 2005 (`0x901002D1` y `0x9010110C` en el paso 2).
  - En esas mismas 80, `+0x2B4` del componente vale **siempre 0**. Pasa también en los dispositivos que su cliente ya controla de forma nativa (el anfitrión con los suyos; el invitado con los de su mundo propio).
  - Conclusión: `+0x2B4` no es el padre del acople, o no está relleno a los 1,5 s.
  - Como `classify()` usaba ese campo, **todos** los dispositivos de jugador cayeron en «world (not docked)» y nunca salió una línea `client request … docked to …`. Arreglado en §8.4.
- **La puerta es la comprobación de `0x8E59D0` [V].** En el mundo del anfitrión, el invitado:
  - en el paso 1 tuvo **0** llamadas aceptadas;
  - en el paso 2 las aceptadas crecen al mismo ritmo que las 3082 de su dispositivo: 89 → 143 → 202 → 252 (meseta 05:05–05:07) frente a 99 → 133 → 170 → 256 (la misma meseta).
- **Errores de juego.** El log se corta en 10 líneas por tipo, así que se cuentan las causas en la captura, dentro del mundo del anfitrión:

  | | 2356 (RelaseCatch) | 2935 (Projectile) |
  |---|---|---|
  | Paso 1 | 10 | 15 |
  | Paso 2 | 9 | 10 |

  - Los dos pasos superan la base de S27 (9 / 6–8), pero ya el paso 1, en `probe`, la superaba.
  - Todos caen junto a recogedores y proyectiles de NPC, nunca junto a un dispositivo.
  - El paso 1 tuvo además un `Actor AsyncPostProcessForCreate` que el paso 2 no tuvo.
  - El anfitrión solo registró `responseTrChangeAIStateAck`.
- **Req con la clave del personaje** (invitado, mundo del anfitrión).

  | Req | Paso 1 (634 s) | Paso 2 (432 s + 100 s) |
  |---|---|---|
  | 2380 `DoFrameEventReq` | 95 | 203 |
  | 2387 | 0 | 6 |
  | 2287 | 11 | 0 |
  | 2504 / 3010 | 0 | 0 |

  - Las 2380 llevan posición y dirección del golpe: vienen del combate. La parte que cae a ≤1 s de un evento del dispositivo **baja**, del 48 % (46/95) al 31 % (62/203).
  - Las 6 de 2387 van una a una con los 2250 de su propio personaje (disparos a 356, 385 y 395 s).
  - El uso del gancho fue comparable: 6 y 8 despliegues.
- **Veredicto: la fase 1 se queda en `1`.**

### 8.2 `gimmick_event_dedup`: se cumple §7.4 [V]

- **Criterio 1, uno a uno.** En el paso 1, 15 `would withhold` y 15 `DUPLICATE created`; en el paso 2, 3 y 3. Las mismas claves en el mismo orden:
  - paso 1: `B010016B`, `B0102453 … B010259E`;
  - paso 2: `B010015C`, `B0100972`, `B0100CE7`.

  De los 18 duplicados, 15 los causó el usuario 2. Los otros 3 (`B0102453`, `B0102490`, `B01024A7`) los causó el anfitrión: la 2779 del invitado llegó antes (16/0/0 ms) y cuentan como emparejados.
- **Criterio 2, sin proyectil perdido.** `keys whose last 2779 got no 2250` = 0/0 en los dos pasos. Al servidor llegan 15/15 y 3/3 2779 (anfitrión/usuario 2), y cada clave tiene las 2779 de los dos.
- **Criterio 3, controlador conocido.** 0 casos de `controller unknown` o `own user unknown`. La tabla tiene entre 909 y 5975 claves conocidas y nunca se llenó.
- **Decisión:** `gimmick_event_dedup=1` en el paso 2 del invitado en la S29.
- **Riesgo que S28 no midió: el caso lejano.** Si el invitado está lejos del anfitrión, junto a un *gimmick* que el cliente del anfitrión no tiene cargado, su 2779 sería la única y en el modo 1 se perdería el proyectil.
  - Para medirlo, el anfitrión cuenta ahora `2779 of user 2 for a key the host's client never sent a 2779 for` (§8.4). Solo se puede medir mientras el invitado esté en `probe`, es decir, en el paso 1.

### 8.3 Por qué el invitado no ve la cuerda del anfitrión: el reflejo del problema ya arreglado [V]

- **El invitado no ejecuta el dispositivo del anfitrión.** Los dispositivos son `B010013F–44` (paso 1) y `B010014E–53` (paso 2).
  - Su 2005 llega con el controlador `0x90100000` (`another player's: unchanged`).
  - En el mundo del anfitrión, el invitado solo manda un 2774 por clave y por creación (×6 en el paso 1, ×12 en el paso 2) y **0** de 3082/2609/2758. Por tanto, su cliente no ejecuta la máquina de estados.
- **La cuerda sí llega.** El invitado aplica los 2763 de las tres cuerdas del anfitrión (`RemoteRope`: `remote 3 -> written 3, failed 0`). Es lo mismo que pasaba con su cuerda propia en el paso 1: la cuerda está enganchada, pero el lanzador (`RemoteCatchPivot`) no se ha desplegado.
- **Un controlador maneja el dispositivo acoplado a OTRO personaje con las acciones replicadas.** El cliente del anfitrión manda, para el dispositivo del invitado:
  - H28-1: 3082/2609/2758 140/31/68 y 2618 ×54;
  - H28-2: 196/46/62 y, tras volver a entrar, 82/9/10;
  - con despliegue `003ECC59` (×6 y ×8), guardar `EDFD0EBB` y `92786A19`, todos aceptados.

  Comparados con lo que el invitado manejó en local en el paso 2 (`an3.py`):
  - **los 78 eventos del anfitrión tienen pareja** (mismo hash y clave): mediana −39 ms, p10 −0,18 s, p90 +0,86 s;
  - los 8 despliegues caen a menos de ±70 ms;
  - el dueño emite más (197, por ejemplo 48 × `86C00635` que solo tiene él).

  El anfitrión no recibe ningún paquete del dispositivo del invitado: lo mueve solo con las acciones del personaje.
- **Código.**
  - [V] Existe el evento de la línea de tiempo del personaje `GamePlayEvent_SendGimmickEventToDockingChild` (cadena `0x56323B0`, registro `0x1410D0`, vtable `0x5627D00`) y la lista `CharacterInfo._dockingChildEventList` (lector `0x14F01A0`). La acción del padre dispara los eventos del hijo acoplado en todo cliente que reproduce esa acción.
  - [E] Llegan a la vf `+0x740` `0x8E59D0`, que tiene unos 40 llamadores en los nodos `0x50xxxx–0x55xxxx`, `0x90C750` y `0x8DE850`. El tramo virtualizado entre los dos no se pudo seguir.
- **Req que mandaría el invitado como controlador:** 3082, 2609, 2758, 2618 y 2774 al crearlo. Todas están en `kWithheldOps`. Las de transformada (2556/2828/3067) no cambian: ya las manda hoy, 6,9/s en el paso 1 frente a 7,3/s en el paso 2 para su propio dispositivo.

### 8.4 Implementado para la S29 (compilado y con tests, sin probar en el juego)

1. **La sonda del cliente se clasifica por el padre del 2005.** Antes usaba `+0x2B4`; ahora busca el padre de jugador en el mapa que llenan los 2005, en los dos papeles. El mapa se vacía en cada cambio de mundo.
   - La clase de antes «world (not docked)» pasa a llamarse «not docked to a player (world / NPC)».
   - Ahora sí salen las líneas por clave `client request … docked to THIS client's character / the other player's character`, con los rechazos.
   - La comprobación de 1,5 s solo compara `+0x90`; `+0x2B4` queda en la línea como información (`not compared`).
2. **`[session2] docked_gimmicks_other = 0 | probe | 1`** (solo en el invitado; necesita `docked_gimmicks` en `probe` o `1`; el anfitrión dice en el log que la ignora). En `process_2005`, para un tipo 7 acoplado a **otra** clave de jugador (`0xA…`) se aplican las mismas comprobaciones que en la fase 1:
   - usuario propio conocido;
   - `+0x12` es una clave de usuario distinta de la propia;
   - el registro es «Game Mechanic»;
   - el paquete está bien formado.

   Lo que hace cada modo:
   - **`1`:** escribe su usuario en `+0x12` y añade la clave a «solo local» con su padre y las marcas `kKeyOn | kKeyOther`. Así `guest_send` retiene sus 3082/2758/2609/2774/3089/2779/2826/2618.
   - **`probe`:** solo `would rewrite (other player, probe)` y `would withhold`.
   - **Líneas:** `REWRITTEN (other player, local drive)` y `other player's, … unchanged` con el motivo.
3. **«Solo local» guarda el padre de cada clave** (`LocalKeySet<128>`, válido para las dos reglas).
   - Una clave sale si un 2005 la anuncia con otro padre o sin acoplar, o si llega un 2516.
   - Se vacía con **cada** 2756 (S28: uno por cada entrada en un mundo y ninguno al revivir) y en cada conversación nueva.
   - Motivo: en G28-1, el mundo propio del invitado reutilizó `A0100001` y `B010013F–44`.
   - Cada clave lleva su propia decisión (`1` o `probe`); el resumen separa `withheld/would own` de `withheld/would other player`.
4. **Anfitrión, solo log:**
   - `TOTAL for the host's N` junto a `TOTAL for its own N` (S28: 2774 ×6 por creación);
   - en `gimmick_event_dedup`, `2779 of user 2 for a key the host's client never sent a 2779 for: keys N, 2779 M`, más una línea por clave (64 como mucho), unos 5 s después de la última, con la distancia entre los jugadores y si el *gimmick* existe en el cliente del anfitrión.
5. **Tests.**
   - `optd_docked_gimmicks`, `test_other_player`, sobre los registros reales de G28-2 (personaje `A01002D2`/`0x901002D1`, dispositivo del anfitrión `B010014E`): off, `1`, `probe`, retirada por padre o sin acoplar, cada comprobación y las dos reglas en un mismo paquete.
   - `optd_gimmick_event_dedup`, `test_lone`.

### 8.5 Prueba con Shadow (sesión 29)

**Paso 1** (invitado `cdmp.p8-s29-shadow.ini`: `docked_gimmicks=1`, `docked_gimmicks_other=probe`, `gimmick_event_dedup=probe`; anfitrión `cdmp.p8-s29-host-paso1.ini`, todo en `probe`).

En pantalla: el invitado sigue viendo su propia cuerda, y el anfitrión, las dos.

En el log del **invitado** tienen que aparecer:
- `would rewrite (other player, probe)` ×6 por cada aparición del dispositivo del anfitrión, siempre «Game Mechanic», con `withheld/would other player` en `0/N` (como mínimo 2774);
- en `client requests`, la clase `docked to the other player's character` con llamadas **rechazadas** de `003ECC59`/`EDFD0EBB`/`92786A19`, alineadas (±0,2 s) con las 3082 que manda el anfitrión para su dispositivo. Es la prueba en vivo de que esa es la puerta;
- en la clase `docked to THIS client's character`, llamadas aceptadas.

En el log del **anfitrión**:
- `TOTAL for the host's` = 2774 ×6 por creación (la base);
- `2779 of user 2 for a key the host's client never sent a 2779 for`: cuántas y a qué distancia. Para medirlo, separarse más de 100 m junto a trampas o torres.

**Paso 2** (invitado `cdmp.p8-s29-shadow-paso2.ini`: `docked_gimmicks=1`, `docked_gimmicks_other=1`, `gimmick_event_dedup=1`; anfitrión `cdmp.p8-s29-host-paso2.ini`).

**Pasa si:**
- el invitado ve el lanzador y la cuerda del anfitrión, el anfitrión sigue viendo las dos cuerdas y el invitado la suya;
- `REWRITTEN (other player, local drive)` ×6 por cada aparición; `check … MATCH` con `+0x90` = el usuario del invitado;
- `withheld/would other player` > 0 (3082/2609/2758/2774);
- en el anfitrión, `TOTAL for the host's` = **0** y `TOTAL for its own` = 0;
- la tasa de 3082 del anfitrión para su propio dispositivo no cambia (la captura del anfitrión);
- en `gimmick_event_dedup`: `DUPLICATE created` = 0 en el anfitrión; `user 2` en `2779 at the server's receive` baja exactamente en lo que el invitado cuenta como `WITHHELD`; y por cada clave `WITHHELD` del invitado, el anfitrión tiene su 2779 y su 2250 (`the host's client's … got no 2250` = 0).

**Aborto:** el invitado vuelve a `docked_gimmicks_other=probe` y `gimmick_event_dedup=probe` si:
- se cierra o se cuelga cualquiera de los dos;
- aparece un tipo de error de juego nuevo;
- el anfitrión deja de ver algo que ya veía;
- sale un `REWRITTEN` que no sea «Game Mechanic»;
- `TOTAL for the host's` o `TOTAL for its own` es mayor que 0;
- `DIFFERENT` (ahora solo `+0x90`);
- hay una 2779 retenida sin la 2779 o el 2250 del anfitrión;
- **(revisión M1 de `d5fdf82`)** crece del paso 1 al paso 2, en algún *opcode*, lo que manda el usuario 2 con la clave del **dispositivo del anfitrión** o del **personaje del anfitrión** en `+0` o `+1`. Se mira en la línea del anfitrión `DockedGimmicks summary, user 2 requests of any opcode naming the host's device or character …` (por *opcode*; las primeras 3 de cada uno salen como `DockedGimmicks #n user 2 request <op> naming the host's DEVICE|CHARACTER …`) y en las capturas, con un uso comparable del gancho y del tiempo en el mundo del anfitrión.
  - El motivo: con `+0x12` reescrito, el cliente del invitado cree que controla el dispositivo del anfitrión. Podría mandar Req que no están en `kWithheldOps` (transformada 2556/2828/3067, que hoy pasan y el servidor podría aceptar: la posición del dispositivo del anfitrión saldría entonces de la simulación del invitado; o 2221) o Req con la clave del personaje del anfitrión como padre (2380/2387/2356…).
  - La base de S28 (paso 2, mundo del anfitrión, 532 s) en la captura del invitado: 2556 ×1001, 2828 ×24, 3067 ×132 y 2774 ×12 con las claves del dispositivo del anfitrión; con la del personaje del anfitrión, la 2249 del bucle de acople (×8 en el primer tramo, ×1061 en 100 s tras volver a entrar).
  - En la S29, el paso 2 lleva `tree_bend=probe` (el paso 3 lo activa): la comparación paso 1 → paso 2 no tiene el doblado de los árboles activo en ninguno de los dos pasos.

**Riesgos:**
- **Visual:** las dos simulaciones locales difieren unas décimas, y el réplica emite menos eventos que el dueño.
- **Autoridad:** no cambia, porque las Req del invitado no salen de su PC.
- **2774:** retenerlo quita la única escritura de alguien que no es el controlador que hay hoy (`comp+0x42D`, 2774 ×6 por creación).
- **Bucle de 2249:** se sigue midiendo el que manda el invitado hacia `A0100001`. En el paso 2 de S28 bajó de 6777 a 8 en el primer tramo, pero volvió a 1061 en 100 s tras volver a entrar: no queda claro (§2.3).
