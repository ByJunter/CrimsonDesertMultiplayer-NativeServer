# Clima del invitado igual al del anfitrión: simulador, reinicio y diseño

Build **1.0.0.2976**, PE `0x6AB28F00`, base `0x140000000`. Las direcciones son RVA salvo que pongan `0x14…`.
Investigación de solo lectura (2026-10-12): Ghidra (servidor de `D:\RE`), capturas `p8-sesion16..23`, scripts
`%TEMP%\noche-clima\align*.txt` del informe anterior y escaneos con capstone sobre la copia de análisis. No se ha
editado código, no se ha lanzado el juego y no se ha parado Ghidra.

Etiquetas: **[V]** comprobado (descompilado, bytes, capturas); **[E]** deducido del código; **[H]** hipótesis.
H = anfitrión, G = invitado (Shadow). "X" = el sistema de clima de la escena (§1).

Complementa `clima-hora.md` (§2 "Clima", §5.3, §9.6) y corrige una afirmación suya (§4.2).

---

## 0. Dictamen

1. **[V] El clima del cliente es una cadena de cuatro piezas, todas locales:**
   - componentes de clima en escena: `WeatherComponent` con volumen, colocados en el nivel o creados por
     `GlobalEffect`;
   - cuatro **componentes de script simuladores** que escriben en el `WeatherComponent` de su propio objeto:
     clima, nubes, hora y superficie;
   - un **mezclador** (`0x3DC0340`) que combina todos los `WeatherComponent` registrados en un "holder" propio del
     sistema de clima X;
   - los **consumidores**, que leen solo ese holder de X: efectos de lluvia, el `2560`, las condiciones de los
     `GlobalEffect` y el render.
2. **[V] El simulador no es determinista entre PCs.** Las duraciones de las fases de lluvia (5–10 s, 20–30 s y
   180–250 s), la mezcla de capas de nubes (180–300 s, umbrales 80/60 %) y la semilla nocturna salen del LCG
   `0x343FD/0x269EC3` **por hilo** (`TLS+0x4AD8`). Ese LCG se siembra con `GetCurrentThreadId()` y lo consume todo
   el código de ese hilo. Además, el desplazamiento de las nubes (`X+0x124/+0x128`) se integra desde que se creó X.
   **Alinear el reinicio o la semilla no puede dar el mismo cielo.**
3. **[V/E] El "reinicio" de G no es un mensaje.** El simulador vuelve a inicializarse (`0xCDC780`) cuando su handle
   débil al `WeatherComponent` muere, es decir, cuando el objeto de clima de la escena se descarga y se vuelve a
   cargar.
   - En S16, G recibe la ola de descarga del anfitrión (`208.193–208.217`).
   - Envía `GamePlayTriggerInOutReq` (`208.199`, `208.299`) y `RegionInOutReq` (`208.355`).
   - Un frame cambia el viento (`2560` en `208.341`) y al frame siguiente se reinicia todo: lluvia 0,5→0,
     `[3]` 0,645→0,4002 y `[4]` 0,583→0,007 (`2560` en `208.373`).
4. **[H] La "regla de 60 s" no existe dentro de los simuladores.** Sus temporizadores dependen de `dt`. En S16 los
   dos hitos de G quedan **exactamente a un minuto de servidor** de los de H, y con la misma fase dentro del minuto:
   `210349/270339` = 180000/240000 + 30,3 s, y `245976/305985` = 240000/300000 + 6,0 s. Apunta a una entrada
   alineada con `tick % 60000`, que no se ha localizado. Se mide en §6.
5. **[V] No existe un "aplicar clima natural" nativo ni un mensaje S→C de clima.** El servidor no manda nada en la
   carga: `FogDataAck` es la niebla del mapa y `GlobalGameEventUpdateAck` va vacío. Las vías nativas que hay son
   **anulaciones** de entradas (§4): la pila de triggers `GameWeather`, el controlador del buff y las capas
   `GlobalEffect`. **Corrección [V]:** el buff no pasa una "clave". `ctrl->vf8` recibe un **float**
   (`VMOVSS XMM1,[rdi+0x90]` en `0x931E7C`): la precipitación.
6. **[V] Sí existe un modo nativo de "valores de clima externos": el Weather Viewer, el byte `X+0x31`.** Con él a 1:
   - los cuatro simuladores salen en su primera instrucción útil;
   - el mezclador no recorre los `WeatherComponent`;
   - `0x382A1A0` deriva lluvia y nieve del mando `precipitation`;
   - los getters devuelven los valores crudos.

   La descripción interna lo confirma: "aplica al momento los valores del Weather Viewer; se ignoran los scripts y
   los `WeatherComponent` del nivel".
7. **Diseño (§5):**
   - **H** lee el vector de mandos final de su holder de X (de 4 a 10 Hz, solo lectura).
   - Lo envía por el túnel como **estado de control 12** (sin cambio de protocolo del mod).
   - **G**, desde un gancho en la **entrada verificada `0x3DC4730`** (la actualización de X, en su hilo), pone su X
     en modo visor y escribe los mandos de H antes de llamar a la función original.
   - Si no hay instantánea fresca, quita el modo visor y G vuelve a su clima nativo en ese mismo frame.

   G ve el mismo clima que H con la maquinaria del juego: viewer, derivación de lluvia y nieve, muestreo local del
   clima e integración de las nubes.

---

## 1. Mapa del clima en el cliente

| Pieza | Dónde | Qué es |
|---|---|---|
| **X** (sistema de clima de la escena) | `X = [[[0x146C8CEC8]+0x60]+0xF68]` (camino de los simuladores; `0x6DA0A0` es `mov rax,[rcx+0x60]; ret` = vf60 del módulo `0x6C8CEC8`) [V]. 9ADF20 lo lee como `[[[0x146D69198]+0x38]+0x38]` [V] | `ReflectObjectExtension`, vtable `0x145D51268` y ctor `0x3DBB060`, de unos 4 MB (`X+0x4001E8`). Lo actualiza `0x3DC4730(X, dt)`, llamado desde `0x3B10737` con `RCX=[[rdi+0x90]+0xF68]` [V]. Que los dos caminos den el mismo X es [E] (misma familia de getters, mismo `+0x60`); lo mide P0-a |
| Holder de X | `[X+0x60]` (refcontado; el objeto empieza en `holder-0x28`, ctor `0x38294B0`), creado en `0x3DBB5B0` [V] | `A=[holder+0x18]` (objeto de `0x1D8`), `B=[holder+0x20]` (`0x158`), `C=[holder+0x28]`. X se registra como *listener* de sus bloques (vf288) |
| Flag visor | **byte `X+0x31`** [V] | Lo leen solo `0xCDCA40`, `0xCD8E40`, `0xCDFA40` y `0xCE0490` (vía `+0xF68`, escaneo completo de `.rsrc`) y, dentro de X, `0x3DBCA30`, `0x3DC0340`, `0x3DC4730`, `0x3DC6CF0` y los getters `0x3DC3A10/3AC0/3B70` [V]. Nadie en `.rsrc` lo escribe por `+0xF68` [V escaneo] |
| `WeatherComponent` (WC) | ctor `.xtls 0x152CC03F0` (meta `0x41FF80`, tamaño `0xB0`, vtable `0x145C721E8`) [V] | `+0x88` prioridad, `+0x8C` distancia de mezcla (100), `+0x90` peso (1,0), `+0x98` holder propio (mismos bloques A/B/C). Sus métodos están en `.xtls` (no se enganchan) |
| Mezclador | `0x3DC0340(X)`, llamado por `0x3DC4730` [V] | Si `X+0x31==0`, recorre la lista de X (`X+0x80`, n `X+0x88`). Por cada objeto busca el `TriggerVolumeComponent` (meta `0x41B330`) y pondera por posición de cámara (`X+0xE8`). El resultado va al holder de X |
| Simuladores | 4 componentes de script en el **mismo objeto** que su WC (`0xA7A5950` recorre `obj+0x210`) [V] | §2 |
| Capas `GlobalEffect` | gestor `[[0x146D691E0]+0x70]+0x40` (= `EnvMgr+0x40`), 128 ranuras de `0x58` [V] | Cada capa con datos de clima crea un objeto de escena con WC rellenado desde `GameGlobalEffectInfo_Weather` (`0x41D5F0` → `0x41DB60`) [V] |
| `EnvMgr` | `[GameClient+0x38]` = `[0x146D691E0]+0x70`; se actualiza en `0xA8ACD0`, desde el bucle principal `0xAD291C/0xAD2CF2` [V] | Pila de triggers `GameWeather` (`+0x78/+0x80`), mezcla desde/hacia (`+0x88/+0x90`, progreso `+0x98..+0xA8`) → `ctrl->vf8/10/18/20/28` (§4) |
| Consumidores | `0x9ADF20` (2560), `WeatherEffectScriptComponent 0xCDB5F0` (partículas), condiciones `GlobalEffect` (`0x41BA00`) [V] | Todos leen el holder de X o sus getters |

