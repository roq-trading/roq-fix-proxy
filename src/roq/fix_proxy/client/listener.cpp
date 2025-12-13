/* Copyright (c) 2017-2026, Hans Erik Thrane */

#include "roq/fix_proxy/client/listener.hpp"

#include "roq/logging.hpp"

#include "roq/fix_proxy/client/session.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {
namespace client {

// === HELPERS ===

namespace {
auto create_listener(auto &handler, auto &settings, auto &context) {
  if (std::empty(settings.client.listen_address)) {
    return std::unique_ptr<io::net::tcp::Listener>();
  }
  auto network_address = io::NetworkAddress{settings.client.listen_address};
  log::debug("network_address={}"sv, network_address);
  return context.create_tcp_listener(handler, network_address);
}
}  // namespace

// === IMPLEMENTATION ===

Listener::Listener(Handler &handler, Settings const &settings, io::Context &context) : handler_{handler}, listener_{create_listener(*this, settings, context)} {
}

// io::net::tcp::Listener::Handler

void Listener::operator()(io::net::tcp::Connection::Factory &factory) {
  struct Bridge final : public client::Factory {
    explicit Bridge(io::net::tcp::Connection::Factory &factory) : factory_{factory} {}

   protected:
    std::unique_ptr<Session> create(uint64_t session_id, Shared &shared) override {
      log::info("Connected (session_id={})"sv, session_id);
      return std::make_unique<Session>(factory_, session_id, shared);
    }

   private:
    io::net::tcp::Connection::Factory &factory_;
  } bridge{factory};
  handler_(bridge);
}

void Listener::operator()(io::net::tcp::Connection::Factory &factory, io::NetworkAddress const &network_address) {
  struct Bridge final : public client::Factory {
    Bridge(io::net::tcp::Connection::Factory &factory, io::NetworkAddress const &network_address) : factory_{factory}, network_address_{network_address} {}

   protected:
    std::unique_ptr<Session> create(uint64_t session_id, Shared &shared) override {
      log::info("Connected (session_id={}, peer={})"sv, session_id, network_address_.to_string_2());
      return std::make_unique<Session>(factory_, session_id, shared);
    }

   private:
    io::net::tcp::Connection::Factory &factory_;
    io::NetworkAddress const &network_address_;
  } bridge{factory, network_address};
  handler_(bridge);
}

}  // namespace client
}  // namespace fix_proxy
}  // namespace roq
