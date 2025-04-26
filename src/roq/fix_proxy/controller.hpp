/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "roq/utils/container.hpp"

#include "roq/io/context.hpp"

#include "roq/io/sys/signal.hpp"
#include "roq/io/sys/timer.hpp"

#include "roq/fix/proxy/manager.hpp"

#include "roq/fix_proxy/config.hpp"
#include "roq/fix_proxy/settings.hpp"
#include "roq/fix_proxy/shared.hpp"

#include "roq/fix_proxy/tools/crypto.hpp"

#include "roq/fix_proxy/auth/session.hpp"

#include "roq/fix_proxy/server/session.hpp"

#include "roq/fix_proxy/client/manager.hpp"
#include "roq/fix_proxy/client/session.hpp"

namespace roq {
namespace fix_proxy {

struct Controller final : public io::sys::Signal::Handler,
                          public io::sys::Timer::Handler,
                          public fix::proxy::Manager::Handler,
                          public auth::Session::Handler,
                          public server::Session::Handler {
  Controller(Settings const &, Config const &, io::Context &, std::span<std::string_view const> const &connections);

  Controller(Controller &&) = delete;
  Controller(Controller const &) = delete;

  void run();

 protected:
  bool ready() const { return ready_; }

  // io::sys::Signal::Handler
  void operator()(io::sys::Signal::Event const &) override;

  // io::sys::Timer::Handler
  void operator()(io::sys::Timer::Event const &) override;

  // fix::proxy::Manager::Handler

  // authentication:
  std::pair<fix::codec::Error, uint32_t> operator()(fix::proxy::Manager::Credentials const &, uint64_t session_id) override;

  // server:
  // - connection
  void operator()(Trace<fix::proxy::Manager::Disconnected> const &) override;
  void operator()(Trace<fix::proxy::Manager::Ready> const &) override;
  // - manager => server
  void operator()(Trace<fix::codec::Reject> const &) override;
  void operator()(Trace<fix::codec::Logon> const &) override;
  void operator()(Trace<fix::codec::Logout> const &) override;
  void operator()(Trace<fix::codec::Heartbeat> const &) override;
  void operator()(Trace<fix::codec::TestRequest> const &) override;
  // - client => server
  void operator()(Trace<fix::codec::BusinessMessageReject> const &) override;
  void operator()(Trace<fix::codec::UserRequest> const &) override;
  void operator()(Trace<fix::codec::TradingSessionStatusRequest> const &) override;
  void operator()(Trace<fix::codec::SecurityListRequest> const &) override;
  void operator()(Trace<fix::codec::SecurityDefinitionRequest> const &) override;
  void operator()(Trace<fix::codec::SecurityStatusRequest> const &) override;
  void operator()(Trace<fix::codec::MarketDataRequest> const &) override;
  void operator()(Trace<fix::codec::NewOrderSingle> const &) override;
  void operator()(Trace<fix::codec::OrderCancelReplaceRequest> const &) override;
  void operator()(Trace<fix::codec::OrderCancelRequest> const &) override;
  void operator()(Trace<fix::codec::OrderMassCancelRequest> const &) override;
  void operator()(Trace<fix::codec::OrderStatusRequest> const &) override;
  void operator()(Trace<fix::codec::OrderMassStatusRequest> const &) override;
  void operator()(Trace<fix::codec::TradeCaptureReportRequest> const &) override;
  void operator()(Trace<fix::codec::RequestForPositions> const &) override;
  void operator()(Trace<fix::codec::MassQuote> const &) override;
  void operator()(Trace<fix::codec::QuoteCancel> const &) override;

  // client:
  // - connection
  void operator()(Trace<fix::proxy::Manager::Disconnect> const &, uint64_t session_id) override;
  // - manager => client
  void operator()(Trace<fix::codec::Reject> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::Logon> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::Logout> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::Heartbeat> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::TestRequest> const &, uint64_t session_id) override;
  // - server => client
  void operator()(Trace<fix::codec::BusinessMessageReject> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::TradingSessionStatus> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::SecurityList> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::SecurityDefinition> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::SecurityStatus> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::MarketDataRequestReject> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::MarketDataSnapshotFullRefresh> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::MarketDataIncrementalRefresh> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::ExecutionReport> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::OrderCancelReject> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::OrderMassCancelReport> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::TradeCaptureReportRequestAck> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::TradeCaptureReport> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::RequestForPositionsAck> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::PositionReport> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::MassQuoteAck> const &, uint64_t session_id) override;
  void operator()(Trace<fix::codec::QuoteStatusReport> const &, uint64_t session_id) override;

  // auth::Session::Handler
  void operator()(auth::Session::Insert const &) override;
  void operator()(auth::Session::Remove const &) override;

  // server::Session::Handler
  void operator()(Trace<server::Session::Ready> const &) override;
  void operator()(Trace<server::Session::Disconnected> const &) override;

  // utilities

  template <typename... Args>
  void dispatch(Args &&...);

  template <typename T>
  void dispatch_to_server(Trace<T> const &);

  template <typename T>
  bool dispatch_to_client(Trace<T> const &, uint64_t session_id);

 private:
  utils::unordered_map<std::string, std::tuple<std::string, uint32_t, std::string>> const username_to_password_and_strategy_id_and_component_id_;
  tools::Crypto crypto_;
  io::Context &context_;
  std::unique_ptr<io::sys::Signal> const terminate_;
  std::unique_ptr<io::sys::Signal> const interrupt_;
  std::unique_ptr<io::sys::Timer> const timer_;
  std::unique_ptr<fix::proxy::Manager> proxy_;
  Shared shared_;
  std::unique_ptr<auth::Session> auth_session_;
  server::Session server_session_;
  client::Manager client_manager_;
  bool ready_ = {};
};

}  // namespace fix_proxy
}  // namespace roq
