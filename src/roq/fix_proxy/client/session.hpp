/* Copyright (c) 2017-2026, Hans Erik Thrane */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "roq/trace.hpp"

#include "roq/io/buffer.hpp"

#include "roq/io/net/tcp/connection.hpp"

#include "roq/fix/proxy/manager.hpp"

#include "roq/fix_proxy/shared.hpp"

namespace roq {
namespace fix_proxy {
namespace client {

struct Session final : public io::net::tcp::Connection::Handler {
  Session(io::net::tcp::Connection::Factory &, uint64_t session_id, Shared &);

  Session(Session const &) = delete;

  void force_disconnect();

  // fix::proxy::Manager

  // - connection
  void operator()(Trace<fix::proxy::Manager::Disconnect> const &);

  // - manager => client
  void operator()(Trace<fix::codec::Reject> const &);
  void operator()(Trace<fix::codec::Logon> const &);
  void operator()(Trace<fix::codec::Logout> const &);
  void operator()(Trace<fix::codec::Heartbeat> const &);
  void operator()(Trace<fix::codec::TestRequest> const &);

  // - server => client
  void operator()(Trace<fix::codec::BusinessMessageReject> const &);
  void operator()(Trace<fix::codec::TradingSessionStatus> const &);
  void operator()(Trace<fix::codec::SecurityList> const &);
  void operator()(Trace<fix::codec::SecurityDefinition> const &);
  void operator()(Trace<fix::codec::SecurityStatus> const &);
  void operator()(Trace<fix::codec::MarketDataRequestReject> const &);
  void operator()(Trace<fix::codec::MarketDataSnapshotFullRefresh> const &);
  void operator()(Trace<fix::codec::MarketDataIncrementalRefresh> const &);
  void operator()(Trace<fix::codec::ExecutionReport> const &);
  void operator()(Trace<fix::codec::OrderCancelReject> const &);
  void operator()(Trace<fix::codec::OrderMassCancelReport> const &);
  void operator()(Trace<fix::codec::TradeCaptureReportRequestAck> const &);
  void operator()(Trace<fix::codec::TradeCaptureReport> const &);
  void operator()(Trace<fix::codec::RequestForPositionsAck> const &);
  void operator()(Trace<fix::codec::PositionReport> const &);
  void operator()(Trace<fix::codec::MassQuoteAck> const &);
  void operator()(Trace<fix::codec::QuoteStatusReport> const &);

 protected:
  // io::net::tcp::Connection::Handler

  void operator()(io::net::tcp::Connection::Read const &) override;
  void operator()(io::net::tcp::Connection::Disconnected const &) override;

  // outbound

  template <std::size_t level, typename T>
  void send_and_close(Trace<T> const &);

  template <std::size_t level, typename T>
  void send(Trace<T> const &);

  template <std::size_t level, typename T>
  void send(Trace<T> const &, std::chrono::nanoseconds sending_time);

  // inbound

  void parse(Trace<fix::Message> const &);

  template <typename T, typename... Args>
  void dispatch(Trace<fix::Message> const &, Args &&...);

  void check(fix::Header const &);

  // utils

  void close();

 private:
  std::unique_ptr<io::net::tcp::Connection> const connection_;
  uint64_t const session_id_;
  Shared &shared_;
  // messaging
  struct {
    uint64_t msg_seq_num = {};
  } outbound_;
  struct {
    uint64_t msg_seq_num = {};
  } inbound_;
  std::string comp_id_;
  io::Buffer buffer_;
  std::vector<std::byte> decode_buffer_;
  std::vector<std::byte> decode_buffer_2_;
};

}  // namespace client
}  // namespace fix_proxy
}  // namespace roq
