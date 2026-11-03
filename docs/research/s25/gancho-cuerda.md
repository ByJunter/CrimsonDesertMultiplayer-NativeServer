# El gancho con cuerda azul (RemoteCatch) del invitado y del anfitrión (S25): por qué se ve mal

12 de octubre de 2026, rama `opcion-d`. Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`),
direcciones en RVA. §0–9 son la investigación (sin tocar `src/` ni lanzar el juego); lo implementado está en §10 y
la primera prueba en el juego (anfitrión solo) y el lanzamiento, en §11.

- **[V]** comprobado (captura, log del mod o decompilado/desensamblado de esta build, citado al lado).
- **[E]** deducción fuerte que falta medir en vivo.
- **[H]** hipótesis.

## Fuentes y relojes

| Id | Fichero | Qué |
|---|---|---|
| G1 | `…\scratchpad\s25-shadow\cdmp_packets_20261012-054022.bin` | invitado, 1.ª parte (personaje `0xA010034E`). 5 `RemoteCatchResultAck`: 4 suyos y 1 del anfitrión |
| G2 | `…\scratchpad\s25-shadow\cdmp_packets_20261012-060451.bin` | invitado, 2.ª parte (`0xA020280A`). 1 `RemoteCatchResultAck` suyo |
| H | `C:\Emulador CD\backups\p8-sesion25-host\cdmp_packets_20261012-063747.bin` + `cdmp_20261012-063736.log` | anfitrión, paso 1 (`0xA0100001`). Los 6 anteriores |
| H2 | `…\cdmp_packets_20261012-071449.bin` | anfitrión, paso 2 (`catch_seq=1`): **ningún** gancho |

- t(H) = t(G1) + 171,35 s = t(G2) + 1635,05 s (mismas anclas que `finisher-sigilo.md`). Pared del anfitrión =
  06:38:00,502 + t(H).
- Las otras capturas de S25 (061845, 062330, 062806, 063009) y las de Shadow del 5 al 7 de octubre (opción C,
  marioneta `0xA010F001`) no aportan episodios de la opción D.
- Scripts (Python `-I`) en `…\scratchpad\gancho\` (§9).

---

## 0. Resumen

1. **Qué es [V].** El gancho es un *remote catch*. El cliente del que lanza manda `StartActionReq 9A368460` y un
   `DoFrameEventReq` de 636 B (variante 6) con el objetivo. El servidor lo copia tal cual en
   **`RemoteCatchResultAck` 2763**, que va por `ToNear` **a los dos clientes**. Cada cliente dibuja la cuerda
   aplicando ese paquete en el componente `RemoteCatch` (`[actor+0x68]+0x78`) del que lanza: `0xB5B710` →
   `0x592340` → `0x594AD0`. **La suelta no tiene paquete del servidor para un árbol.**
2. **Defecto 1: el objetivo va identificado por un número local de cada PC [V].**
   - El bloque de objetivo lleva el id de escena del árbol (16 B, estable entre PCs) **y** un *handle* de cuerpo
     físico (`u64` en `+0x1E`, `0x80000000_0W00NNNN`; p. ej. `0x800000000100069F`). El punto del gancho va **en el sistema de ese cuerpo**.
   - El cliente que lanza saca el handle de **su** física (`[[obj+0x58]+0xE0]->vf70`, `0x5947AB–0x594826`). El
     observador lo resuelve contra **la suya** (`[WorldSystem+0xE8]->vf3B0(handle)`, `0x5A5370`) y le sale otro
     cuerpo.
   - Prueba directa: **el mismo árbol** (escena `0x090000DE6690FC82`) es el handle `…069F` en el PC del invitado y
     `…0C2C` en el del anfitrión. Los dos estaban a 1,6 m el uno del otro.
   - El tiempo de vuelo de la cuerda (`distancia / 45 m/s`, `0x5A12A0`) lo confirma. En quien lanza tarda
     0,25–0,40 s (11–18 m). En el observador, 4,2 s (**189 m**: «hacia un edificio lejano»), 4,9 s (222 m) y
     **263,6 s (11,9 km)**: «un láser volando por todas partes».
3. **Defecto 2: el observador nunca suelta la cuerda [V].**
   - La suelta es un *frame event* del cliente (`ClientFrameEventReleaseRemoteCatch` `0x7F8F40`) que solo corre en
     el cliente que controla al personaje.
   - El servidor (`0x27D9110`) solo manda `ReleaseRemoteCatchResultAck` 2471 cuando el objetivo es un **actor**
     (`0x2903950`, `0x2902DC0`), nunca para un árbol. 0 paquetes 2471 en S25.
   - Consecuencias: la cuerda del observador sigue en el aire y su ranura queda ocupada. El siguiente gancho de ese
     personaje falla con `"모종의 이유로 클라에서 RemoteCatch에 실패했습니다…"` (`exe+0x592536`): es la comprobación
     «ranura vacía» de `0x1808100`.
4. **No es lo de los remates.** `catch_seq` (`0x935DD0`, 2156) no toca este camino. Aquí no hay byte de secuencia:
   hay un número local que traducir y una suelta que falta.
5. **Arreglo propuesto (§5, sin protocolo nuevo).** Clave `[session2] remote_rope = 0 | probe | 1`, en **los dos
   PCs**.
   - **(F1)** Traducir en el observador el handle con la misma correspondencia del juego: id de escena → objeto →
     cuerpo → handle **local**. Gancho a la entrada de `0x592340`.
   - **(F2)** Soltar en el observador cuando el servidor procesa la suelta de quien lanza. **Implementado** (§10.2 y
     §10.2.1): en el anfitrión, tras la suelta del servidor (`0x27D9110`) o el cambio de acción que la vacía en un
     lanzamiento (`0x1807390`), el servidor manda el 2471 nativo con `ToNear(lanzador)`, como ya hace para los
     objetivos actor.
   - **(F3)** Limpiar una ranura que se haya quedado sucia, para que nunca vuelva a bloquear el siguiente gancho.
6. **Se puede reproducir en un PC [E].** Hay guiones `cdmp_peer --mode replay` cortados de G1 con el gancho real
   del invitado (§8), que contestan al `ChangeActionSessionIDAck` como el cliente real (§10.5: por eso no salió el
   2763 el 12-10 19:04). Lo que solo da Shadow está en §8.3.

---

## 1. Todos los ganchos de S25 (pregunta 1)

### 1.1 Secuencia de un gancho (G1 138, invitado) [V]

```
138.292 C>S StartActionReq  A010034E  A1 B0983640                 (apuntar; antes 136.958/137.099 FA732C46)
138.480 C>S StartActionReq  A010034E  A0 9A368460                 (lanzar)  + DoFrameEventReq 9A368460 f14..f19 (517 B)
138.496 C>S StartActionReq  A010034E  A1 45F18D37
138.565 C>S DoFrameEventReq A010034E  9A368460 f20, 636 B, variante 6 (+0x203): el objetivo
138.618 S>C RemoteCatchResultAck 2763 (97 B, ToNear: le llega igual al otro cliente)
138.639 C>S IsRemoteCatchEffectStartStateReq 2108  00 01        (la cuerda sale: estado 2)
138.651 C>S StartActionReq  2821F20A
139.004 C>S IsRemoteCatchEffectStartStateReq 2108  00 00        (la cuerda llega: estado 3; 0,365 s ≈ 16 m)
139.016…160 SA A747BF2F, 7CC69D61 cada 1,36 s (sujetar), D9C333C4, 76D8C2D8, 6B4EE3CA, 563271FE ×5 (tirar)
170.146 C>S StartActionReq  D20295AD + 170.304 DoFrameEventReq D20295AD f3 (517 B)  (soltar)
        —   ningún paquete del servidor por la suelta
```

- El otro cliente recibe solo el 2763 y las `StartActionAck` de cada acción. En H llegan
  `9A368460`/`2821F20A`/`A747BF2F`/`7CC69D61` a partir de 309,933 y **`D20295AD` en 341,599** (`acks.py`).
- El gancho del anfitrión (H 366,571–372,742) es idéntico, con el mismo `D20295AD f3` para soltar.
- **Fallo propio (sin 2763):** G1 1193,926, `9A368460` f14–19 sin f20 y `2108 00 01` sin `00 00`. El anfitrión no
  recibe nada.
- **Lanzamiento** (`1F4EF471`, el «salir volando»): no hay ninguno en S25. Todas las sueltas son `D20295AD f3`.
  **Corrección (§11):** su `1F4EF471 f5` **no** es un *frame event* de suelta, sino uno de IA (variante 3). La ranura
  del servidor la vacía el **cambio de acción**, sin paquete para un árbol.

### 1.2 Formato del 2763 (lector `0xB5B710` + `0x1806940`) [V]

| Off | Campo | Valor en S25 |
|---|---|---|
| `+0x00` | u32 quien lanza | `A010034E`, `A0100001`, `A020280A` |
| `+0x04` | u8 ranura (0–1) | 0 |
| `+0x05` | u8 tipo de remote catch (`switch` de `0x592340`/`0x594AD0`: 0 normal, 1–3 actor, 4 ancla) | 0 |
| `+0x06` | 3 × u8 | `01 01 00` |
| `+0x09` | u8 **clase de objetivo** (`0x5A5370`: 0 actor+hueso, 1 cuerpo de actor, 2 cuerpo / objeto de escena) | 2 |
| `+0x0A` | u32 clave de actor objetivo | 0 |
| `+0x0E` | **id de escena** (u64 objeto + u64 nivel `0x0100FE0F`), estable entre PCs | ver tabla |
| `+0x1E` | **u64 handle de cuerpo físico, local de cada PC** | ver tabla |
| `+0x26` | vec3 **punto en el sistema de ese cuerpo** | ver tabla |
| `+0x32` / `+0x3E` | vec3 (0) / quat | |
| `+0x4E` | cadena (u32 + NUL), vacía | |
| `+0x53` / `+0x57` | u32 `0x85FB13C7` / **f32 velocidad de la cuerda 45,0** | |
| `+0x5B` / `+0x5C` | u8 / cadena vacía | |

### 1.3 Los seis episodios y lo que pasó en cada cliente [V salvo marca]

Para el vuelo de la cuerda en cada cliente se mide el intervalo `2108 00 01 → 00 00` de **ese** cliente. Como
`[comp+0xA94] = |destino − mano| / 45` (`0x5A12A0`), el intervalo es directamente la distancia que calculó ese
cliente.

| t(H) / pared | Quién lanza | Escena · handle del que lanza | Punto `+0x26` | Quien lanza: vuelo | **Observador** |
|---|---|---|---|---|---|
| 310,014 / 06:43:10 | invitado (G1 138,618) | `DE6690FC82` · `…069F` | (−0,09; 9,62; −0,19) | 0,365 s ≈ 16 m | anfitrión: **4,196 s ≈ 189 m**. Suelta del invitado relevada (341,599) sin efecto |
| 366,675 / 06:44:07 | **anfitrión** (G1 195,289) | `DE6690FC82` · **`…0C2C`** | (−0,02; 8,80; −0,10) | 0,396 s ≈ 18 m | invitado: **4,931 s ≈ 222 m** |
| 388,489 / 06:44:29 | invitado (G1 217,119) | `DA2C553AD9` · `…0AD5` | (0,16; 5,43; −0,64) | 0,299 s ≈ 13 m | anfitrión: **`RemoteCatch에 실패`** (ranura aún ocupada por la de 310) **[E]** |
| 1152,706 / 06:57:13 | invitado (G1 981,341) | `DE6690F883` · `…084E` | (−10683,5; 618,5; −3895,8) **(mundo)** | 0,246 s ≈ 11 m | anfitrión: **263,584 s ≈ 11,9 km**. Suelta del invitado en 1154,884, sin efecto; «llega» en 1416,311 |
| 1229,211 / 06:58:29 | invitado (G1 1057,859) | `DE6690F883` · `…084E` | (−10682,5; 618,2; −3893,5) | 0,173 s ≈ 8 m | anfitrión: **`RemoteCatch에 실패`** (la de 1152 sigue volando) |
| 2127,204 / 07:13:27 | invitado (G2 492,185) | **0** · `0x8000000002000F02` | (12,4; 569,4; 24,8) | 0,114 s ≈ 5 m | anfitrión: **`RemoteCatch에 실패`** (personaje nuevo, ranura libre: no puede resolver el handle) **[E]** |

- **Mismo árbol, distinto handle.** La escena `0x090000DE6690FC82` es `…069F` en el invitado y `…0C2C` en el
  anfitrión.
  - En 310 y 366 el anfitrión estaba en (−10747,4; 615,8; −3938,8) y el invitado en (−10748,7; 615,7; −3939,7)
    (último `MoveActorReq` de cada uno, `pos.py`). Estaban juntos, con el árbol cargado en los dos PCs: el
    anfitrión lo enganchó él mismo 56 s después.
- **El modelo de vuelo cuadra con la geometría.** En 981, el punto es de mundo (cuerpo `…084E` con transformada
  identidad en el invitado) y el invitado estaba en (−10690,7; 617,5; −3886,5). Eso son 11,75 m de los pies al
  punto, frente a los 11,1 m que dan 0,246 s a 45 m/s.
- **Errores en el log del anfitrión.** Hay tres pares `RemoteCatch에 실패` (`exe+0x592536`) +
  `RemoteCatchResultAck처리에 실패` (`exe+0xB5BBC9`), a las 06:44:29,011, 06:58:29,728 y 07:13:27,713. Caen a
  +20 ms de los tres 2763 marcados. En el invitado no hay ninguno.
- **Ningún 2471 en S25.** Las sueltas del invitado sí llegan relevadas como `StartActionAck D20295AD` (H 341,599,
  392,562, 1154,884, 1231,967), pero no cambian nada en el anfitrión.

### 1.4 Los episodios del informe del usuario [E sobre la imagen; V sobre los datos]

- **«Lanzó la cuerda hacia un edificio lejano»** es H 310,014 (06:43:10). El cliente del anfitrión unió la cuerda
  a **su** cuerpo `…069F`, que es otra cosa a unos 189 m del invitado.
- **«Al soltar, la cuerda se quedó en el aire como un láser volando por todas partes»** es H 1152,706 (06:57:13).
  - El punto de mundo (−10683; 618; −3896), transformado por el cuerpo `…084E` del anfitrión (otro objeto), cae a
    ~11,9 km.
  - La cuerda estuvo **264 s** saliendo de la mano del invitado hacia ese punto. Cada movimiento del personaje la
    barría por el cielo.
  - La suelta del invitado (06:57:15) no la quitó. Después de «llegar» (07:01:36) siguió puesta.
- **«Funciona mal» en general:** 3 de los 5 ganchos del invitado ni siquiera se dibujaron en el anfitrión (las
  tres líneas `RemoteCatch에 실패`).

---

## 2. Cómo construye la cuerda el cliente observador (pregunta 2)

### 2.1 Camino del 2763 (decompilado y desensamblado de esta build) [V]

```
0xB5B710 RemoteCatchResultAck::vf2 (cola de Acks)
  busca a quien lanza por clave; comp = [[catcher+0x68]+0x78]
  0xB5BB7B call 0x592340 RemoteCatchComponent_Apply(comp, &err, &slot, &kind, &3B, &info, &hash, &speed, u32, &name)
    switch(kind): 0 → (nada); 1-3 → busca el actor objetivo; 4 → ancla; 5/otros → error
    0x5924EB call 0x594AD0(comp, slot, struct, &hash, speed, …)
      0x1807A40 → 0x1808100: si byte [comp+0x20+slot*0x58] != 6 (ranura NO vacía) → return 0
                              si no, copia el objetivo en la ranura
      case 0: guarda punto/quat locales en comp+slot*0x3E0+0x1DC / +0x1CC
      0x5A40E0 → 0x5A5370(info): transformada de mundo del objetivo; si falla → return 0
         clase 2, handle != -1: [[WorldSystem]+0xE8]->vf3B0(handle, &xf)  (sin cuerpo → false)
                                 destino = xf * punto local
         clase 2, handle == -1: id de escena → ResolveUuidLookup → transformada del objeto (o mundo si id 0)
      hash != 0 → 0x5ABBC0 → 0x5A12A0: estado [comp+0x9BC+slot*0x1A0] = 2 (vuelo),
                  [+0xA94] = |destino − mano| / speed, manda 2108 (0xB56B00, sin clave)
      createRemoteCatchRopeBody (script) con el destino; cdmp_Component_RegisterTick
    si 0x594AD0 devuelve 0 → GameErrorLogger "모종의 이유로 클라에서 RemoteCatch에 실패했습니다…" (ret 0x592536)
  err != 0 → "RemoteCatchResultAck처리에 실패했습니다." (ret 0xB5BBC9)
```

- **Tick:** `0x59C0D0` → `0x59D470`. Lo llama `FUN_1408A1640` (`0x8A1697`) para todo actor con el componente.
  Por ranura:
  - estado 2 → `0x59E150`: al terminar el vuelo, `0x5A1530` pone el estado 3 y manda `2108 00 00`;
  - estado 3 → `0x5A40E0`: si el objetivo deja de resolverse, suelta.

  Que los `2108 00 00` del anfitrión en 314,228 y 1416,311 salieran demuestra que este tick **sí corre** en el
  anfitrión para el personaje remoto.

### 2.2 Cómo identifica el objetivo quien lanza [V]

`ClientFrameEventRemoteCatch::vf1` `0x7F88B0` → `0x593B50`. Para un objeto de escena convierte a clase 2 así
(`0x5947AB–0x594859`):

```
mgr = [[[0x146D69190 WorldSystem]+0xC8]]+0x80
0x1851010 ResolveUuidLookup(mgr, &att, &idEscena)        ; att = SyncSceneObjectDataAttacher
obj = [att+8]; body = [obj+0x58]
si body && byte[body+0x15] != 1 && [body+0xE0]:  handle = [[body+0xE0]]->vf(+0x70)()   ; el MISMO vf70 que da el
                                                                                         ; handle de cuerpo de un actor
