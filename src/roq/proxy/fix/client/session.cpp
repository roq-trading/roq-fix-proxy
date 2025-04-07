/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/proxy/fix/client/session.hpp"

#include <fmt/core.h>

#include <magic_enum/magic_enum_format.hpp>

#include <nameof.hpp>

#include <exception>

#include "roq/logging.hpp"

#include "roq/exceptions.hpp"

#include "roq/utils/chrono.hpp"  // hh_mm_ss
#include "roq/utils/update.hpp"

#include "roq/utils/debug/fix/message.hpp"

#include "roq/utils/codec/base64.hpp"

using namespace std::literals;

namespace roq {
namespace proxy {
namespace fix {
namespace client {

// === CONSTANTS ===

namespace {
auto const FIX_VERSION = roq::fix::Version::FIX_44;
}  // namespace

// === HELPERS ===

namespace {
auto validate_req_id(auto &req_id) {
  static auto const web_safe = true;
  return utils::codec::Base64::is_valid(req_id, web_safe);
}
}  // namespace

// === IMPLEMENTATION ===

Session::Session(Handler &handler, io::net::tcp::Connection::Factory &factory, Shared &shared, uint64_t session_id)
    : handler_{handler}, connection_{factory.create(*this)}, shared_{shared}, session_id_{session_id}, last_update_{clock::get_system()},
      decode_buffer_(shared.settings.client.decode_buffer_size), decode_buffer_2_(shared.settings.client.decode_buffer_size) {
}

void Session::operator()(Event<Stop> const &) {
}

void Session::operator()(Event<Timer> const &event) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
      if ((last_update_ + shared_.settings.client.logon_timeout) < event.value.now) {
        log::warn("Closing connection (reason: client did not send a logon message)"sv);
        close();
      }
      break;
    case WAITING_CREATE_ROUTE: {
      assert(user_response_timeout_.count());
      if (user_response_timeout_ < event.value.now) {
        auto const error = roq::fix::codec::Error::USER_RESPONSE_TIMEOUT;
        auto logout = roq::fix::codec::Logout{
            .text = magic_enum::enum_name(error),
        };
        send_and_close<2>(logout);
      }
      break;
    }
    case READY:
      if ((last_update_ + shared_.settings.client.heartbeat_freq) < event.value.now) {
        last_update_ = event.value.now;
        if (waiting_for_heartbeat_) {
          log::warn("Closing connection (reason: client did not send heartbeat)"sv);
          auto const error = roq::fix::codec::Error::MISSING_HEARTBEAT;
          auto logout = roq::fix::codec::Logout{
              .text = magic_enum::enum_name(error),
          };
          send_and_close<2>(logout);
        } else {
          auto test_req_id = fmt::format("{}"sv, event.value.now);  // XXX TODO something else
          auto test_request = roq::fix::codec::TestRequest{
              .test_req_id = test_req_id,
          };
          send<4>(test_request);
          waiting_for_heartbeat_ = true;
        }
      }
      break;
    case WAITING_REMOVE_ROUTE: {
      assert(user_response_timeout_.count());
      if (user_response_timeout_ < event.value.now) {
        auto const error = roq::fix::codec::Error::USER_RESPONSE_TIMEOUT;
        auto logout = roq::fix::codec::Logout{
            .text = magic_enum::enum_name(error),
        };
        send_and_close<2>(logout);
        // XXX HANS release route
      }
      break;
    }
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::BusinessMessageReject> const &event) {
  auto &[trace_info, business_message_reject] = event;
  if (ready())
    send<2>(business_message_reject);
}

void Session::operator()(Trace<roq::fix::codec::UserResponse> const &event) {
  auto &[trace_info, user_response] = event;
  user_response_timeout_ = {};
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
      break;
    case WAITING_CREATE_ROUTE:
      switch (user_response.user_status) {
        using enum roq::fix::UserStatus;
        case LOGGED_IN: {
          auto heart_bt_int = std::chrono::duration_cast<std::chrono::seconds>(shared_.settings.client.heartbeat_freq);
          auto response = roq::fix::codec::Logon{
              .encrypt_method = roq::fix::EncryptMethod::NONE,
              .heart_bt_int = static_cast<uint16_t>(heart_bt_int.count()),
              .raw_data_length = {},
              .raw_data = {},
              .reset_seq_num_flag = {},
              .next_expected_msg_seq_num = {},
              .username = {},
              .password = {},
          };
          log::debug("logon={}"sv, response);
          send<2>(response);
          (*this)(State::READY);
          break;
        }
        default:
          log::warn("user_response={}"sv, user_response);
          make_zombie();
      }
      break;
    case READY:
      break;
    case WAITING_REMOVE_ROUTE:
      switch (user_response.user_status) {
        using enum roq::fix::UserStatus;
        case NOT_LOGGED_IN: {
          auto success = [&]() {
            // username_.clear();
            // party_id_.clear();
            auto const error = roq::fix::codec::Error::GOODBYE;
            auto response = roq::fix::codec::Logout{
                .text = magic_enum::enum_name(error),
            };
            send_and_close<2>(response);
          };
          auto failure = [&](auto &reason) {
            log::warn(R"(Unexpected: failed to release session, reason="{}")"sv, reason);
            make_zombie();
          };
          shared_.session_logout(session_id_, success, failure);
          break;
        }
        default:
          log::warn("user_response={}"sv, user_response);
          make_zombie();
      }
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::SecurityList> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityDefinition> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::SecurityStatus> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataRequestReject> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataSnapshotFullRefresh> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::MarketDataIncrementalRefresh> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderCancelReject> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::OrderMassCancelReport> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::ExecutionReport> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::RequestForPositionsAck> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::PositionReport> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::TradeCaptureReportRequestAck> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::TradeCaptureReport> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::MassQuoteAck> const &event) {
  send_helper<2>(event);
}

void Session::operator()(Trace<roq::fix::codec::QuoteStatusReport> const &event) {
  send_helper<2>(event);
}

void Session::operator()(State state) {
  if (utils::update(state_, state))
    log::info("DEBUG: session_id={}, state={}"sv, session_id_, state_);
}

bool Session::ready() const {
  return state_ == State::READY;
}

bool Session::zombie() const {
  return state_ == State::ZOMBIE;
}

void Session::force_disconnect() {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
    case READY:
    case WAITING_REMOVE_ROUTE:
      close();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::close() {
  if (state_ != State::ZOMBIE) {
    (*connection_).close();
    make_zombie();
  }
}

// io::net::tcp::Connection::Handler

void Session::operator()(io::net::tcp::Connection::Read const &) {
  if (state_ == State::ZOMBIE)
    return;
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
      auto bytes = roq::fix::Reader<FIX_VERSION>::dispatch(buffer, helper, logger);
      if (bytes == 0)
        break;
      if (shared_.settings.test.fix_debug) {
        auto message = buffer.subspan(0, bytes);
        log::info<0>("[session_id={}]: {}"sv, session_id_, utils::debug::fix::Message{message});
      }
      assert(bytes <= std::size(buffer));
      total_bytes += bytes;
      buffer = buffer.subspan(bytes);
      if (state_ == State::ZOMBIE)
        break;
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
  make_zombie();
}

// utilities

void Session::make_zombie() {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
      break;
    case WAITING_CREATE_ROUTE:
    case READY:
    case WAITING_REMOVE_ROUTE: {
      TraceInfo trace_info;
      Session::Disconnected disconnected;
      Trace event{trace_info, disconnected};
      handler_(event, session_id_);
      break;
    }
    case ZOMBIE:
      return;
  }
  (*this)(State::ZOMBIE);
  shared_.session_remove(session_id_);
}

template <std::size_t level, typename T>
void Session::send_helper(Trace<T> const &event) {
  if (ready())
    send<level>(event.value);
}

template <std::size_t level, typename T>
void Session::send_and_close(T const &event) {
  assert(state_ != State::ZOMBIE);
  auto sending_time = clock::get_realtime();
  send<level>(event, sending_time);
  close();
}

template <std::size_t level, typename T>
void Session::send(T const &event) {
#ifndef NDEBUG
  auto can_send = [&]() {
    if constexpr (std::is_same<T, roq::fix::codec::Logon>::value) {
      return state_ == State::WAITING_CREATE_ROUTE;
    }
    return state_ == State::READY;
  };
  assert(can_send());
#endif
  auto sending_time = clock::get_realtime();
  send<level>(event, sending_time);
}

template <std::size_t level, typename T>
void Session::send(T const &event, std::chrono::nanoseconds sending_time) {
  log::info<level>("send (=> client): {}={}"sv, nameof::nameof_short_type<T>(), event);
  assert(!std::empty(comp_id_));
  auto header = roq::fix::Header{
      .version = FIX_VERSION,
      .msg_type = T::MSG_TYPE,
      .sender_comp_id = shared_.settings.client.comp_id,
      .target_comp_id = comp_id_,
      .msg_seq_num = ++outbound_.msg_seq_num,  // note!
      .sending_time = sending_time,
  };
  auto helper = [&](auto &buffer) {
    auto message = event.encode(header, buffer);
    return std::size(message);
  };
  if ((*connection_).send(helper)) {
  } else {
    log::warn("HERE"sv);
  }
}

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
  if (std::empty(comp_id_)) [[unlikely]]
    comp_id_ = message.header.sender_comp_id;
  switch (message.header.msg_type) {
    using enum roq::fix::MsgType;
    // - session
    case TEST_REQUEST:
      dispatch<roq::fix::codec::TestRequest>(event);
      break;
    case RESEND_REQUEST:
      dispatch<roq::fix::codec::ResendRequest>(event);
      break;
    case REJECT:
      dispatch<roq::fix::codec::Reject>(event);
      break;
    case HEARTBEAT:
      dispatch<roq::fix::codec::Heartbeat>(event);
      break;
      // - authentication
    case LOGON:
      dispatch<roq::fix::codec::Logon>(event);
      break;
    case LOGOUT:
      dispatch<roq::fix::codec::Logout>(event);
      break;
      // - market data
    case TRADING_SESSION_STATUS_REQUEST:
      dispatch<roq::fix::codec::TradingSessionStatusRequest>(event);
      break;
    case SECURITY_LIST_REQUEST:
      dispatch<roq::fix::codec::SecurityListRequest>(event);
      break;
    case SECURITY_DEFINITION_REQUEST:
      dispatch<roq::fix::codec::SecurityDefinitionRequest>(event, decode_buffer_);
      break;
    case SECURITY_STATUS_REQUEST:
      dispatch<roq::fix::codec::SecurityStatusRequest>(event, decode_buffer_);
      break;
    case MARKET_DATA_REQUEST:
      dispatch<roq::fix::codec::MarketDataRequest>(event, decode_buffer_);
      break;
      // - order management
    case ORDER_STATUS_REQUEST:
      dispatch<roq::fix::codec::OrderStatusRequest>(event, decode_buffer_);
      break;
    case ORDER_MASS_STATUS_REQUEST:
      dispatch<roq::fix::codec::OrderMassStatusRequest>(event, decode_buffer_);
      break;
    case NEW_ORDER_SINGLE:
      dispatch<roq::fix::codec::NewOrderSingle>(event, decode_buffer_);
      break;
    case ORDER_CANCEL_REQUEST:
      dispatch<roq::fix::codec::OrderCancelRequest>(event, decode_buffer_);
      break;
    case ORDER_CANCEL_REPLACE_REQUEST:
      dispatch<roq::fix::codec::OrderCancelReplaceRequest>(event, decode_buffer_);
      break;
    case ORDER_MASS_CANCEL_REQUEST:
      dispatch<roq::fix::codec::OrderMassCancelRequest>(event, decode_buffer_);
      break;
      // - position management
    case REQUEST_FOR_POSITIONS:
      dispatch<roq::fix::codec::RequestForPositions>(event, decode_buffer_);
      break;
      // - trade capture
    case TRADE_CAPTURE_REPORT_REQUEST:
      dispatch<roq::fix::codec::TradeCaptureReportRequest>(event, decode_buffer_);
      break;
      // - quotes
    case MASS_QUOTE:
      dispatch<roq::fix::codec::MassQuote>(event, decode_buffer_, decode_buffer_2_);
      break;
    case QUOTE_CANCEL:
      dispatch<roq::fix::codec::QuoteCancel>(event, decode_buffer_);
      break;
    default:
      log::warn("Unexpected: msg_type={}"sv, message.header.msg_type);
      send_business_message_reject(
          message.header,
          {},  // XXX the message could contain a ref_id field, but we don't know what we don't know...
          roq::fix::BusinessRejectReason::UNSUPPORTED_MESSAGE_TYPE,
          roq::fix::codec::Error::UNEXPECTED_MSG_TYPE);
      break;
  };
}

template <typename T, typename... Args>
void Session::dispatch(Trace<roq::fix::Message> const &event, Args &&...args) {
  auto &[trace_info, message] = event;
  auto value = T::create(message, std::forward<Args>(args)...);
  log::info<1>("session_id={}, {}={}"sv, session_id_, nameof::nameof_short_type<T>(), value);
  Trace event_2{trace_info, value};
  (*this)(event_2, message.header);
}

void Session::operator()(Trace<roq::fix::codec::TestRequest> const &event, roq::fix::Header const &header) {
  auto &[trace_info, test_request] = event;
  log::info<1>("test_request={}"sv, test_request);
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case WAITING_CREATE_ROUTE:
    case READY: {
      auto heartbeat = roq::fix::codec::Heartbeat{
          .test_req_id = test_request.test_req_id,
      };
      send<4>(heartbeat);
      break;
    }
    case WAITING_REMOVE_ROUTE:
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::ResendRequest> const &event, roq::fix::Header const &header) {
  auto &[trace_info, resend_request] = event;
  log::info<1>("resend_request={}"sv, resend_request);
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_MSG_TYPE);
      break;
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      assert(false);
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::Reject> const &event, roq::fix::Header const &) {
  auto &[trace_info, reject] = event;
  log::warn("reject={}"sv, reject);
  close();
}

void Session::operator()(Trace<roq::fix::codec::Heartbeat> const &event, roq::fix::Header const &header) {
  auto &[trace_info, heartbeat] = event;
  log::info<1>("heartbeat={}"sv, heartbeat);
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY:
      waiting_for_heartbeat_ = false;
      break;
    case WAITING_REMOVE_ROUTE:
      break;
    case ZOMBIE:
      break;
  }
}

