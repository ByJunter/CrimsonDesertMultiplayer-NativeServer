# P8 S18: cómo desmonta un jugador de forma nativa (controlador y observador)

11 de octubre de 2026. Solo lectura estática (capstone sobre el PE instalado, build **1.0.0.2976**, base
`0x140000000`, direcciones RVA) más los logs de la sesión 18 del anfitrión
(`C:\Emulador CD\backups\p8-sesion18-host\cdmp_20261011-161343.log` y `cdmp_packets_20261011-161354.log`). No se
lanzó el juego ni se abrió Ghidra (no se creó copia; sin `java.exe`). No se tocó código.

- **[V]** comprobado (bytes/instrucciones o líneas del log citadas).
- **[E]** deducción fuerte.
- **[H]** hipótesis; hay que medirla.

## 0. Resumen

1. **El desmontaje de un jugador no pasa por `0x2228E40`. Lo hace en línea el manejador de cambio de acción del
   CC**: `ClientCharacterControlActorComponent::vf89` `0x8610A0` llama primero a `0x87CBC0` (cambio de método
   de movimiento: `Exit` del viejo y `RideOff::Enter` del nuevo, que **lee** el vínculo) y justo después a
   `0x397F40`. Esta función llama directamente a `caballo.Vehicle->vf43` (quita el ocupante y baja la cuenta) y a
   `jinete.Vehicle->vf42` (borra `Link+0x10/+0x14`), con **bandera 0** y `p4 = [def. de acción+0x34]`.
   **Fase:** el mismo fotograma en que empieza RideOff, inmediatamente después de su `Enter` [V código].
2. **En el observador pasa lo mismo, sin nosotros** [V S18]. En la transición 1, la muestra de `RemoteMotion` que
   pasa al método 7 RideOff ya tiene el `link` a 0. Además, nuestra retirada 0,9 s después dio
   `already applied` (vínculo, ocupante y cuenta ya a 0). `0x2228E40` está enganchado y no se llamó.
3. RideOff no necesita el vínculo después de `Enter`: guarda la clave del caballo en `método+0xD4`. Su `vf2`
   remoto (`0xAB0540`) y su `Exit` (`0x683820`→`.xtls 0x914B860`) no leen el vínculo ni retiran nada [V].
   - Retirar **antes** de `Enter` lo rompe (`Enter`=0).
   - Retirar **después** no le afecta, aunque es redundante.
4. **La bandera 12 no es una pose.**
   - Es la de "expulsar a todos los jinetes" de `ClientVehicle::vf49` `0xA46B00` (expulsa y además teletransporta
     al suelo).
   - En `vf42`/`vf43` las banderas 0 y 12 caen en la misma rama por defecto. Solo cambian algo con actores
     **controlados localmente** (`vf188(5)`).
   - En una pareja remota (C8 0/0) dan el mismo resultado [V].
5. **La postura agachada de S18 no la causa la retirada.** El jinete remoto se queda en el **método 7 con la
   acción `C9356CEB` durante más de 40 s** y no instala ninguna de las 2991 que siguen llegando: ni las de a pie,
   ni la de volver a montar, ni las de cabalgar [V log].
   - Pasa también en la transición 1, donde la retirada fue nativa.
   - Lo que devuelve a la locomoción es instalar la siguiente acción (`0x48AD40`→`0x863530`), luego `vf89` y
     luego `0x87CBC0` (`RideOff::Exit`). Ese paso no ocurre.
   - Puertas candidatas en §4 [H]. **Cerrado en §6:** el filtro está en `0x2329A90`→`.xtls 0xEE58A40`. Una
     acción que llega por la red se descarta mientras la acción actual de una capa tenga la bandera de definición
     `0x40` [V código]. Un jugador remoto no tiene otra vía para cambiar de acción [E].
6. **Recomendación** (§5): no retirar al recibir el `Detach` del controlador; dejar que lo haga la ruta nativa
   del propio observador; quedarse solo con un respaldo, con bandera 0 y **nunca** con el método en 6/7; y medir
   por qué se rechazan las acciones tras RideOff.

