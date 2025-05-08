/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/server/session.hpp"

#include <nameof.hpp>

#include "roq/logging.hpp"

#include "roq/utils/debug/fix/message.hpp"
#include "roq/utils/debug/hex/message.hpp"

#include "roq/fix/reader.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {
namespace server {

// === CONSTANTS ===

namespace {
auto const FIX_VERSION = fix::Version::FIX_44;
}  // namespace

// === HELPERS ===

namespace {
auto create_connection_factory(auto &settings, auto &context, auto &uri) {
  log::debug("uri={}"sv, uri);
  auto config = io::net::ConnectionFactory::Config{
      .interface = {},
      .uris = {&uri, 1},
      .validate_certificate = settings.net.tls_validate_certificate,
  };
  return io::net::ConnectionFactory::create(context, config);
}

auto create_connection_manager(auto &handler, auto &settings, auto &connection_factory) {
  auto config = io::net::ConnectionManager::Config{
      .connection_timeout = settings.net.connection_timeout,
      .disconnect_on_idle_timeout = {},
      .always_reconnect = true,
  };
  return io::net::ConnectionManager::create(handler, connection_factory, config);
}
}  // namespace

// === IMPLEMENTATION ===

Session::Session(Handler &handler, Settings const &settings, io::Context &context, io::web::URI const &uri, fix::proxy::Manager &proxy)
    : handler_{handler}, sender_comp_id_{settings.server.sender_comp_id}, target_comp_id_{settings.server.target_comp_id}, debug_{settings.server.debug},
      connection_factory_{create_connection_factory(settings, context, uri)},
      connection_manager_{create_connection_manager(*this, settings, *connection_factory_)}, decode_buffer_(settings.server.decode_buffer_size),
      decode_buffer_2_(settings.server.decode_buffer_size), proxy_{proxy}

{
}

void Session::operator()(Event<Start> const &) {
  (*connection_manager_).start();
}

void Session::operator()(Event<Stop> const &) {
  (*connection_manager_).stop();
}

void Session::operator()(Event<Timer> const &event) {
  (*connection_manager_).refresh(event.value.now);
}

// fix::proxy::Manager::Handler

// - manager => server

void Session::operator()(Trace<fix::codec::Reject> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::Logon> const &event) {
  auto &[trace_info, logon] = event;
  auto logon_2 = logon;
  logon_2.next_expected_msg_seq_num = inbound_.msg_seq_num + 1;  // note!
  Trace event_2{trace_info, logon_2};
  send(event_2);
}

void Session::operator()(Trace<fix::codec::Logout> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::Heartbeat> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::TestRequest> const &event) {
  send(event);
}

// - client => server

void Session::operator()(Trace<fix::codec::BusinessMessageReject> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::UserRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::TradingSessionStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::SecurityListRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::SecurityDefinitionRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::SecurityStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::MarketDataRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::NewOrderSingle> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::OrderCancelReplaceRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::OrderCancelRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::OrderMassCancelRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::OrderStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::OrderMassStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::TradeCaptureReportRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::RequestForPositions> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::MassQuote> const &event) {
  send(event);
}

void Session::operator()(Trace<fix::codec::QuoteCancel> const &event) {
  send(event);
}

// io::net::ConnectionManager::Handler

void Session::operator()(io::net::ConnectionManager::Connected const &) {
  log::debug("Connected"sv);
  TraceInfo trace_info;
  auto connected = fix::proxy::Manager::Connected{};
  create_trace_and_dispatch(proxy_, trace_info, connected);
}

void Session::operator()(io::net::ConnectionManager::Disconnected const &) {
  log::debug("Disconnected"sv);
  TraceInfo trace_info;
  auto disconnected = fix::proxy::Manager::Disconnected{};
  create_trace_and_dispatch(proxy_, trace_info, disconnected);
  // XXX HANS
  Disconnected disconnected_2;
  Trace event{trace_info, disconnected_2};
  //
  handler_(event);
  outbound_ = {};
  inbound_ = {};
}

void Session::operator()(io::net::ConnectionManager::Read const &) {
  auto logger = [this](auto &message) {
    if (debug_) [[unlikely]] {
      log::info("{}"sv, utils::debug::fix::Message{message});
    }
  };
  auto buffer = (*connection_manager_).buffer();
  size_t total_bytes = 0;
  while (!std::empty(buffer)) {
    TraceInfo trace_info;
    auto parser = [&](auto &message) {
      try {
        check(message.header);
        Trace event{trace_info, message};
        parse(event);
      } catch (std::exception &) {
        log::warn("{}"sv, utils::debug::fix::Message{buffer});
#ifndef NDEBUG
        log::warn("{}"sv, utils::debug::hex::Message{buffer});
#endif
        log::error("Message could not be parsed. PLEASE REPORT!"sv);
        throw;
      }
    };
    auto bytes = fix::Reader<FIX_VERSION>::dispatch(buffer, parser, logger);
    if (bytes == 0) {
      break;
    }
    assert(bytes <= std::size(buffer));
    total_bytes += bytes;
    buffer = buffer.subspan(bytes);
  }
  (*connection_manager_).drain(total_bytes);
}

void Session::operator()(io::net::ConnectionManager::Write const &) {
}

// outbound

template <typename T>
void Session::send(Trace<T> const &event) {
  auto &[trace_info, value] = event;
  log::info<2>("send (=> server): {}={}"sv, nameof::nameof_short_type<T>(), value);
  auto sending_time = clock::get_realtime();
  auto header = fix::Header{
      .version = FIX_VERSION,
      .msg_type = T::MSG_TYPE,
      .sender_comp_id = sender_comp_id_,
      .target_comp_id = target_comp_id_,
      .msg_seq_num = ++outbound_.msg_seq_num,  // note!
      .sending_time = sending_time,
  };
  auto helper = [&](auto &buffer) {
    auto message = value.encode(header, buffer);
    if (debug_) [[unlikely]] {
      log::info("{}"sv, utils::debug::fix::Message{message});
    }
    return std::size(message);
  };
  if ((*connection_manager_).send(helper)) {
  } else {
    log::warn("HERE"sv);
  }
}

// inbound

void Session::parse(Trace<fix::Message> const &event) {
  auto &[trace_info, message] = event;
  try {
    switch (message.header.msg_type) {
      using enum fix::MsgType;
      case REJECT:
        dispatch<fix::codec::Reject>(event);
        break;
      case LOGON:
        dispatch<fix::codec::Logon>(event);
        break;
      case LOGOUT:
        dispatch<fix::codec::Logout>(event);
        break;
      case HEARTBEAT:
        dispatch<fix::codec::Heartbeat>(event);
        break;
      case TEST_REQUEST:
        dispatch<fix::codec::TestRequest>(event);
        break;
      case RESEND_REQUEST:
        dispatch<fix::codec::ResendRequest>(event);
        break;
      case BUSINESS_MESSAGE_REJECT:
        dispatch<fix::codec::BusinessMessageReject>(event);
        break;
      case USER_RESPONSE:
        dispatch<fix::codec::UserResponse>(event);
        break;
      case TRADING_SESSION_STATUS:
        dispatch<fix::codec::TradingSessionStatus>(event, decode_buffer_);
        break;
      case SECURITY_LIST:
        dispatch<fix::codec::SecurityList>(event, decode_buffer_);
        break;
      case SECURITY_DEFINITION:
        dispatch<fix::codec::SecurityDefinition>(event, decode_buffer_);
        break;
      case SECURITY_STATUS:
        dispatch<fix::codec::SecurityStatus>(event, decode_buffer_);
        break;
      case MARKET_DATA_REQUEST_REJECT:
        dispatch<fix::codec::MarketDataRequestReject>(event, decode_buffer_);
        break;
      case MARKET_DATA_SNAPSHOT_FULL_REFRESH:
        dispatch<fix::codec::MarketDataSnapshotFullRefresh>(event, decode_buffer_);
        break;
      case MARKET_DATA_INCREMENTAL_REFRESH:
        dispatch<fix::codec::MarketDataIncrementalRefresh>(event, decode_buffer_);
        break;
      case EXECUTION_REPORT:
        dispatch<fix::codec::ExecutionReport>(event, decode_buffer_);
        break;
      case ORDER_CANCEL_REJECT:
        dispatch<fix::codec::OrderCancelReject>(event, decode_buffer_);
        break;
      case ORDER_MASS_CANCEL_REPORT:
        dispatch<fix::codec::OrderMassCancelReport>(event, decode_buffer_);
        break;
      case TRADE_CAPTURE_REPORT_REQUEST_ACK:
        dispatch<fix::codec::TradeCaptureReportRequestAck>(event, decode_buffer_);
        break;
      case TRADE_CAPTURE_REPORT:
        dispatch<fix::codec::TradeCaptureReport>(event, decode_buffer_, decode_buffer_2_);
        break;
      case REQUEST_FOR_POSITIONS_ACK:
        dispatch<fix::codec::RequestForPositionsAck>(event, decode_buffer_);
        break;
      case POSITION_REPORT:
        dispatch<fix::codec::PositionReport>(event, decode_buffer_);
        break;
      case MASS_QUOTE_ACK:
        dispatch<fix::codec::MassQuoteAck>(event, decode_buffer_, decode_buffer_2_);
        break;
      case QUOTE_STATUS_REPORT:
        dispatch<fix::codec::QuoteStatusReport>(event);
        break;
      default:
        log::warn("Unexpected msg_type={}"sv, message.header.msg_type);
    }
  } catch (std::exception &e) {
    log::error(R"(Exception: what="{}")"sv, e.what());
    (*connection_manager_).close();
  }
}

template <typename T, typename... Args>
void Session::dispatch(Trace<fix::Message> const &event, Args &&...args) {
  auto &[trace_info, message] = event;
  auto value = T::create(message, std::forward<Args>(args)...);
  log::info<1>("{}={}"sv, nameof::nameof_short_type<T>(), value);
  create_trace_and_dispatch(proxy_, trace_info, value);
}

void Session::check(fix::Header const &header) {
  auto current = header.msg_seq_num;
  auto expected = inbound_.msg_seq_num + 1;
  if (current != expected) [[unlikely]] {
    if (expected < current) {
      log::warn(
          "*** SEQUENCE GAP *** "
          "current={} previous={} distance={}"sv,
          current,
          inbound_.msg_seq_num,
          current - inbound_.msg_seq_num);
    } else {
      log::warn(
          "*** SEQUENCE REPLAY *** "
          "current={} previous={} distance={}"sv,
          current,
          inbound_.msg_seq_num,
          inbound_.msg_seq_num - current);
    }
  }
  inbound_.msg_seq_num = current;
}

}  // namespace server
}  // namespace fix_proxy
}  // namespace roq