### 1.1 Esquema de los mandos (`GameGlobalEffectInfo_Weather` → bloques) [V]

Mapeo sacado de `0x41DB60`. El valor `FLT_MAX` significa "no se pone". Hay un byte de "fijado" por campo en
`A+0x172..+0x17C` y `B+0xF6..`. Los nombres siguen el orden de lectura de `0x1504FC0`; por eso la correspondencia
nombre↔índice es **[E]**, apoyada en el uso.

| p | Campo | Bloque | Comprobación de uso |
|---|---|---|---|
| 0 | `_precipitation` | A+0x12C | Objetivo de lluvia/nieve (`0xCDD320`); en modo visor, `0x382A1A0` lo copia a lluvia o nieve |
| 1 | `_cloudiness` | A+0x130 | Objetivo y estado actual de nubes (`0xCD8E40`, `0xCDCA40`) |
| 2 | `_humidity` | A+0x134 | Objetivo y estado actual de humedad |
| 3 | `_windSpeed` | A+0x138 | Viento = `A+0x1A4 + A+0x138` (`0x3DC3B70`); integra las nubes |
| 4–8 | `_puddleRate`, `_snowPuddleRate`, `_snowAmount`, `_snowRate`, `_iceRatio` | A+0x13C, +0x144, +0x148, +0x14C, +0x150 | Los escribe el simulador de superficie `0xCDFA40` |
| 9–10 | `_windDegree`, `_altitudeWindRatio` | A+0x154, +0x158 | |
| 11–15 | `_sunDirX/Y`, `_moonSizeAngle`, `_moonDirX/Y` | B+0x64..+0x74 | |
| 16–25 | `_mie*`, `_ozoneRatio`, `_directionalLightLuminanceScale`, `_heightFog*` | B+0x84..+0xA4 (vec3 en B+0x78..+0x80) | Los simuladores de hora y de clima escriben B+0x84/88/8C/94/98/9C/A0 |
| 27 | `_cloudBaseDensity` | B+0xA8 | **= `2560[3]`** (nubes·0,3+0,25) |
| 30 | `_cloudScrollMultiplier` | B+0xB4 | Integra `X+0x124/+0x128` |
| 31 | `_cloudAltitude` | B+0xB8 | Atenúa la lluvia por altura en los getters |
| 28–43 | Resto de nubes (`Contrast`, `Alpha`, `Thickness`, `Near`, `FadeRange`, `Detail*`, `Cirrus*`, `Flow`, **`_cloudSeed` B+0xE8**) | B+0xAC..+0xE8 | Simuladores de nubes y de clima: B+0xC4, D4–E0 |
| 44–46 | `underWater*` / `extinction` | C+0x1B8, +0x1E0, +0x1B0 | |

Salidas que no están en el esquema:
- A+0x160, +0x164, +0x168 (nieve), +0x16C (lluvia);
- A+0x190/+0x194: [E] vector de dirección del viento (los usa `0x3DC4730`);
- A+0x198..+0x1A8: muestra del **ClimateTexture en la cámara local** que hace `0x3DC0FE0` cada frame. Es local y
  no se copia;
- `X+0x124/+0x128`: acumuladores del desplazamiento de las nubes, `+= windSpeed · A+0x190/0x194 · dt · B+0xB4`.

El `2560` de 6 floats es `[lluvia, nieve, temperatura, B+0xA8, max(B+0x9C, min(B+0x88,100)/100), viento]` [V].

---

## 2. El simulador (pregunta 1)

### 2.1 Clases y vtables [V]

| Componente | vtable | vf1 clon | vf7 update | Otras |
|---|---|---|---|---|
| `WeatherSimulatorScriptComponent` | `0x14564D0E0` | `0xCDC590` | **`0xCDCA40`** | vf9 `0xCD8C90` (desacopla: `WC+0xA9=0`), vf12 **`0xCDC780`** (init/reinicio) |
| `WeatherCloudSimulatorScriptComponent` | `0x14564CB68` | `0xCD8A30` | `0xCD8E40` | vf12 `0xCD8CB0` |
| `WeatherSurfaceSimulatorScriptComponent` | `0x14564D1A8` | `0xCDF7E0` | `0xCDFA40` | vf12 `0xCDFA20` |
| `weatherTimeSimulatorScriptComponent` | `0x14564D250` | `0xCE0390` | `0xCE0490` | — |
| `WeatherEffectScriptComponent` (partículas) | `0x14564D038` | `0xCD9930` | vf6 `0xCDACD0`, vf7 `0xCDB5F0` | Lee los getters de X; **no** mira `X+0x31` |
| `worldWeatherEffectScriptComponent` | `0x14564D530` | `0xCE1820` | vf6 `0xCE1A60`, vf7 `0xCE1B60` | |

(Las vtables de la tabla van en VA.) Son `.pacpp` compilados a C++:
`WeatherSimulator.pacpp`, registrado en `0xC28EA0`.

### 2.2 Estado de `WeatherSimulatorScriptComponent` (0xB0 B) [V salvo que se indique]

| Off. | Contenido |
|---|---|
| +0x10 | Objeto dueño. Su `[+0x60]+8` es el objeto de escena donde se busca el WC |
| +0x18 | Handle débil al WC (`[h+8]` = WC+0x28; un handle muerto da 0) |
| +0x20, +0x30 | Listas de **transiciones**. Fotogramas clave de 0x18 B: `{float* canal, objetivo, tasa}`; se añaden con `.xtls 0x154A88A30` y avanzan con `0x3DC5A00(lista, dt)` |
| +0x40 / +0x44 | Último objetivo de humedad (A+0x134) / valor actual |
| +0x48 / +0x4C | Último objetivo de nubes (A+0x130) / valor actual |
| +0x50, +0x54 | Canal de tormenta o intensidad (lo usan `0xCDF070` y `B+0xA8`) [E] |
| +0x58 | Último objetivo de precipitación (A+0x12C) |
| +0x5C/+0x60, +0x64/+0x68 | Lluvia y nieve (inicial/actual) |
| +0x6C, +0x70 | Factor noche (22–06 h) e intensidad aleatoria nocturna |
| +0x74 | Número del LCG (se resiembra en el reinicio y a las 22:00) |
| +0x78 | Última hora entera |
| +0x7C..+0x98 | Canales de capas de nubes (→ B+0xD8/DC/E0/D4) |
| +0x9C..+0xA8 | Canales extra (→ B+0xC4 y otros) |
| +0xAC / +0xAD | Cambio lluvia↔nieve / `A+0x19C ≤ 0` (hiela) |