## 1. Controlador: qué borra el vínculo de un jugador, desde dónde y cuándo

### 1.1 Cadena [V]

```
(acción nueva instalada en el CC)
ClientCharacterControlActorComponent::vf89 0x8610A0        (vtable 0x55B5460, slot 89 = +0x2C8; RTTI .?AVClientCharacterControlActorComponent@pa@@)
  ├─ 0x2327320  Common CC vf89 (base)                       → dentro, 0x2320EE0 = RideOff en línea con bandera 8 (muerte)
  ├─ …
  ├─ 0x861B2A call 0x87CBC0(mc=[CC+0xC8], acción, u8)      cambio de método:
  │     0x87CFB8 call [old+0x40]  (vf8 Exit del método viejo)
  │     0x87CFE4 mc+0xF0 = nuevo; 0x87D007 call [new+0x38] (vf7 Enter) → [método+0x44]
  │        → RideOff::Enter 0x682C20 lee Link+0x10/+0x14 y guarda la clave en método+0xD4, el asiento en +0xD8
  └─ 0x861B3C call 0x397F40([CC+0xF0], cambios[2×0x78], previas)
        ├─ sale si: no hay acción nueva en la capa, TS+0x3DC≠0, falta Vehicle/Link, Link+0x14 = 0xFF
        ├─ caballo = FindActor(Link+0x10)                (0x8ADFE0)
        ├─ f = [ [CC+0x88+8·capa]+0x10 ]+0x0A  (banderas de la definición de la acción actual)
        │    0x80 = acción "montado"  → no retira (sincroniza otros ocupantes y sale)
        │    0x40 = acción de bajar   → retira
        │    ninguna → retira si las acciones previas tenían 0x40/0x80 (sil)
        ├─ 0x3983C2 call [caballo.Vehicle+0x158] (vf43)(&res, &clave_jinete, p4=[def+0x34], 0, bandera 0)
        └─ 0x398431 call [jinete.Vehicle+0x150]  (vf42)(&res, caballo,      p4,            0, bandera 0)
```

- `vf43` del caballo: `ClientVehicle 0xA46030`→`.xtls 0x9DD1410`→Common `0x2229500`.
  - Busca el asiento del jinete (`0x2227FA0`).
  - Pone `V+0x48[asiento]=0`, `V+0xEA=1` si es el asiento 0, `V+0xC8--` (`0x2229625..0x2229637`).
  - Hace los efectos del caballo con asiento 0: `CC->vf260(0)`, `0x221C200`, `0x17F58A0`. La reacción del
    caballo según la bandera solo ocurre si el caballo es local (`vf188(5)` y `+0x5E&0x10`).
- `vf42` del jinete: `ClientVehicle 0xA45CA0`→`.xtls 0x9DCEE10`→Common `0x2228F50`.
  - `0x222B170`→`.xtls 0xE7012A0`: `Link+0x10 = 0`, `Link+0x14 = 0xFF`, `Link+0x18 = 0`.
  - `[comps+0x50]+0x11C = 0xFFFF`.
  - Anota el último vehículo en `Link+0x1C` y la marca de tiempo en `+0x20`.
  - Solo si el jinete es local y la bandera es 2/5/10/11/13: arranca una acción forzada (`0x207AB50`).
  - La parte cliente (tipos 1 y 3–9): campos `[CC+0x108]`, transform/gráficos (`0x36A69D0`), `UpdateRidingInfo`
    y HUD (`0xA48E50`).
- **Servidor [E]:** `0x2A78CA0` (procesa StartActionReq) → `0x2A797C0` → `0x2A7A300`: el mismo par `vf43`+`vf42`
  en línea, bandera 0. Por eso S17 no vio `0x2228E40` en el reino servidor para jugadores.
- Otros RideOff en línea (`vf43`+`vf42` sin el thunk), con su bandera:
  - `0x20AC330` → 3 (cambio de asiento `0x2229F10` y otros);
  - `0x123AFA0` → 5;
  - `0x2055540` → 6;
  - `0x2320EE0` → 8 (en el vf89 Common);
  - `.xtls 0xE6D5420` → 7 (NPC del servidor; detrás del thunk `0x2226620`).

