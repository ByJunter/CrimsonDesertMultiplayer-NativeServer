# Prueba con dos PC (anfitrión + invitado)

Primera sesión seria con el amigo. Sale del protocolo de `docs/research/pendiente-robustez.md` §7,
adaptado a lo que ya funciona (fases 0-3 de `docs/PLAN-PENDIENTES.md`).

- **A** = anfitrión (el usuario), con `cdmp.ini.anfitrion` renombrado a `cdmp.ini`.
- **I** = invitado (el amigo), con el `cdmp.ini` del zip (el del invitado).
- Duración: unos 60 minutos, hablando por voz.
- **Antes de cada paso, A pulsa Ctrl+F12** (marca en el log) y dice el número en voz alta
  ("marca 7: I viaja"). Si algo raro pasa, los dos pulsan Ctrl+F12 y lo apuntan.
- **Nadie guarda la partida** en toda la prueba.

## 0. Preparación (los dos)

1. Copia de seguridad de `%LOCALAPPDATA%\Pearl Abyss\CD\save` a otra carpeta.
2. **El mismo zip en los dos.** Copiar a `bin64`: `winmm.dll`, `cdmp.asi` y `cdmp.ini` (A: el
   de anfitrión renombrado).
3. Cada uno pone su `player_name`.
4. Si al entrar sale el aviso rojo "Versiones distintas del mod", parad: alguno no tiene el zip
   bueno.

## A. Conexión y lo básico (10 min)

| Paso | Quién | Qué | Qué mirar |
|---|---|---|---|
| A1 | A y luego I | Abrir el juego y cargar la partida | Panel (F8): "conectado a …" |
| A2 | Los dos | Viaje rápido al mismo punto | La copia del otro aparece en pocos segundos |
| A3 | Los dos | Andar, correr, saltar, escalar, planear 2 min | Que la copia no se teletransporte ni se hunda |
| A4 | Los dos | **Nadar** 1 min (nunca probado) | ¿La copia nada, flota o desaparece? |
| A5 | I | Cambiarse una pieza de equipo y sacar el arma | La copia de I en A lo refleja |
| A6 | Los dos | Llamar al caballo, montar, cabalgar juntos y desmontar | Que la copia monte y desmonte bien |

## B. Ciclo de vida (15 min)

| Paso | Quién | Qué | Qué mirar |
|---|---|---|---|
| B1 | A | Viaje rápido lejos y volver | En I, la copia de A desaparece y vuelve, sin parpadeos en bucle |
| B2 | I | Lo mismo | Igual, en A |
| B3 | I | Abrir el mapa o el inventario 30 s y cerrarlo | En A, ¿la copia de I salta o se acelera al volver? |
| B4 | I | Morir (por ejemplo, una caída alta) y reaparecer | En A, la copia de I muere, reaparece y se mueve |
| B5 | A | Morir y reaparecer | Igual, en I |
| B6 | I | Salir al menú principal, esperar 20 s y volver a cargar | En A, la copia se va y vuelve |
| B7 | I | **Cerrar el juego** y volver a entrar | Se reconectan solos |

## C. Combate (20 min)

Buscad un grupo pequeño de enemigos (2-4). Nota: I también ve los enemigos de su propia partida en
el mismo sitio (aún no se quitan). Si podéis, elegid un grupo que I ya haya matado en su partida.

| Paso | Quién | Qué | Qué mirar |
|---|---|---|---|
| C1 | A | **Ctrl+F7** y empezar a pelear | I ve las copias de los enemigos de A moverse y atacar |
| C2 | I | Pegar a una copia hasta matarla | En A, el enemigo real se tambalea, pierde vida y muere, con botín para A |
| C3 | I | Dejar que los enemigos le peguen | I pierde vida y se tambalea con cada golpe; ¿le atacan a menudo o casi nunca? |
| C4 | I | Dejarse matar por los enemigos de A | I muere con su animación normal y reaparece |
| C5 | A | Viaje rápido en pleno combate | En I, las copias desaparecen |
| C6 | I | Viaje rápido en pleno combate y volver | Que no queden copias congeladas |

## D. Carga (10 min)

| Paso | Quién | Qué | Qué mirar |
|---|---|---|---|
| D1 | Los dos | Campamento grande (8 o más enemigos), 2 min de pelea con Ctrl+F7 | Tirones, retraso, copias que no aparecen |
| D2 | Los dos | Ciudad con mucha gente, 2 min | Tirones o retraso de la copia del otro |

## Qué envía cada uno al terminar (un zip por jugador)

- De `bin64`: `cdmp.log` y, si hubo reinicios del juego, `cdmp.1.log`, `cdmp.2.log`…;
- el `cdmp_packets_<fecha>.bin` de la sesión (el `.log` de paquetes no hace falta);
- su `cdmp.ini`;
- la lista de marcas con lo que vio en cada una ("M7: la copia de A se quedó quieta 5 s");
- vídeo o capturas, si los hay;
- si el juego se cerró solo: la hora exacta.

## La sesión es buena si

- No hay cierres del juego ni cuelgues.
- A y B salen sin tener que reiniciar el juego.
- En el combate: al menos un enemigo real de A muerto por I, e I herido y tambaleado por los
  enemigos de A.
