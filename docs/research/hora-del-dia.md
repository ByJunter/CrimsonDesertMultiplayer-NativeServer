# Hora del día (y clima) entre anfitrión e invitado

Build analizada: **1.0.0.2976**, PE timestamp `0x6AB28F00`, base `0x140000000`. Todas las
direcciones son RVA salvo que se indique `0x14…`. Análisis **estático**: el juego no estaba
abierto, así que no hay lecturas en vivo. Lo que dependa de valores en tiempo de ejecución está
marcado como pendiente de verificar.

Problema (prueba de dos PC, 2026-10-05): en el mismo momento era de día en el anfitrión y de
noche en el invitado.

**Por qué pasa.** Cada PC ejecuta su propio servidor dentro del proceso. La hora sale de la
partida guardada de cada uno (`FieldSaveData::_fieldTimeRaw`) y avanza por separado. Las
diferencias de carga, pausas, sueños y guardados hacen que las dos horas se separen. Nada en el
mod las sincroniza (`reference/CrimsonDesertCoop` tampoco lo hace).

## 1. Cómo representa el juego la hora

### 1.1 `pa::FieldTime` (32 bytes en memoria, 31 en el cable) — confianza **alta**

Sale del deserializador `0x20CF3F0` (→ `.xtls 0xE2B2260`, solo reubicado) y de las
conversiones `0x20D3460` / `0x20D3360`. El RTTI incluye `VariantItem<FieldTime>`,
`GameMeta_ClassTemplate<FieldTime>`, `getCurrentFieldTime` y `getSecondsOfDay`.

| Off. | Tipo | Campo | Notas |
|---|---|---|---|
| +0x00 | u32 | día | contador de días desde el inicio |
| +0x04 | u32 | hora | 0–23 |
| +0x08 | u32 | minuto | 0–59 |
| +0x0C | u32 | segundo | 0–59 |
| +0x10 | u32 | milisegundo | 0–999 |
| +0x14 | u8 | día de la semana | `día % 7` (lo calcula `fromRaw`) |
| +0x16 | u16 | **escala de tiempo** | vale 12 por defecto (todos los constructores ponen `0xC`) |
| +0x18 | u64 | **ancla** | ms del reloj del proceso (base del servidor) en que se fijó el valor |

En el cable el orden es el mismo, sin el byte de relleno +0x15: 5×u32, u8, u16 y u64, es decir,
31 bytes.

**Tiempo "raw".** `toRaw` está en `0x20D3460` y `fromRaw(ft, raw)` en `0x20D3360`. Primero
calcula `ms = ((h·60+m)·60+s)·1000+ms`. Un día raw son `0x4819080` = 75.600.000 unidades
(21 h):
- de 03:00 a 21:00: `raw = día·75,6M + ms − 5,4M`, a velocidad normal;
- de 21:00 a 03:00: `raw = día·75,6M + (ms [+64,8M]) / 2`. Las 6 horas de noche ocupan 3 h raw y
  pasan el doble de rápido.

**Cómo avanza.** `CommonField::GetCurrentFieldTime` (`0x20E55A0`, sin mutar) sigue estos pasos:
1. Si el nivel tiene hora fija (`_useFixedFieldTime`, byte `+0x6A` de la info de nivel), la
   devuelve (`+0x48`). Esto ocurre en mazmorras y zonas especiales.
2. Si no, toma la copia global (§1.2). Si `field+0x17C == 0` (no pausado), calcula
   `ahora = 0x1416B80() + desfase` y
   `raw' = toRaw(ft) + (ahora − ft.ancla) · ft.escala`, y luego aplica
   `fromRaw(ft, raw')` y `ft.ancla = ahora`.

Con escala 12, un día dura 75,6M/12 ms ≈ **105 min reales** (90 de día y 15 de noche). La
comunidad dice "1 min real ≈ 12 min de juego, unas 2 h por día", lo que cuadra.

### 1.2 Las dos copias globales — confianza **media-alta** (verificar en vivo)