### 1.2 Fase [V]

**Al empezar RideOff**, en la misma llamada a `vf89` que ejecuta `RideOff::Enter`, justo después. No es un
fotograma clave ni el final de la animación.

- Concuerda con S17: `RideOff::Enter (local player) -> 1`, y en el montaje siguiente el vínculo ya es 0.
- Concuerda con S18: el `MountDetach (observed)` del invitado llegó al anfitrión (t=290,708) **antes** que la
  propia 2991 `C9356CEB` (paquete 290,724; despacho 290,739).
- La acción previa `667E3B35` (método 4) **no** retira: la muestra −272..−30 ms todavía tiene `link 0xB010037D`.

## 2. RideOff sobre un actor remoto (modo 2): qué hace con el vínculo

Vtable `0x5597190`: vf0 `0x682BA0`, vf1/vf3 `0x6832D0`→`0x6832E0` (actualización local), vf2 `0xAB0540`
(remota), vf7 `Enter 0x682C20`, vf8 `Exit 0x683820`→`.xtls 0x914B860` [V].

| Fase | Vínculo | Qué hace [V] |
|---|---|---|
| `Enter` `0x682C20` | **lo lee** (`Link+0x10/+0x14`); sin él devuelve 0 | Busca el caballo. Guarda clave/asiento en `método+0xD4/+0xD8` y la ventana de tiempo en `+0x48/+0x4C`. Transform del asiento (`vf40 0xA453F0`), corrección `0x877F90` sobre `mc+0x1A8`, `mc+0x2A5=1`, enlace de los CC+0x130 (`0x150C02450`, tipos 1/3–9) y conjunto de animación por caballo (`0x972740`) |
| vf2 remoto `0xAB0540` | no lo usa | Interpola (`0x88E6A0`) y física (`mc+0x488`). Sale si `vf188(2)` |
| vf1 local `0x6832E0` | no lo usa | Mezcla asiento→suelo por el tiempo de la capa. Usa `método+0xD4` |
| `Exit` `0x914B860` | no lo usa | Busca el caballo por `+0xD4`. Si el caballo es local y no tiene ocupantes, le lanza una acción. Desenlaza los CC+0x130 (`0x2D297C0`), reinicia el método (`+0xD4=0`, `+0xD8=0xFF`, ventanas −1) y llama a `0x895660(mc,0)`, que limpia el estado de enganche del `mc` |

**Consecuencias:**
- RideOff **no se desengancha solo**. Lo hace `vf89`→`0x397F40`, que corre en cualquier cliente donde se instale
  la acción RideOff del jinete, también en el observador [V S18].
- Una retirada externa:
  - **antes** de `Enter`: `Enter=0` (`+0x44=0`), sin animación ni `SetPosition`. Es el defecto original.
  - **después** de `Enter`: no rompe RideOff (todo está en caché), pero sobra. Los efectos de `vf42` cliente son
    los mismos que los nativos.
- **[V S18]** Transición 1:
  - 290,739: despacho de `C9356CEB`.
  - 290,741: `RideOff::Enter (remote player) -> 1`, con `Link 0xB010037D/0`.
  - Sonda #2: `method 4 -> 7 RideOff, link 0xB010037D -> 0x0` en el mismo t0.
  - 291,606: nuestra llamada encuentra `already applied`, ocupante 0, cuenta 0.

## 3. Banderas: qué usar en el observador

| Bandera | Quién la usa [V] | Efecto en `vf42`/`vf43` |
|---|---|---|
| 0 | desmontaje por acción de jugador (cliente `0x397F40`, servidor `0x2A7A300`) | ninguno extra |
| 7 | NPC en el servidor (`0xE6D5420`) | ninguno extra |
| 8 | muerte (`0x2320EE0`; 2938 con 8) | reacción del caballo (solo si es local) |
| 12 | `ClientVehicle::vf49 0xA46B00`: "expulsar a todos" del vehículo, que además **teletransporta** al jinete al suelo (`TS->vf(0x1E8)`) | ninguno extra en `vf42`; caballo: acción por defecto (solo si es local) |
| 2, 5, 10, 11, 13 | otras causas | acción forzada **solo si el jinete es local** (`vf188(5)`) |
| 9 / 14 | — | cambian la rama cliente de `vf42` (`CC+0x100`) y `vf42` Common llama a `0x232FF90(CC)` con 9 |

