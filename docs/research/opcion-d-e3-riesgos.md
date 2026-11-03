# Opción D, etapa E3: riesgos para el anfitrión (análisis estático)

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Solo lectura del exe en disco (capstone, `build/research/cddis.py` y scripts propios). El juego no
se abrió ni se ejecutó `deploy.ps1`.

Leyenda: **[E]** leído en el desensamblado normal (`.rsrc`); **[E-x]** leído en código mutado de
`.xtls` (legible, pero ofuscado); **[S]** suposición razonada; **[V]** visto en capturas.

Escenario: usuario 2 (sesión 2, cuenta u64 = 1) manda en el servidor del anfitrión
`LoginCharacterToFieldServerReq` con **la misma ranura** que el anfitrión y luego
`MoveFieldCompleteReq`, `LoadingCompleteReq`, `LoginCharacterToGameReq`, `GameLoadingStartReq`,
`MoveFieldCompleteReq`, `LoadingCompleteReq`, desde un cliente sintético que no carga escena.

---

## Resumen y veredicto

**El mundo del anfitrión no se reinicia.** La carga de la ranura solo escribe en los componentes
del `ServerUserActor` del usuario 2 [E]. Además, cada `MoveField` saca del pool un **`ServerField`
nuevo cuyo dueño es el usuario que viaja** [E-x]. El usuario 2 vive en su propia instancia de
campo, con sus propias stages, gimmicks y pausa de hora. Hay cuatro efectos globales reales y un
cambio de expectativas:

| # | Qué puede salir mal | Gravedad | Mitigación barata |
|---|---|---|---|
| R1 | **La hora del mundo del servidor (`FieldTime` global `exe+0x6A6D238`) salta** a la hora guardada de la ranura. Pasa en el `MoveField` del usuario 2 (es dueño de su campo) y en su `LoadingCompleteReq` con byte `01`, que no comprueba el dueño (§1.4) [E] | media | Registrar los 32 B de `exe+0x6A6D238` antes y después de cada Req del usuario 2. Mandar `LoadingCompleteReq` con `00` (salta la copia, §1.4). Si hace falta, restaurar el valor tras el guion |
| R2 | **Autoguardado del usuario 2.** Tras su `LoadingCompleteReq`, el temporizador periódico 2011 lo incluye y pide `SaveGameDataAutoTimer` 2077 para él, sin mirar quién es (§3.2) [E-x]. Escribiría la ranura cargada, que es la del anfitrión [S] | alta sin P6; baja con P6 | P6 activo (ya bloquea ficheros). Fallo esperado: un `SaveToFileCurrentPlayerAck` 3016 con error a la sesión 2, sin reintento inmediato (§3.3). El anfitrión también falla al guardar (D-4) y puede ver el aviso |
| R3 | **Estado global del gestor de guardado.** Es un objeto único que comparten todos: guarda `+0x60`, "recarga pendiente" `+0x61/+0x64/+0x88`, búferes `+0x80/+0x90`. Cualquier `MoveFieldCompleteReq` consume esos búferes y cualquier `LoadingCompleteReq` borra las banderas, sea del usuario que sea (§1.5) [E] | baja (carrera) | No lanzar E3 mientras el anfitrión muere/revive con carga, viaja rápido o guarda |
| R4 | **Congelación breve del invitado en cada autoguardado:** 2077 para todos los hilos del servidor (barrera TLS `+0x4460`) mientras guarda (§4) [E] | baja | Ninguna para E3 |
| E | **Cambio de expectativa para E3:** como el usuario 2 está en otro `ServerField`, es probable que el cliente del anfitrión **no** reciba su `ActorLoadDataAck` por `ToNear`: los sectores son por campo [S fuerte] | — | En E3, mirar el log: ¿el `ServerField` del usuario 2 es distinto del del anfitrión (`field+0x180` = dueño)? Si lo es, hace falta una pieza nueva: que el usuario 2 entre en el campo del anfitrión (gancho en `0x2B07790`, §1.6) |

Otras cosas que no son riesgo para el anfitrión:
- **El "rescate" no está en el servidor** (§2). No hay temporizador del servidor que vigile la carga.
  Si el usuario 2 se queda a medias, su personaje se queda en "en traslado" dentro de su propio
  campo. Los fallos de carga hacen *logout* **del personaje que pidió**
  (`0x2068340(actor, 0xB, error)`), no del anfitrión [E/E-x].
- **La guarda `[gestor+0x60]` no bloquea:** si está ocupada, devuelve al momento el error
  `[exe+0x6CF7CA4]` y llega un `Nak` 1013 a la sesión 2 [E].
