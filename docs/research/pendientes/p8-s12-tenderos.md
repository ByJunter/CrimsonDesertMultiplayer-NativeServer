# P8 sesión 12: el invitado sigue sin poder hablar con los tenderos

Build del juego 1.0.0.2976 (base `0x140000000`). Fecha: 2026-10-10. Solo lectura: registros de la sesión 12,
capturas `.bin` (lector propio por registros `CDPK`), registro del anfitrión de la sesión 11 como referencia nativa y
capstone sobre `CrimsonDesert.exe` (incluido `.xtls`). Sin juego, sin cambios de código.

Convenciones: **[V]** comprobado (dato contado o código leído), **[E]** interpretación fundada, **[H]** sin demostrar.
X = gestor de escenas del Kliff del anfitrión (`A0100001`) replicado en el invitado; M = el del Kliff propio del
invitado (`A0100336`), el que devuelve `ClientField::vf48` (`0x9A5BE0`).

Datos:
- Invitado: `backups\p8-sesion12-shadow\cdmp_20261010-215406.log`, `cdmp_packets_20261010-215423.bin`.
  Reloj: `capture t = 0` ≈ 21:54:53,60 del registro (de `qpc … (~21:59:35.554) capture t=281.957946`).
- Anfitrión: `backups\p8-sesion12-host\cdmp_20261010-225143.log`, `cdmp_packets_20261010-225154.bin`.
- Referencia nativa: `backups\p8-sesion11-host\cdmp_packets_20261010-201238.bin` (H1 de la sesión 11).

## 1. Resumen

- **Causa [V en el código, E en la sesión]:** las entradas que SetInteraction añade al NPC desde X guardan la
  **clave de la etapa de X** (`entrada+0x20`). Al construir el aviso de interacción, el cliente valida cada entrada
  dinámica buscando esa clave en el gestor **del campo del jugador local** (M, por `ClientField::vf48`), con
  `0x1F6C190`. En el invitado la etapa del tendero solo existe en X → la búsqueda falla → la entrada se descarta → no
  hay aviso → no hay 2671 ni 2720.
- **Agravante [V]:** cuando un NPC tiene entradas dinámicas (`comp+0x90 > 0`), el recolector `0x2123710` devuelve
  **solo** esas y no las estáticas del NPC. Con S11 (veto) el tendero se quedaba con sus estáticas, que no incluyen la
  charla de tienda [E]; con S12 (permitido) se queda con las dinámicas de X, que fallan todas. En ambos casos, nada.
- **El anfitrión no habló con ningún tendero en la sesión 12** [V: 0 paquetes 2720 en su captura; sus 9 2671 son
  NPC→NPC]. La secuencia nativa de referencia es la de la sesión 11 (§2.3).
- **Arreglo mínimo para el aviso:** redirigir esa única búsqueda de M a X cuando la clave es de una etapa de X
  (mismo truco que `guest_seq_route` hace con el valor de `vf48`). **No basta para abrir la tienda:** la charla
  avanza una escena del **anfitrión** en el servidor (§4.3). El arreglo de fondo es que el servidor del anfitrión cree
  las escenas `cd_seq_funcnpc_*` también para el usuario 2.

## 2. Pregunta 1: tenderos cercanos y paquetes

### 2.1 Tenderos y escenas de la sesión 12 [V]

Escenas de tienda que llegaron al invitado (todas copiadas del anfitrión, dueño `A0100001`, y enrutadas a X; el
invitado solo recibió **una** escena propia, la `0x19D`, con un único enlace a `A0100336`). Enlaces sacados de
`GuestStage [2661 in 0xC05BB0]`; posiciones, de la primera terna de coordenadas tras la clave en los S>C (2991/2723/2005)
de la captura del invitado (aproximadas, [E]):

