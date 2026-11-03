# Remates y derribos sigilosos del invitado (S25, paso 1): por qué no funcionan

12 de octubre de 2026, rama `opcion-d`. Build del juego **1.0.0.2976** (PE `0x6AB28F00`, base `0x140000000`),
direcciones en RVA. **Solo investigación:** no se tocó `src/`, no se lanzó el juego y no hay commits. El único
fichero escrito en el repositorio es este.

- **[V]** comprobado (captura, log del mod o decompilado/desensamblado de esta build, citado al lado).
- **[E]** deducción fuerte que falta medir en vivo.
- **[H]** hipótesis.

## Fuentes y relojes

| Id | Fichero | Qué |
|---|---|---|
| G1 | `…\scratchpad\s25-shadow\cdmp_packets_20261012-054022.bin` + `cdmp_20261012-054004.log` | invitado, 1.ª parte (personaje `0xA010034E`). **Sin combate del invitado**: 19 golpes, casi todos al anfitrión |
| G2 | `…\scratchpad\s25-shadow\cdmp_packets_20261012-060451.bin` + `cdmp_20261012-060436.log` | invitado, 2.ª parte (personaje `0xA020280A`, usuario `0x90202808`). **Aquí está todo el combate** (t ≈ 270–480 s) |
| H | `C:\Emulador CD\backups\p8-sesion25-host\cdmp_packets_20261012-063747.bin` + `cdmp_20261012-063736.log` | anfitrión (personaje `0xA0100001`, caballo `0xB0100003`) |

- Pared de G2 = 06:05:16,644 + t; pared de H = 06:38:00,502 + t (anclas `capture t=` de los logs).
- **t(H) = t(G2) + 1635,05 s** y **t(H) = t(G1) + 171,35 s** (los mismos `CatchActionResultAck` en las dos capturas).
- El `.log` de texto del sniffer solo trae 48 B de cada paquete: todo lo de abajo sale de los `.bin` (lector
  `BinRecord` de `troc_sniffer.cpp`). La captura del anfitrión solo ve **su** cliente: las Req del usuario 2 entran
  al servidor por el túnel y no salen en ella.

---

## 0. Resumen

1. **Qué paquetes son [V].** Remate y derribo sigiloso son la misma familia: un **agarre** (*catch*,
   `B_EnemyCatch_00`).
   - El cliente que agarra manda `StartActionReq` 2223 de su personaje y `DoFrameEventReq` 2380 con el frame del
     agarre y la víctima en la lista de objetivos.
   - El servidor contesta a los dos clientes (ToNear) `CatchActionResultAck` 2156 + `AttackTargetAck` 2922 (`F7`).
   - Después, el frame de lanzamiento del que agarra produce `ReleaseCatchResultAck` 2512 + 2922 (`E3`) +
     `VaryHPAck` + `NotifyHandleDeadAck` 2530 (muerte).
   - **El servidor no anima a la víctima.** La animación emparejada la pone **el cliente que controla a la víctima**
     al recibir el 2156: `StartActionReq` de la víctima con la acción que trae el 2156 y padre = quien agarra, más dos
     `ChangeAIStateReq` (suspende su IA).
2. **Por qué falla con el usuario 2 [V].** Los enemigos los controla el **cliente del anfitrión**. En los 11 agarres
   del anfitrión, su cliente lanza la acción de la víctima 13–30 ms después del 2156. **En los 9 del invitado no lo
   hace nunca (0/9).**
   - No es el servidor: acepta los 9 agarres, sin ningún Nak, y aplica el daño y las muertes.
   - No es el mod: ningún filtro toca estos opcodes; `refused 0 blocked 0` en el anfitrión y `0 dropped` en el
     invitado.
   - No es la predicción del invitado: 113 de 116 golpes con objetivo tienen respuesta, y los 3 restantes no son
     golpes a enemigos.
   - **Es una comprobación del cliente** que aplica el 2156 (`ClientCatch_ApplyResult` `0x935DD0`, `0x935E8E`).
     Compara un **número de secuencia** que trae el paquete (su último byte) con el de la acción en curso del que
     agarra *en ese cliente* (`[[[[catcher+0x68]+0x40]+0x48]+0x93]`). Si no coinciden, sale en silencio, antes de
     enganchar a la víctima y de lanzar su acción.
   - Ese número es el byte `+0x3C` de la `StartActionReq` del que agarra: coincide en **20 de 20** agarres. El
     servidor **no lo reenvía**: las 2.684 `StartActionAck` que recibió el anfitrión lo llevan a 0.
   - Así, en el cliente del anfitrión, el personaje remoto del invitado nunca tiene la secuencia del agarre, la
     comprobación falla, y la víctima (que controla ese cliente) no se entera de que la han agarrado.