- **La pausa por menú** del anfitrión no toca la barrera `+0x4460`. Esa barrera solo la activa el
  autoguardado (§4) [E].

---

## 1. `LoginCharacterToFieldServerReq` con la ranura ya cargada

### 1.1 Cadena y guarda de concurrencia [E]

```
0x298BA10 (vf2) → 0x2C3E030(troc, TrocParams r13, u8, &characterNo, &ranura, &u32@13, wstr)
  r15 = TrocParams->actor = ServerUserActor del usuario 2
  1. busca entre SUS hijos ([[usuario+0x68]+0x18]) uno con ese characterNo ([[[hijo+0x68]+0x20]+0x70])
     - en modo un jugador (byte [exe+0x6CEEDE8] == 1) va a la carga lo encuentre o no
  2. 0x2C3E0FF: componente [[usuario+0x68]+0x10] (lobby), 0x9D9B30 vacía SU lista de lobby,
     0x2A1CF80 → ClearCharacterLobbyDataForSinglePlayAck con ToTarget(sesión del usuario 2)
  3. 0x186F570(gestor = [[exe+0x6D69698]+0x48], &res, ranura, usuario 2)
  4. si res == 0: 0x2A82F90 [VM] crea/coloca el personaje → wstring y u32 en [[pj+0x68]+0xB8]
     → 0x2940EE0 [VM](…, ranura) → 0x29878B0 (MainPlayerData…, ToTarget) → 0x206B750(usuario, pj)
  5. si algo falla: log 0x9116E0 ("Actor[%lld/%lld/%S]login character") y, si el error es de
     tipo 1 en la tabla 0x1427C10, Nak 1013 (0x3F5, 17 B) por 0x2773630 a TrocParams->session
     (la sesión 2). Si 0x2940EE0 falla: +0x98 = error, +0x9C = 0xB, vf80/vf110 → logout del pj
```

`0x186F570` (`optd/e3/d_186f570.asm`):
- comprueba la ranura (0-2, 3, 100-108, 200-207; si no, error `[0x6CF7900]`) y el dueño (si es
  nulo, error `[0x6CF6B68]`);
- **guarda `[gestor+0x60]`**, con el lock `[gestor]`:
  - si vale 0, la pone a 1 y sigue;
  - si no, devuelve **al momento** `[0x6CF7CA4]`, sin esperar;
  - la vuelve a poner a 0 al terminar, tanto si sale bien como si falla;
- `0x20688A0(usuario, &res, [gestor+0x50], ruta "%#%#", ranura)` hace la carga real;
- si sale bien y la ranura es `0xCE`, `0x1871040` borra esa ranura de un solo uso (no es la
  nuestra).

**Con la ranura del anfitrión ya cargada no hay rechazo:** la guarda solo marca "operación del
gestor en curso" y la carga del anfitrión terminó hace rato. **Solo falla si coincide con otra
operación del gestor.** La misma guarda la usan:
- `UserLogin` `0x2C17520`;
- el volcado de guardado `0x1871930` (de 2070);
- el borrado `0x1871040`;
- `SaveDataSlotRefreshReq` `0x2A22380`;
- `.xtls 0x103BE8E0` (ruta de `SaveToFileCurrentPlayerReq`);
- `NewGame`/`CreateCharacter` (`.xtls 0x1069F640`).

Si choca con un guardado del anfitrión, uno de los dos recibe `0x6CF7CA4`: el login del usuario 2
falla con Nak, o el guardado del anfitrión con `SaveToFileCurrentPlayerAck` de error.

**Ojo, ya en E2:** `UserLogin` `0x2C17520` (el `LoginUserToFieldServerReq`) **ya carga una ranura
entera en el usuario 2**: la más reciente de 0-2/100-108, salvo las opciones `SafeLaunchDummyTest`
o `@@@NO_SVC_COMMAND_141`, y la carga por `0x186F570` en `0x2C17881` [E]. Así que E2 ya ejerce la
carga de componentes. Si E2 no rompió nada, es buena señal para B4.

### 1.2 Qué escribe la carga: solo componentes del usuario 2 [E]

`0x20688A0` recorre `[usuario+0x78]` (n = `[usuario+0x80]`, pasos de 16 B). Para cada componente:
- `vf(0xD8)` da el nombre;
- se busca la sección en el `MemoryArchive`;
- se llama a `componente->vf(0x50)(res, versión, archivo, ranura)`.

Componentes de servidor con carga real (vf `0x50` ≠ el stub `0x35EA90`; `optd/e3/comps.txt`):