### 2.3 Paso de actualización `0xCDCA40(comp, dt)` [V]

1. `if ([[[0x146C8CEC8]+0x60]+0xF68]+0x31) return;`, que es el modo visor.
2. Si el handle está muerto, busca el WC en el objeto (`0xA7A5950`). **Siempre que pasa por aquí llama a vf12 =
   `0xCDC780`.**
3. **Precipitación:** `0xCDD320` compara A+0x12C con +0x58. Si cambia, o si hay cambio lluvia↔nieve, mete una rampa
   de ±0,03 en lluvia o nieve y llama a `0xCDF070`. Esa función monta una **secuencia con duraciones aleatorias**:
   `rand(5,10)`, `rand(20,30)` y `rand(180,250)` s (`0x1379340`).
4. **Humedad:** si A+0x134 ≠ +0x40 y ≠ +0x44, rampa de ±0,001.
5. **Nubes:** si A+0x130 cambia, llama a `0xCDF110`, que elige capas de nubes con tiradas `%100 < 80/60` y
   duraciones de `rand(180,300)` s.
6. **Hora del día:** `0xCDD080` lee `world.vf+0x100` (horas), calcula el factor noche y resiembra a las 22:00.
7. `0x3DC5A00(+0x20, dt)` avanza las transiciones y se escriben las salidas en el WC: A+0x130/134/160/164/168/16C
   y B+0x84/88/8C/94/98/9C/A0/A8/C4/D4..E0.

Los simuladores de nubes, hora y superficie siguen el mismo patrón. El de hora (`0xCE0490`) escribe
B+0x84/88/8C/94 a partir de la hora y de A+0x134+A+0x1A0. El de superficie (`0xCDFA40`) escribe
A+0x13C/144/148/14C/150/160/164.

**Patrón de "propiedad animada" [V]:** un escritor externo pone un objetivo en el campo del WC. El simulador lo
detecta porque difiere a la vez de su último objetivo y de su valor actual, monta la transición y escribe de vuelta
el valor actual en ese mismo campo.

### 2.4 Entradas

- **Los objetivos del WC** (precipitación, nubes, humedad, viento, etc.) vienen de los datos [V]:
  - del WC colocado en el nivel, con volumen;
  - de las capas `GlobalEffect` (secuenciador, jefes, Abismo: `0x5C57F0`, `0x5C8B30`, `0x5D03E0`, `0x5FC580`,
    `0xC5CFE0`, `0xCC6F60` con `"Weather_Abyss_Cloud_0001"`);
  - del mezclador, ponderado por la posición de la cámara.

  Mismo sitio y mismos datos dan los mismos objetivos. En S17, S21 y S22 `B+0xA8` en régimen estable coincide en H
  y G (0,2500 / 0,4450 / 0,4600) y cambia de una sesión a otra [V capturas].
- **`dt`**, la hora del día del mundo (`world.vf+0x100`) y la temperatura y el viento del ClimateTexture en la
  cámara [V].
- **El LCG del hilo** [V] (`0x1378F10`: `TLS+0x4AD8`, sembrado con `GetCurrentThreadId()*0x343FD+0x269EC3`).
- **Ni el tick del servidor ni la semilla del mundo** entran directamente [E]. El tick compartido solo llega a
  través de la hora del campo (2144/2816) y de las condiciones de `GlobalEffect` (`0x41BA00` evalúa
  `CompareTargetParameter_GlobalEffect` con lluvia, nieve, viento, temperatura, `B+0xA8`, segundos del día y
  humedad).

### 2.5 Qué lo reinicia

- **[V]** El reinicio es `0xCDC780`:
  - resiembra +0x74;
  - copia los valores **actuales** del WC a los pares de canales: humedad, nubes, lluvia, nieve y capas B;
  - vacía las dos listas (`0x7EF060`);
  - marca `WC+0xA9=1`.

  Solo se ejecuta cuando el handle +0x18 está muerto: en el primer update tras crear el objeto, o cuando se destruye
  el WC. Un objeto recién cargado trae los valores por defecto de sus datos: lluvia 0, nubes 0,5 (`[3]=0,4002`),
  niebla 0 (`[4]=0,007`). Son exactamente los valores de "reinicio" que se ven en los 2560 de H y de G después de
  cada carga [V].
- **[E]** Cuando H viaja, el servidor descarga la vista de H. G comparte el observador (pendiente nº 4) y recibe la
  misma ola. Su cliente descarga los objetos de nivel o sincronizados de esa zona, entre ellos los volúmenes de clima
  (WC + `TriggerVolumeComponent` + simuladores) en los que estaba. La secuencia de S16:
  - frame N: cambia el peso de la mezcla (solo cambia el viento: `[5]` 6,77→3,156);
  - frame N+1: el objeto se recrea con los valores por defecto y sus simuladores se inicializan;
  - todo eso ocurre a unos 150 ms de la ola, intercalado con los `GamePlayTriggerInOutReq` y `RegionInOutReq` de G
    [V marcas de tiempo].
- **[H]** Qué objeto concreto es. Lo resuelve P0-c (§6).

### 2.6 La "regla de 60 s"

- **[V]** Los simuladores no tienen un paso fijo: todo depende de `dt`, de las tasas (±0,001, ±0,01, ±0,03 por
  unidad de `dt` [E]), de duraciones aleatorias y de la hora del día (un evento a la hora entera = 300 s reales a
  ×12). Las transiciones de los triggers `GameWeather` duran 10 s por defecto (`GlobalVariableFloat` en `0x6CC7E08`,
  hash `0x894A03C6`).
- **[V]** Los datos de S16, en el eje del tick del servidor:

  | Hito | H | G | `tick % 60000` |
  |---|---|---|---|
  | `[3]≥0,45` y `[4]≥0,15` | 210349 | 270339 | 30349 / 30339 |
  | lluvia > 0 | 245976 | 305985 | 5976 / 5985 |
  | `[4]≥0,15` tras el reinicio común | 495785 | 495824 | 15785 / 15824 |

  La fase dentro del minuto coincide a ±10 ms y G va un minuto entero por detrás. Después del reinicio común
  coinciden en el mismo minuto.
- **[H]** Hay una entrada que cambia alineada con `tick % 60000`, porque los dos reinicios previos (las cargas de H y
  de G, separadas unos 130 s) no explican un desfase exacto de 60,0 s con rampas deterministas desde el reinicio.
  Candidatos:
  - una condición de `GlobalEffect` o de trigger evaluada por minuto de servidor;
  - un objeto de nivel cuyo estado se publica con un temporizador de 60 s (sincronizado).

  No se encontró en los simuladores ni en `0x41BA00`/`0xA8ACD0`. **Con el diseño de §5 deja de importar**, porque G
  no simula; aun así se mide en P0-d.

---

## 3. ¿Es determinista? (pregunta 2)

- **[V] No.** Lo impiden:
  - el LCG por hilo, compartido con todo el código del hilo (IA, partículas…);
  - las duraciones aleatorias de las fases de lluvia y de las capas de nubes;
  - los acumuladores `X+0x124/+0x128`, que se integran desde que se creó X;
  - el muestreo de clima de la cámara local.