- Tabla de saltos de `vf42` Common en `0x2229458`: las banderas 0/1, 3, 4, 6, 7, 8 y 12 van directas al final.
- En un observador con jinete y caballo remotos, 0 y 12 son equivalentes [V].
- Para imitar al controlador: **bandera 0, p4 0** (p4 solo alimenta la reacción de un caballo local).
- La bandera 12 no deja al jinete en ninguna pose.

## 4. Lo que devuelve al jinete remoto a la locomoción, y por qué no ocurrió en S18

**Ruta nativa [V código]:**
1. 2991 `0xB57720` → `0x48AD40` → `0x863530` (comprobaciones) → `0x863880` (instala la acción en la capa).
2. `vf89` → `0x87CBC0`: `RideOff::Exit` y `Enter` del método de a pie.

En el controlador, la cadena de acciones local hace la transición. En el remoto solo puede hacerla la siguiente
2991.

**Lo que muestra S18 [V log]** (anfitrión, jinete `0xA010037B`):
- Tras `C9356CEB` (290,74) llegan 2991 de a pie: `841A7C99` (291,585), `25DC7FB9`, `26107D3C`, `874F689B`… hasta
  311,456. Después llegan la de montar `91605616` (311,841) y las de cabalgar `F79C7424` (313,9; 316,07).
- Aun así, `RemoteMotion` (lee `mc+0xF0` y la acción real de cada capa) da **método 7, acción `c9356ceb/7b6aff57`**:
  - sonda #2: +0..+5023 ms;
  - sonda #3: −1000..+5023 ms alrededor del compromiso de la transición 2 (311,84);
  - sonda #4: hasta 30,2 s después.
- En la transición 2 **no hay** `RideOn::Enter` del remoto. El vínculo que creamos en 311,84 queda sin RideOn.
  Como no cambia la acción, `0x397F40` no corre, y la retirada de 317,47 la hace nuestro respaldo (`APPLIED`).
- Es decir: **ninguna 2991 posterior a RideOff se instala**. El jinete se queda en la última pose de RideOff
  (agachado junto al caballo). Esto ocurre con la retirada nativa, así que no lo causa nuestra llamada.

**Puertas que pueden rechazar en silencio una 2991 [V código; cuál es, H]:**
1. `0x48AD40`, `0x48AE2A`: `[[CC+0x60]+0x10] & 1` y bandera del Ack = 0 → descartada.
2. `0x863530`, `0x863603`: sesión de acción `r9 = [[jinete+0x68]+0x58]+0x18 < [CC+0x58]+8` → resultado 0 sin
   instalar (sesión vieja).
3. `0x863530`: el hash no está en el chart de la capa (`[capa+0x110]`, luego `[CC+0x70+8·i]`, `0x24314C0`) → error
   `[0x6CF6C48]`.
   - **[H] preferida:** RideOff deja la capa con un chart de monta sin las acciones de a pie, y el remoto nunca
     ejecuta el paso local que lo devuelve.
4. `0x863530`: requisito de estado (`0x20C6B40`/`0x20C8350`, `comps+0x38`) → error `[0x6CF7D30]`.

**Sonda propuesta** para la próxima sesión (lecturas SEH en el despacho de 2991 del jinete remoto, durante 3 s
desde su `RideOff::Enter`):
- el resultado de `0x48AD40` y el de `0x863530`;
- el bit `[[CC+0x60]+0x10]&1` y la bandera del Ack;
- `[CC+0x58]+8` frente a `[[jinete+0x68]+0x58]+0x18`;
- `[capa0+0x110]` (puntero del chart) antes y después de RideOff;
- `mc+0xF0` y `método7+0x44`.

La sonda S3 existente (`guest_s3_diag`: `0x48AD40`, `0x863530`) ya da "StartAction … from 2991 … -> result".
Hay que activarla en el anfitrión y filtrar por esa clave.

