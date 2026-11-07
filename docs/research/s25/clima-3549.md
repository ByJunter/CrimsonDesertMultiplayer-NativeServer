# Clima (`weather_sync`) en la build 1.0.0.3549: direcciones, capas y formato de red

Build **1.0.0.3549**, PE `0x6AEB7D59`, imagen `0x13CFF000`, base `0x140000000`. RVA salvo que pongan `0x14…`.
Investigación **solo estática** (2026-11-05): capstone sobre el `.exe` instalado con `tools/scripts/rtti_dump.py`
(`Pe`) y `tools/porting/signature_map.py` (`exception_ranges`). No se lanzó el juego, no se leyó memoria viva y no se
tocó `src/`, `tests/` ni `bin64`. Los scripts están en el scratchpad de la sesión (`weather3549/s*.py`).

Etiquetas: **[V]** leído en el PE (bytes o desensamblado); **[I]** inferido; **[N]** no encontrado.

Complementa `docs/research/noche/clima-sincronizar.md` (2976) y sustituye el párrafo "Build 1.0.0.3527: NOT ported"
de `weather_sync_logic.h`.

---

## 0. Dictamen

1. **Todo lo que el módulo usa en 2976 tiene equivalente verificado en 3549**, incluidos los tres puntos que las
   notas de 3527 daban por no encontrados: la ranura de M (`0x70D9FC0`), la vtable de X (`0x14611E330`) y las
   puertas de los cuatro simuladores. La vtable de X no aparecía porque **no tiene RTTI** (en `vt-8` hay un puntero a
   código, no un COL): se sacó de su constructor.
2. **El gancho va en la entrada de `0x40C2580`** (la actualización de X). Prólogo distinto del de 2976 pero limpio:
   `48 8B C4 | 53 | 55 | 56 | 57 | 41 54 | 41 56 | 41 57 | 48 81 EC 90 00 00 00`. Un solo llamador
   (`0x3DF5CB7`), ninguna otra referencia y ningún salto hacia dentro del prólogo.
3. **Las capas cambiaron**, y ahora se conocen por el nombre que les da el propio juego (sus *setters* reflejados
   cargan la cadena con el nombre del campo). A gana `oceanWindDegree` en `A+0x158`; B gana un cuarto `mie*` y un
   color nuevo en los datos (`rayleighScatteringColor`, `B+0x30`); C es la de 2976 menos `0x108`; X tiene cuatro
   acumuladores nuevos y un ángulo que gira.
4. **Hace falta otra versión del formato de red**: estado de control 12, **versión 2**, 69 palabras en el orden de
   `kFields3549` (carga 304 bytes, trama 309). 2976 sigue en la versión 1 (61 palabras, 277 bytes). Las dos builds no
   juegan juntas (el saludo compara el timestamp del PE) y cada lado solo decodifica su versión. `kVersion` del
   protocolo del mod no cambia.
5. El parche (tabla por build, puerta, capas, tests) está listo y comprobado fuera del árbol del proyecto: se aplica
   limpio, compila con `/W4` sin avisos y `optd_weather_sync` pasa. **3527 sigue negándose:** comparte distribución
   con 3549, pero su PE no se leyó para este módulo.

## 1. Tabla de direcciones 2976 → 3549

