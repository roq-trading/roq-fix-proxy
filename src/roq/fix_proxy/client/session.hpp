/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "roq/event.hpp"
#include "roq/stop.hpp"
#include "roq/timer.hpp"
#include "roq/trace.hpp"

#include "roq/io/buffer.hpp"

#include "roq/io/net/tcp/connection.hpp"

#include "roq/fix/codec/error.hpp"

#include "roq/fix/codec/business_message_reject.hpp"
#include "roq/fix/codec/execution_report.hpp"
#include "roq/fix/codec/heartbeat.hpp"
#include "roq/fix/codec/logon.hpp"
#include "roq/fix/codec/logout.hpp"
#include "roq/fix/codec/market_data_incremental_refresh.hpp"
#include "roq/fix/codec/market_data_request.hpp"
#include "roq/fix/codec/market_data_request_reject.hpp"
#include "roq/fix/codec/market_data_snapshot_full_refresh.hpp"
#include "roq/fix/codec/mass_quote.hpp"
#include "roq/fix/codec/mass_quote_ack.hpp"
#include "roq/fix/codec/new_order_single.hpp"
#include "roq/fix/codec/order_cancel_reject.hpp"
#include "roq/fix/codec/order_cancel_replace_request.hpp"
#include "roq/fix/codec/order_cancel_request.hpp"
#include "roq/fix/codec/order_mass_cancel_report.hpp"
#include "roq/fix/codec/order_mass_cancel_request.hpp"
#include "roq/fix/codec/order_mass_status_request.hpp"
#include "roq/fix/codec/order_status_request.hpp"
#include "roq/fix/codec/position_report.hpp"
#include "roq/fix/codec/quote_cancel.hpp"
#include "roq/fix/codec/quote_status_report.hpp"
#include "roq/fix/codec/reject.hpp"
#include "roq/fix/codec/request_for_positions.hpp"
#include "roq/fix/codec/request_for_positions_ack.hpp"
#include "roq/fix/codec/resend_request.hpp"
#include "roq/fix/codec/security_definition.hpp"
#include "roq/fix/codec/security_definition_request.hpp"
#include "roq/fix/codec/security_list.hpp"
#include "roq/fix/codec/security_list_request.hpp"
#include "roq/fix/codec/security_status.hpp"
#include "roq/fix/codec/security_status_request.hpp"
#include "roq/fix/codec/test_request.hpp"
#include "roq/fix/codec/trade_capture_report.hpp"
#include "roq/fix/codec/trade_capture_report_request.hpp"
#include "roq/fix/codec/trade_capture_report_request_ack.hpp"
#include "roq/fix/codec/trading_session_status_request.hpp"
#include "roq/fix/codec/user_request.hpp"
#include "roq/fix/codec/user_response.hpp"

#include "roq/fix_proxy/shared.hpp"

namespace roq {
namespace fix_proxy {
namespace client {

struct Session final : public io::net::tcp::Connection::Handler {
  struct Disconnected final {};
  struct Handler {
    virtual void operator()(Trace<Disconnected> const &, uint64_t session_id) = 0;
    // user
    virtual void operator()(Trace<fix::codec::UserRequest> const &, uint64_t session_id) = 0;
    // security
    virtual void operator()(Trace<fix::codec::SecurityListRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::SecurityDefinitionRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::SecurityStatusRequest> const &, uint64_t session_id) = 0;
    // market data
    virtual void operator()(Trace<fix::codec::MarketDataRequest> const &, uint64_t session_id) = 0;
    // orders
    virtual void operator()(Trace<fix::codec::OrderStatusRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::NewOrderSingle> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::OrderCancelReplaceRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::OrderCancelRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::OrderMassStatusRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::OrderMassCancelRequest> const &, uint64_t session_id) = 0;
    // positions
    virtual void operator()(Trace<fix::codec::RequestForPositions> const &, uint64_t session_id) = 0;
    // trades
    virtual void operator()(Trace<fix::codec::TradeCaptureReportRequest> const &, uint64_t session_id) = 0;
    // quotes
    virtual void operator()(Trace<fix::codec::MassQuote> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<fix::codec::QuoteCancel> const &, uint64_t session_id) = 0;
  };

  Session(Handler &, io::net::tcp::Connection::Factory &, Shared &, uint64_t session_id);

  bool ready() const;

  void force_disconnect();

  void operator()(Event<Stop> const &);
  void operator()(Event<Timer> const &);