| Componente | vf `0x50` | Componente | vf `0x50` |
|---|---|---|---|
| ContentsMisc | `0x28B7360` | Quest | `0x2908090` |
| EquipSlot | `0x20C24E0` | QuestDialog | `0x29042F0` |
| Faction | `0x2869790` | Sequencer | `0x2BE7890` |
| Friendly | `0x286A990` | Skill | `0x2BED910` |
| GameEventHandler | `0x28FE9C0` | Status | `0x293FE70` |
| Inventory | `0x212A8D0` | Store | `0x2BFE260` |
| Knowledge | `0x2B73CC0` | SubLevel | `0x2C0F650` |
| Lobby | `0x2B7F4D0` | TransformSync | `0x2BC2970` |
| MercenaryClan | `0x2146F30` | Wanted | `0x25D6B10` |

Barrido de globales (`globscan.py` y `realmrefs.py` a profundidad 1;
`compload_glob.txt`/`compload_realm.txt`):
- Las únicas escrituras RIP-relativas son inicializaciones perezosas de estáticos (`0x195xxxx`)
  y contadores de memoria.
- Las únicas referencias por realm son **lecturas** de `exe+0x6D69528/530` (datos estáticos del
  juego) y del reloj `0x6D69430/0x6C8CD80`.
- La carga de ContentsMisc, la más grande, escribe en su propio componente (`r12+0x380`…).

Conclusión: **misiones, facciones, conocimiento, inventario, stages de secuenciador y SubLevel se
cargan en los componentes del usuario 2, no en un gestor global** [E, profundidad 1; no es
exhaustivo para llamadas indirectas]. Lo que hay de "mundo" en la partida (hora por nivel en
TransformSync `+0x5D0`, gimmicks del campo) se aplica después, al viajar, y solo al campo del que
el usuario es dueño (§1.3, §1.6).

### 1.3 El campo es por usuario: aislamiento del mundo [E-x] + [S]

- **Adquirir campo:** `MoveFieldOnceTimer` 2027 (lo crea `GameLoadingStartReq` →
  `0x2BC9200` → `0x2BC9470` → … → `.xtls 0x106DE160`) → `0x2BBFA80` →
  `0x2B07790(gestorCampos = [[exe+0x6D696A0]+0x40], &res, tipo 2, &out, usuario = [pj+0xA0])`.
  Pasos:
  - busca la entrada del nivel;
  - `0x2B08CB0` → `.xtls 0x1066CDB0` **saca un `ServerField` del pool** (si la lista libre está
    vacía, construye uno con `0x2B0B640` → `0x2AF6670`);
  - lo reinicia con `0x2AF7360` (generación `+0x2C8` + 1);
  - `0x2AF7820` lo inicializa con el nivel;
  - **`field+0x180 = [usuario]+0x60`** (`0x2B079A7`);
  - `0x2AF7C20` lo arranca.
- **Comprobaciones de dueño** (`field+0x180 == [[pj+0xA0]+0x60]`) en:
  - `0x2BC0A30` (hora al viajar);
  - `.xtls 0xE52E900` (`0x21A1CC0`, hora del componente → global);
  - `0x2BC7670` (`MoveFieldComplete`: `0x2910DE0` y ramas de stages);
  - `0x2BCA3D0` (`0x29228C0` y `0x287DA50`, por `0x20DF100`);
  - `0x20DF100` = personaje principal del dueño del campo.

Consecuencia [S, fuerte]: **el usuario 2 recibe su propia instancia de campo**, y el estado de
mundo que se aplica al entrar se aplica a **su** campo. El campo del anfitrión no se toca. No he
visto ninguna rama que reutilice el campo de otro usuario. Lo que el pool devuelve es siempre un
campo libre, reiniciado. El efecto secundario se explica en §1.6.

### 1.4 Lo que sí es global: la hora del mundo (R1) [E]

- `0x2BC0A30` (dentro de `MoveFieldOnceTimer`), **si el que viaja es el dueño del campo** (el
  usuario 2 lo es del suyo):
  - toma la hora guardada para ese nivel en `TransformSync+0x5D0` (de **su** partida), o la hora
    por defecto (`0x6CF1158`…), o la fija del nivel;
  - **la escribe en el `FieldTime` global del servidor `exe+0x6A6D238`** (`0x2BC0C50`).
