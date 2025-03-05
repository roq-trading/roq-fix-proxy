/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "roq/api.hpp"

#include "roq/io/context.hpp"

#include "roq/io/web/uri.hpp"

#include "roq/io/net/connection_factory.hpp"
#include "roq/io/net/connection_manager.hpp"

#include "roq/fix/message.hpp"

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
#include "roq/fix/codec/user_request.hpp"
#include "roq/fix/codec/user_response.hpp"

#include "roq/proxy/fix/settings.hpp"

namespace roq {
namespace proxy {
namespace fix {
namespace server {

struct Session final : public io::net::ConnectionManager::Handler {
  struct Ready final {};
  struct Disconnected final {};
  struct Handler {
    virtual void operator()(Trace<Ready> const &) = 0;
    virtual void operator()(Trace<Disconnected> const &) = 0;
    //
    virtual void operator()(Trace<roq::fix::codec::BusinessMessageReject> const &) = 0;
    // user
    virtual void operator()(Trace<roq::fix::codec::UserResponse> const &) = 0;
    // security
    virtual void operator()(Trace<roq::fix::codec::SecurityList> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::SecurityDefinition> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::SecurityStatus> const &) = 0;
    // market data
    virtual void operator()(Trace<roq::fix::codec::MarketDataRequestReject> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::MarketDataSnapshotFullRefresh> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::MarketDataIncrementalRefresh> const &) = 0;
    // orders
    virtual void operator()(Trace<roq::fix::codec::OrderCancelReject> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::OrderMassCancelReport> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::ExecutionReport> const &) = 0;
    // positions
    virtual void operator()(Trace<roq::fix::codec::RequestForPositionsAck> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::PositionReport> const &) = 0;
    // trades
    virtual void operator()(Trace<roq::fix::codec::TradeCaptureReportRequestAck> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::TradeCaptureReport> const &) = 0;
    // quotes
    virtual void operator()(Trace<roq::fix::codec::MassQuoteAck> const &) = 0;
    virtual void operator()(Trace<roq::fix::codec::QuoteStatusReport> const &) = 0;
  };

  Session(Handler &, Settings const &, io::Context &, io::web::URI const &);

  void operator()(Event<Start> const &);
  void operator()(Event<Stop> const &);
  void operator()(Event<Timer> const &);

  bool ready() const;

  // user
  void operator()(Trace<roq::fix::codec::UserRequest> const &);
  // ssecurity
  void operator()(Trace<roq::fix::codec::SecurityListRequest> const &);
  void operator()(Trace<roq::fix::codec::SecurityDefinitionRequest> const &);
  void operator()(Trace<roq::fix::codec::SecurityStatusRequest> const &);
  // market data
  void operator()(Trace<roq::fix::codec::MarketDataRequest> const &);
  // orders
  void operator()(Trace<roq::fix::codec::OrderStatusRequest> const &);
  void operator()(Trace<roq::fix::codec::NewOrderSingle> const &);
  void operator()(Trace<roq::fix::codec::OrderCancelReplaceRequest> const &);
  void operator()(Trace<roq::fix::codec::OrderCancelRequest> const &);
  void operator()(Trace<roq::fix::codec::OrderMassStatusRequest> const &);
  void operator()(Trace<roq::fix::codec::OrderMassCancelRequest> const &);
  // positions
  void operator()(Trace<roq::fix::codec::RequestForPositions> const &);
  // trades
  void operator()(Trace<roq::fix::codec::TradeCaptureReportRequest> const &);
  // quotes
  void operator()(Trace<roq::fix::codec::MassQuote> const &);
  void operator()(Trace<roq::fix::codec::QuoteCancel> const &);

 private:
  enum class State;

 protected:
  void operator()(State);

  // io::net::ConnectionManager::Handler
  void operator()(io::net::ConnectionManager::Connected const &) override;
  void operator()(io::net::ConnectionManager::Disconnected const &) override;
  void operator()(io::net::ConnectionManager::Read const &) override;
  void operator()(io::net::ConnectionManager::Write const &) override;

  // inbound

  void check(roq::fix::Header const &);

  void parse(Trace<roq::fix::Message> const &);

  template <typename T>
  void dispatch(Trace<roq::fix::Message> const &, T const &);

  // - session

  void operator()(Trace<roq::fix::codec::Reject> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::ResendRequest> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::Logon> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::Logout> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::Heartbeat> const &, roq::fix::Header const &);

  void operator()(Trace<roq::fix::codec::TestRequest> const &, roq::fix::Header const &);

  // - business

  void operator()(Trace<roq::fix::codec::BusinessMessageReject> const &, roq::fix::Header const &);

  // - security

  void operator()(Trace<roq::fix::codec::SecurityList> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::SecurityDefinition> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::SecurityStatus> const &, roq::fix::Header const &);

  // - market data

  void operator()(Trace<roq::fix::codec::MarketDataRequestReject> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::MarketDataSnapshotFullRefresh> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::MarketDataIncrementalRefresh> const &, roq::fix::Header const &);

  // - user

  void operator()(Trace<roq::fix::codec::UserResponse> const &, roq::fix::Header const &);

  // - orders

  void operator()(Trace<roq::fix::codec::OrderCancelReject> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::OrderMassCancelReport> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::ExecutionReport> const &, roq::fix::Header const &);

  // - positions

  void operator()(Trace<roq::fix::codec::RequestForPositionsAck> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::PositionReport> const &, roq::fix::Header const &);

  // - trades

  void operator()(Trace<roq::fix::codec::TradeCaptureReportRequestAck> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::TradeCaptureReport> const &, roq::fix::Header const &);

  // - quotes

  void operator()(Trace<roq::fix::codec::MassQuoteAck> const &, roq::fix::Header const &);
  void operator()(Trace<roq::fix::codec::QuoteStatusReport> const &, roq::fix::Header const &);

  // outbound

  template <typename T>
  void send(T const &value);

  template <typename T>
  void send_helper(T const &value);

  void send_logon();
  void send_logout(std::string_view const &text);
  void send_heartbeat(std::string_view const &test_req_id);
  void send_test_request(std::chrono::nanoseconds now);

 private:
  Handler &handler_;
  // config
  std::string_view const username_;
  std::string_view const password_;
  std::string_view const sender_comp_id_;
  std::string_view const target_comp_id_;
  std::chrono::nanoseconds const ping_freq_;
  bool const debug_;
  // connection
  std::unique_ptr<io::net::ConnectionFactory> const connection_factory_;
  std::unique_ptr<io::net::ConnectionManager> const connection_manager_;
  // messaging
  struct {
    uint64_t msg_seq_num = {};
  } inbound_;
  struct {
    uint64_t msg_seq_num = {};
  } outbound_;
  std::vector<std::byte> decode_buffer_;
  std::vector<std::byte> decode_buffer_2_;
  // state
  enum class State {
    DISCONNECTED,
    LOGON_SENT,
    READY,
  } state_ = {};
  std::chrono::nanoseconds next_heartbeat_ = {};
};

}  // namespace server
}  // namespace fix
}  // namespace proxy
}  // namespace roq
