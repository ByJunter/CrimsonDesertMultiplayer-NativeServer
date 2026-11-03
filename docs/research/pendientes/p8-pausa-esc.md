# P8: la pausa del ESC (y de los demás menús) con un invitado conectado

10 de octubre de 2026, rama `opcion-d` (HEAD al escribir: `7ccc885`). Build del juego 1.0.0.2976. Direcciones en
**RVA** (base `0x140000000`). En este juego la sección de código se llama `.rsrc`.

**[V]** = comprobado en instrucciones (capstone sobre el PE de solo lectura
`D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, Python `-I`, o decompilación de Ghidra), en una captura o en un log.
**[E]** = inferencia o hipótesis pendiente de medir.

Solo análisis: no se tocó código, no se lanzó el juego, no se escribió en `bin64` y no se hizo commit. Para
Ghidra usé una **copia propia** del proyecto en el scratchpad, con mi propio servidor de consultas
(`-readOnly`, puerto 18523). Al terminar paré el servidor y borré la copia. El proyecto original
`D:\RE\CrimsonDesert_2976\project` no se tocó: su última escritura sigue siendo del 9-10 a las 16:40. No se propone
enganchar `.xtls` ni sus thunks.

Contexto: [p8-sesion6.md](p8-sesion6.md) §5 (ESC del anfitrión a las 23:52:35 y cierre `exe+0x47EFE20`),
[../pausa-copia-congelada.md](../pausa-copia-congelada.md) §1.1 (captura del 2026-10-05: el menú congela el reloj),
[../hora-del-dia.md](../hora-del-dia.md), [../opcion-d-e3-riesgos.md](../opcion-d-e3-riesgos.md) §4 (barrera
TLS `+0x4460`) y [../opcion-d-d0.md](../opcion-d-d0.md) (colas y temporizadores del servidor).

---

## 0. Resumen

1. **La pausa no es un indicador global ni un salto de la actualización. Es una escala de tiempo de la interfaz
   que multiplica el `dt` del frame [V].**
   - Cada raíz de UI lee al crearse un atributo `timeScale` de su layout (por defecto `"1"`) y lo guarda en su
     vista de escena (`+0x23C`). Lo hace `UIGamePlayControlCommonRoot::vf1` en `0xE41DA0`, escritura en
     `0xE41EA7`.
   - Cada frame, `0x35CD990` devuelve la escala de la primera vista de UI abierta cuya escala no sea 1. Si
     vale 0, `0xAD1300` la suaviza hacia 0 y multiplica el `dt` por ella. Cuando llega a 0 registra
     `"stopVibrationAll : UITimeScale_0"` y para las vibraciones.
   - Los valores concretos de cada menú están en los datos del layout, que no he leído [E]. Que ESC, mapa,
     inventario y ajustes pongan 0 lo dice la captura del 05-10 [V]; que lo hagan por este atributo lo dice la
     cadena de código [V].
2. **En un jugador, el reloj del servidor interno es ese mismo reloj de juego [V].**
   - En modo local, `0x8C8682` pone `[0x6C8CD35] = 1`. Con ese byte a 1, `FrameClock_NowMs` (`0x1416B80`) devuelve el
     reloj acumulado por frame **también en los hilos del servidor**, y ese reloj solo avanza con el `dt`
     escalado (`0xAD19D3`).
   - Por eso, con el menú abierto, `ServerTimeInfoAck` sigue saliendo cada 5 s de pared pero con el mismo valor
     (captura del 05-10).
3. **El servidor no deja de recibir ni de ejecutar las Req del usuario 2 por la pausa [V código, E fuerte en
   vivo].**
   - El hilo de red (`RecvLoop` `0x27733F0`) y los trabajadores de diferidas (`0x27731C0`) no miran ni la pausa ni
     el reloj de juego. Solo se paran con la barrera de guardado TLS `+0x4460`.
   - Lo que sí se para es todo lo que cuelga del reloj de juego:
     - la rueda de temporizadores de juego (`ServerNet+0x68`);
     - la lista de "once timers" de juego (`+0x70`, que drena el 1023);
     - entre ellos `UpdateBuffOnceTimer`, `OnceTimer_UpdateServerAIChart`, `StageBranchStateOnceTimer` y
       `SaveGameDataAutoTimer`;
     - y la hora que el servidor sella en lo que envía.
   - Además se congela el cliente del anfitrión, que es quien simula la IA de los NPC.
   - En la sesión 6B, el corte de las 23:52:35 lo produjeron las **excepciones** de nuestro asignador
     (`failed 13`, [p8-sesion6.md](p8-sesion6.md) §5.2), no la pausa.
4. **Sí hay un punto nativo único para "no pausar".**
   - `0x35CD990` (escala de UI) solo tiene dos llamadas, ambas en `0xAD1300` (`0xAD143C` y `0xAD14D6`). Ningún
     otro código lee su resultado ni el valor suavizado `world+0x312C0` (barrido de `.rsrc`, `.xtls`, `.text` y
     `.xtext`).
   - Un gancho de entrada en `0x35CD990` que devuelva `1.0f` mientras haya invitado quita la pausa de **todos**
     los menús, minijuegos incluidos, sin tocar la UI.
   - Ni la cámara lenta de habilidades y secuencias (canales `+0xCC0`/`+0xCD0`) ni la depuración usan esa
     función.
5. **Recomendación:** gancho `ui_pause_block` en los dos lados mientras la sesión esté conectada.
   - Antes, una sonda de solo lectura (T0, un PC, sin Shadow) que registre los cambios del valor original para
     confirmar qué menús ponen 0.
   - Riesgos que hay que probar: el mapa (cámara observadora y streaming), la entrada mientras el menú está
     abierto, los temporizadores y recompensas que pasan a correr, y el audio y la vibración (cosmético).
   - Hay que seguir tolerando las cargas, la barrera de guardado, la cámara lenta y las cinemáticas
     (`GamePause` de secuencia).

---

## 1. Pregunta 1: cómo pausa el juego

### 1.1 El reloj de juego en modo local [V]

| Pieza | Dirección | Qué hace |
|---|---|---|
| `FrameClock_NowMs` | `0x1416B80` | `80 3D AE 61 87 05 00` `cmp byte [0x6C8CD35],0` / `75 17` `jne 0x1416BA0` |
| | | Si el byte es 0 y el hilo es de servidor (TLS `+0x1EC`), salta al reloj real `0x1416AF0`. |
| | | Si el byte es ≠ 0, **todos los hilos** (servidor incluido) devuelven `base + frac/1000`, con `base` en `[0x6C8CD68]` y `frac` en `[0x6C8CD70]`. |
| Activación (modo local) | `0x8C8682` en `0x8C8520` | `44 88 25 AC 46 3C 06` `mov byte [0x6C8CD35], r12b(=1)`. |
| | | Condición: el modo `[[rbx+0x20]+0x48]` es 1, 4 o 5, o bien `[rbx+0xCA0]` (servidor local), y `[0x6CEEE38] == 0`. Luego conecta con el servidor interno. |
| | | En la otra rama de conexión (`0x35C150`, `0x35C184`) el byte se pone a 0. |
| Avance por frame | `0xAD19BE`–`0xAD19E3` | Con el byte a 1: `48 01 1D 96 B3 1B 06` `add [0x6C8CD70], rbx`, donde `rbx = dt_escalado·1e6`. |
| | | Con el byte a 0: `base = reloj real` y `frac = 0`. |

Notas:
- `frac` está en **µs** (la división del lector es /1000), no en ns como dice el nombre `cdmp_g_FrameClockFracNs`.
  El nombre `g_FrameClockDisabled` de `0x6C8CD35` también engaña: con 1, el reloj por frame **manda**, y lo
  usan también los hilos del servidor.
- Corrige un detalle de [../hora-del-dia.md](../hora-del-dia.md) §1.1: la rama "cacheada por frame" no depende de
  `== 0`. Con 0 es el reloj real muestreado una vez por frame; con 1 es el reloj escalado y acumulado.
- Confirmación en vivo [V]: captura del 05-10 ([../pausa-copia-congelada.md](../pausa-copia-congelada.md) §1.1).
  El ms de `ServerTimeInfoAck` (1022, temporizador de rueda **real**, ver §2.2) se queda fijo durante 26–144 s
  de menú. Eso solo pasa si el servidor lee el reloj de juego.

### 1.2 Cómo se calcula el `dt` del frame: `0xAD1300` [V]

La llama el bucle principal: `MainLoop_Tick` `0x617290` → `MainLoop_NetUpdate` `0xAD2380` → `0xAD1300`. El
bucle principal **sigue corriendo** en pausa: dibuja, mueve la UI y procesa los paquetes del cliente (la
captura del 05-10 muestra paquetes recibidos e inyectados durante el menú).

`p` es el objeto que recibe `0xAD1300` y `F = [p+0x60]` es el objeto de tiempo del frame. Pasos:

1. `F+0x64` = dt real (QPC) o forzado (`[0x6CCB048]`). `F+0x6C` guarda el dt sin escalar (lo usa la UI).
2. `dt *= [[p+0x18]+0x60]+0x24C`, la escala del motor. La toca la depuración
   `PearlAbyssEngine.Debug.FreezeTimeScale` (`0x36B90A0`) y algún evento de secuencia (`0xC3FE30` la repone a
   1). Se limita a 2,0.
3. **Primera llamada a la escala de UI**: `0xAD143C` `E8 4F C5 AF 02` → `0x35CD990`.
   - Si devuelve ≠ 1, `xmm7 = escala_UI` en `0xAD146A` (`C5 F8 28 F9`).
   - `xmm7` llega a `[[p+0x18]+0x70]->vf 0x160` en `0xAD193F`: una escala global para otro subsistema
     (animación, física o efectos, sin identificar [E]).
   - Esto ocurre **antes** de la puerta de modo y por tanto se aplica siempre.
4. **Puerta de modo local**: `0xAD148F`–`0xAD14B2`.
   - Condiciones: modo `[[[p+0x28]+0x1100]+0x48]` en {1, 3, 4, 5} **y** `[[p+0x40]+0xCA0] != 0`.
   - `0xAD14AB` `44 38 A0 A0 0C 00 00` `cmp [rax+0xCA0], r12b` / `0xAD14B2` `0F 84 A8 03 00 00` `je 0xAD1860`.
   - Si no se cumple, se salta todo el bloque siguiente: es el comportamiento "en línea", sin pausa de menús.
5. Dentro del bloque:
   1. Segunda llamada `0xAD14D6` `E8 B5 C4 AF 02` → `0x35CD990`.
   2. Suavizado exponencial en `world+0x312C0`. La velocidad viene de la variable global `[0x6D2A028]` = 2,5. Por
      debajo de `[0x6D2A1B8]` = 0,05 se fija a 0.
   3. `0xAD1569`–`0xAD1572` `vmulss xmm0, xmm6, [rax+0x64]` / `vmovss [rax+0x64], xmm0`: **el dt se multiplica
      por la escala de UI suavizada**.
   4. En `0xAD1580`–`0xAD15B5`: si la escala llega a 0 y está activa la variable `[0x6CCB1D8]`, aparece
      `"stopVibrationAll : %#"` con **`"UITimeScale_0"`** (`0x55CCFC0`) y se paran las vibraciones.
   5. Después se aplican los canales de cámara lenta `game+0xCC0`/`+0xCD0` (`0x9514A0`, 0x14 B por ranura, la
      ranura 3 manda). Los escriben la cámara lenta de acciones (`0x4856E0` ranura 0, `0x622FF0` ranura 1), la
      escala de secuencias (`0xC3F010` ranura 4) y el modo especial (`0x9520E0`, ranura 3, `SpecialModeInfo
      _timeScale`/`_playerTimeScale` [E]). El resultado va a `+0xCE0` y vuelve a multiplicar el dt.
6. `F+0x50`: 1 = escala `F+0x54`, 2 = dt 0, 3 = un paso de 1/60 y después 2. No encontré quien lo ponga a 2 o 3
   fuera de `0xAD1300`, así que no es la pausa de menús [E].
7. El dt final (≤ 1,19e-7 → 0) alimenta el reloj (§1.1) y todas las actualizaciones de `0xAD2380`.

### 1.3 De dónde sale el 0: la escala de UI [V]

**`0x35CD990` (escala de UI, tamaño 0x111):**
- Recorre la lista `[mundo+0x38]` de vistas de escena. El mundo es `**([[p+0x18]+0x88]+0x78)`, la primera
  escena raíz.
- Para cada vista toma `V = [[e+0xA0]+0x10]` y solo cuenta las que tienen estado `V+0x26F` en 0 o 1, es decir,
  abiertas (2 = ocultándose, 3 = sin cargar; las pone `0x3D3C8E0`).
- La escala de la vista es `V+0x118->vf 0x168()` si es distinta de 1; si no, `V+0x23C`.
- Devuelve la **primera** escala distinta de 1, o 1 si no hay ninguna.

**`V+0x118` es la raíz de UI:**
- `0x3D3C8E0` le llama `vf 0x50`/`vf 0x68` con la escena.
- Su `vf 0x168` es `0x63DA70`, `return 1.0f`, en **las 365 vtables** `UIGamePlayControl*`. Ninguna raíz lo
  redefine (en una vtable hay `0x359BA0`).
- Así que la escala la decide **`V+0x23C`**.

**Quién escribe `V+0x23C`:**

| Escritor | Qué hace |
|---|---|
| `0x3D36FB0` | Inicialización a 1,0 (`0x3D37263`). |
| **`0xE41DA0` = `UIGamePlayControlCommonRoot::vf1`** | Es la creación de la raíz: vtable `0x568BC08`, ranura 1. Lee el atributo de layout **`timeScale`** del nodo (`[nodo+0xE0]`) o de su plantilla (`[[nodo+0xA0]+0x68]`), con id interno en `0x6CD1954`. Si no existe usa `"1"`. Hace `atof` y escribe en **`0xE41EA7` `C5 FA 11 B0 3C 02 00 00` `vmovss [rax+0x23C], xmm6`**. |
| | **117 llamadas**: 78 en `.rsrc` (las `vf1` de cada raíz) y 39 en copias de `.xtls`. |
| | Comprobado que la llaman: `_Root_MainMenu` (`0xD7FC00` → `0xD7FC21`), `InventoryEquipment` (`0xD52540` → `0xD52555`), **`_Root_ExtraReward`** (`0xE90BE0` → `0xE90C14`), `Settings` (copia `.xtls` `0xB0268A0` → `0xB0268BC`) y `GamePause` (copia `.xtls` `0xA915850` → `0xA91587B`). |
| Minijuegos de cartas y dados (`0x10415E0`, `0x1044150`) | Al abrir guardan la escala en `this+0x2B4` y ponen `V+0x23C = 0`. Al cerrar la reponen. |
| Panel de comandos de depuración (`0x3957250`) | Lee `timeScale` por cadena. |

Conclusión:
- **La pausa la declara cada menú en su layout**, con `timeScale="0"` o un valor menor que 1 [E fuerte: el
  código está verificado; los valores están en los datos].
- Mientras esa vista esté abierta (estado 0/1), el mundo se escala por ella. Al cerrarla (estado 2/3) deja de
  contar y el suavizado vuelve a 1 en ≈1 s.

**`_Root_ExtraReward`** (vtable `0x569C6B8`) también pasa por `0xE41DA0`. Si su layout lleva `timeScale="0"`, el
aviso de recompensas **pausa el reloj de juego del anfitrión**, y con él la rueda de juego del servidor. Encaja
con que ese aviso bloqueara la carga del usuario 2 [E: hay que medirlo con T0].

### 1.4 Descartado como mecanismo de la pausa de menús

| Candidato | Por qué no |
|---|---|
| Barrera TLS `+0x4460` (`SaveGameDataAutoTimer` 2077, `0x2C35EB0`) | Solo la usa el autoguardado ([../opcion-d-e3-riesgos.md](../opcion-d-e3-riesgos.md) §4) [E]. |
| `engine+0xE9` ("pausa de actualización") | Lo cambia `PearlAbyssEngine.ToggleResumePauseUpdate` (`0x36B6680`, que lo refleja en `world+0x30689`) y lo borra el manejador de arranque seguro `0xA0C6C0`. En `0x617290` cambia la ruta de dibujo, no el dt. |
| `Debug.PauseFrameIndex` / `IncreaseFrameIndex` / `FreezeTimeScale` | Depuración (`0x36B8F90`, `0x36B9020`, `0x36B90A0`). |
| `F+0x50` (§1.2 punto 6) | Sin escritores de menú. |
| Ranuras `vf 0x1A8/0x1B0/0x1B8` de las raíces | `0x3E729A0` las usa para suscribir la raíz a los avisos de estado de la escena (`0x3D3A3E0`, listas `V+0x128/+0x138/+0x148`), no para pausar. |
| `UIGamePlayControlRootGamePause` | Su apertura (`0xD44460`) llama `[[[0x6D691E8]+0x68]+0x38]->vf 0x148`, y el cierre `vf 0x150`. Es la pausa de **cinemática** (letterbox, `selector-game-resume-keyguide`). Aparte de eso, también lee `timeScale` (vf1 en `.xtls`). |
| `field+0x17C` (pausa de la hora del campo) | No se encontró un camino desde los menús. La hora del día ya se para sola porque su reloj (`0x1416B80` vía `0x20E55A0`) es el de juego. |

### 1.5 Qué se para con un menú abierto (anfitrión, modo local)

| Sistema | Estado | Base |
|---|---|---|
| Bucle principal, render y UI | Corren (la UI usa `F+0x6C` sin escalar) | [V] |
| Recepción y Acks del cliente (`0xAD2380` → `ProcessRecvQueue`, `NetworkThread` `0x35B800`) | Corren | [V] código y captura del 05-10 |
| Actores del cliente: animación, IA de NPC (modo 3), físicas, secuencias | Parados (dt = 0; `vf 0x160` recibe escala 0) | [V] dt; [E] por subsistema |
| Envío de Req del cliente del anfitrión: Move/Action propios y de NPC simulados | No salen (no hay cambios) | [V] captura del 05-10, "no Reqs sent" |
| Reloj de juego (`0x1416B80`) para el cliente **y** el servidor | Congelado | [V] |
| Hora del día (`FieldTime`; copias `0x6A6D218` y **`0x6A6D238`**) | Congelada: `0x20E55A0` avanza con `ahora − ancla`, y `ahora` está quieto | [V] cadena; [E] en vivo |
| Hilo de red del servidor (`RecvLoop` `0x27733F0`) y diferidas (`0x27731C0`) | Corren | [V] §2.1 |
| Rueda real del servidor (`+0x58`) y lista "once" real (`+0x60`, la drena el 1007) | Corren | [V] §2.2 |
| Rueda de juego (`+0x68`) y lista "once" de juego (`+0x70`, la drena el 1023) | **Paradas** | [V] §2.2 |
| Hilo de red del mod y túnel | Corren | [V] logs de la sesión 6 |

---

## 2. Pregunta 2: ¿sigue el servidor procesando paquetes en pausa?

### 2.1 Entrada de Req: no depende de la pausa [V]

- **`RecvLoop` `0x27733F0` (ServerRunThread):**
  - Hace `Queue::Pop(Q0, 1000 ms)` → `OnRecvPacket` (`0x2773C20`) → devuelve el búfer a Q3.
  - Solo espera si `TLS+0x4460 != 0` (autoguardado).
  - Las Req con `troc+0x1C == 0` (Move 2556, StartAction 2223, Rotate 2828, ChangeAIState 2734) se ejecutan ahí
    mismo.
  - Las que tienen `troc+0x1C == 1` (UseItem 2855, LoadingComplete 2445, StageBranchState 2436,
    UpdateStageChartEvent 2591, UpdateSequencerGamePlayDataEvent 2173, Login 3093) van a la cola diferida por
    usuario `ServerNet+0x38` (`site_CheckDeferredFlag`).
- **`DeferredThreadLoop` `0x27731C0`:** espera la misma barrera y después hace `DeferredQueue::Pop` →
  `ProcessAsyncRequest` (`0x2774730`). No consulta ningún reloj.
- Ni la escala de UI ni `0x6C8CD35` aparecen en estos caminos. **La pausa no impide consumir las Req del
  usuario 2** [V código; E fuerte en vivo, porque el único dato en vivo es la sesión 6B, que se cortó por la
  excepción del asignador].

### 2.2 Dos ruedas de temporizadores: real y de juego [V]

En `ProcessDeferredAndTimers` `0x27725F0`:
- La rueda `ServerNet+0x58` se evalúa con el reloj real: `0x27729F8` `call 0x14352B0` con `now = 0x1416AF0()`.
- La rueda `ServerNet+0x68` se evalúa con el reloj de juego: el código de `0x1416B80` está copiado en línea en
  `0x2772A0E`, con el mismo `cmp [0x6C8CD35]`.
- El `Sleep` se calcula con las dos.
- El temporizador **1007** (`UserOnceTimerProcessRepeatTimer`, `troc+0x88 = 0`, rueda real) drena la lista
  "once" `+0x60`.
- El **1023** (`GamePlayOnceTimerProcessRepeatTimer`, `troc+0x88 = 1`, rueda de juego) drena la lista `+0x70`.
- `EnqueueTimer` (`0x2774430` → `.xtls 0x100B4050`) elige `+0x60` o `+0x70` según `troc+0x88`.

Temporizadores de **juego** (`flag_88 = 1`, `build/research/troc_messages.csv`). **Con el menú abierto no
avanzan:**

| Grupo | Temporizadores |
|---|---|
| IA y combate | `OnceTimer_UpdateServerAIChart` 2032, `OnceTimer_DelayCallAIEvent` 2033, `KillActorByRunOutFatal` 2007, `KillSelf` 2037 |
| Estado del actor | `UpdateBuff` 2022, `UpdatePassiveBuff` 2023, `UpdateDataDefinedRegenerate` 2087, `CheckChangeAnimationSpeed` 2016 |
| Secuencias y escenas | **`StageBranchStateOnceTimer` 2044**, `SequencerTimelineEventSummonCharacter` 2069, `UpdateFactionSpawnStage` 2046, `StartChallengeCompleteTimer` 2051, `SummonGimmickDestoryTimeLimit` 2071 |
| Mundo | `GlobalGameEvent` 2012, `UpdateFactionManager` 2013, `UpdateMercenaryClan` 2014, `UpdateBank` 2018, `AutoCreateItem` 2019, `KillExceededFishInPond` 2021 |
| Objetos y vehículo | `FeedToActor` 2072, `LossVehicleInventoryItemByMoveForce` 2073, `CreateItemAndEquipXXX` 2076 |
| Guardado | **`SaveGameDataAutoTimer` 2077** |

Temporizadores **reales** (siguen): `ServerTimeInfo` 1022, `GamePlaySaveData` 2011, `ActorGarbageCollection`
2010, `MoveFieldOnceTimer` 2027, `LogoutAllActorOnceTimer` 2063, `InteractionReward` 2040, `HandleGameEvent` 2052,
`DeleteSequencerOnceTimer` 2054, entre otros (57 en total).

### 2.3 Qué deja de hacer el anfitrión para el usuario 2 mientras pausa [V/E]

1. **Mundo congelado:** el cliente del anfitrión deja de simular los NPC, que en P8 los simula él; no hay
   2223, 2556 ni 2734 de NPC. El invitado ve los NPC y al anfitrión quietos [V captura del 05-10; E en P8].
2. **Consecuencias diferidas paradas:** las Req del invitado se ejecutan, pero lo que esas Req dejan en la rueda
   de juego no avanza. Por ejemplo, `StageBranchStateReq` → `StageBranchStateOnceTimer` 2044 →
   `StageOnBranchEventAck`; también los buffs, la IA del servidor y la regeneración [E fuerte].
3. **Reloj del invitado:**
   - El 1022 sigue llegando cada 5 s con el tick congelado.
   - Su manejador (`0xBDF310`) pone `desfase = tick − ahora_invitado`, así que el "ahora" del invitado queda
     clavado y retrocede a saltos de 5 s.
   - Los búferes de interpolación remota (`T = ahora − 200`) se paran.
   - Al reanudar, saltos y ajustes de `ts` (el mismo patrón que `pausa-copia-congelada.md` §2) [E].
4. **Sesión 6B:**
   - Lo que se rompió a las 23:52:35 fue `push_request`, por una excepción en `0x47EFA78`: el heap global
     compartido, [p8-sesion6.md](p8-sesion6.md) §5.4–5.5.
   - El hilo del servidor (T4572) estaba en `0x2774A9F` (`call [rax+8]`, un candado dentro de
     `ProcessAsyncRequest`). Encaja con una sección crítica que dejó tomada nuestra excepción, no con la pausa
     [E].
   - La pausa empezó en esa misma ventana. No se puede separar su efecto con esos datos.

---

## 3. Pregunta 3: un camino nativo "sin pausa" y qué afecta

### 3.1 Puntos de decisión (todos en `.rsrc`)

| # | Punto | Bytes (build 2976) | Efecto si se anula mientras hay invitado | Valoración |
|---|---|---|---|---|
| **A** | **Entrada de `0x35CD990`** (escala de UI) | `48 89 5C 24 08` `mov [rsp+8],rbx`; `48 89 6C 24 10`; `48 89 74 24 18`; `48 89 7C 24 20`; `41 56`; `48 83 EC 30`; `48 8B 41 38` | Devolver `1.0f`: ningún menú ni minijuego escala el mundo. Desaparece la escala 0 que recibe `vf 0x160` (paso 3) y el `dt` deja de multiplicarse (paso 5.3). El reloj de juego y la rueda de juego del servidor siguen. **No toca** la cámara lenta de habilidades o secuencias, que va por otros canales. | **Recomendado.** Solo tiene dos llamadas (`0xAD143C`, `0xAD14D6`); no hay más lectores (§0.4). El prólogo es limpio para `safetyhook::create_inline`, como `clock_gate.cpp`. |
| B | Las dos llamadas `0xAD143C` `E8 4F C5 AF 02` y `0xAD14D6` `E8 B5 C4 AF 02` | (las de la columna anterior) | Igual que A, redirigiendo cada `call` a un trozo propio. | Equivalente a A, con dos parches en vez de uno. |
| C | Escritura por raíz `0xE41EA7` `C5 FA 11 B0 3C 02 00 00` (`vmovss [rax+0x23C], xmm6`) en `0xE41DA0` | — | Forzar `xmm6 = 1` al crear cada raíz. | Incompleto: no cubre las raíces ya creadas ni los minijuegos (`0x10415E0` escribe `+0x23C = 0` directamente). Útil solo como sonda, para saber qué layout pide qué escala. |
| D | Puerta de modo `0xAD14A7`–`0xAD14B2` (`48 8B 47 40` / `44 38 A0 A0 0C 00 00` / `0F 84 A8 03 00 00`) | — | Forzar el salto equivale a portarse como "en línea": sin suavizado de UI y sin canales `+0xCE0` (adiós cámara lenta y "modo especial"). | No basta, porque el paso 3 sigue pasando la escala de UI a `vf 0x160`. Además quita efectos de juego. Solo como alternativa si también se quisiera anular la cámara lenta (§4.b). |
| E | `0x6C8CD35` (reloj local) | `0x8C8682` | Poner 0 haría que el servidor use el reloj real. | **No**: el cliente seguiría pausado (dt 0), el reloj del cliente y el del servidor se separarían y se rompe la base de tiempo de todos los `ts`. |

Diseño mínimo de A:

```c
float hk_UiTimeScale(void* world) {
    float r = orig(world);
    if (ui_pause_block_active())      // anfitrión: usuario 2 dentro; invitado: túnel conectado
        { note_transition(r); return 1.0f; }
    return r;
}
```

- Se ejecuta en el hilo principal dos veces por frame. Coste despreciable.
- `world+0x312C0` se recupera solo: si el gancho se activa con un menú ya abierto, el mundo vuelve a 1 en ≈1 s.

### 3.2 Qué cambia al quitar la pausa

| Área | Efecto esperado | Base / riesgo |
|---|---|---|
| UI | Ninguno directo: usa `F+0x6C` y su escena sigue con estado 0. | [E] bajo |
| Entrada | Con el menú abierto, la entrada debería ir a la UI y el personaje no moverse [E: no verificado en el código]. | **Probar**: tecla mantenida al abrir el menú, mando |
| Personaje del anfitrión | Queda quieto pero **vulnerable**: el mundo sigue, los enemigos atacan, la caída o la natación continúan. Es lo normal en un juego en línea. | Aceptado |
| IA de NPC y réplica | El cliente del anfitrión sigue simulando y enviando 2223, 2556 y 2734: el invitado ve el mundo vivo. | Es el objetivo |
| Reloj, temporizadores y recompensas | El reloj de juego avanza en los menús. Buffs, enfriamientos, regeneración, la hora del día y la rueda de juego (2077 autoguardado, 2044 escenas) siguen. Desaparece el "tiempo gratis" de los menús. | Aceptado. El autoguardado ya pasa por nuestra guarda (`save guard`). |
| Guardado manual desde el menú | Guardar con el mundo en marcha. El juego puede negarse por reglas propias (combate). | [E] probar |
| Mapa del mundo (`UIGamePlayControlRootWorldMap`, `UIPhaseData_WorldMapObserver`, `Menu_WorldObserver`) | El mapa mueve una cámara u observador. Si el streaming sigue a ese observador con el mundo en marcha, puede descargar el entorno del personaje mientras corre la IA. | **Riesgo principal**; probar en T1 |
| Inventario con vista del personaje | Las animaciones de la vista siguen; el personaje real sigue en el mundo. | Bajo |
| Audio | El RTPC de Wwise `TimeScaleUI` (`0x5A9E0D8`) probablemente apaga o filtra el sonido en los menús. Ya no lo hará. | Cosmético [E] |
| Vibración | No se dispara `stopVibrationAll`. | Cosmético |
| Minijuegos (cartas, dados) | No pausan el mundo. | Aceptable; se puede dejar fuera del gancho si molesta |
| Modo foto (`UIPhaseData_PhotoMode`) | Si su layout pide `timeScale=0`, ya no congela. | Aceptable en cooperativo, o excepción por raíz [E] |
| Cinemática pausada (`GamePause`, `vf 0x148/0x150`) | La secuencia se sigue pausando por su propio camino. Solo deja de congelarse el resto del mundo. | [E] probar |

---

## 4. Pregunta 4: recomendación

### 4.a Diseño "sin pausa con invitado"

1. **Opción de ini `ui_pause_block`**: `0` apagado, `1` solo con sesión, `probe` solo registrar y `force` siempre
   (solo para T1). Por defecto, `probe`.
2. **Gancho A** en `0x35CD990`, con firma de 30 bytes comprobada antes de instalar (los de §3.1). Cuándo se
   activa:
   - **anfitrión**: con el usuario 2 conectado (`session 2 ready … logged in`) hasta su logout completo;
   - **invitado**: con el túnel conectado (§4.c).
3. **Registro**:
   - Una línea por transición del valor original (`UI timeScale 1 → 0 (bloqueado)` y vuelta).
   - El puntero de la vista y, si se puede leer sin riesgo, el nombre de la raíz (`[V+0x118]`, la vtable con
     RTTI).
   - Cada 5 s, el tick de `0x1416B80` y `[0x6C8CD35]` para confirmar que el reloj avanza.
4. **Sin más cambios**: no tocar la puerta D ni la cámara lenta.

**Pruebas:**

| Id | Dónde | Qué | Criterio |
|---|---|---|---|
| T0 (sonda) | Un PC, sin invitado, `ui_pause_block=probe` | Abrir ESC, mapa, inventario, ajustes, árbol de habilidades, misiones y conocimiento; un aviso de recompensa (ExtraReward); un minijuego y el modo foto si están a mano. | Cada menú que pausa registra `0` (o < 1). El tick de `0x1416B80` queda fijo. Confirma §1.3 y la lista de menús. |
| T1 | Un PC, `ui_pause_block=force` (sin sesión) | Los mismos menús durante 30–60 s, un enemigo cerca y el mapa abierto un rato largo. | El reloj avanza; el enemigo actúa; al cerrar el mapa el entorno sigue cargado y el personaje en su sitio; la entrada no se escapa al personaje. |
| T2 | Un PC con `cdmp_peer` en el mundo del anfitrión | ESC del anfitrión 60 s. | `seen by the server` sigue subiendo. El ms de los `ServerTimeInfoAck` que recibe el peer avanza. El peer recibe 2223, 2556 y 2734 de NPC durante el menú. 0 `ERR`. |
| T3 | Shadow (P8) | ESC y mapa del anfitrión 60 s; después ESC del invitado 60 s, con y sin gancho en el invitado. | El invitado ve NPC y anfitrión animados. Sin saltos ni congelación al cerrar. Sin cierres. **Necesita Shadow.** |

Condición previa para T2 y T3: el arreglo del asignador de la sesión 6
([p8-sesion6.md](p8-sesion6.md) §5.6 y §7.2.1–4). Sin él, cualquier pausa vuelve a confundirse con la excepción.

### 4.b Lo que hay que seguir tolerando

| Estado | Por qué |
|---|---|
| **Cargas del anfitrión** (viaje, muerte, cambio de campo) | El cliente no simula durante la carga, así que los NPC se quedan quietos para el invitado. Además, lo que corra en el hilo principal se retrasa. Avisar al invitado o retener sus Req de forma acotada, sin cortar la sesión. |
| **Barrera de autoguardado TLS `+0x4460`** (2077 y `0x2C35EB0`) | Para todos los hilos del servidor mientras guarda. Con el gancho, el 2077 vuelve a avanzar con el reloj. Que nuestro código nunca bloquee dentro de `OnRecvPacket`. |
| **Cámara lenta** (canales `+0xCC0`/`+0xCD0`: acciones, secuencias, "modo especial" ranura 3) | En modo local también ralentiza el reloj del servidor para todos. Es un desfase menor y temporal. Si molesta, la puerta D la quitaría, pero también los efectos. |
| **Cinemáticas** (sequencer y `GamePause` de cinemática) | Su pausa propia sigue. Las secuencias personales del invitado ya tienen su problema ([p8-sesion6.md](p8-sesion6.md) §4). |
| **Pausas de la ventana** (minimizar o perder el foco) | No las he buscado. Si el juego pausa sin foco, no pasará por la escala de UI [E: comprobar en T1]. |
| **Tirones y frames largos** | El dt se limita a 2 s por frame (`0xAD1408`). Un tirón mayor pierde tiempo de juego y el reloj del servidor se queda atrás (−0,92 s visto el 05-10). |
| **Guardado y carga desde el menú** | Cargar una partida con invitado debe seguir bloqueado o llevar al logout ordenado (C4). |

### 4.c Cuando el invitado abre su propio ESC

**Sin gancho en el invitado:**
- El cliente del invitado también está en modo local (`0x6C8CD35 = 1` en su proceso) y su UI pone la escala a
  0: su mundo y su reloj se congelan **localmente** [V mecanismo; E en P8].
- Su cliente deja de enviar Move y Action. El servidor del anfitrión no ve nada especial: el personaje del
  usuario 2 se queda con su último estado.
  - Si iba corriendo, el anfitrión puede verlo **correr en el sitio** hasta que vuelva. Es el mecanismo de
    [../pausa-copia-congelada.md](../pausa-copia-congelada.md) §3: falta un Move de parada.
  - No hay teletransporte: al volver sigue desde la misma posición.
- Mientras tanto el invitado sigue **recibiendo**: los Acks del anfitrión se procesan en su hilo principal.
  - Los 1022 recolocan su desfase cada 5 s. Su "ahora" avanza a saltos sobre el reloj parado.
  - Los actores remotos se quedan quietos o saltan; al cerrar el menú, sincroniza en ≤ 5 s.
- El invitado, al no ser dueño del campo, no para ningún temporizador del servidor. Sus temporizadores de juego
  locales son irrelevantes en P8.
- **Aceptable**, salvo el "correr en el sitio".

**Con el mismo gancho A en el invitado (recomendado):**
- Su mundo sigue y su personaje queda quieto con la entrada en la UI.
- Su reloj no se queda atrás y desaparecen los saltos.
- Comprobar en T3 que su cliente no envía nada raro mientras el menú está abierto (por ejemplo, Req de mundo de
  NPC, ya vistas en [p8-sesion6.md](p8-sesion6.md) §3.6).
- El problema de "ver un objeto" del ESC del invitado ([p8-sesion6.md](p8-sesion6.md) §4) es independiente: es
  el enrutado del 2810/2111.

---

## 5. Pendiente y dudas

1. **Los valores de `timeScale` de cada layout** (ESC/MainMenu, mapa, inventario, ajustes, ExtraReward, modo
   foto) están en los datos: T0 los dará sin abrir los PAZ. Si algún menú usa un valor entre 0 y 1 (cámara lenta
   de UI, por ejemplo una rueda de objetos), el gancho también lo anula.
2. **`[[p+0x18]+0x70]->vf 0x160`**, el consumidor de la escala sin suavizar, no está identificado. El gancho A lo
   cubre igualmente.
3. **El mapa**: comprobar en T1 si su observador mueve el centro de streaming.
4. **La pausa sin foco de ventana**: sin buscar.
5. **En vivo**: confirmar que `RecvLoop` y las diferidas siguen consumiendo con el menú abierto. T2 lo cubre con
   los contadores `seen by the server` y `failed`, una vez arreglado el asignador.

---

## 6. Implementado

10 de octubre de 2026, rama de trabajo sobre `opcion-d` `42d42b7`. Implementa la recomendación §4.a (gancho A).
No se lanzó el juego ni se tocó `bin64`. Nada en `.xtls` ni en sus thunks.

### 6.1 Qué hace y dónde está

| Pieza | Archivo |
|---|---|
| Lógica pura (modos, decisión, sesión viva, réplica del recorrido de vistas, evento del anillo, contadores por UI, modelo del `dt`) | `src/mod/session2/ui_pause_logic.h` |
| Gancho, hilo propio (instalación + vaciado), registro | `src/mod/session2/ui_pause.cpp` / `.h` |
| Opción del ini | `src/mod/config.h/.cpp` (`[session2] ui_pause_block`, cadena; por defecto `0`) |
| Arranque en los dos papeles | `src/mod/mod.cpp`: anfitrión (`session2`) y rama del invitado (`guest=1`) |
| Sesión viva del invitado | `guest.cpp` `guest_remote_live()` (solo atómicos) |
| Tests | `tests/optd_ui_pause_tests.cpp` (`optd_ui_pause`) |
| Herramienta T2 | `tools/scripts/analyze_capture.py <captura> ticks` (tick de cada `ServerTimeInfoAck` 2188) |
| Ini de prueba | `docs/research/ini/cdmp.ui-pause-probe.ini` (T0), `cdmp.ui-pause-force.ini` (T1); línea comentada en `cdmp.p8-2322-local.ini` (T2), `cdmp.p8-2322-host-B.ini` y `cdmp.p8-2322-shadow.ini` (T3) |

**`[session2] ui_pause_block`** (necesita `enabled=1`; las claves nuevas vienen apagadas):

| Valor | Efecto |
|---|---|
| `0` / `off` / vacío (**por defecto**) | No se instala nada. |
| `probe` | Gancho instalado, **sin cambiar nada**: devuelve siempre el valor original. Registra cada cambio del valor original con la vista y la raíz de UI (clase RTTI) que lo pide y su `timeScale`, y cuánto avanza el reloj de juego mientras dura. |
| `1` / `on` / `session` | Devuelve `1.0` en vez de una pausa o ralentización de UI **solo mientras haya sesión P8 viva**: en el anfitrión, con el usuario 2 conectado (`server_user2_present()`: la sesión 2 tiene `ServerUserActor`, hasta su logout completo); en el invitado, con su conexión jugando en el anfitrión (`guest_remote_live()`: conexión decidida remota, túnel arriba y sin cortar). Un invitado en `guest_mode=observe` nunca está vivo. |
| `force` | Siempre, haya o no sesión (solo para pruebas de un PC). |
| otro | Se avisa (`WRN UiPause: ui_pause_block='…' is not 0, probe, 1 or force; it stays off`) y queda apagado. |

La recomendación §4.a decía "por defecto, probe"; por la regla del proyecto (claves nuevas apagadas) el valor por
defecto es `0`.

### 6.2 Tabla del gancho (build 1.0.0.2976)

| Dirección | Sección | Tipo | Bytes comprobados antes de instalar | Qué hace |
|---|---|---|---|---|
| **`0x35CD990`** (escala de UI) | `.rsrc` | **inline** (`SafetyHookInline`), único gancho | 30 bytes de prólogo, 7 instrucciones enteras: `48 89 5C 24 08` / `48 89 6C 24 10` / `48 89 74 24 18` / `48 89 7C 24 20` / `41 56` / `48 83 EC 30` / `48 8B 41 38` | Llama al original y decide (§6.3). |
| `0xAD143C` | `.rsrc` | solo comprobado | `E8 4F C5 AF 02` (`call 0x35CD990`; retorno `0xAD1441`) | Identifica la 1.ª llamada por su dirección de retorno (cuenta frames). |
| `0xAD14D6` | `.rsrc` | solo comprobado | `E8 B5 C4 AF 02` (`call 0x35CD990`; retorno `0xAD14DB`) | 2.ª llamada: la que multiplica el `dt` en modo local. |
| `0x63DA70` | `.rsrc` | solo comprobado | `C5 FA 10 05 3C 56 73 05 C3` (`vmovss xmm0,[1.0f]` ; `ret`) | La `vf 0x168` de las raíces; el recorrido la reconoce sin llamarla. |
| `0x6C8CD35` / `0x6C8CD68` / `0x6C8CD70` | datos | solo lectura (hilo propio, cada 5 s) | — | Byte del reloj local, base (ms) y acumulado (µs) de `FrameClock_NowMs` `0x1416B80` (comprobado con capstone: `cmp byte [rip+0x58761AE]`, `mov r8,[rip+0x58761C1]`, `mov rcx,[rip+0x58761BD]`). |

Comprobaciones estáticas sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe` (capstone y numpy, Python `-I`) [V]:

- **ABI**: un solo argumento, `rcx` (= `**([[p+0x18]+0x88]+0x78)`, la primera escena raíz; las dos llamadas lo
  cargan justo antes: `0xAD1439 mov rcx,[rcx]` y `0xAD14D3 mov rcx,rbx`). Nada en pila: el cuerpo solo lee `rcx`
  y sus propios registros guardados. Devuelve un `float` en **`xmm0`** (`0x35CDA34 vmovaps xmm0,xmm6` y las
  salidas `0x35CDA97`/`0x35CDA89`). Guarda `rbx/rbp/rsi/rdi/r14` y `xmm6` (no volátiles). Las llamadas solo usan
  `xmm0` (`0xAD1441 vmovaps xmm1,xmm0`; `0xAD14DB vmovaps xmm8,xmm0`) y confían en `xmm6–xmm13`, `r12`, `r15`,
  todos no volátiles: el desvío es una función C++ normal (`float __fastcall(void*)`), que los respeta.
- **Tamaño y desenrollado**: `.pdata` `0x35CD990–0x35CDAA1` (0x111 bytes), prólogo de 0x24 bytes en su
  `UNWIND_INFO`. Ninguna de las 7 instrucciones parcheables es relativa a RIP (la primera que lo es está en
  `0x35CD9B4`, fuera de los 30 bytes; su constante `[0x5D730B4]` vale `1.0f`).
- **Saltos**: barrido de `E8`/`E9` rel32 y `0F 80–8F` rel32 en `.rsrc`, `.xtls`, `.text` y `.xtext`, y de
  `EB`/`7x` rel8 alrededor de la función: **ninguno cae en `0x35CD991–0x35CD9AD`**. Los únicos que apuntan a
  `0x35CD990` son `0xAD143C` y `0xAD14D6` (los del §1.3). Ningún puntero absoluto a `0x14_35CD990` en el
  archivo; su RVA aparece una sola vez, en su entrada de `.pdata`.
- **Al instalar** (`ui_pause.cpp install()`): build (base `0x140000000`, sello `0x6AB28F00`), los cuatro bloques de
  bytes de la tabla, `[rva, rva+len)` dentro de `.rsrc` según la tabla de secciones en memoria
  (`gad::in_real_code`), gancho creado deshabilitado y **su parche debe acabar en un límite de instrucción** de
  los 30 bytes comprobados (5 con trampolín cercano; 15 con el salto absoluto de 14 bytes) y coincidir con
  ellos. Si algo falla: `ERR UiPause: … REFUSED`, nada escrito, la opción queda apagada. Se espera al código del
  juego como los otros ganchos P8 (hasta 2 minutos).

### 6.3 Qué hace el desvío (hilo principal del juego)

1. `orig = original(world)`: **la única llamada al juego**.
2. `decide(modo, sesión viva, orig)` (lógica pura, `ui_pause_logic.h`):

| `orig` | Clase | `probe` | `1` sin sesión | `1` con sesión | `force` |
|---|---|---|---|---|---|
| `1.0` exacto | sin petición | pasa | pasa | pasa | pasa |
| `[0, 0,05)` | **pausa** (el juego la lleva a 0 en `0xAD1541`) | pasa (anota "would") | pasa | **1.0** | **1.0** |
| `[0,05, 1)` | **ralentización de UI** | pasa (anota "would") | pasa | **1.0** | **1.0** |
| `> 1` finito | más rápido (no es pausa) | pasa | pasa | pasa | pasa |
| NaN, ±∞, negativo | no entendido | pasa | pasa | pasa | pasa |

3. Contadores atómicos por llamada (1.ª, 2.ª u otra), y del "episodio" actual (frames con escala de UI y
   cuántos se bloquearon).
4. Si el valor original **cambia**, o cada 30 frames mientras no sea 1, **replica en solo lectura** el
   recorrido de `0x35CD990` (§1.3: lista `[world+0x38]`, `V = [[e+0xA0]+0x10]`, estado `V+0x26F` 0/1, raíz
   `V+0x118`, `vtable+0x168`, `V+0x23C`) con lecturas protegidas por SEH (`fast_read`, sin `VirtualQuery`), sin
   reservar memoria ni tomar candados. Guarda hasta 3 vistas que piden algo (índice, estado, vista, vtable de la
   raíz, `timeScale`) y cuál explica el valor. Si la `vf 0x168` de la raíz no es `0x63DA70` el valor de esa vista
   no se puede saber sin llamarla: se marca "ambigua" y no se llama.
5. Mete **un evento fijo** (`p8p::Event`) en un anillo sin candados de 1024 (si está lleno, se cuenta y se pierde;
   nunca espera). Cambio de valor = `kEvTransition`; mismo valor con otra vista detrás (un menú abierto desde
   otro) = `kEvSource`.
6. Devuelve `1.0f` o `orig`.

**La cámara lenta de juego no se toca, y se distingue así:**
- Por **estructura** [V]: `0x35CD990` solo lee vistas de UI (su raíz `V+0x118` y `V+0x23C`, el `timeScale` del layout
  o el 0 de los minijuegos). La escala del motor (`[[p+0x18]+0x60]+0x24C`, `0xAD141A`), los canales de cámara
  lenta `game+0xCC0/+0xCD0 → +0xCE0` (`0x9514A0`: acciones, secuencias, modo especial) y la pausa de cinemática
  (`GamePause`, `vf 0x148/0x150`) entran en `0xAD1300` por otros caminos, antes y después. El gancho no los ve.
- Por **valor**: solo se sustituyen `[0, 1)`. `1`, `> 1` y valores raros pasan tal cual.
- Por **atribución** (diagnóstico): cada evento dice qué vista y raíz de UI pidió el valor; si un valor < 1 no lo
  explicara ninguna vista abierta, el registro dice `source: NO open view explains it` (no se ha visto; sería
  motivo para revisar).
- Una ralentización **de UI** (un menú con `timeScale` entre 0 y 1, por ejemplo una rueda de objetos) **sí** se
  anula, porque es petición de un menú y, en modo local, ralentizaría el reloj del servidor para todos. T0 dirá
  si existe alguna.
- El test `test_slowmo_passthrough` modela `0xAD1300` (suavizado a 2,5/s, umbral 0,05, canales después): menú a 0
  más cámara lenta 0,25 da exactamente `dt·0,25` con el gancho y 0 sin él.

### 6.4 Hilos y memoria

| Hilo | Qué hace |
|---|---|
| Principal del juego (`0xAD1300`) | El desvío de §6.3. Sin heap (ni del juego ni del mod), sin cadenas, sin candados, sin E/S. Solo llama al original. `QueryPerformanceCounter`/`GetCurrentThreadId` para sellar el evento. |
| Hilo propio de `ui_pause` (se crea con `CreateThread` en `ui_pause_start`; primero instala y luego se queda como vaciado) | Cada 100 ms: sesión viva (`server_user2_present()` o `guest_remote_live()`, lecturas protegidas y atómicos). Cada 1 s: vacía el anillo y formatea (nombres RTTI de las vtables con `rtti_name_of_vtable`, en caché). Cada 5 s: lee los tres globales del reloj. Cada 60 s: resumen y tabla por UI. Usa solo el heap del CRT del mod (cadenas, mapa por UI); **nunca el heap del juego** ni funciones del juego (p8-heap-inyeccion.md). |

Es el mismo patrón que `p8_probes` (anillo + hilo periódico propio) y funciona igual en el anfitrión y en el
invitado, con o sin `[net]`. El hilo de red no se toca.

### 6.5 Líneas de log esperadas

```
INF UiPause ON (probe, host): inline hook at exe+0x35CD990 (UI time scale; callers exe+0xAD143C / exe+0xAD14D6), patch 5 of the 30 verified bytes, inside .rsrc | read only: transitions of the original value and the view behind them
INF UiPause [probe] qpc … s (~hh:mm:ss.mmm) capture t=…: UI timeScale 1 -> 0 (pause) | would be blocked by ui_pause_block=1/force (probe: nothing changed) | session not live | seen at 0xAD143C | views: N in the list, M open, 1 asking for a scale | source: <clase RTTI de la raíz> (vtable exe+0x…) view 0x… index i state 1 timeScale 0 | previous episode: 0 frames (0 blocked)
INF UiPause clock: game clock (0x1416B80) +0 ms in 5000 ms of wall time | local clock byte 1 | UI value now 0 (NOT blocked)
INF UiPause [probe] …: UI timeScale 0 -> 1 (none) | back to 1 (no UI view asks for a scale) | … | previous episode: 600 frames (0 blocked)
INF UiPause [probe] …: UI timeScale 0 (pause), now explained by another view | … | source: <otra clase> …
INF UiPause summary: ui_pause_block probe (host, hook on) | session not live | frames F (UI scale in force S, blocked 0) | dt calls D (blocked 0) | other callers 0 | transitions T, source changes C, walks W (incomplete 0), events lost 0 | UI value now 1 | current episode 0 frames (0 blocked)
INF UiPause per UI: | <clase>: 2 episodes, 900 frames with its scale (0 blocked), lowest 0 | …
```

Con `force` o `1` con sesión: `UiPause [force (always)] …: UI timeScale 1 -> 0 (pause) | BLOCKED: the game gets 1.0
(the world keeps running) | …`, `UiPause clock: … +5000 ms in 5000 ms … (blocked, the game gets 1.0)`, y en el
resumen `blocked` ≈ `UI scale in force`. Con `1`: `UiPause: P8 session LIVE (user 2 is logged in) -> menus no
longer pause the world` al entrar el usuario 2 y `UiPause: P8 session over (no other player) -> menus pause as in
single player` al salir (en el invitado: `this guest plays on the host`). Un menú abierto sin sesión en modo `1`:
`passed: no live P8 session (single-player pause as usual)`.

Señales de alarma: `REFUSED` (no se instaló; el juego sigue como siempre), `other callers` > 0 (alguien más llama a
la función), `walks … (incomplete N)` creciendo, `events lost` > 0 (solo se pierde registro), `source: NO open view
explains it` o `source?` (raíz con `vf 0x168` propia).

### 6.6 Plan de pruebas

Condición previa para T2/T3: el arreglo del asignador de la sesión 6 (p8-heap-inyeccion.md), ya en `opcion-d`.
Partida de laboratorio (ranura 108); copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` antes de T1 (escribe en la
memoria del juego).

**T0 — sonda (un PC, un jugador, sin Shadow).** Ini `cdmp.ui-pause-probe.ini`.
1. Cargar la partida, esperar `UiPause ON (probe, host)`. Quieto unos segundos en el mundo.
2. Abrir y dejar abierto **unos 10 s** cada uno, volviendo al mundo entre uno y otro: ESC (menú principal); y
   desde él, o con su tecla, **mapa**, **inventario**, **ajustes**, árbol de habilidades, misiones, conocimiento.
   Además, abrir el mapa **desde** el ESC y volver (para ver `now explained by another view`).
3. Provocar un **aviso de recompensa (`_Root_ExtraReward`)**: recoger o completar algo que lo muestre. Es la
   pregunta abierta de §1.3: si su layout pone `timeScale=0`, ese aviso pausaba el reloj de juego del anfitrión
   y su rueda de juego, lo que encaja con que bloqueara la carga del usuario 2.
4. Si están a mano: un minijuego (cartas o dados), el modo foto y una rueda de objetos o habilidades (¿hay
   ralentización de UI entre 0 y 1?).
5. Control de foco: Alt+Tab 10 s con el mundo a la vista (no debería salir ninguna línea `UiPause [probe]`; si el
   juego pausa sin foco, no es por esta vía: mirarlo en la captura con `ticks`).
6. Criterios: por cada menú que pausa, una línea `UI timeScale 1 -> 0 (pause)` con su clase y `UiPause clock: …
   +0 ms` mientras está abierto; al cerrar, `-> 1`. En la captura del sniffer (`bin64\cdmp_packets_*.bin`):
   `analyze_capture.py <captura> ticks` debe dar `FROZEN` en los intervalos de menú. Resultado: la **lista de
   menús que pausan** (y con qué valor) y la de los que no.

**T1 — force (un PC, sin sesión).** Ini `cdmp.ui-pause-force.ini`.
1. Ponerse junto a un **enemigo** que ataque (o provocarlo) y abrir el **mapa 60 s**: el enemigo debe seguir
   actuando (el personaje queda vulnerable, es lo esperado); `UiPause clock … +≈5000 ms`.
2. Cerrar el mapa: entorno cargado, personaje en su sitio, sin texturas o colisiones perdidas (**riesgo
   principal**: el observador del mapa y el streaming). Repetir con el mapa movido lejos y con zoom.
3. **Inventario** 30 s y **ajustes** 30 s, ESC 30 s: el mundo sigue (agua, NPC, hora del día).
4. **Entrada**: mantener W (o el stick) al abrir el menú y mover/atacar/saltar con el menú abierto, también con
   mando: el personaje **no** debe moverse, atacar ni girar la cámara del mundo. Lo mismo al cerrar.
5. Guardar desde el menú (partida de laboratorio): anotar si el juego lo permite con el mundo en marcha.
6. Sin cierre. `UiPause summary` con `blocked` > 0 y `other callers 0`. `ticks` sin `FROZEN` durante los menús.
   Notas cosméticas: audio del menú (`TimeScaleUI` de Wwise), vibración.

**T2 — anfitrión + `cdmp_peer` (un PC).** Ini `cdmp.p8-2322-local.ini` con `;ui_pause_block=1` descomentado.
1. Juego en la ranura 108, quieto. Esperar `Session2 C4: reconnect on` y `UiPause ON (1 (only while a P8 session
   is live), host)`.
2. Guion **`login_e3lc00soak_child_112627.troc`** (300 s dentro y despedida; **no** los `act*`, obsoletos,
   p8-crash-esc-1831598.md):
   ```
   .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00soak_child_112627.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\uipause-t2.bin
   ```
3. Al ver `UiPause: P8 session LIVE (user 2 is logged in)`: ESC del anfitrión **60 s**, volver, mapa **60 s**.
4. Criterios:
   - `UiPause … BLOCKED` y `UiPause clock: … +≈5000 ms` durante los menús.
   - `analyze_capture.py D:\cdmp-grabaciones\uipause-t2.bin ticks`: los `ServerTimeInfoAck` que recibe el
     usuario 2 avanzan ≈ 5000 ms por 5 s también durante los menús (**ningún `FROZEN` ni `SLOW`**): el reloj de
     juego, y con él la rueda de juego del servidor (`ServerNet+0x68`: buffs, IA del servidor, `StageBranchState`
     2044, autoguardado 2077), sigue.
   - `seen by the server` sigue subiendo en la línea `Session2:`; 0 `ERR`; el peer recibe 2223/2556/2734 de NPC
     durante el menú (`analyze_capture.py … phases`).
   - Tras la despedida: `P8 session over`; un ESC entonces da `passed: no live P8 session` y vuelve a pausar.
   - Control opcional: repetir con `ui_pause_block=probe`: los mismos menús dan `FROZEN` en `ticks`.

**T3 — Shadow (P8 real). Necesita Shadow.** Anfitrión `cdmp.p8-2322-host-B.ini` con `ui_pause_block=1`;
Shadow `cdmp.p8-2322-shadow.ini`, **primero sin** la línea y luego con ella.
1. Invitado dentro del mundo. ESC y mapa del anfitrión 60 s cada uno: el invitado ve NPC y anfitrión animados,
   sin congelación ni saltos al cerrar; sin cierres en ninguno de los dos.
2. ESC del invitado 60 s, sin gancho en Shadow: su mundo se congela localmente (esperado, §4.c) y el anfitrión
   puede verlo "correr en el sitio". Con gancho: su mundo sigue y su personaje queda quieto.
3. Comprobar en el log del invitado que con el menú abierto no envía nada raro (Req de NPC del mundo) y que su
   `UiPause` dice `this guest plays on the host`.
4. Logs de los dos lados y capturas del sniffer.

### 6.7 Riesgos

| Riesgo | Mitigación / cómo se ve |
|---|---|
| **Mapa**: su cámara observadora podría mover el centro de streaming con el mundo en marcha | T1 paso 2; si pasa, excluir la raíz del mapa (la atribución por clase ya existe) |
| **Entrada** que llega al personaje con el menú abierto | T1 paso 4 |
| El personaje del que abre el menú **queda vulnerable**; buffs, enfriamientos, regeneración, hora del día y recompensas corren; no hay "tiempo gratis" | Es lo que pide el producto (como un juego en línea) |
| Guardar con el mundo en marcha | T1 paso 5; con sesión, el guardián de guardado de opción D sigue activo |
| **ExtraReward** y minijuegos o modo foto dejan de pausar | T0 los identifica; aceptable en cooperativo |
| Una raíz con `vf 0x168` propia (no `0x63DA70`) | Su valor no se lee (sin llamadas al juego); el evento dice `source?`. El bloqueo no depende de la atribución |
| Latencia de "sesión viva": se consulta cada 100 ms | Si el usuario 2 sale con un menú abierto, el mundo vuelve a pausar (suavizado ≈1 s); es el comportamiento de un jugador |
| Coste en el hilo principal | Dos llamadas por frame con atómicos; el recorrido (solo lectura) solo en cambios y cada 30 frames con un menú abierto |
| Registro | 2000 líneas de evento como máximo (después solo se cuentan); el anillo pierde eventos si se llena (`events lost`), nunca bloquea |
| Pausa por **foco de ventana**, tirones (> 2 s por frame), cinemáticas, cargas, barrera de autoguardado `+0x4460` | Fuera de esta vía (§4.b); T0 paso 5 mira el foco |
| Audio (`TimeScaleUI`) y vibración (`stopVibrationAll` ya no salta) | Cosmético |
| **Dato a aclarar en T0/T2**: la grabación del peer `D:\cdmp-grabaciones\c4r_esc_soak.bin` (10-10 01:13–01:20, log `backups\logs\cdmp_20261010-0113_esc-ok-host.log`) tiene 62 `ServerTimeInfoAck` **sin ningún intervalo congelado** (`ticks`). Si en esa prueba el anfitrión tuvo el ESC abierto más de 5 s, contradiría §1.1 en vivo; si fue un ESC corto, no dice nada | `UiPause clock` (T0) y `ticks` (T0/T2) lo miden directamente |

### 6.8 Build y tests

`.\scripts\build.ps1` en el worktree: compila sin avisos y **23/23 tests** (`optd_ui_pause` nuevo: modos y
comentarios en línea del ini, tabla de decisión entera, sesión viva en los dos papeles, paso de la cámara lenta en
el modelo de `0xAD1300` y recuperación ≈1 s, réplica del recorrido de vistas sobre memoria falsa —vistas
ocultas, sin raíz, raíz con `vf` propia, lista ilegible o con basura—, empaquetado del evento y contadores por UI).
Los tests no prueban el gancho, el ABI ni el juego: eso es el análisis estático de §6.2 y T0–T3.

## Anexo: direcciones

| RVA | Qué |
|---|---|
| `0x1416B80` / `0x1416AF0` | `FrameClock_NowMs` / reloj real |
| `0x6C8CD35` | 1 = reloj de juego escalado para todos los hilos (modo local) |
| `0x6C8CD68` / `0x6C8CD70` | Base (ms) / acumulado (µs) |
| `0x8C8520` (`0x8C8682`) | Conexión local: pone `0x6C8CD35 = 1` |
| `0x617290` → `0xAD2380` → `0xAD1300` | Bucle principal → actualización → cálculo del `dt` |
| `0xAD143C`, `0xAD14D6` | Llamadas a la escala de UI |
| `0xAD14A7`–`0xAD14B2` | Puerta de modo local (en línea = sin pausa) |
| `0xAD1569`–`0xAD1572` | `dt *= escala UI suavizada` |
| `0xAD15B5` | `"UITimeScale_0"` (`stopVibrationAll`) |
| `0xAD19D3` | `frac += dt·1e6` |
| `0x35CD990` | **Escala de UI** (primera vista abierta ≠ 1) |
| `world+0x312C0` | Escala suavizada |
| `V+0x23C`, `V+0x26F`, `V+0x118` | Escala, estado (0/1 abierta, 2 ocultándose, 3 sin cargar) y raíz de la vista |
| `0xE41DA0` (`0xE41EA7`) | `CommonRoot::vf1`: atributo `timeScale` → `V+0x23C` |
| `0x63DA70` | `vf 0x168` de todas las raíces: `1.0f` |
| `0x10415E0` / `0x1044150` | Minijuegos: `V+0x23C = 0` al abrir |
| `0x9514A0`, `game+0xCC0/+0xCD0/+0xCE0` | Canales de cámara lenta |
| `0x27733F0` / `0x27731C0` | `RecvLoop` / `DeferredThreadLoop` del servidor (sin dependencia de la pausa) |
| `0x27725F0` | Ruedas `+0x58` (real) y `+0x68` (juego); 1007 → lista `+0x60`, 1023 → lista `+0x70` |
| `0x2774430` | `EnqueueTimer`: elige la lista por `troc+0x88` |

## Correcciones tras la revisión de la noche (2026-10-10)

Detalle completo en `p8-revision-noche.md` §6.

- El gancho de `0x35CD990` se instala **cuando el jugador local lleva 3 s en el mundo**: el actor controlado
  existe, con las vtables comprobadas. Nunca durante el arranque ni en la pantalla de título, tampoco con `force`.
- Mientras espera, una línea por minuto (`UiPause start-up: …`) con el avance del reloj del juego y los
  contadores del manejador de trampas de safetyhook. Si un hilo falla en una página parcheada, la línea sale al
  momento como `WRN`.
- Con el gancho puesto, `WRN UiPause: NO FRAMES for N s …` si el bucle principal deja de llamarlo 10 s.
- safetyhook se compila con su manejador de trampas corregido (`third_party/safetyhook-patch`). Ya no reintenta
  para siempre un fallo de lectura o escritura en una página que el mod parcheó, que era la hipótesis del
  bloqueo del primer arranque. Test `optd_hook_traps`: con el archivo original el test se queda girando (el perro
  guardián lo corta a los 20 s); con el corregido pasa.