## 5. Recomendación para el observador

**Vínculo:**
- Mantener el compromiso (`0x2228260`, sin cambios): lo necesitan `RideOn::Enter` y `RideOff::Enter`.
- El vínculo tiene que existir **cuando se instala la 2991 de RideOff** del jinete (`C9356CEB`, `412C2C2B`…).
  Hoy ya existe: se compromete al montar y nadie lo toca hasta ese momento.

**Quién retira y cuándo:**
- La retirada la hace **el propio observador de forma nativa**, en ese mismo despacho, justo después de
  `RideOff::Enter` (`vf89`→`0x397F40`, bandera 0).
- Si la acción de bajar fuera otra (sin RideOff), la retirada llega en la primera acción no montada (`sil`).

**Cambios en `mount_sync`:**
1. **El `Detach` del controlador deja de ser el disparador.** Que pase `detach pending` a "**esperando la
   retirada nativa**", sin llamar a nada. En cada despacho:
   - si `Link+0x10` ya no es ese caballo → `already applied` y `detached`. Es el caso normal; en S18 ocurrió en el
     mismo fotograma de `RideOff::Enter`.
2. **Respaldo único** (para RideOff que nunca se instala, caballo que desaparece o acciones rechazadas). Solo
   cuando se cumplen las dos:
   - el vínculo hecho por este cliente sigue ahí ≥ 1,5–2 s después del `Detach` del controlador;
   - `mc+0xF0 ∉ {6 RideOn, 7 RideOff}`.

   Entonces: `0x2228E40(&res, caballo, &clave, p4=0, bandera=0)`, con las guardas de §10.7 (1) y `verify_detach`.
   La bandera 0 imita al controlador. La 12 no aporta nada en remoto.
   **Nunca** con el método en 6/7: antes de `Enter` lo rompe, y durante RideOff sobra.
3. El respaldo de 6 s "a pie" sin evento se queda, con la misma condición de método.
4. **Nuevo compromiso:** no aplicar `0x2228260` si `mc+0xF0 == 7` y `método7+0xD4 ≠ 0`, es decir, si el jinete
   remoto sigue atascado en un RideOff anterior. En S18, la transición 2 enlazó a un jinete que nunca ejecutó
   RideOn. Aplazar como `not ready` y anotarlo.

**La postura agachada no se arregla con la retirada.** Mientras §4 no esté medido, hay dos opciones:
- **(a)** Para jugadores remotos, `ride_filter` vuelve a reescribir **las acciones de bajar** (`667E3B35`,
  `C9356CEB`, `412C2C2B`) como locomoción, igual que en S17:
  - sin animación de bajar, pero termina de pie;
  - el vínculo lo quita `0x397F40` en la primera acción no montada [E], y si no, el respaldo de (2).
- **(b)** Dejar RideOff nativo y medir con la sonda de §4 qué puerta rechaza la acción siguiente. Corregir esa
  puerta es la única forma de tener **animación y final de pie** a la vez. Si fuera (2) o (3), la solución nativa
  sería instalar la acción con la bandera "forzar" de la 2991 o ejecutar la transición de chart que hace el
  controlador. No se puede decidir sin medir.

**Para la sesión 19:**
- (a) + sonda §4 activas en el anfitrión;
- un desmontaje del invitado con (a) desactivado solo para ese desmontaje (interruptor);
- éxito: tras `RideOff::Enter -> 1`, `mc+0xF0` sale de 7 y la acción de la capa 0 cambia a la de a pie en
  ≤ duración de RideOff + 200 ms.

## 6. Cierre estático de §4: por qué el jinete remoto no sale de RideOff, y arreglo mínimo

Solo lectura (capstone). Mismas reglas que arriba.

### 6.1 Recorrido de una 2991 posterior a RideOff y todas sus salidas tempranas

`0xB57720` (vf2 de 2991) → `0x48AD40(Z, &res, &A[2], u8 bandera, &B)` → `0x863530` (`ClientCC::vf43`, vtable
`0x55B55B8`) → `0x863880` (`ClientCC::vf44`, `0x55B55C0`) → `0x2329A90` (thunk `E9 AB EF B2 0C`) →
`.xtls 0xEE58A40` ("pedir acciones", Common CC). Las salidas en orden:

| # | Dónde | Qué lee | Resultado si falla | ¿RideOff la activa? |
|---|---|---|---|---|
| 1 | `0x48AE2A` | `[[CC+0x60]+0x10] & 1` y la bandera del Ack | `*res=0`, no aplica | No hay escritura en RideOff/`vf42`/`vf43`. Las 2991 de antes de RideOff pasaban [E] |
| 2 | `0x8635BD` | `r9 = [[jinete+0x68]+0x58]+0x18` frente al mismo valor del actor de `0x17EE710` (el jinete, porque no es vehículo) | sale con 0 | No: compara el valor consigo mismo |
| 3 | `0x863607` | `r9 < [CC+0x58]+8` (sesión de acción vieja) | sale con 0 | No hay escritura en RideOff [E] |
| 4 | `0x8636A7`/`0x8636C4` | hash en el chart de la capa (`[capa+0x110]`, luego `[CC+0x70+8·i]`, `0x24314C0`) | `[0x6CF6C48]` | No: los hashes de a pie y el de montar existen; el remoto los usó antes |
| 5 | `0x86373B…0x863784` | requisito de estado (`comps+0x38`, `0x20C6B40`/`0x20C8350`) | `[0x6CF7D30]` | No relacionado [E] |
| 6 | `0x863995` (`0x863880`) | las dos capas resueltas (`+8/+0x10/+0x18`) | `[0x6CF6C50]` | No |
| **7** | **`.xtls 0xEE58A40`** | ver abajo | **descarta en silencio** (`al=1`; `0x863880` deja `*res=0`) | **Sí** |

**Puerta 7 [V código]:**
- `0x1EC9C00` monta cada entrada de 0x78 B a partir del bloque de red. En `0x1EC9C26` hace `or r8b,2` y luego
  `or [entrada+0x50], r8b`: **toda acción recibida por 2723/2991 lleva el bit 1 de `+0x50`** ("viene de la red").
- `0xEE58A40` calcula `bl` = ese bit en cualquiera de las dos entradas (`0xEE58B3F`) y `dil` = `+0x51` bit 3
  (`net+0x42`, `0xEE58B2B`).
- Con `dil||bl` (siempre en una 2991) rechaza si **cualquiera** de estas definiciones tiene `def+0xA & 0x40`:
  - la pendiente `[CC+0x80]`: capa 0 `+0x18` (`0xEE58C0E`) y capa 1 `+0x90` (`0xEE58C3D`);
  - la **actual** de la capa 1 `[[CC+0x90]+0x10]` (`0xEE58C64`);
  - la actual de la capa 0 `[[CC+0x88]+0x10]` (`0xEE58C87`).
- Si pasa: copia a `[CC+0x80]` (`0x86B3A0`), pone `CC+0xA8=1` y llama al `vf(0x1B0)(1,1)` del actor. En el
  siguiente fotograma, `vf89` → `0x87CBC0` (Exit del método viejo y Enter del nuevo).
- En el reino servidor (`TLS+0x1EC≠0`) esta puerta no existe: va a `0x232AC80` (`0xEE58B21`). Por eso el
  servidor sí acepta las acciones del controlador.

**`def+0xA & 0x40` [V código]** es la marca de "acción de bajar del vehículo". La miran también:
- `0x397F40`: `0x40` → retira el vínculo;
- `0x232EF80`: no sigue al caballo;
- `0x2229174` (`vf42`): `0xC0` → no fuerza acción.

`C9356CEB/7B6AFF57` es la acción de RideOff que dispara la retirada nativa. Que la puerta sea 7 encaja con el log:
- **[V log]:** desde que `c9356ceb/7b6aff57` es la acción actual, ninguna 2991 cambia las capas: ni las de a pie
  (`841A7C99`, `25DC7FB9`…), ni la de montar `91605616` (sin `RideOn::Enter`), ni las de cabalgar `F79C7424`.