- `LoadingCompleteReq` 2445 → `.xtls 0x1049EA30` [E-x] → `0x2BCAF00(TransformSync del pj, &res,
  u8 de la Req)` → `0x219D7F0`:
  - si el u8 ≠ 0: `comp+0x468 = ahora`;
  - **`FieldTime` global (`0x6A6D238` en un hilo del servidor) = `comp+0x450`**;
  - `field+0x17C = 0` (quita la pausa de la hora del campo **del usuario 2**);
  - **no comprueba el dueño** (`0x219D82B`-`0x219D8F4`).
  - Después, sin depender del u8:
    - `pj+0xC3 = 1` ("cargado", lo usa el autoguardado periódico, §3.2);
    - `[[usuario+0x68]+0x20]+0x398 &= ~4`;
    - `[[exe+0x6D694E0]+0x20]+0x7C = 0xFFFF` (global del realm del servidor; no sé qué es);
    - **gestor de guardado `[[exe+0x6D696A0]+0x30]`: `+0x61 = 0`, `+0x64 = -1`, `+0x88 = 0`** (§1.5).

Efecto en el anfitrión: la hora que el **servidor** cree que es el mundo salta a la de la ranura
del disco, que es la del último guardado, o a la por defecto. El cliente del anfitrión tiene su
propia copia (`0x6A6D218`) y no se entera mientras no le llegue un `SetFieldTimeAck`. En las
capturas no aparece ninguno [V].

Lo que sí usa la copia del servidor:
- la lógica del servidor que depende de la hora (horarios de NPC, `UpdateFieldNightAndDay` 2017);
- el siguiente `MoveField` del anfitrión, que leerá ese valor [S].

**Mitigación barata:**
- `LoadingCompleteReq` con payload `00` en lugar de `01`. Se salta la copia y la quita-pausa de
  `0x219D7F0`. El resto del handler y su Ack siguen igual [E]. No evita la escritura del
  `MoveField` (el usuario 2 es dueño de su campo).
- Para eso: guardar los 32 B de `exe+0x6A6D238` antes del paso 3 y restaurarlos al final, en el
  hilo del servidor. Como mínimo, registrarlos en el log.

### 1.5 El gestor de guardado es un objeto único (R3) [E]

- Se llega a él por tres rutas: `[[exe+0x6D69698]+0x48]`, `[[exe+0x6D696A0]+0x30]` y
  `[[exe+0x6D696A8]+0x10]`.
  - Las tres usan la misma guarda `+0x60` y los mismos campos, así que casi seguro son el mismo
    objeto [S].
- `ReviveLoadingReq` 2362 → `0x2947E00` pone `+0x61 = 1`, `+0x64 = valor` y `+0x88 = 1`:
  "recarga pendiente al revivir".
- `MoveFieldComplete` → `0x2BC7670`:
  - si alguna de esas banderas está puesta, toma la rama de recarga;
  - **en todos los casos** llama a `pj->vf(0x458)(campo, gestor+0x90)` (`0x28FB800` →
    `0x2B7DDB0`, componente de lobby);
  - después `0x186DD70(gestor+0x90)` (lo vacía) y `0x1871680(gestor)`, que lee el registro
    pendiente `+0x80` y lo vacía (`0x1873140`) si es del nivel actual.
- Nadie comprueba **de quién** es lo pendiente. Si el anfitrión tiene una recarga o un registro
  pendiente y el `MoveFieldComplete` o el `LoadingComplete` del usuario 2 llegan antes, el usuario
  2 se lo come y el anfitrión carga mal.
  - Es una carrera: en E3 solo pasa si el anfitrión muere/revive o viaja durante el guion.

### 1.6 Consecuencia para el éxito de E3 (no es un riesgo) [S]

- El área de interés vive en el `ServerField`: `SetTransform` → `0x2AF8090`/`0x2AF8AF0`, que son
  métodos de `ServerField`.
- Si el usuario 2 está en otra instancia, **el anfitrión no será observador de su sector** y no
  recibirá su `ActorLoadDataAck`. Así que "¿ve el anfitrión al segundo Kliff?" probablemente
  dará **no**, aunque todo lo demás vaya bien.
- Qué mirar en el log de E3:
  - el `ServerField` del personaje 2 (`FieldAttacherForActor`, `0x2BC4280` sobre
    `[[pj+0x68]+0x1A0]`);
  - su `+0x180`, comparado con el del anfitrión.
- Si son distintos, la opción D necesita una pieza nueva: en `0x2B07790`, para el usuario 2,
  devolver el campo del anfitrión en lugar de sacar uno del pool.
  - Eso sí metería al usuario 2 en el mundo del anfitrión, y entonces las ramas "solo dueño"
    dejarían de aplicarle.

---

## 2. Carga sin completar o sin escena: no hay "rescate" en el servidor

