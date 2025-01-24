/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "roq/utils/container.hpp"

#include "roq/io/context.hpp"

#include "roq/io/sys/signal.hpp"
#include "roq/io/sys/timer.hpp"

#include "roq/proxy/fix/config.hpp"
#include "roq/proxy/fix/settings.hpp"
#include "roq/proxy/fix/shared.hpp"

#include "roq/proxy/fix/auth/session.hpp"

#include "roq/proxy/fix/server/session.hpp"

#include "roq/proxy/fix/client/manager.hpp"
#include "roq/proxy/fix/client/session.hpp"

namespace roq {
namespace proxy {
namespace fix {

struct Controller final : public io::sys::Signal::Handler,
                          public io::sys::Timer::Handler,
                          public auth::Session::Handler,
                          public server::Session::Handler,
                          public client::Session::Handler {
  Controller(Settings const &, Config const &, io::Context &, std::span<std::string_view const> const &connections);

  Controller(Controller const &) = delete;

  void run();

 protected:
  bool ready() const { return ready_; }

  // io::sys::Signal::Handler
  void operator()(io::sys::Signal::Event const &) override;

  // io::sys::Timer::Handler
  void operator()(io::sys::Timer::Event const &) override;

  // auth::Session::Handler
  void operator()(auth::Session::Insert const &) override;
  void operator()(auth::Session::Remove const &) override;

  // server::Session::Handler
  void operator()(Trace<server::Session::Ready> const &) override;
  void operator()(Trace<server::Session::Disconnected> const &) override;
  //
  void operator()(Trace<codec::fix::BusinessMessageReject> const &) override;
  // - user
  void operator()(Trace<codec::fix::UserResponse> const &) override;
  // - security
  void operator()(Trace<codec::fix::SecurityList> const &) override;
  void operator()(Trace<codec::fix::SecurityDefinition> const &) override;
  void operator()(Trace<codec::fix::SecurityStatus> const &) override;
  // - market data
  void operator()(Trace<codec::fix::MarketDataRequestReject> const &) override;
  void operator()(Trace<codec::fix::MarketDataSnapshotFullRefresh> const &) override;
  void operator()(Trace<codec::fix::MarketDataIncrementalRefresh> const &) override;
  // - orders
  void operator()(Trace<codec::fix::OrderCancelReject> const &) override;
  void operator()(Trace<codec::fix::OrderMassCancelReport> const &) override;
  void operator()(Trace<codec::fix::ExecutionReport> const &) override;
  // - positions
  void operator()(Trace<codec::fix::RequestForPositionsAck> const &) override;
  void operator()(Trace<codec::fix::PositionReport> const &) override;
  // - trades
  void operator()(Trace<codec::fix::TradeCaptureReportRequestAck> const &) override;
  void operator()(Trace<codec::fix::TradeCaptureReport> const &) override;
  // - quotes
  void operator()(Trace<codec::fix::MassQuoteAck> const &) override;
  void operator()(Trace<codec::fix::QuoteStatusReport> const &) override;

  // client::Session::Handler
  void operator()(Trace<client::Session::Disconnected> const &, uint64_t session_id) override;
  // - user
  void operator()(Trace<codec::fix::UserRequest> const &, uint64_t session_id) override;
  // - security
  void operator()(Trace<codec::fix::SecurityListRequest> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::SecurityDefinitionRequest> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::SecurityStatusRequest> const &, uint64_t session_id) override;
  // - market data
  void operator()(Trace<codec::fix::MarketDataRequest> const &, uint64_t session_id) override;
  // - orders
  void operator()(Trace<codec::fix::OrderStatusRequest> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::NewOrderSingle> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::OrderCancelReplaceRequest> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::OrderCancelRequest> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::OrderMassStatusRequest> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::OrderMassCancelRequest> const &, uint64_t session_id) override;
  // - positions
  void operator()(Trace<codec::fix::RequestForPositions> const &, uint64_t session_id) override;
  // - trades
  void operator()(Trace<codec::fix::TradeCaptureReportRequest> const &, uint64_t session_id) override;
  // - quotes
  void operator()(Trace<codec::fix::MassQuote> const &, uint64_t session_id) override;
  void operator()(Trace<codec::fix::QuoteCancel> const &, uint64_t session_id) override;

  // utilities

  template <typename... Args>
  void dispatch(Args &&...);

  template <typename T>
  void dispatch_to_server(Trace<T> const &);

  template <typename T>
  bool dispatch_to_client(Trace<T> const &, uint64_t session_id);

  template <typename T>
  void broadcast(Trace<T> const &, std::string_view const &client_id);

  template <typename Callback>
  bool find_req_id(auto &mapping, std::string_view const &req_id, Callback callback);

  void add_req_id(auto &mapping, std::string_view const &req_id_client, std::string_view const &req_id_server, uint64_t session_id, bool keep_alive);

  bool remove_req_id(auto &mapping, std::string_view const &req_id);

  template <typename Callback>
  void clear_req_ids(auto &mapping, uint64_t session_id, Callback);

  inline void clear_req_ids(auto &mapping, uint64_t session_id) {
    clear_req_ids(mapping, session_id, []([[maybe_unused]] auto &req_id) {});
  }

  void ensure_cl_ord_id(std::string_view const &cl_ord_id, roq::fix::OrdStatus);
  void remove_cl_ord_id(std::string_view const &cl_ord_id);

  void user_add(std::string_view const &username, uint64_t session_id);
  void user_remove(std::string_view const &username, bool ready);
  bool user_is_locked(std::string_view const &username) const;

 private:
  io::Context &context_;
  std::unique_ptr<io::sys::Signal> const terminate_;
  std::unique_ptr<io::sys::Signal> const interrupt_;
  std::unique_ptr<io::sys::Timer> const timer_;
  Shared shared_;
  std::unique_ptr<auth::Session> auth_session_;
  server::Session server_session_;
  client::Manager client_manager_;
  bool ready_ = {};
  // req_id mappings
  struct {
    struct {
      utils::unordered_map<std::string, uint64_t> client_to_session;
      utils::unordered_map<uint64_t, std::string> session_to_client;
      // user_request_id => session_id
      utils::unordered_map<std::string, uint64_t> server_to_client;
      // session_id => user_request_id
      utils::unordered_map<uint64_t, std::string> client_to_server;
    } user;
    struct Mapping final {
      // req_id(server) => {session_id, req_id(client), keep_alive}
      utils::unordered_map<std::string, std::tuple<uint64_t, std::string, bool>> server_to_client;
      // session_id => req_id(client) => req_id(server)
      utils::unordered_map<uint64_t, utils::unordered_map<std::string, std::string>> client_to_server;
    };
    Mapping security_req_id;
    Mapping security_status_req_id;
    Mapping trad_ses_req_id;
    Mapping md_req_id;
    Mapping ord_status_req_id;
    Mapping mass_status_req_id;
    Mapping pos_req_id;
    Mapping trade_request_id;
    Mapping cl_ord_id;
    Mapping mass_cancel_cl_ord_id;
  } subscriptions_;
  struct {
    // cl_ord_id(server) => order status
    utils::unordered_map<std::string, roq::fix::OrdStatus> state;
  } cl_ord_id_;
  // WORK-AROUND
  uint32_t total_num_pos_reports_ = {};
};

}  // namespace fix
}  // namespace proxy
}  // namespace roq