- **[V] Lo que sí sale igual** son los objetivos (mismo lugar y mismos datos) y las rampas deterministas que van
  hacia ellos. Por eso, tras el reinicio común de S16, los cruces de umbral coinciden a 0,04 s, y en S21/S22 a
  0,45/0,65 s.
- **Alinear los reinicios** (arreglando W2 / pendiente nº 4) reduciría las diferencias **gruesas**, pero no daría el
  mismo cielo. **Alinear la semilla no es posible:** no hay una llamada nativa para fijar el estado del LCG de un
  hilo sin afectar al resto del juego, y el orden de las tiradas depende de todo el frame.

---

## 4. Vías nativas de aplicación y lectura del estado de H (pregunta 3)

| Vía | Entrada | Argumentos (verificados) | Qué consigue | Por qué no basta |
|---|---|---|---|---|
| **Modo visor** | byte `X+0x31` | 1 = valores externos | [V] Para los 4 simuladores y el mezclador. En `0x3DC4730`, `0x382A1A0(holder-0x28)` pone lluvia = A+0x12C, o nieve si la temperatura muestreada A+0x198 ≤ 0, además de A+0x160 y `A+0x164=min(10p,1)`. Los getters devuelven valores crudos | **Es la base del diseño** |
| Pila de triggers `GameWeather` | `0xA8B2D0(EnvMgr, handler, &id16, enter)` (`EnvMgr=[[0x146D691E0]+0x70]`) | Entrada de 0x28 B: id de 16 B, precip, humedad, nubes, dirección del viento (grados → rad), velocidad del viento. Propiedades del handler: `+0x28 _precipitation`, `+0x2C _humidity`, `+0x30 _cloudiness`, `+0x34 _windDirectionDegree`, `+0x38 _windSpeed` [V] | `0xA8BC40` mezcla de la entrada anterior a la de arriba y `0xA8ACD0` llama a `ctrl->vf8(precip)`, `vf10(nubes)`, `vf18(humedad)`, `vf28(dir rad)` y `vf20(velocidad)` [V] | Solo cambia **objetivos**; los simuladores de G siguen eligiendo sus fases al azar |
| Buff `ChangeWeather` | `0x931DC0` | `[[0x146D69190]+0xE0]->vf168()->vf8(float [data+0x90])`; libera con `vf8(0.0)` [V] | Solo la precipitación | Igual que la anterior, y además un solo canal |
| Capa `GlobalEffect` (escenas, jefes) | `0x41B6D0(slots=[[0x146D691E0]+0x70]+0x40, req)` → índice; quitar: `.xtls 0x1488732B0(slots, idx)`; peso: `slots[idx*0x58+0x1C]` (lo hace `0xC5D170`) | `req`: `u16 GameGlobalEffectKey`, `u32` prioridad, `u64` objeto trigger o 0, `u64` 0, `f32[3]` -1, `f32` blendRatio, `f32` blendDistance, `u8` 1 [V, `0xC5CFE0`]. Nombre → clave con `0x217FF70` | Crea un objeto de escena con un WC rellenado desde la tabla de datos | Son ajustes predefinidos; no representan un estado natural arbitrario |
| Mensaje del servidor | — | — | [V] No hay ninguno S→C de clima. El `2560` es C→S y solo escribe `field+0x178` | — |

### 4.1 Leer el clima de H [V]

No hay una "clave de clima natural". El estado es el **vector de mandos del holder de X**, siempre válido mientras
exista X:

- `S=[[0x146C8CEC8]+0x60]`, `X=[S+0xF68]` (vtable `0x145D51268`), `holder=[X+0x60]`;
- `A=[holder+0x18]`, `B=[holder+0x20]`, `C=[holder+0x28]`;
- los campos de §1.1, más A+0x160..+0x16C, A+0x190/+0x194 y `X+0x124/+0x128`.

Para diagnóstico:
- las capas activas: en cada ranura de `[EnvMgr+0x40]` con `s16 != -1`, `+0x18` prioridad y `+0x1C` peso;
- la cima de la pila de triggers: `[EnvMgr+0x90]`, de 0x28 B;
- `X+0x88`, el número de WC registrados.

### 4.2 Corrección a `clima-hora.md`

`clima-hora.md` §2 y §9.6 dicen `ctrl->vf8(claveClima)`. Es **un float**, la precipitación (`0x931E7C` VMOVSS).

---

## 5. Diseño: espejo del entorno en modo visor (pregunta 4)

### 5.1 Idea

- H no cambia nada.
- G deja de simular y de mezclar su propio clima: usa el modo visor nativo del motor y recibe de H los mandos
  finales.

Todo lo que viene después en G es nativo:
- derivación de lluvia y nieve con su temperatura local (`0x382A1A0`);
- muestreo del ClimateTexture en su cámara (`0x3DC0FE0`);
- integración de las nubes (`0x3DC4730`);
- partículas de lluvia (`0xCDB5F0` lee los getters);
- condiciones y efectos de `GlobalEffect`, incluidos los relámpagos;
- el `2560` de G, que sigue bloqueado por W1.

### 5.2 Anfitrión: muestreo (solo lectura, sin ganchos)

- Hilo worker del mod, de 4 a 10 Hz, con lecturas protegidas (SEH/VirtualQuery) de la cadena de §4.1. Se exige
  `[X]==0x145D51268` antes y después de copiar, y todos los valores finitos y en rango.
- **Vector v1 (unos 60 floats, unos 250 B):**
  - A: +0x12C (ver la nota), +0x130, +0x134, +0x138, +0x13C, +0x144..+0x158, +0x160, +0x164, +0x168, +0x16C,
    +0x190, +0x194;
  - B: +0x64..+0xE8 (34 floats, más el vec3 de B+0x78..+0x80);
  - C: +0x178, +0x1B0, +0x1B8, +0x1E0;
  - X: +0x124, +0x128;
  - el tick de servidor de la muestra (dominio del 2188) y la posición del personaje de H.

  **Nota:** en modo visor, A+0x12C es lo que manda la lluvia. H envía como mando
  `precip = max(A+0x16C, A+0x168)`, su lluvia o nieve **actual**, no su objetivo.
- **No se envían** A+0x198..+0x1A8 (clima de la cámara de H) ni `X+0x4001E8` (factor de altura de la cámara).

### 5.3 Transporte

- **Estado de control 12 `ENV_MIRROR`** dentro de los lotes `ToGuestClient`: opcode 1, slot -1, payload
  `u8 12 | u8 ver=1 | u32 seq | u64 tick | u16 máscara | f32[...]`. Es el mismo mecanismo que los estados 4 y 5.
  Un invitado antiguo lo ignora y el protocolo del mod no cambia, así que **`kVersion` sigue en 8**.
- Solo se envía si G anuncia el bit de capacidad **`0x04` (`kCapsEnvMirror`)** en su paquete `kCapsOpcode`.
- Si el vector no cambia en más de 1e-4, se manda uno cada segundo para que G sepa que sigue fresco.

### 5.4 Invitado: aplicación en el hilo de X

- **Gancho inline (safetyhook) en la entrada de `0x3DC4730`.** Es una función con `.pdata`, llamada solo desde
  `0x3B10737` [V], con un prólogo limpio sin RIP-relativo:
  `48 89 5C 24 10 | 55 | 56 | 57 | 48 83 EC 50 | C5 F8 29 74 24 40` [V]. Se instala solo en el rol de invitado con
  `env_mirror=1`, y de forma diferida (tras `WorldSystem` y con el mundo cargado), como el overlay.