Qué hace el servidor con cada Req de carga:

| Req | Handler | Qué hace en el servidor | Si falla |
|---|---|---|---|
| `GameLoadingStartReq` 2437 | `0x29C59D0` [E] | `0x2BC9200(TransformSync)` → `0x2BCA3D0` (envía `MoveFieldAck`; si el que viaja es el dueño del campo actual, `0x29228C0` y `0x287DA50`) → encola `MoveFieldOnceTimer` 2027 al momento (`.xtls 0x106DE160`) | Nak 1013 a su sesión y `0x2068340(pj, 0xB, error)` |
| `MoveFieldCompleteReq` 2549 | `0x29BF120` → `.xtls 0x10499470` [E-x] | `0x2BC6BB0(TransformSync)` → `0x2BC7670`: ráfaga de Ack, `UpdateStageOnceTimer` 2043 (`0x287EF60`), §1.5 | Nak 1013 y `0x2068340` |
| `LoadingCompleteReq` 2445 | `.xtls 0x1049EA30` [E-x] | `0x2BCAF00` (§1.4) + `LoadingCompleteAck` (`0x299E3A0`, ToTarget) | `0x2068340(pj, 0xB, 0x6CF6B54/0x6CF6EBC)` |
| `LoginCharacterToGameReq` 2601 | `.xtls 0x104967F0` | **VM de verdad** (tabla de saltos): no se puede leer | ? |

`0x2068340` → `.xtls 0xE0D9AF0` [E-x]:
- si el estado del actor (`+0x5E`) lo permite: `+0x98 = error`, `+0x9C = motivo`, `vf(0x80)` y
  `vf(0x110)`;
- `vf(0x110)` de `ServerChildOnlyInGameActor` es `0x28F26C0`;
- es el *logout* o expulsión **del personaje que mandó la Req**. No toca al anfitrión.

**Buscando el "rescate":**
- En la tabla de mensajes no hay ningún temporizador de "carga atascada". Los temporizadores de
  "Kill" son otra cosa:
  - `KillSelfOnceTimer` 2037 lo crean las invocaciones (`0x278BBB0`);
  - `KillActorByRunOutFatalRepeatTimer` 2007 está virtualizado y es el "fatal" agotado.
- No encuentro nada que vigile el tiempo entre `MoveFieldAck` y `MoveFieldCompleteReq`. En las
  capturas el cliente tarda 11 s y 18 s sin consecuencias [V].
- `UserOnceTimerProcessRepeatTimer` 1007 y `GamePlayOnceTimerProcessRepeatTimer` 1023 solo vacían
  colas de temporizadores por usuario (`opcion-d-d0.md` §10).
- Las cadenas `CheckMoveFieldComplete`, `WaitSyncLoadingComplete`, `CheckCompletedSceneLoading`…
  (`exe+0x59024F8`) son los **estados de carga del cliente**. Esa máquina de estados vive en el
  cliente.

Lectura de lo que vio World Builder [S]:
- Su `ReloadStageStartReq` (`0x2A5F580` → `0x2896120` → `0x2BC4950`, destino en
  `TransformSync+0x550`) mueve **al personaje del anfitrión**, que su propio cliente simula.
- El servidor lo aparca en la escena de aparcamiento (0, 1000, 0) mientras dura el traslado.
- El cliente, que no inició esa carga, no completa el apretón de manos. Su física deja caer y mata
  al personaje, que está aparcado a 1.000 m sin suelo.
- El servidor no simula jugadores (§2.8 del documento principal).

En E3:
- el personaje 2 no lo simula nadie;
- el cliente del anfitrión no lo controla (`+0xC8 = 0`) y, si está en otro campo, ni lo ve.

Así que **no espero ni teletransporte ni muerte del usuario 2 por quedarse a medias**. Y menos
aún del anfitrión.

Lo que sí ocurre si la carga se queda a medias [E]:
- el personaje 2 sigue con `SetMoveFieldAck(1)` ("en traslado") en su campo;
- `pj+0xC3` no se pone a 1, así que **no entra en el autoguardado periódico** (§3.2);
- se queda colgado un `ServerField` del pool hasta el logout (no hay ruta de logout legible,
  B6).

"Sin escena" (sin los `SceneLevelLoadComplete…`): el servidor no exige esas Req para mandar
`MoveFieldCompleteAck`. Se vio en la captura: el Ack sale 3 ms después de la Req y antes que los
`SceneLevelLoadComplete…` [V]. No he encontrado ningún contador que los espere [E, parcial].

---

## 3. Guardados durante el login del personaje 2

