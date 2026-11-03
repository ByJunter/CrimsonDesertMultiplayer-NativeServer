# Opción D: que el usuario 2 entre en el `ServerField` del anfitrión (campo compartido)

Build **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`). Direcciones en RVA (`exe+…`).
Análisis estático de solo lectura del exe en disco (capstone, `C:\Emulador CD\build\research\`).
No se abrió el juego, no se ejecutó `deploy.ps1` y no se tocó el código del repositorio.

Leyenda: **[E]** leído en el desensamblado normal (`.rsrc`); **[E-x]** leído en código mutado de
`.xtls` (legible); **[S]** suposición razonada.

Contexto previo: `opcion-d-e3-riesgos.md` §1.3-§1.6 (cada `MoveField` saca un `ServerField` nuevo
del pool con `field+0x180` = clave del usuario que viaja).

---

## Resumen y recomendación

**El `ServerField` está hecho para varios usuarios, pero esta build deja entrar solo a uno.**

- El campo guarda una **lista de usuarios** `field+0x2A0` (`{ServerUserActor*, nº de actores}`,
  16 B, n en `+0x2A8`) y un contador de actores de usuario `+0x2B0`. La llenan
  `ServerField::AddActor` y `RemoveActor`. `ToField` la recorre. El campo solo se cierra cuando
  **todos** los actores de usuario se han ido. Todo eso es código de MMO y funciona tal cual
  [E/E-x].
- **Pero `AddActor` tiene un tope de 1:** si el usuario del actor no está en la lista y la lista
  ya tiene a alguien, falla con **`eErrNoHasntEnterFieldPermision`** (`0x6CF7CE8`). Es
  `cmp dword [field+0x2A8], 1 ; jae error` en `.xtls 0x1064C806` [E-x].
- **No hay ninguna rama que reutilice un campo.**
  - `0x2B07790` siempre saca uno del pool.
  - El gestor de campos no tiene ninguna tabla de campos activos por nivel ni por dueño. Solo
    tiene, por nivel, un contador de instancias.
  - En esta build solo existe el pool de **tipo 2**: el gestor está en modo "carga bajo demanda"
    (`gestor+0x69 = 1`). Los pools 0 y 1 solo se crean en el modo MMO (`gestor+0x69 = 0`) [E].
- La única forma de encontrar el campo de alguien es **por su personaje**: `TransformSync+0x4F0`
  (lo llamo "FM") guarda `+0x08` = clave del campo (copia de `field+0x2C8`) y `+0x10` = `ServerField*`.
  `FieldAttacherForActor` `0x2BC4280` → `0x2BBF340` lo lee [E].

**Recomendación: dos ganchos pequeños en ENTRADAS de funciones `.rsrc`**, más un interruptor de
configuración (`[session2] share_field=1`).

| Gancho | Dónde | Qué hace |
|---|---|---|
| **H1** | entrada de `FieldManager::AcquireField` `0x2B07790` | Si el que viaja **no** es el anfitrión y el nivel pedido es el del campo actual del anfitrión: no llama al original. Le da el campo del anfitrión: AddRef con `0x14343A0`, `out+8 = campo`, `out+0x10 = 1`, `out+0x20 = 0` (**no "recién creado"**) y `*res = 0` |
| **H2** | entrada de `FieldMover::EnterField` `0x2BC04F0` | Si el que viaja no es el dueño del campo: antes del original, con los dos locks del campo, mete `{usuario2, 0}` en `field+0x2A0`. Así `AddActor` lo encuentra y no aplica el tope. Si el original falla y la entrada sigue a 0, la quita |

Lo que cambia con esto:
- **Mejoras:**
  - `ToNear`, `ToField` y los sectores pasan a ser comunes.
  - Las ramas "solo dueño" dejan de ejecutarse para el usuario 2: hora global, stages de
    `MoveFieldComplete`, autoguardado motivo 5, `UpdateStageOnceTimer`. **El riesgo R1 de E3 casi
    desaparece**: el usuario 2 recibe la hora **del campo del anfitrión** (`0x2BC0A30`, rama de no
    dueño) [E].
- **Liberación:** no hay doble liberación. El campo vuelve al pool por un único camino
  (`cmpxchg` en `+0x4B`), y solo cuando está cerrado (`+0x4A = 0`) y sin referencias. Solo se
  cierra cuando `+0x2B0` llega a 0, es decir, cuando se han ido el anfitrión **y** el usuario 2
  [E].
- **Lo nuevo que hay que vigilar:**
  - si el anfitrión viaja, el usuario 2 se queda en un campo cuyo dueño ya no está (§5);
  - el cliente del anfitrión recibirá un segundo Kliff con el mismo `characterNo` (E3 §5);
  - el usuario 2 no recibe la sincronización de stages que solo se manda al dueño.

**Alternativa (cruzar observadores entre campos): no.** Los sectores, sus observadores y
aparecer/desaparecer son métodos del `ServerField`, y hacerlo a mano sería mucho más frágil (§4).

---

## 1. Cómo se encuentra el campo de un usuario y qué guarda el gestor

### 1.1 El campo actual de un personaje [E]

`TransformSync+0x4F0` = "FM" (lo que `MoveFieldOnceTimer` llama `rcx`/`r12`):

| Offset | Qué es | Fuente |
|---|---|---|
| `FM+0x00` | puntero a holder → `[+8]` = el actor | `0x2BBFAAC` |
| `FM+0x08` | clave del campo (copia de `field+0x2C8`); `0x30000` = ninguno | `.xtls 0x106DA086`, `0x2BBED97` |
| `FM+0x10` | `ServerField*` en crudo (sin referencia propia) | `.xtls 0x106DA08A` |
| `FM+0x50` | sector actual | `0x2BBEB6C` |
| `FM+0x58` / `FM+0x5A` | bandera / contador "accesos en curso" (`SetField` falla si `+0x5A ≠ 0`, error `0x6CF6B40`) | `0x2BBF386`, `.xtls 0x106D9FED` |
| `FM+0x60` | `FieldInfoKey` de destino (u32) | `0x2BBFAF6` |
| `FM+0x64` | índice estático del nivel de destino (u16) | `0x2BBFB92` |
| `FM+0x68..0x70` | posición de destino | `0x2BC062A` |
| `FM+0x88` / `+0x90` | acompañantes que viajan (paso `0x34`: u16 nivel, u32 clave de actor, posición en `+0x24`) | `0x2BBFD13` |

Funciones:
- **Leer:** `FieldAttacherForActor` `0x2BC4280(TS, out)` → `0x2BBF340(FM, out)`. Da un
  `FieldAttacherForActor` (vt `0x5912E60`) con `+8` = campo, `+0x10` = válido (AddRef con
  `0x14343A0`) y `+0x28` = holder.
- **Fijar:** `0x2BBE900` → `.xtls 0x106D9F50(FM, &res, campo)` [E-x].
  - Primero llama a cada componente `vf(0xA0)`.
  - Falla con `eErrNoInvalidField` si `FM+0x10` ya tenía campo.
  - Si va bien: `FM+8 = field+0x2C8` y `FM+0x10 = campo`.
- **Salir:** `0x2BBEAB0(FM, …)`:
  - quita al usuario de los sectores (`0x2AF2D80`) y al actor de los observadores (`0x2B00380`);
  - llama a `ServerField::RemoveActor` `0x2AFF430`;
  - deja `FM+8 = 0x30000` y `FM+0x10 = 0` [E].

### 1.2 Gestor de campos `[[exe+0x6D696A0]+0x40]` [E]

- `[exe+0x6D696A0]` es un contexto de 0x58 B que se rellena en `0x27C0450`:
  - `+0x40` = `módulo+0xD8` (gestor de campos);
  - `+0x48` = `módulo+0xE8` (SAM);
  - `+0x30` = gestor de guardado;
  - `+0x18` = `módulo+0x148`.
- El gestor se crea en `0x27BF0FD`. **No tiene vtable.** Disposición:

| Offset | Qué es |
|---|---|
| `+0x00/+0x08/+0x10` | pool por tipo 0/1/2 (`[gestor + tipo*8]`) |
| `+0x18` | `WindowsLock<2>` |
| `+0x48`/`+0x4C`/`+0x58`/`+0x60` | hash `FieldInfoKey` (u32) → **entrada de nivel** |
| `+0x68` | "inicializado" |
| `+0x69` | **modo bajo demanda** (1 en el juego) |
| `+0x70` | `SceneObjectGamePlayDataManager` |
| `+0x78` | tabla por nombre de nivel |

- **Entrada de nivel:**
  - `+0` = u32 **nº de instancias vivas** (`inc` en `0x2B07935`, `dec` en `0x2B07CE7`);
  - `+4` = u16 índice estático del nivel;
  - `+8` = datos de navegación/vóxel (se cargan bajo demanda con `0x2A75580`);
  - `+0x10` = objeto de nivel.
- `FieldManager::Initialize` `0x2B06E90(gestor, &res, param, modo)`:
  - con `modo ≠ 0` (bajo demanda), crea **solo el pool tipo 2** (`0x2B06F19`);
  - con `modo = 0` (MMO), crea los pools 0, 1 y 2 con los límites del mundo y precarga la
    navegación de todos los niveles (`0x2B07020`-`0x2B07055`, `0x2B0712E`).
  - **Ni en un modo ni en el otro guarda campos por nivel**: solo las entradas de nivel.
- **No hay tabla de campos activos por dueño ni por nivel.**
  - `field+0x180` solo se escribe en `0x2B079A7` (adquirir) y en `0x2AF74A8` (reinicio)
    [E, barrido de `.rsrc` del área `0x2AF0000`-`0x2B10000`].
  - En `.rsrc`, la única función que usa `gestor+0x40` es `MoveFieldOnceTimer` `0x2BBFA80`
    (`optd/field/fmgr_users.py`). Las 24 lecturas de `[exe+0x6D696A0]` en `.xtls` usan `+0x48`,
    `+0x30`, `+0x28` y `+0x18`, nunca `+0x40` [E-x].

### 1.3 La lista de usuarios del campo: diseño multiusuario [E/E-x]

Disposición relevante de `ServerField` (vt `0x5B29A60`, ctor `0x2AF6670`, 0x310 B):

| Offset | Qué es |
|---|---|
| `+0x08` | puntero a lock R/W (2º lock) |
| `+0x10` | contador de referencias "fuertes" (`lock inc` en `0x1434440`) |
| `+0x18` | lock (1º lock) |
| `+0x48` | u16 contador de adjuntos (lo sube `AddActor` y lo baja `RemoveActor` o el dtor del attacher `0x1434880`) |
| `+0x4A` | **abierto/activo** |
| `+0x4B` | "ya devuelto al pool" (`cmpxchg`) |
| `+0x17C` | pausa de la hora del campo |
| `+0x180` | **clave del usuario dueño** |
| `+0x1F0` | `WindowsRWLock<8,0>` (actores y sectores) |
| `+0x230` | rejilla de sectores |
| **`+0x2A0`** | `vector<{ServerUserActor*, u32 nActores}>`: n en `+0x2A8`, capacidad en `+0x2AC` |
| **`+0x2B0`** | total de actores de usuario en el campo |
| `+0x2B8` | lista de actores (32 B: `+8` = actor), n en `+0x2C0` |
| `+0x2C8` | **clave del campo**: u16 índice, u8 tipo de pool (`+0x2CA`), u8 generación con el bit 7 = "inicializado" |
| `+0x2D0` | entrada de nivel |
| `+0x2E3` | "abierto para actores" |

Quién toca `+0x2A0`:

| Función | Qué hace |
|---|---|
| `ServerField::AddActor` `0x2AFF1E0` → **`.xtls 0x1064C770(campo, &res, actor)`** [E-x] | `usuario = actor+0xA0`. Si hay usuario: lock `+0x18` y luego lock `[+8]`. Exige `+0x4A ≠ 0`. Busca al usuario en `+0x2A0`. **Si no está y `+0x2A8 ≥ 1` → `eErrNoHasntEnterFieldPermision`** (`0x1064C7F6`-`0x1064C80D`). Si está, `++entrada.n`; si no, mete `{usuario, 1}` con `0x40B5B0`. Además `++[+0x48]` y `++[+0x2B0]`. Después, `0x2AFF6A0` (registro real en sectores y lista de actores). Si eso falla, deshace la entrada |
| `ServerField::RemoveActor` `0x2AFF430` → **`.xtls 0x1064CFF0(campo, actor)`** [E-x] | Con los mismos dos locks: `--[+0x48]`, `--entrada.n` (si llega a 0, la quita intercambiándola con la última y hace `--[+0x2A8]`) y `--[+0x2B0]`. Quita el actor de `+0x2B8`. Si había usuario, llama a **`CloseIfNoUserActors` `0x2AFEA00`** |
| `CloseIfNoUserActors` `0x2AFEA00` [E] | Si `[+0x2B0] > 0`, no hace nada. Si no: `+0x2E3 = 0`, expulsa a los actores que queden (`0x2068340`, motivos `0xB`/`0x18`, `eErrNoClosedField` `0x6CF6B8C`) y pone **`+0x4A = 0`** |
| `ToField` `0x2A75EF0` → `0x2AFFBF0` [E] | Con el lock `[+8]` copia la lista y, por cada usuario, `usuario->vf(0xE8)` (su sesión) → destinatarios |
| `0x2AFFA80` (desde `.xtls 0x10665580`) [E] | copia de la lista |
| ctor `0x2AF6670`, `0x2AF6AC0` (dtor), reinicio `0x2AF7360` [E] | inicialización y vaciado |

**Respuesta a la pregunta 1:**
- Sí, el código de MMO admite campos compartidos:
  - hay una lista de usuarios con contador por usuario;
  - el cierre depende del total de actores de usuario;
  - `ToField` difunde a todos.
- La lista la llena `AddActor`, al entrar cualquier actor de un usuario, sea el personaje o un
  acompañante. En `MoveField` eso ocurre en `0x2BC0547` (personaje) y `0x2BC0754` (acompañante),
  dentro de `0x2BC04F0`. También entran por aquí los NPC (`0x2AFA110`), pero sin usuario.
- Lo único que lo impide es el tope de 1 de `AddActor`.
  - El tope compara con una **constante** 1. Antes llama a `0x4A2010` (información estática del
    nivel) y no usa su resultado. Parece un "máximo de usuarios por campo" que en esta build se
    quedó fijo [S].

---

## 2. `0x2B07790` y `MoveFieldOnceTimer`: no hay reutilización

### 2.1 `FieldManager::AcquireField` `0x2B07790(gestor, res*, u8 tipo, out*, usuario)` [E]

- `out` es un attacher de 0x28 B del llamador. En `out+0x24` va la `FieldInfoKey` (copiada de
  `FM+0x60`).
- El quinto argumento (`[rsp+0x28]` en la entrada) es el `ServerUserActor` que viaja (`[pj+0xA0]`).
- Pasos:
  1. Hash `gestor+0x48`… → entrada de nivel `r13`. Si no la encuentra: `eErrNoInvalidFieldInfoKey`.
  2. Si `[r13+8] == 0` y modo bajo demanda: carga la navegación (`0x2A75580`). Si falla:
     `eErrNoInvalidNavigationField_Voxel`.
  3. **`0x2B08CB0(pool = [gestor + tipo*8], &tmp)`** → `.xtls 0x1066CDB0`:
     - saca un campo libre, o lo construye con `0x2B0B640` si la lista libre está vacía;
     - lo reinicia con `vf4` `0x2AF7360(campo, generación+1)`;
     - si sale vacío: `eErrNoMemoryIsLack`.
  4. Bajo `gestor+0x18`: `++[r13]`. Después `0x2AF7820(campo, &res, r13, modo)`
     (`field+0x2D0 = r13`) y `+0x4A = 1`.
  5. **`field+0x180 = [usuario+0x60]`** (`0x2B079A7`) y `0x2AF7C20(campo)` (arrancar).
  6. `out->vf(0x10)` (suelta lo anterior), `out+8 = campo`,
     `out+0x10 = TryAddRef(campo)` (`0x14343A0`), `out+0x18 = 0`, **`out+0x20 = 1`**
     ("recién creado"), y suelta `tmp` (`0x1434880`).
- **El "tipo 2"** es simplemente el índice del pool. Con `gestor+0x69 = 1` solo existe el pool 2
  (§1.2). No tiene nada que ver con reutilizar.

### 2.2 `MoveFieldOnceTimer` `0x2BBFA80(FM, res*)` [E]

```
0x2BBFB2A  [TS+0x3DC] = 1 ; actor->vf(0x180)
0x2BBFBB2  recoge observadores (FM+0x110…) → 0x2B8FB10
0x2BBFCF9  acompañante: si el nivel actual == FM+0x64 → clave = pj+0x60; si no, busca en FM+0x88
           → 0x2A82370(SAM, clave) ; si está en otro nivel → 0x2BBEAB0(su FM) (sale de su campo)
