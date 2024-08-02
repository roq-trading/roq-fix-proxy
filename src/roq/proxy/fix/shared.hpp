/* Copyright (c) 2017-2024, Hans Erik Thrane */

#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "roq/utils/container.hpp"

#include "roq/utils/regex/pattern.hpp"

#include "roq/proxy/fix/config.hpp"
#include "roq/proxy/fix/settings.hpp"

#include "roq/proxy/fix/tools/crypto.hpp"

namespace roq {
namespace proxy {
namespace fix {

struct Shared final {
  Shared(Settings const &, Config const &);

  Shared(Shared &&) = default;
  Shared(Shared const &) = delete;

  uint64_t next_session_id = {};

  utils::unordered_set<std::string> symbols;

  bool include(std::string_view const &symbol) const;

  Settings const &settings;

  std::string encode_buffer;

  void add_user(std::string_view const &username, std::string_view const &password, uint32_t strategy_id, std::string_view const &component);
  void remove_user(std::string_view const &username);

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
    auto result = session_logon_helper(session_id, component, username, password, raw_data, strategy_id);
    if (std::empty(result))
      success(strategy_id);
    else
      failure(result);
  }

  template <typename Success, typename Failure>
  void session_logout(uint64_t session_id, Success success, Failure failure) {
    auto result = session_logout_helper(session_id);
    if (std::empty(result))
      success();
    else
      failure(result);
  }

  void session_remove(uint64_t session_id);

  template <typename Callback>
  void session_cleanup(Callback callback) {
    for (auto session_id : sessions_to_remove_) {
      session_cleanup_helper(session_id);
      callback(session_id);
    }
    sessions_to_remove_.clear();
  }

  template <typename Callback>
  bool session_find(std::string_view const &username, Callback callback) {
    auto iter = username_to_session_.find(username);
    if (iter == std::end(username_to_session_))
      return false;
    callback((*iter).second);
    return true;
  }

  std::string create_request_id();

 protected:
  std::string_view session_logon_helper(
      uint64_t session_id,
      std::string_view const &component,
      std::string_view const &username,
      std::string_view const &password,
      std::string_view const &raw_data,
      uint32_t &strategy_id);
  std::string_view session_logout_helper(uint64_t session_id);
  void session_remove_helper(uint64_t session_id);
  void session_cleanup_helper(uint64_t session_id);

  utils::unordered_map<std::string, std::tuple<std::string, uint32_t, std::string>> username_to_password_and_strategy_id_and_component_id_;
  utils::unordered_map<std::string, uint64_t> username_to_session_;
  utils::unordered_map<uint64_t, std::string> session_to_username_;
  utils::unordered_set<uint64_t> sessions_to_remove_;

 private:
  std::vector<utils::regex::Pattern> const regex_symbols_;

  uint64_t next_request_id_ = {};
  tools::Crypto crypto_;
};

}  // namespace fix
}  // namespace proxy
}  // namespace roq