3. **Arreglo propuesto (§4).** Es solo del anfitrión, sin protocolo nuevo, con un único gancho a la **entrada** de
   `0x935DD0`.
   - Se aplica cuando el que agarra no lo controla este cliente (`byte [actor+0xC8] == 0`) y la víctima sí.
   - En ese caso se copia en el byte de secuencia del paquete (el 20.º argumento, solo se usa en esa comparación) el
     valor que ese cliente tiene del que agarra. Desde ahí todo es nativo: enganche, IA suspendida, acción de la
     víctima con padre = personaje del invitado y relevo al invitado por el servidor.
   - Clave `[session2] catch_seq = 0 | probe | 1`. **Antes, `probe` (solo log)**: la cadena de punteros y el valor
     que tiene el anfitrión no se han leído nunca en vivo (§6).

---

## 1. Qué piden un remate y un derribo (pregunta 1)

### 1.1 Remate completo del invitado (G2, víctima `0xB0203C38`, ya tambaleada por golpes) [V]

```
342.861 C>S StartActionReq  A020280A A0 DCC3F7ED A1 B1AF75E3 target B0203C38   (+0x3C = 0x5D)
343.303 C>S DoFrameEventReq A020280A DCC3F7ED frame 13, objetivos [B0203C38]
343.355 S>C CatchActionResultAck  catcher A020280A victim B0203C38 "B_EnemyCatch_00"  ... último byte 0x5D
343.355 S>C AttackTargetAck B0203C38 <- A020280A flags F7  (+ AttackerAck 2509)
343.420 C>S DoFrameEventReq DCC3F7ED frame 18           -> 343.517 2922 flags E3
343.660 C>S StartActionReq  A020280A A0 8029E0A7 (lanzamiento)
344.262 C>S DoFrameEventReq 8029E0A7 frame 6 (+ frame 16, 4 objetivos)
344.365 S>C ReleaseCatchResultAck A020280A/B0203C38 + 2922 E3 + VaryHPAck + NotifyHandleDeadAck (muerta)
344.398 S>C StartActionAck B0203C38 A0 B456159F (muerte), padre -1
```

Entre 343,355 y 344,365 **no llega ninguna `StartActionAck` de la víctima** al invitado. En el anfitrión (H 1978,405)
su cliente, que controla a la víctima, solo manda `EchoActionSessionIDReq` (+3 ms). No manda acción ni IA hasta la
muerte (+1023 ms, `B456159F`).

### 1.2 Derribo de un enemigo distraído (G2 286,425, víctima `0xB0203203`) [V]

- La víctima peleaba con el anfitrión a caballo: todas sus `StartActionAck` de los 15 s anteriores tienen objetivo
  `0xB0100003`, el caballo del anfitrión, y ninguna al invitado.
- El invitado manda `StartActionReq 3B47AD2F` (`+0x3C = 0x41`) y `DoFrameEventReq 3B47AD2F/f4`. El servidor
  contesta `CatchActionResultAck` (acción de víctima `D3300533`, secuencia `0x41`) + 2922 `F7`, y en f6 2922 `E3`.
- En el anfitrión (H 1921,477) la IA de la víctima sigue con lo suyo: `9B68944F` a +256 ms y `4FD6A2FF` a +631 ms,
  con objetivo el caballo.
- **No hay `ReleaseCatchResultAck`**. El enemigo no cae: muere 8 s después a golpes normales (G2 294,681).
- Es el derribo cuando el enemigo está distraído **[E]**: el único agarre de S25 sobre un
  enemigo que no peleaba con el invitado. Los nombres de las acciones no se conocen.

### 1.3 Los 9 agarres del invitado frente a los 11 del anfitrión (`ctab.py` sobre H) [V]

