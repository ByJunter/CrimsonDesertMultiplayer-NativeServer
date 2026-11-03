# Informe: el juego se cierra al crear la copia del caballo del otro jugador

Crimson Desert, build **1.0.0.2976** (parche 2.03.02).
- PE timestamp `0x6AB28F00`, base fija `0x140000000`, sin ASLR. Todas las direcciones son RVA (`exe+…`).
- El ejecutable lleva Denuvo: parte del código está "mutado" en la sección `.xtls`.
- El juego manda sus cierres a Pearl Abyss (sentry/crashpad).

Fecha: 2026-10-03. Este documento es autocontenido: está pensado para dárselo a otra persona o IA
y pedirle ideas.

---

## 1. Contexto: qué hace el mod

Es un mod cooperativo (C++20, DLL `.asi` cargada con Ultimate ASI Loader). Usa hooks con
**safetyhook** y overlay ImGui en DX12.

**El juego es un cliente/servidor MMO dentro del propio proceso.** El cliente y un servidor local
se hablan con mensajes "Troc": `u16 opcode | u8 slot | u16 len | payload`. Nombres y opcodes
salen del RTTI del exe.
- Hooks en `ClientNet::PostPacket` (`exe+0x35C670`, cliente→servidor) y
  `ClientNet::OnRecvPacket` (`exe+0x35CA00`, servidor→cliente).
- El mod **inyecta** mensajes servidor→cliente (Acks) desde el hook de `OnRecvPacket`, en el hilo
  de red del cliente. Así el cliente "cree" que el servidor le manda cosas.

**Cómo aparece el otro jugador:**
1. Cada PC captura las `ActorLoadDataAck` (opcode 2005) de su propio personaje al cargar la
   partida.
2. Las manda al otro PC.
3. El otro PC las inyecta con **claves de actor nuevas**: usuario `0x9010F001`, jugador
   `0xA010F001`, cuerpo `0xB010F001`. El cliente crea un "jugador remoto", como en un MMO.
4. Ese personaje se mueve inyectando `MoveActorAck`, `StartActionAck` y `RotateActorAck` hechos a
   partir de los `MoveActorReq` / `StartActionReq` / `RotateActorReq` reales del otro jugador.

**Todo esto funciona y está probado en vivo con un amigo por Steam:** movimiento, escalada,
equipo, icono en el minimapa.

El personaje remoto está en **modo de control 2** ("remoto"). Lo devuelve `0x88E8E0(mc)`.
- 1 = local, 2 = remoto, 3 = simulado localmente con IA, como un NPC.
- Es 2 porque `u32 [actor+0x90]` (usuario dueño) ≠ 0.
- En modo 2 el actor no simula nada: solo interpola el búfer de red y reproduce las acciones que
  le llegan por `StartActionAck`.

---

## 2. Objetivo

Que también aparezca el **caballo** del otro jugador: una copia `0xB010F002` que se mueva y que el
jugador copia pueda montar.

Datos del caballo real:
- Es un actor del usuario (tipo de registro **5 = montura**, clave `0xB0100003`).
- Llega en su propia `ActorLoadDataAck` (~8.770 B), 0,1 s después del jugador, al cargar la
  partida.
- Su IA corre en el cliente local (es "nuestro").

---

## 3. Cómo se crea la copia del caballo

`ActorLoadDataAck` es una lista `[u16 len][registro]…`. El manejador (`0xCB25A0` → `.xtls
0xA79EA60`) llama a `0x8BD530(ClientActorManager, registro)` por cada registro.

**Paquete del caballo: dos registros.**