0x2BBFE11  0x2BBEAB0(FM)  → el personaje SALE de su campo actual
0x2BBFEF3  0x780350(&tmp16, FM+0x60) → u16 índice de nivel (0xFFFF → eErrNoInvalidStaticInfo)
0x2BBFF4A  0x2B07790(gestor, &tmp16 (como res), 2, &out(+0x24 = FM+0x60), [pj+0xA0])
0x2BC0019  0x2BC0A30(FM, campo)           hora (rama de dueño o de no dueño)
0x2BC003B  0x2BC0D00(FM, campo, &vec)
0x2BC005B  0x2BC04F0(FM, &res, campo, acompañante)   ENTRAR (SetField + AddActor ×2)
           si falla: si out+0x20 ("recién creado") → campo+0x4A = 0 (lo cierra) ; logout del pj
0x2BC00AF  éxito: FM… = 0x30000 ; …
fin        dtor de out → suelta la referencia del temporizador
```

- **No hay ninguna rama "entrar en un campo ya creado".** `0x2B07790` tiene un solo llamador
  (`0x2BBFF4A`), `0x2B08CB0` también, y el gestor no indexa campos.
  - En el modo MMO el camino es el mismo [E]. Si allí se compartía campo, sería por algo que no
    está en esta build. Por ejemplo, la "permission" de `AddActor`: el error se llama
    `HasntEnterFieldPermision` [S].
- **Detalle útil para H1:** cuando se llama a `0x2B07790`, `*(u16*)res` contiene **el índice
  estático del nivel pedido**. El llamador reutiliza el mismo hueco de pila para el u16 de
  `0x780350` y para `res` [E].
  - Se compara con `*(u16*)([campo+0x2D0] + 4)`: la entrada de nivel guarda ese índice en `+4`
    (`0x2B07124`).
  - Alternativa más formal: repetir la búsqueda en el hash con `out+0x24` y comparar el puntero
    de la entrada con `campo+0x2D0`.

---

## 3. Diseño de la pieza nueva

### 3.1 H1: entrada de `0x2B07790`

Precondiciones; si alguna falla, se llama al original:

1. `share_field` está activo y `usuario` (5º argumento) ≠ el usuario del anfitrión. El mod ya lo
   conoce por P1: es el de la sesión 1. Comparar `[usuario+0x60]` con `[anfitrión+0x60]`.
2. `pjA = [anfitrión+0xD0]` ≠ 0; `TSA = [[pjA+0x68]+0x1A0]`; `FMA = TSA+0x4F0`; `F = [FMA+0x10]` ≠ 0.
3. `[FMA+8] == [F+0x2C8]` (misma generación), `byte [F+0x4A] == 1` y `[F+0x180] == [anfitrión+0x60]`.
4. `word [FMA+0x5A] == 0`: el anfitrión no está en mitad de un acceso o traslado. Mejor aún: no
   compartir mientras el anfitrión tenga un `MoveFieldOnceTimer` en curso [S].
5. **Mismo nivel:** `*(u16*)res == *(u16*)([F+0x2D0]+4)`. Si el nivel es otro, el usuario 2 recibe
   su propia instancia, como hoy.

Acción (copia de los pasos 6 y 7 del original, sin pool, sin `++[r13]`, sin tocar `+0x180`):

```
if (!TryAddRef(F) /*0x14343A0*/) return orig(...);
out->vtbl[2](out);                 // vf(0x10): soltar lo que tuviera (vacío al llegar)
out+0x18 = 0; out+0x08 = F; out+0x10 = (u8)1; out+0x20 = (u8)0;   // NO "recién creado"
*(u32*)res = 0; return res;
```

- **`out+0x20 = 0` es obligatorio.** Si la entrada falla después, `0x2BC006A` cerraría el campo
  del anfitrión (`+0x4A = 0`) [E].
- Referencias: la que toma `TryAddRef` la suelta el dtor de `out` al final del temporizador, igual
  que en el original. **Saldo nulo.**
  - El campo sigue vivo porque `+0x4A = 1` y el anfitrión está dentro.
- **Generación (`+0x2C8`):** no se toca. `SetField` copiará la clave actual del campo del
  anfitrión a `FM+8` del usuario 2, y es coherente.
  - **Contador de instancias del nivel `[r13]`:** no se incrementa. La única devolución al pool
    hará un solo `--`.

### 3.2 H2: entrada de `0x2BC04F0(FM, res*, campo, acompañante)`

```
pj = [[FM]+8]; u = [pj+0xA0];
if (u && [campo+0x180] != [u+0x60]) {           // no es el dueño ⇒ caso compartido
    lock(campo+0x18); lock([campo+8]);          // MISMO orden que AddActor
    if (!find(campo+0x2A0, u)) { push 0x40B5B0(campo+0x2A0, &{u, 0}); inserted = 1; }
    unlock([campo+8]); unlock(campo+0x18);
}
r = orig(FM, res, campo, acompañante);
if (inserted && *r != 0) { mismos locks; si la entrada de u sigue con n == 0 → quitarla
                           (intercambio con la última, --[campo+0x2A8]) }