| t (H) | Quien agarra → víctima | Acción del agarre (seq) | Acción de víctima en el 2156 | **Req de la víctima desde el cliente del anfitrión** (≤ 1,5 s) | IA (≤ 0,3 s) | Suelta / muerte |
|---|---|---|---|---|---|---|
| 830,664 | anfitrión → `B0102921` | `2D16D5B5` (80) | `D6FABEEE` | **`D6FABEEE` +16 ms** | 2 | +1,66 s |
| 837,714 | anfitrión → `B010288B` | `DCC3F7ED` (85) | `C4ACB859` | **`C4ACB859` +14 ms, padre `A0100001`**; otra vez +349 ms | 2 | +0,91 s |
| 840,893 … 2095,632 | anfitrión → 9 más | `DCC3F7ED`, `573BDA2D`, `56A3B30F`, `A1FAC71B` | `C4ACB859`, `89A6A8AC`, `0D668195`, `2A353CC2` | **la acción del 2156, +13–30 ms** (padre = anfitrión en 9 de 11) | 2–4 | +0,8–1,4 s, todas |
| 1921,477 | **invitado** → `B0203203` (distraído) | `3B47AD2F` (41) | `D3300533` | **ninguna**; IA propia `9B68944F` +256 ms | 1 | — |
| 1978,405 | invitado → `B0203C38` | `DCC3F7ED` (5D) | `C4ACB859` | **ninguna** (muerte +1023 ms) | 0 | +1,00 s |
| 2002,283 | invitado → `B0203C31` | `32182BE5` (6F) | `680F4303` | **ninguna**; IA `DE9F6A5D` +105 ms | 5 | — |
| 2010,011 | invitado → `B0203C31` | `9D7AE56A` (74) | `EB49AD13` | **ninguna** | 0 | — |
| 2072,980 | invitado → `B0200096` | `0F0E0615` (8A) | `237E36D3` | **ninguna** | 0 | +0,74 s |
| 2074,853 | invitado → `B02038E5` | `E0FCFE94` (8C) | `503569F5` | **ninguna** | 0 | +0,93 s |
| 2081,812 | invitado → `B0202D83` | `56A3B30F` (92) | `0D668195` | **ninguna** | 0 | — |
| 2086,779 | invitado → `B0203C3E` | `DCC3F7ED` (98) | `C4ACB859` | **ninguna** | 0 | +0,91 s |
| 2105,345 | invitado → `B0202894` | `CBEB47C1` (A3) | `89A748C4` | **ninguna** | 0 | +0,97 s |

- **Anfitrión 11/11, invitado 0/9** en la acción de la víctima. El mismo combo (`DCC3F7ED` → `C4ACB859`) funciona con
  uno y no con el otro.
- La suelta la produce siempre el `DoFrameEventReq` **del que agarra**: `8029E0A7/f6`, `2D16D5B5/f13`,
  `573BDA2D/f15`, `911377A5/f5` o `3FD64E46/f7` (`rel.py`).
- 4 de los 9 agarres del invitado se quedan sin suelta: tras el agarre, su cliente no manda el frame de suelta.
  - Ejemplo: en 446,755 sí lanza `911377A5`, pero solo manda f14, nunca f5.
  - **[H]** su agarre local se deshace porque la víctima no entra en la animación emparejada. Por qué no está medido.
- Formato del 2156 (85 B; lector `0xB582B0`):
  - `+0` quien agarra, `+4` víctima, `+8` u8 tipo (0);
  - dos cadenas (`u32` longitud + texto + NUL): hueso y socket;
  - u64 1, f32, **u32 acción de la víctima** (`C4ACB859`…), u32 grupo de acciones globales `0x64528294`, u32 0;
  - 6 × u8 (el 6.º, `c8`, vale 1 en todos), u8, u32 y **u8 secuencia** (último byte).

### 1.4 Cuando el anfitrión remata, el invitado lo ve bien [V]

G1 666,341 (agarre del anfitrión a `B010288B`):
- llega el 2156;
- 16 ms después, `StartActionAck B010288B C4ACB859` **con padre `A0100001`**, otra vez a +345 ms;
- después la suelta y la muerte.

El invitado no controla a la víctima ni lo necesita: la acción emparejada le llega relevada.

---

## 2. Por qué falla para el usuario 2 (pregunta 2)

### 2.1 Lo que no es [V]

| Candidato | Evidencia |
|---|---|
| Validación del servidor (distancia, permisos, «el usuario local», dueño del objetivo) | Los 9 agarres del invitado reciben `CatchActionResultAck` en los dos clientes, sin `CatchNak` 2394 ni `StartActionNak` 2570 en ninguna captura. Además, las sueltas aplican daño y matan (`VaryHPAck`, 2530) |
| Filtros o vetos del mod | Nada en `src/mod` menciona 2156/2512/2394/3050 (grep). El inyector del anfitrión, todo S25: `in: handed 390925 refused 0 blocked 0` (H 07:14). El túnel del invitado: `out 78524 packets, 0 bad, 0 dropped`, `in 269194 packets, 0 rejected`. `ride_filter` solo reescribe las 2991 de montar/desmontar |
| El servidor ignora o rechaza la predicción del invitado | G2: 116 `DoFrameEventReq` del invitado con objetivo, 113 con respuesta completa. Los 3 sin respuesta: dos golpes a sí mismo (`EB340761`) y un saqueo (`0F56DD5D`, contestado con `LootDropCharacterRewardListAck`) (`dfe.py`) |
| Animación emparejada que exige a los dos en un cliente | Es parecido, pero el juego ya la reparte: el cliente que controla a la víctima la lanza al recibir el 2156 y la releva (§1.4). Falla porque ese cliente **descarta** el 2156 del invitado (§2.2) |