### 3.1 Disparadores en la secuencia de E3 [E]

Caminos hasta la petición de autoguardado `0x28FC5F0` → `.xtls 0x103BC7A0`. Esta función encola
`SaveGameDataAutoTimer` 2077 para el actor si `desc+1 == 1`. Los caminos salen de `uppath.py`:

| Origen | ¿En E3? |
|---|---|
| `GameLoadingStartReq` → `0x2BC9200` → `0x2BCA3D0` → `0x29228C0` (motivo 5) | Solo si el personaje es el **dueño del campo actual** y `[TransformSync+0x178] != -1` (una stage o secuenciador pendiente). El usuario 2 es dueño de su campo de aparcamiento, así que **puede pasar** si su partida trae algo pendiente [E] |
| `MoveFieldComplete` → `0x2BC6BB0` → `0x28F82A0` → `0x2BCA3D0` → … | Lo mismo |
| **`GamePlaySaveDataRepeatTimer` 2011** → `.xtls 0x10859D60`/`0x10859ED0` (§3.2) | **Sí**, tras el `LoadingCompleteReq` del usuario 2 |
| `SaveAutoReq` 3037, `UseItem*`, `PushEntitlementItemReq`, `RegistPlayGuideList*`, `PushTargetToInventoryReq`, `ProcessPickUpItemOnceTimer`, `DeleteSequencerOnceTimer`, `ReportGimmickDropableCheckResultReq` | No: el guion no los manda |

`LoginCharacterToFieldServerReq` (`0x2C3E030`) no guarda. Solo **lee** (`0x186F570`).

### 3.2 El autoguardado periódico incluye al usuario 2 [E-x]

`GamePlaySaveDataRepeatTimer` 2011 (`.xtls 0x10859D60`):
- decrementa un contador global `[exe+0x6DAA958]`; cuando llega a 0, lo pone a 10;
- entonces llama a `0x2C3A5B0` → `.xtls 0x10859ED0`.

`0x10859ED0`, en modo un jugador:
- toma la lista de actores del SAM (`0x2A83CB0`);
- por cada uno con `desc+1 == 1` que sea el personaje principal de su usuario
  (`[[pj+0xA0]+0xD0] == pj`), esté cargado (`pj+0xC3`) y cuyo nivel permita autoguardar (info
  `+0x6B`), pide el autoguardado (`0x28FC5F0`, motivo 3);
- **no mira el usuario.** En cuanto el usuario 2 manda `LoadingCompleteReq`, entra en la lista.

Al ejecutarse 2077 (`0x2C35EB0`):
1. barrera que para todos los hilos del servidor (§4);
2. `0x28FC6F0(pj)` → `0x186E1A0(gestor = [[exe+0x6D696A8]+0x10], …)`, **virtualizado**: no se ve
   a qué ranura escribe;
3. lo lógico es la de la partida cargada, **la del anfitrión** [S].

### 3.3 Reacción al fallo de escritura (P6) [E]

- `0x28FC6F0`:
  - si `0x186E1A0` devuelve error → `0x2A1FD40` → **`SaveToFileCurrentPlayerAck` 3016** con el
    error, por `ToTarget(pj->vf(0xE8))`: **a la sesión del personaje que se guardaba**;
  - no reintenta: el siguiente intento es la próxima vuelta del temporizador.
  - Si sale bien y hay datos pendientes, encola `FlushPendingSaveDataOnceTimer` 2070 (0x816) por
    `0x2774430`.
- `FlushPendingSaveDataOnceTimer` 2070 (`.xtls 0x10839370`):
  - `0x1871930` toma la guarda `+0x60` y comprueba `+0x58`/`+0x5C`;
  - si falla: 3016 + Nak 1013 a la sesión del usuario;
  - no se reprograma él solo [E-x].
- Para el usuario 2, esos Ack van a la sesión 2 (el sintético los tira).
- Con D-4, **el anfitrión también falla al guardar** mientras `[session2] enabled=1`. Su cliente
  recibirá 3016 con error en cada autoguardado. Probablemente verá un aviso de "no se pudo
  guardar" [S]. El handler del cliente `0xBE02B0` no lo he leído.
- Lo que no se puede ver es si `0x186E1A0`, que es una VM, reintenta por dentro o deja ficheros
  temporales a medias.
  - El bloqueo devuelve `ACCESS_DENIED` en `CreateFile*`/`MoveFileEx`. Si el juego escribe a un
    temporal y luego renombra, los dos pasos fallan [S].
  - Esto lo responde la prueba E0 ("intentar guardar una vez").