| Qué | 2976 | 3549 | Estado y evidencia |
|---|---|---|---|
| Actualización de X `(X en RCX, dt en XMM1)` | `0x3DC4730` | `0x40C2580` | [V] `.pdata` `0x40C2580..0x40C2C8C`; `vmovaps xmm10, xmm1` en `+0x2E`, `mov rsi, rcx` en `+0x32` |
| Su único llamador | `0x3B10737` `E8 F4 3F 2B 00` | `0x3DF5CB7` `E8 C4 C8 2C 00` | [V] escaneo E8/E9 de todas las secciones ejecutables: un solo resultado; 0 qwords y 0 `lea` RIP hacia `0x40C2580` |
| Antes del `call` | `mov rcx,[rcx+0xF68]` | `0x3DF5CB0` `48 8B 89 70 0F 00 00` `mov rcx,[rcx+0xF70]` | [V]; `rcx = [rdi+0x98]` (S) y `xmm1 = [rdi+0xA8]` (dt). La función `0x3DF5C50` se llama por la vtable de `0x60E8D00` |
| Ranura de M (camino de los simuladores) | `0x6C8CEC8` | `0x70D9FC0` | [V] los cuatro simuladores hacen `mov rcx,[rip→0x70D9FC0]; call [rax+0x60]; mov rdx,[rax+0xF70]; cmp byte [rdx+0x31],0`; el ctor de M `0x40C5120` hace `mov [rip→0x70D9FC0], rcx` |
| S = `[M+0x60]` (getter de M) | vf `+0x60` = `0x6DA0A0` | vf `+0x60` = `0x7AA9B0` `48 8B 41 60 C3` | [V] vtable final de M `0x14611EAA0` (ctor `0x40C5120`, fábrica `0x6D7D30` con 0xF8 bytes); `[0x611EAA0+0x60] = 0x1407AA9B0` |
| X = `[S+…]` | `+0xF68` | `+0xF70` | [V] llamador y simuladores |
| Segundo camino (el emisor del 2560/2689) | `[[[0x6D69198]+0x38]+0x38]` | `[[[0x71BDEF8]+0x48]+0x38]` | [V] `0xAB3010` (emisor de `SyncWeatherStateReq`, opcode 2689 en 3527/3549) pasa ese puntero como `this` a los getters de X `0x40C1720/17D0/1880/1910`. Otros usuarios del mismo camino: `0x7B468F`, `0x7BA0C0`, `0x9CEBD8` |
| vtable de X | `0x145D51268` | `0x14611E330` | [V] el ctor `0x40B9150` escribe `[X] = 0x14611E330` en último lugar (y `[X+0x28] = 0x14611E300`, la interfaz de *listener*); la fábrica `0x40C4510` reserva `0x100228` bytes, llama al ctor y no escribe otra vtable. Sin RTTI |
| Modo visor | byte `X+0x31` | byte `X+0x31` | [V] ctor: `mov word [X+0x30], 0`; lo leen la actualización, el mezclador, `0x40BAC10` (exporta un bit 2 a los parámetros de render) y los getters |
| Holder | `[X+0x60]` | `[X+0x60]` | [V] ctor lo pone a 0; actualización y getters lo leen |
| Bloques A / B / C | `[holder+0x18/0x20/0x28]` | `[holder+0x20/0x28/0x30]` | [V] actualización (`lea rcx,[rax+0x20]`… con respaldo `cmove 0x48/0x50/0x58`), getters, mezclador, mapa de datos |
| WC registrados | `X+0x80`, n `X+0x88` | `X+0x80`, n `X+0x88` | [V] mezclador `0x40BE2A3/0x40BE2AA` |
| Mezclador | `0x3DC0340` | `0x40BE1E0` | [V] `cmp byte [rcx+0x31],0; jne` al principio; solo copia al holder (vf+0x160) si recogió algo (`cmp [rsp+0x48],0`) |
| Derivación del modo visor | `0x382A1A0` | `0x3B06D80` | [V] fuera de `.pdata` (hoja); ver §2.2 |
| Muestra local (temperatura…) | `A+0x198`, 5 floats | `A+0x1A0`, 5 floats | [V] la actualización copia 0x14 bytes a `A_obj+0x1C8` (`A+0x1A0`); `0x3B06D80` lee la temperatura en `A+0x1A0` |
| Getter de lluvia | `0x3DC3A10` | `0x40C1720` | [V] visor: `A+0x170` crudo; si no, atenuado por la altura de cámara frente a `B+0xBC` |
| Getter de nieve / viento / temperatura | — | `0x40C17D0` / `0x40C1880` / `0x40C1910` | [V] nieve `A+0x16C`; viento `clamp(A+0x1AC + A+0x138, 0, 60)` (fuera del visor, `max` con `A+0x15C · X+0x100218`) |
| Mapa datos → bloques | `0x41DB60` | `0x4BCAB0` | [V] §3 |
| Reloj de servidor | `0x6C8CD68`, `0x6C8CD70`, `0x6D69438` | `0x70D9E60`, `0x70D9E70`, `0x71BE198` | [V] los mismos sitios que en 3527: `0x15F9B40`/`0x15F9B4B` (`mov r8/rcx, [rip→…]`) y `0x1386C1C` (`mov rbx,[rip→0x71BE198]`) |
| Código real | `[0x1000, 0x51EB000)` | `[0x1000, 0x5595000)` | [V] sección ejecutable y no escribible (`.xtext` en 3549; el nombre no se usa) |