### 2.2 La causa: la comprobación de secuencia de `ClientCatch_ApplyResult` [V código + V datos; E el valor leído]

Camino del 2156 en un cliente (decompilado y desensamblado de esta build):

```
0xB582B0 CatchActionResultAck::vf2      (cola de Acks, hilo principal; único llamador de 0x935DD0: 0xB58943)
  busca a quien agarra por clave; comp = [[catcher+0x68]+0x70]  (ClientCatchActorComponent, vtable 0x55BAE48)
  -> 0x935DD0 ClientCatch_ApplyResult(comp, …, p17 = &c8, …, p20 = &seq)
       0x935E4A  cmp byte [p17],0 ; jz FindCaught            (c8 = 1 en los 20 agarres)
       0x935E7E  rcx = [[[[comp+8]+0x68]+0x40]+0x48]         (CC del que agarra -> +0x48)
       0x935E8E  cmp byte [rcx+0x93], seq ; jz FindCaught
                 si no: (seq == comp+0x6C -> comp+0x68 = 0) y SALE, sin error ni log
     FindCaught: busca a la víctima; si no existe, error 0x6CF705C (CatchTargetNotFound)
       -> 0x20AC330: engancha con vf1A0 0x934E70 (socket "B_EnemyCatch_00"), y si victim->vf188(5)
          (byte [victim+0xC8] != 0: «lo controla este cliente»), LaunchGlobalAction(grupo 0x64528294,
          acción = u32 del 2156) -> StartActionReq de la víctima
```

- **La secuencia es la de la acción del que agarra:** el último byte del 2156 es igual al `+0x3C` de su última
  `StartActionReq`, el byte `+0x38` del bloque A0. Coincide en **11/11** agarres del anfitrión (H) y **9/9** del
  invitado (G2) (`seqeq.py`).
  - Ejemplo: `DCC3F7ED` del invitado lleva `+0x3C = 0x5D` y su 2156, `…5D`. En el anfitrión, `DCC3F7ED` lleva `0x85`
    y su 2156, `…85`.
- **El servidor no la reenvía:** las **2.684** `StartActionAck` que recibió el cliente del anfitrión (1.579 de
  personajes y 1.105 de `0xB`) tienen `+0x3C = 0` y también a 0 el byte equivalente del bloque A1 (`seqhist.py`).
  - Ejemplo: la Req del invitado `DCC3F7ED`… `03 00 5D` llega al anfitrión (H 1977,982) como `03 00 00`.
- **Consecuencia:** en el cliente del anfitrión, la acción en curso del personaje del invitado no tiene esa
  secuencia, así que `0x935E8E` sale y no hay enganche ni acción de la víctima.
  - Que el byte de `+0x93` sea en concreto 0 está **[E]**: no se ha leído en vivo.
  - Que `[CC+0x48]` no es nulo para el remoto sí está **[V]**: la instrucción lo desreferencia en los 9 agarres sin
    ningún crash.
  - En el propio cliente del que agarra coincide siempre. Por eso el anfitrión, que agarra y controla a la vez,
    funciona.
- **El 2156 es lo que lanza la acción de la víctima, no el frame del agarre [V]:** en los 10 agarres del anfitrión
  con timing limpio, el orden es `DoFrameEventReq` (−1 a −16 ms), 2156 y 2922 (0), `EchoActionSessionIDReq` (+10 a
  +28 ms), Req de la víctima (+2–3 ms después del Echo) (`order.py`).
  - El Echo es la respuesta al 2922 del mismo lote, así que la acción de la víctima sale al procesar ese lote, por
    `0xB582B0` → `0x935DD0` → `0x20AC330`.
  - Con el invitado, el Echo sale (H 1978,408, +3 ms) y la Req no. El lote se procesó y la cadena del 2156 se cortó.
- **En el MMO** los monstruos los controlaba el servidor, que hacía su parte (`ServerCatchActorComponent`). Por eso un
  observador no necesitaba pasar esta comprobación por un agarrador remoto. En el modelo de un jugador, que es el de
  la opción D, el controlador de la víctima es un cliente, y la comprobación lo deja fuera cuando quien agarra es
  remoto.

### 2.3 Lo que ve cada uno [V paquetes; E pantalla]

- **Invitado:** su personaje hace la animación del remate. Su propio 2156 sí pasa la comprobación: engancha a la
  víctima en local, pero no lanza su acción, porque no la controla (`vf188(5)` falso). La víctima se queda en su
  último estado o sigue su IA (lo que releva el anfitrión) en vez de seguir el agarre.
  - En 5 de 9 casos la suelta mata igualmente: el daño es del servidor.
  - En 4 no hay suelta, así que no hay remate. Es el caso del enemigo distraído (§1.2).
