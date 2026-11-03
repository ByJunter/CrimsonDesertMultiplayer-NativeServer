# Salida del usuario 2 a mitad de una carga (P8, 2026-10-09 16:20)

Fecha: 9 de octubre de 2026. Build 1.0.0.2976, base `0x140000000`, direcciones como RVA. Estudio
estático (Ghidra de solo lectura y Capstone sobre `D:\RE\CrimsonDesert_2976\bin\CrimsonDesert.exe`) y
lectura del log. No se ha ejecutado el juego ni se ha cambiado `src/`. Scripts de análisis en
`<scratch>\salidacarga\` (`scan5e.py`, `scanimm.py`, `scan58.py`,
`callers.py`, `pe.py`).

Marcas: **[E]** leído en código, **[E-x]** leído en `.xtls`, **[V]** visto en el log, **[S]** inferencia.

## Resumen

- **[V] La 2001 sí llegó al servidor.** Se entregó una vez y el hilo del servidor la recogió
  (`171 handed/171 seen 2001 handed 1x`). Aun así, en 70 s **no salió ningún ACK de logout**
  (`logout Acks out 0`; en C4a y C4b salían 2). La sesión seguía unida (`+0x90`), el personaje seguía en
  el campo del anfitrión y en el SAM. Es decir: la 2001 se procesó, pero la fase final del logout nunca
  llegó. No es que se perdiera.
- **[V/S] El invitado se cortó en la ventana A de la carga compartida.** Su servidor ya había mandado
  `MoveFieldAck`, pero el cliente todavía no había enviado `MoveFieldCompleteReq`. No era la ventana B
  (después de `MoveFieldCompleteAck`).
- **[E/E-x] Mecanismo encontrado.** El logout nativo termina por **referencias**: la fase final solo
  arranca cuando se suelta la última. Además, el personaje sale del campo (`LeaveField`) solo al
  soltarse un acceso de campo (`FieldAttacherForActor`). Pero `FieldMover::GetField` niega ese acceso
  mientras `FM+0x58 != 0` («traslado en curso»):
  - `GameLoadingStartReq` pone esa marca;
  - **solo `MoveFieldComplete` la borra**.

  Si el cliente desaparece entre las dos, el logout empieza pero el personaje nunca sale del campo.
  Por eso el usuario no llega a su `vf58`, ni a 2002, ni al `Close` que borra `+0x90`.
- **[S] Arreglo de raíz propuesto.** Antes de la 2001, terminar de forma nativa la fase que quedó
  pendiente, inyectando lo mismo que mandaría el cliente:
  - en la ventana A: `MoveFieldCompleteReq` (`00`) y esperar `MoveFieldCompleteAck`;
  - en la ventana B: `LoadingCompleteReq` (`00`), solo si la prueba `cut9` demuestra que hace falta.

  Después, la 2001 de siempre. No se recomienda escribir `FM+0x58`, llamar a `LeaveField` ni usar otra
  entrada de logout.
- **Prueba en un solo PC:** tres guiones nuevos, `login_e3lc00cut{3,8,9}_112627.troc`, generados con
  `make_troc_script.py --end-after-step N`. `check_troc_scripts.py` los comprueba y `ctest optd_scripts`
  pasa.

## 1. Qué espera el logout nativo (pregunta 1)

### 1.1 El inicio no mira la carga [E/E-x]

| Paso | Dirección | Qué hace |
|---|---|---|
| Manejador 2001 | `.xtls 0x1089A360` | Toma el usuario del contexto (`session+0x90`) y llama a `ServerActor::Logout` (`0x2068340` → `.xtls 0xE0D9AF0`) con motivo 4. Si devuelve error, manda Nak 1013 a la sesión (`0x1089A3C9`…`0x1089A46E`). |
| Inicio del logout | `.xtls 0xE0D9AF0` | Solo rechaza el estado `+0x5E` 0x20/0x40 («ya saliendo», `0xE0D9B22`–`0xE0D9B42`). Si no, guarda motivo y error, llama a vf80 (`+0x5E = 0x20`) y a vf110. |
| vf110 del usuario | `0x2A7D4E0` → `.xtls 0x1051B8D0` | Fase de componentes (`0xE0DB5A0`, vf30), suelta el actor controlado (`0x206AEC0`, `user+0xD8 = 0`) y llama a vf120/vf118 (vf40/vf38 de los componentes). |

No he encontrado ninguna comprobación de `LoadingComplete`, `pj+0xC3`, `TS+0x3DC` ni `FM+0x58` en este
tramo. El usuario 2 tenía el estado 4 (sonda `+5E 0x0004`), así que el inicio debió aceptarse [S].

### 1.2 El final depende de soltar referencias [E]

`vf1` de todos los actores (`0x1436610`, en las vtables `0x5B257A8` de usuario y `0x5B1E2B0` de
personaje) es **Release**:
- cuando `refcount(+0x10) + contador de ámbito (+0x48)` llega a 0 y el estado es 0x20, lo pasa a 0x40
  (`0x14366B3`/`0x1436726`);
- después llama a **vf50**:
  - del usuario: `0x2A7D540` → `.xtls 0x1051E5E0`, que encola el interno 2004;
  - del personaje: `0x28F2D40` → `.xtls 0x103820F0`, que también construye 2004 (`0x10382152`);
- si eso falla, llama directamente a vf58.

El manejador de 2004 (`0x2C48410` → `.xtls 0x1089C4A0`) llama a vf58. El vf58 del usuario
(`0x1051F190`) pone `user+0x1F8 = 0`, envía 2002 (`0x1051F236`/`0x1051F2C8`) y cierra la sesión, y ese
cierre pone `+0x90 = 0` (`0xB8A9031`).

El 2003 lo envía `ServerUserLoginActorComponent` en su vf+0x120 (`0x2C16EC0` → `.xtls 0x107E4820`,
`mov 0x7D3`, 7 bytes).

**Por tanto:** con `+0x90` puesto y ningún ACK, el usuario nunca soltó su última referencia. Algo sigue
vivo y lo sujeta.

### 1.3 Qué lo sujeta: el personaje no puede salir del campo mientras el traslado está abierto [E/E-x]

| Dirección | Hecho |
|---|---|
| `0x2BC9470` `GameLoadingStart_Step` → `0x2BBF630` → `.xtls 0x106DB020` | Pone `FM+0x58 = 1` (`0x106DB1C1`). Si `FM+0x5A == 0`, la pone a 2 y encola `MoveFieldOnceTimer` (`0x106DB21B`). FM = `TransformSync+0x4F0`. |
| `0x2BBFA80` `MoveFieldOnceTimer` | `TS+0x3DC = 1` (`0x2BBFB2A`), sale del campo viejo, entra en el nuevo (aquí actúan H1/H2) y envía `MoveFieldAck`. **No borra `FM+0x58`.** |
| `0x2BC6BB0` `TransformSync::MoveFieldComplete` +0xFC → `0x2BC18A0` → `.xtls 0x106E7630` | **El único sitio donde se borra:** `FM+0x58 = 0` (`0x106E7718`), limpia el destino (`0x2BBF580`) y pone `TS+0x3DC = 0` (`0x106E7735`). Solo hay otro `= 0`, `0x106DE2AF`, que es la rama de fallo al encolar. |
| `0x2BBF340` `FieldMover::GetField` | Solo da acceso (`FM+0x5A++`) si `FM+0x58 == 0`, `FM+0x10 != 0` y vf1D8. Si no, devuelve un `FieldAttacherForActor` vacío. |
| `0x2BC42C0` (vf+0x148 de TS) → `0x2BBF140` | Al soltar el acceso: `FM+0x5A--`. Al llegar a 0, si el actor ya no está «logined» (vf108 `0x17EA570`, bit 0x10 de `+0x5E`), llama a `LeaveField(FM, res, 1, motivo)` `0x2BBEAB0` → `RemoveActor`. |
| `0x28F26C0` vf110 del personaje | Pide el acceso al empezar (`0x28FB8B0`, que en realidad es `GetField`) y lo suelta al final (`+0x148`). **Esa suelta es la que hace salir al personaje del campo en un logout.** |

Consecuencia en la ventana A:
1. `FM+0x58 = 2`, así que `GetField` falla en todos los llamadores.
2. No hay suelta del acceso, y por tanto tampoco `LeaveField`.
3. El personaje se queda en `field+0x2A0`, en los sectores y en el SAM (lo que dice la barrera).
4. Sus referencias no llegan a 0, y por eso ni él ni su usuario llegan a vf50/vf58.

El último eslabón (que el usuario espera a su personaje) es [S]. Es coherente con el orden normal
(2003 y luego 2002).

No he encontrado otra ruta de `LeaveField` en el logout que no pase por `GetField`. El callback
`0x2BBE6D0` sigue sin explicar.

**Ventana B** (después de `MoveFieldCompleteAck` y antes de `LoadingCompleteReq`): `FM+0x58` ya es 0, así
que la salida del campo debería funcionar [S]. `LoadingComplete` (`0x2BCAF00`/`0x219D7F0`) solo toca:
- `pj+0xC3 = 1`;
- `[[user+0x68]+0x20]+0x398 &= ~4`;
- la hora;
- las marcas globales `+0x61/+0x64/+0x88` del gestor de guardado.

Ninguna de ellas aparece en el camino del logout. Esto es una predicción que se puede refutar: `cut9`.

## 2. ¿Llegó la 2001? (pregunta 2) [V]

Log `C:\Emulador CD\backups\logs\cdmp_20261009-1614_p8d.log`:

- **L755, 16:19:49.397:** termina la carga del aparcamiento (`loading completes … flag 1 -> 0`, `loads 1`).
- **L762–765, 16:19:53.67:** viaje compartido. H1 da el campo del anfitrión y H2 deja `users 2 user actors 3`.
- **L759 → L769:** `injected 163 → 170` (solo 7 Req más). Nunca hubo un segundo «loading completes».
  - Un cliente real, tras `MoveFieldCompleteReq`, manda unas 65 `SceneLevelLoadComplete…`, `RegionInOut` y
    `OnDemandEquip` (pasos 10–12 del guion JSON). Por eso el `MoveFieldCompleteReq` compartido casi
    seguro **no llegó a salir**: ventana A [S fuerte].
  - Las capturas muestran 11–18 s de carga entre `MoveFieldAck` y `MoveFieldCompleteReq`
    (`opcion-d-e3-riesgos.md` §2). El invitado cayó unos 6 s después.
- **L790–791, 16:20:04.751:** `peer timed out after 5000 ms` y `steam:… left`.
- **L811–814, 16:20:19.76:** el túnel se para y empieza C4 con `trigger=link_lost` (102 claves).
  `LogoutSessionToServerReq 2001 handed to the server`.
- **L815–818:** a los 0,13 s, `a FAIL +0x90`, `b sam FAIL 101 of 102`, `c field FAIL … departed user in its
  list: YES … userActors 0x3` y `d queues pass (… 171 handed/171 seen 2001 handed 1x)`.
- **L831–832, 16:20:29.994:** `QUARANTINED`.
- **L822 … L920, 16:20:22 → 16:21:32:** `2001 handed 1, logout Acks out 0, Req refused after it 0`. `out:`
  sigue subiendo de 1632 a 3254: el servidor sigue mandando cosas al usuario 2, que sigue de
  observador en el campo.

**Diagnóstico:** la 2001 se procesó, pero no se llegó al cierre de la sesión. «Seen» significa que el
hilo del servidor recogió el PB en `on_server_recv`. El manejador se resuelve por `session+0x90`, que
seguía apuntando al usuario.

No se puede descartar del todo un Nak 1013 (el inicio rechazado), porque `take_frame` no cuenta los 1013
de la sesión 2. Haría falta que el estado ya fuera 0x20/0x40, y no hay indicios de ello.

**Sugerencia de lectura para la barrera** (sin ganchos; en `src/`, para quien lo toque): registrar
- `+0x5E` del usuario y del personaje (se espera 0x20 sin pasar a 0x40);
- `FM+0x58`/`FM+0x5A` (se espera 2/0);
- `TS+0x3DC` (se espera 1);
- los Nak 1013 enviados a la sesión 2.

## 3. Reproducción en un PC (pregunta 3)

Guiones nuevos en `docs/research/data/`, generados con la opción nueva `--end-after-step N` de
`make_troc_script.py` y registrados en `check_troc_scripts.py`. Cada uno termina con `end`. Después
`cdmp_peer` se queda callado 10 s (`--linger`), se cierra, y a los 15 s salta C4 por `link_lost`.

| Guion | Corte | Ventana | Predicción |
|---|---|---|---|
| `login_e3lc00cut8_112627.troc` | justo después del `MoveFieldAck` compartido (paso 8 `GameLoadingStartReq`), sin `MoveFieldCompleteReq` | **A, como P8** | **cuarentena** (reproduce) |
| `login_e3lc00cut9_112627.troc` | justo después del `MoveFieldCompleteAck` compartido (paso 9), sin `LoadingCompleteReq` | B | barrera limpia [S] |
| `login_e3lc00cut3_112627.troc` | justo después del `MoveFieldAck` tipo 3 del aparcamiento (paso 3), sin `MoveFieldCompleteReq` | A, aparcamiento | cuarentena [S] (la petición del aparcamiento sale por otra ruta, `0x2941020`) |

**Procedimiento:**
- Ini [`docs/research/ini/cdmp.c4r.ini`](../ini/cdmp.c4r.ini) como `bin64\cdmp.ini`, ranura 108, anfitrión quieto
  y sin viajar (como C4r).
- Tras una cuarentena ya no se admiten más entradas en esa partida, así que el orden es:
  1. Arranque 1: entra en el mundo y espera `Session2 C4: reconnect on`. Lanza
     ```
     .\build\bin\RelWithDebInfo\cdmp_peer.exe --mode replay --script docs\research\data\login_e3lc00cut9_112627.troc --var SAVE_SLOT=108 --record D:\cdmp-grabaciones\cut9.bin
     ```
     Espera a que `cdmp_peer` acabe solo y unos 30 s más.
     - Si sale `reconnect epoch=2 ready`, en el mismo arranque lanza el de `cut8` (`cut8.bin`).
     - Si `cut9` ya da cuarentena, la ventana B también bloquea (ver §4). Cierra el juego.
  2. Arranque 2 (si el anterior acabó en cuarentena) o arranque 3: `cut3` (`cut3.bin`).
  3. Control: la ronda 3 de C4r (`login_e3lc00`, carga completa más corte), que ya pasa.
- **Opcional, para separar «en carga» de «enlace perdido»:** la despedida en plena carga con el enlace vivo
  (no se guarda en el repositorio):
  ```
  python -I -B tools\scripts\make_troc_script.py docs\research\data\login_script_112627.json --stage E3 --loading-complete 00 --end-after-step 8 --goodbye-after-ms 5000 --out $env:TEMP\cut8bye.troc
  ```
  Si 2003/2002 no llegan en 30 s, el bloqueo es nativo y no del camino `link_lost` del mod.

**Qué mirar en `cdmp_*.log`:**
- `cdmp_peer`: `Acks received` incluye 2174 (y 2144 en `cut9`) y `script done`.
- Log:
  - `logout begin epoch=1 … trigger=link_lost`;
  - `2001 handed to the server`;
  - `check d queues pass (… N handed/N seen 2001 handed 1x)`;
  - en la línea periódica, `logout Acks out 2` (pasa) o `0` (falla);
  - si falla: `check a session FAIL … (+0x90)`, `check c field FAIL … departed user in its list: YES` y
    `QUARANTINED` a los 10 s, con `out:` todavía subiendo.
- Tras cada prueba: la ranura 108 intacta y el anfitrión camina y lucha.
- **Parada:** cualquier cierre del campo del anfitrión, o un `users` o `generation` distinto de la base.

## 4. Arreglo de raíz propuesto (pregunta 4, no implementado)

**Terminar el traslado como lo haría el cliente, y después la 2001.** El cliente nativo no ofrece
logout en una pantalla de carga, así que el servidor nunca tuvo que cerrar un traslado a medias. La
única forma nativa de cerrar `FM+0x58`/`TS+0x3DC` es `MoveFieldComplete`.

1. **Fase por época, solo con lo que ya pasa por el túnel:**
   - C→S en `server_inject`: 3093 (`LoginCharacterToFieldServerReq`), 2437, 2549, 2445;
   - S→C en `take_frame`: 2174, 2144, 2762.

   Las ventanas:
   - A = se vio 2174 y no hubo 2549 después;
   - B = se vio 2144 y no hubo 2445 después;
   - estable = se vio 2762.
2. **Al pedir el logout** (por `link_lost` o despedida) en la ventana A:
   - inyectar `MoveFieldCompleteReq` 2549, slot 0, payload `00`, por el mismo carril P2 y con la etiqueta
     de época;
   - esperar 2144 en `take_frame`, con un tiempo máximo de unos 5 s;
   - después, la 2001.

   En la ventana B: la 2001 directamente, si `cut9` pasa. Si no pasa, antes `LoadingCompleteReq` 2445 `00`
   (el clock gate fuerza el byte a 0) y esperar 2762. La puerta «tras el logout solo la 2001» de
   `server_inject` necesita una excepción exacta para estas dos Req y esta secuencia, una sola vez.
3. **Seguridad ante fallos:**
   - cada paso tiene un tiempo máximo; si vence, se entrega la 2001 igualmente (el comportamiento de
     hoy), con la misma barrera y la cuarentena como último recurso;
   - una fase desconocida se trata igual que hoy;
   - no se escriben `FM+0x58`, `TS+0x3DC` ni `+0x5E`;
   - no se llama a `LeaveField`, `RemoveActor` ni `0x2068340`;
   - no se usan 2063 (global) ni 2564 (convergen en el mismo `GetField`).
   - Si `MoveFieldComplete` falla, el propio servidor ya hace `Logout(pj, 0xB, …)` (`0x2BC6BB0`).

**Riesgos:**
- Ráfaga de `ActorLoadDataAck` hacia una sesión muerta: la cola de salida debe descartarla de forma
  acotada.
- El personaje del usuario 2 se publica un instante a los observadores del anfitrión antes de
  desaparecer. Lo cubren la guarda de capas (E3b) y el child hold (D-53).
- En el aparcamiento (el usuario 2 es dueño del campo), `MoveFieldComplete` puede pedir autoguardado
  (motivo 5) y consume el registro global del gestor (`gestor+0x90`, R3). P6 tiene que seguir activo.
- En B, `LoadingComplete` reinicia marcas globales del gestor y mete `pj+0xC3` en el autoguardado.
  Por eso se usa solo si `cut9` lo exige.

**Criterio:** `cut8` y `cut3` deben acabar con `logout Acks out 2`, la barrera limpia y `reconnect
epoch=N+1 ready`. Si siguen en cuarentena con el `MoveFieldComplete` inyectado, la hipótesis de §1.3
queda refutada y el siguiente sospechoso es una referencia que suelta `LoadingComplete`.

## 5. Afirmaciones refutables

1. **[E/E-x]** «Mientras `FM+0x58 != 0`, `GetField` niega el acceso y el logout no puede llamar a
   `LeaveField`.» Se refuta con una lectura en vivo de `FM+0x58 = 0` tras la 2001 en `cut8`, o con
   `LeaveField` llamado por otra ruta.
2. **[V/S]** «P8 cayó en la ventana A.» Se refuta con una grabación del invitado que muestre
   `MoveFieldCompleteReq` antes del corte.
3. **[S]** «`cut9` pasa.» Si da cuarentena, `LoadingComplete` sujeta otra referencia y el arreglo
   necesita también el paso B.

## Ficheros

- Informe: este fichero.
- Guiones: `docs/research/data/login_e3lc00cut3_112627.troc`, `…cut8…`, `…cut9…`.
- Generador: `tools/scripts/make_troc_script.py` (opción `--end-after-step`; las salidas anteriores no
  cambian).
- Comprobación: `tools/scripts/check_troc_scripts.py` (tres entradas nuevas; `ctest -R optd_scripts`
  pasa).
- Pendiente: `tests/optd_tests.cpp` (~l. 991) no incluye estos guiones en su lista de análisis. No se ha
  tocado porque el parser es de `src/`.
