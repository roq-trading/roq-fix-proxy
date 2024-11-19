/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>

#include "roq/io/context.hpp"

#include "roq/proxy/fix/settings.hpp"

#include "roq/proxy/fix/client/factory.hpp"
#include "roq/proxy/fix/client/session.hpp"

namespace roq {
namespace proxy {
namespace fix {
namespace client {

struct Listener final : public io::net::tcp::Listener::Handler {
  struct Handler {
    virtual void operator()(Factory &) = 0;
  };

  Listener(Handler &, Settings const &, io::Context &);

 protected:
  // io::net::tcp::Listener::Handler
  void operator()(io::net::tcp::Connection::Factory &) override;
  void operator()(io::net::tcp::Connection::Factory &, io::NetworkAddress const &) override;

 private:
  Handler &handler_;
  std::unique_ptr<io::net::tcp::Listener> const listener_;
};

}  // namespace client
}  // namespace fix
}  // namespace proxy
}  // namespace roq