- **Pre-llamada**, con `RCX = X`, que valida el propio motor:
  1. Si el espejo está activo (§5.5) y hay una instantánea de menos de 3 s:
     - si `X+0x31==0`, se pone a 1 y se apunta "lo puse yo";
     - se escriben los mandos interpolados en `[X+0x60]` A/B/C. Interpolación: dos muestras en el eje del tick,
       con 300 ms de retraso y el reloj ya alineado por T1/T2;
     - `X+0x124/+0x128` se fija como `acc_H + pendiente·(ahora−tick_H)`, solo si el error pasa de un umbral;
     - si se quiere imitar el "no llueve por encima de las nubes" de H, la lluvia se atenúa con la altura de la
       cámara de G y B+0xB8 antes de escribir A+0x12C (opcional).
  2. Si no está activo o la instantánea está caducada y "lo puse yo": `X+0x31=0`.
  3. Se llama a la función original.
- **Coste por frame:** unos 60 stores y un seqlock, sin reservar memoria ni escribir en el log dentro del gancho.
- Cualquier excepción hace `__except`, desactiva el espejo, deja `X+0x31=0` y apunta un contador.
- **Sin espejo, G vuelve a su clima en ese mismo frame.** El mezclador vuelve a ejecutarse, y los simuladores de G
  siguen desde su estado congelado: sus WC no se han tocado.

### 5.5 Puertas (lógica pura, con tests)

El espejo solo está activo si se cumplen todas:
- sesión 2 conectada y G en fase `Stable` (2762 recibido y sin movimiento pedido);
- G no está en carga;
- `|pos_G − pos_H| < env_mirror_radius` (500 m por defecto). Fuera de ese radio, G usa su propio clima local;
- G no está reproduciendo una secuencia propia (`guest_seq_play`), que podría traer su propia capa `GlobalEffect`
  y el modo visor la anularía [H].

Claves en `[session2]`:
- `env_mirror=0|probe|1`: en `probe`, H envía y G decide y registra la línea "WOULD apply Δ…" sin escribir;
- `env_mirror_radius`.

### 5.6 Hilos y vida de los objetos

- **H:** solo lee desde el worker. Leer memoria liberada da basura, que se filtra; no se escribe nada en H.
- **G:** escribe en el hilo que actualiza X y justo antes de que lo haga el motor, como haría el propio Weather
  Viewer. El holder de X vive lo mismo que X (lo crea `0x3DBB5B0` y no cuelga de los objetos de nivel), así que las
  olas de descarga no lo invalidan [E, P0-c]. No se guarda ningún puntero entre llamadas.

### 5.7 Riesgos

- **Efectos secundarios del modo visor** [V escaneo]: se paran los 4 simuladores y el mezclador, y los getters dejan
  de atenuar por altura. Además:
  - se ignoran las capas `GlobalEffect` y los volúmenes propios de G (autoridad de H, intencionado);
  - [H] alguna ruta de `.xtls` podría leer `X+0x31`. Antes de llevarlo a dos PC se valida con la prueba de un PC
    (§6, P1).
- **Campos que no son float** dentro de B+0x64..+0xE8: P0-b registra los dwords en bruto para comprobarlo antes de
  escribir.
- **Que H y G estén en zonas distintas**: se cubre con la puerta de radio.
- Los relámpagos y los efectos aleatorios de `GlobalEffect` siguen siendo por cliente; solo coinciden en "hay
  tormenta".
- **Escritura en memoria del juego:** antes de probar se copian las partidas a `backups/` (CLAUDE.md). El gancho
  está en `.rsrc` y no en `.xtls`, en la entrada de una función.
- **W1 se mantiene.** **W2** (los reinicios de G cuando viaja H) deja de verse, pero sigue gastando trabajo; continúa
  dentro del pendiente nº 4.

### 5.8 Alternativas descartadas

- **Sincronizar la semilla o la fase:** imposible (§3).
- **`ctrl` / triggers / buff:** solo cambian objetivos (§4).
- **Escribir los WC o los canales de los simuladores de G:** los WC viven en objetos de nivel que se descargan y
  vuelven a cargar (riesgo de usar memoria liberada); además tocan estado interno.
- **Escribir el holder sin modo visor:** el mezclador lo sobrescribe en cada frame.
- **Escribir desde el worker de G:** sin el hilo de X hay riesgo de usar memoria liberada en el teardown de la escena.

---

## 6. Qué medir primero

### P0, sondas de solo lectura en H y G

Todo con lecturas protegidas, sin ganchos ni llamadas, añadidas a `clock_probe` o como `env_probe=1`.

- **P0-a, identidad.** Una vez por segundo: `S`, `X_a=[S+0xF68]`, `X_b=[[[0x146D69198]+0x38]+0x38]`, `[X]`,
  `X+0x31`, `holder=[X+0x60]`, `[holder-0x28]`, A, B, C y `X+0x88`.
  - Criterio: `X_a==X_b`, vtable `0x145D51268` y `X+0x31==0`.
- **P0-b, vector.** A 2 Hz: el vector v1 de §5.2 en float y en hex, más A+0x198..+0x1A8, el tick del 2188 y la
  posición.
  - Criterio: con H y G juntos, B+0xA8 y los mandos de datos coinciden (como en S17/S21/S22); las salidas
    aleatorias divergen.
  - También sirve para comprobar que B+0x64..+0xE8 son todos floats.
- **P0-c, estabilidad.** En G, en cada 2437/2643/2762 propio o de H y cada segundo: `holder`, A, B, `X+0x88` y los
  punteros de la lista `X+0x80`.
  - Criterio: X y su holder no cambian con las olas; la lista de WC sí cambia. Eso confirma §2.5 y la seguridad de
    §5.6.
- **P0-d, entradas (opcional).** La cima de la pila de triggers de `EnvMgr` (`[EnvMgr+0x90]`, progresos
  `+0x98..+0xA8`) y las ranuras `GlobalEffect` activas, con `tick % 60000` de cada cambio de A+0x12C/0x130/0x134 en
  el WC (vía X no se puede: se mira la serie de B+0xA8).
  - Sirve para confirmar o descartar la cuadrícula de 60 s (§2.6).

### P1, un solo PC (H solo, sin Shadow)

Con las partidas copiadas en `backups/`, un modo de prueba escribe `X+0x31=1` y `A+0x12C=1.0` durante 30 s y luego
lo restaura.

Criterio:
- llueve al momento: captura con `CopyFromScreen`;
- el `2560` de H pasa a lluvia = 1;
- al restaurar, el clima vuelve.

Valida el modo visor y sus efectos secundarios sin el segundo PC.

### P2, dos PC

Es el guion de §6 de `clima-hora.md`, con `env_mirror=probe` y después `=1`:
1. H espera 90 s;
2. entra G;
3. 60 s juntos;
4. viaja G;
5. viaja H;
6. capturas en los dos PC en el mismo tick.

Criterios:
- |vector G − vector H| ≤ 1e-3 en los mandos tras ≤ 0,5 s (los valores locales A+0x198.. quedan excluidos);
- el cielo y la lluvia son iguales en las capturas;
- al viajar H, G sigue el reinicio de H en ≤ 0,5 s, que es la semántica nativa de H;
- al cortar el enlace, G vuelve a su clima en 1 frame;
- 0 excepciones en el gancho.

**Este paso necesita Shadow:** hay que avisar al usuario cuando P0/P1 estén listos.

---

## 7. Lo que sigue sin saberse

- La entrada alineada a `tick % 60000` de S16 [H] (P0-d).
- El objeto exacto que se recarga en G con la ola de H (volumen de clima del nivel) [E/H] (P0-c).
- Qué hace `ctrl` por dentro (clase de `[[0x146D69190]+0xE0]` y de `EnvMgr+0x48`, vf168) [H]. No hace falta para el
  diseño.