- **Anfitrión:** ve al invitado hacer el gesto al aire, y la víctima sigue con su IA. No se suspende: 0
  `ChangeAIStateReq` en 7 de 9, frente a 2 en cada agarre propio.
- **Remates del anfitrión vistos por el invitado:** bien (§1.4).

### 2.4 Lo que falta saber [H]

- Si el derribo sigiloso tiene otra variante (otra interacción u otro frame) que el cliente del invitado **ni
  siquiera intenta**, por ejemplo porque comprueba en local el estado de alerta de un enemigo remoto. En S25 no se ve
  ningún intento así: 0 `InteractionPreemptionStepReq` del invitado con un enemigo vivo; las de `0x000F4249` /
  `0x000F428C` / `0x000F4274` son saqueos de su compañero `0xB020280B`. Hay que preguntar al usuario o grabarlo.
- Por qué 4 de 9 agarres del invitado no llegan a la suelta (§1.3).

---

## 3. Causa raíz

En la opción D, el **controlador** de los enemigos es el cliente del anfitrión. El juego, en su modo de un jugador,
aplica el agarre (`CatchActionResultAck`) en el cliente que controla a la víctima, solo si la acción en curso de quien
agarra **en ese cliente** tiene la secuencia que trae el paquete (`0x935E8E`). Esa secuencia viaja en la
`StartActionReq` de quien agarra (`+0x3C`), pero el servidor la pone a 0 al reenviarla. Así, para un agarrador remoto
(el usuario 2) la comprobación falla siempre y en silencio:

- el cliente del anfitrión no engancha a la víctima;
- no le suspende la IA;
- no lanza su acción emparejada (el u32 del 2156, con padre = quien agarra).

Remate y derribo sigiloso son agarres, así que los dos fallan igual. El servidor y el mod no rechazan nada.

---

## 4. Arreglo propuesto (pregunta 3; descrito, no implementado)

### 4.1 Dónde y qué

**Solo en el anfitrión**, módulo nuevo `src/mod/session2/catch_seq{.cpp,.h,_logic.h}`, clave
`[session2] catch_seq = 0 | probe | 1`, apagada por defecto.

- **Gancho:** `safetyhook::create_inline` en la **entrada** de `ClientCatch_ApplyResult` `0x935DD0`.
  - Prólogo verificado: `48 89 5C 24 08  48 89 74 24 10  48 89 7C 24 18  4C 89 4C 24 20  55 41 54 41 55`. Se
    comprueban los bytes antes de instalar, dentro de `.rsrc`, como los demás ganchos.
  - Es una entrada de función, no un punto a mitad. Su único llamador es el manejador del 2156 (`0xB58943`, xrefs).
  - Corre en el hilo principal del cliente, el que aplica los Acks.
- **Firma:** 20 argumentos, todos punteros salvo `comp`; se reenvían tal cual. Interesan:
  - `comp` (`rcx`): el `ClientCatchActorComponent` del que agarra; el actor es `[comp+8]` y su clave `[actor+0x60]`;
  - `p3`: puntero a la clave de la víctima;
  - `p17`: puntero al u8 `c8`;
  - `p20`: puntero al u8 de secuencia. Es una variable del manejador y **solo se usa en la comparación de
    `0x935E8E`** (decompilado).
- **Regla** (todas las lecturas con SEH: `fast_read` o `__try`):
  1. `*p17 != 0`;
  2. quien agarra **no** lo controla este cliente: `byte [catcher+0xC8] == 0`;
  3. la víctima **sí**: se busca por la clave `*p3` y se exige `byte [victim+0xC8] != 0`;
  4. se lee `b = [[[[catcher+0x68]+0x40]+0x48]+0x93]`; si no se puede leer, no se toca nada y se cuenta.
- **`probe`:** solo log y contadores. Uno por agarre, con quien agarra, víctima, sus `+0xC8`, `c8`, `seq`, `b`,
  `comp+0x68`/`+0x6C` y «se corregiría sí/no». También cuenta los agarres en los que quien agarra **sí** se controla
  aquí: ahí `b` debería ser igual a `seq`.
- **`1`:** si se cumplen 1–3 y `b != seq`, `*p20 = b` y se llama al original. Desde ahí todo es nativo: enganche,
  `ChangeAIState`, `LaunchGlobalAction` y la `StartActionReq` de la víctima con padre = personaje del invitado, que el
  servidor acepta porque la víctima es del anfitrión. El servidor la releva al invitado como en §1.4.
  - Se elige `p20` y no poner `c8 = 0`, aunque las dos saltan la comprobación: `c8` también se copia en los datos del
    agarre (`local_55` → `param_4+0x1B` de `0x20AC330`). `p20` no va a ningún otro sitio.
  - Si hiciera falta, la regla vale igual en el invitado, para el caso simétrico de un enemigo del anfitrión que agarra
    al personaje del invitado. Primero solo `probe`: en S25 no hubo ninguno.
