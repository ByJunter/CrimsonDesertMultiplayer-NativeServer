#pragma once
#include <string_view>
namespace cdmp::session2 {
// Worker del anfitrión; 0 no instala nada. probe/1 observan sin cambiar retorno
// nativo.
void enemy_damage_probe_start(std::string_view mode, bool host);
// Único consumidor: hilo de red del anfitrión. Líneas y resúmenes acotados.
void enemy_damage_probe_poll();
} // namespace cdmp::session2