- Si alguna ruta de `.xtls` lee `X+0x31` [H] (P1).
- El hilo de `0x3B106D0`, que llama a `0x3DC4730`. Por el diseño basta con que sea el hilo de X [E].

---

## Implementado (2026-10-12, build 1.0.0.2976)

Código: `src/mod/session2/weather_sync.h/.cpp` (hilo, gancho, túnel) y `src/mod/session2/weather_sync_logic.h`
(lógica pura: tabla de campos, trama del estado 12, cordura, interpolación, puertas, máquina del selftest, doble búfer
sin bloqueos). Tests: `tests/optd_weather_sync_tests.cpp` (`optd_weather_sync`). La clave se llama `weather_sync`, no
`env_mirror` como en §5.

### Comprobado en Ghidra antes de escribir código [V]

- `0x3DC4730`: prólogo `48 89 5C 24 10 | 55 | 56 | 57 | 48 83 EC 50 | C5 F8 29 74 24 40` (18 bytes, instrucciones
  enteras, ninguna relativa a RIP); un solo llamador, `0x3B10737` = `E8 F4 3F 2B 00`, justo después de
  `0x3B10730 mov rcx,[rcx+0xF68]`; `dt` llega en XMM1. Orden interno: si `X+0x31`, `0x382A1A0(holder-0x28)`; luego el
  mezclador; luego la muestra local `A+0x198..`; luego la integración de `X+0x124/+0x128`.
- `0x382A1A0`: con temperatura `A+0x198 > 0` pone lluvia `A+0x16C = A+0x160 = A+0x12C` y `A+0x164 = min(10·p, 1)`;
  con `≤ 0`, nieve `A+0x168 = A+0x12C` y lo demás a 0.
- `0x3DC0340` (mezclador): no hace nada si `X+0x31 != 0`; si no, mezcla los WC y copia al holder con su vf+0x160
  **solo si hay alguno registrado** (`X+0x88 > 0`).
- `0xCDCA40` sale en su primera comprobación si `[[[0x146C8CEC8]+0x60]+0xF68]+0x31 != 0`; la vf60 del módulo es
  `0x6DA0A0` = `48 8B 41 60 C3` (`mov rax,[rcx+0x60]; ret`).
- Getter de lluvia `0x3DC3A10`: en modo visor devuelve `A+0x16C` en crudo; si no, lo atenúa con la altura de la cámara
  frente a `B+0xB8`.
- Constructor `0x3DBB060`: vtable `0x145D51268`, `X+0x30` (u16) = 0 y `X+0x124 = X+0x128 = 0`.
- `0x41DB60` (datos → bloques): floats con centinela `FLT_MAX`; `B+0xEC`, `B+0xF0`, `C+0x1B4` y `C+0x1D4` son
  **colores RGBA** (u8×4, centinela 0xFF); `B+0xE8` (`_cloudSeed`) es float. `B+0x78..+0x80` no salen de los datos
  (tipo desconocido).

### Claves (`[session2]`)

| Clave | Valores | PC | Qué hace |
|---|---|---|---|
| `weather_sync` | `0` (por defecto) | los dos | Nada. |
| | `probe` | los dos | **Solo lectura** (P0): lecturas protegidas desde su propio hilo a 5 Hz; sin gancho, sin llamadas, sin escribir. |
| | `selftest` | **solo anfitrión, un PC** (P1) | La sonda + gancho en la entrada de `0x3DC4730`: 15 s después de que el mundo esté listo, 30 s con `X+0x31 = 1` y `A+0x12C = 1.0` en cada frame; luego lo devuelve. En el invitado se queda en `probe`. |
| | `1` | los dos (P2) | Anfitrión: la sonda + manda su vector como estado de control 12. Invitado: anuncia caps `0x04`, guarda las muestras y las escribe en modo visor desde el gancho. |
| `weather_sync_radius_m` | `2000` | invitado, modo 1 | Más lejos del anfitrión que esto, el invitado usa su propio clima. |

"Mundo listo" = el lector del jugador (`PlayerReader`: actor controlado, no aparcado en el punto de carga) lleva 3 s
leyendo bien; deja de estarlo tras 2 s seguidos sin leer (histéresis: un fallo suelto no corta nada).

### Vector y túnel

- Vector v1 = 61 palabras: A (17: `+0x12C..+0x158`, las salidas `+0x160..+0x16C`, `+0x190/+0x194`), B (36:
  `+0x64..+0xF0`), C (6: `+0x178`, `+0x1B0`, `+0x1B4`, `+0x1B8`, `+0x1D4`, `+0x1E0`) y X (`+0x124`, `+0x128`).
  Clases: **Lerp** (floats interpolados), **Hold** (floats de la muestra más nueva, sin interpolar y comprobados:
  `windDegree`, la semilla `B+0xE8`), **Raw** (copia bit a bit, sin comprobar: solo los colores u8×4 `B+0xEC/+0xF0`,
  `C+0x1B4/+0x1D4`), **Accum** (`X+0x124/+0x128`: interpolados y extrapolados hasta 1 s), **Derived**
  (`A+0x160..+0x16C`: se envían y registran, nunca se escriben, porque `0x382A1A0` los rehace cada frame) y
  **Unknown** (`B+0x78..+0x80` y `C+0x178`: tipo desconocido, podrían ser datos propios de cada PC; solo registro,
  nunca se escriben).
- Cordura: floats finitos y |v| < 1e7; `±FLT_MAX` (el "sin poner" de los datos) pasa tal cual, nunca se interpola, se
  cuenta aparte (`FLT_MAX (not set)` en la línea de 5 s) y en el mando de precipitación vale 0. Una muestra rechazada
  nunca se pierde en silencio: el anfitrión escribe `sample #n NOT sent` y el invitado `the host's sample #n dropped`
  (las 5 primeras, con el campo) y las cuentan las líneas de 5 s.
- El invitado escribe en `A+0x12C` el **mando** `max(lluvia, nieve)` actual del anfitrión (`A+0x16C`, `A+0x168`),
  limitado a [0, 1], no su objetivo (§5.2).
- **Estado de control 12** (opcode 1, slot -1, solo en lotes `ToGuestClient`): `u8 12 | u8 versión 1 | u16 máscara |
  u32 seq | u64 tick | f32 pos[3] | u32 palabras[61]` = 272 bytes de carga, trama de 277. Máscara: `0x01` A, `0x02` B,
  `0x04` C, `0x08` X, `0x10` posición del personaje del anfitrión, `0x20` el anfitrión tenía `X+0x31 != 0`.
- Solo se manda si el invitado anunció **caps `0x04`** en su paquete `kCapsOpcode` de esa conexión (las caps de la
  fase B de la partida propia usan `0x02` y los estados 6-11). Un invitado antiguo ignora el estado 12. El formato del
  protocolo del mod no cambia: **`kVersion` sigue igual**.
- Anfitrión: el hilo de `weather_sync` lee y deja la trama en un buzón (SRWLOCK); el hilo de red la empuja a 5 Hz si el
  enlace está conectado y sin romper, el invitado anunció `0x04`, el usuario 2 está en el mundo
  (`server_user2_in_world`) y el atraso del túnel es menor de 256 KB.
- Invitado: el hilo de red valida la trama (forma exacta, versión, máscara conocida, floats finitos y < 1e7) y la
  publica en un **doble búfer sin bloqueos** (seqlock de palabras atómicas). El gancho se queda con las dos más nuevas e
  interpola en el eje del tick del anfitrión con 300 ms de retraso (si el tick falta o no crece, con las horas de
  llegada).

### El gancho (invitado en modo 1, anfitrión en selftest)