- Pasa igual **sin vínculo** (S9, doc forense). La puerta no depende del vínculo.
- **[E]** Falta confirmar en vivo que el bit `0x40` está en la definición de `c9356ceb` o en la de su A1. Es una
  lectura: `[[CC+0x88]+0x10]+0xA` y `[[CC+0x90]+0x10]+0xA` en la sonda `RemoteMotion`.

**Método 4 (`667E3B35`) sale normal [V log].** Sonda #2: 9 muestras en método 4 con el vínculo puesto; a los
~270 ms, `C9356CEB` lo sustituye (método 4 → 7). Es decir, `667E3B35` no lleva `0x40` y no bloquea.

### 6.2 Qué lo resuelve en el controlador y por qué no corre en el remoto

- **Controlador [V código + E]:** la acción RideOff no se "limpia". La sustituye la **transición del chart
  local**:
  1. `CC frame` `0x85FBA0` llama a `0x232D2A0` (`0x85FC94`/`0x860110`).
  2. En el reino cliente, `0x232D2A0` llama a `0x232D5C0`, que evalúa las transiciones de la capa (`CC vf99
     0x385F00`, `vf100 0x385FC0`, `0x242D370`).
  3. Las entradas siguientes (`0x1EC9500`/`0x1EC9690`) llevan `+0x52 |= 1` (`0x232D451`/`0x232D495`) y **sin**
     el bit 1 de red.
  4. `0x232D4C1` llama a `0x2329A90`. En `0xEE58A40`, sin `dil/bl`, se toma la otra rama (`0xEE58B83`), que no
     mira `0x40`.

  Ni el update local de RideOff (`0x6832D0`→`0x6832E0`), ni `Exit` (`0x914B860`), ni la marca `mc+0x2A5`
  (`0x68318E`) intervienen en la puerta. `Exit` corre después de la sustitución.
- **Remoto [V código; valor en vivo H]:** `0x85FBA0` solo evalúa el chart si se cumple todo esto
  (`0x85FC0E…0x85FC46`):
  - `CC+0xA8 == 0`;
  - argumento `r8b == 0`;
  - `[CC+0x140]+0x57C ≠ 2`;
  - y, **si el actor es de tipo jugador** (`[actor+0x88]+1 == 1`), `actor+0x96 ≠ 0`.

  Para una copia de jugador remoto, todo encaja con que `actor+0x96 == 0`: sus capas solo avanzan con 2991, como se
  ve antes de RideOff, y nunca hay una transición local que saque a RideOff. En cuanto la acción actual lleva
  `0x40`, todas las 2991 se descartan y **no hay ninguna ruta nativa de salida en modo 2**. Coincide con S9 y S18.

  En los NPC (tipo ≠ 1) el chart sí se evalúa. Por eso su desmontaje no se queda colgado.

  Comprobación barata: leer `actor+0x96` del jugador local y del remoto (solo lectura).

### 6.3 Arreglo mínimo coherente con el juego

**A. Sin ganchos nuevos ni escrituras: la acción `0x40` nunca llega a ser la actual del remoto.**
- `ride_filter` reescribe **solo** las acciones de bajar con `0x40` (`C9356CEB`, `412C2C2B`; la lista se puede
  aprender leyendo `[[CC+0x88]+0x10]+0xA` del **jugador local** cuando hace su propio RideOff) y deja pasar
  `667E3B35` (método 4).
- Resultado: se ve la primera parte de la animación de bajar, la raíz sigue los Move y el jinete termina de pie. La
  montura siguiente (`91605616`) entra.
- El vínculo:
  - lo quita `0x397F40` en la primera acción no montada si la previa era de montar (`sil`, [E]);
  - si no, el respaldo de §5 (`0x2228E40(…, 0, 0)`, nunca con el método en 6/7).
- Es lo que propone el doc forense, con la mejora de no reescribir `667E3B35`.