| Dirección | Qué es | Quién la escribe |
|---|---|---|
| `0x146A6D218` (32 B) | FieldTime **del cliente** (hilo sin la marca TLS `+0x1EC`) | `0x21A1CC0`, `0xA38B00` (SetFieldTimeAck), `0x219D7F0`, `0x2BC0A30` |
| `0x146A6D238` (32 B) | FieldTime **del servidor** (marca TLS `gs:[0x58]→[+0x1EC] != 0`) | mismas funciones, rama TLS ≠ 0 |

Todas eligen la copia según la marca TLS `+0x1EC`. La asignación cliente/servidor sale de dos
indicios:
- con la marca puesta, `0x20E55A0` no suma el desfase cliente→servidor;
- con la marca puesta, `0x1416B80` usa el reloj real en lugar del de fotograma.

Hay que confirmarlo leyendo las dos copias en vivo.

**Reloj ("ahora")** — confianza alta:
- `0x1416B80`: en el hilo principal del cliente devuelve el **ms del fotograma**,
  `[0x146C8CD68] + [0x146C8CD70]/1e6` (si `[0x146C8CD35] == 0`). En el servidor devuelve
  `(reloj_ns − [0x146EFBC70]) / 1e6`. Las dos son ms desde una época del proceso.
- `[0x146D69438]` (i64): **desfase cliente→servidor en ms**. Lo escribe el manejador de
  `ServerTimeInfoAck` (`0xBDF310`): `desfase = tick_servidor − ahora_cliente`.
  `[0x146D69430]` = `unix_servidor − time()`.
- `ServerTimeInfoAck` (2188, S→C, cada 5 s, 20 B: `u32 0 | u64 tick ms del servidor | u64
  segundos Unix`) **no** lleva la hora del juego. Solo sincroniza relojes. Visto 1:1 en todas
  las capturas.

**Consecuencia.** El ancla `+0x18` está en la base de tiempo **del servidor de ese PC**. Un
FieldTime copiado tal cual de otro PC tendría un ancla sin sentido: la hora saltaría (ancla en
el pasado) o se congelaría (ancla en el futuro, porque `if (ahora > ancla)` no avanza). **El
receptor tiene que reconstruir el ancla con su propio reloj.**

### 1.3 Escritores y caminos de red — confianza **alta** en el formato, **media** en los efectos

| Mensaje | Opcode | Dir. | Formato | Manejador |
|---|---|---|---|---|
| `SetFieldTimeReq` | 2125 | C→S | `FieldTime(31) | u8 flag | u32 clave` = **36 B** | `0x29BFD70` → `.xtls 0x1049D430` → `ServerFieldComp::SetFieldTime` `0x2BC8970(comp=[[actor+0x68]+0x1A0], &ft, flag, &clave)` |
| `SetFieldTimeAck` | 2816 | S→C | `u32 clave_actor | FieldTime(31)` = **35 B** | `0xB88990` → `.xtls 0xA41D560` → `0xA38B00(mgr=[[[0x146D69188]+0x30]+0x1148], &ft, 1, 0, 0)` |
| `SyncWeatherStateReq` | 2560 | C→S | 6 × 4 B (24 B), ver §3 | `0x29C5DF0` → `CommonField::SetWeatherState` `0x20E6FE0` |
| `UpdateFieldNightAndDay` | 2017 | temporizador interno del servidor | — | `0x2C3BC70` (cada 10 ticks llama a `0x2C3BCD0`; probablemente lee la hora, no la fija; confianza baja) |
| `SetTimeWrapCoolTimeReq/Ack`, `SetTimeWrapCoolEndTimeAck` | 2669 / 2731 / 2529 | | enfriamiento de "saltar tiempo" (`_timeWrapCoolTime`, `CheckCanTimeWrap`) | al cargar llega siempre `SetTimeWrapCoolEndTimeAck = 01 00 10 A0 | u64 0x3463A4` |