```

- Con `{u, 0}` ya en la lista, `AddActor` toma la rama "ya está" (`0x1064C813`) y suma `n = 1`,
  `+0x2B0` y `+0x48` como para cualquier usuario [E-x].
  - El acompañante (2ª llamada a `AddActor`, `0x2BC0754`) ya encuentra al usuario.
- Los locks son objetos con vtable: `[vt+8]` = lock, `[vt+0x10]` = unlock, como en
  `0x1064C7AE`/`0x1064C7C1`.
- **¿Por qué no hacerlo dentro de H1?** Se podría, pero entre H1 y `AddActor` pueden fallar
  `0x2BC0D00` o `SetField` y quedaría una entrada `{u, 0}` huérfana. Con H2 la inserción va pegada
  a `AddActor` y se deshace si algo falla.
- **¿Por qué no enganchar `AddActor` `0x2AFF1E0`?**
  - Es un *thunk* `E9 rel32` hacia `.xtls`. Parchear el *stub* de Denuvo es un riesgo distinto que
    no conviene correr [S].
  - Además es ruta caliente: también entran por ahí todas las altas de NPC.

### 3.3 Comprobaciones de dueño (`field+0x180`) que fallarán para el usuario 2 y qué pasa

Con H1, `field+0x180` sigue siendo el anfitrión. Para el usuario 2:

| Sitio | Rama solo dueño | Consecuencia para el usuario 2 | Valoración |
|---|---|---|---|
| `0x2BC0A30` (hora al viajar) [E] | `0x2BC0A78`: escribir el `FieldTime` global | Va a `0x2BC0CBD`: `0x20E55A0(campo)` (hora **del campo del anfitrión**) → `0x21A1CC0` (su `comp+0x450`). El global no se toca | **bueno**: arregla R1 en el `MoveField` |
| `.xtls 0xE52E900`/`0x21A1CC0` [E-x] | copia al global | solo su componente | bueno |
| `0x219D7F0` (`LoadingCompleteReq`, u8 ≠ 0) [E] | **no comprueba el dueño** | global = `comp+0x450`, que ahora es la hora del anfitrión, así que el salto es casi nulo [S]. Pero **`field+0x17C = 0` quita la pausa del campo del anfitrión** | seguir mandando `00` |
| `0x2BC7670` `MoveFieldComplete` `0x2BC7885` [E] | `0x2910DE0(gestor de stages del campo = 0x2B00C70(campo), …)`: aplica y manda stages (`CompareStageTargetParameter`, `ToTarget`) | el usuario 2 **no** recibe la sincronización de stages | bueno para el anfitrión. Para un cliente real (P8) faltará el estado de stages (§5) |
| `0x2BC7670` `0x2BC7BC4` [E] | rama de "recarga pendiente" (`0x28F82A0` → `0x2BCA3D0`…, `0x2BD2690`) | no la toma | bueno (menos R3) |
| `0x2BC7670` `0x2BC8469` [E] | `0x2A82590` + **`0x287EF60` `UpdateStageOnceTimer` 2043** | no crea el temporizador de stages | bueno |
| `0x2BCA3D0` (`GameLoadingStartReq`) [E] | `0x29228C0` (autoguardado motivo 5) y `0x287DA50` | no los llama | bueno (menos R2) |
| `0x20DF100` (67 llamadores) [E] | devuelve el **personaje principal del dueño del campo** | en el campo compartido, todo lo "del dueño" (altas por descriptor `0x2791CD0`, gimmicks `0x22Axxxx`, …) apunta al **pj del anfitrión** | coherente con que el anfitrión simula el mundo (§2.8). Lo que provoque el usuario 2 se atribuye al anfitrión [S] |

Lo que **no** cambia: R2 (autoguardado periódico 2011), R3 (estado global del gestor de
guardado) y R4.

### 3.4 Liberación: ¿doble liberación cuando el usuario 2 se va? No [E/E-x]

1. El usuario 2 viaja o sale → `0x2BBEAB0(su FM)` → `RemoveActor`:
   - `--n` de su entrada (se borra al llegar a 0) y `--[+0x2B0]`;
   - `CloseIfNoUserActors` ve `+0x2B0 > 0` (el anfitrión) y **no cierra**;
   - su `FM` queda en `0x30000/0`.
2. Las referencias de los attachers (`+0x10`/`+0x48`) siempre van por parejas `TryAddRef` /
   `0x1434880`. H1 añade exactamente una, que suelta el dtor de `out`.
3. **Devolver al pool** (`vf2` `0x2AF7640` → `0x2B07CB0`: `--[entrada de nivel]`, `+0x4A = 0`,
   a la lista libre) solo ocurre cuando:
   - `+0x4A == 0` (cerrado);
   - `+0x10 + +0x48 == 0`;
   - y se gana el `cmpxchg` de `+0x4B`.
   Es decir: **un solo ganador** y solo después del cierre, que exige que se hayan ido todos.
4. **Riesgo real (no de doble liberación):** que el usuario 2 desaparezca **sin** pasar por
   `RemoveActor`. El logout no se puede leer (B6). Su entrada seguiría en `+0x2A0`, `ToField`
   llamaría a `vf(0xE8)` de un usuario ya desconectado (`+0x1F8 = 0` → sesión nula) y `+0x2B0`
   nunca bajaría a 0 [S].
   - Mitigación: en la desconexión de la sesión 2, si su `pj` sigue con `FM+0x10 = F`, forzar la
     salida por el camino nativo, o al menos registrarlo.

---

## 4. Alternativa: que `ToNear` y los observadores crucen campos. No

- `ToNear` (`0x2BC4F70`) toma el sector de `TS+0x540`/`FM+0x50`, y los observadores de ese sector
  salen de `sector+0x68` (`0x2AF2E60`). Los sectores pertenecen a la rejilla del campo
  (`field+0x230`).
- Altas y bajas (`0x2AF8AF0` → `0x2B000C0`/`0x2B00380`) y la limpieza (`0x2BBEAB0`,
  `CloseIfNoUserActors`) son métodos del `ServerField`, sobre sus propias listas y locks.
- Para cruzar campos habría que, a mano:
  - registrar al usuario 2 como observador en los sectores del campo del anfitrión, y viceversa;
  - replicar las altas y bajas de actores en los dos sentidos;
  - quitarlo todo al salir.
  Eso es reimplementar `0x2AF8AF0` fuera del juego, con locks de dos campos y sin el cierre nativo.
- Además, el mundo del usuario 2 seguiría duplicado (NPC, gimmicks y stages en dos instancias), con
  dos estados distintos.
- **Conclusión:** compartir el campo (H1 + H2) es mucho más pequeño y usa el diseño multiusuario
  que ya existe.

---

## 5. Riesgos

| # | Riesgo | Gravedad | Mitigación |
|---|---|---|---|
| C1 | **El anfitrión viaja** (viaje rápido, muerte con recarga o `ReloadStage`) con el usuario 2 dentro. El anfitrión sale (el campo no se cierra) y saca **otro** campo del pool. El usuario 2 se queda en un campo cuyo dueño (`+0x180`) ya no está. `0x20DF100` devolverá el pj del anfitrión, que está en otra instancia [E + S] | alta para E4, nula para E3b | E3b: no viajar ni morir. E4: (a) que el usuario 2 siga al anfitrión (forzarle un `MoveField` al nuevo campo); o (b) ampliar H1: si el que viaja **es** el anfitrión y existe un campo abierto del mismo nivel con `+0x180` = anfitrión y el usuario 2 dentro, reutilizarlo. Hay que comprobar antes que la recarga de stages no espera un campo limpio [S] |
| C2 | **Segundo Kliff en el cliente del anfitrión:** ahora sí le llega `ActorLoadDataAck` del pj 2, con `characterNo 0x7FFF…` repetido (E3 §5) | media | Esperado. Vigilar la pantalla de equipo. Es la prueba de que H1/H2 funcionan |
| C3 | Carrera: H1 lee el `FM` del anfitrión desde otro hilo del servidor mientras el anfitrión viaja | baja | Condiciones 3 y 4 de H1 (clave y `+0x5A`). No lanzar E3b durante un viaje |
| C4 | Entrada huérfana en `+0x2A0` si el usuario 2 cae sin `RemoveActor` (§3.4) | media | Registrar `+0x2A8`/`+0x2B0` al cerrar la sesión 2. Forzar la salida nativa |
| C5 | El usuario 2 no recibe stages ni la recarga pendiente (solo dueño) | baja en E3b | Para P8 habrá que mandarle el estado de stages del campo [S] |
| C6 | `LoadingCompleteReq` con `01` quita la pausa (`+0x17C = 0`) del campo **del anfitrión** | baja | Mandar `00` |
| C7 | Lo que haga el usuario 2 en el campo (gimmicks, altas, misiones de campo) se atribuye al pj del anfitrión (`0x20DF100`) | media, a futuro | Ninguna en E3b. Es el comportamiento "el anfitrión es el dueño del mundo" |
| C8 | Integridad de Denuvo sobre `.rsrc` | igual que los demás ganchos | Solo entradas `.rsrc`. Nada en `.xtls` ni en *thunks* |
| C9 | Si H2 no se aplica (firma no encontrada), `AddActor` falla con `0x6CF7CE8` y el pj 2 hace logout (`0x2068340(pj, 0xB, …)`). El campo del anfitrión **no** se cierra, gracias a `out+0x20 = 0` | baja | Activar H1 solo si H2 está instalado |

---

## 6. Etapa de prueba E3b (propuesta)

**Requisitos:**
- E3 tal cual (sesión 2, cliente sintético, P6 activo);
- `share_field=1`;
- H1 y H2 con log;
- el anfitrión quieto en un nivel normal: sin viajar, sin morir y sin menús de guardado.
  `LoadingCompleteReq` con `00`.

**Log antes del guion:**
- del anfitrión:
  - `F = [FMA+0x10]`, `[F+0x2C8]`, `[F+0x180]`, `[F+0x2A8]` (esperado 1), `[F+0x2B0]` y `[F+0x48]`;
  - `[F+0x10]`, `[F+0x4A]` y la entrada de nivel `[[F+0x2D0]]` (nº de instancias);
- los 32 B de `exe+0x6A6D238`.

**Lo que se espera ver:**

| Momento | Línea esperada |
|---|---|
| `GameLoadingStartReq` del usuario 2 → 2027 | `H1: viajero clave=<u2> nivel=<idx> == nivel anfitrión → COMPARTIR F=<ptr> clave=<…> refs +0x10 n→n+1` y **no** `orig` |
| (no debe aparecer) | ninguna llamada a `0x2B08CB0` ni `++[entrada de nivel]`. El nº de instancias del nivel no cambia |
| `0x2BC04F0` | `H2: no-dueño, insertado {u2,0}; tras orig res=0; +0x2A8 = 2, n(u2) = 1 (o 2 con acompañante), +0x2B0 = previo + k` |
| fin del temporizador | `[F+0x10]` vuelve al valor previo (el dtor de `out`) |
| tras el `MoveField` | `FM2+0x10 == F` y `FM2+8 == [F+0x2C8]`. `exe+0x6A6D238` **sin cambios** |
| `MoveFieldCompleteReq` | no se crea `UpdateStageOnceTimer` 2043 para el pj 2. El anfitrión no recarga nada |
| tráfico | **sesión 1:** `ActorLoadDataAck` del pj 2 (`ToNear`) y Ack de `ToField` provocados por él. **Sesión 2:** `ActorLoadDataAck` del anfitrión y de los NPC cercanos |
| no debe aparecer | `eErrNoHasntEnterFieldPermision` (`0x6CF7CE8`), `eErrNoInvalidField` ni logout del pj 2 |
| cierre de la sesión 2 o salida | `RemoveActor` del pj 2: `+0x2A8 = 1`, `+0x2B0` = previo, `[F+0x4A] = 1` (el campo sigue abierto) y el anfitrión sigue jugando |

**Criterio de éxito:**
- el anfitrión ve aparecer al segundo Kliff;
- ni el `FieldTime` del servidor ni el estado del campo del anfitrión cambian;
- al salir el usuario 2, el campo sigue abierto y sin errores.

**Criterio de aborto:**
- cualquier cierre del campo del anfitrión (`+0x4A = 0`);
- `0x6CF7CE8` (H2 no funciona);
- `+0x2B0` descuadrado tras la salida.

---

## 7. Direcciones (build 2976)

### 7.1 Funciones que se proponen enganchar (entrada `.rsrc`)

| RVA | Función | Primeros bytes (cortes) | RIP-rel. / saltos internos | AOB única (40 B) |
|---|---|---|---|---|
| `0x2B07790` | **H1** `FieldManager::AcquireField(gestor, res*, u8 tipo, out*, usuario)` | `48 89 5C 24 10` \| `48 89 74 24 18` \| `48 89 7C 24 20` \| `55` \| `41 54` \| `41 55` \| `41 56` \| `41 57` (cortes 5, 10, 15, 16, 18, 20, 22, 24) | no / no | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 4D 8B F9 41 0F B6 D8 48 8B` |
| `0x2BC04F0` | **H2** `FieldMover::EnterField(FM, res*, campo, acompañante)` | **los mismos 24 B** que `0x2B07790` (cortes 5, 10, 15, 16, 18, 20, 22, 24) | no / no | `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 4D 8B F1 4D` |