| Escena (clave X) | NPC | Posición aprox. (x, z) | SetInteraction X | Distancia mínima del invitado |
|---|---|---|---|---|
| `cd_seq_funcnpc_butcher` 0x197 | `B0100156` | (-10677,6, -3905,4) | 3 add t=54,35; sin remove | **≈ 2,7 m** a las 21:56:37 (t≈103) |
| `cd_seq_funcnpc_seatdeale` 0x198 | `B0100157`, `B0100158` | (-10814,3, -3990,2) | 3 add t=54,36; 3 remove t=281,96 | lejos |
| `cd_seq_funcnpc_grocery` 0x199 | `B0100159` | (-10653,6, -3879,6) | 3 add t=54,37 | ≈ 7,5 m (21:59:57–22:00:07) |
| `cd_seq_funcnpc_smithy` 0x19A | `B010015A` | (-10636,5, -3848,3) | 3 add t=54,38 | **≈ 3,6 m** (22:01:22) |
| `cd_seq_funcnpc_general` 0x19B | `B010015B` | (-10641,7, -3870,8) | 3 add t=54,39 | ≈ 22 m |
| `cd_seq_funcnpc_bank` 0x1A4 | `B0100C14` | (-10700,8, -3798,1) | 3 add t=283,44 | lejos |
| `cd_seq_funcnpc_pub` 0x1A5 | `B0100C15` | (-10663,2, -3838,0) | 3 add t=283,46 | **≈ 2,3 m** (22:00:52) |
| `cd_seq_funcnpc_contribut` 0x1B2 | `B0100C2E` | (-10596,2, -3794,2) | 3 add t=283,69 | lejos |
| `cd_seq_funcnpc_clothes` 0x1BF | `B01012EA`, `B01012EB` | (-10640,9, -3835,4) | 3 add t=344,63 | ≈ 22 m |
| `cd_seq_funcnpc_craftsmit` 0x1C0 | `B01014B6` | (-10634,9, -3841,4) | 3 add t=359,52 | ≈ 29 m |

- Los registros no traen nombres de NPC; las claves `B01…` cambian entre sesiones (Delkin era `B0100156` en S11 con la
  escena «grocery» 198; en S12 `B0100156` es el de la carnicería 0x197). Identifico por escena, no por nombre.
- El invitado estuvo a 2–4 m del carnicero, del tabernero y del herrero **después** de sus altas en X.

### 2.2 Lo que mandó y recibió el invitado [V, `cdmp_packets_20261010-215423.bin`]

| Opcode | Invitado C>S / S>C | Anfitrión S12 C>S / S>C |
|---|---|---|
| 2857 SetQuestDialogReq / 2948 Ack | 712 / 712 | 763 / 763 |
| 2671 InteractionPreemptionStepReq / 2663 Ok | 3 / 3 | 9 / 9 (todos NPC→NPC) |
| 2720 InteractionDoStepReq / 2916 Ok | 3 / 3 | 0 / 0 |
| 3000 PreemptionFail | 0 | 0 |
| 2146 StoreDataListAck | 675 | 675 |

- Los 2857 son automáticos: ~2 por NPC al cargarlo (los tenderos `B0100156…15B` reciben 2 cada uno en ambos lados).
  **No** son intentos de hablar.
- Las tres interacciones del invitado, todas con éxito (2671 → 2663 → 2720 → 2916):
  - t=105,49 `B01005F0` interacción `0x000F426D`: un actor nacido a t=93,5 a **1 m del carnicero** (-10676,5,
    -3904,7). El invitado, delante del carnicero, interactuó con ese objeto en vez de con el NPC.
  - t=162,48 `B01001C8` `0x00002B2F` (-10700,6, -3908,2): estática.
  - t=370,32 `B0100C0D` `0x000F430E` (-10662,8, -3831,1): el NPC del pulso (`cd_minigame_armwrest_bas` 0x1A2).
- **Ningún 2671 a un tendero.** El 2671 sale al pulsar sobre un aviso; sin aviso no hay 2671. El síntoma está antes
  del envío: en la construcción del aviso (§4).

### 2.3 Secuencia nativa de la tienda (anfitrión, sesión 11) [V, H1]

```
R323655 t414.985 C>S 2671  A0100001 → B0100153  interacción 000F4321 … 00000001
R323676 t414.992 S>C 2663  A0100001 A0100001 B0100153 000F4321 …
R323683 t415.010 C>S 2720  A0100001 → B0100153  000F4321 00000000
R323737 t415.024 S>C 2916  A0100001 A0100001 B0100153 000F4321
R323768/9        S>C 2524 + 2981 escena 196 rama 6   (la charla y la tienda la mueve el servidor por la escena)
```
Igual con Delkin (`B0100156`, R412453–412508). La interacción de tienda es `0x000F4321` (clave ≥ `0xF4240`: las de
las entradas dinámicas). En S12 el invitado nunca mandó `F4321`.

