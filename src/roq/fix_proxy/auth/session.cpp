/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/auth/session.hpp"

#include <nlohmann/json.hpp>

#include "roq/web/socket/client.hpp"

#include "roq/logging.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {
namespace auth {

// === HELPERS ===

namespace {
auto create_connection(auto &handler, auto &settings, auto &context, auto &uri) {
  auto config = web::socket::Client::Config{
      // connection
      .interface = {},
      .uris = {&uri, 1},
      .host = {},
      .validate_certificate = settings.net.tls_validate_certificate,
      // connection manager
      .connection_timeout = settings.net.connection_timeout,
      .disconnect_on_idle_timeout = {},
      .always_reconnect = true,
      // proxy
      .proxy = {},
      // http
      .query = {},
      .user_agent = ROQ_PACKAGE_NAME,
      .request_timeout = {},
      .ping_frequency = settings.auth.ping_freq,
      // implementation
      .decode_buffer_size = settings.auth.decode_buffer_size,
      .encode_buffer_size = settings.auth.encode_buffer_size,
  };
  return web::socket::Client::create(handler, context, config, []() { return std::string(); });
}
}  // namespace

// === IMPLEMENTATION ===

Session::Session(Handler &handler, Settings const &settings, io::Context &context, io::web::URI const &uri)
    : handler_{handler}, connection_{create_connection(*this, settings, context, uri)} {
}

void Session::operator()(Event<Start> const &) {
  (*connection_).start();
}

void Session::operator()(Event<Stop> const &) {
  (*connection_).stop();
}

void Session::operator()(Event<Timer> const &event) {
  auto now = event.value.now;
  (*connection_).refresh(now);
}

// io::net::ConnectionManager::Handler

void Session::operator()(web::socket::Client::Connected const &) {
}

void Session::operator()(web::socket::Client::Disconnected const &) {
}

void Session::operator()(web::socket::Client::Ready const &) {
  auto request = R"({)"
                 R"("jsonrpc":"2.0",)"
                 R"("method":"subscribe",)"
                 R"("id":"test")"
                 R"(})"sv;
  (*connection_).send_text(request);
}

void Session::operator()(web::socket::Client::Close const &) {
}

void Session::operator()(web::socket::Client::Latency const &) {
}

void Session::operator()(web::socket::Client::Text const &text) {
  log::info(R"(DEBUG: text="{}")"sv, text.payload);
  auto json = nlohmann::json::parse(text.payload);
  auto result = json.at("result"sv);
  for (auto &obj : result) {
    auto action = obj.at("action"sv).template get<std::string_view>();
    auto component = obj.at("component"sv).template get<std::string_view>();
    auto username = obj.at("username"sv).template get<std::string_view>();
    if (action == "insert"sv) {
      auto password = obj.at("password"sv).template get<std::string_view>();
      auto strategy_id = obj.at("strategy_id"sv).template get<uint32_t>();
      log::info<1>(R"(action="{}", component="{}", username="{}", password="{}", strategy_id={})"sv, action, component, username, password, strategy_id);
      auto insert = Insert{
          .component = component,
          .username = username,
          .password = password,
          .strategy_id = strategy_id,
      };
      handler_(insert);
    } else if (action == "remove"sv) {
      log::info<1>(R"(action="{}", component="{}", username="{}")"sv, action, component, username);
      auto remove = Remove{
          .component = component,
          .username = username,
      };
      handler_(remove);
    } else {
      log::warn(R"(Unexpected: action="{}")"sv, action);
    }
  }
}

void Session::operator()(web::socket::Client::Binary const &) {
}

}  // namespace auth
}  // namespace fix_proxy
}  // namespace roq
