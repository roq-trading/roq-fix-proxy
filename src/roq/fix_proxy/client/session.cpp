/* Copyright (c) 2017-2026, Hans Erik Thrane */

#include "roq/fix_proxy/client/session.hpp"

#include <nameof.hpp>

#include <exception>

#include "roq/logging.hpp"

#include "roq/utils/debug/fix/message.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {
namespace client {

// === CONSTANTS ===

namespace {
auto const FIX_VERSION = fix::Version::FIX_44;
}  // namespace

// === IMPLEMENTATION ===

Session::Session(io::net::tcp::Connection::Factory &factory, uint64_t session_id, Shared &shared)
    : connection_{factory.create(*this)}, session_id_{session_id}, shared_{shared}, decode_buffer_(shared.settings.client.decode_buffer_size),
      decode_buffer_2_(shared.settings.client.decode_buffer_size) {
}

void Session::force_disconnect() {
  close();
}

// fix::proxy::Manager

// - connection

void Session::operator()(Trace<fix::proxy::Manager::Disconnect> const &event) {
  close();
}

// - manager => client

void Session::operator()(Trace<fix::codec::Reject> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::Logon> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::Logout> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::Heartbeat> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::TestRequest> const &event) {
  send<2>(event);
}

// - server => client

void Session::operator()(Trace<fix::codec::BusinessMessageReject> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::TradingSessionStatus> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::SecurityList> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::SecurityDefinition> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::SecurityStatus> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::MarketDataRequestReject> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::MarketDataSnapshotFullRefresh> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::MarketDataIncrementalRefresh> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::ExecutionReport> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::OrderCancelReject> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::OrderMassCancelReport> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::TradeCaptureReportRequestAck> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::TradeCaptureReport> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::RequestForPositionsAck> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::PositionReport> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::MassQuoteAck> const &event) {
  send<2>(event);
}

void Session::operator()(Trace<fix::codec::QuoteStatusReport> const &event) {
  send<2>(event);
}

// io::net::tcp::Connection::Handler

void Session::operator()(io::net::tcp::Connection::Read const &) {
  buffer_.append(*connection_);
  auto buffer = buffer_.data();
  try {
    size_t total_bytes = 0;
    auto helper = [&](auto &message) {
      TraceInfo trace_info;
      check(message.header);
      Trace event{trace_info, message};
      parse(event);
    };
    auto logger = [&]([[maybe_unused]] auto &message) {
      // note! here we could log the raw binary message
    };
    while (!std::empty(buffer)) {
      auto bytes = fix::Reader<FIX_VERSION>::dispatch(buffer, helper, logger);
      if (bytes == 0) {
        break;
      }
      if (shared_.settings.test.fix_debug) {
        auto message = buffer.subspan(0, bytes);
        log::info<0>("[session_id={}]: {}"sv, session_id_, utils::debug::fix::Message{message});
      }
      assert(bytes <= std::size(buffer));
      total_bytes += bytes;
      buffer = buffer.subspan(bytes);
    }
    buffer_.drain(total_bytes);
  } catch (SystemError &e) {
    log::error("Exception: {}"sv, e);
    close();
  } catch (Exception &e) {
    log::error("Exception: {}"sv, e);
    close();
  } catch (std::exception &e) {
    log::error("Exception: {}"sv, e.what());
    close();
  } catch (...) {
    auto e = std::current_exception();
    log::fatal(R"(Unhandled exception: type="{}")"sv, typeid(e).name());
  }
}

void Session::operator()(io::net::tcp::Connection::Disconnected const &) {
  // xXX FIXME HANS
}

// outbound

template <std::size_t level, typename T>
void Session::send(Trace<T> const &event) {
  auto sending_time = clock::get_realtime();
  send<level>(event, sending_time);
}

template <std::size_t level, typename T>
void Session::send(Trace<T> const &event, std::chrono::nanoseconds sending_time) {
  auto &[trace_info, value] = event;
  log::info<level>("send (=> client): {}={}"sv, nameof::nameof_short_type<T>(), value);
  assert(!std::empty(comp_id_));
  auto header = fix::Header{
      .version = FIX_VERSION,
      .msg_type = T::MSG_TYPE,
      .sender_comp_id = shared_.settings.client.comp_id,
      .target_comp_id = comp_id_,
      .msg_seq_num = ++outbound_.msg_seq_num,  // note!
      .sending_time = sending_time,
  };
  auto helper = [&](auto &buffer) {
    auto message = value.encode(header, buffer);
    return std::size(message);
  };
  if ((*connection_).send(helper)) {
  } else {
    log::warn("HERE"sv);
  }
}

// inbound

void Session::parse(Trace<fix::Message> const &event) {
  auto &[trace_info, message] = event;
  if (std::empty(comp_id_)) [[unlikely]] {
    comp_id_ = message.header.sender_comp_id;
  }
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
    case TRADING_SESSION_STATUS_REQUEST:
      dispatch<fix::codec::TradingSessionStatusRequest>(event);
      break;
    case SECURITY_LIST_REQUEST:
      dispatch<fix::codec::SecurityListRequest>(event);
      break;
    case SECURITY_DEFINITION_REQUEST:
      dispatch<fix::codec::SecurityDefinitionRequest>(event, decode_buffer_);
      break;
    case SECURITY_STATUS_REQUEST:
      dispatch<fix::codec::SecurityStatusRequest>(event, decode_buffer_);
      break;
    case MARKET_DATA_REQUEST:
      dispatch<fix::codec::MarketDataRequest>(event, decode_buffer_);
      break;
    case NEW_ORDER_SINGLE:
      dispatch<fix::codec::NewOrderSingle>(event, decode_buffer_);
      break;
    case ORDER_CANCEL_REPLACE_REQUEST:
      dispatch<fix::codec::OrderCancelReplaceRequest>(event, decode_buffer_);
      break;
    case ORDER_CANCEL_REQUEST:
      dispatch<fix::codec::OrderCancelRequest>(event, decode_buffer_);
      break;
    case ORDER_MASS_CANCEL_REQUEST:
      dispatch<fix::codec::OrderMassCancelRequest>(event, decode_buffer_);
      break;
    case ORDER_STATUS_REQUEST:
      dispatch<fix::codec::OrderStatusRequest>(event, decode_buffer_);
      break;
    case ORDER_MASS_STATUS_REQUEST:
      dispatch<fix::codec::OrderMassStatusRequest>(event, decode_buffer_);
      break;
    case TRADE_CAPTURE_REPORT_REQUEST:
      dispatch<fix::codec::TradeCaptureReportRequest>(event, decode_buffer_);
      break;
    case REQUEST_FOR_POSITIONS:
      dispatch<fix::codec::RequestForPositions>(event, decode_buffer_);
      break;
    case MASS_QUOTE:
      dispatch<fix::codec::MassQuote>(event, decode_buffer_, decode_buffer_2_);
      break;
    case QUOTE_CANCEL:
      dispatch<fix::codec::QuoteCancel>(event, decode_buffer_);
      break;
    default:
      log::warn("Unexpected: msg_type={}"sv, message.header.msg_type);
      break;
  };
}

template <typename T, typename... Args>
void Session::dispatch(Trace<fix::Message> const &event, Args &&...args) {
  auto &[trace_info, message] = event;
  auto value = T::create(message, std::forward<Args>(args)...);
  log::info<1>("session_id={}, {}={}"sv, session_id_, nameof::nameof_short_type<T>(), value);
  create_trace_and_dispatch(shared_.proxy, trace_info, value, message.header, session_id_);
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

// utils

void Session::close() {
  (*connection_).close();
}

}  // namespace client
}  // namespace fix_proxy
}  // namespace roq