- Inline (safetyhook) en la **entrada** de `0x3DC4730`, instalado una vez, cuando el mundo está listo. Antes se exige:
  la build (PE `0x6AB28F00`, base `0x140000000`), que `[0x3DC4730, +18)` esté en `.rsrc` y dentro de
  `[0x1000, 0x51EB000)`, los 18 bytes del prólogo, el `call` de `0x3B10737`, y que safetyhook copie exactamente 5, 6,
  7, 8, 12 o 18 bytes (instrucciones enteras) iguales a los esperados. Si algo falla: `REFUSED`, nada escrito.
- En el hilo que actualiza X, antes de llamar a la original: lecturas con `fast_read` (SEH) y escrituras con SEH que
  solo atrapa violaciones de acceso. La llamada a la original va **fuera** de cualquier `__try`. Sin reservas de
  memoria, sin bloqueos, sin escribir en el log: los eventos van a un anillo sin bloqueos que vacía el hilo.
- Solo se escribe en la X que se está actualizando en ese momento (viva). Las X que puso en modo visor se apuntan con
  su valor original de `X+0x31`; una X que ya tenía `X+0x31 != 0` sin haberlo puesto el mod no se toca.
- Modo 1: en la primera escritura en una X guarda las palabras del holder que puede escribir y las cuatro salidas
  `A+0x160..+0x16C`; escribe primero los valores (Lerp/Hold/Raw/Accum de los bloques presentes; C solo si está) y
  después `X+0x31 = 1`, así que en ese mismo frame la original ya corre en modo visor. Si la muestra tiene 1 s o más,
  falta, no trae A/B/X o no está sana, o una puerta se cierra (no juega en el anfitrión ahora, no está en el mundo, sin
  posición, fuera del radio), **devuelve las palabras guardadas** (las escritas desde entonces, cada una solo en el
  bloque del que salió; nunca los acumuladores de nubes, que seguirían saltando) y luego `X+0x31`: con
  `X+0x88 = 0` (ningún WC registrado) el mezclador no las reescribiría; con WC registrados las sobrescribe en ese mismo
  frame, así que nunca se ve nada viejo. El clima propio vuelve **en ese mismo frame** (los WC del invitado no se
  tocaron).
- Fallo de acceso (revisión, punto 2): deja de escribir y devuelve lo suyo; vuelve a escribir tras 300 llamadas
  seguidas con todas las lecturas bien (unos 5 s) o en la siguiente apertura de la puerta, como mucho 10 fallos por
  partida (después, modo 1 apagado hasta reiniciar, con una línea).
- Una X nueva (otra dirección: la escena se recreó) **desaloja** los registros de las demás X (revisión, punto 3): si
  su primera palabra sigue siendo la vtable del sistema de clima y su `X+0x31` sigue siendo el 1 que puso el mod, se
  devuelve el indicador; si no, no se toca nada (liberada o reutilizada). El conjunto nunca se llena.
- Puerta del radio con histéresis (revisión, punto 5): cerrada, se abre a menos de `R − m`; abierta, se cierra a más de
  `R + m`, con `m` = 5 % de R entre 10 y 100 m (100 m con 2000). Como mucho una línea de puerta cada 5 s; los cambios
  no escritos se suman a la siguiente y a la línea de 5 s.
- Por qué basta con devolver `X+0x31` desde el gancho (revisión, punto 7): mientras `0x3DC4730` no corre para esa X
  (pausa de menú, pantalla de carga) el holder no cambia en este cliente de todas formas, porque el mezclador, la única
  vía por la que los WC de los simuladores llegan a él, corre dentro de `0x3DC4730`. Un simulador que siga corriendo
  por su lado solo se queda congelado (su WC intacto), lo que no retrasa nada visible, y los getters devuelven los
  últimos valores escritos (el mismo cielo del frame anterior). Escribir desde otro hilo competiría con el de la
  actualización y podría tocar una X liberada por la carga; la siguiente actualización de una X viva la libera en ese
  mismo frame, y la de una X nueva desaloja el registro viejo.
- La puerta de "escena propia del invitado" **no se puede detectar en esta build** sin más ingeniería inversa: queda
  como entrada de la lógica pura, siempre en falso [H].

### Líneas del log

| Línea | Cuándo |
|---|---|
| `Config: P8 session 25 weather: weather_sync '…' (both) weather_sync_radius_m N (guest)` | al arrancar |
| `WeatherSync ON (weather_sync=…, host\|guest): …` | al arrancar el hilo |
| `WeatherSync [probe host]: tick N (%60000 = M) \| X … (M+0x60+0xF68) / … ([0x6D69198]+0x38+0x38): same\|DIFFERENT, vtable ok, viewer X+0x31 = 0 \| holder … A … B … C … \| WeatherComponents X+0x88 = n \| precipitation … cloudiness … \| B: … \| C: … \| scroll X+124/128 (…) \| local sample A+0x198.. (…) \| sanity: … \| X changes n \| world ready …` | cada 5 s (también en `selftest` y `1`, con un añadido del modo) |
| `WeatherSync [… …]: X CHANGED: X a -> b (path 2: …) holder … A … B … C … \| tick … (%60000 = …)` | al momento (muestreo a 5 Hz) |
| `WeatherSync [… …]: target changed: precipitation a -> b cloudiness … humidity … \| tick … (%60000 = …)` | cuando cambian los objetivos (P0-d; no mientras el invitado escribe) |
| `WeatherSync [… …]: full vector: …` y `B raw words +0x64..+0xF0: …` | al primer vector y cada minuto |
| `WeatherSync: exe+0x3DC4730 (X's update, one caller 0x3B10737) hooked at its entry (N bytes patched)` / `… REFUSED, nothing written` | al instalar el gancho |
| `WeatherSync selftest: APPLY BEGINS on X …: before: X+0x31 = 0 precipitation(12C) … rain(16C) … temperature sample(198) …` | P1, empieza |
| `WeatherSync selftest: one frame later (the original ran once in viewer mode): X+0x31 = 1 precipitation(12C) 1.000 rain(16C) … snow(168) …` | P1, frame siguiente |
| `WeatherSync selftest: RESTORED (done\|aborted\|fault) on X … after N frames: …` | P1, termina |
| `WeatherSync [1 guest]: gate OPEN\|closed (…); D m from the host (radius R m, margin M m) (+N changes … not logged)` | cambio de puerta (como mucho una cada 5 s) |
| `WeatherSync [1 guest]: X … -> VIEWER MODE (X+0x31 0 -> 1): the host's sample #n (age … ms, host tick …), precipitation command …; K holder words kept for the release (WeatherComponents X+0x88 = n)` | empieza a aplicar |
| `WeatherSync [1 guest]: X … back to its own weather (X+0x31 = its value written, K holder words put back, WeatherComponents X+0x88 = n): <motivo> after N frames` | deja de aplicar |
| `WeatherSync [… …]: a guarded access FAILED in the hook (…); writes stop until 300 clean frames or the next opening of the gate (fault i of at most 10)` | **error**: deja de escribir |
| `WeatherSync [1 guest]: writes allowed again on X … after fault i (…)` / `… faults: mode 1 stays OFF for the rest of this run` | vuelve a escribir / se apaga |
| `WeatherSync [1 guest]: X … is no longer the one the game updates (now …): its record dropped; its X+0x31 put back\|left\|NOT touched` | X recreada |
| `WeatherSync [1 host]: sample #n NOT sent: …` / `WeatherSync [1 guest]: the host's sample #n dropped: …` | muestra no sana (5 primeras) |
| `Session2 guest: weather_sync=1: the tunnel also announces caps 0x04 …` | invitado, al abrir el túnel |
| `WeatherSync [1 host]: the guest announced caps 0x.. without 0x04 …` | anfitrión, invitado sin modo 1 |