**Mitigación barata extra (opcional):** en la entrada de `ExecuteTroc`, saltarse el `Process` de
`SaveGameDataAutoTimer` 2077 cuando `TrocParams->actor` sea el personaje del usuario 2.
- Es un temporizador de una sola vez creado por petición, así que saltarlo no rompe ningún
  re-armado. Matiza D-5 [S].
- Ahorra el `ACCESS_DENIED` y la barrera.

---

## 4. La pausa por hilo del servidor (TLS `+0x4460`)

Estructura [E] (`f_27733f0_srvloop.asm`, `0x2C35EB0`, `0x27751D0`):
- **Bloque TLS `+0x4460`:**

  | Byte | Significado |
  |---|---|
  | `[0]` | petición de pausa |
  | `[1]` | "estoy parado" |
  | `[2]` | "procesando un paquete" |

- **Registro:** `0x27751D0` mete `{threadId, &tls+0x4460}` en la lista `ServerNet+0xF8`
  (n = `+0x100`). Lo llaman `ServerRunThread` (`0x2773407`) y `.xtls 0x100B1F10` (otro hilo del
  servidor).
- **Bucle de red `0x27733F0`:**
  - tras sacar un paquete, pone `[2] = 1`;
  - mientras `[0] != 0`, pone `[1] = 1` y hace `Sleep(10)`;
  - después procesa y pone `[2] = 0`.
  - El bucle de diferidas `0x27731C0` usa el mismo bloque.
- **Único escritor de la petición que he encontrado:** `SaveGameDataAutoTimer` 2077
  (`0x2C35EB0`).
  1. Pone `[0] = 1` en **todos** los hilos de la lista, y `[1] = 1` en el suyo.
  2. Espera con `Sleep(10)` a que todos los que estén en mitad de un paquete se paren.
  3. Guarda (`0x28FC6F0`).
  4. Vuelve a poner `[0] = 0`.
  - No hay más usos legibles: `optd/tlsoff.py 4460 a` solo da otras tres coincidencias en
    `.xtls`, que son ruido de mutación. `srvnet_f8.py` solo encuentra `0x2C35EB0`.

Respuestas:
- **Es una barrera de "parar el mundo" para guardar, no una pausa de menú.** No he encontrado
  ningún camino desde la interfaz o los menús del cliente hasta ella [E].
- Abrir un menú en el anfitrión **no** congela el servidor por esta vía. El bucle de red sigue
  con su `pop` de 1 s, y el reloj del servidor (`0x1416B80`, rama de servidor) es el real [E].
- Lo que sí se congelaría al abrir un menú [S]:
  - el **cliente** del anfitrión, que es quien simula la IA de todos los NPC (§2.8). El invitado
    vería los NPC quietos;
  - la hora del campo del anfitrión, que el cliente pausa.
- La barrera sí congela al invitado durante cada autoguardado: unos milisegundos, o lo que tarde
  el fallo de E/S con P6.
  - **Riesgo de bloqueo total:** si un hook nuestro se bloquea dentro de `OnRecvPacket` (con
    `[2] = 1`), el autoguardado espera para siempre. El mid-hook de la sesión 2 nunca debe
    esperar a nada.

---

## 5. `characterNo` (`0x7FFFFFFFFFFFFFFF`) e `itemNo` repetidos (parcial)

- **Servidor:**
  - `0x2C3E030` busca el `characterNo` **solo entre los hijos de su propio usuario** [E].
  - El SAM registra actores **por clave** (índice de 20 bits; `0x2A837E0`, `0x2A84F00`), no por
    `characterNo` [E].
  - No he encontrado ningún mapa global del servidor indexado por `characterNo` [E, búsqueda no
    exhaustiva].
- **`itemNo`:**
  - Los objetos viven en el componente de inventario de cada usuario.
  - Si el contador de números de objeto sale de la partida (hay `_itemNo`/`_reserveItemNo` en el
    guardado), los dos usuarios generarán **los mismos `itemNo` nuevos** [S].
  - No he localizado ninguna tabla global por `itemNo` en el servidor. `AquireItemNodeIdAck` 2535
    es S→C y no lo he seguido.
- **Cliente del anfitrión:** `0x8AEB30` devuelve el primer actor con un `characterNo` dado
  (`apariencia-estado-coop.md` §5.3) [V]. Solo importa si el anfitrión llega a ver al personaje
  2; con campos separados (§1.6), no lo verá.
- `ActorLoadDataAck` de usuario lleva el `characterNo` en `+0x17`. Si algún día el anfitrión
  recibe el del clon, su cliente tendrá dos actores con `0x7FFF…`, y es de esperar lo mismo que
  ya rompió la pantalla de equipo.