att.vtbl = ScopeAttacherBase 0x5725E48; 0x1434880 FieldAttacher dtor(&att)
si handle == -1 → no hay gancho
```

- El servidor (`0x27D8EB0` → `0x2902DC0` → escritor `0x2968330`) **copia el bloque sin tocarlo**. Su única
  comprobación es su propia ranura (`0x1807A40`).
- Así, el número de `+0x1E` vale solo en el PC que lo generó. La correspondencia estable id de escena → cuerpo
  existe en los dos PCs, pero el observador no la usa mientras el handle no sea −1.
- Esto corrige `interacciones-mundo.md` R1 y el comentario de `offsets.h:171` («el árbol se identifica solo,
  porque el id de escena es del nivel»): el campo que de verdad usa el cliente es el handle.

### 2.3 Qué comprueba `exe+0x592536` [V]

No es una comprobación de secuencia como la de `0x935DD0`. Es el error que sale cuando `0x594AD0` devuelve 0:

1. **Ranura no vacía** (`0x1808100`): `byte [comp+0x20+slot*0x58] != 6`. La vacía es `0x10106` (`0x1806B20`), y
   solo la vuelve a poner la suelta (`0x5A7BC0` → `0x1807C80`).
2. **Objetivo irresoluble aquí** (`0x5A40E0`/`0x5A5370`): `vf3B0(handle)` no encuentra cuerpo, o el id de escena
   no está cargado. Esto pasa **después** de que `0x1808100` haya copiado el objetivo en la ranura, y nadie la
   libera. Es el defecto 3: la ranura queda ocupada sin cuerda.
3. `kind > 4` (no visto).

En S25, 388,489 y 1229,211 son el caso 1, y 2127,204 el caso 2 **[E]**: personaje recién cargado, sin 2763
previo.

### 2.4 Por qué la cuerda nunca se suelta en el observador [V]

- **En quien lanza:** `D20295AD f3` ejecuta `ClientFrameEventReleaseRemoteCatch::vf1` `0x7F8F40`. Este manda su
  `DoFrameEventReq` (`0x4EC430`) y llama a la suelta `0x5A7A10(comp, slot, b9, b10, 1)`.
- **En el observador:** los *frame events* de acción del personaje remoto no se ejecutan. El anfitrión no mandó
  **ningún** `DoFrameEventReq` de tipo 0 con las claves del invitado; los 25 de `0xA020280A` son de tipo 1, de
  otra familia (`dfecount.py`, `dfeof.py`).
  - Recibe la `StartActionAck D20295AD`, pero la ranura sigue igual. Prueba: la cuerda de 1152,706 seguía en
    vuelo (estado 2) en 1416,311, 261 s después de la suelta relevada de 1154,884.
- **En el servidor:** `ServerFrameEventReleaseRemoteCatch::vf1` `0x27D9110` libera **su** ranura. Solo hay dos
  caminos al escritor de 2471, `0x29688C0`:
  - `0x2903950`, vf148 del componente del **actor objetivo**, cuando su clave guardada coincide;
  - `0x2902DC0`, tipo 1 con objeto inválido.

  Para un árbol (clase 2, actor 0) no sale ninguno. Así que en la opción D un observador no tiene ningún camino
  nativo para soltar la cuerda de un árbol.
- **Otros caminos de suelta.** Llamadores de `0x5A7A10`:
  - `0x59D470` (fallos y tiempo máximo del tick);
  - `0x117F0C0` y `0x117F7E0` (slots 1 y 2 del script `RemoteCatchFunctions`);
  - `0x87F750`, `0x58B380` y `0x59F760`.

  Por otro lado está la suelta forzada antes de un teletransporte (`0x9FED30`, «processRemoteCatchReleaseForced»).
  Ninguno se dispara con la suelta normal del otro jugador.

### 2.5 Efecto lateral: el 2108 del observador [V código, E efecto]

- El observador manda `IsRemoteCatchEffectStartStateReq` (2 B, **sin clave**) por la cuerda del **otro**
  (`0xB56B00` no mira quién la controla). En H: 310,032/314,228 y 1152,727/1416,311.
- El servidor lo aplica al personaje de **esa sesión** (`0x2971BD0`). Pero solo escribe `comp+0xF0+slot` si su
  propia ranura en el servidor está ocupada.
- Es casi inocuo. Solo confunde el indicador si los dos usan la cuerda a la vez. Con F1 el par vuelve a durar
  ~0,3 s.

---

## 3. `catch_seq` y este camino (pregunta 3) [V]

- `catch_seq` engancha `ClientCatch_ApplyResult` `0x935DD0`: `CatchActionResultAck` 2156, componente `Catch`
  `[actor+0x68]+0x70`, comprobación del byte de secuencia `+0x93`.
- La cuerda es `RemoteCatchResultAck` 2763 → `0x592340`, componente `RemoteCatch` `+0x78`. Otro opcode, otro
  manejador, otro componente y **ninguna comparación de secuencia**: `catch_seq` no lo toca. H2 (`catch_seq=1`)
  además no tiene ningún gancho.
- **El patrón sí se parece:** el paquete trae un valor local del PC de quien lanza, y el cliente que aplica tiene
  que usar el suyo. Aquí ese valor es un **handle de física**, no un byte, y el suyo se obtiene con la
  correspondencia nativa por id de escena (§2.2).
- **Lo que no tiene equivalente en los agarres:** en un agarre el servidor sí manda la suelta
  (`ReleaseCatchResultAck` 2512 `ToNear`). Para la cuerda no hay suelta, así que hace falta F2.
- **Dirección inversa (el invitado ve la cuerda del anfitrión):** también está mal.
  - G1 195,298 → 200,229: 4,931 s ≈ 222 m, frente a 0,396 s en el propio anfitrión.
  - La ranura del invitado para `0xA0100001` tampoco se suelta **[E]**: el anfitrión no volvió a lanzar en S25.

---

## 4. Causa raíz

En la opción D cada cliente dibuja las cuerdas de los demás aplicando el `RemoteCatchResultAck` que el servidor
copia del cliente que lanza. Ese paquete identifica el punto de enganche por un **handle de cuerpo físico local de
ese PC**. El observador lo interpreta contra su propia física y engancha **otro objeto**, a veces a cientos de
metros o a kilómetros. Cuando no lo encuentra, falla y deja la ranura sucia.

Además, la suelta de una cuerda a un árbol solo existe como *frame event* del cliente que lanza, y el servidor no
la anuncia. El observador **nunca suelta**: su cuerda se queda colgada y su ranura ocupada, y todos los ganchos
siguientes de ese personaje fallan en `0x1808100`.

No intervienen ni el servidor (acepta todo), ni el túnel, ni ningún filtro del mod (nada en `src/mod` toca
2763/2471/2108, grep).

---

## 5. Arreglo propuesto (pregunta 4)

> **Propuesta original.** Lo implementado está en §10, con los cambios de la revisión independiente: F2 va entera
> en el servidor, con el escritor nativo del 2471 y sin anillo ni gancho en el tick; F3 trata cualquier ranura
> ocupada al llegar el 2763 siguiente.

**Módulo nuevo** `src/mod/session2/remote_rope{.cpp,.h,_logic.h}`. Clave `[session2] remote_rope = 0 | probe | 1`,
apagada por defecto, en **anfitrión e invitado**. Instalación como `catch_seq`:
- hilo propio;
- espera a 3 s en el mundo;
- comprobación de build, `.rsrc` y bytes de cada entrada y de cada bloque que se imita, con `REFUSED` si algo no
  cuadra;
- `safetyhook::create_inline` solo en **entradas** de función.

### 5.1 F1: traducir el objetivo en el observador (los dos PCs)

- **Gancho:** entrada de `RemoteCatchComponent_Apply` **`0x592340`**.
  - Prólogo `48 89 5C 24 08 | 48 89 74 24 10 | 48 89 7C 24 18 | 4C 89 74 24 20 | 55 | 48 8D 6C 24 D9 | 48 81 EC F0
    00 00 00` (límites 5/10/15/20/21/26/33).
  - Su único llamador es `0xB5BB7B` (`E8 C0 67 A3 FF`).
  - Corre en el hilo que aplica los Acks.
- **Argumentos** (`0xB5BB38–0xB5BB7B`):
  - `rcx` comp, `rdx` &err, `r8` &slot, `r9` &kind;
  - `[rsp+0x28]` &3 B, **`[rsp+0x30]` &info**, `[rsp+0x38]` &hash, `[rsp+0x40]` &speed, `[rsp+0x50]` &nombre.
  - `info` es la variable local del manejador (`rbp+0x10`, 0x4C B): `+0` clase, `+4` actor, `+8` id de escena,
    **`+0x18` handle**, `+0x24` punto. **Solo la usa esta llamada.**
- **Regla** (lecturas con SEH: `fast_read`/`__try`):
  1. quien lanza es `[comp+8]`, y **no** lo controla este cliente: `byte [catcher+0xC8] == 0`;
  2. `*kind == 0`, `info.clase == 2`, `info.handle != -1` y el id de escena no es cero;
  3. `local` = la correspondencia nativa de §2.2, imitando `0x5947C7–0x59484F` (se verifican esos bytes):
     - `ResolveUuidLookup` `0x1851010`;
     - `[obj+0x58]`, `byte +0x15`, `[+0xE0]->vf70`;
     - restaurar `ScopeAttacherBase` y llamar al dtor `0x1434880`.

     Son las mismas llamadas que hace el juego en el mismo hilo al apuntar.
- **`probe`:** solo log y contadores. Una línea por 2763 con:
  - clave, aquí/remoto, `kind`, clase, id de escena, handle del paquete, handle `local`, ranura antes;
  - `err` después;
  - **vuelo `[comp+0xA94+slot*0x1A0]` en s y en m (× speed)**, que es la medida directa del error.
- **`1`:** si `local != -1` y es distinto del handle del paquete, `info.handle = local` y se llama al original.
  - Si no se puede resolver aquí (id 0, objeto no cargado, sin cuerpo): en la primera prueba **pasa sin tocar** (como
    hoy) y se cuenta. Saltarse el `Apply` (sin cuerda ni ranura sucia) se activa solo cuando la medida diga cuántos
    son (regla de las guardas sin medir).
- Los ganchos propios y los de los NPC de este cliente no se tocan. Los de clase 0/1 (actores, con claves
  compartidas) tampoco.

### 5.2 F2: soltar en el observador

> **Sustituido en §10.2.** Un anillo consumido en el tick `0x59C0D0` podía soltar una cuerda **más nueva** del mismo
> personaje. Lo implementado llama, en el propio hilo del servidor y justo después del original, al escritor nativo
> `0x29688C0` con un buscador ToNear del que lanza, igual que `0x2903950`. Así los dos clientes reciben la suelta en
> orden por el camino nativo de Acks.

**Anfitrión, lado servidor:** gancho en la entrada de `ServerFrameEventReleaseRemoteCatch::vf1` **`0x27D9110`**.
- Prólogo `48 89 5C 24 08 | 48 89 74 24 10 | 48 89 7C 24 20 | 55 | 41 54 | 41 55 | 41 56 | 41 57 | 48 8D 6C 24
  D1`.
- Su única referencia es la vtable `0x5B1D858`, slot 1 de `0x5B1D850`. Se engancha el **código**, no la vtable.
- Lee:
  - `actor = [[rsp+0x28 → p5]]` y su clave `[actor+0x60]`;
  - `d = [r8+8]`: ranura `byte [d+8]`, motivo `byte [d+9]`.
- Llama al original. Si devolvió `err == 0`:
  - **quien suelta es el personaje del usuario 2:** encola `{clave, ranura, motivo}` en un anillo sin bloqueo para
    el cliente del anfitrión;
  - **cualquier otro** (el anfitrión o sus NPC): `server_push_extra` de un **`ReleaseRemoteCatchResultAck` 2471
    nativo**, 6 B `{u32 clave, u8 ranura, u8 motivo}` (formato de `0x29688C0`), a la sesión del usuario 2. Su
    cliente lo aplica con su manejador `0xB5BD60` → `0x5A7A10(comp, slot, motivo, 0, 1)`, como en el MMO para
    objetivos actor.
- En el hilo del servidor solo hay lecturas, el anillo y la cola del mod. **Ninguna llamada al juego.**

**Anfitrión, lado cliente:** gancho en la entrada del tick del componente **`0x59C0D0`**.
- Prólogo `40 55 | 53 | 56 | 57 | 41 54 | 41 55 | 41 56 | 41 57 | 48 8D 6C 24 E1 | 48 81 EC D8 00 00 00`.
- Llamadores: `0x8A1697` y el salto `0x8A5DA0`.
- Corre cada frame para cada actor con el componente, y hoy ya corre para el personaje remoto (§2.1).
- **Camino rápido:** un `load` atómico del anillo; si está vacío, al original.
- Si no, con clave `[[rcx+8]+0x60]` en el anillo y ranura ocupada (`byte [comp+0x20+slot*0x58] != 6`):
  `0x5A7A10(comp, slot, motivo, 0, 1)`.
  - Es **la misma llamada que hace el manejador nativo del 2471**. En el hilo del juego suelta en el momento
    (`0x5A7BC0`); en otro deja la petición nativa en `comp+0xC90`, que procesa `0x58B8F0`.
  - Prólogo de `0x5A7A10`: `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 50`.
- **Invitado:** nada más. El 2471 que empuja el anfitrión usa el camino nativo.

### 5.3 F3: limpieza (los dos PCs, dentro de F1; también activa en `1`)

- **Antes del original:** si quien lanza es remoto y su ranura sigue ocupada, la suelta se perdió. Se llama a
  `0x5A7A10(comp, slot, 2, 0, 1)` (motivo 2, el de los fallos del tick nativo) y después se aplica el gancho
  nuevo.
- **Después del original:** si `*err != 0` y la ranura quedó ocupada (defecto 3), la misma suelta.
- **Tiempo máximo:** una cuerda remota ocupada más de N s sin suelta se cuenta en `probe`. Con el tick de F2 podría
  soltarse, pero **solo después de medir** cuánto dura una cuerda real: en S25, hasta 32 s sujetando.

### 5.4 Por qué ahí

| Alternativa | Por qué no |
|---|---|
| Reescribir el `DoFrameEventReq` del que lanza con un punto de mundo | Cambia su propia cuerda (el que lanza también aplica el 2763) y el doblado del árbol, que usa el cuerpo |
| Poner el handle a −1 (resolver por escena con la transformada del objeto) | El punto está en el sistema del **cuerpo**. En el episodio de 981 ese cuerpo es identidad y el objeto no: mandaría la cuerda a ~11 km igual |
| Que el servidor mande el 2471 a todos (`ToNear`) | También le llega a quien lanza, que ya soltó: doble suelta y riesgo de cortar su cuerda siguiente |
| Detectar la suelta por hashes de acción en el observador (`D20295AD`, `1F4EF471`…) | Frágil; el servidor ya tiene la señal exacta (`0x27D9110`) |
| Mandar un punto de mundo por el túnel | Protocolo nuevo y esperar el paquete. Solo hace falta para los objetivos sin id de escena (1 de 6), fase 2 si se necesita |

---

## 6. Riesgos

1. **Llamadas al juego desde ganchos** (`ResolveUuidLookup`, dtor, `vf70`, `0x5A7A10`).
   - Son las mismas que hace el juego en los mismos contextos: 0x593B50 al apuntar, 0xB5BD60 al soltar.
   - Se verifican sus bytes. Un SEH no deshace un lock a medias, así que el código del gancho debe ser mínimo.
2. **El handle de algunos objetivos de clase 2 puede no salir de esa correspondencia** (otro cuerpo del mismo
   objeto, impactos que no pasan por la conversión de clase 1 a 2).
   - Si pasa, F1 apuntaría a otro cuerpo del objeto bueno.
   - Lo mide `probe` con los ganchos **propios**: el handle `local` tiene que ser **igual** al del paquete en
     todos.
3. **Objeto no cargado en el observador o id de escena 0.** Sin F1 hoy: cuerda equivocada o error. Con F1 en `1`:
   lo mismo de hoy, pero sin ranura sucia. Saltarlo solo cuando se haya medido.
4. **Doble suelta.**
   - En el anfitrión se evita leyendo la ranura antes.
   - En el invitado, el 2471 nativo no la comprueba: si F1 no aplicó, `0x5A7BC0` corre con la ranura vacía
     **[H, inocuo según el código; medir]**.
5. **Sincronía visual.** La cuerda desaparece en el anfitrión ~RTT/2 después de la suelta real, y el personaje
   remoto se ve ~200 ms tarde. Diferencias de unas décimas.
6. **Lanzamiento (`1F4EF471`):** vacía la ranura del servidor con el cambio de acción, sin *frame event* de suelta
   ni 2471 para un árbol. Medido el 12-10 y cubierto en §10.2.1 y §11.
7. **El 2108 del observador** (§2.5) sigue saliendo, ya con duración normal, y lo aplica el servidor al personaje
   **propio** del observador (§10.7, punto 6).

## 7. Qué medir antes de `1` (sin guardas que rechacen)

1. **Un PC, solo el anfitrión, `remote_rope=probe`:**
   - sus propios ganchos tienen que dar `local == handle del paquete` y un vuelo de 0,2–0,4 s;
   - su suelta tiene que dar una línea del gancho `0x27D9110`, con `would push 2471` (sin usuario 2 conectado, solo
     cuenta).

   **Sin Shadow.**
2. **Un PC con `cdmp_peer` y el guion de §8, `probe`:**
   - el 2763 del usuario 2 sale como «remoto, paquete `…069F`, local `X`» (`X` es el handle del árbol en esta
     sesión), con un vuelo equivocado;
   - la suelta del guion (`D20295AD f3`) sale en `0x27D9110` como «usuario 2 → soltaría en el cliente»;
   - el segundo gancho del guion de dos episodios da `RemoteCatch에 실패` (ranura ocupada). Así se reproduce el
     fallo.
3. **Lo mismo con `1`:**
   - vuelo de ~0,3–0,4 s en el anfitrión;
   - cuerda en el árbol (captura de pantalla);
   - desaparece con la suelta;
   - el segundo gancho entra sin error.

   En la grabación `--record` de `cdmp_peer`, cuando el anfitrión lanza su propia cuerda durante la prueba, tiene
   que aparecer el **2471 empujado** a la sesión 2.
4. **Con Shadow, `1` en los dos PCs** (§8.3).

---

## 8. Reproducir en un PC (pregunta 5)

### 8.1 Guiones ya construidos [V sintaxis y flujo; E reproducción]

En `docs/research/data/`, generados desde G1 por `tools/scripts/make_grapple_script.py` (las órdenes están en su
cabecera). El test `optd_remote_rope` los ejecuta contra un servidor falso:
- todas las Req llevan `PLAYER_KEY`;
- el `DoFrameEventReq` de 636 B conserva el id de escena y el handle de Shadow;
- cada suelta `D20295AD f3` está presente;
- a un `ChangeActionSessionIDAck` 2954 forzado contestan con **un** `EchoActionSessionIDReq` 2350
  `{u32 actor, u64 sesión}` (§10.5, causa del fallo del 12-10 19:04);
- termina con la despedida.

| Guion | Contenido |
|---|---|
| `grapple_s25_g1_138.troc` | Login E3 (`login_e3lc00_112627.troc`) + el gancho 1 (G1 136,0–172,5): `SceneLevelOnDemandGimmickSpawnReq` del árbol `…FC82`, 55 `StartActionReq` + 55 `ProcessAfterBeginFrameEventReq`, 8 `DoFrameEventReq` (con el de 636 B y el **handle `…069F` de Shadow**), 49 `RotateActorReq` y 2 × 2108. Despedida nativa 10 s después |
| `grapple_s25_g1_walk_138_217.troc` | G1 105–225 s: el invitado **anda** desde el inicio de la ranura 108 (−10717,7; 620,2; −3914,6) hasta el árbol (−10748,8; 615,7; −3939,5) entre 111 y 127 s (`MoveActorReq`/`RotateActorReq` a 10 por segundo, 489 Req) y lanza los ganchos 1 y 2 (árboles `…FC82` y `…3AD9`). **El 2.º reproduce la «ranura ocupada».** Sustituye a `grapple_s25_g1_138_217.troc` (sin movimientos), que se borró |
| `grapple_s25_g1_981.troc` | Gancho 4 (979–986 s): árbol `…F883` con punto de mundo. Es el caso «láser». En otro sitio: (−10690; 617; −3887) |

- La clave `0xA010034E` va con `put u32 0 PLAYER_KEY` (`put u32 1` en los `DoFrameEventReq`), como
  `login_e3lc00act_112627.troc`. Se mantienen los tiempos originales con `wait`.
- La regla del eco, al principio de cada guion:
  ```
  learn AS_KEY u32 2954 0
  learn AS_ID u64 2954 8
  reply 2954 2350 0 000000000000000000000000
  put u32 0 AS_KEY
  put u64 4 AS_ID
  ```
- No se toca el handle: precisamente es lo que reproduce el fallo, porque ese número no significa nada en el PC del
  anfitrión.

### 8.2 Cómo usarlo

1. Copia de las partidas en `backups\` (la prueba **escribe** en la memoria del juego).
2. El usuario 2 entra con la ranura 108 (`--var SAVE_SLOT=108`) y aparece donde empezó el invitado de S25; el guion
   de paseo lo lleva al árbol. Alternativa: una partida del anfitrión en el árbol y `grapple_s25_g1_138.troc`
   (§10.5).
3. `[session2] remote_rope=probe` y el *sniffer* en el `cdmp.ini` del anfitrión (cuando exista el módulo; sin él,
   la prueba sirve igual como reproducción del fallo).
4. Como en `opcion-d-pruebas.md` §C4:
   - el procedimiento exacto está en §10.5 (paso B).
5. **Esperado hoy:**
   - el personaje del usuario 2 lanza;
   - el `2108 00 01 → 00 00` del cliente del anfitrión tarda lo que dé el cuerpo `…069F` de esta sesión (o aparece
     `RemoteCatch에 실패` si no existe);
   - la cuerda no se suelta con `D20295AD`;
   - el 2.º gancho falla con `RemoteCatch에 실패`.
6. **Esperado con `1`:** §7.3.
7. **Si el servidor no acepta el `DoFrameEventReq` reproducido** (no llega el 2763): mirar las líneas `Game error`
   del log. Puede hacer falta añadir al guion las `OnDemandEquipDockingGimmickSpawnReq` 2249 de la herramienta,
   que se dejaron fuera **[H]**.

### 8.3 Lo que solo da Shadow

- F1 en el **cliente real del invitado**: traducir el handle del anfitrión con lo que el invitado tenga cargado
  (streaming propio).
- Que el cliente del invitado aplique el **2471 empujado**: su cuerda del anfitrión se suelta. El servidor y la
  grabación sí se ven en un PC.
- La latencia real y la imagen de los dos lados. El lanzamiento (`1F4EF471`) visto por el otro jugador (§11).
- Objetivos sin id de escena (como el de G2 492). En un PC solo se ve el contador de «sin resolver».

---

## 9. Scripts y direcciones

**Scripts** (`…\scratchpad\gancho\`, Python `-I`; `cap.py` es el lector `BinRecord` copiado de `fin\`):

| Script | Para qué |
|---|---|
| `idx.py <bin> <pkl>` / `lsidx.py` / `full.py` | Índice de los opcodes de catch/cuerda de cada captura y volcado completo de los 2763 |
| `acts.py <bin> t0 t1 claves` | Acciones, `DoFrameEventReq`, 2763/2471/2108 de unas claves |
| `win.py`, `c2s.py`, `movt.py` | Ventanas y composición del tráfico C>S |
| `dfe6.py`, `dfehead.py` | `DoFrameEventReq` variante 6 y cabecera |
| `dfecount.py`, `dfeof.py` | `DoFrameEventReq` del anfitrión por atacante (0 de tipo 0 con claves del invitado) |
| `acks.py` | `StartActionAck` de suelta relevadas al anfitrión |
| `pos.py` | Posición de cada personaje en cada gancho |
| `mkgrapple.py`, `chktroc.py` | Primera versión de los guiones de §8. La definitiva es `tools/scripts/make_grapple_script.py` |
| `pedis.py`, `opimm.py`, `dispscan.py` | Búsqueda de opcodes y desplazamientos en el exe |

**Direcciones** (build 2976, RVA, servidor de Ghidra):

| RVA | Qué |
|---|---|
| `0xB5B710` / `0xB5BB7B` | Manejador del 2763 / su llamada a `0x592340` |
| `0x592340` | `RemoteCatchComponent_Apply` (error en `0x592531`, retorno `0x592536`) |
| `0x594AD0`, `0x1807A40`, `0x1808100` | Núcleo del gancho, ranura vacía (`== 6`) y copia |
| `0x5A40E0`, `0x5A5370` | Transformada del objetivo (clase 2: `[WorldSystem+0xE8]->vf3B0(handle)`) |
| `0x5ABBC0` → `0x5A12A0`, `0x5A1530`, `0xB56B00` | Vuelo (`+0xA94 = dist/speed`), llegada, envío del 2108 |
| `0x59C0D0` → `0x59D470`, `0x59E150` | Tick del componente (llamador `0x8A1697`) |
| `0x5A7A10`, `0x5A7BC0`, `0x1807C80` | Suelta (petición, núcleo, ranura a `0x10106`); petición diferida `comp+0xC90`, consumida en `0x58B8F0` |
| `0x7F88B0` → `0x593B50` (`0x5947AB–0x594859`) | Construcción del objetivo por quien lanza (id de escena → handle) |
| `0x1851010`, `0x1434880`, `0x5725E48` | `ResolveUuidLookup`, dtor del *attacher*, vtable `ScopeAttacherBase` |
| `0x7F8F40` | `ClientFrameEventReleaseRemoteCatch::vf1` (suelta en quien lanza) |
| `0x27D8EB0` → `0x2902DC0`, `0x2968330` | Servidor: remote catch y escritor del 2763 (copia el objetivo) |
| `0x27D9110` | Servidor: `ServerFrameEventReleaseRemoteCatch::vf1` |
| `0x29688C0` ← `0x2903950`, `0x2902DC0` | Escritor del 2471: solo objetivos actor |
| `0xB5BD60` | Cliente: manejador del 2471 → `0x5A7A10(comp, slot, motivo, 0, 1)` |
| `0x2971BD0` | Servidor: 2108 (sin clave; solo si la ranura de la sesión está ocupada) |
| `0x2327320` (`0x2328EFC`) ← `0x2A7BD50` (servidor), `0x8610A0` (cliente) | Cambio de acción: `[[actor+0x68]+0x78]->vf140` |
| `0x1807390` (vf140 común y del servidor; el `0x58B380` del cliente la llama primero) | Suelta por cambio de acción: sin la marca `[acción+9] & 4`, vf148 del objetivo (`0x2903950`, 2471 solo actor) y vf158 propio `0x1807C80`, o todo con `0x1807090` |
| `0x5A12A0` | Vuelo: estado 2 y `+0xA94 = distancia / velocidad`, **después** del `Apply` |
| RTTI | `pa::ClientRemoteCatchActorComponent` vtable `0x55890D8` (slot 32 = `0x58B8F0`); `pa::ServerRemoteCatchActorComponent` `0x5B1F508` (vf140 `0x1807390`, vf148 `0x2903950`, vf158 `0x1807C80`); `pa::CommonRemoteCatchActorComponent` `0x57CE430`; `pa::ServerFrameEventReleaseRemoteCatch` `0x5B1D850`; `pa::gameClientScript::RemoteCatchFunctions` `0x56FFB48` |

---

## 10. Implementado (12 de octubre de 2026; compilado y con tests; en el juego solo el paso A, §11.1)

**Clave** `[session2] remote_rope = 0 | probe | 1`, apagada por defecto. F1, F3 y la medida del 2471 van en **los dos
PCs**; F2 solo en el **anfitrión** (en el invitado no se instala). Es la propuesta de §5 con los cambios de las
revisiones independientes y del lanzamiento medido (§11).

- **Ficheros:**
  - `src/mod/session2/remote_rope{.cpp,.h,_logic.h}`;
  - `src/mod/config.{h,cpp}` (`s2_remote_rope`) y `src/mod/mod.cpp` (línea `Config:` y arranque en los dos papeles);
  - `tests/optd_remote_rope_tests.cpp` (test `optd_remote_rope`) y `CMakeLists.txt`;
  - los guiones `docs/research/data/grapple_s25_g1_{138,walk_138_217,981}.troc` y
    `tools/scripts/make_grapple_script.py`.
- **Inis:**
  - `cdmp.p8-s26-host-paso1.ini` y `cdmp.p8-s26-shadow.ini`: `remote_rope=probe`;
  - `cdmp.p8-s25-host-activo.ini` y `cdmp.p8-s26-shadow-paso2.ini` (= `cdmp.p8-s26-shadow.ini` con
    `remote_rope=1`): `remote_rope=1`.
- **Instalación** (como `catch_seq`): hilo propio que espera a que el personaje local lleve 3 s en el mundo. Antes de
  instalar nada comprueba, y si algo falla da `REFUSED`, no escribe nada y el módulo queda apagado:
  - la build (marca de tiempo del PE);
  - que todo está en `.rsrc`;
  - los bytes de cada entrada enganchada y de cada bloque que se imita o se llama (lista en el log de error);
  - el único llamador `0xB5BB7B`;
  - las ranuras de vtable `0x560BA60` → `0xB5BD60`, `0x5B1D858` → `0x27D9110` y, en el anfitrión, `0x5B1F648` →
    `0x1807390` y `0x5B1F660` → `0x1807C80` (vtable `pa::ServerRemoteCatchActorComponent` `0x5B1F508`);
  - que safetyhook solo parchea instrucciones enteras.

  Los ganchos se habilitan juntos; si uno falla, se deshacen los demás.
- **Fallos dentro de una llamada al juego.** Un fallo de acceso dentro de una de las llamadas del módulo
  (`ResolveUuidLookup`, `vf70`, dtor, `vf3B0`, `0x5A7A10`, el escritor) **apaga esa llamada el resto de la sesión**
  (`FAULTED`); nunca se reintenta. Además, el 2763 en el que pasó **no se aplica**: el gancho escribe en `*err` el
  error nativo de `0x592340` (`DAT_146cf70a4`, `0xD661CF29` en S25) y vuelve sin llamar al original, porque un
  cerrojo pudo quedar tomado y el `Apply` toma los mismos. Ese 2763 falla como hoy y el siguiente ya no pasa por la
  llamada apagada.
- **`probe` no es «solo leer».** Hace las mismas lecturas que el juego (`native-equivalent reads`): la búsqueda toma
  el cerrojo del gestor de escena y una referencia (que devuelve el dtor), y llama a `vf70`/`vf3B0`. **No escribe
  ningún estado del juego.** Los lanzamientos propios se calculan solo hasta **20 coincidencias seguidas**
  (`kOwnChecks`); después sale `RemoteRope: own-throw check done` y ya no se calculan.

### 10.1 F1 + F3: entrada de `RemoteCatchComponent_Apply` `0x592340` (los dos PCs, hilo que aplica los Acks)

En cada 2763 lee con SEH:
- quien lanza (`[comp+8]`, su clave `+0x60` y `+0xC8`);
- `*slot` y `*kind`;
- el bloque de objetivo del manejador (clase, actor, id de escena, handle, punto);
- tipo (`[comp+0x20+slot*0x58]`) y estado (`[comp+0x9BC+slot*0x1A0]`) de la ranura y, si está ocupada, la clase
  (`+8`) y el actor objetivo (`+0xC`) que guarda.

Decide con `rrp::decide_apply` (puro, con tests):

- **F3** (quien lanza **no** se controla aquí y la ranura está ocupada): la ranura es por fuerza vieja, porque la del
  servidor pasó `0x1807A40`.
  - **Solo el caso medido:** la ranura vieja guarda un árbol (`kind 0`, clase 2, actor objetivo 0). Cualquier otra
    (un actor) sale como `STALE slot … holding a non-tree target: NOT released` y se cuenta: `0x5A7BC0` →
    `0x1807C80` borraría la clave de quien sujeta en el objetivo (`[[objetivo+0x68]+0x78]+0x18`) sin comprobar que
    es de este lanzador, cosa que el camino nativo del 2471 solo hace tras la comprobación del servidor
    (`0x2903950`).
  - En `1`, antes de llamar se comprueba que el componente es el del cliente (vtable `0x55890D8`) y que el objeto de
    cerrojo de quien lanza (`[[comp+8]+8]`, el que toma `cdmp_Actor_Lock 0x393650`) tiene `vf18/20/30/40` en código
    del exe; si no, `release NOT called` y se cuenta. Después se llama al **`0x5A7A10(comp, slot, 2, 0, 1)` nativo**
    (la misma llamada que el manejador del 2471, `0xB5BF44`). En el hilo principal (`dword 0x6A652D4`) suelta en el
    momento; en otro deja la petición nativa y el log lo dice.
  - Cubre las dos causas: una suelta que no llegó (también tras un lanzamiento si F2 no estuviera) y un `Apply`
    fallido que dejó la ranura llena (`0x5A40E0` después de `0x1808100`).
  - En `probe`, solo la línea.
- **F1** (quien lanza no se controla aquí, `kind 0`, clase 2, handle `!= -1` y un id de escena):
  - Calcula el handle de **este** PC como lo hace quien lanza (`0x5947C7..0x59484E`):
    - `ResolveUuidLookup 0x1851010`, solo si los punteros del gestor y la vtable de su cerrojo son legibles y apuntan
      a código del exe;
    - comprueba el resultado como `0x5A5370`: referencia tomada `[att+0x10]` y objeto `[att+8]`;
    - después `[obj+0x58]`, `[+0x15] != 1`, `[+0xE0]` y su `vf70`, con todo leído antes de usarlo;
    - y **siempre** devuelve la referencia con el dtor `0x1434880` (con la vtable `ScopeAttacherBase`, como el nativo).
  - En `1`, si sale otro handle, se escribe **solo** en `+0x18` del bloque local del manejador, la copia que lee
    `0x5923AD` (`kInfoCopyBytes`). Si no se resuelve: **pasa sin tocar** y se cuenta.
  - Los lanzamientos propios se calculan también como comprobación (deben coincidir con el paquete) y **nunca** se
    escriben; tras 20 coincidencias seguidas, ya no se calculan (`own throw (check done, not computed)`).
  - Además, para medir, la transformada `vf3B0` del cuerpo del paquete y del de este PC, y el punto de mundo
    resultante. Es la medida de los objetivos «en coordenadas de mundo» tipo `…084E`/`F883`.
- Las comprobaciones se hacen antes de desreferenciar: el SEH no es el flujo normal, porque no deshace un cerrojo a
  medias.
- **El vuelo.** `0x5A12A0` pone el estado 2 y `+0xA94 = distancia / velocidad` **después** de que vuelva el `Apply`
  (justo al volver, el estado es 1 y `+0xA94` aún es 0: el `flight 0.000 s` de todas las líneas del 12-10 19:11).
  El hilo del módulo vuelve a buscar al lanzador **por su clave** y lee estado y vuelo (lecturas con `VirtualQuery`)
  hasta que el estado es 2 o 3, como mucho 3 s; entonces escribe la línea del `apply` con el vuelo y el momento de la
  lectura. Como mucho 16 `apply` esperan a la vez.

### 10.2 F2: entrada de `ServerFrameEventReleaseRemoteCatch::vf1` `0x27D9110` (solo anfitrión, hilo del servidor)

1. Antes del original lee la ranura **del servidor** del que suelta: tipo, clase, actor objetivo, escena y handle.
   Después la libera el propio original.
2. Llama al original.
3. Si devolvió `err 0` y el objetivo es de los que el servidor deja sin 2471 (`kind 0`, clase 2, actor objetivo 0),
   en `1` llama al **escritor nativo `0x29688C0`** en ese mismo hilo, como `0x2903950` (`0x2903A79..0x2903AB2`):
   - buscador `ToNearSendTargetFinder` (`0x5B25418`, `+8 = 1`, `+0x10` = el que suelta);
   - `&ranura`, `&motivo`.

   Los dos clientes reciben `ReleaseRemoteCatchResultAck {u32 clave, u8 ranura, u8 motivo}` en orden, por el camino
   nativo `0xB5BD60` → `0x5A7A10(comp, slot, motivo, 0, 1)` en el hilo principal.
   Antes de llamarlo se comprueba lo primero que el escritor lee a través del buscador (`ToNear::vf1` `0x2A75860` →
   `0x2BC51E0` → `0x2BC4F70`): el `TransformSync` del lanzador `[[actor+0x68]+0x1A0]` y su vtable, su dueño
   `[ts+8]` y las entradas `vfE8`/`vf160`/`vf1D8` de su vtable, en código del exe. Si no, `writer NOT called` y se
   cuenta (un fallo dentro del escritor, tras poner a 0 su contexto de envío `TLS+0x250`, apagaría F2 en los dos
   ganchos). `TLS+0x250` es el del propio hilo del servidor, el mismo con el que el juego llama al escritor.
4. En `probe`: `WOULD SEND`.
5. Objetivos actor (el nativo ya manda su 2471), ranura vacía, error del original o fuera de alcance (`kind`/clase
   distintos): no se manda nada y se cuenta.
6. **Ranura ya vacía** (`err 0xD661CF29`, tipo 6): pasa tras cada lanzamiento (§11) y con `EB340761` sin cuerda.
   Si el hilo del módulo sabe que esa ranura de ese lanzador ya se vació por un cambio de acción (§10.2.1), o ya
   escribió una línea de ranura vacía desde el último 2763, la **pliega**: no escribe línea y la cuenta en
   `release events on a slot already freed`. Si no, una línea con `the server slot was ALREADY EMPTY (freed before
   this release event by a path not hooked …)`: un camino de suelta que no está cubierto.

Lo que se mide:
- **El duplicado de quien lanza.** Su cliente ya soltó en local; el MMO hacía lo mismo para objetivos actor. Medido
  el 12-10 19:11 con objetivos actor: el 2471 nativo llega a quien lanza con la ranura ya vacía (`slot before 6
  state 5/7 -> after 6`), sin efecto (§11.1).
- **Personajes y NPC:** se reconocen por el byte de control y por las claves, nunca por direcciones.
- **NPC cuya cuerda controla el cliente del invitado** (punto 6 de la revisión):
  - en el invitado, F1 los trata como propios y no los toca;
  - en el anfitrión son remotos y se traducen;
  - su suelta la procesa el servidor del anfitrión, así que F2 manda su 2471 a los dos.

### 10.2.1 F2 en el lanzamiento: entrada de la vf140 `0x1807390` (solo anfitrión, solo el componente del servidor)

El lanzamiento vacía la ranura del servidor **sin** pasar por `0x27D9110` (§11.2). F2 engancha también la raíz de
ese camino:

- **Gancho:** entrada de `0x1807390`, la vf140 de `pa::CommonRemoteCatchActorComponent` y de
  `pa::ServerRemoteCatchActorComponent` (ranura `0x5B1F648`). Prólogo `48 89 5C 24 08 | 48 89 74 24 18 | 48 89 7C 24
  20 | 55 | 41 54 | 41 55 | 41 56 | 41 57 | 48 8D 6C 24 C9 | 48 81 EC 00 01 00 00` (límites 5/10/15/16/18/20/22/24/
  29/36).
  - En el servidor la llama el cambio de acción `0x2327320` (`0x2328EE4..0x2328F02`, la única llamada a vf140 de ese
    componente); en el cliente, la vf140 `0x58B380` la llama primero (`0x58B3D9`). **Solo se mira el componente del
    servidor** (su vtable `0x5B1F508`); el del cliente pasa directo, con una lectura.
  - Se verifican también la prueba de la marca de mantener (`0x1807610`), la llamada a vf148 del objetivo
    (`0x1807646`, motivo 2, el lanzador `[rsi+8]`) y la vf158 propia (`0x18076DF`, `(slot, 2, 0, 1)`).
- **Antes del original:** si alguna ranura está ocupada, su tipo, clase, actor objetivo, escena y handle, y el
  lanzador `[comp+8]` con su clave.
- **Después:** para cada ranura ocupada antes, su tipo ahora. Decide con `rrp::decide_action_release` (puro, con
  tests):
  - sigue ocupada (`563271FE` cada 1,5 s, `D20295AD`…): nada, ni línea ni cuenta;
  - vaciada, **árbol** (`kind 0`, clase 2, actor 0): en `1`, el mismo escritor que §10.2 con `ToNear(lanzador)`,
    `&ranura` y **motivo 2** (el que `0x1807390` pasa a vf148); en `probe`, `WOULD SEND`;
  - vaciada, objetivo actor: el nativo ya mandó su 2471 por vf148 → `0x2903950`. Solo se cuenta;
  - otra clase/`kind`, o ilegible: nada, una línea.
- **Por qué es seguro llamar al escritor ahí:** es el mismo hilo (el del servidor) y el mismo contexto en el que el
  juego lo llama para un objetivo actor: dentro de `0x1807390`, vía vf148 → `0x2903950`. Un fallo apaga el escritor
  para toda la sesión (el mismo interruptor que §10.2).
- **Orden en los clientes:** el 2471 sale antes que cualquier 2763 nuevo de esa ranura (el servidor los escribe en
  ese orden), así que no puede cortar la cuerda siguiente.
- **Riesgo: la cuerda de quien lanza (revisión independiente de `cabea6a`) [V código; sin medir en vivo].** En la
  suelta normal quien lanza ya soltó en local (`0x7F8F40` → `0x5A7A10`) y su duplicado encuentra la ranura vacía. En
  el lanzamiento no es seguro:
  - su cliente solo suelta si su `0x58B380` → `0x1807390` decide lo mismo que el servidor. `0x58B380` puede mantener
    una ranura ocupada (estado 4, que lleva `0x59F760`) a través del cambio de acción si la acción nueva tiene la
    marca de mantener (`[acción+9] & 4`) o `[+0xF6] == 2`;
  - y la suelta total del servidor (`0x180756D` → `0x1807090`) no mira esa marca cuando
    `[[actor+0x68]+0x20]+0x273` está puesto, `[acción+0x10C] < 0` o la clave de quien sujeta (`comp+0x18`) es 0:
    valores del actor que pueden diferir entre servidor y cliente;
  - entonces el 2471 llega a una ranura en estado 2/3/4 y `0x5A7BC0` la recoge (`0x5A0C60`, estado 5) y la vacía
    (`0x1807C80`): **le cortaría el vuelo en cada lanzamiento**, también al anfitrión solo en `1`. `probe` no lo ve
    porque no manda nada.
- **Medida (sin guarda que rechace):** al sacar del anillo cada lanzamiento `WOULD SEND`/`SENT`, el hilo del módulo
  busca al lanzador por su clave **en este cliente** (≤ 500 ms después) y añade a la línea su ranura:
  - lanzador controlado aquí (el anfitrión, sus NPC): `the thrower's OWN client slot when the server freed it: kind
    K state S (+N ms) -> free` o `-> BUSY: a 2471 would cut its rope`; el resumen las cuenta (`free`, `BUSY`);
  - lanzador remoto: `this client's copy (observer) of the thrower's slot: …` (ocupada es lo esperado).

  En `probe`, en el anfitrión, mide hoy sus propios lanzamientos. En `1` se lee después de que el 2471 ya se aplicó:
  la medida es entonces la línea `2471 applied … controlled HERE … slot before`.
- **Lo que no se implementa (todavía):** mandar el 2471 del lanzamiento solo al otro cliente.
  - No hay buscador nativo «cerca menos el lanzador»: en esta build `ToNearExceptOneSelf`/`ExceptOneControl` están
    vaciados (su `vf1` no añade ninguna sesión; `segunda-sesion.md`, `interacciones-mundo.md`).
  - Se podría con lo del mod: si lanza el anfitrión, `server_push_extra` (`server.h`) de un 2471 de 6 B a la sesión
    del usuario 2; si lanza el usuario 2, soltar en el cliente del anfitrión (la llamada de F3 a `0x5A7A10` en el hilo
    principal, o inyectar el 2471 en su recepción). Pero ya no es el escritor nativo ni su buscador: dos mecanismos
    del mod, uno por dirección. Solo merece la pena si la medida da `BUSY`.
- **Una línea por lanzamiento:** `server release on an ACTION CHANGE … -> kind 6 after vf140 0x1807390 … WOULD SEND`
  (o `2471 SENT`). Los `EB340761 f0/f1` que llegan después a `0x27D9110` con la ranura vacía se pliegan en ella
  (§10.2, punto 6).

### 10.3 Medida del 2471: entrada de `0xB5BD60` (los dos PCs, solo log)

- Por cada 2471 aplicado: clave, ranura, motivo, quién lo controla aquí, y tipo y estado de la ranura **antes y
  después** del manejador nativo.
- Así se ve qué hace el duplicado en quien lanza (estado en `comp+0x9BC`) y que el observador pasa de ocupado a `6`.

### 10.4 Líneas del log

**Arranque:**
```
Config: P8 session 25 rope: remote_rope 'probe' (both; the server's 2471 on the host)
RemoteRope ON (remote_rope=probe, host): hooks at the entries of RemoteCatchComponent_Apply 0x592340 and the 2471 handler 0xB5BD60, of the server's ServerFrameEventReleaseRemoteCatch::vf1 0x27D9110 and of the remote catch component's vf140 0x1807390 (the action change: the launch) once the world is up; every rope computed, logged and counted with native-equivalent reads (...), no game state written (...)
RemoteRope: hooked at their entries (5/5/5/5 bytes patched): exe+0x592340 (...), exe+0xB5BD60 (the 2471 handler, log only), exe+0x27D9110 (the server's release frame event), exe+0x1807390 (the remote catch component's vf140: ...); remote_rope=probe: computed and logged with native-equivalent reads, no game state written
```
En el invitado sale `(remote_rope=…, guest)` y solo dos ganchos. Si sale `REFUSED`, el módulo no hizo nada.

**Por cada 2763 aplicado** (como mucho 400 líneas en total; la del `apply` sale cuando se conoce el vuelo, hasta 3 s
después):
```
RemoteRope [probe] #n apply HH:MM:SS... : thrower 0xA0...... REMOTE (+0xC8 = 0) | slot 0 kind 0 class 2 target actor 0x00000000 | scene 090000DE6690FC82:000000000100FE0F | packet handle 0x800000000100069F | this PC's 0x80000000010XXXXX (ok) | remote thrower, handle TRANSLATED -> WOULD WRITE with remote_rope=1 | slot before 6 state S | slot free | after: err 0x0 slot 0 state 1 | flight 0.412 s x speed 45.0 = 18.5 m (state 2 at +310 ms) | point (x, y, z) | main thread
RemoteRope [probe] #n body: thrower 0xA0......, the packet's handle 0x...: translation (...) rotation (...) scale (...) -> hook point in the world (X, Y, Z)
RemoteRope [probe] #n body: thrower 0xA0......, this PC's handle 0x...: ... -> hook point in the world (X, Y, Z)
```
- **Veredictos de F1:**
  - `own throw, handle match` / `MISMATCH` / `not resolved` / `own throw (check done, not computed)`;
  - `remote thrower, same handle here` / `handle TRANSLATED` (`-> WOULD WRITE` en probe, `-> WRITTEN` en 1);
  - `remote thrower, UNRESOLVED (passed through)`, con el motivo entre paréntesis tras el handle: `scene object not
    loaded here`, `no physics body`, …;
  - `not a body target (untouched)`, `UNREADABLE`.
- **F3:**
  - `slot free`;
  - `STALE slot (remote thrower): WOULD release first with remote_rope=1` en probe;
  - `STALE slot (remote thrower): native release first -> RELEASED (slot empty now)` en 1, o `-> release DEFERRED` /
    `STILL busy` / `FAULTED` / `release NOT called (component / lock object not the expected ones)`;
  - `STALE slot (remote thrower) holding a non-tree target: NOT released (not measured, logged only)`.
- Fin de línea `| a FAULT in a game call: this 2763 NOT applied (native error), that call off` si hubo un fallo.
- **El vuelo:** `flight F s x speed 45.0 = D m (state 2|3 at +N ms)`. `× speed` es la distancia de la mano al punto
  que usa **este** cliente: la medida directa del error. Si no llega: `flight not seen (state S at +3000 ms)`,
  `flight not measured (the thrower is not on this client …)` o `(the apply did not fill the slot)` (un `Apply`
  fallido).

**Por cada suelta en el servidor** (anfitrión):
```
RemoteRope [probe] #n server release ...: thrower 0xA0...... (REMOTE here (user 2's)) slot 0 reason R b10 B | server slot kind 0 class 2 target actor 0x00000000 scene ... handle 0x... | err 0x0 | WOULD SEND 2471 ToNear(thrower) with remote_rope=1 | server thread T
```
En `1`: `2471 SENT ToNear(thrower) (writer 0x29688C0 returned)`. Para el anfitrión: `(controlled by this client (the
host or its NPCs))`.

**Por cada lanzamiento** (anfitrión; una sola línea):
```
RemoteRope [probe] #n server release on an ACTION CHANGE ...: thrower 0xA0100001 (controlled by this client (the host or its NPCs)) slot 0 reason 2 | server slot kind 0 class 2 target actor 0x00000000 scene 090000DE66910183:000000000100FE0F handle 0x8000000001000AB2 -> kind 6 after vf140 0x1807390 (the launch path: no release frame event, and the native code sends a 2471 only for an actor target) | WOULD SEND 2471 ToNear(thrower) with remote_rope=1 | the thrower's OWN client slot when the server freed it: kind 6 state 7 (+212 ms) -> free: the 2471 meets an empty slot | server thread T
```
Con la ranura propia aún ocupada: `… kind 0 state 4 (+N ms) -> BUSY: a 2471 would cut its rope (keep remote_rope=probe)`.
En `1`: `2471 SENT ToNear(thrower) (writer 0x29688C0 returned)` y, al final de la parte del lanzador, `[read after
this 2471: its '2471 applied ... slot before' line is the measure]`. Si el escritor no se llamó: `(writer NOT called:
the thrower's TransformSync or its owner's vtable is not the expected one)`.

**Por cada 2471 aplicado:**
```
RemoteRope [1] #n 2471 applied ...: thrower 0xA0...... slot 0 reason R | REMOTE (+0xC8 = 0) | slot before 0 state 3 -> after 6 state X | main thread
```

**Cada 60 s:**
```
RemoteRope summary (remote_rope=…, host|guest): hooks on | 2763 applies … | own: handle match …, MISMATCH …, unresolved … | remote: same handle …, to translate … -> written … | stale slots … -> released …, deferred …, still busy …, release off …, lock check failed …, non-tree stale slots logged only … | own throws not computed (check done) … | applies failed … (slot left dirty …, not applied after a fault …) | server releases …: 2471 sent … | slots freed by an action change (the launch) …: trees 2471 sent …, actor targets … (their native 2471), other …, writer faults … | the thrower's own client slot then: free …, BUSY …, unreadable … | release events on a slot already freed … (folded, not logged) | writer not called (TransformSync check) … | 2471 applied … (own …, remote …, slot busy before …) | faults: F1 0 release 0 writer 0 body 0 | …
```

### 10.5 Prueba en UN PC (anfitrión + `cdmp_peer`)

**Por qué falló la del 12-10 19:04 (sin 2763) [V captura + código]:**
- El guion `grapple_s25_g1_138_217.troc` no traía movimientos. El usuario 2 apareció en el inicio de la ranura 108
  (≈ (−10717,7; 620,2; −3914,6)), a unos 40 m del árbol, y su primer `StartActionReq` repetido traía la posición del
  árbol.
- El servidor forzó entonces una acción (`26107D3C`) con `ChangeActionSessionIDAck` 2954 al usuario 2. El guion no
  contestó con `EchoActionSessionIDReq` 2350, y hasta ese eco el servidor **acepta las Req de acción sin aplicarlas**
  (`opcion-d-accion-usuario2.md` §2.1, comprobación 11). No corrió `9A368460` en el servidor, ni su *frame event* 20,
  ni salió 2763. El anfitrión solo recibió una `StartActionAck` del usuario 2 y ningún `MoveActorAck`.
- Los 2945 `UnspawnOnDemandGimmickNak` eran de *gimmicks* del propio anfitrión; el árbol sí se cargó (2145 Ack).

**Arreglo:** todos los guiones contestan a cada 2954 con su 2350 `{u32 actor, u64 sesión}` (§8.1), como el cliente
real (en la captura del anfitrión, `EchoActionSessionIDReq … 87 02 10 b0 01 00 00 00 00 00 00 00`). Además, el
guion de paseo `grapple_s25_g1_walk_138_217.troc` empieza donde aparece el usuario 2 con la ranura 108, que es donde
empezó el invitado de S25, y lo lleva andando al árbol como en S25. El test `optd_remote_rope` lo reproduce contra un
servidor falso que fuerza un 2954 (con una sesión de más de 32 bits) en el primer `StartActionReq`: sale **un** 2350
con los 12 bytes correctos, los dos `DoFrameEventReq` de 636 B y las dos sueltas.

**Preparación:**
1. Copia de `%LOCALAPPDATA%\Pearl Abyss\CD\save` en `backups\`.
2. `.\scripts\build.ps1` y `.\scripts\deploy.ps1`.
3. `bin64\cdmp.ini` = `docs\research\ini\cdmp.p8-noche-prueba1pc-b.ini` con, en `[session2]`, `guest_own_save=0`,
   `catch_seq=probe` y `remote_rope=probe` (lo mismo que el 12-10 19:04; `[sniffer] enabled=1` y
   `save_guard=redirect` ya vienen).

**Paso A (`probe`, sin `cdmp_peer`; cuerda y lanzamiento del anfitrión).** Hecho el 12-10 19:11 sin el lanzamiento
cubierto (§11.1). Repetir con esta build: dos veces cuerda a un árbol, sujetar 3–5 s y soltar; dos veces cuerda y
**salir volando** (`1F4EF471`).
- **Pasa** si:
  - cada `apply` dice `own throw, handle match`, `err 0x0` y `flight 0,2–0,5 s … (state 2|3 at +N ms)` (ya no
    `0.000`);
  - cada suelta normal da `server release … WOULD SEND`;
  - **cada lanzamiento da una sola línea** `server release on an ACTION CHANGE … kind 0 class 2 … -> kind 6 … WOULD
    SEND`, y ninguna `native error` después (el resumen las cuenta en `release events on a slot already freed`);
  - **en esa línea, `the thrower's OWN client slot when the server freed it: kind 6 … -> free`** en todos los
    lanzamientos (el resumen: `the thrower's own client slot then: free N, BUSY 0`). **Un solo `BUSY`** (cualquier
    estado, también 4): **no** pasar a `remote_rope=1` (ni el paso C, ni el paso 2 de Shadow); el 2471 del
    lanzamiento cortaría la cuerda de quien lanza;
  - ninguna `ALREADY EMPTY`, `FAULTED` ni `REFUSED`.

**Paso B (`probe`, con `cdmp_peer`; reproduce el fallo).** Con el anfitrión ya en el mundo cerca del inicio de la
ranura 108 (o en ella), desde el worktree:
```
.\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\grapple_s25_g1_walk_138_217.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\cuerda-B2.bin
```
- Dura unos 3 min (entrada, 2 min de G1 y la despedida 10 s después).
- **Esperado:**
  - el usuario 2 anda hasta el árbol en los primeros ~22 s del tramo de G1 (G1 111–127 s; captura de pantalla:
    junto al árbol de (−10748,8; 615,7; −3939,5));
  - si el servidor fuerza una acción: en `cuerda-B2.bin` un 2954 S>C seguido de un 2350 C>S del usuario 2;
  - **1.er `apply`** del usuario 2 (unos 11 s después de llegar al árbol): `REMOTE`, `packet handle 0x800000000100069F`,
    `this PC's 0x…`, `handle TRANSLATED -> WOULD WRITE` (o `same handle`), con el vuelo de este cliente;
  - **`server release`** del usuario 2 (`REMOTE here (user 2's)`): `WOULD SEND`;
  - **2.º `apply`** (árbol `…3AD9`): `STALE slot (remote thrower): WOULD release first`, `err` distinto de 0 y el par
    `RemoteCatch에 실패` / `RemoteCatchResultAck처리에 실패`: **el fallo de S25 en un PC**.
- **Si no sale ningún 2763:** buscar en `cuerda-B2.bin` los `MoveActorAck` del usuario 2 (si no anda, el servidor
  rechazó el paseo) y los 2954/2350, y las líneas `Game error` del log. Entonces, la alternativa:
  - en el juego, ir al árbol (−10748,8; 615,7; −3939,5) y **guardar en una ranura `N`** (con `save_guard=redirect` el
    guardado va al *sandbox* y el usuario 2 lo lee en esa misma ejecución);
  - `cdmp_peer … --script docs\research\data\grapple_s25_g1_138.troc --var SAVE_SLOT=N --record
    D:\cdmp-grabaciones\cuerda-B3.bin` (un solo gancho, sin movimientos).

**Paso C (`1`, el mismo guion):** `remote_rope=1` en el `cdmp.ini`. Reiniciar el juego, el mismo sitio y el mismo
comando con `cuerda-C.bin`. Mientras corre, el anfitrión lanza una vez su cuerda a un árbol y **sale volando**.
- **Pasa** si:
  - **1.er `apply`:** `handle TRANSLATED -> WRITTEN`, `err 0x0`, vuelo de 0,25–0,5 s (≈ 11–22 m), y en la línea
    `body` de `this PC's handle` el punto de mundo en el árbol (a menos de ~25 m de (−10748, 616, −3940));
  - **`server release`:** `2471 SENT ToNear(thrower) (writer 0x29688C0 returned)`;
  - justo después, **`2471 applied`** del usuario 2 con `slot before` ocupado (≠ 6) `-> after 6`;
  - **2.º `apply`:** `slot free` (sin `STALE`), `TRANSLATED -> WRITTEN`, `err 0x0`;
  - el lanzamiento del anfitrión: `server release on an ACTION CHANGE … 2471 SENT`; en `cuerda-C.bin` (si el usuario
    2 está cerca) un 2471 S>C `01 00 10 a0 00 02` justo después; y en el anfitrión `2471 applied … controlled HERE …
    slot before 6`;
  - **ninguna** línea `RemoteCatch에 실패` en todo el log;
  - en la captura del anfitrión (`cdmp_packets_*.log`), los `ReleaseRemoteCatchResultAck 2471 … len 6` S>C;
  - captura de pantalla: la cuerda del segundo personaje va al árbol y desaparece al soltar.
- **Abortar (volver a `probe`):**
  - cualquier `FAULTED`;
  - un cierre o un cuelgue;
  - `release DEFERRED` / `NOT the main thread` en las líneas de `apply`;
  - **cualquier** `2471 applied` de **quien lanza** (`controlled HERE`) con `slot before` distinto de 6, en cualquier
    estado (también 4): el 2471 le cortó su propia cuerda (`0x5A7BC0` recoge las ranuras en estado 2/3/4 con
    `0x5A0C60`);
  - una línea de lanzamiento con `-> BUSY`.

**Opcional:** `grapple_s25_g1_981.troc` reproduce el caso «láser» (punto en coordenadas de mundo), pero en
(−10690; 617; −3887), con una ranura guardada allí.

### 10.6 Prueba con Shadow

**Paso 1 (`probe` en los dos):**
- Anfitrión `cdmp.p8-s26-host-paso1.ini`, invitado `cdmp.p8-s26-shadow.ini`.
- Juntos, cada uno lanza dos veces la cuerda a un árbol (sujetar 3–5 s, soltar), una vez a otro árbol y **una vez
  sale volando** (`1F4EF471`).
- **Pasa** si:
  - en cada PC, sus propios lanzamientos dan `own throw, handle match`;
  - los del otro dan `REMOTE … TRANSLATED -> WOULD WRITE` con un `this PC's` resuelto;
  - en el anfitrión, cada suelta de los dos da `server release … WOULD SEND`, y cada salida volando, `server release
    on an ACTION CHANGE … WOULD SEND`; en las del anfitrión, `the thrower's OWN client slot … -> free` (con un `BUSY`,
    no hacer el paso 2: §10.5).
- Que salga `RemoteCatch에 실패` en el observador es lo esperado (el fallo sin arreglo).

**Paso 2 (`1` en los dos):**
- Anfitrión `cdmp.p8-s25-host-activo.ini`, invitado `cdmp.p8-s26-shadow-paso2.ini`. Mismas acciones y además:
  - **los dos con cuerda a la vez**: cada uno a su árbol, sujetando a la vez 3–5 s y soltando por turnos;
  - una salida volando de cada uno mientras el otro mira.
- **Pasa** si:
  - `TRANSLATED -> WRITTEN` con vuelo de 0,2–0,5 s en el observador de cada lado;
  - `2471 SENT` por cada suelta y cada salida volando en el anfitrión;
  - `2471 applied` en el observador (ocupado `-> 6`) y en quien lanza (`slot before 6`, siempre; en el invitado
    también tras sus salidas volando: es la única medida de su lado);
  - con los dos a la vez: la cuerda **propia** de cada uno se engancha, tira y se suelta igual que sola, aunque la del
    otro esté puesta (el 2108 del observador toca la ranura propia en el servidor, §10.7 punto 6), y en la captura del
    anfitrión salen los pares `2108 00 01`/`00 00` de los dos;
  - con la salida volando: en el observador la cuerda desaparece al salir volando, no en el siguiente gancho;
  - ningún `RemoteCatch에 실패` en ninguno de los dos logs;
  - el usuario confirma en los dos PCs que la cuerda va al árbol y desaparece al soltar, y que la segunda funciona.
- **Abortar** con los mismos criterios de §10.5.

### 10.7 Riesgos que quedan

1. **Llamadas al juego desde el gancho del 2763** (`ResolveUuidLookup`, `vf70`, dtor, `vf3B0`, `0x5A7A10`). Son las
   mismas y en el mismo hilo que el juego (`0x593B50`, `0x5A5370`, `0xB5BD60`).
   - `ResolveUuidLookup` toma por dentro el cerrojo del gestor. Un fallo dentro lo dejaría tomado; por eso se
     comprueban antes los punteros y su vtable, tras un fallo esa llamada se apaga y ese 2763 no se aplica.
2. **La correspondencia de quien lanza puede no ser la única fuente del handle** (impactos de clase 2 directos). La
   mide el paso A: un `own … MISMATCH` lo delataría. En el 12-10 19:11: 4 de 4 coincidencias.
3. **El duplicado del 2471 en quien lanza.** Tras una suelta normal, medido sin efecto (§11.1). **Tras un
   lanzamiento puede cortarle la cuerda en el aire** (§10.2.1): su cliente puede mantener la ranura (estado 4)
   mientras el servidor la suelta. Se mide en `probe` (la ranura propia en la línea del lanzamiento) y en `1` (`2471
   applied … controlled HERE … slot before`). No hay en el modo D un buscador «cerca menos uno» que funcione (los
   `ExceptOne*` están vacíos); la alternativa con `server_push_extra` está descrita en §10.2.1 y no se implementa
   sin esa medida.
4. **F3 fuera del hilo principal:** queda diferido. El `Apply` de ese mismo 2763 fallaría igual que hoy y el
   siguiente funcionaría; el log lo dice. F3 no suelta ranuras con objetivo actor (no medido).
5. **Sin id de escena** (1 de 6 en S25) **u objeto no cargado en el observador:** no hay traducción; pasa como hoy,
   pero la ranura ya no bloquea la cuerda siguiente (F3).
6. **El 2108 del observador** (§2.5). **Corrección:** en `1` sí cambia. Con la cuerda remota bien puesta, el cliente
   del observador manda `2108 00 01` unos 0,3 s después de **cada** cuerda remota (y `00 00` al soltar), sin clave.
   El servidor (`0x2971BD0`) lo aplica al personaje **propio** del observador y escribe su `comp+0xF0+ranura` (el
   estado «efecto empezado» de **su** cuerda en el servidor) **si su ranura de ese índice está ocupada**, es decir,
   cuando los dos tienen cuerda a la vez. Antes del arreglo salía con duraciones raras o nada (el `Apply` fallaba).
   Qué hace el servidor con ese byte no está medido **[E]**: el paso 2 de Shadow lo prueba con los dos a la vez.
7. **Otros caminos que vacían la ranura del servidor** sin `0x27D9110` ni el cambio de acción (§11.2): vf190
   `0x1808410`, una cuerda nueva sobre una ranura ocupada (`0x2902DC0+0x822`), la vf178 `0x2903AD0` (tipo 7) y los
   llamadores externos de vf158 (bajarse de una montura `0x156F550`, `0x28DC440`, `0x2B44540`, …). No se cubren; si
   uno vacía un árbol y luego llega un *frame event* de suelta, sale la línea `ALREADY EMPTY`, y F3 limpia el
   observador en el 2763 siguiente.
8. **El vuelo** se lee desde el hilo del módulo buscando al lanzador por clave, hasta 3 s después del `apply`. Si en
   ese tiempo la cuerda ya se soltó, sale `flight not seen (state S …)`.

---

## 11. El lanzamiento y la prueba del anfitrión solo (12 de octubre de 2026, 19:11)

Datos: `C:\Emulador CD\backups\p8-cuerda-anfitrion\cdmp_20261012-191134.log` y
`cdmp_packets_20261012-191144.{log,bin}`. Anfitrión solo, `remote_rope=probe`, build `c365fae` (antes de §10.2.1).
Tiempos `t` de la captura.

### 11.1 Resultados [V]

| `#` | t | Qué | Resultado |
|---|---|---|---|
| 1, 3, 5, 8 | 120,30 / 142,62 / 158,76 / 188,11 | Cuerda propia a cuatro árboles (`…FC82`, `…003B`, `…0183`, `…3881`) | `own throw, handle match` las 4; punto de mundo a 11–13 m sobre la traslación del cuerpo. El de 188,11 tiene rotación identidad (cuerpo en (−10783,17; 607,25; −3991,36)) |
| 2, 4 | 132,25 / 152,00 | Suelta normal (`D20295AD f3`) | Ranura del servidor `kind 0 class 2 actor 0`, `err 0x0`, `WOULD SEND` |
| — | 167,62 / 198,22 | **Salida volando** `1F4EF471` tras las cuerdas 5 y 8 | Ninguna línea en el momento: la ranura del servidor se vació sin pasar por `0x27D9110` |
| 6, 7, 9, 10 | 168,24 / 170,99 / 200,77 / 202,33 | `EB340761 f0/f1` después de cada salida | Ranura ya vacía (`kind 6 class 3`), `err 0xD661CF29`, `native error`: 2 líneas por salida |
| 11, 12 | 207,45 / 209,03 | `EB340761 f0/f1` sin cuerda (otra caída) | Igual: `EB340761` lleva sus *frame events* de suelta aunque no haya cuerda |
| 13, 16 | 249,37 / 258,20 | Cuerda a un NPC (`0xB0100791`) y a una calabaza (`0xB01007E0`), clase 1 | `not a body target (untouched)` |
| 14, 17 | 254,10 / 258,87 | Su suelta | `actor target (the native path; nothing added)` |
| 15, 18 | 254,13 / 258,90 | El 2471 nativo en quien lanza | `slot before 6 state 5 / 7 -> after 6`: el duplicado llega con la ranura ya vacía y no hace nada |

- Todas las líneas `apply` decían `flight 0.000 s`: se leía demasiado pronto (§10.1). Arreglado.
- Ninguna `MISMATCH`, `UNREADABLE`, `FAULTED` ni `REFUSED`. El resumen: `server releases 8: 2471 would be sent 2,
  actor targets 0, other 6`.

### 11.2 Cómo vacía el lanzamiento la ranura del servidor [V código y captura; E cuál de las dos acciones]

- **En la captura** (t 158,7–171): cuerda 5 (`2763` en 158,754); `563271FE` cada 1,5 s mientras sujeta;
  `StartActionReq 1F4EF471` en 167,624 (con objetivo `0xB01003A4`, un *gimmick* «Game Mechanic» cercano);
  `DoFrameEventReq 1F4EF471 f5` **variante 3** en 167,738; `BE0B7EED` en 167,915; `EB340761` con `f0`/`f1`
  (variante 7) en 168,224 y 170,975. **Ningún paquete S>C del gancho** (ni 2471 ni 2763) en todo el tramo.
- **`1F4EF471 f5` no es una suelta:** la variante 3 es `ClientFrameEventAIEvent` → en el servidor
  `ServerFrameEventAIEvent::vf1` `0x27D9E90`, que no llama a vf158.
- **Lo que sí la vacía es el cambio de acción** del personaje en el servidor:
  1. el despacho del cambio de acción `0x2327320` (en el servidor desde `0x2A7BD50`, función virtual del lado
     servidor; en el cliente desde la actualización de `CharacterControl` `0x8610A0`) llama a `[[actor+0x68]+0x78]->vf140` (`0x2328EFC`: es la única
     llamada a vf140 de ese componente en el exe);
  2. para el componente del servidor es `0x1807390` (la de `CommonRemoteCatchActorComponent`);
  3. por cada ranura ocupada (`0x18082A0`) con su byte `+1` puesto, si la acción nueva **no** tiene la marca de
     mantener (`[acción+9] & 4`, `0x1807610`):
     - busca el actor objetivo por la clave de la ranura (`0x1806C00`); **solo si existe** llama a su vf148
       (`0x2903950` en el servidor: comprueba la clave guardada y manda el 2471 con `0x29688C0`);
     - y siempre su propia vf158 `0x1807C80(comp, ranura, 2, 0, 1)`, que deja la ranura en `0x10106`;
  4. en el caso «muerto» o `[acción+0x10C] < 0` hace lo mismo con todas las ranuras vía `0x1807090`.
- **Para un árbol** (actor objetivo 0) no hay vf148: **no sale nada** para los otros clientes. Solo reciben el cambio
  de acción del lanzador por el reenvío normal de acciones.
- **El observador no suelta solo con ese cambio de acción [V S25]:** en S25 la ranura del anfitrión para la cuerda
  del invitado siguió ocupada a través de todas las acciones siguientes del invitado, hasta que el 2763 siguiente
  falló (`RemoteCatch에 실패`). Así que tras un lanzamiento la cuerda se queda en el observador hasta que F3 la limpia
  en el gancho siguiente. Por eso F2 cubre esta raíz (§10.2.1).
- Cuál de `1F4EF471` y `BE0B7EED` no tiene la marca: no medido (las dos caen entre 167,62 y 168,22); la línea nueva
  lo dirá por su hora.
- **En quien lanza** su vf140 del cliente `0x58B380` llama primero a la misma `0x1807390`, pero con los valores de
  su actor, que pueden diferir de los del servidor, y puede mantener la ranura (estado 4). Que suelte antes de que
  llegue el 2471 de F2 **no está medido** (§10.2.1, §10.7 punto 3).
- Otros caminos del servidor a `0x1807C80` (§10.7, punto 7): `0x27D9110` (suelta normal), `0x1807390`/`0x1807090`
  (cambio de acción), vf190 `0x1808410`, `0x2902DC0+0x822`, vf178 `0x2903AD0` y los llamadores externos de vf158
  (`0x156F219`, `0x156F5C9`, `0x2052A6B`, `0x2111E0F`, `0x28DC7D7`, `0x2B44EA0`, `0x2B46AA1`, `0x32517CA`,
  `0x32519A0`).

---

## 12. Sesión 27 con Shadow (kit 27, build `86c3905`): resultados y lo que queda (12 de octubre de 2026)

Solo lectura: no se tocó código. Datos:

| Id | Fichero | Qué |
|---|---|---|
| G27-1 | `…\scratchpad\s27-shadow\cdmp_packets_20261012-210724\` (log `cdmp_20261012-210705.log`) | invitado, paso 1 (`remote_rope=probe`, personaje `0xA010033E`) |
| G27-2 | `…\scratchpad\s27-shadow\cdmp_packets_20261012-213205\` (log `cdmp_20261012-213151.log`) | invitado, paso 2 (`1`, `0xA0100377`) |
| H27-1 | `C:\Emulador CD\backups\p8-sesion27-host\cdmp_20261012-220452.log` + `cdmp_packets_20261012-220503.*` | anfitrión, paso 1 (`probe`) |
| H27-2 | `…\cdmp_20261012-222907.log` + `cdmp_packets_20261012-222919.*` | anfitrión, paso 2 (`1`) |

- Relojes: pared del anfitrión ≈ pared del invitado + 1 h − 1,3 s. En las capturas del paso 2, t(H27-2) ≈ t(G27-2) + 171,7 s;
  en el paso 1, t(H27-1) ≈ t(G27-1) + 155,0 s (mismo 2763 en los dos lados).
- Scripts nuevos en `…\scratchpad\gancho\` (Python `-I`): `s27idx.py` (índice de una captura), `s27win.py` (ventana
  con decodificación), `hook27.py` (quién manda los eventos del dispositivo del gancho), `tools27.py`, `dock27.py`,
  `opcount.py`.

### 12.1 El arreglo hizo lo previsto [V]

El usuario confirma que la cuerda va al árbol bueno y se suelta. Contadores del paso 2 (`remote_rope=1` en los dos):

| | Anfitrión (H27-2, resumen de 22:45:46) | Invitado (G27-2, 21:45:36 y la última línea) |
|---|---|---|
| 2763 aplicados | 11: propios 1, del otro 10 (2 sin cuerpo) | 11: propios 10 (2 sin cuerpo), del otro 1 |
| F1 | 8 traducidos → **escritos 8**, 0 sin resolver, 0 `MISMATCH` | 1 traducido → **escrito 1**; propios: 8 `handle match` |
| F3 | 0 ranuras viejas | 0 ranuras viejas |
| `Apply` fallidos | 2: las dos cuerdas del invitado **sin id de escena** (handle `0x8000000004001156`, §10.7 punto 5), con su par `RemoteCatch에 실패` | 0 (en el paso 1 hubo 3, los esperados en `probe`) |
| F2, suelta normal | 2471 enviados 8; 11 sueltas con ranura ya vacía (2 `ALREADY EMPTY` registradas, 9 plegadas) | — |
| F2, lanzamiento | 3 lanzamientos del invitado → 3 × 2471 enviados; ningún fallo del escritor | — |
| 2471 aplicados | 11: del invitado 10 (ocupada `-> 6` en todos), propio 1 (`slot before 6 state 5`) | 11: propios 10, **todos `slot before 6`** (estado 5 o 7); del anfitrión 1 (`0 state 3 -> 6 state 5`) |
| Fallos de llamadas | F1 0, suelta 0, escritor 0, cuerpo 0; `writer not called` 0 | 0 |

- **Criterio de aborto:** ningún `2471 applied … controlled HERE … slot before` distinto de 6 en ninguno de los dos PC.
  F2 no cortó la cuerda de quien lanza.
- Las dos cuerdas sin id de escena fallan en el anfitrión como antes (no hay traducción). La ranura que dejan sucia la
  limpia el 2471 de la suelta (`slot before 0 state 7 -> after 6`), así que la cuerda siguiente no falla.
- **Paso 1 (`probe`), medida del lanzamiento en el anfitrión:** 3 lanzamientos propios con `the thrower's OWN client
  slot when the server freed it: kind 6 state 7 -> free` (resumen: `free 3, BUSY 0`).

### 12.2 La cuerda propia del invitado no se ve en su pantalla [V datos; E efecto visual]

**No lo causa `remote_rope`.** En el invitado cada 2763 propio se aplica bien: `own throw, handle match`, `err 0x0`,
estado 2/3 y un vuelo coherente con el del anfitrión (p. ej. G27-2 #1: 0,253 s; el mismo gancho en el anfitrión:
0,255 s). F3 no actuó nunca (0 ranuras viejas). Los 2471 de F2 le llegan con la ranura ya vacía (`slot before 6`).

**Lo que falta es el dispositivo del gancho.** Cada personaje lleva acoplado un *gimmick* «Game Mechanic» con los
*sockets* `RemoteCatchPivot`, `HandTwist`, `InteractionPivot` e `InteractionUI`: el lanzador del brazo, de donde sale
la cuerda. Su máquina de estados la mueve el cliente que lo **controla**, con `GimmickExecuteFrameEventReq` 3082 /
`UpdateGimmickReq` 2758 / `GimmickBranchStateReq` 2609, a la vez que las acciones del gancho:

| Hash del evento | Va con | |
|---|---|---|
| `003ECC59` (`6BA22087` / `76B28076`) | `3975EABF` | sacar el lanzador |
| `DA667CC9` | el 2763 | enganche |
| `92786A19` | `2821F20A` | |
| `A55A59FF` | `A747BF2F` | |
| `550D1ED6` | `D20295AD` | suelta |
| `EDFD0EBB` | `8FEDA3FD` | guardar |

**Quién los manda** (`hook27.py`, eventos `C>S` del dispositivo de cada jugador):

| Sesión | Dispositivo del anfitrión | Dispositivo del invitado |
|---|---|---|
| S25 (sin `remote_rope`) | el anfitrión (`0xB0100152`, `0xB0100151`) | **el anfitrión** (`0xB010042A`, `0xB0100429` de `0xA010034E`); el invitado, **ninguno** |
| S27 paso 1 (`probe`) | el anfitrión (`0xB010014A`) | **el anfitrión** (`0xB010042E` de `0xA010033E`); el invitado, ninguno |
| S27 paso 2 (`1`) | el anfitrión (`0xB0100156`) | **el anfitrión** (`0xB01004A1` de `0xA0100377`: 13 «sacar», 8 enganches, 8 sueltas, 64 «guardar»…); el invitado, ninguno |

**Por qué [V]:**
- En el campo del anfitrión **todos** los *gimmicks* llevan como dueño al usuario del anfitrión: el campo `+0x14` de su
  2005 es `0x90100000`. Da igual quién los pidiera. Por ejemplo, el árbol `…FC82` (`0xB0100723`) lo creó la petición
  del invitado y tiene ese mismo dueño.
- El dispositivo del invitado (`0xB01004A1`, 2005 en H27-2 t 237,087) se creó tras la
  `OnDemandEquipDockingGimmickSpawnReq` 2249 del **anfitrión** para `0xA0100377` (t 236,506). El invitado mandó la
  suya ≈ 0,2 s después; las repetidas reciben `UpdateOnDemandEquipDockingNak` 2829 (28 460 en G27-2).
- Solo el cliente que controla el *gimmick* ejecuta su máquina de estados, y el servidor no reenvía esos estados:
  - el invitado no recibe **ningún** paquete con `0xB01004A1` salvo su 2005;
  - `UpdateGimmickAck` 2297 llega 154 veces al anfitrión y 0 al invitado.
- En su propio mundo (antes de entrar al del anfitrión, dueño `0x90100376`), el cliente del invitado sí movía sus
  dispositivos (G27-2 t 35,66).

**Consecuencia [E]:**
- En la pantalla del invitado su lanzador nunca se despliega, y la cuerda, que sale de su `RemoteCatchPivot`, no se
  dibuja aunque el componente `RemoteCatch` esté enganchado.
- El anfitrión sí la ve, porque su cliente ejecuta el dispositivo del invitado.
- **Ya pasaba en S25**, sin `remote_rope`: es de la opción D, no del arreglo.

**Predicción a comprobar [H]:** la cuerda del **anfitrión** tampoco debería verse bien en la pantalla del invitado. El
invitado ni siquiera tiene el dispositivo del anfitrión: su 2005 (H27-2 t 68,1) salió antes de que entrara el invitado
y no está en lo que recibe al entrar (ni en G27-1 ni en G27-2).

**Arreglo de raíz: opciones (sin código; hay que medir antes):**
1. **Que el cliente del invitado controle los *gimmicks* acoplados a su propio personaje.** Habría que buscar en el
   servidor dónde se asigna el controlador de los acoplados (`0x2970790` → `0x28AD170` para la 2249). Pero entonces el
   anfitrión perdería los estados del dispositivo del invitado, porque nadie los reenvía. Haría falta reenviarlos
   también.
2. **Reenviar los eventos del dispositivo al cliente del jugador que lo lleva** y ejecutarlos allí por el camino
   nativo. Primero hay que encontrar la función del cliente que ejecuta un *frame event* de *gimmick* y ver si funciona
   con uno que no controla.
3. **Antes de elegir, en la próxima sesión, mirar en pantalla:**
   - ¿se despliega el lanzador del brazo del invitado cuando lanza?
   - ¿ve el invitado la cuerda del anfitrión?
   - ¿ve el anfitrión el lanzador del invitado?

### 12.3 El árbol no se dobla en la pantalla del otro [V datos; E código]

> **Sustituido por §14 (S28).** Que ningún paquete lleva el doblado sigue valiendo. El mecanismo de `TreePulling` /
> `0x8A30C0` que sigue aquí es **falso**: S28 lo midió y no corre ni en quien lanza.

**Ningún paquete lleva el doblado [V].**
- El árbol se convierte en *gimmick* bajo demanda una sola vez y los dos clientes lo tienen: 2005 + 2145 + 2665 por
  `ToNear`. Por ejemplo, `…FC82` → `0xB0100723` en S27-2 y `0xB0101F00` en S27-1.
- Durante el tirón y el lanzamiento no recibe ningún evento. Lo único que lleva su clave son los
  `GamePlayTriggerInOutReq` 3068 y los eventos de su *trigger* (`677A2658` entrar / `C8F2F4BE` salir), que manda el
  cliente del anfitrión como controlador.
- Los `SpawnOnDemandGimmickNak` 2953 / `UnspawnOnDemandGimmickNak` 2945 son las respuestas de siempre de «ya existe»
  (`p8-s14-viajeros.md` §1.3). No tienen que ver.

**Tampoco es el dispositivo:** el anfitrión ejecuta los eventos del dispositivo del invitado (§12.2) y aun así no ve
doblarse el árbol cuando tira el invitado.

**Es un *frame event* del cliente de quien lanza [E código]:**
- `pa::ClientFrameEventTreePulling` (vtable `0x5584130`, vf1 `0x6D46D0`) pone a 0 un temporizador del controlador de
  movimiento del personaje: `[[[actor+0x68]+0x40]+0xC8]+0x63C`.
- `0x8A30C0`, llamada desde `0x87A700` ← `cdmp_MovementController_UpdateByMode` `0x88F4E0`, avanza ese temporizador
  hasta `DAT_146cb8788` (después vuelve a −1). En cada *frame* aplica una magnitud y una dirección al cuerpo lejano del
  par que cuelga de `[mc+0x188]+0x50`, con la vf4A8 de su dueño.
- Es el tirón sobre el cuerpo del árbol **[E]**; el significado exacto de cada campo es **[H]**.
- **En el observador no se ejecuta:**
  - le llegan las acciones de quien lanza (`StartActionAck` 2991: `6B4EE3CA`, 7 × `563271FE`, `1F4EF471`, `BE0B7EED`,
    H27-2 t 312,5–323,7);
  - pero los *frame events* de cliente de la acción de un personaje remoto no corren en el observador (§2.4 [V]);
  - y `TreePulling` es solo de cliente: no genera ningún `DoFrameEventReq`.

  Así que el observador no arranca el temporizador y el árbol no se dobla.

**Arreglo de raíz (dirección; sin código).** Hacer en el observador lo mismo que hace `TreePulling`, para el personaje
remoto y en el mismo momento: poner a 0 su `+0x63C`, desde el hilo y en el sitio donde el juego lo hace. Antes hay que
**medir**, con ganchos de solo log a la entrada de `0x6D46D0` y `0x8A30C0` en los dos PC:
- qué personaje lo ejecuta y si lo controla ese cliente;
- en qué acción y *frame* salta `TreePulling`;
- si el controlador de movimiento del personaje **remoto** pasa por `0x8A30C0` y tiene el par de cuerpos en `+0x188`.

Si no lo tiene, habrá que crear ese par igual que en quien lanza o dar otro rodeo. Activar en general los *frame
events* de cliente de los personajes remotos queda descartado: duplicaría muchos efectos.

### 12.4 Errores de juego del invitado en S27 [V]

| Error (origen) | G27-1 / G27-2 | Anfitrión | ¿Relacionado con la cuerda? |
|---|---|---|---|
| `RemoteCatch에 실패` (`exe+0x592536`) | 3 / **0** | 7 / 2 | Paso 1: esperado en `probe` (dos cuerdas del anfitrión sin cuerpo con ese handle y una sin id de escena). Paso 2, anfitrión: las dos sin id de escena |
| `Gimmick RelaseCatch 시점의 기믹 이벤트 호출에 실패` (`exe+0x20AD762`) | 9 / 9 | 0 / 0 | No. Ver nota 1 |
| `Projectile을 생성하는데 실패` (`exe+0x21DF65A`, en `0x21DF510`) | 6 / 8 | 0 / 0 | No. Ver nota 2 |
| `responseTrChangeAIStateAck에 실패` (`exe+0xB69976`, `cdmp_TrocTrChangeAIStateAck__Process`) | 10 / 10 | 10 / 10 | No. Ver nota 3 |

1. **`Gimmick RelaseCatch`:**
   - Lo da la suelta del componente `Catch` `0x20AD400` cuando lo agarrado es un *gimmick* (tipo 7): llama a la vf148
     de su componente de *gimmick* (`[+0x68]+0x30`), que devuelve un error.
   - Aquí viene de impactos de proyectil (`0x21DE130` ← `cdmp_OnHitProjectile_Process` `0x2BDFEC0`). Coincide con los
     `CreateProjectileAck` 2250 / `RemoveProjectileAck` 2751 de proyectiles del mundo (`0xB0100EBF/EC0`, `0xB0101784`,
     `0xB0102003`), por ejemplo en G27-2 t 319,8 y 451,4. La de 21:33:57 es anterior a cualquier cuerda.
   - Ya salía en S21, S25 y S26. **[E]** Es la misma raíz que §12.2: el invitado no controla los *gimmicks* del campo.
2. **`Projectile을 생성하는데 실패`:** en los mismos momentos (proyectiles del mundo). Ya salía en S25 y S26.
3. **`responseTrChangeAIStateAck`:** ya salía en S21 y S22, en los dos PC, y no coincide con ningún gancho.

### 12.5 Siguiente paso

1. Las tres comprobaciones en pantalla de §12.2 en la próxima sesión con Shadow.
2. Un módulo `probe` de solo log para el doblado (§12.3: `0x6D46D0` y `0x8A30C0`, con clave del personaje, si lo
   controla ese cliente, `+0x63C` y `+0x188`) y para el dispositivo: quién ejecuta los eventos `003ECC59` / `DA667CC9`
   en cada PC.
3. No se toca `remote_rope`: en S27 funcionó según lo previsto.

---

## 13. Implementado (sondas): medir antes del arreglo de raíz del doblado y del dispositivo

> **La parte A (doblado) quedó refutada en S28 (§14.2).** La parte B (dispositivo) sigue siendo útil.

**Clave** `[session2] rope_probe = 0 | 1`, en **los dos PC** y **solo log**: no escribe nada en el juego ni llama a
ninguna función suya.
- **Ficheros:**
  - `src/mod/session2/rope_probe{.cpp,.h,_logic.h}`;
  - `tests/optd_rope_probe_tests.cpp` (test `optd_rope_probe`);
  - `config` (`s2_rope_probe`), `mod.cpp` (línea `Config:` y arranque en los dos papeles);
  - `session2.cpp/.h`: solo la entrada de la sonda en el reparto de la captura (`sniffer_tap_rope_probe`).
- **Inis de la sesión 28:** `rope_probe=1`, con comentario, en `cdmp.p8-s27-host-paso1.ini`, `cdmp.p8-s26-shadow.ini` y
  `cdmp.p8-s26-shadow-paso2.ini`.
- **Instalación** como `remote_rope`: hilo propio, 3 s en el mundo y `prepare_inline` con reintentos. Comprueba la
  build, `.rsrc`, los bytes de las dos entradas y del único llamador de `0x8A30C0` (`0x87B2BE..0x87B2CA`), y la ranura de
  vtable `0x5584138` → `0x6D46D0`. Si algo no cuadra, `REFUSED` y no se escribe nada.

### 13.1 Qué hace

**A. Doblado del árbol (ganchos de solo log en las entradas):**
- **`0x6D46D0`**, vf1 de `pa::ClientFrameEventTreePulling`. Registra:
  - el personaje (clave) y si lo controla este cliente (`+0xC8`);
  - el temporizador `[[[actor+0x68]+0x40]+0xC8]+0x63C` antes y después;
  - el estado del par de cuerpos `[mc+0x188]+0x50` después;
  - bytes crudos para saber qué acción y *frame* lo disparó: `this+8..+0x28`, los datos del evento y el cuarto
    argumento. Se cruzan con la hora de la captura.
- **`0x8A30C0`**, la aplicación del tirón en cada *frame*:
  - cuenta en el propio gancho, **solo para los personajes de jugador** (clave `0xA…`), las llamadas, las corridas del
    temporizador, los *frames* con el temporizador en marcha y los que empujaron. Los demás personajes (NPC, animales)
    solo suman a un contador global. Son lecturas protegidas y contadores atómicos, bajo el cerrojo del actor que ya
    tiene el llamador;
  - **empuje:** `+0x638` solo se escribe cuando empuja (y −1 al terminar la corrida, que en el mismo *frame* puede
    volver a escribirse `>= 0`). Se lee antes y después de la llamada, y cuenta como empuje solo si después es `>= 0` y
    antes era negativo o distinto. Un empuje con el mismo valor exacto que el *frame* anterior no se cuenta (el `dt`
    cambia, así que es raro);
  - las corridas van **por controlador de movimiento**: otro controlador, o el temporizador que vuelve atrás
    (`TreePulling` lo pone a 0 otra vez), cierran la anterior y abren una nueva;
  - solo al **empezar** y al **terminar** cada corrida manda un evento con el par.

**B. Dispositivo del gancho (sin gancho; la captura del *sniffer*):**
- los eventos del dispositivo que **manda este cliente**: 2758 / 3082 con los *hashes* de §12.2 y 2609 de un dispositivo
  conocido;
- cada 2005 de un dispositivo que **recibe** (con `RemoteCatchPivot` y `HandTwist`): clave, dueño `+0x14` y personaje
  acoplado `+0xEA` (= `+0x104`);
- los 2516 de los dispositivos;
- los personajes de jugador que aparecen en el tráfico del servidor (2991 / 2763).

### 13.2 Líneas que hay que buscar (`grep RopeProbe cdmp.log`)

```
RopeProbe ON (host|guest): log only. ...
RopeProbe: hooked at their entries (5/11 bytes patched): exe+0x6D46D0 (...) and exe+0x8A30C0 (...); nothing of the game is written
RopeProbe #n TreePulling vf1 <hora>: character 0xA0…… (controlled HERE (+0xC8 = 2) | REMOTE here) | movement-controller timer +0x63C -1.000 -> 0.000 | body pair after: pair complete: far yes (ok yes, type T, object vtable exe+0x…, +0x60 0x……) near yes link on | raw: ...
RopeProbe #n tree-pull run START <hora>: character 0xA0…… (...) | timer 0.0xx, +0x638 x | body pair: ...
RopeProbe #n tree-pull run END <hora>: character 0xA0…… (...) | N frames, M with a pull applied (+0x638 >= 0) | timer -1.000 | body pair: ...
RopeProbe #n device 2005 RECEIVED <hora>: device 0xB0…… docked to character 0xA0…… (controlled HERE | REMOTE here | not found) | owner +0x14 0x90100000
RopeProbe #n device event SENT by this client <hora>: device 0xB0…… of character 0xA0…… (...) | deploy 003ECC59 | catch DA667CC9 | release 550D1ED6 ... | the device's 2005 was received | NOT received here
RopeProbe #n player character 0xA0…… in the server's traffic (op 2991|2763) <hora>: ...
RopeProbe summary (host|guest): hooks on | TreePulling vf1 ran N times (characters controlled here H, remote R) | 0x8A30C0: players C calls (P player characters, table full 0), every other character O calls | players: 0xA0…… (controlled HERE): 0x8A30C0 ran C times | timer runs R, running frames F, applied A; 0xA0…… (REMOTE here): 0x8A30C0 NEVER ran | UNKNOWN (table full) | devices (...): 0xB0…… docked to 0xA0…… (...), owner 0x……, 2005 received|NOT received, sent HERE: deploy d catch c 9278 x A55A y release r stow s (3082 copies n, branch b) | ...
```

### 13.3 Qué significa cada resultado para el arreglo de raíz

**Doblado (§12.3).** Cuando el invitado sale volando con el anfitrión mirando, y al revés:

| Medida | Si sale… | Consecuencia |
|---|---|---|
| `TreePulling vf1` en quien lanza | `controlled HERE`, timer `-1 -> 0`, `pair complete` | Confirma el mecanismo de §12.3 |
| `TreePulling vf1` en el observador | ninguna línea para el personaje remoto | Confirma que no corre en el observador |
| `TreePulling vf1` en el observador | `REMOTE here` | Corre pero no dobla: mirar el par y el temporizador |
| Resumen del observador | el personaje remoto: `0x8A30C0 ran C times` | Su controlador de movimiento pasa por `0x8A30C0`. El arreglo puede ser poner a 0 su `+0x63C` en el momento del *frame event* (lo nativo, en el hilo del juego), **si** también tiene el par |
| Resumen del observador | `NEVER ran` (con `UNKNOWN (table full)` no se sabe: la tabla de 64 jugadores se llenó, no debería pasar) | El modo de movimiento del remoto no llama a `0x87A700`. Poner el temporizador no basta: hace falta otro sitio (el modo remoto) o aplicar el empuje de otra forma |
| `run START/END` en quien lanza | `N frames, M applied` con M > 0 | El empuje existe y dura N *frames* |
| Par en el observador (si llega a correr) | `NO pair` / `pair INCOMPLETE` | Hay que crear o enlazar el par (`[mc+0x188]`) del remoto con el cuerpo del árbol: es más que un temporizador |

Los bytes crudos de `TreePulling` (`this+8…`, datos del evento), junto con la hora y el `StartActionReq` que la
precede en la captura, dicen qué acción y *frame* lo dispara: el momento en que el observador tendría que hacer lo mismo.

**Dispositivo (§12.2).**

| Medida | Si sale… | Consecuencia |
|---|---|---|
| Anfitrión: `device event SENT … of character 0xA0…(invitado) (REMOTE here)` | `deploy` / `catch` / `release` del invitado | Confirma que el anfitrión mueve el dispositivo del invitado |
| Invitado: ninguna línea `device event SENT` tras entrar en el campo del anfitrión | — | Confirma que el invitado no mueve el suyo. Arreglo: darle el control de lo acoplado a su personaje, o reenviarle los eventos |
| Invitado: `device 2005 RECEIVED … docked to 0xA0…(anfitrión)` | ausente | El invitado no tiene el dispositivo del anfitrión: la cuerda del anfitrión tampoco saldría de su lanzador. Hará falta anunciárselo (2005) además de los eventos |
| Invitado: `device 2005 RECEIVED … docked to 0xA0…(invitado) (controlled HERE)`, `owner 0x90100000` | presente | Su dispositivo existe pero es del dueño del campo: es el caso de §12.2 |
| Comprobación visual de §12.2 | el lanzador del invitado no se despliega en su pantalla | Junto con lo anterior, causa confirmada |

### 13.4 Riesgos

- **`0x8A30C0` corre en cada *frame* para cada personaje con controlador de movimiento.**
  - Para un NPC o un animal: dos lecturas protegidas y un contador global relajado.
  - Para un jugador: además el temporizador y `+0x638` antes y después, y contadores en la tabla (64 entradas, solo
    claves `0xA…`: no se llena con los NPC).
  - Sin bloqueos ni llamadas al juego; los eventos solo salen al empezar o terminar una corrida.
  - Si la tabla se llenara, el resumen dice `UNKNOWN (table full)` en vez de `NEVER ran`.
- **Los dos ganchos están en entradas verificadas** y solo se acepta el salto de 5 bytes:
  - `0x6D46D0` mueve 5 bytes;
  - `0x8A30C0` mueve 11 bytes enteros (`mov rax,rsp` | `push rdi` | `sub rsp,0x100`), sin direcciones relativas;
  - el recurso de 14 bytes de safetyhook se rechaza (`REFUSED`).
- **Lecturas:** el 5.º argumento de `0x6D46D0` (una ranura de la pila del llamador, en el mismo hilo) se lee con SEH;
  el resto con `game::fast_read`, que repone una página de guarda si la toca.
- **La parte del dispositivo no engancha nada:** depende de `[sniffer] enabled=1`.

---

## 14. El doblado del árbol (S28): la causa real y `tree_bend` (S29)

Build 1.0.0.2976, base `0x140000000`. Sustituye el mecanismo de §12.3 y la parte A de §13 (`TreePulling` /
`0x8A30C0`), que S28 refutó. Lo demás de §12.3 (ningún paquete lleva el doblado) sigue valiendo.

Marcas: **[V]** verificado (datos o código), **[E]** deducido del código sin medirlo en el juego, **[H]** hipótesis.

### 14.1 Datos de S28

| Id | Fichero | Qué |
|---|---|---|
| H28-1 | `C:\Emulador CD\backups\p8-sesion28-host\cdmp_20261013-053042.log` + `cdmp_packets_20261013-053053.*` | anfitrión, paso 1 |
| H28-2 | `…\cdmp_20261013-055640.log` + `cdmp_packets_20261013-055652.*` | anfitrión, paso 2 |
| G28-1 | `…\scratchpad\s28-shadow-p1\cdmp_20261013-043247.log` + `cdmp_packets_20261013-043307.*` | invitado, paso 1 |
| G28-2 | `…\scratchpad\s28-shadow-p2\cdmp_20261013-045951.log` + `cdmp_packets_20261013-050005.*` | invitado, paso 2 (`docked_gimmicks=1`) |

En cada paso los dos jugadores tiraron de árboles 3 o 4 veces (soltar y salir volando). `rope_probe=1` y
`remote_rope=1` en los dos PCs.

### 14.2 Lo que midió `rope_probe` [V]

Las sondas estaban puestas en los cuatro logs (`RopeProbe: hooked at their entries (5/11 bytes patched)`). Había
ganchos propios con ellas activas: H28-1 tiene 5 `2763` propios aplicados, H28-2 3, G28-1 2 y G28-2 3.

| Log | `TreePulling vf1` | `0x8A30C0`, personaje propio | `0x8A30C0`, personaje remoto |
|---|---|---|---|
| H28-1 (resumen 05:56:00) | **0 veces** | `0xA0100001`: 66 136 llamadas, **timer runs 0**, applied 0 | `0xA0100336`: `NEVER ran` |
| H28-2 (06:10:53) | **0** | `0xA0100001`: 32 374, timer runs 0 | `0xA01002D2`, `0xA010110D`: `NEVER ran` |
| G28-1 (04:46:27) | **0** | `0xA0100336`: 39 515, timer runs 0 | `0xA0100001`: `NEVER ran` |
| G28-2 (05:10:21) | **0** | `0xA01002D2`: 25 976 y `0xA010110D`: 1 325, timer runs 0 | `0xA0100001`: `NEVER ran` |

- No hay ninguna línea `TreePulling vf1` ni `tree-pull run START/END` en ningún log.
- **§12.3 queda refutado:**
  - `pa::ClientFrameEventTreePulling` no se ejecuta ni siquiera en el PC de quien lanza, y allí el árbol sí se dobla;
  - el temporizador `+0x63C` no arranca nunca, aunque `0x8A30C0` corre en cada *frame* para el personaje propio.
- `TreePulling` es otro *frame event* de acción, que registra la fábrica de *frame events* de cliente `0x4EDC40` →
  `0x4E6B10`. La cuerda sobre un árbol no lo usa. Que `0x8A30C0` no corra para el remoto no tiene que ver con el
  doblado.
- Dispositivo, solo como dato:
  - en G28-2 el invitado mandó 107 eventos de su dispositivo `0xB01003C0` (deploy 11, catch 3, release 3);
  - H28-2 sigue mandando eventos de ese mismo dispositivo (deploy 8, catch 3). Es tema de `docked_gimmicks`.

### 14.3 Quién dobla el árbol [V código; E en el juego]

```
script de cliente (pa::gameClientScript::RemoteCatchFunctions, vtable 0x56FFB48)
  vf2 0x117F7E0 (p1, dt xmm1, actor r8, slot r9b, estado [rsp+0x28])
    0x117F873: f6 = byte [[[[actor+0x68]+0x40]+0x88]+0x10]+0xF6
    objetivo = FUN_1405b1600([actor+0x68]+0x78, slot); árbol = FUN_140491ac0(objetivo)
    (sin árbol o f6 != 1: 0x1180EA0 → quizá suelta 0x5A7A10 razón 2; sin árbol: 0x117FDA0)
    0x117F911 call 0x1190FD0 (xmm1 dt, r8b = (f6 == 1), r9 actor, [rsp+0x20] estado)
      r8b == 0 → estado+0x38C = 0, +0x370 = 0; nada más
      objetivo = FUN_1405b1600(catchComp, 0)  ← ranura 0 fija
        lee la ranura (0x18082A0) y resuelve su id de escena con ResolveUuidLookup → objeto de escena
      árbol = FUN_140491ac0(objetivo)  ← busca el TreeComponent en [obj+0x210]
      tensión de la cuerda (0x5A4760, [obj+0xAC/+0xB0/+0xB4]) y dirección personaje → árbol (en el plano)
      ENTRADA LOCAL: eje "Key_RemoteCatchTreePulling" / botón "RemoteCatch_ControlPosition_Pull"
                     (DAT_146d69180); si char [[actor+0x68]+0x1B0]+0xB9 != 0, se usa ese valor en lugar de la entrada
      integra la cantidad en estado+0x378 (velocidad +0x37C) y la dirección en +0x380..+0x388
      0x1191532 call SetBend 0x349EA10(árbol, &dir, cantidad)
```

**`cdmp_TreeComponent__SetBend` `0x349EA10`** (la transformación queda verificada):
- dirección en `+0x2A8` / `+0x2B0`, `+0x2AC = 0` y normaliza;
- cantidad en `+0x2A4` y `+0x2B4 = 1`;
- si `+0x59 & 8 == 0`, se registra para actualizarse con `[[comp+0x60]+8 − 0x28]->vf4A8(comp)`;
- después `+0x59 |= 8`.

La lee `0x349ECD0`, que la empaqueta en los datos de instancia del *mesh*: es un doblado **visual**, no física. También
la lee `0x349E340`, que transforma un punto por el doblado.

**Por qué el otro PC no lo ve:**
- `SetBend` tiene **un solo llamador**: `0x1190FD0`.
- `0x1190FD0` tiene **un solo llamador**: `0x117F7E0`, la vf2 del *script*.
- El valor sale de la **entrada local de quien lanza** y nadie lo manda (§12.3: ningún paquete lo lleva).
- El juego es de un jugador: no hay un camino nativo que replique el doblado.
- Aunque `0x117F7E0` corriera en el observador para el remoto, leería la entrada **del observador**: cantidad ≈ 0, o
  se reinicia si `f6 != 1`. **[E]**
- Que no corra para el remoto lo mide `tree_bend` (M1, M2 y el contador `FIGHTS`).

**Por qué el árbol se identifica igual en los dos PCs:**
- `FUN_1405b1600` resuelve el objetivo por el **id de escena de la ranura**: ranura `comp+0x20+slot*0x58`, id de
  16 bytes en `+0x10`.
- Ese id es el mismo en los dos PCs, y `remote_rope` F1 ya deja la ranura del remoto con el árbol bueno.
- Un árbol **sin** id de escena (el *handle* `0x8000000004001156`, §10.7.5) no se dobla ni siquiera en quien lanza:
  `FUN_1405b1600` devuelve nulo.

### 14.4 Puntos de enganche verificados (Ghidra, servidor `cdmp-query`)

Todos en `.rsrc`. Los bytes movidos son instrucciones enteras, sin direcciones relativas a `rip`. Ninguna referencia
apunta al interior del parche (`xrefs` de cada límite interior: 0). `grep` en `src/`: ningún módulo engancha estas
direcciones ni sus funciones.

| Punto | RVA | Función | Gancho | Bytes movidos | Referencias a la entrada | Argumentos en la entrada |
|---|---|---|---|---|---|---|
| M1 | `0x117F7E0` | `RemoteCatchFunctions::vf2` (tamaño `0x15C`) | *mid*, solo log | **5**: `48 89 5C 24 08`; sigue `48 89 6C 24 10` | ranura de vtable `0x56FFB58` y `.pdata`; sin `call` directo | `xmm1` dt, r8 actor, r9b ranura, `[rsp+0x28]` estado |
| M2 | `0x1190FD0` | aplicación del doblado (tamaño `0x5B4`) | *mid*, solo log | **7**: `48 8B C4` + `48 89 58 10`; sigue `48 89 68 18` | `call` en `0x117F911` (`E8 BA 16 01 00`) y `.pdata`; ninguna a `0x1190FD3` | `xmm1` dt, r8b `f6==1`, r9 actor, `[rsp+0x28]` estado |
| M3 | `0x349EA10` | `TreeComponent::SetBend` (tamaño `0x8C`) | *mid*, solo log | **5**: `48 89 5C 24 08`; sigue `57` | `call` en `0x1191532` (`E8 D9 D4 30 02`, tras `vmovss xmm2,[rdi+0x378]` / `lea rdx,[rdi+0x380]` / `mov rcx,rbp`) y `.pdata` | rcx árbol, rdx `&dir`, `xmm2` cantidad |
| M4 | `0x59C0D0` | tick de `RemoteCatch` (tamaño `0x278`) | *inline* (aplica después del original) | **5**: `40 55` `53` `56` `57`; sigue `41 54` | `call` en `0x8A1697` (en `0x8A1640`), `jmp` en `0x8A5DA0` (`E9 2B 63 CF FF`) y `.pdata`; ninguna a `+2`…`+5` | rcx componente (dueño en `[comp+8]`), `xmm1` dt; rdx, r8 y r9 se escriben antes de leerse |

Nativos que llama M4 en `1`, comprobados por sus primeros bytes:
- `FUN_1405b1600` `0x5B1600` (`48 89 5C 24 10 48 89 74 24 18 57`);
- `FUN_140491ac0` `0x491AC0` (`48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18`).

**Hilos [E]:**
- **`0x59C0D0` va como trabajo del planificador de la actualización de actores, `0x8A0FC0`** (cola
  `[DAT_146d691b8+0x98]`):
  - personaje **no controlado aquí**: trabajo `0x8A5DA0`, que solo se programa si alguna ranura tiene estado `!= 7`
    (`byte [comp+0x9BC+slot*0x1A0]`). Con la cuerda enganchada (estado 3) se programa;
  - personaje controlado: trabajo `0x8A5D40` → `0x8A1640` → `0x59C0D0`.
- Que corre para el remoto en el observador está verificado indirectamente **[V]**: su único descendiente `0x59D470`
  manda el `2108` que el anfitrión emitió por cuerdas del invitado en S25 (§2.1).
- No está medido si va en el hilo principal (id en `0x6A652D4`) o en un *worker*, ni en qué hilo corre el `SetBend`
  nativo. Lo miden M3 y M4.

### 14.5 Transporte: paquete de control del túnel Troc, estado 15, en los dos sentidos

Es el mismo camino que `mount_sync`: opcode 1, *slot* −1, `u16 len` y un *payload* que empieza por `u8 estado`
(`src/common/troc_frame.h`). Ocupados antes:

| Estados | Uso |
|---|---|
| 1, 2, 4 | listo, muerto, personaje del anfitrión |
| 3 | `child_reannounce` |
| 5 | `mount_sync` |
| 6–11 | partida propia del invitado |
| 12 | clima |
| 13, 14 | reentrada |

El **15** estaba libre y queda anotado en `troc_frame.h`. `protocol.cpp` / `kVersion` (`cdmp_peer`) no cambia.

| | Envío | Recepción |
|---|---|---|
| **Anfitrión** | En `push_outgoing` (`session2.cpp`), antes de los paquetes del servidor, solo con una conversación activa. Sin conversación, `discard_mount_events` los tira (contador `no conversation`) | `tunnel_on_packet`, justo antes de `mount_sync_on_frame`. Con `rejoin` activo, un paquete de control solo entra dentro de la conversación ya ligada a esa conexión |
| **Invitado** | `guest_take_outgoing` (hilo de red del mod), `g_to_host.push`, **solo mientras juega en el anfitrión** (`remote_live_for_keys()`); si no, se tiran | `guest_deliver`, rama de control, antes del clima; solo con `remote_live_for_keys()` |

Un compañero de versión anterior lo ignora (invitado) o lo tira como «no es un Req del servidor» (anfitrión). No hace
falta un bit de *caps*.

**Formato** (`tree_bend_logic.h`, LE, *payload* de 52 B, *frame* de 57 B):

```
u8 15 | u8 version 1 | u8 kind (1 Sample, 2 End) | u8 slot (0) | u8 flags (bit0 r8b de 0x1190FD0) | u8 0 | u16 0
u32 personaje 0xA… | u32 seq (por emisor, creciente) | u32 run (corrida del emisor)
16 B id de escena (ranura 0 de quien lanza, +0x10)
f32 cantidad [-1, 1] | f32 dir.x | f32 dir.z | u32 reloj del emisor (ms QPC, 32 bits bajos)
```

`decode` exige: tamaño exacto, estado 15, versión 1, `kind` 1 o 2, reservados a 0, clave `0xA…`, `slot < 2`, `seq` y
`run` distintos de 0, id de escena puesto (uno de sus tres primeros *dwords*, como los nativos), valores finitos,
`|cantidad| ≤ 1` y, en un `Sample`, dirección no nula.

**Límites:**
- **Emisor** (`tbl::Sender`):
  - como mucho 40 Hz por (personaje, ranura): 25 ms entre *frames*;
  - uno inmediato si `|Δcantidad| > 0.15`;
  - 20 Hz si cambia (`|Δ| ≥ 0.01`, 50 ms);
  - 4 Hz con la cantidad quieta (250 ms);
  - `End` cuando pasan 150 ms sin `SetBend`. Un árbol nuevo (otro id de escena) cierra la corrida anterior con su
    `End` y abre otra;
  - cola de salida de 8 (`tbl::OutQueue`): se tira el `Sample` más viejo, nunca un `End`.
  - Son ≈ 1,1 KB/s por sentido mientras se tira, y 0 en reposo.
- **Receptor** (`tbl::Receiver`):
  - 4 entradas por (clave, ranura) con las dos últimas muestras de la corrida;
  - una conexión nueva lo olvida todo;
  - descarta `seq` viejo o repetido, salvo tras 2 s de silencio (el emisor se reinició);
  - no aplica nada después de un `End` ni con muestras de más de 500 ms;
  - interpola con 100 ms de retraso entre las dos últimas muestras (si están a 1 s o menos), con la dirección
    normalizada.

### 14.6 `[session2] tree_bend = 0 | probe | 1` (los dos PCs, simétrico)

Ficheros: `src/mod/session2/tree_bend{.cpp,.h,_logic.h}` y `tests/optd_tree_bend_tests.cpp` (test `optd_tree_bend`).
También `config` (`s2_tree_bend`), `mod.cpp` (línea `Config:` y arranque en los dos papeles), `session2.cpp`,
`guest.cpp` y `troc_frame.h`.

Cada cliente es **lanzador** de su propio personaje (controlado aquí, `+0xC8 != 0`, clave `0xA…`) y **observador**
del personaje del otro (`+0xC8 == 0`).

**Instalación:** hilo propio, mundo cargado 3 s.
- Se comprueban la build, `.rsrc`, las entradas, las llamadas `0x117F911` / `0x1191532`, el salto `0x8A5DA0`, la ranura
  de vtable `0x56FFB58` → `0x117F7E0`, las entradas de los dos nativos y el id del hilo principal.
- M1, M2 y M3 son *mid hooks* (`p8_install_mid`: bytes exactos, nada de los 14 B de reserva). M4 es *inline*
  (`prepare_inline`, solo 5 B).
- Si algo falla: `REFUSED`, se quita lo puesto y no se escribe nada.

**Medidas (`probe` y `1`):**

| # | Gancho | Qué registra |
|---|---|---|
| M1 | `0x117F7E0` | por clave: llamadas, en el hilo principal, `f6 == 1`, corridas (huecos de más de 200 ms). Líneas `vf2 run START/END` con ranura, `f6` y tid |
| M2 | `0x1190FD0` | por clave: llamadas con `r8b` = 1 o 0. Guarda en TLS (actor, estado, clave, control, `r8b`) para M3. Si corre para un remoto, suma a `FIGHTS` |
| M3 | `SetBend` | si corre **dentro** del tick `0x59C0D0` (contador de profundidad de M4 en el hilo; revisión B1) y en el de su propio componente; árbol (rcx), dirección (`*rdx`), cantidad (`xmm2`). Es del lanzador si `rdx == estado+0x380` del TLS. Id de escena de la ranura 0 de ese actor. Las del personaje propio van al emisor (también en `probe`) y dan las líneas `bend run START/END`. Tras cada `End`, `bend tail`: `+0x2A4` del árbol cada 100 ms durante 3 s, con `+0x2B4` / `+0x59` al principio y al final, para ver si el árbol vuelve solo. Si corre para un remoto, suma a `FIGHTS` |
| M4 | `0x59C0D0` | después del original y solo para jugadores remotos: llamadas, en el hilo principal, último tid. Con una muestra viva del receptor (*try-lock*), comprueba que el componente es el del cliente (vtable `0x55890D8` y el cerrojo del dueño con código del juego), y que la ranura 0 está enganchada (estado 3) con el mismo id de escena. Línea `would apply` / `applied` / `NOT applied` al cambiar el resultado o, como mucho, una por segundo y lanzador |

**Solo en `1`, en el observador**, con el resultado `would apply`:
1. `obj = FUN_1405b1600(comp, 0)`;
2. `tree = FUN_140491ac0(obj)`;
3. `SetBend(tree, {dx, 0, dz}, cantidad)`.

Esa llamada propia no cuenta en M3 (`own SetBend calls`). Con `End` o una muestra caducada deja de llamar, igual que
quien lanza. Nunca se identifica nada por dirección: clave del personaje, ranura e id de escena.

**Revisión de `b1d30f7` (aplicada en el commit siguiente):**
- **B1 (bloqueaba `1`):** `SetBend` no solo escribe campos. También llama a la vf4A8 de `[[árbol+0x60]+8]−0x28`, que
  registra el componente para actualizarse, y hace `+0x59 |= 8` (lectura-modificación-escritura **sin** `LOCK`).
  - M4 corre como trabajo del planificador de actores (`0x8A5DA0`), y no se sabía dónde corre el `SetBend` nativo.
    Comparar `main` / `worker` **entre PCs** no demuestra que sea el mismo contexto.
  - Ahora `m4_detour` lleva en el hilo un contador de profundidad y el componente del tick. Con eso, M1 y M3 dicen si
    el vf2 / `SetBend` **nativo** de quien lanza corre **dentro** de `0x59C0D0`, y si es dentro del tick de su propio
    componente.
  - El decompilado sugiere que el tick llama a `[comp+0xC68]->vf+0x20(dueño, ranura, dt, comp+0x168+ranura*0x3E0)`,
    una firma parecida a la de vf2. **[H]**: falta medirlo.
  - El resumen compara también, **en el mismo PC**, los tid del `SetBend` nativo propio con los tid del tick de un
    lanzador remoto.
- **L1:** la tabla por personaje cambia a una limpia en cada conexión nueva o cuando se llena (`KeySwitch`, 32
  entradas). El resumen dice `table N/32 used, full F, switches S`.
- **L2:** un árbol nuevo con la corrida **activa** manda su `End` **antes** del primer `Sample` de la nueva
  (`end_first`, `seq` menor). Con la corrida ya terminada no hay un `End` repetido.
- **L3:** si la cola hacia el anfitrión rechaza un *frame*, se cuenta (`refused by the queue to the host`).
- **L4:** el 5.º argumento de `0x1190FD0` (`[rsp+0x28]`) se lee con SEH, como `docked_gimmicks`.

### 14.7 Prueba

**Un PC (anfitrión + `cdmp_peer`, `tree_bend=probe`), antes de Shadow:**
1. Aparecen `TreeBend ON (host, tree_bend=probe)` y, con el mundo cargado,
   `TreeBend: hooked at their entries (5/7/5/5 bytes patched)`.
2. Sin tirar de nada: en `TreeBend summary`, M3 0 y `frames queued 0`; ninguna línea `bend run`.
3. Al tirar de un árbol:
   - `vf2 run START … controlled HERE`;
   - `bend run START … tree scene X:Y … INSIDE|OUTSIDE the RemoteCatch tick 0x59C0D0 … sent`;
   - `bend run END … native SetBend inside this PC's RemoteCatch tick 0x59C0D0: A of N frames (B in its own
     component's tick) | End sent`;
   - `bend tail`.
4. En el resumen: `context: this PC's own native SetBend: inside the RemoteCatch tick … OUTSIDE …`, `M3 SetBend > 0`,
   `frames queued > 0` y `FIGHTS 0`.

Esto ya responde a la mitad de B1, la del contexto del `SetBend` nativo, **sin Shadow**.

**Líneas que buscar (los tres pasos):**

```
grep "TreeBend" cdmp.log                                   # todo (<= 400 líneas de eventos + un resumen por minuto)
grep "TreeBend summary" cdmp.log                           # contadores; "context:" = el criterio B1 en ESTE PC
grep -E "TreeBend (#[0-9]+ )?bend run|TreeBend bend tail" cdmp.log                       # quien lanza (INSIDE/OUTSIDE)
grep -E "TreeBend received|TreeBend #[0-9]+ (would apply|applied|NOT applied)" cdmp.log  # el observador
grep -E "TreeBend summary \((before a new connection|the per-character table is FULL)" cdmp.log  # cambios de tabla
```

**S29, pasos 1 y 2 (`probe`; análisis completo en `s29-resultados.md` §5) [V]:**

| | G1 | H1 | G2 | H2 |
|---|---|---|---|---|
| Tirones (`max − min > 0,2`) / tramos | 7 / 14 | 4 / 8 | 5 / 10 | 4 / 8 |
| Recibidos `new run`/`end`, `bad 0` | 8/8 | 14/14 | 8/8 | 10/10 |
| `would apply` / `state != 3` | 1992 / 82 (96 %) | 2384 / 96 | 2033 / 74 | 1968 / 70 |
| FIGHTS / tabla llena | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| `SetBend` nativo dentro del tick / fuera | 0 / 3865 | 0 / 1073 | 0 / 2399 | 0 / 1184 |
| `SetBend` nativo en el hilo principal | 95 % | 77 % | 95 % | 73 % |
| M4 del remoto en el hilo principal | 100 % | 100 % | 100 % | 100 % |

**Qué significa:**
- El transporte, el emisor y la búsqueda del árbol en el observador funcionan, y no hay peleas.
- **El criterio B1 tal como estaba escrito no se cumple:** el `SetBend` nativo **no** corre dentro de `0x59C0D0`. Va en
  el trabajo de actualización de actores del propio lanzador: en el hilo principal (73–95 %) y en hasta 12 *workers*.
  El nativo no tiene un contexto único.
- M4 del remoto corre **siempre** en el hilo principal. Aplicar desde el hilo principal es uno de los contextos que el
  propio juego usa para `SetBend`.
- **Nuevo [V código, E en el juego]:** `0x8A30C0` lleva una **copia en línea de `SetBend`** (escribe `+0x2A8`, `+0x2A4`
  y `+0x2B4`, llama a la vf4A8 y hace `+0x59 |= 8`) dentro de la rama con el temporizador `+0x63C` en marcha. Esa rama
  escribe también `+0x638` = cantidad, que es justo el «applied» de `rope_probe`. La llama `0x87A700+0xBC6`, con
  `MovementController_UpdateByMode` `0x88F4E0`. Parece el doblado del árbol que tiene el personaje encima.
  - M3 no la ve, y el FIGHTS de antes no la contaba.
  - En S28 y S29, `0x8A30C0` no corrió **nunca** para un jugador remoto (`NEVER ran`).

**Decisión (usuario, para el kit 30): activar `1` con guardas** (commit siguiente a `1e3d8f4`). El paso 3 del kit 29
queda sin usar.

**Guardas de `1`** (en `probe` se evalúan igual, así que sus `would apply` dicen lo que haría `1`):
1. **Solo en el hilo principal** del juego (id en `0x6A652D4`). En otro hilo, `skipped (not main thread)`.
2. **No si el jugador local está doblando el mismo árbol:** su último `SetBend` nativo (M3), con el mismo id de escena,
   hace menos de 150 ms. Se cuenta como `skipped (own bend on the same tree)`.
   - Ese «último `SetBend` propio» lo escriben hasta una docena de hilos y lo lee el principal. Se guarda con un
     *seqlock* (`tbl::OwnBend`), para que la lectura sea siempre una copia coherente. Si no consigue una copia
     estable, cuenta como el mismo árbol (se salta).
   - **2b** (revisión de `6c9e192`): la copia en línea de `SetBend` en `0x8A30C0` del jugador **local** también dobla
     un árbol, el que tiene debajo. Ahí el id de escena no está a mano, así que mientras haya habido una en los
     últimos 150 ms **no se aplica nada**, sea el árbol que sea: `skipped (own inline bend)`. Necesita `rope_probe=1`.
3. **SEH alrededor de los tres nativos** (`FUN_1405b1600`, `FUN_140491ac0`, `SetBend`), solo para violaciones de
   acceso. Al primer fallo, `FAULTED (application off)`: una línea `ERR` y la aplicación queda apagada para el resto de
   la partida. Las medidas siguen (`disabled after a fault`). La línea `ERR` pide **cerrar el juego al acabar la
   sesión**: un fallo dentro de un nativo puede dejar tomado un cerrojo del juego.
4. **Contador de `0x8A30C0`:**
   - lo da el gancho de entrada que ya tiene `rope_probe`; **no hay un segundo gancho**. Llama a `tree_bend_note_pull`
     con la clave, el control, si el temporizador corría y si escribió la copia en línea;
   - por jugador remoto: `0x8A30C0 as a remote N (timer running R, INLINE bends I, during an apply D)`;
   - `FIGHTS` suma ahora M2/M3 para un remoto **y** las copias en línea para un remoto;
   - necesita `rope_probe=1` (los INIs del kit 30 lo tienen). Sin él, el resumen dice `0x8A30C0: NOT measured`.

**Kit 30** (`cdmp.p8-s30-*`; `rope_probe=1` en todos):

| Paso | INIs | `tree_bend` |
|---|---|---|
| 1 | `cdmp.p8-s30-host-paso1.ini` / `cdmp.p8-s30-shadow.ini` | `probe`: comprueba las guardas en sus `would apply` / `skipped …` y el contador de `0x8A30C0` |
| 2 | `cdmp.p8-s30-host-paso2.ini` / `cdmp.p8-s30-shadow-paso2.ini` | `1` |

**Paso 1, criterio para pasar a `1`:**

| Condición | Dónde se mira |
|---|---|
| Lo de S29 se mantiene | `bend run` con `max − min > 0.2` y `sent > 0`; `received … new run/end`, `bad 0`; `would apply … matches … state 3` en el 90 % de las líneas o más |
| La guarda del hilo no lo bloquea todo | en `observer:`, `would apply` mucho mayor que `skipped (not main thread)` (S29: M4 al 100 % en el principal) |
| **FIGHTS 0 contando la copia en línea** | `FIGHTS 0 (… M2/M3 0, inline 0x8A30C0 0)` en los dos PCs, con `0x8A30C0 (rope_probe's hook): …` medido (no `NOT measured`). Por remoto: `INLINE bends 0` |
| La tabla no se llenó | `full 0`, o cambios solo por conexión nueva |

**Paso 2 (`1`):**
- aparecen `TreeBend #n applied` y, en el resumen, `observer: … applied N > 0`;
- en pantalla, el árbol del otro se dobla;
- `application on` (no `OFF after a fault`) y ningún `FAULTED`;
- `NOT applied … no object / no TreeComponent` en 0 o casi;
- `FIGHTS 0` también con `1` activo (`during an apply` es solo informativo);
- ningún error nuevo (`GameErrorLogger`, `exception`).

**Líneas que buscar:**

```
grep "TreeBend summary" cdmp.log        # "application on|probe|OFF after a fault", "0x8A30C0 (rope_probe's hook): … INLINE bends on a remote I",
                                        # "FIGHTS F (… M2/M3 a, inline 0x8A30C0 b)", "observer: … would apply / applied / skipped (not main thread) /
                                        # skipped (own bend on the same tree) / skipped (own inline bend) / FAULTED …", "own-bend records skipped (another writer) K", y por remoto "0x8A30C0 as a remote N (…, INLINE bends I, during an apply D)"
grep -E "TreeBend #[0-9]+ (would apply|applied|NOT applied)" cdmp.log   # el observador, línea a línea (el motivo al final)
grep -E "TreeBend (#[0-9]+ )?bend run|TreeBend bend tail" cdmp.log      # quien lanza
grep "TreeBend received" cdmp.log
grep -E "TreeBend: mode 1's native apply .* FAULTED" cdmp.log           # la guarda 3 (debe salir vacío)
```

**Si el paso 1 dice otra cosa, no se activa:**
- **`FIGHTS > 0`**, sea M2/M3 o la copia en línea: el juego dobla el árbol del remoto por su cuenta en este PC.
  Aplicar encima pelearía con él; es otro diseño.

### 14.8 Riesgos

- **`0x59C0D0` corre para cada actor con `RemoteCatch` y una ranura activa** (también NPC). El gancho lee la clave y
  sale si no es un jugador remoto. Sin cerrojos dentro, salvo un *try-lock* del receptor (si está ocupado, cuenta
  `busy` y no hace nada).
- **`SetBend` llama a la vf4A8 del dueño y escribe `+0x59` sin `LOCK` (B1).** El nativo ya corre en el hilo
  principal y en *workers*; `1` solo aplica en el principal. Si un *worker* doblara el mismo árbol en el mismo
  instante podría perderse un bit `+0x59` (el árbol no se registraría ese *frame*).
  - Las guardas 2 y 2b evitan los casos en que **este** PC dobla a la vez:
    - con su `SetBend` nativo, en el mismo árbol;
    - con la copia en línea de `0x8A30C0`, en cualquier árbol durante 150 ms.
  - No cubren que otro código del juego, que no se ha medido, escriba en el mismo árbol en ese mismo *frame*.
- **SEH y cerrojos:** un fallo dentro de `FUN_1405b1600` puede dejar tomado el cerrojo del dueño del componente. Por
  eso solo se capturan violaciones de acceso, al primer fallo la aplicación se apaga, y antes de llamar se comprueban
  la vtable del componente del cliente y las del cerrojo, como en `remote_rope`. Un fallo así es un error grave que
  hay que mirar, no algo para reintentar.
- **El contador de `0x8A30C0` depende de `rope_probe=1`**, que mantiene su gancho de 11 B en la entrada. `tree_bend`
  no engancha `0x8A30C0`: no hay doble gancho.
- **Los dos tiran a la vez de árboles distintos:** cada PC aplica el del otro; no hay conflicto.