- Sin cambios de protocolo: no sube `kVersion`, no hay mensajes de túnel nuevos y el invitado no cambia.

### 4.2 Por qué ahí y no en otro sitio

| Alternativa | Por qué no |
|---|---|
| Que el invitado mande la `StartActionReq` de la víctima | El servidor la tira: comprobación 7 de `opcion-d-accion-usuario2.md` §2.1, `actor+0x90` ≠ usuario de la sesión, `eErrNoNoHavePermision`, severidad 2, sin respuesta |
| Reenviar el byte `+0x3C` en las `StartActionAck` (servidor o `OnRecvPacket`) | No consta que el cliente ponga `[CC+0x48]+0x93` a partir del Ack. `0x863530` copia la acción decodificada en `[CC+0x50]+0x10`, no en `+0x48`. Además cambia todas las acciones remotas de los dos clientes |
| Reescribir el 2156 en `OnRecvPacket` del anfitrión (hilo de red) | Habría que leer `b` desde otro hilo antes de que el hilo principal aplique el paquete (carrera). Poner `c8 = 0` cambia además los datos del agarre |
| Que el mod inyecte una `StartActionReq` sintética de la víctima | El cliente del anfitrión no la reproduciría (es el controlador; el servidor no se la devuelve), la IA no se suspende y el anfitrión vería otra cosa |
| Ceder el control de la víctima al invitado (`OverriedOwnershipStateTypeyAck` 2168) | Arreglo de fondo del modelo MMO; mucho más riesgo y sin estudiar (`combate-retardo.md` §8.1) |

### 4.3 Efecto esperado [E]

- Con el gancho a `1`, el agarre del usuario 2 se comporta en el cliente del anfitrión como uno propio:
  - Req de la víctima con la acción del 2156 a +13–30 ms;
  - dos `ChangeAIStateReq`;
  - padre = `0xA…` del invitado.
- El invitado recibe esa acción por el relevo nativo unos 30–40 ms después. Con los ~55 ms que tarda su propio 2156,
  la víctima entra en el agarre unos 100–130 ms después del frame. Es la misma latencia que ya tienen las reacciones a
  sus golpes (`combate-retardo.md` §0).
- Sueltas y muertes ya funcionan, y vienen del frame de quien agarra.

---

## 5. Riesgos

1. **Estado del agarre en el personaje remoto [H, el principal].** `0x20AC330` también actúa sobre quien agarra en el
   cliente del anfitrión (`vf160`, `FUN_1417FBEA0(catcher,0)`, campos `comp+0x28…+0x63`).
   - Si deja la copia del invitado en un estado que bloquea sus acciones hasta la suelta (cf. `RideOff`, S9), un
     agarre sin suelta la congelaría.
   - Con el gancho, un agarre que el invitado abandona (4/9 en S25, sin 2512 ni 3050) dejaría a la víctima enganchada
     en el anfitrión.
   - Hay que medirlo (§6). Si pasa, el siguiente paso es una suelta nativa iniciada por el anfitrión, que hoy no está
     diseñada.
2. **`combat_follow` en el anfitrión:** ya no sigue actores con padre (`combat_follow_logic.h` `Skip::Parent`). Pero
   2 de las 11 acciones de víctima del anfitrión van sin padre (`D6FABEEE`, `89A6A8AC`). Con esas, su `2697` sintético
   (pose de mundo de la víctima enganchada a la copia del invitado, que va 200 ms por detrás) puede pelear en el
   invitado con el enganche local.
   - Hay que contarlo. Si molesta, se excluye a la víctima entre el 2156 y su 2512/3050/2530.
3. **Doble enganche en el invitado:** su `ApplyResult` local engancha a la víctima (pasa la comprobación) y después
   llega la acción relevada con padre = su propio personaje. En el MMO pasaba lo mismo con un jugador que agarraba un
   monstruo **[E]**, pero no se ha visto en esta build.
4. **Lecturas de memoria en el hilo principal del juego:** 4 punteros, con SEH, solo cuando llega un 2156 (unos pocos
   por minuto). Escribe un byte en la pila del llamador. No toca el heap del juego.
5. **Alcance:** solo agarradores no controlados aquí con víctima controlada aquí. Los agarres del anfitrión y los de
   NPC contra NPC, como los `B_EnemyCatch_C_00` de `0xB0101069` / `0xB020330E`, no cambian.

## 6. Qué medir antes (sin guardas que rechacen)

1. **Un PC, con el juego (anfitrión solo), `catch_seq=probe`:** el anfitrión hace remates. Se espera en el log que,
   con quien agarra controlado aquí, `b == seq` en todos. Así se valida en vivo la cadena
   `[[[[catcher+0x68]+0x40]+0x48]+0x93]`. **No necesita Shadow.**