**B. Animación completa y final de pie: imitar la transición del controlador, sin escribir memoria del juego
[E; experimental].**
- **Gancho:** el thunk `0x2329A90`.
  - Bytes `E9 AB EF B2 0C` (`jmp 0xEE58A40`), inicio de función en `.rsrc`.
  - Llamadores: `0x8639C8` (`0x863880`, ruta de red), `0x232D4C1` (chart), `0xA7A10E` (`0xA79770`) y
    `.xtls 0xEE681B3`.
  - Ningún salto apunta a `0x2329A91..94` (barrido E8/E9 de `.rsrc`, `.xtls`, `.text` y `.xtext`).
  - Es el mismo tipo de parche de 5 bytes que `0xA45740`/`0x2228E40`.
- **El desvío solo actúa si se cumple todo esto** (lecturas SEH; hilo principal del cliente):
  1. la dirección de retorno es `exe+0x8639CD` (viene de `0x863880`, es decir, de 2991/2723);
  2. `actor = [CC+8]` es el jinete remoto de una transacción de `mount_sync` (clave), con `C8 == 0`;
  3. la acción **actual** de alguna capa tiene `0x40` (`[[CC+0x88]+0x10]+0xA` o `[[CC+0x90]+0x10]+0xA`, las
     mismas lecturas que la puerta) y `mc+0xF0 == 7`;
  4. ninguna entrada nueva tiene `0x40` (`[entrada+0x18]+0xA`). Así no se deja pasar otro RideOff duplicado
     (S18 316,56).
- **Qué hace:**
  - copia las 2×0x78 B de `rdx` a un búfer del mod y quita en la copia el bit 1 de `+0x50` y el bit 3 de
    `+0x51`;
  - llama al original con `(rcx, copia)`.

  Así la petición entra por la misma rama que la transición del chart del controlador (`0xEE58B83`). Con
  `+0x52 & 1 == 0`, que es lo que deja `0x1EC9C00`, no la rechaza aunque haya algo pendiente. Después sigue
  `vf89` → `0x87CBC0` → `RideOff::Exit 0x914B860` (el caballo es remoto, así que no hay acción del caballo) →
  `Enter` del método de a pie. `0x397F40` sale enseguida porque no hay vínculo.
- **Momento:** la siguiente 2991 del jinete llega justo cuando el controlador ha salido de RideOff (más la red). Es
  la temporización nativa: no hace falta medir la duración de la animación.
- **Riesgos [H]:**
  - No se encontró, en el código no mutado del área CC (`0x2320000–0x2340000`, `0x858000–0x8A0000`,
    `0x1EC9000–0x1ECB000`), otro lector del bit 1 de red que el de la puerta. `.xtls` no se ha barrido entero.
  - Comprobar en la primera prueba:
    - que el observador no emite `StartActionReq` 2223 con la clave del jinete remoto en su `cdmp_packets`;
    - que `mc+0xF0` sale de 7 en la primera 2991 de a pie;
    - que la capa 0 cambia.
  - Si algo sale mal, desactivarlo y volver a A.
- No se toca `.xtls`, ni se escribe en el actor, el CC ni la pila del juego: solo se pasa una copia propia como
  argumento.

**Recomendación:**
- A para la sesión 19, porque es seguro.
- B detrás de un interruptor (`mount_sync_rideoff_exit=1`, por ejemplo), probado en un solo desmontaje con la
  sonda de §4 (añadir `def+0xA` de las capas y `actor+0x96`).
- La retirada del vínculo queda como en §5: nativa y, si hace falta, el respaldo con bandera 0 fuera de los métodos
  6/7.

## 7. Direcciones citadas

`0x8610A0` (CC vf89 cliente), `0x2327320` (CC vf89 Common), `0x87CBC0` (cambio de método), `0x397F40` (RideOff
por acción), `0x2A797C0`/`0x2A7A300` (servidor), `0xA46030`/`0x9DD1410`/`0x2229500` (vf43),
`0xA45CA0`/`0x9DCEE10`/`0x2228F50` (vf42), `0x222B170`→`0xE7012A0` (borra el Link),
`0x2228E40`→`0xE6F2E60` (thunk), `0xA46B00` (vf49, bandera 12), `0x682C20`/`0x6832E0`/`0xAB0540`/`0x683820`→
`0x914B860` (RideOff), `0x895660`, `0x48AD40`, `0x863530`, `0x863880`, `0x17EE710`.
