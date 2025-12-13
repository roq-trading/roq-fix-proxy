/* Copyright (c) 2017-2026, Hans Erik Thrane */

#pragma once

#include <memory>

#include "roq/io/context.hpp"

#include "roq/fix_proxy/settings.hpp"

#include "roq/fix_proxy/client/factory.hpp"

namespace roq {
namespace fix_proxy {
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
}  // namespace fix_proxy
}  // namespace roq