| Offset | Campo | Real | Copia |
|---|---|---|---|
| `+0x00` | u16 len del registro 0 | 9 | 9 |
| `+0x02` | registro 0: tipo 0 (usuario), u32 clave, u32 | `00 90100000 0` | `00 9010F001 0` |
| `+0x0B` | u16 len del registro 1 | `0x2235` | `0x2235` |
| `+0x0D` | u8 tipo de actor (1 jugador, **5 montura**, 7 mundo…) | 5 | 5 |
| `+0x0E` | u32 clave | `0xB0100003` | `0xB010F002` |
| `+0x12` | u32 dueño (usuario) del registro | `0x90100000` | `0x9010F001` |
| `+0x16` | u8 índice en el contenedor de hijos del usuario | 2 | 2 |
| `+0x1F` | primer u32 de los datos = `actor+0x90` (decide el modo de control) | `0x90100000` | `0x9010F001` |

**Qué cambia en la copia:**
- Las claves usuario, jugador y caballo.
- Un miembro de grupo dentro del paquete.
- La posición: aparece 3 veces y se reescriben las 3.

Por lo demás es **idéntica byte a byte** a la real.

**Camino de creación con dueño ≠ 0:**
1. `0x8AF580` → `0x8ADFE0(dueño)`, que exige `u16 [usuario+0x5E] & 4` (usuario "logined").
2. Fábrica `0x4634C0` (`.xtls 0x898D890`).
3. Ranura en el contenedor de hijos del usuario.
4. Unpack `0x17EA310`, que hace `actor+0x90 = +0x1F` y llama a `vf11`/`vf13`/`vf14` de cada
   componente.

Con dueño = 0 se usa la fábrica `0x4631C0` (como el cuerpo del jugador, tipo 7).

**El servidor real manda antes del caballo** un "preámbulo" de Acks: `VaryHPAck`, `ApplyBuffAck`
×n, `RefreshMercenaryDataAck` (11,6 KB), `JoinPartyAck`, `SetMinStatRateAck`… Nosotros **no** lo
reproducimos. El análisis estático dice que nada de eso toca acciones ni capas de animación.

---

## 4. Intentos y resultados (todos en vivo, un solo PC, "espejo" por loopback)

| # | Qué se hizo | Resultado |
|---|---|---|
| A | Carga con dueño `0x9010F001`, en la misma tanda que el jugador copia | **No se crea**, sin error ni cierre. Hipótesis: el usuario copia aún no tenía `+0x5E & 4` |
| B | Carga **sin dueño** (`+0x12 = 0`, `+0x1F` = `0x9010F001`) | **Cierre** en `exe+0x1F63FE6` |
| C | Con dueño, inyectada **3 s después** del jugador | **Cierre** en `exe+0x1F63FE6` (el mismo) |
| D | C + **R1**: mid-hook al inicio de `0x1F63F70` que devuelve 0 si la "reproducción" de la capa es nula. + **R2**: retener la carga hasta el primer `StartActionReq` del caballo del emisor e inyectar su `StartActionAck` en la misma tanda, justo detrás | R1 actuó 1 vez. Luego **cierre** en `exe+0x232D5FF` |
| E | D + mid-hook al inicio de `0x2329700` que se salta toda la función para nuestras copias si una capa tiene gráfico pero no reproducción | El salto actuó 1 vez. Luego **cierre** en `exe+0x925F0E` |

En B a E el cierre llega **~0,23 s después de inyectar la carga del caballo**:
- D: carga a las 22:05:44.838, cierre a las 45.073.
- E: carga a las 22:32:47.873, cierre a las 48.108.

El sniffer registra la entrega de la carga, pero **no** la del `StartActionAck`. No se sabe si el
Ack se aplicó o si esperaba en la cola de reintentos del cliente, que guarda los Ack de actores
"aún no listos" (`0x8B53C0`).

---

## 5. Los cierres en detalle

Los tres son lecturas de un puntero nulo, en un **hilo trabajador** que actualiza actores en
paralelo. La cadena común es:

```
bucle de hilo 0x1414DE0 → ParallelTaskWorker::vf3 0x13FB350 → tarea "actualizar actor" 0x8B56D0
  → actualización del ClientCharacterControlActorComponent (CC) 0x85FBA0 → …
```

