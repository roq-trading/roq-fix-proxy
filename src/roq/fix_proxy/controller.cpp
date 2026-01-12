/* Copyright (c) 2017-2026, Hans Erik Thrane */

#include "roq/fix_proxy/controller.hpp"

#include <fmt/format.h>

#include <magic_enum/magic_enum_format.hpp>

#include "roq/event.hpp"
#include "roq/timer.hpp"

#include "roq/exceptions.hpp"

#include "roq/utils/common.hpp"
#include "roq/utils/safe_cast.hpp"
#include "roq/utils/update.hpp"

#include "roq/fix/map.hpp"

#include "roq/fix/codec/error.hpp"

#include "roq/logging.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {

// === CONSTANTS ===

namespace {
auto const TIMER_FREQUENCY = 100ms;
}  // namespace

// === HELPERS ===

namespace {
template <typename R>
auto create_username_to_password_and_strategy_id_and_component_id(auto &config) {
  using result_type = std::remove_cvref_t<R>;
  result_type result;
  for (auto &[_, user] : config.users) {
    result.try_emplace(user.username, user.password, user.strategy_id, user.component);
  }
  return result;
}

auto create_proxy(auto &handler, auto &settings) {
  auto options = fix::proxy::Manager::Options{
      .server{
          .username = settings.server.username,
          .password = settings.server.password,
          .ping_freq = utils::safe_cast(settings.server.ping_freq),
          .request_timeout = settings.server.request_timeout,
      },
      .client{
          .auth_method = settings.client.auth_method,
          .auth_timestamp_tolerance = settings.client.auth_timestamp_tolerance,
          .logon_heartbeat_min = utils::safe_cast(settings.client.logon_heartbeat_min),
          .logon_heartbeat_max = utils::safe_cast(settings.client.logon_heartbeat_max),
          .request_timeout = settings.client.request_timeout,
          .heartbeat_freq = utils::safe_cast(settings.client.heartbeat_freq),
      },
      .test{
          .disable_remove_cl_ord_id = settings.test.disable_remove_cl_ord_id,
      }};
  return fix::proxy::Manager::create(handler, options);
}

auto create_auth_session(auto &handler, auto &settings, auto &context) -> std::unique_ptr<auth::Session> {
  if (std::empty(settings.auth.uri)) {
    return {};
  }
  io::web::URI uri{settings.auth.uri};
  return std::make_unique<auth::Session>(handler, settings, context, uri);
}

auto create_server_session(auto &handler, auto &settings, auto &context, auto &connections, auto &proxy) {
  if (std::size(connections) != 1) {
    log::fatal("Unexpected: only supporting a single upstream fix-bridge"sv);
  }
  auto &connection = connections[0];
  auto uri = io::web::URI{connection};
  return server::Session{handler, settings, context, uri, proxy};
}
}  // namespace

// === IMPLEMENTATION ===

Controller::Controller(Settings const &settings, Config const &config, io::Context &context, std::span<std::string_view const> const &connections)
    : username_to_password_and_strategy_id_and_component_id_{create_username_to_password_and_strategy_id_and_component_id<
          decltype(username_to_password_and_strategy_id_and_component_id_)>(config)},
      crypto_{settings.client.auth_method, settings.client.auth_timestamp_tolerance}, context_{context},
      terminate_{context.create_signal(*this, io::sys::Signal::Type::TERMINATE)}, interrupt_{context.create_signal(*this, io::sys::Signal::Type::INTERRUPT)},
      timer_{context.create_timer(*this, TIMER_FREQUENCY)}, proxy_{create_proxy(*this, settings)}, shared_{settings, *proxy_},
      auth_session_{create_auth_session(*this, settings, context)}, server_session_{create_server_session(*this, settings, context, connections, *proxy_)},
      client_manager_{settings, context, shared_} {
}

void Controller::run() {
  log::info("Event loop is now running"sv);
  {
    MessageInfo message_info;
    Start start;
    create_event_and_dispatch(server_session_, message_info, start);
  }
  (*timer_).resume();
  context_.dispatch();
  {
    MessageInfo message_info;
    Stop stop;
    create_event_and_dispatch(server_session_, message_info, stop);
  }
  log::info("Event loop has terminated"sv);
}

// io::sys::Signal::Handler

void Controller::operator()(io::sys::Signal::Event const &event) {
  log::warn("*** SIGNAL: {} ***"sv, event.type);
  context_.stop();
}

// io::sys::Timer::Handler

void Controller::operator()(io::sys::Timer::Event const &event) {
  auto timer = Timer{
      .now = event.now,
  };
  // (*proxy_)(event);
  dispatch(timer);
}

// fix::proxy::Manager::Handler

// authentication:

std::pair<fix::codec::Error, uint32_t> Controller::operator()(fix::proxy::Manager::Credentials const &credentials, [[maybe_unused]] uint64_t session_id) {
  auto iter_1 = username_to_password_and_strategy_id_and_component_id_.find(credentials.username);
  if (iter_1 == std::end(username_to_password_and_strategy_id_and_component_id_)) {
    log::warn("Invalid: username"sv);
    return {fix::codec::Error::INVALID_USERNAME, {}};
  }
  auto &[secret, strategy_id, component] = (*iter_1).second;
  if (credentials.component != component) {
    log::warn("Invalid: component"sv);
    return {fix::codec::Error::INVALID_COMPONENT, {}};
  }
  if (!crypto_.validate(credentials.password, secret, credentials.raw_data)) {
    log::warn("Invalid: password"sv);
    return {fix::codec::Error::INVALID_PASSWORD, {}};
  }
  return {{}, strategy_id};
}

// server:

// - connection

void Controller::operator()(Trace<fix::proxy::Manager::Disconnected> const &) {
}

void Controller::operator()(Trace<fix::proxy::Manager::Ready> const &) {
}

// - manager => server

void Controller::operator()(Trace<fix::codec::Reject> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::Logon> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::Logout> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::Heartbeat> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::TestRequest> const &event) {
  dispatch_to_server(event);
}

// - client => server

void Controller::operator()(Trace<fix::codec::BusinessMessageReject> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::UserRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::TradingSessionStatusRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::SecurityListRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::SecurityDefinitionRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::SecurityStatusRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::MarketDataRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::NewOrderSingle> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::OrderCancelReplaceRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::OrderCancelRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::OrderMassCancelRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::OrderStatusRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::OrderMassStatusRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::TradeCaptureReportRequest> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::RequestForPositions> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::MassQuote> const &event) {
  dispatch_to_server(event);
}

void Controller::operator()(Trace<fix::codec::QuoteCancel> const &event) {
  dispatch_to_server(event);
}

// client:

// - connection

void Controller::operator()(Trace<fix::proxy::Manager::Disconnect> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

// - manager => client

void Controller::operator()(Trace<fix::codec::Reject> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::Logon> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::Logout> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::Heartbeat> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::TestRequest> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

// - server => client

void Controller::operator()(Trace<fix::codec::BusinessMessageReject> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::TradingSessionStatus> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::SecurityList> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::SecurityDefinition> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::SecurityStatus> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::MarketDataRequestReject> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::MarketDataSnapshotFullRefresh> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::MarketDataIncrementalRefresh> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::ExecutionReport> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::OrderCancelReject> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::OrderMassCancelReport> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::TradeCaptureReportRequestAck> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::TradeCaptureReport> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::RequestForPositionsAck> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::PositionReport> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::MassQuoteAck> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

void Controller::operator()(Trace<fix::codec::QuoteStatusReport> const &event, uint64_t session_id) {
  dispatch_to_client(event, session_id);
}

// auth::Session::Handler

void Controller::operator()(auth::Session::Insert const &) {
  // shared_.add_user(insert.username, insert.password, insert.strategy_id, insert.component);
}

void Controller::operator()(auth::Session::Remove const &) {
  // shared_.remove_user(remove.username);
}

// server::Session::Handler

void Controller::operator()(Trace<server::Session::Ready> const &) {
  ready_ = true;
}

void Controller::operator()(Trace<server::Session::Disconnected> const &) {
  ready_ = false;
  client_manager_.get_all_sessions([&](auto &session) { session.force_disconnect(); });
}

// utilities

template <typename... Args>
void Controller::dispatch(Args &&...args) {
  MessageInfo message_info;
  Event event{message_info, std::forward<Args>(args)...};
  if (static_cast<bool>(auth_session_)) {
    (*auth_session_)(event);
  }
  server_session_(event);
  client_manager_(event);
}

template <typename T>
void Controller::dispatch_to_server(Trace<T> const &event) {
  server_session_(event);
}

template <typename T>
bool Controller::dispatch_to_client(Trace<T> const &event, uint64_t session_id) {
  auto success = false;
  client_manager_.find(session_id, [&](auto &session) {
    session(event);
    success = true;
  });
  if (!success) {
    log::warn<0>("Undeliverable: session_id={}"sv, session_id);
  }
  return success;
}

/*
template <typename T>
void Controller::broadcast(Trace<T> const &event, std::string_view const &client_id) {
  auto iter = subscriptions_.user.client_to_session.find(client_id);
  if (iter == std::end(subscriptions_.user.client_to_session))
    return;
  auto session_id = (*iter).second;
  client_manager_.find(session_id, [&](auto &session) { session(event); });
}
*/

}  // namespace fix_proxy
}  // namespace roq