- Las dos comparten 24 B de prólogo (820 coincidencias en el exe con 24 B). Hacen falta 40 B o
  resolverlas por llamada:
  - `call` en `0x2BBFF4A` → `0x2B07790`;
  - `call` en `0x2BC005B` → `0x2BC04F0`;
  - las dos dentro de `MoveFieldOnceTimer` `0x2BBFA80`, cuya AOB de 40 B es única:
    `48 89 54 24 10 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 68 FF FF FF 48 81 EC 98 01 00 00 C5 F8 29 B4 24 80 01 00`.
- Un parche de 5 B (`jmp rel32`) o de 14 B corta limpio en 5 o 15.

### 7.2 Resto de direcciones

| RVA | Qué es |
|---|---|
| `0x2BBFA80` | `MoveFieldOnceTimer` 2027 (procesa) |
| `0x2B08CB0` → `.xtls 0x1066CDB0` | `Pool<ServerField>::Acquire` (generación = byte alto de `+0x2C8`, + 1) |
| `0x2B07CB0` | `FieldManager::ReturnField` (`--[entrada de nivel]`, `+0x4A = 0`, lista libre del pool `[gestor + byte[+0x2CA]*8]`) |
| `0x2AF7640` | `ServerField` `vf2` (devolución al pool) |
| `0x2B06E90` | `FieldManager::Initialize` (modo `+0x69`: solo el pool 2 en un jugador) |
| `0x2B07F70` | creación de un pool por tipo |
| `0x27BF0FD`…`0x27BF1A9` | ctor del gestor de campos (`módulo+0xD8`) |
| `0x27C0450` | rellena `[exe+0x6D696A0]` (`+0x40` = gestor de campos) |
| `0x2AFF1E0` → `.xtls 0x1064C770` | `ServerField::AddActor` (**tope de 1 usuario: `0x1064C806`**) |
| `0x2AFF430` → `.xtls 0x1064CFF0` | `ServerField::RemoveActor` |
| `0x2AFEA00` | `ServerField::CloseIfNoUserActors` (`+0x2B0 == 0` → expulsar y `+0x4A = 0`) |
| `0x2AFFBF0` | `ToField`: recorre `field+0x2A0` → `vf(0xE8)` |
| `0x2AFFA80` | copia de la lista de usuarios |
| `0x2BC4280` / `0x2BBF340` | `FieldAttacherForActor` (leer `FM+0x10`) |
| `0x2BBE900` → `.xtls 0x106D9F50` | `FieldMover::SetField` (`FM+8`/`FM+0x10`) |
| `0x2BBEAB0` | `FieldMover::LeaveField` (sectores, observadores, `RemoveActor`, `FM = 0x30000/0`) |
| `0x2BC0A30` | hora al viajar (no dueño → `0x20E55A0` + `0x21A1CC0`) |
| `0x14343A0` / `0x1434880` / `0x1434470` | `TryAddRef` / dtor del attacher / AddRef interno |
| `0x40B5B0` | `vector<16 B>::push_back(vec, &e)` |
| `0x20DF100` | personaje principal del dueño del campo |
| `0x2910DE0`, `0x2B00C70` | sincronización de stages al entrar (solo dueño), gestor de stages del campo |
| `0x6CF7CE8` | `eErrNoHasntEnterFieldPermision` |
| `0x6CF6B54` / `0x6CF6B8C` / `0x6CE51D8` / `0x6CF7340` / `0x6CDBA18` / `0x6CF6B40` | `InvalidField` / `ClosedField` / `InvalidFieldInfoKey` / `InvalidNavigationField_Voxel` / `InvalidStaticInfo` / error de `SetField` con acceso en curso |