Qué hace `0x2BC8970` (servidor), en orden:
1. comprueba que el actor está en su field (`0x17FA6C0`, `field+0x180`) y `0x206AE30`;
2. llama a `0x2842C70` (sin identificar);
3. **`0x21A1CC0(comp, &ft)`**: `comp+0x450 = ft`, y si es el field actual también escribe la
   copia global;
4. **difunde `SetFieldTimeAck` con `ToNearSendTargetFinder`** (`0x299AED0`), que llega
   también al propio cliente;
5. solo si `flag != 0`, busca `clave` en una tabla (`[0x146D6C1F8]`) y hace cálculos de
   posición. Parece ser el punto donde "despiertas" al dormir en cama u hoguera. **Con
   `flag = 0` solo cambia la hora.**

Qué hace `0xA38B00` (cliente, al recibir el Ack): si hay actor/field, llama a `0x21A1CC0` sobre
su componente, que escribe `comp+0x450` y la copia global del cliente. Después llama a
`0xA39330` y publica un evento, probablemente `UITimeOfDayEvent_UPDATE_TIME`. Si no hay field,
como durante una carga, solo escribe la global.

`0x2BC0A30` (servidor, al entrar en un field) toma la hora guardada del nivel (tabla en
`+0x5D0`) o unas por defecto (globales `0x146CF1158…12B8`), le suma `[this+0x80]·1000` ms y la
instala. Ver §4.

**En las capturas no aparece ni un `SetFieldTimeReq` ni un `SetFieldTimeAck`.** Se revisaron
todas las de `D:\cdmp-grabaciones`, las de `bin64` y la del Shadow del 2026-10-05. Es decir:
- no hay una sincronización periódica de la hora entre servidor y cliente;
- la hora solo viaja cuando alguien la cambia (dormir, esperar, eventos de secuenciador
  `GamePlayEvent_SetTimeOfDay`);
- en esas sesiones nadie durmió.

### 1.4 Cadenas útiles

- Datos de guardado: `_fieldTimeRaw`, `_modifiedFieldTimeSecondsOfDays`,
  `_resetTimeSecondsOfDays`, `_currentFieldTime`, `_timeWrapCoolTime` y `_useTimeWrapCoolTime`.
- Por nivel: `_useFixedFieldTime` y `_fixedFieldTime`.
- Debug del motor: `PearlAbyssEngine.Debug.TimeOfDayForward/Backward(x2)` y
  `CycleTimeOfDayNext/Prev`.
- Comandos de consola: `/settimeofdaylowerlimit`, `/settimeofdayupperlimit` y `changeweather`.
  Seguramente están desactivados en la versión de tienda; no se probó.

## 2. Solicitudes de truco y Acks inyectables

- **No hay ningún `…CheatReq` de hora o clima** en los 1.115 mensajes. Los de truco son de
  stats, kill, spawn, misiones, objetos, etc. (`troc_messages_2976.csv`).
- No hace falta: **`SetFieldTimeReq` con `flag = 0` es justo "pon esta hora"**, y el propio
  servidor responde con `SetFieldTimeAck` a su cliente. Se inyecta con `sniffer_inject_c2s`
  (`u16 2125 | u8 slot 0 | u16 36 | payload`), igual que `VaryStatCheatReq`. Confianza
  **media-alta**: formato y cadena de llamadas confirmados; efectos secundarios de `0x2842C70`
  sin verificar.
- Alternativa solo para el cliente: `puppet_inject_s2c(2816, u32 0 | FieldTime)`. Cambia lo que
  se ve, pero el servidor del invitado sigue con su hora: horarios de NPC, tiendas y la hora que
  se guarda. Además se revierte al entrar en otro field, porque `0x2BC0A30`/`0x219D7F0`
  reinstalan la hora del servidor. Sirve como **plan B** o para depurar.
- Escribir directamente en `0x146A6D218/238` es posible, pero no se recomienda. Habría que
  escribir también `comp+0x450` del field, y la interfaz no recibe el evento. El paquete hace
  todo eso por nosotros sin tocar memoria.

## 3. Clima — confianza **media-baja**