## 3. Pregunta 2: altas de SetInteraction en X [V]

- `GuestXVeto [dialogue] … ALLOWED … SetInteraction add … stage 'cd_seq_funcnpc_*'`: 3 altas por escena de tienda
  (tabla §2.1). El registro nombra la etapa y su clave, **no** los actores resueltos; que caigan en el tendero es [E]
  (S11: la escena enlaza solo `0x20000002` = Kliff del anfitrión y el NPC; el selector especial daría el Kliff).
- Bajas: solo `seatdealer` 0x198 (3, t=281,96, durante el relevo de escenas de t≈282) y `node_her_hernandc` 0x147
  (1). Total del resumen: `SetInteraction X 42 (add 38 / remove 4) / M 0 / 42 ALLOWED 42`. Las de carnicero, tendero,
  herrero, general, banco, taberna, etc. seguían dadas de alta cuando el invitado se acercó.
- **M 0:** ninguna escena propia del invitado registra interacciones; no tiene escenas `funcnpc` propias.
- `BlockInteraction`: 0 llamadas (no aparece en los resúmenes) → los tenderos no estaban «ocupados».
- `SetQuestDialogAICategory`: `X 12 / would 12` con «not vetoable: stays a probe even with 1» → **corrió nativa** las
  12 veces; no estaba vetada. No está en la ruta del aviso (§4) [E].

## 4. Pregunta 3: qué exige el cliente para el aviso / 2671 / 2720

### 4.1 Ruta [V, capstone]

```
UI de interacción 0xAC25D0 / 0xAC30E0 / 0xAC7040
  → 0x9BA050(compJugador, actorObjetivo, &lista)        ; «interacciones disponibles»
      ↳ descartes generales: estado [[J+0x68]+0x40]+0xC8 == 7, J.vf188(2), 0x9B9070 (ocupado),
        montura/vehículo 0x1F70190, 0x211FA90(objetivo), mismo dueño vf368 + global 0x6CF7E94
      ↳ 0x2123710([[obj+0x68]+0x98], dl=1, &tmp)         ; recolector
           si dl && comp+0x90 > 0: copia SOLO las entradas dinámicas comp+0x88 y sale (0x212379C)
           si no: estáticas (tipo de actor 7, datos del horario [[obj+0x68]+0x58]+0x90, info del actor +0x338)
      ↳ por entrada: 0x9B94B0 (= vtable ClientInteractionActorComponent +0x170)
           → 0x211FCB0 «CanInteract» (cadena de Y):
               0x2124060: busca la entrada por clave de info en el NPC (vuelve a llamar al recolector,
                          dl = J.vf178 = «es el jugador local»: [J+0xA0] == usuario local && [J+0x94])
               0x21206C0, J.vf1E8→vf18, J.vf188(2), 0x2120CC0 (distancia/ángulo, 0x5D73988) …
               0x2126920(entrada, J)   ← condición de la entrada dinámica (§4.2)
           → filtros propios de 0x9B94B0 (entrada+0x20 con jugador en etapa 0x17EE020, tipo de NPC 1/3–6/8/9:
             0x1800EC0 y [[[[obj+0x68]+0x40]+0x58][0]…+0x88]+0x10]+0x109 & 4)
  pulsar → 0xABF770 → 0x9B5EB0 (ranura +0x180, manda 2671) … → 0x9BC7640 (ranura +0x188, manda 2720)
```

### 4.2 La condición de la entrada dinámica `0x2126920` [V]

