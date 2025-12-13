/* Copyright (c) 2017-2026, Hans Erik Thrane */

#pragma once

#include <vector>

#include "roq/utils/container.hpp"

#include "roq/fix/proxy/manager.hpp"

#include "roq/fix_proxy/settings.hpp"

namespace roq {
namespace fix_proxy {

struct Shared final {
  Shared(Settings const &, fix::proxy::Manager &);

  Shared(Shared const &) = delete;

  uint64_t next_session_id = {};

  Settings const &settings;
  fix::proxy::Manager &proxy;

  void session_remove(uint64_t session_id) { sessions_to_remove_.emplace(session_id); }

  template <typename Callback>
  void session_cleanup(Callback callback) {
    for (auto session_id : sessions_to_remove_) {
      callback(session_id);
    }
    sessions_to_remove_.clear();
  }

 private:
  utils::unordered_set<uint32_t> sessions_to_remove_;
};

}  // namespace fix_proxy
}  // namespace roq
