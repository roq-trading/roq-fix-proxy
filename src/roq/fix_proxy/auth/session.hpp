/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>

#include "roq/api.hpp"

#include "roq/io/context.hpp"

#include "roq/io/web/uri.hpp"

#include "roq/web/socket/client.hpp"

#include "roq/fix_proxy/settings.hpp"

namespace roq {
namespace fix_proxy {
namespace auth {

struct Session final : public web::socket::Client::Handler {
  struct Insert final {
    std::string_view component;
    std::string_view username;
    std::string_view password;
    uint32_t strategy_id = {};
  };

  struct Remove final {
    std::string_view component;
    std::string_view username;
  };

  struct Handler {
    virtual void operator()(Insert const &) = 0;
    virtual void operator()(Remove const &) = 0;
  };

  Session(Handler &, Settings const &, io::Context &, io::web::URI const &);

  void operator()(Event<Start> const &);
  void operator()(Event<Stop> const &);
  void operator()(Event<Timer> const &);

 protected:
  // io::web::socket::Client::Handler
  void operator()(web::socket::Client::Connected const &) override;
  void operator()(web::socket::Client::Disconnected const &) override;
  void operator()(web::socket::Client::Ready const &) override;
  void operator()(web::socket::Client::Close const &) override;
  void operator()(web::socket::Client::Latency const &) override;
  void operator()(web::socket::Client::Text const &) override;
  void operator()(web::socket::Client::Binary const &) override;

 private:
  Handler &handler_;
  std::unique_ptr<web::socket::Client> const connection_;
};

}  // namespace auth
}  // namespace fix_proxy
}  // namespace roq