## 8. Scripts y volcados (`C:\Emulador CD\build\research\optd\field\`)

| Fichero | Qué es |
|---|---|
| `vtdump.py` | slots de una vtable (`ServerField` `0x5B29A60`) |
| `fmgr_users.py` → `fmgr_users.txt` | usos de `[[exe+0x6D696A0]+0x40]` |
| `prolog.py` → `prologues.txt` | prólogos, cortes, RIP-relativo y saltos internos |
| `both.py`, `g_2a0.txt`, `g_2a8.txt`, `gx_2a0.txt`, `gx_2b0.txt`, `w_4a.txt`, `wx_4a.txt` | barridos de `+0x2A0/+0x2A8/+0x2B0/+0x4A` |
| `d_*.asm` / `x_*.asm` | volcados: `2bc4280`, `2affbf0`, `2a75ef0`, `2b08cb0`, `2af7820`, `2af7c20`, `2af7360`, `14343a0`, `2a82370`, `2af6670_ctor`, `2affa80`, `2af8690`, `2af9ae0`, `2bbeab0`, `2bc04f0`, `2bc0d00`, `2aff1e0`, `x_1064c770_addactor`, `x_1064cff0`, `2afea00_closeifempty`, `2b07cb0_release`, `2b07f70_mgrinit`, `2b06e90_mgrsetup`, `27c0450`, `27beb50`, `28cbe40`, `2a987c0`, `2bd5b60`, `2bd5dd0`, `2bbe900`, `x_106d9f50_setfield`, `2bbf340_getfield` |

Volcados anteriores que se reutilizan (`optd\e3\`): `d_2b07790_getfield.asm`, `d_2bbfa80.asm`,
`x_1066cdb0_fieldpool.asm`, `d_2bc7670.asm`, `d_2bc0a30.asm`, `d_2bca3d0.asm`, `d_2910de0.asm`.