El **clima lo simula el cliente**: los componentes de script `weatherSimulator`,
`weatherTimeSimulator`, `weatherCloudSimulator`, `weatherSurfaceSimulator` y
`worldWeatherEffect`. El cliente se lo comunica al servidor con `SyncWeatherStateReq` (C→S)
cuando cambia; no lo envía periódicamente.

`SetWeatherState` (`0x20E6FE0`) calcula `field+0x178` (`TypedFlag32<WeatherState>`) a partir de
estos campos:

| Off. | Interpretación | Uso en `0x20E6FE0` |
|---|---|---|
| +0x00 f32 | lluvia | `> 0` → estado 1 |
| +0x04 f32 | nieve | `> 0` → estado 2 (gana a la lluvia); si ninguna de las dos → `0x20` (despejado) |
| +0x08 f32 | **temperatura (°C)** | no se usa aquí. En las capturas va de 8,9 a 19,4. En el invitado bajó de 19,4 a 11,5 durante 30 min de noche |
| +0x0C f32 | ¿humedad/nubes? (0,40–0,49) | `>= umbral [0x146CF0F28]` → `|0x10` |
| +0x10 f32 | ¿niebla/lluvia ligera? (0,007–0,22) | `>= umbral [0x146CF0FC8]` → `|8` |
| +0x14 f32 | ¿viento? (2,9–4,5) | `>= umbral [0x146CF0F78]` → `|4` |

Lo que no sabemos: si el simulador es **determinista dado FieldTime + región** (entonces basta
con sincronizar la hora) o si usa estado o aleatoriedad propios. Hay más vías posibles, pero
están sin investigar:
- un **buff que cambia el clima** (`ClientChangeWeatherBuffProcessor`, `ChangeWeatherBuffData`,
  `GameGlobalEffectInfo_Weather`), que se podría inyectar como `ApplyBuffAck`;
- el comando `changeweather`;
- escribir en el componente `weatherSimulator`.

**Recomendación:** primero sincronizar la hora y medir. Con la hora alineada, comparar los
`SyncWeatherStateReq` de los dos PC en una prueba real. Si coinciden, ya está. Si no, abrir una
investigación propia sobre el clima.

## 4. Dormir, esperar y viaje rápido

- **Dormir o esperar** (cama, hoguera u olla; 3/6/12 h; disponible desde el capítulo 3) es el
  "TimeWrap". Tiene enfriamiento (`SetTimeWrapCoolTimeReq`, `CheckCanTimeWrap`), y casi seguro
  el cambio de hora viaja como `SetFieldTimeReq` con `flag = 1` y la clave de la cama. **No
  está capturado**, así que conviene grabar uno. Confianza media.
- **Viaje rápido o cambio de field:** el servidor reinstala la hora al entrar (`0x2BC0A30`).
  Suma `[this+0x80]` segundos, que podría ser el tiempo de viaje, y sale de la tabla de hora por
  nivel. Para el invitado significa que **un cambio de field puede pisar la hora sincronizada**
  si su servidor tiene otra. Inyectar en el servidor (`SetFieldTimeReq`), en vez de solo en el
  cliente, lo evita en gran parte. Aun así hay que resincronizar después de `MoveFieldCompleteAck`
  o `LoadingCompleteReq`. Confianza baja-media.
- **Dormir en el invitado:** su servidor cambiaría la hora y se separaría de la del anfitrión.
  Fase 1: el anfitrión manda, y la siguiente resincronización devuelve al invitado a su hora
  (salto visible). Fase 2: detectar en el invitado el C→S `SetFieldTimeReq` con el sniffer y
  reenviarlo al anfitrión como petición ("el invitado quiere dormir N h"), o bloquearlo.
- **Pausa o menús:** `field+0x17C` congela `GetCurrentFieldTime`. No sabemos si la pausa del
  anfitrión llega a activarlo. Por eso hace falta resincronizar cada cierto tiempo y no basta con
  enviar la hora una vez.