**Aviso:** `signature_mapping_2976_3527.json` da `0x969B40` como `unique_candidate` del prólogo de `0x3DC4730`. Es
otra función que empieza con los mismos 18 bytes; la actualización real de 3549 tiene otro prólogo.

## 2. Detalles verificados

### 2.1 El punto de gancho `0x40C2580` [V]

```
+00 48 8b c4                 mov rax, rsp
+03 53                       push rbx
+04 55                       push rbp
+05 56                       push rsi
+06 57                       push rdi
+07 41 54                    push r12
+09 41 56                    push r14
+11 41 57                    push r15
+13 48 81 ec 90 00 00 00     sub rsp, 0x90
+20 c5 f8 29 70 b8           vmovaps [rax-0x48], xmm6
```

- Límites de instrucción ≥ 5: **5, 6, 7, 9, 11, 13, 20**. Ninguna es relativa a RIP. `mov rax, rsp` se puede reubicar:
  el trampolín se entra con `call` desde el desvío, así que `rsp` vale lo mismo que en la entrada original.
- Ningún salto apunta a `[0x40C2581, 0x40C2594)`: ni desde la función, ni con `E8`/`E9`/`0F 8x` rel32 desde
  cualquier sección ejecutable, ni con saltos cortos cercanos. El único patrón `7E 28` está en mitad de
  `45 88 7E 28` en `0x40C2567`.
- Con el salto de 5 bytes, safetyhook copia `48 8B C4 53 55`. Con el de 14 copiaría los 20 bytes, que también están
  verificados. `whole_head()` solo acepta esos prefijos.

### 2.2 Lo que hace la actualización [V]

```
if (X+0x31) 0x3B06D80(holder-0x28)            ; derivación del visor
0x40BE1E0(X)                                   ; mezclador (no hace nada con X+0x31)
A+0x1A0..+0x1B3 = 0x40BEFE0(X, [X+0x100]+0xC8)  ; muestra local
w = clamp(A+0x1AC + A+0x138, 0, 60)
X+0x13C += w·A+0x198·dt·B+0xB8 ;  X+0x140 += w·A+0x19C·dt·B+0xB8      (el scroll de 2976, X+0x124/+0x128)
X+0x14C: ángulo (grados, mod 360) que gira hacia A+0x154 a un ritmo limitado por |k·dt|
         (k depende de [X+0x100]+0xC8/+0xD0 y de un global de .bss); X+0x144/+0x148 = (cos, −sin)(X+0x14C·π/180)
X+0x154 += w·A+0x198·dt·0.15 ;  X+0x158 += w·A+0x19C·dt·0.15
X+0x15C += w·A+0x198·dt·0.25 ;  X+0x160 += w·A+0x19C·dt·0.25
X+0x138 = 0x40BEC50(X, …) ;  X+0x100218 = f(altura de cámara)   (locales, no se envían)
si 0x37F9F10(…) (sobre el mar): A+0x154 se acerca a A+0x158 con su setter 0x3AD2C80
```

`0x3B06D80`: con temperatura `A+0x1A0 > 0`, `A+0x170 = A+0x164 = A+0x12C`, `A+0x168 = min(10·A+0x12C, 1)` y
`A+0x16C = 0` (lluvia). Si no, `A+0x170 = A+0x164 = A+0x168 = 0` y `A+0x16C = A+0x12C` (nieve). Es el mismo
`0x382A1A0` de 2976, con todo desplazado `+4`.

El *setter* de `windDegree` (`0x3AD2C80`) avisa al manejador de cambios del bloque A (`0x3AD5B00`), que recalcula
`A+0x198/+0x19C = (cos, −sin)(windDegree)`. **Una escritura cruda no lo dispara**, así que el invitado tiene que
escribir también `A+0x198/+0x19C`, igual que en 2976.

### 2.3 Puertas de los simuladores [V]

Los cuatro `vf7` (vtables con RTTI) salen en su primera comprobación con `[[[0x70D9FC0]]->vf+0x60()+0xF70]+0x31 != 0`:

| Simulador | vtable | vf7 | puerta |
|---|---|---|---|
| `WeatherSimulatorScriptComponent` | `0x1459E5A88` | `0xE64D70` | `0xE64DC7` |
| `WeatherCloudSimulatorScriptComponent` | `0x1459E5510` | `0xE60DD0` | `0xE60DF7` |
| `WeatherSurfaceSimulatorScriptComponent` | `0x1459E5B50` | `0xE68870` | `0xE68897` |
| `weatherTimeSimulatorScriptComponent` | `0x1459E5BF8` | `0xE692D0` | `0xE69317` |

Son los **únicos** que leen `X+0x31` a través de `+0xF70` en el código real, y nadie lo escribe por ese camino (igual
que en 2976). `WeatherEffectScriptComponent` (vf7 `0xE636D0`) no lo mira.

## 3. Capas A / B / C con nombres [V]

Método: cada campo reflejado tiene un *setter* que hace `lea rsi,[rcx+OFF]` sobre el objeto del bloque (=
`bloque−0x28`) y `lea rdx,[rip→"_nombre"]`. Por ejemplo, `0x3AE7EB0` es `_cloudBaseDensity` en `[rcx+0xD4]`, o sea
`B+0xAC`. El tipo sale de la comparación: `vucomiss` es un float y los bytes comparados con `0xFF` son un color u8×4.
El mapa `0x4BCAB0` dice qué campo de `GameGlobalEffectInfo_Weather` va a cada sitio, con `FLT_MAX` o `0xFF×4` como
"sin poner". Los indicadores "fijado" están en `A+0x176..+0x180`, `B+0xF9..+0x11F` y `C+0xF2..+0xFE`.

**A** (dato → campo): `+0x00 precipitation A+0x12C`, `+0x04 cloudiness 0x130`, `+0x08 humidity 0x134`,
`+0x0C windSpeed 0x138`, `+0x10 puddleRate 0x13C`, `+0x14 snowPuddleRate 0x144`, `+0x18 snowAmount 0x148`,
`+0x1C snowRate 0x14C`, `+0x20 iceRatio 0x150`, `+0x24 windDegree 0x154`, `+0x28 altitudeWindRatio 0x15C`.

- Fuera de los datos: `puddleScale 0x140` y **`oceanWindDegree 0x158`** (nuevo). Su *setter* solo tiene la
  referencia de registro, así que lo escriben la reflexión o el mezclador.
- Salidas de la derivación: `rainDropletAmount 0x164`, `rainDropletRate 0x168`, `snow 0x16C` y `rain 0x170`
  (los `0x160..0x16C` de 2976).
- Dirección del viento: `0x198/0x19C`. Muestra local: `0x1A0..0x1B3`. `A+0x160` no es un campo de clima.

**B**:
- Del sol y la luna, desde los datos: `sunDirX/Y 0x64/0x68`, `moonSizeAngle 0x6C` y `moonDirX/Y 0x70/0x74`.
- Floats que no vienen de los datos: `earthAxisTilt 0x78`, `latitude 0x7C` y `rayleighScaledHeight 0x80` (los
  "B78?..B80?" de 2976).
- Dispersión y niebla: `mieScaledHeight 0x84`, `mieAerosolDensity 0x88`, `mieAerosolDensityMultiRatio 0x8C`,
  `mieAerosolAbsorption 0x90`, `ozoneRatio 0x94`, `directionalLightLuminanceScale 0x98`, `heightFogDensity 0x9C`,
  `heightFogBaseline 0xA0`, `heightFogFalloff 0xA4` y `heightFogNoiseRange 0xA8`.
- Nubes: `cloudBaseDensity 0xAC`, `cloudBaseContrast 0xB0`, `cloudAlpha 0xB4`, `cloudScrollMultiplier 0xB8`,
  `cloudAltitude 0xBC`, `cloudThickness 0xC0`, `cloudNear 0xC4`, `cloudFadeRange 0xC8`, `cloudDetailRatio 0xCC`,
  `cloudDetailScale 0xD0`, `cloudCirrusAltitude 0xD4`, `cloudCirrusDensity 0xD8`, `cloudCirrusWeightR/G/B
  0xDC/0xE0/0xE4`, `cloudFlow 0xE8` y `cloudSeed 0xEC` (float).
- Colores: `volumeFogScatterColor 0xF0`, `mieScatterColor 0xF4` y **`rayleighScatteringColor 0x30`** (este último
  es nuevo en los datos, `data+0xC0`).
