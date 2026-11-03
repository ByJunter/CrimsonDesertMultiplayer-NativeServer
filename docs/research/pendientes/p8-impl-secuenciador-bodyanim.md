# P8 · Sesión 13: NPC de secuenciador de pie — medida de BodyAnim y 2524 de escenas de mundo

10 de octubre de 2026, rama `p8-seq-bodyanim-diag` (sobre `opcion-d` 51f9599). Build del juego **1.0.0.2976**
(PE `0x6AB28F00`, base `0x140000000`), direcciones en **RVA**. No se lanzó el juego.

**[V]** = comprobado (bytes con capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`, SHA-256
`57DA440D…BD67FDE1E7`, o logs de la sesión 12). **[E]** = interpretación. **[H]** = pendiente de la prueba.

Fuentes: `p8-s12-npc-familias.md` §5 (familia (d)), informe externo `npc-raiz-s11-20261010-work\INFORME.txt` §3 y §5
(y su `bodyanim\informe.txt`), `p8-impl-escenas-play.md` §16 (2524 en X, comprobación interna).

## 0. Qué problema mide

- **Familia (d)**: los NPC de secuenciador del anfitrión (Stranger `0xB0100077/78` de la escena `0x19`, guardias
  `0x7E/7F/80` de `0x16C`, `DF4`–`DF9` de `0x1BD/0x1BE`…) se quedan **de pie en espera** en el invitado.
- En el anfitrión, el evento de escena (2524 `StageOnBranchEventAck` → `0xA9E480` → … → `0xADED90` → **BodyAnim
  `0xA79770`**) construye el gráfico dinámico `SeqBodyAnim` en `CC+0x70` (y `CC+0x78`) del NPC, con el clip y el
  tiempo de la escena. Las acciones que resultan (`Req+2D3=1`, hashes `0x837487D4`/`0x35DD392F`) no se relevan y,
  aunque se relevaran, no llevan el clip.
- En la sesión 12 los 117 × 2524 llegaron al invitado pero fueron a **M** (`guest_seq_play_live=0` → no enrutados):
  nadie construyó el gráfico. `GuestSeqRestore` sustituyó la acción por la espera `0x26107D3C`.
- En la sesión 11 (fase 2) se enrutaron **todos** los 2524 a X (atados a `guest_seq_play_live=1`) y la imagen
  empeoró; el tick vivo `0xAA3E70(X)` nunca corrió (`live calls 0`).

Esta rama **solo mide** (nada cambia el juego con lo que se entrega) y prepara un modo que enruta **solo los 2524 de
escenas de mundo**.

## 1. Lo que se instala

### 1.1 Claves (`[session2]`, solo invitado)

| Clave | Valores | Ini Shadow | Ini anfitrión | Qué hace |
|---|---|---|---|---|
| `guest_seq_route_2524` | `0` \| `probe` \| `1` | **probe** | 0 | 2524 de escenas de mundo (§3). Independiente de `guest_seq_play_live` |
| `guest_seq_route_2524_allow` | recursos hex del 2661 | vacío | — | Con `1`, limita el enrutado a esos recursos (vacío = toda escena MUNDO) |
| `guest_seq_bodyanim_diag` | `0` \| `1` | **1** | 0 | Medida de solo lectura (§2). Necesita `guest_seq_route=probe` o `1` |

Inis tocados: `cdmp.p8-s7-shadow.ini`, `cdmp.p8-s7-shadow-fase2.ini`, `cdmp.p8-s12-shadow-seguro.ini` (las tres
claves al final) y `cdmp.p8-s7-host.ini` (a 0).

- **Ojo con la fase 2:** con `guest_seq_route_2524=probe`, el ini de fase 2 (`_live=1`) **ya no enruta** los 2524 a X
  (solo los clasifica). Para el comportamiento de la sesión 11, `guest_seq_route_2524=0`.

### 1.2 Puntos y bytes [V]

Todos en `.rsrc` (`[0x1000, 0x51EB000)`), fuera de `.xtls`. Cada uno con `p8_install_mid` (bytes, sección y
reubicación exacta comprobados; si algo no cuadra, no se instala y una línea ERR lo dice).

| Punto | Bytes del parche | Contexto comprobado | Para qué |
|---|---|---|---|
| `0xA79770` entrada de BodyAnim | `48 8B C4 48 89 58 18` (7 B: `mov rax,rsp ; mov [rax+0x18],rbx`) | 16 B en la entrada; `0xA797B8` `45 0F B6 F8 4C 8B F2 48 8B F1` (r14 = desc, rsi = body) | Medida (§2.2). Mismos bytes que verifica P8Probe (`s1_diag`): con `s1_diag=1` el punto es suyo y aquí no se instala |
| `0x2432F60` entrada de la configuración del gráfico | `48 89 5C 24 20` (5 B: `mov [rsp+0x20],rbx`) | 16 B en la entrada; llamadas `0xA79C19` `4D 8B C4 48 8B CB E8 3C 93 9B 01` y `0xA79FBB` `4C 8B 85 38 01 00 00 48 8B CB E8 96 8F 9B 01`; nombre `0xA79BCD`/`0xA79F6F` | Medida (§2.3) |
| `0xC08178` (manejador 2524) | `48 8D 4D F8 48 89 4C 24 28` (9 B) | los de `p8-impl-escenas-play.md` §3 | Ya existía; ahora también se instala con `guest_seq_route_2524 != 0` |
| `0xA9E55B` (dentro de `0xA9E480`) | `41 BF FD 01 00 00` (6 B) | los de §16.4 | Ya existía (comprobación interna); va con el anterior |

- Ningún salto rel8/rel32 de `.rsrc` cae en `0xA79771..0xA79776` ni en `0x2432F61..0x2432F64` (barrido de bytes de
  toda la sección ejecutable) [V].
- `rsi` solo se escribe una vez en `0xA79770..0xA7A254` (`0xA797BF mov rsi,rcx`) [V]: en la entrada de `0x2432F60`
  llamada desde `0xA79C1F`/`0xA79FC5`, `rsi` es todavía el `body` de BodyAnim.
- En el gancho: lecturas SEH, atómicos y un anillo sin bloqueo. Ni CRT, ni reservas, ni cerrojos, ni llamadas al
  juego. Las líneas salen del hilo de diagnóstico cada 5 s.

## 2. `guest_seq_bodyanim_diag`: qué se mide

**Vigilados** = la lista G5 de `guest_seq_route` (`guest_seq_watch` + todos los NPC de cada 2661 enrutado, hasta 64).
En la sesión 12 eso incluye a todos los de la familia (d).

### 2.1 `GuestBodyAnim [G5 body] 0xKEY: …` (cada 5 s si cambia; cada 60 s aunque no cambie)

- `type` = `[[actor+0x88]+1]` (3 = NPC).
- `CC` = `[[actor+0x68]+0x40]`, comprobando `CC+8 == actor`.
- `CC+0x70` / `CC+0x78`: los gráficos `SeqBodyAnim` de las capas 0 y 1 (0 = nunca construidos). `(INVALIDATED +0x15)`
  si su byte `+0x15` vale 1.
- `+0x23A`: BodyAnim lo pone a 1 tras encolar sus entradas (`0xA7A181`).
- `+0xA8`: acciones pendientes.
- `L0` / `L1`: por capa, `current` = hash de la acción en curso `[[capa+0x10]+0x18]`, `+0x3C` = hash que pide la
  restauración, y si hay `chart` (`+0x110`). Marca `seq_bodyanim` y `WAIT`.
- `hooks: BodyAnim n, configure L0 n / L1 n`: llamadas vistas por los ganchos para ese actor en la sesión.
- Estado final: `GRAPH BUILT (CC+0x70)` | `seq_bodyanim WITHOUT graph` | `idle wait 0x26107D3C, no graph` |
  `no graph, other action` | `NOT in this client` | `no control component`.

### 2.2 `GuestBodyAnim [BodyAnim 0xA79770] … | NPC 0xKEY …`

Una por llamada a BodyAnim de un vigilado (límite 48 cada 5 s; todas se cuentan):
- `body`, `CC`, `+0x70`/`+0x78` **antes** de la llamada, `+0x23A`;
- `desc` y el **clip**: texto y hash de `[[desc+0x18]+0x18]` (`{char*, len, hash@+0xC}`, lo mismo que recibe
  `0x2432F60` en `rdx`);
- los tiempos de `desc` `+0x28/+0x2C/+0x30/+0x3C` (floats que BodyAnim pasa como p5..p8) y `p3`/`p4`.

### 2.3 `GuestBodyAnim [configure 0x2432F60, layer N] …`

Solo cuando la dirección de retorno es `0xA79C24` (capa 0) o `0xA79FCA` (capa 1); los demás llamantes se cuentan
como «other callers» y no se leen. Por vigilado: el gráfico (`rcx`), los dos nombres (clip y mezcla, texto + hash),
`xmm3`, p5..p8 (floats) y el objeto de recurso `[p9]` (`p9 = &body+0xE8`).

### 2.4 `GuestBodyAnim summary` (cada 60 s)

Llamadas a BodyAnim (todas / claves NPC / vigilados), a `0x2432F60` (de BodyAnim L0/L1, otros, vigilados) y la
cohorte de vigilados por estado, cuántos con `CC+0x78`, a cuántos se les vio BodyAnim y cuántos de ellos siguen sin
`CC+0x70`.

## 3. `guest_seq_route_2524`: clasificación y modos

### 3.1 Modos

| `guest_seq_route_2524` | Punto `0xC08178` | ¿Enruta? |
|---|---|---|
| `0` | solo con `guest_seq_play_live=1` (como antes) | como antes: todos, con `guest_seq_play=1` + `_live=1` |
| `probe` | sí (y `0xA9E55B`) | **nunca**; clasifica, juzga y cuenta |
| `1` | sí (y `0xA9E55B`) | solo escenas **MUNDO**, con `guest_seq_route=1`, X tickado por `guest_seq_play`, `pre_2524` (X y M en 3, X sin cargas) y la comprobación interna `0xA9E55B` bajo el cerrojo del juego |

En `probe` el 2524 sigue yendo a M, exactamente como en la sesión 12: el único cambio es que los dos ganchos están
puestos (el interno no hace nada: solo actúa en una llamada que el gancho de enrutado mandó a X).

### 3.2 Clasificación (`gsr::classify_2524`, en este orden)

| Clase | Regla | Ejemplos sesión 12 |
|---|---|---|
| `unknown sequencer` | no se vio su 2661 en este cliente | — |
| `denied` | recurso en `guest_seq_route_deny` | `0x213` / `0x8CD771B1` (sesión 9) |
| `no NPC bound` | su 2661 solo enlaza personajes (escena propia/personal del anfitrión) | `0x164`–`0x167` (solo `0xA0100001`) |
| `its NPCs are not in this client` | ninguno de sus NPC está aquí ahora | — |
| `no NPC of type 3` | ninguno de los presentes es de tipo 3 | `0x163` (Homing Pigeon) [E] |
| `name not read` | no se pudo leer/validar el nombre de la escena: **falla cerrado** | — |
| `function scene` | nombre `cd_seq_funcnpc_*` (tiendas) | `0x197` butcher, `0x198`, `0x199`, `0x19A`, `0x19B`, `0x1A4`, `0x1A5`, `0x1B2`, `0x1BF`, `0x1C0` |
| `NOT on allow` | lista `_allow` no vacía y el recurso no está | — |
| **`WORLD`** | el resto | `0x19`, `0x16C`, `0x13`, `0x1A2`, `0x1BD`, `0x1BE`, `0x195`, `0x19C`, `0x19F` [E] |

- Los NPC enlazados salen del propio 2661 (hasta 8 claves, guardadas con su recurso en la tabla de recursos de
  `guest_seq_route`, hilo principal).
- Tipo: `[[actor+0x88]+1] == 3` (el mismo campo que `GuestSchedBind` escribe como «actor type 3») [V].
- **Nombre [E/H]:** la instancia de X se busca de solo lectura en su mapa (`[X+0x10C]` × `[X+0x120]`, nodo `+8` = id,
  `+0x10` = instancia, `+0x15` = muerta; `0x1F6C190` [V]). El nombre es `[[obj+0x48]+0x1B8]` del objeto cuyo `+0x30`
  es el id (la disposición de «etapa» de `guest_x_veto`, que en la sesión 12 dio `'cd_seq_funcnpc_butcher' (key 0x197)`
  [V]). Se prueba la instancia y luego `[inst+0x58]`; la línea dice cuál coincidió (`via inst` / `via [inst+0x58]` /
  `none`). Si ninguno coincide → `name not read` → nunca se enruta.

### 3.3 La comprobación interna en probe

En `probe` la llamada va a M, así que `0xA9E55B` nunca ve X. Para saber qué diría, el gancho de enrutado lee **de solo
lectura** la instancia de X y aplica la misma función `gsr::scene_2524` (instancia, escena cargada `[[inst+0x58]+0x38]`,
tabla de ramas y entrada de la rama). Se cuenta solo para las escenas MUNDO. Es una lectura sin cerrojo en el hilo
principal (el mismo que escribe ese mapa en los manejadores de Ack): una lectura rota da un dato malo en el log, nunca
un enrutado (en `1` la decisión final sigue siendo la del juego bajo su cerrojo).

### 3.4 Líneas

- Por cada 2524 juzgado: `GuestSeqRoute [2524 StageOnBranchEventAck 0xC08178] … | <veredicto> | resource 0x… |
  scene class: WORLD | binds 3 (NPC 2, in this client 2, type 3 2) | name '…' (via inst) | inner check on X (read-only
  peek, as 0xA9E55B would judge it): OK`.
  - Veredictos nuevos/relevantes: `WOULD route to X (probe)`, `ROUTED …`, `2524 left native: not a world scene`,
    `X is not ticked by guest_seq_play`, `… not at 3 or X loading`.
- Cada 60 s: `GuestSeqRoute [2524 world summary] guest_seq_route_2524=probe | classes: … | WORLD: ROUTED n, WOULD route
  n | WORLD pre_2524: ok n, not playing n | WORLD inner check on X (read-only peek): ok n, no instance n, NOT LOADED n,
  bad branch n | the inner hook (routed calls): … | name via inst n, via [inst+0x58] n, none n | allow list: … | WORLD
  resources seen (for guest_seq_route_2524_allow): XXXXXXXX,YYYYYYYY | scenes (n): 0x19 res 0x… 'name' WORLD [veredicto]
  xN; …`.

## 4. Cómo leer los logs de la sesión 13

Orden recomendado (invitado, `cdmp.log` de Shadow):

1. **Instalación.** `GuestBodyAnim ON … 0xA79770 hooked | 0x2432F60 hooked` y `GuestSeqRoute ON … 2524:
   guest_seq_route_2524=PROBE …` con `2524 StageOnBranchEventAck 0xC08178 (own hook)` en `points:`. Si alguno dice
   `NOT hooked` / `REFUSED`, leer la línea ERR anterior (bytes distintos = otra build u otro gancho).
2. **¿Corre BodyAnim en el invitado?** En `GuestBodyAnim summary`:
   - `BodyAnim … calls N` > 0: el juego llama a BodyAnim en este cliente (para algo: jugador, NPC propios).
   - `watched` = 0 y la cohorte con `idle wait … no graph` / `seq_bodyanim WITHOUT graph` para los de la familia (d):
     **confirma** que su gráfico nunca se construye con los 2524 en M (lo esperado).
   - `watched` > 0: algún camino del invitado ya construye su gráfico. Mirar las líneas `[BodyAnim 0xA79770]` de esos
     NPC (clip, tiempos) y `[G5 body]` (`GRAPH BUILT`, acción actual). Si se ven bien en vídeo, el 2524 a X podría no
     hacer falta para ellos.
3. **¿Qué 2524 serían de mundo?** En `[2524 world summary]`:
   - `classes:` cuántos `WORLD`, `function scene`, `no NPC bound`… Los de la familia (d) (`0x19`, `0x16C`, `0x1BD`,
     `0x1BE`…) deben salir `WORLD`; las tiendas `function scene`; `0x164`–`0x167` `no NPC bound`.
   - `name via inst / [inst+0x58] / none`: si `none` es casi todo, el camino del nombre está mal [H] y el modo `1` no
     enrutaría nada (falla cerrado). Avisar antes de seguir.
   - `scenes (n): …`: revisar a mano los nombres de los `WORLD`. Cualquier escena de misión, cinemática o con cámara
     que salga `WORLD` debe ir a `guest_seq_route_deny` (o quedarse fuera de `_allow`).
4. **¿Podría correr el 2524 en X?** `WORLD pre_2524 ok` y `WORLD inner check … ok` altos. `NOT LOADED` o
   `no instance` altos = la escena de X no está lista cuando llega el 2524 (en `1` el gancho interno lo dejaría en su
   camino nativo de «no encontrado», sin efecto).
5. **Cruce con el vídeo.** El NPC agachado (`0xB0100077/78`, escena `0x19`) y los guardias: su línea `[G5 body]` y el
   2524 de su escena en el mismo minuto.

## 5. Cuándo pasar a `guest_seq_route_2524=1`

Todas estas condiciones en la sesión 13 (probe):

1. Sin cierres ni tirones atribuibles (`FreezeWatch` sin `GuestBodyAnim`/`0xC08178` en pila).
2. La familia (d) confirmada sin gráfico (§4.2) y sus escenas clasificadas `WORLD` (§4.3).
3. `name via none` ≈ 0 para esas escenas; ninguna `cd_seq_funcnpc_*` como `WORLD`; ningún nombre de cinemática,
   misión con cámara o interfaz entre los `WORLD`.
4. `WORLD inner check … ok` en la mayoría de sus 2524 y `WORLD pre_2524 ok` > 0.

Entonces, en el ini de Shadow:
- `guest_seq_route_2524=1`;
- `guest_seq_route_2524_allow=` + la lista `WORLD resources seen` del resumen, quitando lo dudoso (empezar por 2–3
  escenas: `0x19` y los guardias);
- mantener `guest_seq_bodyanim_diag=1`: ahora debe verse `[BodyAnim 0xA79770]` de esos NPC justo tras su 2524
  `ROUTED`, `[configure 0x2432F60, layer 0/1]` con un clip, y `[G5 body]` pasando a `GRAPH BUILT (CC+0x70)` con acción
  `seq_bodyanim` en curso.

`guest_seq_play` sigue haciendo falta (`=1`): solo un X tickado (en 3) recibe 2524. `guest_seq_play_live` puede seguir a 0.
**Revisión de `5f30a52`:** ahora se exige en el código. `guest_seq_play_knows_x` también es cierto con
`guest_seq_play=probe`, en el que nadie tickea X. Por eso `gsr::mode_2524` recibe `guest_seq_play_ticks()`
(`guest_seq_play=1` y no apagado tras un fallo). Sin eso, `guest_seq_route_2524=1` queda en *probe*, y la línea de
instalación lo dice («guest_seq_play is not 1: nothing ticks X»).

## 6. Criterios de aborto

Con `probe` (lo que se entrega) no se escribe nada en el juego. Abortar si:
- cierre con `0xA79770`, `0x2432F60`, `0xC08178` o `0xA9E55B` en la pila o en el `GuestCrash` →
  `guest_seq_bodyanim_diag=0`; si sigue, `guest_seq_route_2524=0` (el punto `0xC08178` deja de instalarse);
- tirones nuevos en escenas con muchos NPC (BodyAnim se cuenta en cada llamada: coste mínimo, pero medir).

Con `1` (siguiente paso), volver a `probe` ante cualquiera de:
- cámara, control, entrada o interfaz del invitado raros tras un 2524 `ROUTED` (efecto personal de la escena del
  anfitrión);
- carga de campo o vuelta al título tras un 2524 `ROUTED` (añadir su recurso a `guest_seq_route_deny`);
- NPC que tiemblan, saltan de pose o se teletransportan (BodyAnim también escribe posición/rotación, `0xA7AA80`);
- más NPC en pose A que con `probe`, o peticiones nuevas del invitado sobre escenas del anfitrión (2888/2105 en la
  captura; S11 fase 2 tuvo 365 × 2888);
- `the inner hook (routed calls): NOT LOADED` o `bad branch` creciendo sin parar;
- cualquier cierre.

## 7. Riesgos y límites

- **[H] Nombre de la escena:** la disposición `+0x30` clave / `+0x48` info / `+0x1B8` nombre es la del objeto «etapa»
  que ven los manejadores de `guest_x_veto`; que sea la instancia del mapa (o `[inst+0x58]`) no está demostrado
  estáticamente. Se comprueba en ejecución por la clave; si falla, nada se enruta (falla cerrado) y el resumen lo dice.
- **[E] Tipo 3:** `[[actor+0x88]+1]` coincide con el tipo del registro 2005 en NPC (3) y mascotas (6); los NPC «con nombre
  de secuenciador» son tipo 4 y no cuentan solos (una escena con solo tipo 4 sale `no NPC of type 3`).
- **[E] Peek sin cerrojo:** recorre el vector denso de nodos (como el `find_instance` de b728ec7), no la cubeta que
  usa `0x1F6C190`. Solo para clasificar y medir.
- **Lecturas G5 sin cerrojo** (hilo de diagnóstico): como el resto de G5; un dato roto solo da una línea mala.
- **`0xA79770` compartido con P8Probe:** con `s1_diag=1` el punto es de P8Probe y aquí solo se instala `0x2432F60`.
- **El modo `1` no está probado.** Enrutar el 2524 a X ejecuta `0xA9E480(X)`: flags de etapa, eventos por `0x2049170`
  y, por la cadena `0xADED90`, BodyAnim con escritura de pose/transformación del NPC. Puede competir con el movimiento
  relevado del anfitrión y disparar efectos personales que `guest_x_veto` no cubra. Por eso: lista blanca, `probe`
  primero y pocas escenas al principio.
- **Fase 2:** `guest_seq_route_2524=probe` también desactiva el enrutado antiguo de todos los 2524 en el ini de fase 2.

## 8. Código y pruebas

| Fichero | Qué |
|---|---|
| `src/mod/session2/guest_bodyanim_diag.{h,cpp}` | Medida: ganchos `0xA79770`/`0x2432F60`, G5 de `CC`, resumen |
| `src/mod/session2/guest_bodyanim_diag_logic.h` | Puro: llamante por dirección de retorno, estados de la lectura, cohorte |
| `src/mod/session2/guest_seq_route_logic.h` | Puro: `Mode2524`, `mode_2524`, `classify_2524`, `is_function_scene`, `scene_name`, veredicto `NotWorld2524`, enlaces del 2661 |
| `src/mod/session2/guest_seq_route.{h,cpp}` | Clasificación en el gancho 2524, peek, líneas y resumen, lista G5 publicada sin bloqueo |
| `src/mod/config.{h,cpp}`, `guest.cpp`, `guest_diag_worker.cpp` | Claves, arranque, tick |
| `tests/optd_seq_bodyanim_tests.cpp` | Modos, clasificación clase a clase (escenas de la sesión 12), nombres, orden de veredictos, tabla de enlaces, estados de BodyAnim |

No se probó en el juego. `.\scripts\build.ps1`: compilación y 41/41 pruebas.
