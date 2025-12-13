/* Copyright (c) 2017-2026, Hans Erik Thrane */

#pragma once

#include <chrono>
#include <memory>

#include "roq/start.hpp"
#include "roq/stop.hpp"
#include "roq/timer.hpp"

#include "roq/utils/container.hpp"

#include "roq/io/context.hpp"

#include "roq/fix_proxy/settings.hpp"
#include "roq/fix_proxy/shared.hpp"

#include "roq/fix_proxy/client/session.hpp"

#include "roq/fix_proxy/client/listener.hpp"

namespace roq {
namespace fix_proxy {
namespace client {

struct Manager final : public Listener::Handler {
  Manager(Settings const &, io::Context &, Shared &);

  void operator()(Event<Timer> const &);

  void dispatch(auto &value) {
    for (auto &[_, item] : sessions_) {
      (*item)(value);
    }
  }

  template <typename Callback>
  void get_all_sessions(Callback callback) {
    for (auto &[_, session] : sessions_) {
      callback(*session);
    }
  }

  template <typename Callback>
  bool find(uint64_t session_id, Callback callback) {
    auto iter = sessions_.find(session_id);
    if (iter == std::end(sessions_)) {
      return false;
    }
    callback(*(*iter).second);
    return true;
  }

 protected:
  // fix::Listener::Handler
  void operator()(Factory &) override;

  // utilities

  void remove_zombies(std::chrono::nanoseconds now);

 private:
  Listener fix_listener_;
  Shared &shared_;
  utils::unordered_map<uint64_t, std::unique_ptr<Session>> sessions_;
  std::chrono::nanoseconds next_garbage_collection_ = {};
};

}  // namespace client
}  // namespace fix_proxy
}  // namespace roq