- Antes de `0x64` hay más campos (`sunLightIntensity 0x18` … `sunSizeAngle 0x60`). Los datos de clima no los tocan y
  2976 tampoco los enviaba.

**C** (la de 2976 menos `0x108`): `seaBaseline 0x70` (era "C178?"), `extinctionCoefficient 0xA8`,
`scatteringAlbedo 0xAC` (color), `underWaterMossRate 0xB0`, `underWaterTintColor 0xCC` (color) y
`underWaterTintStrength 0xD8`.

**Correspondencia de B con 2976 [I, no resuelta].** 2976 tenía 26 floats en `B+0x84..+0xE8`; 3549 tiene 27 en
`B+0x84..+0xEC`. El emisor del 2689 lee `B+0xAC`, `B+0xA0` y `B+0x8C` donde el del 2560 leía `B+0xA8`, `B+0x9C` y
`B+0x88`, lo que coloca el float nuevo en `B+0x84` o en `B+0x88`. Pero el simulador de hora de 3549 escribe
`B+0x84/88/8C/94`, los mismos desplazamientos que `clima-sincronizar.md` da para 2976. Sin el `.exe` de 2976 no se
puede decidir. **No afecta al port:** el vector de 3549 se define con los nombres de 3549.

## 4. El vector de 3549 y el formato de red

`kFields3549` (69 palabras):
- **A (18):** los 11 de datos más `oceanWindDegree` (Hold); las 4 salidas (Derived: se envían, nunca se
  escriben, se devuelven al soltar); `windDirX/Z`.
- **B (38):** el color `+0x30` y de `+0x64` a `+0xF4` sin huecos. Son Unknown (solo registro) `+0x78/+0x7C/+0x80`,
  como en 2976; Hold `cloudSeed`; Raw los tres colores.
- **C (6):** los mismos tipos que en 2976.
- **X (7):** `+0x13C/+0x140` (el scroll de 2976), `+0x154/+0x158/+0x15C/+0x160` (los nuevos, Accum) y `+0x14C`
  (Hold: el ángulo que gira; `+0x144/+0x148` los recalcula la actualización a partir de él).

Lecturas protegidas: A `[0x12C, 0x1A0)`, B `[0x30, 0xF8)`, C `[0x70, 0xDC)` y X `[0x13C, 0x164)`. Todas caben en los
bloques: los indicadores de cada bloque quedan detrás.

**Estado de control 12, versión 2:** `u8 12 | u8 2 | u16 máscara | u32 seq | u64 tick | f32 pos[3] | u32 palabras[69]`.
Son 304 bytes de carga y 309 de trama. La versión 1 (2976) queda igual: 61 palabras, 277 bytes.
- `is_frame` exige el tamaño exacto de la versión de la build. Una trama de la otra build no se saca del flujo,
  igual que hacía 2976 con cualquier forma desconocida.
- `decode` exige la versión.
- Como hay más de 64 palabras, los conjuntos "guardado" y "escrito" de `Owned` pasan a `std::bitset<80>`.

## 5. El parche

`scratchpad/weather3549/weather_sync_3549.patch` (diff unificado sobre los cuatro ficheros) y las copias completas
en `scratchpad/weather3549/new/`. Se aplica con `git -c core.autocrlf=false apply` (con `autocrlf=true`, git escribe
CRLF en ficheros que son LF).
- `weather_sync_logic.h`:
  - `GameAddrs` con `kGame2976` y `kGame3549`, y `Layout` con `kLayout2976` y `kLayout3549`.
  - `make_layout`, `well_formed` y `fields_inside_ranges` en `static_assert`.
  - `whole_head()`, `Frame`, `payload_size()` y `frame_size()`.
  - Todas las funciones de campos reciben el `Layout`.
  - El contenido de 2976 no cambia.
- `weather_sync.cpp`:
  - `G()` y `L()` (atómicos, fijados en `weather_sync_start` antes del hilo y del `g_mode`).
  - La puerta: 2976 como antes; 3549 si `game::is_3549()`; 3527 se niega con su línea.
  - Todos los desplazamientos salen de las tablas.
  - La línea de 5 s de 2976 queda **idéntica** carácter a carácter: los desplazamientos del texto vienen de
    `ProbeOffsets`.
  - De paso se corrige un argumento de más en la línea `RESTORED`: imprimía la nieve como "writes".