```
0x212694D  test [entrada+0x20], 0x1FFFFFFF ; sin clave de etapa → devuelve 2 (válida)
0x212695D  usuario = [J+0xA0] ; nulo → 0
0x2126974  0x206BA40(usuario)  → su personaje [usuario+0xD0]        (invitado: A0100336)
0x2126997  0x17FA6C0(personaje) → [[p+0x68]+0x1A0].vf140 = su ClientField
0x2126A1C  field.vf48  → RDI = M (0x9A5BE0)   ; 0x2126A1F mov rdi,rax ; mov [rbp+0x8E8],rax
0x2126A31  field.vf40  → [rsp+0x30]
0x2126A6A  0x1F6C190(M, &out, [entrada+0x20])  ; buscar la etapa por id de secuenciador EN M
0x2126AB7  no encontrada → 0  (descartada; motivo 0x6CF6D48 en 0x211FCB0)
           encontrada: [etapa+0x160]==0, !0x1F6FC40(M, etapa), etapa[+0x70] → info: el personaje
           ([[J+0x68]+0x20]+0x30) admitido (0x23505A0), 0x1AC10F0(vf40, idx); después la condición
           [entrada+8] (0x2239930 + vf8/vf10) → 1 ó 2
```

- `entrada+0x20` lo escribe `E402CC0` con `[etapa+0x30]`, el id de secuenciador de la etapa que hizo la alta
  (el `key 0x197` de los registros) [V `e402cc0.c`].
- `0x1F6C190` es la misma búsqueda por id que usa `0xA9E480` y que `guest_seq_route` ya conoce [V].
- En el invitado, `vf48` da M (`GuestSeqRoute … vf48 gave M 0x40FC16EAE00 (character 0xA0100336)`) y la etapa 0x197
  vive en X → **toda entrada dada de alta desde X falla aquí** [V código; E aplicado: no hay registro de la llamada].
- En el anfitrión la misma escena vive en su M → pasa. Por eso él sí ve el aviso.

### 4.3 Repaso de los candidatos de la pregunta

| Candidato | Veredicto |
|---|---|
| Entradas `+0x88` de SetInteraction | Llegan [V], pero con la clave de X: fallan en `0x2126920` [V/E]. Además tapan las estáticas [V] |
| BlockInteraction `[[actor+0x68]+0xB0]+0x180` | 0 llamadas en S12 [V]; no es |
| `+0xCF` del NPC, modo remoto 2 | No aparece en la ruta leída [E]; hay un filtro de tipo de NPC (`0x1800EC0`, flag `+0x109 & 4`) sin identificar [H] |
| Distancia | El invitado estuvo a 2–4 m de tres tenderos [V aprox.]; no es [E] |
| SetQuestDialogAICategory | No vetada (probe nativo) [V]; no está en la ruta [E] |
| Dueño / escena propia | **Es esto:** la entrada exige la etapa en el gestor del jugador local |

- El pulso (`B0100C0D`, `F430E`) funcionó; su escena 0x1A2 solo hizo **1** alta en X (las de tienda, 3). Que `F430E`
  fuera estática (y la alta de 0x1A2 cayera en otro actor) es lo coherente con §4.2, pero no está demostrado [H].
- `B01005F0` (`F426D`) junto al carnicero: interacción de otro actor, no del tendero [V].

## 5. Pregunta 4: arreglo y qué registrar

### 5.1 Arreglo mínimo para el aviso (cliente invitado) [E]

- **Punto:** `safetyhook::create_mid` en `0x2126A1F` (`48 8B F8` `mov rdi,rax`, seguido de `48 89 85 E8 08 00 00`
  `mov [rbp+0x8E8],rax`), dentro de `0x2126920` (rango `.rsrc`, función con `.pdata`). En ese punto `RAX` = M y
  `R13` = la entrada (`[r13+0x20]` = id de etapa). Hilo: el de juego que construye el aviso.
- **Qué hace:** si `[r13+0x20] & 0x1FFFFFFF` es el id de un secuenciador que el mod enrutó a X (el registro de
  `guest_seq_route`/`GuestStage` ya los tiene: `corr=2661:A0100001:0x197`) y no es de M, `RAX := X`
  (`[[[A0100001+0x68]+0xA8]+0x18]`). Las dos escrituras siguientes copian X a `rdi` y `[rbp+0x8E8]`, así que la
  búsqueda (`0x2126A6A`), la liberación del cerrojo y `0x1F6FC40` usan X de forma coherente. Nada más cambia.
- **Clave nueva:** `[session2] guest_x_interaction_stage = 0 | probe | 1`. `probe` solo registra (§5.3).
- **Qué cabe esperar:** aparece el aviso; al pulsar sale `2671 A0100336 → B0100156 000F4321`. Luego decide el servidor.

### 5.2 Por qué no basta y qué hace falta de verdad [E/H]