Pendiente: seguir los escritores de `_itemNo` y los mapas por UUID de gimmicks guardados
(`UpdateLogoutExcludedUuidListAck`, `UuidLookup` `0x1851010`, 212 llamadores) para descartar
choques globales.

---

## Direcciones nuevas (build 2976)

| RVA | Qué es |
|---|---|
| `0x186F570` | carga de ranura (guarda `+0x60` no bloqueante; errores `0x6CF7900`, `0x6CF6B68`, `0x6CF7CA4`) |
| `0x20688A0` | carga por componente (`comp->vf(0x50)`) |
| `0x2B07790` | adquirir `ServerField` del pool; `field+0x180 = clave del usuario dueño` |
| `.xtls 0x1066CDB0` | `Pool<ServerField>::Acquire` (crea con `0x2B0B640` → ctor `0x2AF6670`; reinicio `0x2AF7360`) |
| `0x20DF100` | personaje principal del dueño del campo (`field+0x180` → usuario → `+0xD0`) |
| `0x2BC0A30` | hora al viajar; escribe el `FieldTime` global **solo si es el dueño** |
| `0x219D7F0` | parte de `LoadingComplete`; escribe el `FieldTime` global **sin comprobar el dueño** si el u8 ≠ 0 |
| `.xtls 0xE52E900` (`0x21A1CC0`) | `TransformSync::SetFieldTime` (`comp+0x450`; global solo si es el dueño) |
| `0x2BCAF00` | `LoadingComplete` en `TransformSync` (también reinicia el gestor de guardado `+0x61/+0x64/+0x88`) |
| `.xtls 0x1049EA30` / `.xtls 0x10499470` | vf2 reales de `LoadingCompleteReq` / `MoveFieldCompleteReq` |
| `0x2BC6BB0`, `0x2BC7670` | `MoveFieldComplete` en `TransformSync` |
| `.xtls 0xE0D9AF0` (`0x2068340`) | logout o expulsión de un actor (`+0x98` error, `+0x9C` motivo) |
| `.xtls 0x106DE160` / `0x2BBFA80` | encolar / procesar `MoveFieldOnceTimer` 2027 |
| `0x28FC5F0` → `.xtls 0x103BC7A0` | pedir autoguardado (encola 2077 para el actor) |
| `0x28FC6F0` | guardar un actor (`0x186E1A0` en VM; 3016 si hay error; 2070 si quedan datos pendientes) |
| `0x2C35EB0` | `SaveGameDataAutoTimer` 2077 (barrera de hilos + `0x28FC6F0`) |
| `.xtls 0x10859D60` / `0x10859ED0` | `GamePlaySaveDataRepeatTimer` 2011 (cada 10 vueltas; todos los personajes cargados) |
| `.xtls 0x10839370`, `0x1871930` | `FlushPendingSaveDataOnceTimer` 2070 / volcado |
| `0x27751D0` | registro del hilo en `ServerNet+0xF8` (bloque TLS `+0x4460`) |
| `0x2947E00` | `ReviveLoading`: gestor de guardado `+0x61/+0x64/+0x88` |
| `exe+0x6DAA958` | contador del autoguardado periódico |

## Scripts y volcados (`C:\Emulador CD\build\research\optd\e3\`)

| Fichero | Qué es |
|---|---|
| `comps.py` → `comps.txt` | vtables de componentes con su vf `0x50` (carga) |
| `globscan.py` | escrituras y lecturas RIP-relativas en el grafo de llamadas |
| `realmrefs.py` | globales elegidos por realm (`cmovne`) |
| `uppath.py` | sube por los llamadores hasta un handler Troc del CSV |
| `reach.py` | caminos de llamadas desde una raíz hasta unos destinos |
| `savemgr_users.py`, `savemgr_fields.py` | usos de los holders del gestor de guardado y de sus campos |
| `srvnet_f8.py` | usos de la lista de pausa `ServerNet+0xF8` |
| `savecap.py` | Ack de guardado, rescate y carga en las capturas 112627/105149 |
| `d_*.asm` / `x_*.asm` | volcados: `186f570`, `20688a0`, `28b7360`, `28fc6f0`, `2c35eb0`, `2bcaf00`, `219d7f0`, `2bca3d0`, `29228c0`, `2bc0a30`, `2bc7670`, `2b07790`, `2bbfa80`, `x_1049ea30`, `x_10499470`, `x_10859ed0`, `x_10839370`, `x_1066cdb0`, `x_e0d9af0`, `x_106de160` |