  void operator()(Trace<fix::codec::BusinessMessageReject> const &);
  // user
  void operator()(Trace<fix::codec::UserResponse> const &);
  // security
  void operator()(Trace<fix::codec::SecurityList> const &);
  void operator()(Trace<fix::codec::SecurityDefinition> const &);
  void operator()(Trace<fix::codec::SecurityStatus> const &);
  // market data
  void operator()(Trace<fix::codec::MarketDataRequestReject> const &);
  void operator()(Trace<fix::codec::MarketDataSnapshotFullRefresh> const &);
  void operator()(Trace<fix::codec::MarketDataIncrementalRefresh> const &);
  // orders
  void operator()(Trace<fix::codec::OrderCancelReject> const &);
  void operator()(Trace<fix::codec::OrderMassCancelReport> const &);
  void operator()(Trace<fix::codec::ExecutionReport> const &);
  // positions
  void operator()(Trace<fix::codec::RequestForPositionsAck> const &);
  void operator()(Trace<fix::codec::PositionReport> const &);
  // trades
  void operator()(Trace<fix::codec::TradeCaptureReportRequestAck> const &);
  void operator()(Trace<fix::codec::TradeCaptureReport> const &);
  // quotes
  void operator()(Trace<fix::codec::MassQuoteAck> const &);
  void operator()(Trace<fix::codec::QuoteStatusReport> const &);

 protected:
  enum class State {
    WAITING_LOGON,
    WAITING_CREATE_ROUTE,
    READY,
    WAITING_REMOVE_ROUTE,
    ZOMBIE,
  };

  void operator()(State);

  bool zombie() const;

  void close();

  // io::net::tcp::Connection::Handler

  void operator()(io::net::tcp::Connection::Read const &) override;
  void operator()(io::net::tcp::Connection::Disconnected const &) override;

  // utilities

  void make_zombie();

  // - send
  template <std::size_t level, typename T>
  void send_helper(Trace<T> const &);
  template <std::size_t level, typename T>
  void send_and_close(T const &);
  template <std::size_t level, typename T>
  void send(T const &);
  template <std::size_t level, typename T>
  void send(T const &, std::chrono::nanoseconds sending_time);

  // - receive
  void check(fix::Header const &);

  void parse(Trace<fix::Message> const &);

  template <typename T, typename... Args>
  void dispatch(Trace<fix::Message> const &, Args &&...);

  void operator()(Trace<fix::codec::TestRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::ResendRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::Reject> const &, fix::Header const &);
  void operator()(Trace<fix::codec::Heartbeat> const &, fix::Header const &);

  void operator()(Trace<fix::codec::Logon> const &, fix::Header const &);
  void operator()(Trace<fix::codec::Logout> const &, fix::Header const &);

  void operator()(Trace<fix::codec::TradingSessionStatusRequest> const &, fix::Header const &);

  void operator()(Trace<fix::codec::SecurityListRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::SecurityDefinitionRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::SecurityStatusRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::MarketDataRequest> const &, fix::Header const &);

  void operator()(Trace<fix::codec::OrderStatusRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::OrderMassStatusRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::NewOrderSingle> const &, fix::Header const &);
  void operator()(Trace<fix::codec::OrderCancelRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::OrderCancelReplaceRequest> const &, fix::Header const &);
  void operator()(Trace<fix::codec::OrderMassCancelRequest> const &, fix::Header const &);

  void operator()(Trace<fix::codec::TradeCaptureReportRequest> const &, fix::Header const &);

  void operator()(Trace<fix::codec::RequestForPositions> const &, fix::Header const &);

  void operator()(Trace<fix::codec::MassQuote> const &, fix::Header const &);
  void operator()(Trace<fix::codec::QuoteCancel> const &, fix::Header const &);

  void send_reject_and_close(fix::Header const &, fix::SessionRejectReason, fix::codec::Error);

  void send_business_message_reject(fix::Header const &, std::string_view const &ref_id, fix::BusinessRejectReason, fix::codec::Error);

  template <typename T, typename Callback>
  bool add_party_ids(Trace<T> const &, Callback) const;

 private:
  Handler &handler_;
  std::unique_ptr<io::net::tcp::Connection> const connection_;
  Shared &shared_;
  uint64_t const session_id_;
  State state_ = {};
  std::chrono::nanoseconds last_update_ = {};
  struct {
    uint64_t msg_seq_num = {};
  } outbound_;
  struct {
    uint64_t msg_seq_num = {};
  } inbound_;
  std::string comp_id_;
  std::string username_;
  std::chrono::nanoseconds user_response_timeout_ = {};  // XXX FIXME TODO merge with last_update_
  std::string party_id_;
  bool waiting_for_heartbeat_ = {};
  // buffer
  io::Buffer buffer_;
  std::vector<std::byte> decode_buffer_;
  std::vector<std::byte> decode_buffer_2_;  // note! nested
};

}  // namespace client
}  // namespace fix_proxy
}  // namespace roq