- La charla de tienda es la etapa del NPC: tras el 2720 nativo el servidor manda 2524 + 2981 de **esa** escena
  (§2.3). La 0x197 es del **anfitrión** en el servidor. Con 5.1, el servidor del anfitrión: o rechaza el 2671/2720 del
  usuario 2 (su `0x2126920` busca la etapa en el gestor del usuario 2 → 3000 `PreemptionFail`) [H], o lo acepta y
  avanza la escena del anfitrión → el 2981 llega al cliente del anfitrión, que abriría **su** diálogo/tienda, y en el
  invitado la rama va a X con InputBlock/UIControl vetados → sin menú [H].
- **Arreglo de fondo:** que el servidor del anfitrión cree las escenas `cd_seq_funcnpc_*` (y las `node_*` que las
  activan) también para el usuario 2, con dueño `A0100336`. Llegarían al invitado como 2661 propias → M → SetInteraction
  en M con ids de M → aviso, 2671/2720 del usuario 2 y su propia escena 2524/2981 → tienda en M, sin vetos. Hoy el
  invitado solo recibe una escena propia (0x19D). Qué dispara esas escenas en el servidor (progreso de misión del
  personaje, entrada al nodo `cd_seq_node_her_bluemont` 0x195…) está sin investigar [H]; es el siguiente trabajo
  estático en el lado anfitrión.
- 5.1 sirve como paso 1 medible (¿qué responde el servidor?) con `probe` primero. Si el servidor rechaza, 5.1 sola
  no aporta y conviene no activarla.
- **No** volver a vetar SetInteraction para «recuperar» las estáticas: en S11 tampoco salía la tienda.
- Riesgo de S12 a vigilar: con entradas de X un NPC pierde **todas** sus interacciones estáticas en el invitado
  (§4.1). Si aparecen NPC que antes se podían usar y ahora no, la causa es esta; `guest_x_ui_veto_allow=` lo
  devuelve a S11.

### 5.3 Qué registrar la próxima vez

1. **Construcción del aviso** (sin cambiar nada): gancho de lectura a la entrada de `0x2126920` (o en `0x2126A6A`):
   NPC objetivo (no llega a `0x2126920`: tomarlo de `rdx` en `0x9B94B0` y pasarlo por TLS), clave de info
   (`0x3888B0(entrada+0x10)`→`[rax]`), id de etapa `[entrada+0x20]`, ¿está en M?, ¿está en X?, resultado (0/1/2).
   Límite por NPC y por segundo.
2. **Resultado por entrada** en `0x9B94B0`: byte de salida y motivo (`[out+4]`, p. ej. `0x6CF6D48`), solo para NPC a
   menos de 5 m.
3. **Lista dinámica de los tenderos:** al ALLOWED de SetInteraction, los actores resueltos (`0x2049B90`) y
   `comp+0x90` antes/después; en el informe de 5 s, `comp+0x90` y las claves `+0x10`/`+0x20` de los NPC cercanos.
4. **Marcas humanas:** que el jugador de Shadow anote la hora en que pulsa «hablar» con cada tendero (o una tecla de
   marca del overlay), para cruzar con 2671.
5. **Anfitrión:** que hable con un tendero con el invitado cerca (referencia nativa de S12 y prueba de la rama 2981
   en X), y registrar en el servidor el 2671/2720 del usuario 2 y su `3000`/`2663` si 5.1 se activa.

## 6. Reproducción

- Extracción de paquetes: lector de registros `CDPK` (cabecera de 24 bytes `<QBBHHHII`, `op,slot,len` `<HbH`), filtro
  de los opcodes 2857/2948/2671/2663/3000/2720/2916/2525/2743/2146; posiciones: primera terna (x∈(-12000,-9000),
  y∈(0,2000), z∈(-5000,-2500)) tras la clave del NPC en los S>C. Scripts en el scratchpad de la sesión, no en el repo.
- Capstone con `C:\Emulador CD\build\research\cdpe.py` / `xref_index.pkl` (`py -I`), funciones por `.pdata`.
- Ghidra no se ejecutó (escribe en `exports\decomp`); se usaron las decompilaciones existentes de `E402CC0`/`E404A10`
  del trabajo `npc-raiz-s11`.
