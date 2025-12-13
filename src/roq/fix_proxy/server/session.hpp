/* Copyright (c) 2017-2026, Hans Erik Thrane */

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

#include "roq/fix/proxy/manager.hpp"

#include "roq/fix_proxy/settings.hpp"

namespace roq {
namespace fix_proxy {
namespace server {

struct Session final : public io::net::ConnectionManager::Handler {
  struct Ready final {};
  struct Disconnected final {};
  struct Handler {
    virtual void operator()(Trace<Ready> const &) = 0;
    virtual void operator()(Trace<Disconnected> const &) = 0;
  };

  Session(Handler &, Settings const &, io::Context &, io::web::URI const &, fix::proxy::Manager &);

  void operator()(Event<Start> const &);
  void operator()(Event<Stop> const &);
  void operator()(Event<Timer> const &);

  // fix::proxy::Manager::Handler

  // - manager => server
  void operator()(Trace<fix::codec::Reject> const &);
  void operator()(Trace<fix::codec::Logon> const &);
  void operator()(Trace<fix::codec::Logout> const &);
  void operator()(Trace<fix::codec::Heartbeat> const &);
  void operator()(Trace<fix::codec::TestRequest> const &);

  // - client => server
  void operator()(Trace<fix::codec::BusinessMessageReject> const &);
  void operator()(Trace<fix::codec::UserRequest> const &);
  void operator()(Trace<fix::codec::TradingSessionStatusRequest> const &);
  void operator()(Trace<fix::codec::SecurityListRequest> const &);
  void operator()(Trace<fix::codec::SecurityDefinitionRequest> const &);
  void operator()(Trace<fix::codec::SecurityStatusRequest> const &);
  void operator()(Trace<fix::codec::MarketDataRequest> const &);
  void operator()(Trace<fix::codec::NewOrderSingle> const &);
  void operator()(Trace<fix::codec::OrderCancelReplaceRequest> const &);
  void operator()(Trace<fix::codec::OrderCancelRequest> const &);
  void operator()(Trace<fix::codec::OrderMassCancelRequest> const &);
  void operator()(Trace<fix::codec::OrderStatusRequest> const &);
  void operator()(Trace<fix::codec::OrderMassStatusRequest> const &);
  void operator()(Trace<fix::codec::TradeCaptureReportRequest> const &);
  void operator()(Trace<fix::codec::RequestForPositions> const &);
  void operator()(Trace<fix::codec::MassQuote> const &);
  void operator()(Trace<fix::codec::QuoteCancel> const &);

 protected:
  // io::net::ConnectionManager::Handler

  void operator()(io::net::ConnectionManager::Connected const &) override;
  void operator()(io::net::ConnectionManager::Disconnected const &) override;
  void operator()(io::net::ConnectionManager::Read const &) override;
  void operator()(io::net::ConnectionManager::Write const &) override;

  // tools

  // - outbound

  template <typename T>
  void send(Trace<T> const &);

  // - inbound

  void parse(Trace<fix::Message> const &);

  template <typename T, typename... Args>
  void dispatch(Trace<fix::Message> const &, Args &&...);

  void check(fix::Header const &);

 private:
  Handler &handler_;
  // config
  std::string_view const sender_comp_id_;
  std::string_view const target_comp_id_;
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
  // proxy
  fix::proxy::Manager &proxy_;
};

}  // namespace server
}  // namespace fix_proxy
}  // namespace roq