- `weather_sync.h`: solo comentarios.
- `tests/optd_weather_sync_tests.cpp`:
  - Las mismas pruebas para las dos capas.
  - Pruebas nuevas de la tabla de 3549 por nombre.
  - Las tablas de direcciones: el `call` reconstruido llega a la actualización y solo se aceptan prefijos enteros
    del prólogo.
  - Las tramas v2 byte a byte, y que una build no toma las tramas de la otra.
  - El *blend* de los acumuladores nuevos.

Comentarios que se quedan desfasados fuera del parche: `guest.cpp:1882` y `troc_frame.h:91` hablan de "277-byte".

## 6. Qué comprobar en vivo antes de encenderlo

Por orden. Cada paso solo si el anterior sale bien. Antes de `selftest` o `1`, copia de las partidas en `backups/`.

1. **`weather_sync=probe`, un PC, en el mundo.**
   - La línea de 5 s debe decir `X 0x… (M+0x60+0xF70) / 0x… ([0x71BDEF8]+0x48+0x38): same, vtable ok, viewer X+0x31 = 0`,
     sin `getter NOT 0x7AA9B0`. Eso confirma en vivo la ranura de M, los dos caminos y la vtable `0x14611E330`.
   - En `full vector`, cada minuto:
     - `windDirXA+198² + windDirZA+19C² ≈ 1` y `atan2(−Z, X)` ≈ `windDegree` en grados;
     - `windAngleFollowX+14C` cerca de `windDegree`;
     - `cloudScrollX/Z`, `scroll2*` y `scroll3*` creciendo entre líneas (las parejas 2 y 3 con la razón 0,15/0,25);
     - los colores con aspecto RGBA;
     - `sanity: 0 non-finite`.
   - La muestra local `A+0x1A0..` debe ser plausible: el primer valor es la temperatura.
   - `B raw words +0x30..+0xF4`.
2. **`weather_sync=selftest`, solo anfitrión.**
   - `exe+0x40C2580 … hooked at its entry (5 bytes patched)`.
   - A los 15 s, `APPLY BEGINS`; un frame después, `rain(170) 1.000` (o `snow(16C)` bajo cero). En pantalla debe
     llover.
   - A los 30 s, `RESTORED (done)` y la lluvia se para.
   - Abortar con cualquier `REFUSED`, `FAILED`, cierre o congelación.
3. **`weather_sync=1` en dos copias, y después en dos PC.** Los mismos criterios que en P2 de
   `clima-sincronizar.md`. Además:
   - las líneas `full vector` del invitado y del anfitrión en el mismo tick deben coincidir también en
     `oceanWindDegree`, `windAngleFollow` y `scroll2/3`;
   - las nubes deben verse en la misma posición.

Si en el paso 1 sale `DIFFERENT` o `vtable BAD`, hay que leer en vivo:
- `[0x1470D9FC0]`, `[[…]]` (debe ser `0x14611EAA0`) y `[[…]+0x60]+0xF70`;
- `[[[0x1471BDEF8]+0x48]+0x38]`;
- el primer qword de los dos punteros a X.

Con eso se sabe qué camino se equivoca.

## 7. Dudas y lo que no se sabe

- **3527 [N]:** la misma distribución según `build_delta.py`, pero su PE ya no está en disco. Queda apagado.
- **Rutas protegidas (sección RWX) que lean `X+0x31` [N, como en 2976]:** no se pueden escanear; el `selftest` lo
  cubre.
- **`oceanWindDegree` [I]:** el mezclador o la reflexión lo escriben, y no hay llamadas directas a su *setter*. Se
  envía como Hold. Si en el paso 1 resulta ser local de cada PC (distinto en los dos PC estando juntos), pasarlo a
  Unknown.
- **`B+0x18..+0x60` (atmósfera) no se envía**, igual que en 2976. Con la puerta de radio no debería verse; si dos
  regiones con atmósfera distinta coinciden dentro de 2 km, el invitado conserva la suya congelada.
- El ritmo de giro de `X+0x14C` depende de `[X+0x100]+0xC8/+0xD0` (de la cámara, [I]). Por eso se escribe cada frame
  y nunca se devuelve al soltar.
