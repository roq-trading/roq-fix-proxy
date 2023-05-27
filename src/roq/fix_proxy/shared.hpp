/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <string>
#include <vector>

#include "roq/utils/container.hpp"

#include "roq/utils/regex/pattern.hpp"

#include "roq/fix/proxy/manager.hpp"

#include "roq/fix_proxy/config.hpp"
#include "roq/fix_proxy/settings.hpp"

#include "roq/fix_proxy/tools/crypto.hpp"

namespace roq {
namespace fix_proxy {

struct Shared final {
  Shared(Settings const &, Config const &, fix::proxy::Manager &);

  Shared(Shared const &) = delete;

  uint64_t next_session_id = {};

  utils::unordered_set<std::string> symbols;

  bool include(std::string_view const &symbol) const;

  Settings const &settings;
  fix::proxy::Manager &proxy;

  // std::string encode_buffer;

  template <typename Success, typename Failure>
  void session_logon(
      uint64_t session_id,
      std::string_view const &component,
      std::string_view const &username,
      std::string_view const &password,
      std::string_view const &raw_data,
      Success success,
      Failure failure) {
    uint32_t strategy_id = {};
    auto error = session_logon_helper(session_id, component, username, password, raw_data, strategy_id);
    if (error == fix::codec::Error{})
      success(strategy_id);
    else
      failure(error);
  }

  void session_remove(uint64_t session_id) { sessions_to_remove_.emplace(session_id); }

  template <typename Callback>
  void session_cleanup(Callback callback) {
    for (auto session_id : sessions_to_remove_)
      callback(session_id);
    sessions_to_remove_.clear();
  }

 protected:
  fix::codec::Error session_logon_helper(
      uint64_t session_id,
      std::string_view const &component,
      std::string_view const &username,
      std::string_view const &password,
      std::string_view const &raw_data,
      uint32_t &strategy_id);

 private:
  std::vector<utils::regex::Pattern> regex_symbols_;
  utils::unordered_map<std::string, std::tuple<std::string, uint32_t, std::string>> const username_to_password_and_strategy_id_and_component_id_;
  utils::unordered_set<uint32_t> sessions_to_remove_;

  tools::Crypto crypto_;
};

}  // namespace fix_proxy
}  // namespace roq
