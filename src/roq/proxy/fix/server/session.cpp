/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/proxy/fix/server/session.hpp"

#include <fmt/core.h>

#include <magic_enum/magic_enum_format.hpp>

#include <nameof.hpp>

#include "roq/logging.hpp"

#include "roq/exceptions.hpp"

#include "roq/utils/chrono.hpp"
#include "roq/utils/traits.hpp"
#include "roq/utils/update.hpp"

#include "roq/utils/debug/fix/message.hpp"
#include "roq/utils/debug/hex/message.hpp"

#include "roq/fix/reader.hpp"

using namespace std::literals;

namespace roq {
namespace proxy {
namespace fix {
namespace server {

// === CONSTANTS ===

namespace {
auto const FIX_VERSION = roq::fix::Version::FIX_44;
auto const LOGOUT_RESPONSE = "LOGOUT"sv;
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

Session::Session(Handler &handler, Settings const &settings, io::Context &context, io::web::URI const &uri)
    : handler_{handler}, username_{settings.server.username}, password_{settings.server.password}, sender_comp_id_{settings.server.sender_comp_id},
      target_comp_id_{settings.server.target_comp_id}, ping_freq_{settings.server.ping_freq}, debug_{settings.server.debug},
      connection_factory_{create_connection_factory(settings, context, uri)},
      connection_manager_{create_connection_manager(*this, settings, *connection_factory_)}, decode_buffer_(settings.server.decode_buffer_size),
      decode_buffer_2_(settings.server.decode_buffer_size) {
}

void Session::operator()(Event<Start> const &) {
  (*connection_manager_).start();
}

void Session::operator()(Event<Stop> const &) {
  (*connection_manager_).stop();
}

void Session::operator()(Event<Timer> const &event) {
  auto now = event.value.now;
  (*connection_manager_).refresh(now);
  if (state_ <= State::LOGON_SENT)
    return;
  if (next_heartbeat_ <= now) {
    next_heartbeat_ = now + ping_freq_;
    send_test_request(now);
  }
}

bool Session::ready() const {
  return state_ == State::READY;
}

void Session::operator()(Trace<roq::fix::codec::UserRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityListRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityDefinitionRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::NewOrderSingle> const &event) {
  log::debug("new_order_single={}"sv, event.value);
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderCancelReplaceRequest> const &event) {
  log::debug("order_cancel_replace_request={}"sv, event.value);
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderCancelRequest> const &event) {
  log::debug("order_cancel_request={}"sv, event.value);
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderMassStatusRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderMassCancelRequest> const &event) {
  log::debug("order_cancel_request={}"sv, event.value);
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::RequestForPositions> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::TradeCaptureReportRequest> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::MassQuote> const &event) {
  send(event);
}

void Session::operator()(Trace<roq::fix::codec::QuoteCancel> const &event) {
  send(event);
}

void Session::operator()(Session::State state) {
  if (utils::update(state_, state))
    log::debug("state={}"sv, state);
}

// io::net::ConnectionManager::Handler

void Session::operator()(io::net::ConnectionManager::Connected const &) {
  log::debug("Connected"sv);
  send_logon();
  (*this)(State::LOGON_SENT);
}

void Session::operator()(io::net::ConnectionManager::Disconnected const &) {
  log::debug("Disconnected"sv);
  TraceInfo trace_info;
  Disconnected disconnected;
  Trace event{trace_info, disconnected};
  handler_(event);
  outbound_ = {};
  inbound_ = {};
  next_heartbeat_ = {};
  (*this)(State::DISCONNECTED);
}

void Session::operator()(io::net::ConnectionManager::Read const &) {
  auto logger = [this](auto &message) {
    if (debug_) [[unlikely]]
      log::info("{}"sv, utils::debug::fix::Message{message});
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
    auto bytes = roq::fix::Reader<FIX_VERSION>::dispatch(buffer, parser, logger);
    if (bytes == 0)
      break;
    assert(bytes <= std::size(buffer));
    total_bytes += bytes;
    buffer = buffer.subspan(bytes);
  }
  (*connection_manager_).drain(total_bytes);
}

void Session::operator()(io::net::ConnectionManager::Write const &) {
}

// inbound

void Session::check(roq::fix::Header const &header) {
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

void Session::parse(Trace<roq::fix::Message> const &event) {
  auto &[trace_info, message] = event;
  auto &header = message.header;
  switch (header.msg_type) {
    using enum roq::fix::MsgType;
    // session
    case REJECT: {
      auto reject = roq::fix::codec::Reject::create(message);
      dispatch(event, reject);
      break;
    }
    case RESEND_REQUEST: {
      auto resend_request = roq::fix::codec::ResendRequest::create(message);
      dispatch(event, resend_request);
      break;
    }
    case LOGON: {
      auto logon = roq::fix::codec::Logon::create(message);
      dispatch(event, logon);
      break;
    }
    case LOGOUT: {
      auto logout = roq::fix::codec::Heartbeat::create(message);
      dispatch(event, logout);
      break;
    }
    case HEARTBEAT: {
      auto heartbeat = roq::fix::codec::Heartbeat::create(message);
      dispatch(event, heartbeat);
      break;
    }
    case TEST_REQUEST: {
      auto test_request = roq::fix::codec::TestRequest::create(message);
      dispatch(event, test_request);
      break;
    }
      // business
    case BUSINESS_MESSAGE_REJECT: {
      auto business_message_reject = roq::fix::codec::BusinessMessageReject::create(message);
      dispatch(event, business_message_reject);
      break;
    }
      // user
    case USER_RESPONSE: {
      auto user_response = roq::fix::codec::UserResponse::create(message);
      dispatch(event, user_response);
      break;
    }
      // security
    case SECURITY_LIST: {
      auto security_list = roq::fix::codec::SecurityList::create(message, decode_buffer_);
      dispatch(event, security_list);
      break;
    }
    case SECURITY_DEFINITION: {
      auto security_definition = roq::fix::codec::SecurityDefinition::create(message, decode_buffer_);
      dispatch(event, security_definition);
      break;
    }
    case SECURITY_STATUS: {
      auto security_status = roq::fix::codec::SecurityStatus::create(message, decode_buffer_);
      dispatch(event, security_status);
      break;
    }
      // market data
    case MARKET_DATA_REQUEST_REJECT: {
      auto market_data_request_reject = roq::fix::codec::MarketDataRequestReject::create(message, decode_buffer_);
      dispatch(event, market_data_request_reject);
      break;
    }
    case MARKET_DATA_SNAPSHOT_FULL_REFRESH: {
      auto market_data_snapshot_full_refresh = roq::fix::codec::MarketDataSnapshotFullRefresh::create(message, decode_buffer_);
      dispatch(event, market_data_snapshot_full_refresh);
      break;
    }
    case MARKET_DATA_INCREMENTAL_REFRESH: {
      auto market_data_incremental_refresh = roq::fix::codec::MarketDataIncrementalRefresh::create(message, decode_buffer_);
      dispatch(event, market_data_incremental_refresh);
      break;
    }
      // orders
    case ORDER_CANCEL_REJECT: {
      auto order_cancel_reject = roq::fix::codec::OrderCancelReject::create(message, decode_buffer_);
      dispatch(event, order_cancel_reject);
      break;
    }
    case ORDER_MASS_CANCEL_REPORT: {
      auto order_mass_cancel_report = roq::fix::codec::OrderMassCancelReport::create(message, decode_buffer_);
      dispatch(event, order_mass_cancel_report);
      break;
    }
    case EXECUTION_REPORT: {
      auto execution_report = roq::fix::codec::ExecutionReport::create(message, decode_buffer_);
      dispatch(event, execution_report);
      break;
    }
      // positions
    case REQUEST_FOR_POSITIONS_ACK: {
      auto request_for_positions_ack = roq::fix::codec::RequestForPositionsAck::create(message, decode_buffer_);
      dispatch(event, request_for_positions_ack);
      break;
    }
    case POSITION_REPORT: {
      auto position_report = roq::fix::codec::PositionReport::create(message, decode_buffer_);
      dispatch(event, position_report);
      break;
    }
      // trades
    case TRADE_CAPTURE_REPORT_REQUEST_ACK: {
      auto trade_capture_report_request_ack = roq::fix::codec::TradeCaptureReportRequestAck::create(message, decode_buffer_);
      dispatch(event, trade_capture_report_request_ack);
      break;
    }
    case TRADE_CAPTURE_REPORT: {
      auto trade_capture_report = roq::fix::codec::TradeCaptureReport::create(message, decode_buffer_, decode_buffer_2_);
      dispatch(event, trade_capture_report);
      break;
    }
      // quotes
    case MASS_QUOTE_ACK: {
      auto mass_quote_ack = roq::fix::codec::MassQuoteAck::create(message, decode_buffer_, decode_buffer_2_);
      dispatch(event, mass_quote_ack);
      break;
    }
    case QUOTE_STATUS_REPORT: {
      auto quote_status_report = roq::fix::codec::QuoteStatusReport::create(message);
      dispatch(event, quote_status_report);
      break;
    }
    default:
      log::warn("Unexpected msg_type={}"sv, header.msg_type);
  }
}

template <typename T>
void Session::dispatch(Trace<roq::fix::Message> const &event, T const &value) {
  auto &[trace_info, message] = event;
  log::info<1>("{}={}"sv, nameof::nameof_short_type<T>(), value);
  Trace event_2{trace_info, value};
  (*this)(event_2, message.header);
}

void Session::operator()(Trace<roq::fix::codec::Reject> const &event, roq::fix::Header const &) {
  auto &[trace_info, reject] = event;
  log::debug("reject={}, trace_info={}"sv, reject, trace_info);
}

void Session::operator()(Trace<roq::fix::codec::ResendRequest> const &event, roq::fix::Header const &) {
  auto &[trace_info, resend_request] = event;
  log::debug("resend_request={}, trace_info={}"sv, resend_request, trace_info);
}

void Session::operator()(Trace<roq::fix::codec::Logon> const &event, roq::fix::Header const &) {
  auto &[trace_info, logon] = event;
  log::debug("logon={}, trace_info={}"sv, logon, trace_info);
  assert(state_ == State::LOGON_SENT);
  Ready ready;
  Trace event_2{trace_info, ready};
  handler_(event_2);
  (*this)(State::READY);
}

void Session::operator()(Trace<roq::fix::codec::Logout> const &event, roq::fix::Header const &) {
  auto &[trace_info, logout] = event;
  log::debug("logout={}, trace_info={}"sv, logout, trace_info);
  // note! mandated, must send a logout response
  send_logout(LOGOUT_RESPONSE);
  log::warn("closing connection"sv);
  (*connection_manager_).close();
}

void Session::operator()(Trace<roq::fix::codec::Heartbeat> const &event, roq::fix::Header const &) {
  auto &[trace_info, heartbeat] = event;
  log::debug("heartbeat={}, trace_info={}"sv, heartbeat, trace_info);
}

void Session::operator()(Trace<roq::fix::codec::TestRequest> const &event, roq::fix::Header const &) {
  auto &[trace_info, test_request] = event;
  send_heartbeat(test_request.test_req_id);
}

void Session::operator()(Trace<roq::fix::codec::BusinessMessageReject> const &event, roq::fix::Header const &) {
  auto &[trace_info, business_message_reject] = event;
  log::debug("business_message_reject={}, trace_info={}"sv, business_message_reject, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityList> const &event, roq::fix::Header const &) {
  auto &[trace_info, security_list] = event;
  log::debug("security_list={}, trace_info={}"sv, security_list, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityDefinition> const &event, roq::fix::Header const &) {
  auto &[trace_info, security_definition] = event;
  log::debug("security_definition={}, trace_info={}"sv, security_definition, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityStatus> const &event, roq::fix::Header const &) {
  auto &[trace_info, security_status] = event;
  log::debug("security_status={}, trace_info={}"sv, security_status, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataRequestReject> const &event, roq::fix::Header const &) {
  auto &[trace_info, market_data_request_reject] = event;
  log::debug("market_data_request_reject={}, trace_info={}"sv, market_data_request_reject, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataSnapshotFullRefresh> const &event, roq::fix::Header const &) {
  auto &[trace_info, market_data_snapshot_full_refresh] = event;
  log::debug<1>("market_data_snapshot_full_refresh={}, trace_info={}"sv, market_data_snapshot_full_refresh, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataIncrementalRefresh> const &event, roq::fix::Header const &) {
  auto &[trace_info, market_data_incremental_refresh] = event;
  log::debug<1>("market_data_incremental_refresh={}, trace_info={}"sv, market_data_incremental_refresh, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::UserResponse> const &event, roq::fix::Header const &) {
  auto &[trace_info, user_response] = event;
  log::debug("user_response={}, trace_info={}"sv, user_response, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderCancelReject> const &event, roq::fix::Header const &) {
  auto &[trace_info, order_cancel_reject] = event;
  log::debug("order_cancel_reject={}, trace_info={}"sv, order_cancel_reject, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderMassCancelReport> const &event, roq::fix::Header const &) {
  auto &[trace_info, order_mass_cancel_report] = event;
  log::debug("order_mass_cancel_report={}, trace_info={}"sv, order_mass_cancel_report, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::ExecutionReport> const &event, roq::fix::Header const &) {
  auto &[trace_info, execution_report] = event;
  log::debug("execution_report={}, trace_info={}"sv, execution_report, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::RequestForPositionsAck> const &event, roq::fix::Header const &) {
  auto &[trace_info, request_for_positions_ack] = event;
  log::debug("request_for_positions_ack={}, trace_info={}"sv, request_for_positions_ack, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::PositionReport> const &event, roq::fix::Header const &) {
  auto &[trace_info, position_report] = event;
  log::debug("position_report={}, trace_info={}"sv, position_report, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::TradeCaptureReportRequestAck> const &event, roq::fix::Header const &) {
  auto &[trace_info, trade_capture_report_request_ack] = event;
  log::debug("trade_capture_report_request_ack={}, trace_info={}"sv, trade_capture_report_request_ack, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::TradeCaptureReport> const &event, roq::fix::Header const &) {
  auto &[trace_info, trade_capture_report] = event;
  log::debug("trade_capture_report={}, trace_info={}"sv, trade_capture_report, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::MassQuoteAck> const &event, roq::fix::Header const &) {
  auto &[trace_info, mass_quote_ack] = event;
  log::debug("mass_quote_ack={}, trace_info={}"sv, mass_quote_ack, trace_info);
  handler_(event);
}

void Session::operator()(Trace<roq::fix::codec::QuoteStatusReport> const &event, roq::fix::Header const &) {
  auto &[trace_info, quote_status_report] = event;
  log::debug("quote_status_report={}, trace_info={}"sv, quote_status_report, trace_info);
  handler_(event);
}

// outbound

template <typename T>
void Session::send(T const &value) {
  if constexpr (utils::is_specialization<T, Trace>::value) {
    // external
    if (!ready())
      throw NotReady{"not ready"sv};
    send_helper(value.value);
  } else {
    // internal
    send_helper(value);
  }
}

template <typename T>
void Session::send_helper(T const &value) {
  log::info<2>("send (=> server): {}={}"sv, nameof::nameof_short_type<T>(), value);
  auto sending_time = clock::get_realtime();
  auto header = roq::fix::Header{
      .version = FIX_VERSION,
      .msg_type = T::MSG_TYPE,
      .sender_comp_id = sender_comp_id_,
      .target_comp_id = target_comp_id_,
      .msg_seq_num = ++outbound_.msg_seq_num,  // note!
      .sending_time = sending_time,
  };
  if ((*connection_manager_).send([&](auto &buffer) {
        auto message = value.encode(header, buffer);
        if (debug_) [[unlikely]]
          log::info("{}"sv, utils::debug::fix::Message{message});
        return std::size(message);
      })) {
  } else {
    log::warn("HERE"sv);
  }
}

void Session::send_logon() {
  auto heart_bt_int = static_cast<decltype(roq::fix::codec::Logon::heart_bt_int)>(std::chrono::duration_cast<std::chrono::seconds>(ping_freq_).count());
  auto logon = roq::fix::codec::Logon{
      .encrypt_method = roq::fix::EncryptMethod::NONE,
      .heart_bt_int = heart_bt_int,
      .raw_data_length = {},
      .raw_data = {},
      .reset_seq_num_flag = true,
      .next_expected_msg_seq_num = inbound_.msg_seq_num + 1,  // note!
      .username = username_,
      .password = password_,
  };
  send(logon);
}

void Session::send_logout(std::string_view const &text) {
  auto logout = roq::fix::codec::Logout{
      .text = text,
  };
  send(logout);
}

void Session::send_heartbeat(std::string_view const &test_req_id) {
  auto heartbeat = roq::fix::codec::Heartbeat{
      .test_req_id = test_req_id,
  };
  send(heartbeat);
}

void Session::send_test_request(std::chrono::nanoseconds now) {
  auto test_req_id = fmt::format("{}"sv, now.count());
  auto test_request = roq::fix::codec::TestRequest{
      .test_req_id = test_req_id,
  };
  send(test_request);
}

}  // namespace server
}  // namespace fix
}  // namespace proxy
}  // namespace roq
