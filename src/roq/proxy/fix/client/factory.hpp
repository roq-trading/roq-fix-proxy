/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>

#include "roq/proxy/fix/shared.hpp"

#include "roq/proxy/fix/client/session.hpp"

namespace roq {
namespace proxy {
namespace fix {
namespace client {

struct Factory {
  virtual std::unique_ptr<Session> create(Session::Handler &, uint64_t session_id, Shared &) = 0;
};

}  // namespace client
}  // namespace fix
}  // namespace proxy
}  // namespace roq