### 5.1 Las "capas de acción" del CC

- `CC = [[actor+0x68]+0x40]`. Tiene 2 capas: `[CC+0x88]` (capa 0, principal) y `[CC+0x90]`
  (capa 1).
- Cada capa es un objeto de 0x118 B (constructor `0x2435B70`, todo a cero):

| Campo | Significado (deducido) |
|---|---|
| `+0x00`, `+0x08`, `+0x10` | acción actual: `+0x08` datos (velocidad en `+0x27C`), `+0x10` nodo (estado en `+0xEC`, flags en `+0x108`/`+0x10A`) |
| `+0x18` | reproducción de la acción actual |
| `+0x20` / `+0x24` | tiempo anterior / actual |
| `+0x100` | ? (lo lee `0x925CE0`) |
| `+0x110` | gráfico de acciones propio de la capa |

**La copia del caballo tiene la capa principal con gráfico (`+0x110 ≠ 0`) pero sin acción
(`+0x10`, `+0x18` nulos).**
- El caballo real nunca está así: su IA local arranca la acción de espera `0x27FE18E9` a los
  0,31–0,41 s de cargarse. Se ve en 6 de 6 capturas.
- El jugador copia (modo 2) también nace sin acción, pero **no se cierra**. Se supone que su capa
  no tiene gráfico propio (`+0x110 = 0`) hasta la primera acción, así que esas rutas no se
  ejecutan.

### 5.2 Cierre 1: `exe+0x1F63FE6` (intentos B y C)

- Función `0x1F63F70`: avanza el tiempo de la acción de una capa.
  - `rcx = &capa+0x18`; hace `r = *rcx` (nulo) y luego `mov rdx,[rax+0x10]`, que lee `0x10`.
- La llama `0x2329700` (avanza las 2 capas, desde `0x863E80` en `0x8641C6`).
  - `0x2329700` solo comprueba `capa+0x110 ≠ 0`, `dt > 0` y `ritmo > 0`; **no comprueba `+0x18`**.
  - Casi todos los demás consumidores del campo sí lo comprueban.
- Pila: `0x13FB350 → 0x8B56D0 → 0x85FBA0 → 0x863E80 → 0x2329700 (capa 0) → 0x1F63F70`.
- El `dt` del actor que se cerró era 6,485 s. Coincide con el tiempo desde que cargó el caballo
  **real**, no la copia, así que no está 100 % probado que el actor fuera la copia.

### 5.3 Cierre 2: `exe+0x232D5FF` (intento D, con R1 activo)

- Dentro de `0x2329700` → `0x232D2A0` → `0x232D5C0`.
- `0x232D5C0` hace `rax = [r8+0x10]` y luego `movzx eax, word [rax+0xE4]`, con `rax = 0`.
  - `r8` es una estructura local del llamador (`[rsp+0x48]` de `0x232D2A0`), copia de campos de la
    capa.
  - El `+0x10` es el nodo de acción actual, nulo.
- Pila (direcciones de retorno): `0x232D5B2`, `0x8641CB` (la llamada a `0x2329700`),
  `0x860115`, `0x8B5766`, `0x13FB680`.

### 5.4 Cierre 3: `exe+0x925F0E` (intento E, con todo `0x2329700` saltado para la copia)

- Lo llama la actualización del CC `0x85FBA0` en `0x860154`, ya **fuera** de `0x863E80`:
  ```
  rcx = [[[CC+8]+0x68]+0x58]+0x6E8]   ; CC+8 = actor; componente slot 0x58 = IA (Ai)
  call 0x925CE0
  ```
- `0x925CE0` recorre las 2 capas (`[[[actor+0x68]+0x40]+0x88 + 8·i]`):
  - `r11 = [capa+0x10]` (nodo de acción actual) y `r10 = r11`.
  - Solo se comprueba no-nulo para leer `edi = [r11+0x18]`.
  - Después, si la rama "vehículo" está activa, lee `movzx eax, byte [r10+0x108]` **sin
    comprobar**, y se cierra.