// business

// session

void Session::operator()(Trace<roq::fix::codec::Logon> const &event, roq::fix::Header const &header) {
  auto &trace_info = event.trace_info;
  auto &logon = event.value;
  switch (state_) {
    using enum State;
    case WAITING_LOGON: {
      comp_id_ = header.sender_comp_id;
      // validate: target_comp_id
      if (header.target_comp_id != shared_.settings.client.comp_id) {
        log::error(R"(Unexpected target_comp_id="{}" (expected: "{}"))"sv, header.target_comp_id, shared_.settings.client.comp_id);
        send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::UNKNOWN_TARGET_COMP_ID);
        return;  // note!
      }
      // validate: encrypt_method
      if (logon.encrypt_method != roq::fix::EncryptMethod::NONE) {
        log::error(R"(Unexpected encrypt_method={} (expected: NONE))"sv, logon.encrypt_method);
        send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::INVALID_LOGON_ENCRYPT_METHOD);
        return;  // note!
      }
      // validate: heart_bt_int
      std::chrono::seconds heart_bt_int{logon.heart_bt_int};
      if (heart_bt_int < shared_.settings.client.logon_heartbeat_min || heart_bt_int > shared_.settings.client.logon_heartbeat_max) {
        log::error(
            R"(Unexpected heart_bt_int={} (expected range: [{};{}]"))"sv,
            heart_bt_int,
            shared_.settings.client.logon_heartbeat_min,
            shared_.settings.client.logon_heartbeat_max);
        send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::INVALID_LOGON_HEART_BT_INT);
        return;  // note!
      }
      // validate: reset_seq_num_flag
      if (!logon.reset_seq_num_flag) {
        log::error(R"(Unexpected reset_num_flag={} (expected true)))"sv, logon.reset_seq_num_flag);
        send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::INVALID_LOGON_RESET_SEQ_NUM_FLAG);
        return;  // note!
      }
      // authenticate
      auto success = [&](auto strategy_id) {
        username_ = logon.username;
        party_id_ = fmt::format("{}"sv, strategy_id);
        try {
          auto user_request_id = shared_.create_request_id();
          auto user_request = roq::fix::codec::UserRequest{
              .user_request_id = user_request_id,
              .user_request_type = roq::fix::UserRequestType::LOG_ON_USER,
              .username = party_id_,
              .password = {},
              .new_password = {},
          };
          Trace event_2{trace_info, user_request};
          handler_(event_2, session_id_);
          (*this)(State::WAITING_CREATE_ROUTE);
          auto now = clock::get_system();
          user_response_timeout_ = now + shared_.settings.server.request_timeout;
        } catch (NotReady &e) {
          send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NOT_READY);
        }
      };
      auto failure = [&](auto &reason) {
        log::error("Invalid logon (reason: {})"sv, reason);
        send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::INVALID_LOGON);
      };
      shared_.session_logon(session_id_, header.sender_comp_id, logon.username, logon.password, logon.raw_data, success, failure);
      break;
    }
    case WAITING_CREATE_ROUTE:
    case READY:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::UNEXPECTED_LOGON);
      break;
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::Logout> const &event, roq::fix::Header const &header) {
  auto &[trace_info, logout] = event;
  log::info<1>("logout={}"sv, logout);
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      assert(!std::empty(party_id_));
      auto user_request_id = shared_.create_request_id();
      auto user_request = roq::fix::codec::UserRequest{
          .user_request_id = user_request_id,
          .user_request_type = roq::fix::UserRequestType::LOG_OFF_USER,
          .username = party_id_,
          .password = {},
          .new_password = {},
      };
      Trace event_2{trace_info, user_request};
      handler_(event_2, session_id_);
      (*this)(State::WAITING_REMOVE_ROUTE);
      auto now = clock::get_system();
      user_response_timeout_ = now + shared_.settings.server.request_timeout;
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::TradingSessionStatusRequest> const &event, roq::fix::Header const &header) {
  auto &[trace_info, trading_session_status_request] = event;
  // XXX TODO
  send_business_message_reject(
      header,
      trading_session_status_request.trad_ses_req_id,
      roq::fix::BusinessRejectReason::UNSUPPORTED_MESSAGE_TYPE,
      roq::fix::codec::Error::UNEXPECTED_MSG_TYPE);
}