- **Guardado:** si el invitado guarda, su partida se queda con la hora del anfitrión
  (`_fieldTimeRaw`). Es aceptable. Opcional: recordar la hora original y reponerla al
  desconectar.

## 5. Diseño propuesto

### 5.1 Anfitrión: leer su hora (solo lectura)

Cada **5 s** y además **al momento** cuando el sniffer vea en el anfitrión un S→C
`SetFieldTimeAck` (durmió o hubo un evento) o un `MoveFieldCompleteAck`:
1. Leer con protección SEH/`VirtualQuery` los 32 B de `0x146A6D218` (cliente), el desfase
   `[0x146D69438]` y "ahora" (`[0x146C8CD68] + [0x146C8CD70]/1e6`, o llamar a `0x1416B80` por
   firma, que es una entrada de función normal).
2. Calcular `raw_ahora = toRaw(ft) + max(0, ahora + desfase − ft.ancla) · ft.escala`. Las
   funciones `toRaw`/`fromRaw` se reimplementan en el mod: son unas pocas líneas de aritmética
   y se pueden probar con tests unitarios.
3. Enviar `TimeSync`.

### 5.2 Protocolo

Nuevo `EventKind::TimeSync = 13`, no fiable porque es periódico. Los saltos se envían 3 veces
seguidas o como `RelKind` fiable. **Hay que subir `kVersion` de 6 a 7.**

```
u32 seq | u64 raw (ms de juego "raw" en el momento de enviar) | u16 escala | u8 flags
(bit0 = salto/forzar, bit1 = hora fija de nivel, bit2 = pausado)
```
16 B, serializado campo a campo en `protocol.cpp`.

### 5.3 Invitado: aplicar

1. Al recibirlo: `raw_obj = raw + (rtt/2)·escala` (el RTT ya existe con Ping/Pong).
2. Leer su propia hora como en §5.1 y calcular `Δ = raw_obj − raw_local`.
3. Si `|Δ| < 2 min de juego` (≈10 s reales) y no hay `bit0`, no hacer nada. Así se evitan
   inyecciones continuas, eventos de interfaz y recálculo de horarios.
4. Si no, construir `FieldTime ft = fromRaw(raw_obj)` con `ft.escala = escala`,
   `ft.ancla = ahora_local + desfase_local` (base del servidor del invitado) y
   `ft.díaSemana = día % 7`.
5. Inyectar **`SetFieldTimeReq`**: `sniffer_inject_c2s(u16 2125 | 0 | u16 36 | ft(31) | u8 0 |
   u32 0)`. Esto actualiza el servidor del invitado, y su servidor envía `SetFieldTimeAck` a su
   cliente.
6. Comprobar el `SetFieldTimeAck` en el sniffer del invitado. Si no llega en 1 s: registrar el
   fallo y, como plan B, `puppet_inject_s2c(2816, u32 0 | ft)`.
7. Limitar a una inyección cada 5 s, salvo saltos.

**Sin hooks y sin escrituras en memoria:** solo lecturas protegidas e inyección de paquetes.
Todo se hace en el hilo worker; la inyección C→S ya sale desde dentro de un `PostPacket` real.
**Antes de la primera prueba, copia de seguridad de las partidas** (la hora se guarda).

## 6. Plan de pruebas

**Fase 0 — sonda de solo lectura, un PC, sin peer.** En `cdmp.log`, cada 5 s:
- las dos globales decodificadas (día, h:m:s, escala y ancla);
- "ahora" + desfase;
- la hora calculada.

Hay que confirmar cinco cosas:
- (a) `0x146A6D218` es la hora del cliente y coincide con la hora del mapa o la interfaz
  (captura con `CopyFromScreen`);
- (b) la escala es 12;
- (c) avanza ≈ 12 s de juego por segundo real de día y 24 de noche;
- (d) en una mazmorra con hora fija la global sigue avanzando;
- (e) qué pasa en pausa/menú.

