#pragma once

#include <initializer_list>
#include <string>
#include <utility>

#include "esphome/components/api/api_server.h"

namespace tile_dashboard_ha {

// Ruft eine Home-Assistant-Aktion auf (z.B. "light.turn_on" mit entity_id).
//
// Baut die HomeassistantActionRequest direkt, statt api::HomeAssistantServiceCallAction
// zu verwenden: dessen Konstruktor und set_service/init_data/add_data sind mit
// ESPHome 2026.10 weggefallen. Die Request-Struktur und
// APIServer::send_homeassistant_action() sind von 2026.3 bis 2026.10 unveraendert.
// Die Strings muessen nur bis zum Ende von send_homeassistant_action() leben,
// das gilt fuer Literale und die initializer_list-Eintraege.
inline void call_action(const char *action,
                        std::initializer_list<std::pair<const char *, std::string>> data) {
  auto *server = esphome::api::global_api_server;
  if (server == nullptr)
    return;

  esphome::api::HomeassistantActionRequest req;
  req.service = esphome::StringRef(action);
  req.is_event = false;
  req.data.init(data.size());
  for (const auto &kv : data) {
    auto &entry = req.data.emplace_back();
    entry.key = esphome::StringRef(kv.first);
    entry.value = esphome::StringRef(kv.second);
  }
  server->send_homeassistant_action(req);
}

}  // namespace tile_dashboard_ha
