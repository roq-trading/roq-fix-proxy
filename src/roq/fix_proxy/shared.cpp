/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/shared.hpp"

#include "roq/logging.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {

// === HELPERS ===

namespace {
template <typename R>
auto create_username_to_password_and_strategy_id_and_component_id(auto &config) {
  using result_type = std::remove_cvref<R>::type;
  result_type result;
  for (auto &[_, user] : config.users)
    result.try_emplace(user.username, user.password, user.strategy_id, user.component);
  return result;
}

template <typename R>
auto create_regex_symbols(auto &config) {
  using result_type = std::remove_cvref<R>::type;
  result_type result;
  for (auto &symbol : config.symbols) {
    utils::regex::Pattern regex{symbol};
    result.emplace_back(std::move(regex));
  }
  return result;
}
}  // namespace

// === IMPLEMENTATION ===

Shared::Shared(Settings const &settings, Config const &config, fix::proxy::Manager &proxy)
    : settings{settings}, proxy{proxy}, regex_symbols_{create_regex_symbols<decltype(regex_symbols_)>(config)},
      username_to_password_and_strategy_id_and_component_id_{
          create_username_to_password_and_strategy_id_and_component_id<decltype(username_to_password_and_strategy_id_and_component_id_)>(config)},
      crypto_{settings.client.auth_method, settings.client.auth_timestamp_tolerance} {
}

bool Shared::include(std::string_view const &symbol) const {
  for (auto &regex : regex_symbols_)
    if (regex.match(symbol))
      return true;
  return false;
}

fix::codec::Error Shared::session_logon_helper(
    uint64_t session_id,
    std::string_view const &component,
    std::string_view const &username,
    std::string_view const &password,
    std::string_view const &raw_data,
    uint32_t &strategy_id) {
  auto iter_1 = username_to_password_and_strategy_id_and_component_id_.find(username);
  if (iter_1 == std::end(username_to_password_and_strategy_id_and_component_id_)) {
    log::warn("Invalid: username"sv);
    return fix::codec::Error::INVALID_USERNAME;
  }
  auto &[secret, strategy_id_2, component_2] = (*iter_1).second;
  if (component != component_2) {
    log::warn("Invalid: component"sv);
    return fix::codec::Error::INVALID_COMPONENT;
  }
  if (!crypto_.validate(password, secret, raw_data)) {
    log::warn("Invalid: password"sv);
    return fix::codec::Error::INVALID_PASSWORD;
  }
  strategy_id = strategy_id_2;
  return {};
}

}  // namespace fix_proxy
}  // namespace roq