**Fase 1 — inyección en el propio juego, sin peer.** Partidas en `backups/`. Una tecla de
desarrollo inyecta `SetFieldTimeReq` con la hora actual + 6 h (`flag = 0`). Comprobar:
- el S→C `SetFieldTimeAck` en `cdmp_packets` (35 B, los mismos 31 B que enviamos);
- el cambio visual (día → noche);
- que la sonda de la fase 0 lee la hora nueva en las **dos** globales;
- que no hay efectos raros: teletransporte, fatiga, tiendas.

Después, viaje rápido y comprobar que la hora se mantiene. **Grabar también un "dormir 3 h"
real** para ver el `SetFieldTimeReq` del juego (flag y clave) y si el viaje rápido cambia la
hora.

**Fase 2 — loopback con `cdmp_peer`.** Añadir a `cdmp_peer` un modo que envíe `TimeSync` con
una hora fija (por ejemplo 22:00, con `bit0`) y luego cada 5 s. El juego es el invitado.
Comprobar:
- que se hace de noche y se mantiene;
- que no reinyecta en cada paquete (umbral);
- que tras un cambio de field vuelve a cuadrar.

Hacer también la prueba al revés: el juego es el anfitrión y `cdmp_peer` registra los
`TimeSync` recibidos. Comparar con la sonda.

**Fase 3 — dos PC (Shadow como invitado).** Cargar las partidas con horas distintas. En 10 s la
del invitado debe saltar a la del anfitrión. Probar:
- que el anfitrión duerma 6 h: el invitado le sigue al momento;
- que el invitado duerma: en la fase 1 debe volver a la hora del anfitrión.

Comparar los `SyncWeatherStateReq` de los dos lados para decidir si hace falta trabajo aparte
para el clima (§3). Por eso hay que **grabar `cdmp_packets` también en el anfitrión**: hoy solo
existe la del Shadow.

## 7. Resumen de confianza

| Hallazgo | Confianza |
|---|---|
| Disposición de `FieldTime` (día/h/m/s/ms/díaSemana/escala/ancla), `toRaw`/`fromRaw`, noche al doble de velocidad | Alta |
| Formatos de `SetFieldTimeReq` (36 B) y `SetFieldTimeAck` (35 B) | Alta |
| `SetFieldTimeReq` con flag 0 → el servidor fija la hora y envía el Ack a su cliente | Media-alta |
| Globales `0x146A6D218` (cliente) / `0x146A6D238` (servidor) | Media-alta (falta verificar en vivo cuál es cuál) |
| Reloj `0x1416B80`, desfase `[0x146D69438]` de `ServerTimeInfoAck` | Alta |
| `ServerTimeInfoAck` no lleva la hora del juego | Alta |
| No hay sincronización periódica de la hora (0 paquetes en todas las capturas) | Alta |
| Dormir = `SetFieldTimeReq` con flag 1 y clave de cama | Media (sin captura) |
| El viaje rápido o cambio de field reinstala la hora del servidor | Baja-media |
| Clima simulado en el cliente; disposición de `SyncWeatherStateReq` | Media (lluvia/nieve/temperatura), baja (resto) |
| Ningún CheatReq de hora o clima; la referencia no sincroniza la hora | Alta |
| Tablas CE públicas con hora del día | No se encontraron (solo inventario, stats y temperatura del jugador) |

## Herramientas usadas

- `build/research/horse2/xtrace.py <rva>` (con `XV=1`) para el código reubicado o mutado.
- `cddis.py`, `rawref.py <rva…>` (referencias a `0x6A6D218/238`, `0x6D69438`) y
  `exe_strings_all.txt`.
- Capturas: `grep` de los opcodes 2125/2816/2560/2188/2529 en `D:\cdmp-grabaciones\*.log`,
  `bin64\cdmp_packets_*.log` y `<onedrive>\cdmp-shadow\logs\20261005\`.

Fuentes externas (comunidad, solo para cuadrar la duración del día y cómo saltar tiempo):
thegameswiki.com "weather-and-day-night-cycle" y "how-to-skip-time-and-sleep",
games.gg "crimson-desert-how-to-skip-time".
