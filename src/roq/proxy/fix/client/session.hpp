/* Copyright (c) 2017-2024, Hans Erik Thrane */

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

#include "roq/codec/fix/business_message_reject.hpp"
#include "roq/codec/fix/execution_report.hpp"
#include "roq/codec/fix/heartbeat.hpp"
#include "roq/codec/fix/logon.hpp"
#include "roq/codec/fix/logout.hpp"
#include "roq/codec/fix/market_data_incremental_refresh.hpp"
#include "roq/codec/fix/market_data_request.hpp"
#include "roq/codec/fix/market_data_request_reject.hpp"
#include "roq/codec/fix/market_data_snapshot_full_refresh.hpp"
#include "roq/codec/fix/new_order_single.hpp"
#include "roq/codec/fix/order_cancel_reject.hpp"
#include "roq/codec/fix/order_cancel_replace_request.hpp"
#include "roq/codec/fix/order_cancel_request.hpp"
#include "roq/codec/fix/order_mass_cancel_report.hpp"
#include "roq/codec/fix/order_mass_cancel_request.hpp"
#include "roq/codec/fix/order_mass_status_request.hpp"
#include "roq/codec/fix/order_status_request.hpp"
#include "roq/codec/fix/position_report.hpp"
#include "roq/codec/fix/reject.hpp"
#include "roq/codec/fix/request_for_positions.hpp"
#include "roq/codec/fix/request_for_positions_ack.hpp"
#include "roq/codec/fix/resend_request.hpp"
#include "roq/codec/fix/security_definition.hpp"
#include "roq/codec/fix/security_definition_request.hpp"
#include "roq/codec/fix/security_list.hpp"
#include "roq/codec/fix/security_list_request.hpp"
#include "roq/codec/fix/security_status.hpp"
#include "roq/codec/fix/security_status_request.hpp"
#include "roq/codec/fix/test_request.hpp"
#include "roq/codec/fix/trade_capture_report.hpp"
#include "roq/codec/fix/trade_capture_report_request.hpp"
#include "roq/codec/fix/trade_capture_report_request_ack.hpp"
#include "roq/codec/fix/trading_session_status_request.hpp"
#include "roq/codec/fix/user_request.hpp"
#include "roq/codec/fix/user_response.hpp"

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
    virtual void operator()(Trace<codec::fix::UserRequest> const &, uint64_t session_id) = 0;
    // security
    virtual void operator()(Trace<codec::fix::SecurityListRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::SecurityDefinitionRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::SecurityStatusRequest> const &, uint64_t session_id) = 0;
    // market data
    virtual void operator()(Trace<codec::fix::MarketDataRequest> const &, uint64_t session_id) = 0;
    // orders
    virtual void operator()(Trace<codec::fix::OrderStatusRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::NewOrderSingle> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::OrderCancelReplaceRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::OrderCancelRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::OrderMassStatusRequest> const &, uint64_t session_id) = 0;
    virtual void operator()(Trace<codec::fix::OrderMassCancelRequest> const &, uint64_t session_id) = 0;
    // positions
    virtual void operator()(Trace<codec::fix::RequestForPositions> const &, uint64_t session_id) = 0;
    // trades
    virtual void operator()(Trace<codec::fix::TradeCaptureReportRequest> const &, uint64_t session_id) = 0;
  };

  Session(Handler &, uint64_t session_id, io::net::tcp::Connection::Factory &, Shared &);

  bool ready() const;

  void force_disconnect();

  void operator()(Event<Stop> const &);
  void operator()(Event<Timer> const &);

  void operator()(Trace<codec::fix::BusinessMessageReject> const &);
  // user
  void operator()(Trace<codec::fix::UserResponse> const &);
  // security
  void operator()(Trace<codec::fix::SecurityList> const &);
  void operator()(Trace<codec::fix::SecurityDefinition> const &);
  void operator()(Trace<codec::fix::SecurityStatus> const &);
  // market data
  void operator()(Trace<codec::fix::MarketDataRequestReject> const &);
  void operator()(Trace<codec::fix::MarketDataSnapshotFullRefresh> const &);
  void operator()(Trace<codec::fix::MarketDataIncrementalRefresh> const &);
  // orders
  void operator()(Trace<codec::fix::OrderCancelReject> const &);
  void operator()(Trace<codec::fix::OrderMassCancelReport> const &);
  void operator()(Trace<codec::fix::ExecutionReport> const &);
  // positions
  void operator()(Trace<codec::fix::RequestForPositionsAck> const &);
  void operator()(Trace<codec::fix::PositionReport> const &);
  // trades
  void operator()(Trace<codec::fix::TradeCaptureReportRequestAck> const &);
  void operator()(Trace<codec::fix::TradeCaptureReport> const &);

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

  void operator()(Trace<codec::fix::TestRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::ResendRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::Reject> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::Heartbeat> const &, roq::fix::Header const &);

  void operator()(Trace<codec::fix::Logon> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::Logout> const &, roq::fix::Header const &);

  void operator()(Trace<codec::fix::TradingSessionStatusRequest> const &, roq::fix::Header const &);

  void operator()(Trace<codec::fix::SecurityListRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::SecurityDefinitionRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::SecurityStatusRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::MarketDataRequest> const &, roq::fix::Header const &);

  void operator()(Trace<codec::fix::OrderStatusRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::OrderMassStatusRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::NewOrderSingle> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::OrderCancelRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::OrderCancelReplaceRequest> const &, roq::fix::Header const &);
  void operator()(Trace<codec::fix::OrderMassCancelRequest> const &, roq::fix::Header const &);

  void operator()(Trace<codec::fix::TradeCaptureReportRequest> const &, roq::fix::Header const &);

  void operator()(Trace<codec::fix::RequestForPositions> const &, roq::fix::Header const &);

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
  std::vector<std::byte> encode_buffer_;
};

}  // namespace client
}  // namespace fix
}  // namespace proxy
}  // namespace roq