2. **Con Shadow, `probe` en los dos PCs** (es solo log):
   - en el anfitrión, los agarres del usuario 2: que `0x935DD0` **se llama**, que `c8 = 1` y que `b != seq`
     (se espera `b = 0`).
     - Si no se llama, la causa es otra: el `ClientCatchActorComponent` (`+0x70`) no existe en el personaje remoto,
       y el gancho no sirve.
   - en el invitado: sus agarres con `b == seq`, y los del anfitrión con `b != seq`.
3. **Con Shadow, `catch_seq=1` en el anfitrión:**
   - en la captura del anfitrión, por cada 2156 del usuario 2: Req de la víctima con la acción del 2156 y padre =
     `0xA…` del invitado en ≤ 40 ms, y 2 `ChangeAIStateReq`;
   - en la del invitado: la 2991 de la víctima con ese padre;
   - vista: vídeo o captura de un remate y de un derribo de un enemigo que pelea con el anfitrión;
   - abortar (volver a `probe`) si hay víctimas sin 2512/3050/2530 en 5 s, una copia del invitado congelada
     (`divergence_probe`, `FreezeWatch`) o errores nuevos de agarre en `Game error` (`Gimmick RelaseCatch…`,
     `processForceReleaseCatch…`).
4. **Preguntar al usuario** si en el derribo sigiloso llegó a ver la animación de su personaje (agarre enviado, caso
   §1.2) o si ni siquiera le salió la opción (§2.4).

## 7. Hallazgos laterales

- **El gancho de escalada / cuerda del invitado tampoco se reproduce en el anfitrión [V log, H causa].** 3 de los 5
  `RemoteCatchResultAck` del invitado (G1 217,119 y 1057,859; G2 492,185) producen en el anfitrión
  `"모종의 이유로 클라에서 RemoteCatch에 실패했습니다…"` (`exe+0x592536`) + `"RemoteCatchResultAck처리에 실패했습니다"`
  (`exe+0xB5BBC9`) a las 06:44:29, 06:58:29 y 07:13:27.
  - Falla la comprobación `FUN_140594AD0` dentro de `RemoteCatchComponent_Apply` `0x592340`. Probablemente es la
    misma familia de problema (un observador valida a un agarrador remoto). Queda sin estudiar.
- Los `Game error` de agarre del invitado (`Gimmick RelaseCatch…` 06:07:07–06:08:07, `processForceReleaseCatch 실패`
  06:07:54) **no coinciden** con ninguno de sus agarres de combate (06:10:02–06:13:06). No tienen que ver con esto.

## 8. Reproducir