- La rama "vehículo" es `[rbp+0x78] = 1`, que se cumple cuando el componente Vehicle
  `[[actor+0x68]+0x48]` tiene `+0xEB ≠ 0`, `+0x48 ≠ 0` y `+0xEA ≠ 0`. Por eso solo afecta a
  monturas.
- Pila: `0x925F0E` ← `0x860159` (en `0x85FBA0`) ← `0x8B5766` ← `0x13FB680`.

**Conclusión:** muchas funciones de la actualización de una montura dan por hecho que **siempre
hay una acción en la capa principal desde el primer frame**. Parchear lectura a lectura no
termina: cada guarda deja ver la siguiente.

---

## 6. Hipótesis actuales

1. **Principal:** un actor montura en modo 2 (remoto, sin IA local) nunca recibe su primera acción
   a tiempo.
   - El `StartActionAck` inyectado justo detrás de la carga no se aplica antes del primer tick del
     CC. Seguramente va a la cola de reintentos porque el actor aún no está "listo".
   - El CC del caballo empieza a actualizarse en cuanto existe el gráfico de la capa.
2. **Alternativa (H2):** el actor que se cierra es el caballo **real** `0xB0100003`, al que la
   copia "despierta" o corrompe.
   - Comparten el n.º de mercenario 12 y el grupo, y la copia está a ~2,5–12 m.
   - Lo apoya el `dt` del cierre 1.
   - En los cierres 2 y 3 no se ha comprobado qué clave tenía el actor; el volcado de crashpad solo
     trae pilas, no el heap.

---

## 7. Ideas no probadas (para valorar)

1. **Saltar toda la actualización del CC (`0x85FBA0`) de la copia del caballo** mientras
   `[capa0+0x10] == 0`.
   - Más general que los parches anteriores.
   - Riesgo: dejar otros subsistemas a medias. Tampoco se sabe si la acción llegaría a aplicarse
     con el CC parado.
2. **Darle una acción en el hilo del juego, de forma síncrona, justo al crearse**, antes de su
   primer tick:
   - llamando a la función interna que usa el manejador de `StartActionAck`, o a la que usa la IA
     local para la espera `0x27FE18E9`;
   - o hookeando el final de la creación del actor.
3. **Crear la copia sin gráfico propio en la capa** (`+0x110 = 0`), como el jugador copia, para
   que esas rutas no se ejecuten.
   - Habría que encontrar de dónde sale `+0x110`: datos del registro, tipo de personaje o
     componente.
4. **Que el caballo copia sea "simulado localmente" (modo 3)**, como los caballos del mundo, y
   filtrar sus Req salientes.
   - Su IA lo arrancaría bien.
   - Pero ignora el búfer de red, así que habría que moverlo de otra forma (teletransportes,
     `MoveForceAck`), y mandaría Req con su clave al servidor local.
5. **Usar una montura "de mundo"** (en las capturas hay 16 de tipo 5 con dueño 0 y IA local) en
   vez del caballo del usuario.
6. **Reproducir el preámbulo del servidor** (`RefreshMercenaryDataAck`, `JoinPartyAck`, buffs…)
   antes de la carga. El análisis estático dice que no toca acciones, pero no está probado.
7. **Averiguar qué actor exactamente se cierra**: un mid-hook de solo lectura en `0x85FBA0` que
   registre `[[CC+8]+0x60]` (clave) y el estado de las capas en cada frame durante 1 s tras la
   carga. Así se resuelve H1 frente a H2.

---

## 8. Preguntas concretas

1. ¿Cuál es la forma "correcta" de que un actor montura creado por red tenga una acción inicial
   antes de su primer tick de actualización?