void Session::operator()(Trace<roq::fix::codec::SecurityListRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &security_list_request = event.value;
      if (!validate_req_id(security_list_request.security_req_id)) {
        send_business_message_reject(
            header, security_list_request.security_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      handler_(event, session_id_);
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::SecurityDefinitionRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &security_definition_request = event.value;
      if (!validate_req_id(security_definition_request.security_req_id)) {
        send_business_message_reject(
            header, security_definition_request.security_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      handler_(event, session_id_);
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::SecurityStatusRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &security_status_request = event.value;
      if (!validate_req_id(security_status_request.security_status_req_id)) {
        send_business_message_reject(
            header, security_status_request.security_status_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      handler_(event, session_id_);
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::MarketDataRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &market_data_request = event.value;
      if (!validate_req_id(market_data_request.md_req_id)) {
        send_business_message_reject(header, market_data_request.md_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_MD_REQ_ID);
        return;
      }
      handler_(event, session_id_);
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::OrderStatusRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &order_status_request = event.value;
      if (!validate_req_id(order_status_request.ord_status_req_id)) {
        send_business_message_reject(
            header, order_status_request.ord_status_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        auto &[trace_info, order_status_request] = event;
        send_business_message_reject(
            header, order_status_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_PARTY_IDS);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::OrderMassStatusRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &order_mass_status_request = event.value;
      if (!validate_req_id(order_mass_status_request.mass_status_req_id)) {
        send_business_message_reject(
            header, order_mass_status_request.mass_status_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        send_business_message_reject(
            header, order_mass_status_request.mass_status_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_PARTY_IDS);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::NewOrderSingle> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &new_order_single = event.value;
      if (!validate_req_id(new_order_single.cl_ord_id)) {
        send_business_message_reject(header, new_order_single.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_CL_ORD_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        send_business_message_reject(header, new_order_single.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_PARTY_IDS);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::OrderCancelRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &order_cancel_request = event.value;
      if (!validate_req_id(order_cancel_request.cl_ord_id)) {
        send_business_message_reject(header, order_cancel_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_CL_ORD_ID);
        return;
      }
      if (!validate_req_id(order_cancel_request.orig_cl_ord_id)) {
        send_business_message_reject(
            header, order_cancel_request.orig_cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_ORIG_CL_ORD_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        auto &[trace_info, order_cancel_request] = event;
        // XXX FIXME should be execution report
        send_business_message_reject(
            header, order_cancel_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_PARTY_IDS);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::OrderCancelReplaceRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &order_cancel_replace_request = event.value;
      if (!validate_req_id(order_cancel_replace_request.cl_ord_id)) {
        send_business_message_reject(
            header, order_cancel_replace_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_CL_ORD_ID);
        return;
      }
      if (!validate_req_id(order_cancel_replace_request.orig_cl_ord_id)) {
        send_business_message_reject(
            header, order_cancel_replace_request.orig_cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_ORIG_CL_ORD_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        auto &[trace_info, order_cancel_replace_request] = event;
        // XXX FIXME should be execution report
        send_business_message_reject(
            header, order_cancel_replace_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_PARTY_IDS);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::OrderMassCancelRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &order_mass_cancel_request = event.value;
      if (!validate_req_id(order_mass_cancel_request.cl_ord_id)) {
        send_business_message_reject(
            header, order_mass_cancel_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_CL_ORD_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        send_business_message_reject(
            header, order_mass_cancel_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_MSG_TYPE);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::RequestForPositions> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &request_for_positions = event.value;
      if (!validate_req_id(request_for_positions.pos_req_id)) {
        send_business_message_reject(header, request_for_positions.pos_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        auto &[trace_info, request_for_positions] = event;
        send_business_message_reject(
            header, request_for_positions.pos_req_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_PARTY_IDS);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::TradeCaptureReportRequest> const &event, roq::fix::Header const &header) {
  switch (state_) {
    using enum State;
    case WAITING_LOGON:
    case WAITING_CREATE_ROUTE:
      send_reject_and_close(header, roq::fix::SessionRejectReason::OTHER, roq::fix::codec::Error::NO_LOGON);
      break;
    case READY: {
      auto &trade_capture_report_request = event.value;
      if (!validate_req_id(trade_capture_report_request.trade_request_id)) {
        send_business_message_reject(
            header, trade_capture_report_request.trade_request_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::INVALID_REQ_ID);
        return;
      }
      if (add_party_ids(event, [&](auto &event_2) { handler_(event_2, session_id_); })) {
      } else {
        auto &[trace_info, trade_capture_report_request] = event;
        send_business_message_reject(
            header, trade_capture_report_request.cl_ord_id, roq::fix::BusinessRejectReason::OTHER, roq::fix::codec::Error::UNSUPPORTED_MSG_TYPE);
      }
      break;
    }
    case WAITING_REMOVE_ROUTE:
      make_zombie();
      break;
    case ZOMBIE:
      break;
  }
}

void Session::operator()(Trace<roq::fix::codec::MassQuote> const &, roq::fix::Header const &) {
  log::fatal("NOT IMPLEMENTED"sv);
}

void Session::operator()(Trace<roq::fix::codec::QuoteCancel> const &, roq::fix::Header const &) {
  log::fatal("NOT IMPLEMENTED"sv);
}

// helpers

void Session::send_reject_and_close(roq::fix::Header const &header, roq::fix::SessionRejectReason session_reject_reason, roq::fix::codec::Error error) {
  auto response = roq::fix::codec::Reject{
      .ref_seq_num = header.msg_seq_num,
      .text = magic_enum::enum_name(error),
      .ref_tag_id = {},
      .ref_msg_type = header.msg_type,
      .session_reject_reason = session_reject_reason,
  };
  log::warn("reject={}"sv, response);
  send_and_close<2>(response);
}

void Session::send_business_message_reject(
    roq::fix::Header const &header, std::string_view const &ref_id, roq::fix::BusinessRejectReason business_reject_reason, roq::fix::codec::Error error) {
  auto response = roq::fix::codec::BusinessMessageReject{
      .ref_seq_num = header.msg_seq_num,
      .ref_msg_type = header.msg_type,                   // required
      .business_reject_ref_id = ref_id,                  // required (sometimes)
      .business_reject_reason = business_reject_reason,  // required
      .text = magic_enum::enum_name(error),
  };
  log::warn("business_message_reject={}"sv, response);
  send<2>(response);
}

template <typename T, typename Callback>
bool Session::add_party_ids(Trace<T> const &event, Callback callback) const {
  assert(!std::empty(party_id_));
  auto &[trace_info, value] = event;
  if (std::empty(value.no_party_ids)) {
    auto party = roq::fix::codec::Party{
        .party_id = party_id_,
        .party_id_source = roq::fix::PartyIDSource::PROPRIETARY_CUSTOM_CODE,
        .party_role = roq::fix::PartyRole::CLIENT_ID,
    };
    auto value_2 = value;
    value_2.no_party_ids = {&party, 1};
    Trace event_2{trace_info, value_2};
    callback(event_2);
    return true;
  }
  return false;
}

}  // namespace client
}  // namespace fix
}  // namespace proxy
}  // namespace roq
