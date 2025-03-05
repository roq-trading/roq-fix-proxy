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

#include "roq/proxy/fix/shared.hpp"

namespace roq {
namespace proxy {
namespace fix {
namespace client {

struct Session final : public io::net::tcp::Connection::Handler {
  struct Disconnected final {};
  struct Handler {
    virtual void operator()(Trace<Disconnected> const &, uint64_t session_id) = 0;
    // user
    virtual void operator()(Trace<roq::fix::codec::UserRequest> const &, uint64_t session_id) = 0;
    // security
    virtual void operator()(Trace<roq::fix::codec::SecurityListRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::SecurityDefinitionRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::SecurityStatusRequest> const &, uint64_t session_id) = 0;
    // market data
    virtual void operator()(Trace<roq::fix::codec::MarketDataRequest> const &, uint64_t session_id) = 0;
    // orders
    virtual void operator()(Trace<roq::fix::codec::OrderStatusRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::NewOrderSingle> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::OrderCancelReplaceRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::OrderCancelRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::OrderMassStatusRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::OrderMassCancelRequest> const &, uint64_t session_id) = 0;
    // positions
    virtual void operator()(Trace<roq::fix::codec::RequestForPositions> const &, uint64_t session_id) = 0;
    // trades
    virtual void operator()(Trace<roq::fix::codec::TradeCaptureReportRequest> const &, uint64_t session_id) = 0;
    // quotes
    virtual void operator()(Trace<roq::fix::codec::MassQuote> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<roq::fix::codec::QuoteCancel> const &, uint64_t session_id) = 0;
  };

  Session(Handler &, uint64_t session_id, io::net::tcp::Connection::Factory &, Shared &);

  bool ready() const;

  void force_disconnect();

  void operator()(Event<Stop> const &);
  void operator()(Event<Timer> const &);

  void operator()(Trace<roq::fix::codec::BusinessMessageReject> const &);
  // user
  void operator()(Trace<roq::fix::codec::UserResponse> const &);
  // security
  void operator()(Trace<roq::fix::codec::SecurityList> const &);
  void operator()(Trace<roq::fix::codec::SecurityDefinition> const &);
  void operator()(Trace<roq::fix::codec::SecurityStatus> const &);
  // market data
  void operator()(Trace<roq::fix::codec::MarketDataRequestReject> const &);
  void operator()(Trace<roq::fix::codec::MarketDataSnapshotFullRefresh> const &);
  void operator()(Trace<roq::fix::codec::MarketDataIncrementalRefresh> const &);
  // orders
  void operator()(Trace<roq::fix::codec::OrderCancelReject> const &);
  void operator()(Trace<roq::fix::codec::OrderMassCancelReport> const &);
  void operator()(Trace<roq::fix::codec::ExecutionReport> const &);
  // positions
  void operator()(Trace<roq::fix::codec::RequestForPositionsAck> const &);
  void operator()(Trace<roq::fix::codec::PositionReport> const &);
  // trades
  void operator()(Trace<roq::fix::codec::TradeCaptureReportRequestAck> const &);
  void operator()(Trace<roq::fix::codec::TradeCaptureReport> const &);
  // quotes
  void operator()(Trace<roq::fix::codec::MassQuoteAck> const &);
  void operator()(Trace<roq::fix::codec::QuoteStatusReport> const &);

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
  void send_and_close(T const &);
  template <std::size_t level, typename T>
  void send(T const &);
  template <std::size_t level, typename T>
  void send(T const &, std::chrono::nanoseconds sending_time);

  // - receive
  void check(roq::fix::Header const &);

  void parse(Trace<roq::fix::Message> const &);

  template <typename T, typename... Args>
  void dispatch(Trace<roq::fix::Message> const &, Args &&...);

  void operator()(Trace<roq::fix::codec::TestRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::ResendRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::Reject> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::Heartbeat> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::Logon> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::Logout> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::TradingSessionStatusRequest> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::SecurityListRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::SecurityDefinitionRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::SecurityStatusRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::MarketDataRequest> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::OrderStatusRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::OrderMassStatusRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::NewOrderSingle> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::OrderCancelRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::OrderCancelReplaceRequest> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::OrderMassCancelRequest> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::TradeCaptureReportRequest> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::RequestForPositions> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::MassQuote> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::QuoteCancel> const &, roq::fix::Header const &);

  void send_reject_and_close(roq::fix::Header const &, roq::fix::SessionRejectReason, std::string_view const &text);

  void send_business_message_reject(roq::fix::Header const &, std::string_view const &ref_id, roq::fix::BusinessRejectReason, std::string_view const &text);

  template <typename T, typename Callback>
  bool add_party_ids(Trace<T> const &, Callback) const;

 private:
  Handler &handler_;
  uint64_t const session_id_;
  std::unique_ptr<io::net::tcp::Connection> connection_;
  Shared &shared_;
  io::Buffer buffer_;
  std::chrono::nanoseconds const logon_timeout_;
  State state_ = {};
  struct {
    uint64_t msg_seq_num = {};
  } outbound_;
  struct {
    uint64_t msg_seq_num = {};
  } inbound_;
  std::string comp_id_;
  std::string username_;
  std::chrono::nanoseconds user_response_timeout_ = {};
  std::string party_id_;
  std::chrono::nanoseconds next_heartbeat_ = {};
  bool waiting_for_heartbeat_ = {};
  // buffer
  std::vector<std::byte> decode_buffer_;
  std::vector<std::byte> decode_buffer_2_;
};

}  // namespace client
}  // namespace fix
}  // namespace proxy
}  // namespace roq
