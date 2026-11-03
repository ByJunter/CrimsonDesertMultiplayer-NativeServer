# Compañeros: el cierre tras la salida del usuario 2 (`exe+0xAAFC1D`)

Fecha: 2026-10-09. Build 1.0.0.2976, base `0x140000000`, direcciones RVA. Análisis de solo lectura:
- Ghidra: servidor propio, instancia `62fa56ef3818464890e1a9b0177cf000`, pid Java 29680. Parado con
  `server-stop.ps1 -Instance …` → `PARADO=si`, sin procesos de la instancia y el puerto 18490 libre.
- Capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, con scripts en
  `%TEMP%\claude\cierre\` (`cdis.py`, `pdata.py`, `xref.py`, `ripw.py`, `wdisp.py`, `w40.py`,
  `st5e.py`, `inc60.py`).
- Los dos logs de las 12:01 y las 12:04 y el registro de eventos de Windows (Application, Id 1000).

Las direcciones `.xtls` se leyeron como bytes. No se propone engancharlas.

Etiquetas: **[E]** código, **[V]** log o evento, **[S]** inferencia.

## Resumen

1. **[V] El cierre en `0xAAFC1D` es secundario.** El registro de Windows tiene **dos fallos por
   proceso**:

   | Proceso | Primer fallo (≈1 s tras la 2001) | 2001 en el log | Segundo fallo |
   |---|---|---|---|
   | `0x7988` | 12:01:42 en **`0x17F328F`** | 12:01:41.131 | 12:01:50 en `0xAAFC1D` |
   | `0x7E2C` | 12:06:26 en **`0x17F328F`** | 12:06:25.471 | 12:06:36 en un «módulo desconocido», offset `0x163532018` |

   El segundo fallo de las 12:06 es la misma instrucción `mov rbx,[rdi+0x210]`. Se ejecutó dentro
   del trampolín del mid-hook `CompBadThis`, que está fuera de cualquier módulo. Además, la línea
   `CompBadThis` de las 12:06:35.308 es la última del log.
2. **[E][V] El fallo primario es otro lector de la capa sin acción.** `0x17F3240` lee la
   reproducción de la capa principal (`[capa+0x18]+0x38`). Lo llama `0x630BF0`, la aplicación de
   **`CharacterDataDefinedStatAck` (3095)**. Para el hijo del usuario 2 esa reproducción es nula: el
   log lo muestra hasta el final con `play(+18)=false`. Es el mismo defecto que contuvo D-52, pero
   por un camino que D-52 no cubre.
3. **[E] El segundo fallo ocurre al cerrarse el juego, después del primero.** La pila, verificada
   marco a marco, sale de `0x8AADB0`, que destruye todos los actores vivos al eliminar el objeto de
   juego (`0xACD400`).
   - `0xACD400` apaga antes el `CameraManager` (`0xAABA60`), que pone `+0x40` a nulo.
   - Después, un actor con control local (`CC+0x1E9 ≠ 0`) entra en el `vf6` del CC (`.xtls
     0x984A620`).
   - Ese `vf6` llama a `0x869630`, que espera la cámara del jugador.

   No lo provoca saltar la actualización (D-52) ni la retirada de un actor sin acción.
4. **[S] Arreglo de raíz.** El hijo remoto no debe quedar activo con chart y sin acción. Su
   controlador tiene que dar la primera acción, y el servidor debe publicar el hijo con
   restauración 1, como hizo D-48 con Kliff. Mientras eso no exista, vale la contención actual, que
   es negarse a crear el hijo o retenerlo.

   Cubrir lector por lector no es un arreglo: ya hay al menos cinco lectores nativos distintos que
   han fallado (§4).

## 1. `0x869630`, su `rdi`, sus llamadores y la global `0x6D691B8`

### 1.1 Qué es `0x869630`

**[E] Es «soltar el control local» del `ClientCharacterControlActorComponent`.**
- `rdi` es el CC: `[actor+0x68]+0x40`, vtable `0x55B5460`.
- Reinicia la entrada y el movimiento: `+0x120`, `+0x138`, `+0xE0`, `+0x140`, `[[actor+0x68]+0x90]+0x1A0..0x1C8`
  y `+0xC8`.
- Avisa a la cámara de la clave del actor. Así queda el tramo del fallo:

```
0x869853  mov r14,[rdi+0xF8]
0x86985D  mov rbx,[rax+8] ; mov ebx,[rbx+0x60]        ; clave del actor
0x869864  mov rax,[0x6D691B8] ; mov rcx,[rax]           ; CameraManager
0x86986E  mov rax,[rcx+0x40] ; lea rcx,[rax-0x28] ; test rax,rax ; cmovz rcx,rbp(=0)
0x86987D  call 0xAAFC00                                  ; GetComponent<PlayerCameraComponent>
0x869882  mov rcx,[rax+0x340] ; … call [vf+0x40](clave)
0x8698B6  call 0xAAE400 (CameraManager, &clave, 0) ; luego (…, 1)
```

- **[E] `0xAAFC00`** recorre los componentes `[this+0x210]/[+0x218]` y compara el tipo que da
  `0xAA9E50`. Ese tipo es `pa::ReflectMetaObjectBind<pa::PlayerCameraComponent>`. No admite `this`
  nulo.
- **[E]** El `cmovz` es el adaptador habitual de «manejador → objeto»: el manejador apunta a
  `entidad+0x28`. No es una comprobación de nulo, y ningún llamador comprueba el resultado.

### 1.2 Los bytes `+0x1E8` y `+0x1E9` del CC

**[E]** El constructor `0x85CB00` los pone a 0 (`0x85CC60: mov [rdi+0x1E8], rcx`, con `rcx=0`).
- `+0x1E9` significa «lo controla el jugador local».
- Se fijan con un setter en línea:
  - `word [CC+0x1E8] = 0x0101` y llamada a `0x8693F0` (adquirir el control; `jmp .xtls 0x985EF50`);
  - `word = 0x0001` y llamada a `0x869630` (soltarlo).

### 1.3 Llamadores

| Llamador | Qué es | Notas |
|---|---|---|
| `0x77F2A0` (`0x77F4AF`, `0x77F5E1`, `0x77F6D0`, `0x77F7EB`) | Cambio de personaje jugable: la lista `+0x270` y la clave actual `+0x130` | `0x77F4AF` y `0x77F5E1` sueltan al personaje anterior. `0x77F6D0` y `0x77F7EB` vienen después de escribir `0x0101`, así que en la práctica llaman a `0x8693F0` [E] |
| `0x8B9670` (`0x8B97A0`, `0x8B98E6`, `0x8B9A01`) | Por frame, en el `ClientActorManager`, el **conjunto con control local** = principal del usuario local (`[mgr+0x58]+0xD8`, con `+5E&0x10`) + **su montura**, por el vínculo `[[actor+0x68]+0x48]+0x18`: clave `+0x10`, asiento `+0x14 == 0` | `0x8B97A0` y `0x8B98E6` son inalcanzables, porque `+0x1E9` acaba de ponerse a 1. **`0x8B9A01`** suelta las claves de la lista anterior `+0x380` que ya no están [E] |
| `.xtls 0x984A800` | Dentro de `0x984A620`, el **`vf6` del CC** (vtable `+0x30` → thunk `0x85DCC0`) | `0x984A7F4: cmp byte [rdi+0x1E9],0 ; je ; call 0x869630`. Luego `CameraManager::0xAAF660(&clave)` y `0x2DF9AE0` [E] |

**[E] Qué es `vf6`.** Se llama al retirar el componente:
- `0xE0DB5A0` recorre los componentes del actor (`actor+0x78`, cuenta `+0x80`) en orden inverso y
  llama al `vf6` de cada uno (`0xE0DB5F9: call [rax+0x30]`).
- La misma conclusión para otro componente: `copias-grupo-invisibles.md` §2.1.

### 1.4 La global `0x6D691B8`

**[E]** `0x6D691B8` apunta a un contexto del cliente (A).
- Se escribe una sola vez, en `.xtls 0xA027D59`: `mov [rip…], rbx`, dentro de un inicializador que
  rellena `+0x98…+0xC8`.
- Las 378 lecturas de `.rsrc` son todas del tipo `[A+off]`.

**[E] `[A+0]` es `pa::CameraManager`** (vtable `0x55CA3B8`; constructor `0xAAAE90`).

| Campo | Qué es |
|---|---|
| `+0x20` / `+0x28` | Pila de tipos de cámara |
| `+0x30` | FreeCam |
| `+0x38` | Photo |
| **`+0x40`** | Manejador de la entidad **«PlayerCamera»** |
| `+0x48` | Otra cámara |

**[E] Quién escribe `+0x40`:**

| Función | Qué hace |
|---|---|
| `CameraManager::Init` `0xAAB0E0` (`0xAAB515: mov [r15+0x40], rbx`) | Lo crea. Se llama solo desde `0xACD9E0`, la inicialización del objeto de juego |
| `CameraManager::Shutdown` `0xAABA60` | Lo vuelve nulo con `cdmp_AddNodeId_ReleaseLookup(this+0x40)`. `0x38AD90` escribe `*p = 0` |

- Fuera de esas dos funciones no hay más escrituras directas en `0xAA0000–0xAB4000`.
- No se rastrearon liberaciones indirectas por `&B+0x40` en el resto del ejecutable.

**[E] Cuándo se llama `0xAABA60`:**
- Solo desde `0xACD481`, en `0xACD400`.
- `0xACD400` se llama solo desde `0x61A520`: eliminar el objeto de juego.
- A su vez lo llaman:
  - el fin de la aplicación: `0x615910` (dtor de `pa::Application`), `0x6170E0` y `0x3F40F30` (dtor
    de `WinApp`);
  - `0x616D40`, desde `0x3F44930`, el `WinMain` del juego: inicializar, ejecutar el bucle `vf+0x40`
    y borrar.

Comprobado también por bytes en todas las secciones ejecutables.

**[E] Respuesta.** `CameraManager+0x40` solo es nulo antes de `Init` o después de `Shutdown`, es
decir, mientras se destruye el objeto de juego.
- El código nativo lo da por existente en todo el juego: `0x869630`, `0xAAE400`, `0x395E03`,
  `0x6A2D0E` y unas 20 lecturas más con el mismo patrón.
- En un jugador solo, `0x869630` únicamente puede encontrarlo nulo si se retira un CC con control
  local después de apagar la cámara.

## 2. El primer fallo: `0x17F328F`, durante `CharacterDataDefinedStatAck`

**[E]** `0x17F3240(actor)` (tamaño `0x96`, un único llamador):

```
0x17F3246  mov rax,[rcx+0x68] ; mov rsi,[rax+0x40]        ; CC
0x17F324E  mov rcx,[rsi+8] ; call [vf+0x108] ; test al,al ; jz fin
0x17F325F  mov rax,[rsi+0x30] ; movzx ecx,[rax+0x1B]      ; índice de la capa principal
0x17F3283  mov rax,[rsi+rcx*8+0x88]                       ; capa (CC+0x88+8·i, como horse_diag)
0x17F328B  mov rcx,[rax+0x18]                             ; reproducción («play(+18)»)
0x17F328F  mov rax,[rcx+0x38]      ← AV con rcx = 0
0x17F3296  movzx ebx,[rax+0x76] ; call 0x1EC82E0 ; escribe el factor en [rdi+0x10/+0x14]
```

**[E] El llamador** es `0x630BF0`, la aplicación de la Ack: lo llama
`cdmp_troc_CharacterDataDefinedStatAck_Handler` `0xB74AB0` (opcode **3095**, S→C).
- Para cada estadística recibida, si su definición tiene `+0x18`, llama a `0x17F3240`.
- Solo lo hace para los tipos de actor `[[actor+0x88]+1]` 1, 3, 4, 5, 6, 8 y 9. **El hijo es de
  tipo 6.**

**[V] Lo que muestran los logs:**
- El hijo `0xB0100244` (12:01) y `0xB0100246` (12:06) tiene `L0 … play(+18)=false chart=true`
  desde que se crea hasta la despedida.
- Es el único actor del log con la reproducción nula. Kliff, el del usuario 2, tiene `play=true`.
- El fallo llega unos 1,0–1,1 s después de entregar la 2001. **[S]** El servidor manda una 3095
  sobre el hijo, o sobre los actores del usuario que se va, durante su logout nativo.

**[S] Por qué D-52 no lo evita.** D-52 salta `0x85FBA0`, `0x860D30` y `0x2329700`, pero
`0x630BF0` es un manejador de red que se ejecuta en otro punto del frame.

**[S] Por qué nunca pasó con el hijo rechazado.** Sin hijo no hay actor con la reproducción nula.

## 3. El segundo fallo: el juego se cierra con el mundo aún cargado

### 3.1 La pila, marco a marco

**[E][V]** Cada fila se comprobó con el tamaño del marco de cada función:

| Pila | Valor | Comprobación |
|---|---|---|
| +0x00 | `0x869882` | Retorno de la llamada a `0xAAFC00` |
| +0x78 | `0x984A805` | `0xAAFC00` empuja `0x28` y `0x869630` ocupa `0x48 + ret` → +0x78 ✓ (retorno del `vf6` del CC) |
| +0x1C8 | `0xE0DB5FC` | `0x984A620`: 3 push + `0x130` → +0x1C8 ✓ (`0xE0DB5F9 call [rax+0x30]`, el bucle de `vf6`) |
| +0x258 | `0x17E9624` | `0xE0DB5A0`: 3 push + `0x70` → ✓ (`0x17E9600`, destrucción base, llama al thunk `0x20683F0`) |
| +0x288 | `0x8C0C58D` | `0x17E9600`: push + `0x20` → ✓ (`0x8C0C560` = `vf34`, `+0x110` de `ClientChildOnlyInGameActor` `0x5585C40` y otras dos vtables) |
| +0x348 | `0x8AAFCD` | `0x8C0C560`: 5 push + `0x90` → ✓ (`0x8AAFC7 call [rax+0x110]`) |

Los valores `0xE6D55EA` (+0x118) y `0x463C36` (+0x218) son restos de llamadas anteriores dentro de
los mismos marcos. `0xE6D5420` es el `vf6` del componente de vehículo, que se ejecuta antes en ese
mismo bucle.

### 3.2 La destrucción de los actores

**[E] `0x8AADB0`** destruye todos los actores registrados en
`[mgr+8]` = `+0x48` (pendientes de login), `+0x58` (con login hecho) y `+0x28`:
`vf+0x80` y después `vf34`, salvo a los que estén en el estado `0x20` o `0x40` (ya en basura).
- **El actor del fallo venía de `+0x58`.** El bucle empieza en `0x8AAF22: mov rsi,[rax+0x58]`.
- `0x8B7B50` pasa ahí a los actores al terminar su login (`0x8B8BC3`).
- `0x8AADB0` se llama solo desde `0x93FE40`, que solo llama `0xACD400`.

**[E] El orden dentro de `0xACD400`:**
1. `CameraManager::Shutdown` (`0xAABA60`).
2. `PublishLogoutSession` (`0x8CAD10`).
3. Una espera de hasta 30 × `Sleep(100)` a que se liberen las ranuras de red.
4. `0x93FE40`, que lleva a `0x8AADB0`.

### 3.3 Qué actor era y por qué la cámara ya no estaba

**[S] El actor del segundo fallo** tiene control local (`+0x1E9 ≠ 0`). Solo `0x8B9670` y
`0x77F2A0` lo ponen a 1, para el principal local y su montura; el constructor lo deja a 0. Así que
casi seguro es **Kliff del anfitrión** (o su montura), no el hijo del usuario 2. Falta confirmarlo
(§6, P4).

**[S] La salida normal no pasa por aquí.** No hay ningún evento `0xAAFC1D` esta semana salvo los de
estos dos procesos. Quizá porque los cierres normales salen desde el título, sin actores con
control, o porque se cierran matando el proceso. Ese orden nativo solo hace daño cuando la
aplicación se cierra con el mundo cargado, como ocurre tras un fallo no manejado.

**[V] El mismo patrón aparece en otras pruebas:** primer fallo y, 5–10 s después, otro en el mismo
proceso.

| Hora | Proceso | Fallos |
|---|---|---|
| 06:49:22 y 06:49:27 | `0x4864` | `0x1831598` |
| 06:53:48 y 06:53:53 | `0x5BFC` | `0x1831598` |
| 12:01:42 y 12:01:50 | `0x7988` | `0x17F328F`, luego `0xAAFC1D` |
| 12:06:26 y 12:06:36 | `0x7E2C` | `0x17F328F`, luego el trampolín |

**[S] El camino probable:** el primer fallo lo recoge el manejador del bucle principal (el
reportero Sentry, con `crashpad_handler` apartado por K2), la aplicación termina y se ejecuta la
destrucción.

**[V] Encaja con los tiempos:** a las 12:06 el log sigue vivo hasta 32.3; `CompBadThis` llega a
35.308, exactamente 3,0 s después de la última línea (32.308). Esos 3 s son el máximo de la espera
de `0xACD400`.

## 4. Respuesta a la pregunta 2: ¿saltar la actualización o retirar el actor?

**[E] Ninguna de las dos.**
- La cámara no se registra en la primera actualización del CC:
  - `0x85FBA0` solo lee `[A+0x10]`;
  - `0x860D30` con `1E8 && 1E9` llama a `0x9471C0(WorldSystem+0x20)`;
  - `+0x40` lo escriben solo `Init` y `Shutdown`.
- La retirada nativa del hijo al hacer logout (`0x463780` → listas de basura `+0x38`/`+0x68` → estado
  `0x20/0x40` → `collectGarbageActor` `0x8B48B0`) no pasa por `0x869630`. El `+0x1E9` del hijo es 0.
  - **[V]** A las 12:01 y a las 12:06 dejan de verse `update real 0xB01…` y `USER 2` justo después
    de la 2001.

**[E][V] La causa es la de siempre:** un actor remoto creado con chart y sin acción.
- `restore byte 0`.
- `8646D0` con 0 no ejecuta `RestartLayerAction`.

Lectores nativos que han fallado con ese estado, todos en el registro de Windows:

| Dirección | Qué lee |
|---|---|
| `0x232D5FF` | La acción actual `[capa+0x10]` |
| `0x88F832` | Desde `PostUpdate` |
| `0x1F63FE6` | Reproducción vacía |
| `0x1831598` | `[capa+0x10]+0x10B`; 10 llamadores, entre ellos el minimapa `0x9E8D10` |
| **`0x17F328F`** | `[capa+0x18]+0x38`; red, 3095 |

Ninguno comprueba el nulo: para el motor, una capa con chart tiene acción.

## 5. Arreglo de raíz propuesto (pregunta 3)

**[E][V] El contrato nativo:** quien controla un actor le da su primera acción enseguida.
- Los hijos propios mandan su primera Req2223 a 0,372–0,386 s (112627).
- Los NPC reciben su primer Ack a unos 11,6 ms (C1g).
- Si la primera acción llega antes de la instantánea, el 2005 lleva restauración 1 y `0x8646D0`
  arranca las capas con `RestartLayerAction → AfterRestoreAction → 0x2329D70`. Así se arregló Kliff
  en D-48.

**[S] Propuesta, por orden:**

1. **Barrera de activación en el servidor**, no en el cliente que recibe. Para un hijo nuevo de un
   usuario remoto:
   - no enviar su 2005 a los demás usuarios (el cliente del anfitrión) hasta que el servidor haya
     aceptado su primera acción (2223 → 2723);
   - entonces se serializa con restauración 1;
   - si no llega en el plazo (propuesta: 3 s), no se publica, y el hijo queda solo en el campo del
     usuario 2.

   Es lo que ya ocurría «por accidente» cuando el cliente rechazaba el hijo, y nunca hubo cierres. El
   punto de retención en el emisor del servidor no está verificado (companeros.md §B.1: falta su
   emisor de instantáneas); hay que localizarlo antes de escribir código. La retención en `0x35D2A0`
   del cliente (companeros.md §B) es la alternativa. Con ella, el 2005 y el primer 2723 tienen que
   aplicarse en el mismo despacho, antes de que llegue un 3095.
2. **El controlador le da la acción.**
   - En las pruebas con `cdmp_peer`: el guion manda la primera Req2223 del hijo en cuanto el
     servidor lo crea, con la acción capturada del hijo equivalente de 112627 (por tipo y ranura). Es
     el equivalente de D-48 para el hijo.
   - En P8 la manda la IA del cliente real.
3. **No** fabricar en el cliente una acción ni una reproducción, ni forzar el byte de restauración
   (companeros.md §B.2, revisión punto 3). **No** tocar `0x869630` ni `0xAAFC00`: su suposición es
   correcta en todo el juego. El segundo fallo desaparece si no hay primero.

**Contención mientras tanto** [S]. Es defensa en profundidad, no raíz:
- Extender el predicado de D-52 con un mid-hook en la entrada de `0x17F3240`
  (`40 56 48 83 EC 20 48 8B 41 68 48 8B 70 40`).
- Condición: si `owned_by_guest(actor)` y la reproducción `[[CC+0x88+8·idx]+0x18]` es nula, volver
  sin hacer nada. Solo se pierde el factor de velocidad de esa estadística.
- No basta sola: `0x1831560` tiene 10 llamadores, y es la razón para hacer el arreglo 1 o 2.

## 6. Qué hay que verificar en el juego: sonda de una sola prueba (solo lectura)

Todo en `.rsrc` y en la entrada de funciones reales. Comprobar los bytes antes de instalar.

| Punto | Bytes de la entrada | Qué registrar |
|---|---|---|
| **P1** `0x17F3240` | `40 56 48 83 EC 20 48 8B 41 68 48 8B 70 40` | Solo si `owned_by_guest` o la reproducción es nula: clave `[rcx+0x60]`, dueño `[rcx+0x90]`, tipo `[[rcx+0x88]+1]`, `+5E`, `idx=[[CC+0x30]+0x1B]`, la capa y sus `+0x10`/`+0x18`, `[rsp]` (se espera `0x630Dxx`), hilo y hora |
| **P2** `0x630BF0` | `48 89 54 24 10 55 53 56 57 41 54` | Actor `[[rcx+8]+0x60]` y su dueño; número de entradas `u32 [r8+8]`; los primeros ids `u32 [[r8]+0x30·k]` y su valor `[+8]`; hilo y hora |
| **P3** `0xACD400` | `4C 8B DC 49 89 5B 18 49 89 4B 08 55 56` | Hora, hilo y `[rsp]` (`0x61A53D`). Luego `[[rsp+…]]` hasta `0x615B2A` o `0x3F40F7E` |
| **P3** `0xAABA60` | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 54` | Hora, hilo, `[rsp]` |
| **P3** `0x8AADB0` | `48 89 5C 24 18 55 56 57 41 54` | Hora, hilo, `[rsp]` |
| **P4** `0x869630` | `48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 30` | CC `rcx`; actor `[rcx+8]` con clave, dueño, tipo y `+5E`; `CC+0x1E8`/`0x1E9`; la cámara `[[[0x6D691B8]]+0x40]`; `[rsp]` (`0x984A805` es la retirada; `0x8B9A06` es soltar el control por frame) |

Al terminar, apuntar los eventos del sistema:
`Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=…}`, buscando
`CrimsonDesert`.

**Resultado esperado si el diagnóstico es correcto:**
1. P2 con el hijo `0xB01…` unos 1 s después de la 2001.
2. P1 con la reproducción a 0 para esa misma clave, justo antes del evento `0x17F328F`.
3. P3 (`0xACD400`, `0xAABA60`, `0x8AADB0`) unos 3–7 s después, en el hilo principal.
4. P4 con la clave del anfitrión `0xA0100001` (o su montura), `1E9=1` y la cámara a 0, justo antes
   de `0xAAFC1D`.

**Resultados que lo refutarían:**
- P4 con `1E9=1` para una clave del usuario 2, o la cámara distinta de 0. Habría que buscar quién
  puso ese `1E9`: añadir P5 en `0x8B9785` y `0x8B98CB` (setters de `0x8B9670`) y en `0x77F6B4` y
  `0x77F7D4`.
- P3 antes del primer fallo, o sin primer fallo: entonces el juego lo cerró otra cosa, por ejemplo
  el operador.

Hay que confirmar también con quien hizo las pruebas que no cerró el juego a las 12:01:46 ni a las
12:06:32.