La línea de 5 s añade en el anfitrión con `1`: `to the guest: sent N (guest caps 0x05; …)` (0x05 = compresión +
clima; 0x07 con la partida propia); en el invitado con `1`:
`mirror: hook in, gate …, newest sample #n age … ms …, frames applied … released …, faulted no (faults …, frames held
…) | evicted X … | words put back … | gate changes not logged …`.

### P1, un solo PC, esta noche (anfitrión, sin Shadow)

1. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups/` (CLAUDE.md). El ini ya lleva `save_guard=redirect`;
   el selftest solo escribe memoria del **cliente** del anfitrión.
2. `docs/research/ini/cdmp.p8-clima-selftest.ini` como `bin64\cdmp.ini` (= `cdmp.p8-noche-local.ini` con
   `weather_sync=selftest`). Con o sin `cdmp_peer`.
3. Cargar la partida y quedarse **al aire libre, quieto, mirando al cielo**, en un sitio sin lluvia (mejor de día).
   Lo ideal es grabar o hacer capturas cada pocos segundos (`CopyFromScreen`).
4. En el log, por orden: `hooked at its entry`, la línea de 5 s con `selftest: world ready, natural weather first`, y a
   los ~15 s `APPLY BEGINS` (con los valores de antes).
5. **Qué debe verse:** en cuanto sale `APPLY BEGINS` empieza a **llover** (partículas de lluvia, suelo mojándose; nieve
   si la temperatura de la muestra `198` es ≤ 0). `one frame later` debe dar `X+0x31 = 1`, `precipitation(12C) 1.000`
   y `rain(16C) 1.000` (o `snow(168) 1.000`). Las líneas de 5 s muestran `viewer X+0x31 = 1` y `rain(16C) 1.000`.
   El `2560` del anfitrión debería pasar a lluvia = 1 (criterio de §6, P1).
6. A los 30 s, `RESTORED (done)`: `X+0x31` vuelve a 0 y `A+0x12C/160/164/168/16C` a lo de antes. **La lluvia debe
   parar** en unos segundos y el cielo volver al del nivel; las líneas de 5 s vuelven a `viewer X+0x31 = 0` con valores
   naturales.
7. **Abortar** (poner `weather_sync=0` y reiniciar el juego) si: sale `REFUSED` (no se escribió nada: avisar con la
   línea), sale `a guarded access FAILED`, el juego se congela o se cierra, o tras `RESTORED` sigue lloviendo más de
   20 s. Si `APPLY BEGINS` sale pero no llueve y `one frame later` da `rain(16C) 0.000` con temperatura > 0, el modo
   visor no manda la lluvia: parar y avisar. Si sale `X CHANGED` durante los 30 s (carga, viaje), la prueba se aborta
   sola (`RESTORED (aborted)` o "the run ended while the update ran on another X"): repetirla sin moverse.

### P2, dos PC (Shadow invitado, PC del usuario anfitrión)

1. **Primero `probe` en los dos** (`cdmp.p8-s7-host.ini` y `cdmp.p8-s7-shadow.ini` / `-fase2` / el plan B
   `cdmp.p8-s12-shadow-seguro.ini` ya lo llevan). Guion de §6 de `clima-hora.md`: H espera 90 s, entra G, 60 s juntos,
   viaja G, viaja H. Comparar `WeatherSync [probe host]` con `[probe guest]` al mismo tick: en los dos `same` (las dos
   rutas dan la misma X), `vtable ok`, `X+0x31 = 0`; juntos, `cloudBaseDensity(A8)` y los objetivos iguales; las salidas
   (`rain`, capas) divergen. En G, `X CHANGED` durante las olas de H dice si X sobrevive (§5.6, P0-c). Las líneas
   `target changed` dan `tick % 60000` para la cuadrícula de 60 s (P0-d). Las palabras crudas de B confirman que
   `B+0x64..+0xE8` son floats.
2. **Después `weather_sync=1` en los dos.** H: `WeatherSync ON (weather_sync=1, host)` y en sus líneas de 5 s
   `to the guest: sent N (guest caps 0x05 …)` creciendo a 5 por segundo con el usuario 2 en el mundo. G:
   `the tunnel also announces caps 0x04`, `gate OPEN`, `VIEWER MODE`, y sus líneas de 5 s con `viewer X+0x31 = 1`.
3. **Criterios:** el vector de G igual al de H (diferencia ≤ 1e-3 en los mandos, sin contar `A+0x198..`) en las líneas
   del mismo tick; capturas en los dos PC en el mismo momento con el mismo cielo, nubes y lluvia; cuando H viaja, G
   sigue su reinicio en ≤ 0,5 s; al cortar el enlace, G escribe `back to its own weather (… 1 s old or more)` y vuelve
   a su clima; `frames applied` crece y `faulted no`.
4. **Abortar** (`weather_sync=0` en los dos) si: cualquier `FAILED`, cierre o congelación de alguno de los dos juegos,
   G sigue en modo visor (lluvia del anfitrión) más de 5 s tras perder el enlace, o `VIEWER MODE` / `back to its own
   weather` se alternan sin parar (puerta inestable: anotar los motivos).

### Riesgos y lo que falta

- Modo visor [V]: además de la lluvia, para los 4 simuladores (incluido el de hora: el invitado depende del `B` del
  anfitrión para el sol y la niebla, que se le escribe) y el mezclador; los getters dejan de atenuar la lluvia por
  altura. Las capas `GlobalEffect` y los volúmenes propios de G se ignoran mientras se aplica (autoridad de H).
- Si `X+0x88 = 0` (ningún WC registrado) al devolver el indicador, el mezclador no reescribe el holder: el selftest
  devuelve `A+0x12C/160/164/168/16C` y el modo 1 todas las palabras que escribió (las líneas dan `X+0x88`). Las
  palabras devueltas son las de la primera escritura: con `X+0x88 = 0` el invitado vuelve a su cielo de entonces.
- `B+0x78..+0x80` y `C+0x178` (tipo desconocido) solo se registran.
- Desalojar escribe el indicador de una X que ya no se actualiza: solo si su primera palabra sigue siendo la vtable
  y su `X+0x31` sigue siendo el 1 del mod (una X liberada y reutilizada no pasa la vtable) [riesgo bajo, aceptado en
  la revisión].
- [H] una ruta de `.xtls` que lea `X+0x31` (lo cubre P1).
- La puerta de escena local no está (ver arriba). La distancia usa la posición del personaje (world, Y arriba) de los
  dos lados.
- **P2 necesita Shadow.**

## P1 en un PC (12-10, 04:49 y 04:52) [V]

- `weather_sync=selftest`, dos ejecuciones (`backups/noche-clima-selftest-1.log`, `-2.log`). X por los dos caminos es el mismo
  objeto, vtable correcta, `X+0x88 = 60` componentes. Tras 15 s, `X+0x31 = 1` y precipitacion/lluvia 1.0 durante 30 s
  (1485 frames), sostenidas frente al simulador; despues `RESTORED (done)` y `X+0x31 = 0`. Ningun fallo ni ERR.
- En pantalla (capturas en `backups/noche-clima-capturas/`): llovizna y bruma visibles, cielo aun claro porque la
  nubosidad se dejo como estaba (el modo 1 copia el vector entero del anfitrion, nubes incluidas).
- Conclusion: el modo visor acepta valores externos y el juego vuelve a su clima al quitar la bandera. Falta P2 (dos PC).