Scripts (Python `-I`, lector de `BinRecord`) en el scratchpad de la sesión, carpeta `fin\` (no se conservan):

| Script | Para qué |
|---|---|
| `cap.py` | Lector común |
| `ctab.py <H>` | La tabla de §1.3 |
| `seqeq.py <bin> <clave>` | Secuencia del 2156 frente al `+0x3C` de la Req |
| `seqhist.py <bin>` | `+0x3C` en las Req frente a las Ack |
| `order.py <H>` | Orden `DoFrameEventReq` / 2156 / Echo / Req de la víctima |
| `rel.py <bin>` | Qué produce la suelta y la muerte |
| `dfe.py <bin> <clave>` | Golpes sin respuesta |
| `win.py`, `hwin.py`, `ownsa.py`, `ctx.py`, `catches.py` | Ventanas y contexto |

Decompilados y desensamblados (servidor de Ghidra):

- `0xB582B0` (manejador del 2156), `0x935DD0` (desensamblado `0x935E4A–0x935EA4`), `0x20AC330`, `0x934E70`,
  `0x48AD40`, `0x1EC9C00`, `0x863530`, `0xB5A170` (manejador del 2512: sin comprobación de secuencia), `0x9348A0`;
- xrefs de `0x935DD0` y `0x20AC330`;
- RTTI `pa::ClientCatchActorComponent` (vtable `0x55BAE48`).

---

## 9. Implementado (12 de octubre de 2026; compilado y con tests, **sin probar en el juego**)

**Clave** `[session2] catch_seq = 0 | probe | 1`, **solo anfitrión**, apagada por defecto. En el PC del invitado,
si se pone, avisa y no hace nada.

- Inis: `cdmp.p8-s7-host.ini` con `catch_seq=probe` (paso 1) y `cdmp.p8-s25-host-activo.ini` con `catch_seq=1`
  (paso 2). Los del invitado no cambian.
- **Ficheros:** `src/mod/session2/catch_seq{.cpp,.h,_logic.h}`, `src/mod/config.{h,cpp}`, `src/mod/mod.cpp`,
  `tests/optd_catch_seq_tests.cpp` (test `optd_catch_seq`) y `CMakeLists.txt`.

**Cómo funciona:**

1. Un hilo propio espera a que el personaje local lleve 3 s en el mundo.
2. Antes de instalar nada, comprueba (si algo falla: `REFUSED`, no escribe nada y el módulo queda apagado):
   - la build (marca de tiempo del PE);
   - que todo está en `.rsrc`;
   - los 25 bytes de la entrada de `0x935DD0`;
   - su único llamador (`0xB58943`, `E8 88 D4 DD FF`);
   - los 87 bytes de la comparación que imita (`0x935E43–0x935E9A`);
   - que safetyhook solo parchea instrucciones enteras (límites 5/10/15/20/21/23/25).
3. Instala el gancho inline en la **entrada**.
4. En cada llamada (hilo del juego que aplica los Acks) lee con SEH:
   - los argumentos 3, 17 y 20 (clave de la víctima, `c8`, secuencia);
   - el que agarra (`[comp+8]`: su clave, su `+0xC8` y la cadena `[[[[comp+8]+0x68]+0x40]+0x48]+0x93`);
   - `comp+0x68` / `+0x6C`;
   - la víctima por su clave (`game::find_actor`, VirtualQuery + SEH) y su `+0xC8`.
5. Decide (`csq::decide`), cuenta y deja un evento en un anillo sin bloqueo. El hilo propio lo escribe en el log.
6. **En `1`, solo escribe en el caso «quien agarra no se controla aquí, la víctima sí, `c8 != 0`, los dos bytes
   leídos y distintos»:** pone el byte del que agarra en el argumento 20, que solo usa esa comparación. Si algo no se
   puede leer, deja pasar la llamada sin cambios y lo cuenta.

**Qué buscar en el log del anfitrión:**

- `Config: P8 session 25 grabs: catch_seq 'probe'` (o `'1'`).
- `CatchSeq ON (catch_seq=…, host)` al arrancar.
- `CatchSeq: exe+0x935DD0 (ClientCatch_ApplyResult, …) hooked at its entry (N bytes patched)`, unos 3 s después de
  entrar en el mundo. Si en cambio sale `REFUSED`, el módulo no hizo nada.
- Una línea por agarre (como mucho 300):
  `CatchSeq [probe|1] #n qpc … capture t=…: grabber 0x… (controlled HERE | REMOTE, +0xC8 = …) -> victim 0x… (…) | c8 … | packet seq 0x.. | grabber's byte … 0x.. | <veredicto> | comp+0x68 … | from exe+0xB58948 (the 2156 handler)`.
  - Veredictos: `grabber here, match`, `grabber here, MISMATCH …`, `remote grabber, match`,
    `remote grabber, MISMATCH, victim ours` (`-> WOULD REWRITE 0x5D -> 0x00` en probe, `-> REWRITTEN …` en 1),
    `… victim not controlled here`, `… victim not found here`, `no check (c8 0)`, `UNREADABLE (passed through)`.
  - `capture t=` es la `t` de `cdmp_packets_*.log`: se cruza con el `CatchActionResultAck` de la captura.
- Cada 60 s, `CatchSeq summary (catch_seq=…)` con:
  - llamadas, quien agarra aquí o remoto, coincidencias / `MISMATCH`;
  - los remotos con víctima propia y cuántos se reescribieron (o se reescribirían);
  - víctima no controlada aquí, no encontrada, `c8 0`, ilegibles (argumentos / quien agarra / su byte);
  - llamador inesperado, líneas sin escribir y eventos perdidos.

**Criterios:**

- **Paso 1, un PC (`probe`):** cada remate del anfitrión tiene que salir `grabber here, match`. Esto valida en vivo la
  cadena de punteros. Con un `MISMATCH` local o `UNREADABLE`, no se pasa a `1`.
- **Paso 1, con Shadow (`probe`):** los agarres del usuario 2 tienen que salir
  `remote grabber, MISMATCH, victim ours -> WOULD REWRITE`.
  - Si no sale ninguna línea con quien agarra = el personaje del invitado, el gancho no basta (§6.2: sin componente de
    agarre en el remoto).
- **Paso 2 (`1`):** `REWRITTEN` en cada agarre del usuario 2, y §6.3 en las capturas: la Req de la víctima con la
  acción del 2156 y padre = invitado, y 2 `ChangeAIStateReq`.
  - Abortar (volver a `probe`) por víctimas sin suelta en 5 s, una copia del invitado congelada o errores de agarre
    nuevos (§5.1).

**Desviación de §6.2:** en el invitado no se instala (la clave es solo del anfitrión). La medida simétrica en el
invitado (sus agarres con `b == seq`) queda para cuando haga falta.