2. ¿Hay en motores de Pearl Abyss (Black Desert usa la misma familia de código, con mensajes
   Troc, `ClientCharacterControlActorComponent`…) un paso de "post-carga" que fije la acción
   inicial de un actor remoto, y qué mensaje lo dispara?
3. ¿Hay algún patrón mejor que ir parcheando lecturas nulas una por una?

---

## 9. Restricciones del proyecto

- Solo hay un PC para pruebas. El "otro jugador" es un eco por loopback (`cdmp_peer`).
- Cada cierre se envía a Pearl Abyss. Las lecturas de memoria del mod van con SEH/`VirtualQuery`.
- Hooks solo al inicio de funciones (inline) o mid-hooks con safetyhook. Nada de parches a mitad
  de función sin `create_mid`.
- No identificar actores entre PCs por dirección de memoria; se usan claves.
- Antes de cada prueba se copian las partidas y nunca se guarda con la copia cargada.

---

## 10. Evidencia (en el proyecto)

| Archivo | Qué es |
|---|---|
| `captures/horse_crash_210557.dmp`, `captures/cdmp_210510_horse_crash.log` | Intento C (cierre 1) |
| `captures/horse_crash_220545.dmp`, `captures/cdmp_220545_horse_crash2.log` | Intento D (cierre 2) |
| `captures/horse_crash_223248.dmp`, `captures/cdmp_223248_horse_crash3.log` | Intento E (cierre 3) |
| `docs/research/caballo-coop.md` | Análisis completo: mensajes del caballo, formato de carga (§8), cierre 1 y recetas (§9) |
| `docs/research/movimiento-remoto-cliente.md` | Modos de control y movimiento del actor remoto |
| `src/mod/net/remote_puppet.cpp` | `spawn_horse`, `horse_event`, guardas `action_layer_advance` y `action_layers_advance` |
| `src/mod/game/offsets.h` | Direcciones usadas, con su explicación |
| `build/research/horse3/mdmp.py` | Lector de minidumps (excepción, registros, pila) |

El caballo está **desactivado por defecto** (`[coop] horse=0`). Con él desactivado, el
cooperativo funciona sin cierres.

---

## 11. Actualización: resuelto el cierre (2026-10-03, 22:58)

Se siguió la revisión externa. Diagnóstico de solo lectura (`src/mod/net/horse_diag.cpp`) con
mid-hooks en `0x85FBA0` (actualización del CC), `0x8646D0` (restauración de acciones tras cargar
los recursos) y `0x2329D70` (acciones pendientes). Log: `captures/cdmp_horse_diag_ok.log`.

**Resultados:**
- **H1 confirmada.** El actor en riesgo es la copia `0xB010F002`, no el caballo real.
- **Byte de restauración `[[CC+0x40]+0x30]`:** vale **1** en el caballo real y **0** en la copia.
  Por eso `0x8646D0` no le restaura ninguna acción. Datos de restauración de la copia:
  `+0x30 = 00`, `+0xF8 = 0`.
- **El `StartActionAck` sí se entrega**, unos 0,6 s después de la carga.
  - Primero queda pendiente (`CC+0xA8 = 1`).
  - Se aplica unos 0,7 s más tarde: capa 0 con `+0x10` y `+0x18` no nulos.
- **Contención:** saltar `0x85FBA0` y `0x860D30` de la copia mientras la capa 0 tiene gráfico sin
  acción. Bastó con saltar **1** actualización.
  - Sin cierres: el caballo copia aparece, se mueve y el jugador copia lo cabalga.

**Siguiente problema:** al desmontar, el jinete copia se congela a mitad de animación.
- Causa: no hay vínculo real jinete↔caballo, así que `RideOff::Enter` falla.
- Arreglo: al desmontar, recargar solo el jinete y dejar la copia del caballo en el mundo.

**Pendiente (mejora):** poner el byte de restauración y los datos de acción en el paquete de
carga, para que el motor restaure la acción por su ruta nativa y no haga falta la contención.
