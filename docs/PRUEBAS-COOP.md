# Pruebas del cooperativo (versión del 2026-10-03, protocolo v4)

Todo lo de esta lista está escrito y compilado, pero **sin probar en el juego**. Se prueba en un
solo PC con `cdmp_peer --mode relay`, que te devuelve tus propios datos: el "otro jugador" eres
tú, con unos 0,4 s de retraso.

## Preparación
1. Copia de seguridad de las partidas (`%LOCALAPPDATA%\Pearl Abyss\CD\save` → `backups\`).
2. Arranca el puente:
   `build\bin\RelWithDebInfo\cdmp_peer.exe --mode relay --delay 50 --offset 0`
3. `bin64\cdmp.ini` debe tener `[coop] enabled=1`. Ya está así.
4. Abre el juego, carga la partida y muévete. Sale tu copia.
5. Al terminar, sal **sin guardar**.

## Qué probar y qué debería verse

| # | Prueba | Qué debería pasar | Línea del `cdmp.log` |
|---|---|---|---|
| 1 | Andar, correr, saltar, salientes, escalar | Igual que antes | `Co-op: the other player is in our world` |
| 2 | **Cambiar una pieza de equipo** | La copia se la cambia ~0,4 s después | `Capture: equipment change`, `Puppet: equipment change` |
| 3 | **Quitarte una pieza** | La copia se la quita | igual |
| 4 | **Sacar y guardar el arma** | La copia la pasa de la espalda a la mano y al revés | `Capture: weapon docking event` |
| 5 | **Llamar al caballo y montar** | Aparece un segundo caballo (la copia) y la copia del jugador lo monta | `Capture: horse template`, `Puppet: horse queued`, `Puppet: mount/dismount enabled` |
| 6 | **Cabalgar** | Los dos caballos van juntos y el jinete se queda en la silla | — |
| 7 | **Desmontar** | La copia baja del caballo | `mount/dismount enabled` |
| 8 | Recibir daño | Nada visible (la vida de la copia cambia por dentro) | — |
| 9 | Mojarte o quemarte | La copia también (efecto elemental) | — |
| 10 | Morir | La copia muere, y al revivir revive | — |

## La copia se hunde o flota (normal con un solo PC)
- Con un PC, la copia aparece desplazada `[coop] offset_m` metros en X, pero a **tu** altura. En
  terreno inclinado, el suelo a ese lado está más alto o más bajo, y la copia (que no se ajusta al
  suelo: va a la altura que recibe) queda enterrada o flotando. Medido: 0,35 m de media con 4 m,
  0,5 m con 12 m, hasta 1,7 m (`docs/research/pendiente-copia-se-hunde.md`).
- **Con dos PCs no pasa:** el kit usa `offset_m=0` y cada uno envía su posición real sobre el mismo
  terreno.
- Para pruebas de movimiento o visuales, la copia puede ir **detrás de ti** en vez de a un lado:
  `offset_m=0.01` (evita los 2,5 m que se ponen solos con `cdmp_peer`) y `delay_ms` de 1500 a 3000.
- Las separaciones grandes (4-10 m) solo hacen falta para las pruebas de golpes a las copias.

## Si algo va mal
- **El caballo da problemas:** `[coop] horse=0` en `cdmp.ini`.
- **Algo raro en general:** `[coop] enabled=0` desactiva todo el cooperativo.
- Mándame el `cdmp.log` y, si puedes, un video.

## Conectar con un amigo
- **Paquete:** `dist\cdmp-coop-<fecha>.zip`, con instrucciones en `LEEME.txt`.
- **Por Steam:** sin abrir puertos (`transport=steam`; uno pone `role=host`).
  - Comprobado en tu PC que Steam se inicializa y la red de relays queda lista.
  - La conexión real entre dos cuentas está sin probar.
- **Por IP:** con VPN o con el puerto abierto (`transport=udp`).

## Pruebas de combate (opcionales; escriben en el juego)
Sirven para decidir cómo hacer el daño compartido (`docs/research/combate-coop.md` §4 y §6).
**Haz copia de las partidas y no guardes después.**
1. En `cdmp.ini`, sección `[dev]`, añade `combat_tests=1`.
2. Ponte cerca de un enemigo (a menos de 15 m) que se esté moviendo.
3. `Ctrl+F6` (prueba T2): le quita 5 de vida a través del servidor interno del juego. Púlsalo
   varias veces. ¿Baja su vida? ¿Muere? ¿Suelta botín?
4. Pega al enemigo una vez y luego pulsa `Ctrl+F7` (prueba T3): un golpe "por colisión" con el
   hash de tu último ataque. ¿Reacciona? ¿Pierde vida?
5. Cuéntame qué viste. El `cdmp.log` y `cdmp_packets_*.log` guardan lo demás.
