# P8: el cliente real del invitado no crea actores del campo del anfitrión

Build **1.0.0.2976**, base `0x140000000`; todas las direcciones son RVA. Análisis estático de solo
lectura del 2026-10-09 (sin juego, sin tocar código ni `D:\SteamLibrary`).

- **Herramientas:** capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` y las
  decompilaciones ya exportadas en `exports\decomp\`. No pude levantar un servidor de Ghidra propio:
  el permiso de `server-start.ps1` fue denegado.
- **Scripts:** en el scratchpad de la sesión (`a2005.py`, `cc.py`, `ops.py`, `firstact.py`,
  `d.py`, `rp/errtab2.py`, `scan*.py`, `riprefs.py`, `callers.py`), ejecutados con `python -I`.
- **Marcas:** **[V]** verificado en el código (`.rsrc`, o `.xtls` leído como bytes), en la captura
  o en el log; **[E]** inferido.

**Evidencia.**
- Logs del invitado (`p8c\shadow3\cdmp.1.log`, sesión 1; `cdmp.log`, sesión 2).
- Capturas `cdmp_packets_20261009-161253.*` (S1) y `…161853.*` (S2). Los números de línea que cito
  son de sus `.log`.
- Log del anfitrión `backups/logs/cdmp_20261009-1710_p8e-host.log`.

**Reloj [V].** El `t` de la captura es QPC. Lo anclo con la 2490 de cada sesión:
- **S1:** `t0` = 16:13:21.507 (hora de Shadow). La 3093 cuadra con un error de 2 ms.
- **S2:** `t0` = 16:19:18.304. La 3093 cuadra con un error de 3 ms.

## Resumen

1. **El usuario del anfitrión sí llega al invitado, y por la vía nativa [V].**
   - El 2005 de S1 l.1223 (`t`=86.640) trae `U(0x90100000)`, `A0100001` (Kliff del anfitrión),
     otra vez `U(0x90100000)` y `B0100002`. El registro de usuario de tipo 0 lo antepone el
     serializador de `NotifyAppear` (`.xtls 0x10651260`).
   - El invitado **sí tiene** el actor `A0100001`: 2,4 s después manda un
     `OnDemandEquipDockingGimmickSpawnReq` 2249 con esa clave (S1 l.1310; S2 l.1521).
   - Conclusión: **ni el "usuario desconocido" ni el `characterNo` causan estos fallos.**
2. **Hay tres fallos distintos, ninguno de identidad:**
   - **(A)** `B0100002`, el compañero del anfitrión, falla en `CreateOwnedActor`. Es el mismo
     caso que C1 en sentido contrario: el hijo llega en la misma tanda que su personaje principal
     **[V]** y no puede engancharse a él porque todavía está cargando **[E]**.
   - **(B)** Los ~10 "loginCharacter" son **NPC del campo del anfitrión** (dueño 0, control
     `0x90100000`), no su grupo **[E fuerte]**. Fallan en la **restauración de la acción desde el
     registro** (`0x8646D0` → `0x863880`) con `eErrNoInvalidActionChart` **[V código + código de
     error]**. En un jugador ese camino nunca se ejecuta para NPC, porque el cliente los controla.
   - **(C, S2)** El NPC `B0100415` "Loaded Bull" (tipo 3, aparecido en S2 l.2651) falla al crear su
     CC con `eErrNoInvalidCharacterData` **[V]**.
3. **La carga eterna [E fuerte].** El estado `0x12` de la máquina de cambio de campo del cliente
   (`0x7797F0`) espera a que cada actor de una lista esté "listo" (`+0xC2 && +0xCF`). Un actor cuyo
   `loginCharacter` falló nunca pone `+0xCF` **[V código]**. Además, el invitado **nunca manda
   `LoadingCompleteReq` 2445** tras el segundo viaje **[V]**.
4. **El cierre de S2 (`0x1F63FE6`) [V/E].**
   - Ocurre en `cdmp_ActionLayer__AdvancePlayback` (`0x1F63F70`, el `kActionLayerAdvance` del
     mod), con `this` = `&capa+0x18` y la reproducción nula. Es la firma conocida de un actor remoto
     (modo 2) con gráfico y sin acción **[V]**.
   - El invitado **no tiene instalada ninguna guarda de capas** **[V]**: ni "Puppet: action-layer
     crash guard" ni HorseDiag aparecen en sus logs, porque tiene `co-op off`.
   - El candidato más probable es `B0100415` **[E]**: su CC salió mal creado y recibe 4
     `StartActionAck` entre 72.865 y 73.671 s, justo antes de que se corte la captura (74.0 s) y
     acabe el log (~76.6 s).
5. **Recomendación** (§4), todo en `.rsrc` y en el invitado salvo lo indicado:
   - (i) instalar en el invitado la guarda de capas para todo actor con `actor+0x90` ≠ usuario
     local;
   - (ii) para actores remotos, tratar una restauración con gráfico inválido como "sin
     restauración" (el byte 0 nativo), de modo que el login termine y la puerta de carga se abra;
   - (iii) retener en `guest_deliver` los hijos ajenos hasta que su personaje esté en `0x10`;
   - antes de nada, **una sesión de diagnóstico** de solo lectura (§4.4).
   - Descarto reescribir dueños o controladores en el anfitrión (§4.3).

## 1. Qué actores fallan

### 1.1 Línea de tiempo

| | S1 (`t` s) | S2 (`t` s) | Fuente |
|---|---|---|---|
| `MoveFieldAck` 2174 del 2.º viaje (dueño reescrito a `9010022A`/`90100369`) | 77.131 (l.1075) | 56.547 (l.1218) | [V] |
| `MoveFieldCompleteReq` 2549 | 85.989 (l.1087) | 64.019 (l.1238) | [V] |
| 2005 con `U(90100000)`, `A0100001`, `U`, `B0100002` | 86.640 (l.1223) | 64.954 (l.1481) | [V] |
| **"Actor 생성에 실패"** `…90100000B0100002` | **86.661** (+21 ms) | **64.977** (+23 ms) | [V] log |
| 10 × "onCharacterResourceLoadComplete" + "loginCharacter" | **88.918–88.984** | **65.841–65.876** | [V] log |
| 2249 del invitado para `A0100001` (Nak 2829 de vuelta) | 89.071 (l.1310) | 65.922 (l.1521) | [V] |
| 2005 con `B0100415` "Loaded Bull" en (-10610.44, 600.0, -3916.6) | — | **67.881** (l.2651) | [V] |
| "케릭터 데이터가 비정상적" + "createCharacterControl" con esas mismas coordenadas | — | **67.897** | [V] log |
| `MoveFieldCompleteAck` 2144 | 93.795 (l.8897) | 72.752 (l.9168) | [V] |
| `LoadingCompleteReq` 2445 del 2.º viaje | **nunca** (fin 300.4) | **nunca** (fin 74.0) | [V] |

En el anfitrión, la fase del usuario 2 se queda en `2445 1, 2762 1` (solo la del primer campo)
desde 17:15 hasta la salida **[V]**.

Floats de "createCharacterControl" **[V]**:
- `0x44160000_C625C9C4` = x -10610.44, y 600.0;
- `0x…C574C9AF` = z -3916.6;
- las mismas coordenadas en local de sector (-610.44, 600, -916.6).

`B0100415` es la única aparición de `C625C9C4` antes del error.

Contenido del campo del anfitrión que llegó al invitado en S1, entre 50 y 100 s (`a2005.py`)
**[V]**:

| Tipo de registro | Dueño | Control (`rec+0x12`) | Cuántos |
|---|---|---|---|
| 1 (Kliff del anfitrión `A0100001`) | `90100000` | `90100000` | 1 |
| 6 (su compañero `B0100002`, ranura 1) | `90100000` | `90100000` | 1 |
| 3 (personas) | 0 | `90100000` | 192 |
| 4 (personajes con nombre: Damiane, Matthias, Renee, Delkin, Rhett, Alden) | 0 | `90100000` | 6 |
| 5 (monturas y carros) | 0 | `90100000` | 4 |
| 6 (animales) | 0 | `90100000` | 35 |
| 7 (gimmicks) | 0 | `90100000` | 211 |
| 1 y 6 propios del invitado (`A010022B`, `B010022C`) | `9010022A` | `9010022A` | 4 + 1 |

### 1.2 `B0100002`: compañero del anfitrión [V]

- El valor `0x90100000_B0100002` es `[rsp+0x70]` del marco de `0x8BD530`: clave en los 32 bits
  bajos y dueño en los altos. Rama `0x8BDC4A`: falló `0x8AF580` (método de
  `data/c1-actor-rechazado.md` §1.1–1.3).
- Llega **en el mismo paquete y justo detrás** de su personaje principal `A0100001`.
- Sin sonda en el invitado no puedo separar la condición (b) de la c5 de C1 §1.4. Lo más probable
  es **c5** **[E]**: el usuario `0x90100000` acaba de pasar a estado 4 con su registro de tipo 0 y
  `A0100001` sí se crea, pero en ese momento sigue en estado 8 y no en `0x10`. Es la misma firma
  que el mercenario del usuario 2 en C1.
- D-53 no lo retiene en el anfitrión: su byte de restauración es 1 (el anfitrión ya le dio acción),
  y la retención solo actúa con 0.

### 1.3 Los ~10 "loginCharacter": NPC del anfitrión [E fuerte], decodificados [V]

**Qué codifica `0x40000000001` [V]** (`0x4FD2E0` `cdmp_ActorLogin_FinishGroup`):

| Valor | Posición | Significado |
|---|---|---|
| `0x13ECB90` | `[rsp+0x60]` | puntero al búfer en línea `[rsp+0x70]` del vector del grupo |
| `0x400_00000001` | `[rsp+0x68]` | **número de actores en el grupo = 1** y capacidad `0x400` (`0x4FD30F`/`0x4FD314`). `…0002` = un grupo de **2** |
| `0x14557D660` | `[rsp+0x70]` | vtable de la primera entrada (0x20 B) del grupo |
| ptr | `[rsp+0x78]` | el **actor** (`rdi = [entrada+8]`) |

Cada línea es, por tanto, un actor (o dos en el último caso de S2).

**De dónde sale el error [V].**
- **Código:** 882834785 = `0x349EFD61` = **`eErrNoInvalidActionChart`** ("비정상적인
  액션차트입니다", global `0x6CF6C50`). Lo calculé con lookup3 `hashlittle(nombre, 0xC5EDE)` sobre
  la tabla de 1559 errores.
- **Cadena:**
  - `FinishGroup` comprueba `[actor+0x5E] == 8` y llama a `vf78`;
  - luego a `vf140` = `cdmp_Actor__vf40_loginCharacter` `0x17E94E0`, que recorre los componentes
    (`actor+0x78`, entradas de 16 B) y llama a su `vf19` (`+0x98`);
  - el que falla es `ClientCharacterControlActorComponent::vf19` → `.xtls 0x984D830` (solo
    leído), que registra "onCharacterResourceLoadComplete" en `0x984D9AF` (retorno `0x984D9B4`).
- **Valores de esa línea:** son los registros que `0x984D830` guarda en su prólogo.
  - `0x13F4BD0` es el `&out` del llamador.
  - `ptr`/`ptr+0x1D0|0x1B0` son el iterador y el final del vector de componentes de
    `loginCharacter`.
  - `0x1417E9540` es el retorno a `0x17E9540` (`loginCharacter+0x60`).
  - Sale `0x1D0` o `0x1B0`: el CC está a 29 o a 27 entradas del final. Hay **dos clases de actor**
    (en S1, 2 de un tipo y 8 del otro) **[V]**.
- **Lógica de `0x984D830` [V]:**
  - `CC+0x228 = 1`;
  - si `[[CC+0xB8]]+0x130 == 0` → `eErrNoInvalidCharacterResource` (no es el nuestro);
  - si `actor->vf188(5)` (el modo de control) es verdadero, **no restaura** y sale con éxito;
  - en el caso montura/vehículo (`vf298` ∈ {5, 9}) usa `0x232FF90`;
  - si no, llama a **`0x8646D0` `RestoreActionsFromLoadData`**.
- **Lógica de `0x8646D0` y `0x863880` [V decompilado]:**
  - con byte de restauración `[[CC+0x40]+0x30]` = 0 no hace nada;
  - si no, lee de cada capa el hash `+0x3C` **solo si la capa tiene gráfico `+0x110`**;
  - `cdmp_CharacterControl__RestartLayerAction` `0x863880` devuelve **`eErrNoInvalidActionChart`**
    si las dos capas quedan con hash 0, o si un hash no está ni en el gráfico de la capa ni en el
    por defecto (`CC+0x70/+0x78`, vía `cdmp_ActionGraph_Find`).

**Por qué solo ahora [V/E].**
- En un jugador, todo NPC tiene control = usuario local (modo 3; `enemigos-del-anfitrion.md`
  §1.1). `vf188(5)` es verdadero y la restauración se salta: **el cliente nativo nunca restaura un
  NPC** **[E fuerte]**.
- El cliente del invitado es el primero que recibe NPC de otro controlador (`0x90100000`, modo 2)
  ya en marcha, con byte de restauración 1. ~240 personajes pasan por esa rama y casi todos la
  superan. **Unos 10 no encuentran su acción.**
- Hipótesis **[E, sin confirmar]**: son NPC con una acción que pertenece a un gráfico que les da
  otro objeto (un gimmick de interacción, carro, asiento o secuencia). El cliente controlador lo
  engancha en local; un cliente que no controla no lo tiene.
  - El último grupo de S2 es de 2 actores (jinete + vehículo).
  - Hay conductores y carros en el campo (`Wagon Driver` `B0100220`, `Narrow Fish Wagon`,
    `Herspia Wagon Horse`).
- **Identificar las claves exactas no es posible con lo registrado**: el log solo da punteros. Lo
  resuelve el diagnóstico de §4.4.
- **No se puede descartar** que `A0100001` sea uno de los 10: existe en el invitado, pero no sé si
  su login terminó. Su capa 0 trae `0x25DC7FB9`, que no es la espera `0x26107D3C`.

### 1.4 `B0100415` "Loaded Bull" (solo S2) [V]

- "케릭터 데이터가 비정상적" sale de `0x865E27`, dentro de `0x864FF0` (inicialización del CC, que
  llama `.xtls 0x985E3AD`).
- El fallo es `0x468430([CC+0xB8], …)` = falso; el `0x467550` interno da
  `eErrNoInvalidAppearance`, `eErrNoInvalidCharacterGamePlayData` u otro. Esos casos los registra
  `0x473280`/`0x473400`, que el mod no engancha.
- El código del error es 3650191347 = `eErrNoInvalidCharacterData` **[V]**.
- No apareció "Actor onProcessLoadData…" (`0x8BDE34`, sí enganchado). Por eso creo que **el actor
  siguió vivo con el CC a medio inicializar** **[E]**.
- Es una aparición reciente (clave `0x415`, posterior a la entrada del usuario 2). La causa de
  datos queda abierta **[E]**.

## 2. Qué estructura del cliente hace falta y qué paquete la llena

| Pieza | En qué falla | Quién la llena de forma nativa | Estado en P8 |
|---|---|---|---|
| Usuario dueño en el `ClientActorManager` (`0x8ADFE0`/`0x8AE390` → `ClientActorContainer::Find`, `+0x5E & 4`) | solo actores **con dueño** (tipo 1 e hijos) | registro de tipo 0 (9 B) que el serializador de `NotifyAppear` antepone a cada actor de usuario en el 2005 | **llega** (`U(90100000)` en l.1223) [V] |
| Personaje principal del dueño en estado `0x10` (enganche c5, `0x206B535`) | hijos con dueño (`B0100002`) | el login asíncrono del personaje, ~70–80 ms en un jugador | el hijo llega **en el mismo paquete** [V] → falla [E] |
| Gráfico y acción restaurable de cada capa (`CC+0x88/+0x90`, `+0x110`, `+0x3C`) | NPC en modo 2 con byte de restauración 1 | el registro 2005 (bloque del CC) y los datos del juego | ~10 sin acción encontrable [V código / E causa] |
| Primera acción por red (`StartActionAck` 2991 / `StartSpawnActionAck`) | NPC en modo 2 sin restauración | relevo del servidor desde el controlador (anfitrión) | S2: mediana **6,6 s** tras aparecer (p90 7,9 s), detrás del atasco del 2005. S1: de 293 actores, solo 51 recibieron alguna antes de los 300 s, y el anfitrión apenas generó acciones (`finders control` +106 en 3 min) [V] |

`MoveFieldAck` es el único caso en que el cliente necesita que el dueño sea él mismo, y ya está
resuelto (eb30080). **No hace falta otro paquete de "presentar al usuario".**

## 3. Por qué la carga no termina

Máquina de cambio de campo del cliente `0x7797F0` (`switch [P+0x15C]`;
`P = *(g_WorldSystem+0x60)`) **[V decompilado]**:

| Estado | Espera a… | ¿Lo bloquea un actor fallido? |
|---|---|---|
| `0x0E` (`0x77C320`) | el personaje local listo; cada clave de `P+0x280` y de las entradas del `MoveFieldAck` (`P+0x270`, paso `0x30`, clave en `+4`) **existe** y tiene `+0xC2 && +0xCF` | solo los propios (`A010022B`), que sí están listos: manda `StartActionReq` desde 95.7 s |
| `0x11` | `[0x9A5BE0(Q)+0x231] > 1`: componente del personaje del **dueño** (ahora el propio, por eb30080) | no |
| **`0x12`** | una lista de claves en el `ClientActorContainer` (`[[P+0xB8]+8]+0x78`, número `+0x80`). Cada clave se quita si **no existe** o si `vf1D8` (`0x4FE240`: `+0xC2 != 0 && +0xCF`) es verdadero; **en cuanto una existe y no está lista, se queda esperando** | **sí** |
| `0x13`–`0x17` | otros (`0x63EC40`, `0x77CFB0`, `0x77D1F0`, banderas `+0x16C`, `+0xC3`) | no por actores |

- **`+0xCF` [V]** solo lo pone `cdmp_ClientActorManager_ProcessPendingLogins` (`0x8B816B`), tras
  `+0xC1` y con todos los componentes `vf88` listos.
- Un actor cuyo `loginCharacter` falla queda creado (lo encuentra `Find`) pero **nunca llega a
  `+0xCF`**.
- `B0100002`, que no se creó, no bloquea: `Find` da nulo y la clave se quita.
- **Conclusión [E fuerte]:** el túnel eterno de S1 es el estado `0x12` esperando a alguno de los
  ~10 actores con login fallido. **[E]** El escritor de la lista no lo localicé: está en `.xtls` o
  en un ayudante genérico. El diagnóstico de §4.4 lo confirma leyendo `P+0x15C` y la lista.
- **Efectos secundarios [V]:**
  - el juego del invitado sigue vivo (manda `StartActionReq` de su Kliff y su compañero);
  - entra en un bucle de **2165** `OnDemandEquipDockingGimmickSpawnReq` 2249 por el NPC
    `B010021E` (Mushroom Specialist), con 2164 `UpdateOnDemandEquipDockingNak` 2829 (S1 l.2640 en
    adelante);
  - pide el *docking* de NPC que no controla, y el servidor se lo niega;
  - también dispara `CreateProjectileFromGimmickEventReq` de un gimmick del anfitrión
    (`B0100151`, l.2013).

### 3.1 El cierre de S2 en `exe+0x1F63FE6`

- **[V]** `0x1F63F70` = `cdmp_ActionLayer__AdvancePlayback` (`offsets.h: kActionLayerAdvance`).
  - `rcx` = `&capa+0x18`, la ranura de la reproducción de la acción actual;
  - `0x1F63FDA mov rax,[rcx]` lee esa reproducción, que es nula;
  - `0x1F63FE6 mov rdx,[rax+0x10]` falla.
- **Llamadores [V]** (`caballo-coop.md` §9.1):
  - `0x2329700` `cdmp_CharacterControl__AdvanceActionLayers`: solo comprueba `capa+0x110`, no
    `+0x18`;
  - por encima, `0x863E80` ← `0x85FBA0` `CC::Update` ← `0x8B56D0` (tarea paralela de actualizar
    actor);
  - los otros dos llamadores, `0x2BCBF50` y `.xtls 0xCA9B360`, tampoco lo comprueban.
- **`this` no es el objeto que no se creó [V/E]:**
  - es un campo de la capa de un CC **que existe**;
  - `B0100002` no existe (Find nulo);
  - un CC cuyo `onLoginComplete` falló se queda con capa y sin acción.
- **Candidatos, por probabilidad [E]:**
  1. `B0100415` (CC mal inicializado, "character data abnormal"), que recibe 4 `StartActionAck`
     entre 72.865 y 73.671. Encaja con "acción presente, reproducción nula".
  2. Alguno de los ~10 con restauración fallida (gráfico sin acción) cuando se actualiza.
  3. Una aparición nueva en modo 2 antes de su primer `StartSpawnActionAck` (el caso del caballo
     copia).
- **[V]** El invitado no tiene guarda de capas, así que cualquiera de los tres lo cierra.

## 4. Opciones de arreglo

### 4.1 Recomendada (invitado, todo en `.rsrc`)

**(i) Guarda de capas en el invitado para actores remotos (obligatoria).**
- **Qué:** instalar en el invitado la guarda de `0x1F63F70`/`0x2329700` que ya existe
  (`remote_puppet.cpp`, `kActionLayerAdvance`; contención de D-52 en `horse_diag.cpp`), aplicada a
  todo actor con `actor+0x90` ≠ usuario local.
- **Por qué es raíz y no parche:** el invitado es estructuralmente un cliente que **no controla**
  la mayoría de actores del campo. Recibir una acción por red con retraso (aquí 6,6 s de mediana)
  es lo normal, y el motor no lo comprueba porque en un jugador no pasa.
- **Riesgo:** bajo. Ya está validada en el anfitrión. Congela la animación hasta la primera acción.

**(ii) "Sin restauración" en lugar de error, para actores remotos.**
- **Qué:** en la entrada de `0x8646D0` (`.rsrc`; quien lo llama, `.xtls 0x984D963`, no se toca), si
  el actor es remoto y `0x863880` devolvería `eErrNoInvalidActionChart`, devolver 0 sin rearrancar
  nada. Es exactamente lo que el juego hace con byte 0.
- **Efecto:**
  - `onLoginComplete` termina y `+0xC1`/`+0xCF` llegan;
  - el estado `0x12` deja de esperar y sale `LoadingCompleteReq`;
  - la acción real llega con el siguiente `StartActionAck` del anfitrión.
- **Riesgos:**
  - si el hash de verdad viene de un gráfico de interacción, el `StartActionAck` también fallará al
    aplicarse: el NPC se quedará quieto y contenido por (i), sin cerrar;
  - hay que comprobar antes (§4.4) que el fallo es "hash ausente" y no otra cosa.

**(iii) Reordenar los hijos ajenos en `guest_deliver` (`guest.cpp`, el hilo de red del invitado).**
- **Qué:**
  - sacar de cada 2005 los registros con dueño = un usuario ≠ el local y de tipo ≠ 1, junto con su
    `U(...)`;
  - retenerlos hasta que el personaje principal de ese dueño tenga `+0x5E & 0x10` en el cliente
    (lectura por `ClientActorContainer::Find`);
  - entregarlos como un 2005 nativo `[U][hijo]`.
- **Riesgos:**
  - la entrega es la nativa (`deliver_all`), con el mismo hilo y formato;
  - si el principal no llega nunca, hay que vaciar la retención al cambiar de campo.

### 4.2 Alternativa en el anfitrión para (iii)

- Extender D-53 (`child_hold.cpp`, ya envuelve `NotifyAppear` `0x2B000C0`): un hijo de cualquier
  usuario se entrega a un destinatario que no es su dueño **en otra llamada**, ≥ N ms después de su
  principal.
- **Riesgo:** depende del tiempo (en C1 hizo falta ~3 s con el caballo) y no ve el estado real del
  cliente. Es peor que (iii).

### 4.3 Descartadas

| Opción | Por qué no |
|---|---|
| (a) "Presentar el usuario del anfitrión" con otro paquete | ya ocurre: el registro de tipo 0 llega y `A0100001` se crea [V] |
| (b) Reescribir en los 2005 para el usuario 2 el control `rec+0x12` (`0x90100000` → usuario 2) | el invitado pasaría a modo 3 y ejecutaría IA local de todos los NPC. Habría dos IA por NPC, `Req` que el servidor niega (ya se ve con 2249 → Nak) y divergencia. Solo esquiva el síntoma |
| (b') Reescribir el dueño de `A0100001`/`B0100002` | el invitado los tomaría por suyos: chocaría la ranura 1 con su propio compañero (`eErrNoInvalidChildSlotIndex`) y tendría dos personajes principales. Incorrecto |
| Reescribir el byte de restauración a 0 en el anfitrión | su posición en el registro no es fiable (deserializador `0x23307D0` virtualizado), los NPC quedarían todos sin acción y seguiría haciendo falta (i). Es peor que (ii) |
| "Puesta al día" sintética de `StartActionAck` desde el servidor | no existe en el motor (`opcion-d-accion-usuario2.md` §1.4). Sería complejo y redundante si (i) y (ii) funcionan |

**Aparte:**
- el retraso de 6,6 s de los relevos detrás de la ráfaga de 2005 es cosa del transporte de Steam
  (prioridad, pendiente aparte);
- el bucle 2249/Nak y el `CreateProjectile` desde un cliente que no controla son pendientes
  menores.

### 4.4 Diagnóstico previo en el invitado (solo lectura, `.rsrc`, una sesión con Shadow)

1. Mid-hook en `0x4FD491` (rama de error de `FinishGroup`, `rdi` = actor): clave `[rdi+0x60]`,
   control `[rdi+0x90]`, tipo y `ebp`.
2. Entrada de `0x8646D0` (`rcx` = CC, actor = `[CC+8]`), solo para actores remotos:
   - clave;
   - byte `[[CC+0x40]+0x30]`;
   - por capa: `+0x3C`, `+0x110`, `CC+0x70/+0x78`;
   - resultado de `0x863880` (retorno en `0x864B1D`).
3. Cada 5 s: `P+0x15C` y la lista `[[[P+0xB8]+8]+0x78]` (claves, con `+0x5E/+0xC2/+0xCF` de
   cada una).
4. Guarda (i) con registro de la clave que contiene.

Con eso quedan identificadas las ~10 claves, se confirma el estado `0x12` y sale el actor del
cierre.

## 5. Implementado para el diagnóstico (2026-10-09)

Solo en el invitado, con dos claves de `[session2]` que vienen apagadas. El ini de la sesión es
`docs/research/ini/cdmp.p8-shadow-diag.ini`. Código: `src/mod/session2/guest_actor_diag.{h,cpp}`
(lógica pura y probada en `guest_actor_diag_logic.h`) y el predicado nuevo de `remote_puppet`.
**No están hechos** (ii) ni (iii).

- **Usuario local:** se lee cada segundo como `[[[exe+0x6D69190]+0x30]+0x50]+0x90` (el actor
  controlado del `ClientActorManager`; solo se acepta una clave de clase usuario `0x9…`). Hasta que
  se conoce, ningún actor cuenta como remoto.
- **Comprobaciones antes de enganchar:**
  - los bytes de cada punto se comparan con los del ejecutable;
  - la tabla de secciones del PE en memoria se usa para exigir que los bytes parcheados estén dentro
    de `.rsrc` (`[0x1000, 0x51EB000)`), nunca en `.xtls`;
  - si algo no cuadra, el punto se rechaza con una línea `GuestDiag: … REFUSED`.
  - Ninguna rama rel32 cae dentro de los bytes parcheados de `0x4FD491`, `0x8646FC` ni `0x864B1D`
    (barrido de todas las secciones ejecutables de la copia de solo lectura).
- **`guest_actor_diag=1`** (solo lectura):
  1. mid-hook en `0x4FD491` (rama de error de `FinishGroup`);
  2. `0x8646FC` y `0x864B1D`: los mismos puntos y bytes que la sonda de compañeros, ahora
     compartidos en `companion_probe.h` (`cmp::kRestore*`). Si esa sonda estuviera activa, no se
     engancha dos veces (en el invitado no puede estarlo: `guest=1` excluye `create_session`);
  3. cada 5 s, desde el informe del bucle de red: `[P+0x15C]` (`P = [[exe+0x6D69190]+0x60]`) y la
     lista `[[[P+0xB8]+8]+0x78]` (claves u32, número en `+0x80`). Cada clave se resuelve con una
     réplica propia de `ClientActorContainer::Find` (`0x464290`, sin llamar al juego).
- **`guest_layer_guard=1`:** la guarda de `remote_puppet` (`0x1F63F70` y el salto de `0x2329700`),
  ahora también para todo actor con `+0x90` ≠ usuario local (`puppet_guard_not_controlled_by`). Los
  actores propios no cambian.

**Líneas que hay que buscar en `cdmp.log` del invitado:**

| Línea | Qué dice |
|---|---|
| `Config: … actor diag 1 layer guard 1` | las claves llegaron |
| `GuestDiag ON (read only): FinishGroup error exe+0x4FD491 on \| restore … on \| loading state … on \| hooked bytes checked inside .rsrc …` | la sonda está puesta. Si un punto sale `OFF`, buscar `GuestDiag: … REFUSED` / `not the expected code` |
| `GuestDiag guard ON: …` | la guarda está puesta (o `could NOT be installed`) |
| `GuestDiag: local user key 0x9010…` | el usuario local; desde aquí se arma la guarda |
| `GuestDiag login FAILED (FinishGroup exe+0x4FD491): actor … key 0x… control(+0x90) 0x… (NOT controlled here) vt … +5E … \| error 0x349EFD61 (eErrNoInvalidActionChart)` | **las ~10 claves** (máximo 64 líneas) |
| `GuestDiag restore (0x8646D0 -> exit 0x864B1D) actor key … restore byte … \| L0 chart … hash(+3C) … default chart … \| L1 … \| result … -> RestartLayerAction 0x863880 FAILED` | por qué falla la restauración: hash, gráfico de la capa y gráfico por defecto (64 fallos y 16 éxitos como máximo) |
| `GuestDiag loading: state [P+0x15C] 0x12 … \| wait list N keys: 0x…(+5E … C2 … CF 0 … NOT READY LOGIN FAILED) … \| state 0x12 waits for 0x… \| in 0x12 for N s` | confirma la carga eterna y nombra la clave que la bloquea |
| `GuestDiag guard: holding actor 0x… (control 0x…, not the local user …) … \| its login FAILED` | cada actor que contiene la guarda (las 64 primeras claves) |
| `Session2 guest: … \| GuestDiag local user … \| login failures … \| restore … CHART FAILED … \| loading state 0x…, wait list …, waits for 0x… \| guard on: held actors … \| layer guards on … \| not controlled by 0x…: skipped …` | contadores, cada 5 s y cada minuto |
| `Puppet: crash guard: an action layer had no action (N times, from exe+0x…)` | la guarda de `0x1F63F70` paró un cierre (si el hilo del sniffer escribe ese resumen) |
