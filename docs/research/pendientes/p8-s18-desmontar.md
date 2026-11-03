# P8 sesión 18: el jinete del invitado se queda atascado al desmontar (anfitrión con `mount_sync=1`)

11 de octubre de 2026. Análisis forense de solo lectura sobre `opcion-d` `3827894`. No se lanzó el juego, no se tocó
código. Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`, direcciones en RVA).

- **[V]** = comprobado (log, captura, bytes del PE).
- **[E]** = deducción fuerte, falta medirla en vivo.
- **[H]** = hipótesis.

**Datos**

| PC | Ficheros |
|---|---|
| Anfitrión (`mount_sync=1`, `ride_filter=1`) | `backups\p8-sesion18-host\cdmp_20261011-161343.log` (HL) y `cdmp_packets_20261011-161354.log` (HP) |
| Invitado (`mount_sync=probe`, `ride_filter=1`) | `backups\p8-sesion18-shadow\cdmp_20261011-151520.log` (GL) y `cdmp_packets_20261011-151538.{bin,log}` |

- El vídeo no se pudo decodificar (no hay ffmpeg).
- `t` es el `capture t=` de cada PC. Reloj del invitado ≈ anfitrión − 1 h.
- Claves: jinete del invitado `0xA010037B`, su caballo `0xB010037D` (usuario `0x9010037A`); anfitrión `0xA0100001`/`0xB0100003`.
- En el anfitrión, el actor cliente del jinete remoto es `0x348B5FAAA00` y el del caballo `0x3478327FC80`.

## 0. Resumen

**Causa [V en el log; el mecanismo interno es E/H]:**
- Con `mount_sync=1`, `ride_filter` ya **no reescribe** las acciones de desmontar del invitado: las aplaza a
  `mount_sync` (resumen HL 12272: `mount 1 dismount 4 | rewritten 0 (deferred to mount_sync 5)`).
- Como el observador había creado el vínculo, la acción `C9356CEB/7B6AFF57` entra **de verdad** en `RideOff`
  (método 7) en la copia remota, con `Enter -> 1` y el vínculo presente (HL 10531).
- Desde ese momento la copia **no sale nunca del método 7**: sus capas de acción se quedan fijas en
  `c9356ceb/7b6aff57` durante al menos **31 s** (desde t=290,74 hasta, como mínimo, t=322,5; HL 10634, 11209, 11210, 11404).
  En ese tiempo ignora 53 `StartActionAck` posteriores (HP, t 291,5–322,5) y también **toda la segunda montura**.
- Es la pose "agachada junto al caballo". La raíz sigue las muestras Move (sin episodios de divergencia), pero la
  animación se queda congelada en la de `RideOff`.

**No es** un problema de la retirada:
- En la 1.ª transacción, el propio `RideOff` nativo de la copia quitó el vínculo en ≤ 30 ms, igual que hace en el
  controlador (16–31 ms, GL 2498 y 2635). La retirada del observador llegó 0,87 s después, vio `Link 0` y **no llamó
  a nada** (`already applied`, HL 10589).
- El bloqueo empieza en `RideOff::Enter`, antes de cualquier retirada. La bandera 12 no interviene.

**Comparación**

| Sesión | Qué hacía `ride_filter` con el desmontar | Qué se veía en el anfitrión |
|---|---|---|
| S9 | Sin `ride_filter` y sin vínculo | `RideOff` con `Enter 0`: congelado a la altura de la silla, para siempre |
| S17 | Lo reescribía a locomoción a pie (`26107D3C`, A1 vacío) | La copia nunca entraba en `RideOff`: seguía la trayectoria del desmontaje (raíz por Move) sin su animación. Es la línea base que describe el usuario |
| S18 | Lo aplaza (`mount_sync=1`) | La copia entra en `RideOff` con `Enter 1` y se queda atascada, ahora con la raíz moviéndose |

Conclusión: una copia en modo 2 que entra en el método 7 no vuelve a salir, tenga vínculo (S18) o no (S9).

**Arreglo mínimo:**
- `ride_filter` solo aplaza a `mount_sync` las acciones de **montar**; las de **desmontar** (A1 `0x7B6AFF57` con
  padre `0xB`) se reescriben siempre, como en S17.
- `mount_sync` sigue retirando el vínculo con `0x2228E40(&res, caballo, &clave, 0, 12)` en el primer despacho a pie o
  a 1 s del `Detach` observado. Ahora esa retirada sí hará falta, porque ya no corre el `RideOff` nativo de la copia.
  La llamada está probada en vivo: 2.ª transacción, `APPLIED | post: ok`, HL 11221.

Detalle en §4.

## 1. Las transiciones del invitado vistas por el anfitrión

Hay dos montajes y dos desmontajes del invitado (GL 2144–2147, 2497–2498, 2595–2598, 2634–2635). No hubo ninguna
línea `QUARANTINED`, `FAILED`, `game faults` ni `REFUSED` (resumen HL 12177 y 14008: `outcomes [APPLIED 3, already
applied 1]`, `post failures 0`, `game faults 0`).

### 1.1 Transacción 1: montar (t≈241,4) [V]

| HL | t | Qué |
|---|---|---|
| 9174 | 241,37 | 2991 `875B47E6/26107D3C`, padre −1, objetivo el caballo. `ride_filter` no la clasifica: no está en su lista |
| 9175–9176 | 241,373 | `MountBegin` y `MountCommit` del invitado, transición 1, en el mismo lote: `begun` y luego `committed` |
| 9177–9178 | 241,397 | El reino servidor hace su propio vf53 (`OTHER thread`, `C8 1/1`, `*res 0`), como en S17 |
| 9179–9182 | 241,399 | Observador: `0x2228260(RCX vc=0x34783216B80, R8 rider=0x348B5FAAA00, R9b 0, fifth 0)`, `APPLIED`, `post: ok`, `Link 0 -> 0xB010037D/0`, cuenta 0 → 1, `same controller yes` |
| 9183–9184 | 241,401 | La misma 2991 entra en `RideOn::Enter (remote player) -> 1` con `Link 0xB010037D/0`, método 6 |
| 9395–9400 | | RemoteMotion #1: `method 6 RideOn enter 1 … link 0xB010037D` durante 2,24 s; luego `method 0` montado con vínculo (`f79c7424/…`) |

- El montaje con vínculo funciona.
- El "desaparece unos ms al montar" encaja con muestras de raíz espurias dentro de la sonda: `y 32.01..614.76` en el
  segmento RideOn y `y 0.00..614.80` en el primero montado (HL 9399–9400). Es un detalle aparte [E].
- Geometría `linked here` (HL 12177): `|off the seat|` ≤ 0,5 m en 51 de 53 lecturas; máximo 0,67 m a ≥ 10 m/s.

### 1.2 Transacción 1: desmontar (t≈290,5) — aquí empieza el bloqueo [V]

| HL / HP | t | Qué |
|---|---|---|
| HP 277390 / HL 10528 (`rider action`) | 290,453 / 290,472 | 2991 `667E3B35/7B6AFF57`, padre caballo, `ride kind dismount`, `Link here 0xB010037D/0`. **Aplazada** (no reescrita). Sonda: `method 4` (AnimationAll), `enter 1` |
| HL 10529 | 290,708 | `MountDetach … flags 0x9` (observado), bandera 12: `detach pending` |
| HP 277631 / HL 10530 | 290,724 / 290,739 | 2991 `C9356CEB/7B6AFF57`, padre caballo, `ride kind dismount`, `txn detach pending`. **Aplazada** |
| HL 10531 | 290,741 | `RideOff::Enter (remote player) -> 1`, **`Link 0xB010037D/0`** (leído al volver de `Enter`), método 7 |
| HL 10634 | 290,74 + 0…5,0 s | RemoteMotion #2: t0 = `method 4 -> 7 RideOff, link 0xB010037D -> 0x0`; `[+0..+5023 ms x167] method 7 RideOff enter 1 \| act c9356ceb/7b6aff57 \| link 0x0`; raíz movida 5,08 m |
| HP 278377 | 291,585 | Siguiente 2991 del invitado: `841A7C99` (aterrizar) |
| HL 10588–10589 | 291,606 | Retirada: `detach args 0x2228E40(&result, horse=0x3478327FC80, &rider key, p4=0x0, flag=12)` con `judge: apply -> already applied`, `*result 0xFFFFFFFF`, `Link 0 -> 0`. No se llamó: es el valor inicial, y no hay ninguna línea `MountDetach … rider 0xA010037B` del gancho de `0x2228E40` |

**Quién quitó el vínculo [V]:**
- En la primera muestra con método 7 (≤ 30 ms después de `Enter`) el vínculo ya es 0, y nadie pasó por el thunk
  `0x2228E40`.
- Es la ruta nativa de `RideOff`, la misma que en el controlador: `RideOff::Enter (local player) -> 1` y, 31 ms
  después, `MountDetach (observed) … by a route that is not 0x2228E40` (GL 2497–2498; en la transacción 2, 16 ms,
  GL 2634–2635).
- Es decir, la copia del anfitrión hizo **exactamente lo mismo** que el controlador al entrar en `RideOff`.

**Disparo de la retirada [E]:**
- Fue el primer despacho "a pie" (la 2991 `841A7C99`, 0,90 s después del `Detach`), dentro del tope de 1 s.
- Llegó 0,87 s **después** de que el vínculo ya no existiera: no hizo nada y no causó nada.

### 1.3 Entre las dos monturas: atascado [V]

- Del t=291,6 al t=311,8 llegaron al cliente del anfitrión **32 `StartActionAck`** del jinete con acciones a pie
  (HP 278377–294202: `841A7C99`, `25DC7FB9`, `26107D3C`, `874F689B`, `ECF9B7D2`, `FBD56B20`, …).
- RemoteMotion #3 muestra, justo antes del siguiente montaje (`[-1000..-30 ms]`, HL 11209):
  `method 7 RideOff enter 1 | act c9356ceb/7b6aff57 | link 0x0`.
  **Ninguna de esas 32 acciones cambió las capas ni el método.**
- `divergence_probe`: `episodes 0` en todos los resúmenes (HL 10841, 13052, 14506). La raíz sigue al canal: la
  figura se desliza con la pose de `RideOff`, sin quedarse clavada como en S9.

### 1.4 Transacción 2: montar y desmontar con la copia ya atascada [V]

| HL / HP | t | Qué |
|---|---|---|
| HL 11055–11056 | 311,82 | `MountBegin`/`MountCommit` de la transición 2: `committed` |
| HL 11065–11068 | 311,842 | Mismos argumentos de vf53, `APPLIED`, `post: ok`, `Link 0 -> 0xB010037D/0` |
| HL 11069 (`rider action`) / HP 294567 | 311,842 | 2991 `91605616/26107D3C` (`ride kind mount`), aplazada (la única `mount` del resumen) |
| — | | **No hay `RideOn::Enter (remote player)`**: la copia sigue en el método 7 y no acepta la acción de montar |
| HL 11210 | +0…+5 s | `method 7 RideOff enter 1 \| act c9356ceb/7b6aff57 \| link 0xB010037D` toda la segunda monta |
| HL 11206 | 316,569 | `MountDetach` observado: `detach pending` |
| HP 298622 / 298822 | 316,31 / 316,56 | `667E3B35` y `C9356CEB` (A1 `7B6AFF57`), aplazadas; **sin `RideOff::Enter`**, porque ya está en el método 7 |
| HL 11219–11221 | 317,471 | Retirada en el primer despacho a pie (HP 299863 `E21887A3`, 317,465): `0x2228E40(…, p4=0x0, flag=12)`, **`APPLIED \| post: ok \| *result 0x00000000 \| Link 0xB010037D/0 -> 0 \| occupant 0xA010037B -> 0 \| count 1 -> 0`** |
| HL 11404 | 317,47 + 0…5 s | RemoteMotion #4: `method 7 RideOff enter 1 \| act c9356ceb/7b6aff57 \| link 0x0`. Sigue atascado |

- Después no hay más sondas: el disparador de la sonda es un cambio de método o de vínculo de montura.
- El usuario 2 se desconecta a las 16:21:03 (HL 14284 y 14299), sin que conste una salida del método 7.

### 1.5 Las demás preguntas

| Pregunta | Respuesta |
|---|---|
| ¿`ride_filter` aplazó o reescribió? [V] | Aplazó las 5 (1 montar y 4 desmontar), sin reescribir ninguna. `fallback rewrites 0`: el respaldo de `mount_sync` solo reescribe si **falta** el vínculo, y en las dos 2991 de desmontar de la transacción 1 estaba (`its Link here 0xB010037D/0`) |
| ¿La bandera 12 está mal para jugadores? [V] | No: la única llamada real (transacción 2) dio `*res 0` y `post: ok`. La de la transacción 1 no se hizo |
| ¿Retirada demasiado pronto? | No: el vínculo existía en `RideOff::Enter` (`-> 1`) |
| ¿Demasiado tarde o nunca? | No es la causa: el vínculo lo quita el propio `RideOff` nativo. Ninguna de las dos retiradas cambió la pose (la de la transacción 2 quitó el vínculo y el método 7 siguió) |
| ¿`RideOff` necesita el vínculo en alguna fase concreta? | En la copia remota da igual: en S9 sin vínculo (`Enter 0`) y en S18 con vínculo (`Enter 1`) acaba igual, atascada en el método 7 |

## 2. Por qué la copia no sale de `RideOff` (mecanismo)

**Lo comprobado [V, bytes del PE]:**
- La vtable de `RideOff` (`0x5597190`) tiene:
  - `vf1 = vf3 = 0x6832D0`: el update local de los modos 1/3;
  - `vf2 = 0xAB0540`: el de modo 2, pura interpolación (`movimiento-remoto-cliente.md`);
  - `Enter 0x682C20`;
  - `Exit 0x683820`: un `jmp 0x914B860` a `.xtls`, mutado.
- `0x6832D0` es la lógica real del desmontaje: con la clave del vehículo guardada en `Enter` (`método+0xD4`) busca el
  caballo, interpola la fase de la animación (`método+0x48/+0x4C`), escribe las correcciones de `mc+0x2A8`/`mc+0x1D4` y
  llama a `0x878250` sobre `mc+0x1A8`.
- **En el modo 2 esa función no corre nunca.**
- `RideOff::Enter` (Ghidra `682c20.c`) deja `[[método+0x18]+0xC8]+0x2A5 = 1` (RVA `0x68318E`) y prepara
  `0x877F90(mc+0x1A8, …)`.
  - `RideOn::Enter` llama a `0x877F90`, pero **no** escribe `+0x2A5`.
  - En el código no mutado no hay ninguna lectura de `mc+0x2A5` en el área de movimiento (escaneo `byte ptr [..+0x2A5]` de
    `.rsrc`: el único acceso cercano es la escritura de `0x68318E`).

**Hipótesis [H]:**
- La salida del método 7 (y con ella la aceptación de la acción siguiente) depende de esa lógica local, o de `Exit`
  en `.xtls` y del indicador `mc+0x2A5`. Ninguno de los dos avanza en modo 2.
- `RideOn` no tiene ese estado y sale bien con la siguiente acción (sonda #1: 6 → 0 a +2,27 s).
- Para cerrarlo haría falta descompilar `0x914B860` (Exit) o vigilar `mc+0x2A5` en vivo. No hace falta para el
  arreglo.

## 3. Línea base de S17 y lo que pide el usuario

- **S17** (`probe` en los dos; HL17 `cdmp_20261011-150156.log` 17216: `dismount 2 | rewritten 2`; 15497:
  `F79C7424/7B6AFF57 … -> REWRITTEN`).
  - El desmontaje del invitado llegaba como locomoción a pie, así que la copia nunca entraba en `RideOff`: sin
    `RideOff::Enter (remote player)` y sin sonda de método 7.
  - La raíz seguía los Move (la trayectoria del desmontaje), pero **sin la animación**. Es lo que describe el usuario.
- **S18:** sin reescritura, la animación de `RideOff` arranca, pero la copia se queda en ella para siempre (§1.3).
  Visualmente es la "pose agachada" (`c9356ceb`), arrastrada por la raíz junto al caballo.
- En el invitado (`probe`), el anfitrión se vio con la línea base de S17:
  - `RideFilter … 0xA0100001 mount A0 0x872A6750 … -> REWRITTEN` (GL 3060);
  - el `Detach` observado del anfitrión llegó (GL 3718) y `MountApply [probe] … detach … nothing called` (GL 3720);
  - no hay sonda de método 6/7.

## 4. Arreglo mínimo (para la sesión 19)

1. **`src/mod/session2/ride_filter.cpp`, `defer_to_mount_sync` (línea 92)**: aplazar solo los montajes.

   ```cpp
   return mode == Mode::On && o.remote && o.kind == Kind::Mount && mount_sync_ride_filter_defers(o.ack.key);
   ```

   - Un desmontaje (A1 `0x7B6AFF57` con padre `0xB`) se reescribe en `OnRecvPacket` como en S17, aunque haya
     transacción. La copia no entra nunca en el método 7.
   - Test: en `optd_mount_sync` (o en el de `ride_filter`), un `Dismount` con transacción en
     `riding`/`detach pending` da `rewritten`, no `deferred`.
2. **`mount_sync` (sin cambios de lógica):** la retirada que ya existe pasa a ser la que quita el vínculo, porque ya
   no lo quita el `RideOff` nativo de la copia.
   - Se dispara con el `Detach` observado y luego el primer despacho a pie, o a 1 s; si no hay evento, con el respaldo
     de 6 s.
   - La llamada con bandera 12 está medida: `APPLIED`, `post: ok` (HL 11221).
   - Durante los ≤ 0,9 s entre la acción reescrita y la retirada, la copia va a pie con el vínculo aún puesto. Mientras
     cabalga ya está en el método 0 con vínculo (sonda #1), así que no debería afectar [E].
   - Opcional: retirar ya en el despacho de la propia acción de desmontar reescrita, si la transacción está en
     `riding`/`detach pending` (adelanta la retirada ~0,9 s).
3. **Comprobación en S19:**
   - en HL, `RideFilter … dismount … -> REWRITTEN` y ningún `RideOff::Enter (remote player)`;
   - `MountApply [1] … detach -> APPLIED` en cada desmontaje;
   - sondas sin método 7;
   - el siguiente montaje con `RideOn::Enter (remote player) -> 1` (en S18, el 2.º montaje no entró).

**Qué no arregla:** la animación de desmontar en el observador. Se vuelve a la línea base de S17: trayectoria sin
animación.

Para tenerla haría falta, como etapa siguiente [H]:
- **(a)** Reescribir solo la acción del método 7 (`C9356CEB` / `412C2C2B`) y dejar pasar `667E3B35`, que es del
  método 4 (AnimationAll) con `Enter 1` (sonda #2, HL 10633). Se vería la primera parte de la animación.
- **(b)** Encontrar en `Exit 0x914B860` / `mc+0x2A5` qué cierra `RideOff` y cerrarlo a mano en la copia cuando llegue
  la primera acción a pie.

## 5. Notas

- La retención no se usó (`holds 0`): `Begin` y `Commit` llegaron juntos, como en S17.
- Los hooks están todos `hooked` (HL 54). `observer retirements without the controller's Detach 0`. La vigilancia del
  controlador publicó los dos `Detach` (GL 2498, 2635: `observed Detach published` en sus resúmenes).
- Desfase en la silla al cabalgar: la geometría con vínculo da `|off the seat|` ≤ 0,5 m en 51 de 53 lecturas. El
  desfase visible al galope sería la fase de animación (§10.2 (a) de `p8-impl-monturas.md`). No se trata aquí.
