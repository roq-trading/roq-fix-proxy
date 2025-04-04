/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/proxy/fix/controller.hpp"

#include <fmt/core.h>

#include <magic_enum/magic_enum_format.hpp>

#include "roq/event.hpp"
#include "roq/timer.hpp"

#include "roq/exceptions.hpp"

#include "roq/utils/common.hpp"
#include "roq/utils/update.hpp"

#include "roq/fix/map.hpp"

#include "roq/logging.hpp"

using namespace std::literals;

namespace roq {
namespace proxy {
namespace fix {

// === CONSTANTS ===

namespace {
auto const TIMER_FREQUENCY = 100ms;

auto const ORDER_ID_NONE = "NONE"sv;

auto const ERROR_VALIDATION = "VALIDATION"sv;
auto const ERROR_DUPLICATE_CL_ORD_ID = "DUPLICATE_CL_ORD_ID"sv;
auto const ERROR_DUPLICATE_ORD_STATUS_REQ_ID = "DUPLICATE_ORD_STATUS_REQ_ID"sv;
auto const ERROR_DUPLICATE_MASS_STATUS_REQ_ID = "DUPLICATE_MASS_STATUS_REQ_ID"sv;
auto const ERROR_UNKNOWN_SUBSCRIPTION_REQUEST_TYPE = "UNKNOWN_SUBSCRIPTION_REQUEST_TYPE"sv;
auto const ERROR_DUPLICATE_MD_REQ_ID = "DUPLICATE_MD_REQ_ID"sv;
auto const ERROR_UNKNOWN_MD_REQ_ID = "UNKNOWN_MD_REQ_ID"sv;
auto const ERROR_DUPLICATED_POS_REQ_ID = "DUPLICATED_POS_REQ_ID"sv;
auto const ERROR_UNKNOWN_POS_REQ_ID = "UNKNOWN_POS_REQ_ID"sv;
auto const ERROR_DUPLICATE_TRADE_REQUEST_ID = "DUPLICATE_TRADE_REQUEST_ID"sv;
auto const ERROR_UNKNOWN_TRADE_REQUEST_ID = "UNKNOWN_TRADE_REQUEST_ID"sv;
}  // namespace

// === HELPERS ===

namespace {
auto create_auth_session(auto &handler, auto &settings, auto &context) -> std::unique_ptr<auth::Session> {
  if (std::empty(settings.auth.uri))
    return {};
  io::web::URI uri{settings.auth.uri};
  return std::make_unique<auth::Session>(handler, settings, context, uri);
}

auto create_server_session(auto &handler, auto &settings, auto &context, auto &connections) {
  if (std::size(connections) != 1)
    log::fatal("Unexpected: only supporting a single upstream fix-bridge"sv);
  auto &connection = connections[0];
  auto uri = io::web::URI{connection};
  return server::Session{handler, settings, context, uri};
}

template <typename T>
auto get_client_from_parties(T &value) -> std::string_view {
  using value_type = std::remove_cvref<T>::type;
  auto const &party_ids = [&]() {
    if constexpr (std::is_same<value_type, roq::fix::codec::TradeCaptureReport>::value) {
      // assert(!std::empty(value.no_sides));
      return value.no_sides[0].no_party_ids;
    } else {
      return value.no_party_ids;
    }
  }();
  if (std::empty(party_ids))
    return {};
  if (std::size(party_ids) == 1)
    for (auto &item : party_ids) {
      if (!std::empty(item.party_id) && item.party_id_source == roq::fix::PartyIDSource::PROPRIETARY_CUSTOM_CODE &&
          item.party_role == roq::fix::PartyRole::CLIENT_ID)
        return item.party_id;
    }
  log::warn("Unexpected: party_ids=[{}]"sv, fmt::join(party_ids, ", "sv));
  return {};
}

auto create_request_id(std::string_view const &client_id, std::string_view const &cl_ord_id) {
  return fmt::format("proxy-{}:{}"sv, client_id, cl_ord_id);
}

auto get_client_cl_ord_id(auto &cl_ord_id) -> std::string_view {
  if (std::empty(cl_ord_id))
    return cl_ord_id;
  auto pos = cl_ord_id.find(':');
  if (pos != cl_ord_id.npos)
    return cl_ord_id.substr(pos + 1);
  assert(false);
  log::warn(R"(Unexpected: cl_ord_id="{}")"sv, cl_ord_id);
  return cl_ord_id;
}

auto is_order_complete(auto ord_status) {
  return roq::utils::is_order_complete(map(ord_status));
}

auto is_pending(auto exec_type) {
  if (exec_type == roq::fix::ExecType::PENDING_NEW || exec_type == roq::fix::ExecType::PENDING_REPLACE || exec_type == roq::fix::ExecType::PENDING_CANCEL)
    return true;
  return false;
}

auto get_subscription_request_type(auto &event) {
  auto result = event.value.subscription_request_type;
  if (result == roq::fix::SubscriptionRequestType::UNDEFINED)
    result = roq::fix::SubscriptionRequestType::SNAPSHOT;
  return result;
}
}  // namespace

// === IMPLEMENTATION ===

Controller::Controller(Settings const &settings, Config const &config, io::Context &context, std::span<std::string_view const> const &connections)
    : context_{context}, terminate_{context.create_signal(*this, io::sys::Signal::Type::TERMINATE)},
      interrupt_{context.create_signal(*this, io::sys::Signal::Type::INTERRUPT)}, timer_{context.create_timer(*this, TIMER_FREQUENCY)},
      shared_{settings, config}, auth_session_{create_auth_session(*this, settings, context)},
      server_session_{create_server_session(*this, settings, context, connections)}, client_manager_{*this, settings, context, shared_} {
}

void Controller::run() {
  log::info("Event loop is now running"sv);
  Start start;
  dispatch(start);
  (*timer_).resume();
  context_.dispatch();
  Stop stop;
  dispatch(stop);
  log::info("Event loop has terminated"sv);
}

// io::sys::Signal::Handler

void Controller::operator()(io::sys::Signal::Event const &event) {
  log::warn("*** SIGNAL: {} ***"sv, event.type);
  context_.stop();
}

// io::sys::Timer::Handler

void Controller::operator()(io::sys::Timer::Event const &event) {
  auto timer = Timer{
      .now = event.now,
  };
  dispatch(timer);
}

// auth::Session::Handler

void Controller::operator()(auth::Session::Insert const &insert) {
  shared_.add_user(insert.username, insert.password, insert.strategy_id, insert.component);
}

void Controller::operator()(auth::Session::Remove const &remove) {
  shared_.remove_user(remove.username);
}

// server::Session::Handler

void Controller::operator()(Trace<server::Session::Ready> const &) {
  ready_ = true;
}

void Controller::operator()(Trace<server::Session::Disconnected> const &) {
  ready_ = false;
  client_manager_.get_all_sessions([&](auto &session) { session.force_disconnect(); });
  // XXX FIXME clear cl_ord_id_ ???
}

void Controller::operator()(Trace<roq::fix::codec::BusinessMessageReject> const &event) {
  auto dispatch = [&](auto &mapping) {
    auto iter = mapping.server_to_client.find(event.value.business_reject_ref_id);
    if (iter != std::end(mapping.server_to_client)) {
      auto &[session_id, req_id, keep_alive] = (*iter).second;
      auto business_message_reject = event.value;
      // XXX FIXME what about ref_seq_num ???
      business_message_reject.business_reject_ref_id = req_id;
      Trace event_2{event.trace_info, business_message_reject};
      dispatch_to_client(event_2, session_id);
      // XXX FIXME what about keep_alive ???
    }
  };
  switch (event.value.ref_msg_type) {
    using enum roq::fix::MsgType;
    case UNDEFINED:
      break;
    case UNKNOWN:
      break;
    case HEARTBEAT:
      break;
    case TEST_REQUEST:
      break;
    case RESEND_REQUEST:
      break;
    case REJECT:
      break;
    case SEQUENCE_RESET:
      break;
    case LOGOUT:
      break;
    case IOI:
      break;
    case ADVERTISEMENT:
      break;
    case EXECUTION_REPORT:
      break;
    case ORDER_CANCEL_REJECT:
      break;
    case LOGON:
      break;
    case DERIVATIVE_SECURITY_LIST:
      break;
    case NEW_ORDER_MULTILEG:
      break;
    case MULTILEG_ORDER_CANCEL_REPLACE:
      break;
    case TRADE_CAPTURE_REPORT_REQUEST:
      dispatch(subscriptions_.trade_request_id);
      return;  // note!
    case TRADE_CAPTURE_REPORT:
      break;
    case ORDER_MASS_STATUS_REQUEST:
      dispatch(subscriptions_.mass_status_req_id);
      return;  // note!
    case QUOTE_REQUEST_REJECT:
      break;
    case RFQ_REQUEST:
      break;
    case QUOTE_STATUS_REPORT:
      // XXX FIXME TODO
      break;
    case QUOTE_RESPONSE:
      break;
    case CONFIRMATION:
      break;
    case POSITION_MAINTENANCE_REQUEST:
      break;
    case POSITION_MAINTENANCE_REPORT:
      break;
    case REQUEST_FOR_POSITIONS:
      dispatch(subscriptions_.pos_req_id);
      return;  // note!
    case REQUEST_FOR_POSITIONS_ACK:
      break;
    case POSITION_REPORT:
      break;
    case TRADE_CAPTURE_REPORT_REQUEST_ACK:
      break;
    case TRADE_CAPTURE_REPORT_ACK:
      break;
    case ALLOCATION_REPORT:
      break;
    case ALLOCATION_REPORT_ACK:
      break;
    case CONFIRMATION_ACK:
      break;
    case SETTLEMENT_INSTRUCTION_REQUEST:
      break;
    case ASSIGNMENT_REPORT:
      break;
    case COLLATERAL_REQUEST:
      break;
    case COLLATERAL_ASSIGNMENT:
      break;
    case COLLATERAL_RESPONSE:
      break;
    case NEWS:
      break;
    case COLLATERAL_REPORT:
      break;
    case COLLATERAL_INQUIRY:
      break;
    case NETWORK_COUNTERPARTY_SYSTEM_STATUS_REQUEST:
      break;
    case NETWORK_COUNTERPARTY_SYSTEM_STATUS_RESPONSE:
      break;
    case USER_REQUEST:
      break;
    case USER_RESPONSE:
      break;
    case COLLATERAL_INQUIRY_ACK:
      break;
    case CONFIRMATION_REQUEST:
      break;
    case EMAIL:
      break;
    case NEW_ORDER_SINGLE:
      dispatch(subscriptions_.cl_ord_id);
      return;  // note!
    case NEW_ORDER_LIST:
      break;
    case ORDER_CANCEL_REQUEST:
      dispatch(subscriptions_.cl_ord_id);
      return;  // note!
    case ORDER_CANCEL_REPLACE_REQUEST:
      dispatch(subscriptions_.cl_ord_id);
      return;  // note!
    case ORDER_STATUS_REQUEST:
      dispatch(subscriptions_.ord_status_req_id);
      return;  // note!
    case ALLOCATION_INSTRUCTION:
      break;
    case LIST_CANCEL_REQUEST:
      break;
    case LIST_EXECUTE:
      break;
    case LIST_STATUS_REQUEST:
      break;
    case LIST_STATUS:
      break;
    case ALLOCATION_INSTRUCTION_ACK:
      break;
    case DONT_KNOW_TRADE_DK:
      break;
    case QUOTE_REQUEST:
      break;
    case QUOTE:
      break;
    case SETTLEMENT_INSTRUCTIONS:
      break;
    case MARKET_DATA_REQUEST:
      dispatch(subscriptions_.md_req_id);
      return;  // note!
    case MARKET_DATA_SNAPSHOT_FULL_REFRESH:
      dispatch(subscriptions_.md_req_id);
      return;  // note!
    case MARKET_DATA_INCREMENTAL_REFRESH:
      dispatch(subscriptions_.md_req_id);
      return;  // note!
    case MARKET_DATA_REQUEST_REJECT:
      dispatch(subscriptions_.md_req_id);
      return;  // note!
    case QUOTE_CANCEL:
      break;
    case QUOTE_STATUS_REQUEST:
      break;
    case MASS_QUOTE_ACK:
      // XXX FIXME TODO
      break;
    case SECURITY_DEFINITION_REQUEST:
      dispatch(subscriptions_.security_req_id);
      return;  // note!
    case SECURITY_DEFINITION:
      break;
    case SECURITY_STATUS_REQUEST:
      dispatch(subscriptions_.security_status_req_id);
      return;  // note!
    case SECURITY_STATUS:
      break;
    case TRADING_SESSION_STATUS_REQUEST:
      dispatch(subscriptions_.trad_ses_req_id);
      return;  // note!
    case TRADING_SESSION_STATUS:
      break;
    case MASS_QUOTE:
      break;
    case BUSINESS_MESSAGE_REJECT:
      break;
    case BID_REQUEST:
      break;
    case BID_RESPONSE:
      break;
    case LIST_STRIKE_PRICE:
      break;
    case XML_NON_FIX:
      break;
    case REGISTRATION_INSTRUCTIONS:
      break;
    case REGISTRATION_INSTRUCTIONS_RESPONSE:
      break;
    case ORDER_MASS_CANCEL_REQUEST:
      break;
    case ORDER_MASS_CANCEL_REPORT:
      break;
    case NEW_ORDER_CROSS:
      break;
    case CROSS_ORDER_CANCEL_REPLACE_REQUEST:
      break;
    case CROSS_ORDER_CANCEL_REQUEST:
      break;
    case SECURITY_TYPE_REQUEST:
      break;
    case SECURITY_TYPES:
      break;
    case SECURITY_LIST_REQUEST:
      dispatch(subscriptions_.security_req_id);
      return;  // note!
    case SECURITY_LIST:
      break;
    case DERIVATIVE_SECURITY_LIST_REQUEST:
      break;
  }
  // note! must be an internal issue
}

void Controller::operator()(Trace<roq::fix::codec::UserResponse> const &event) {
  auto &user_response = event.value;
  auto iter = subscriptions_.user.server_to_client.find(user_response.user_request_id);
  if (iter != std::end(subscriptions_.user.server_to_client)) {
    auto session_id = (*iter).second;
    if (client_manager_.find(session_id, [&](auto &session) {
          switch (user_response.user_status) {
            using enum roq::fix::UserStatus;
            case LOGGED_IN:
              user_add(user_response.username, session_id);
              break;
            case NOT_LOGGED_IN:
              user_remove(user_response.username, session.ready());
              break;
            default:
              log::warn("Unexpected: user_response={}"sv, user_response);
          }
          subscriptions_.user.client_to_server.erase(session_id);
          subscriptions_.user.server_to_client.erase(iter);
          session(event);
        })) {
    } else {
      // note! clean up whatever the response
      user_remove(user_response.username, false);
    }
  } else {
    log::fatal("Unexpected"sv);
  }
}

void Controller::operator()(Trace<roq::fix::codec::SecurityList> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    auto failure = event.value.security_request_result != roq::fix::SecurityRequestResult::VALID;
    remove = failure || !keep_alive;
    auto security_list = event.value;
    security_list.security_req_id = req_id;
    Trace event_2{event.trace_info, security_list};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.security_req_id;
  auto &mapping = subscriptions_.security_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: security_req_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: security_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::SecurityDefinition> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    auto failure = event.value.security_response_type != roq::fix::SecurityResponseType::ACCEPT_SECURITY_PROPOSAL_AS_IS;
    remove = failure || !keep_alive;
    auto security_definition = event.value;
    security_definition.security_req_id = req_id;
    Trace event_2{event.trace_info, security_definition};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.security_req_id;
  auto &mapping = subscriptions_.security_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: security_req_req_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: security_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::SecurityStatus> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    // note! there is not way to detect a reject
    remove = !keep_alive;
    auto security_status = event.value;
    security_status.security_status_req_id = req_id;
    Trace event_2{event.trace_info, security_status};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.security_status_req_id;
  auto &mapping = subscriptions_.security_status_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: security_status_req_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: security_status_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::MarketDataRequestReject> const &event) {
  auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
    auto market_data_request_reject = event.value;
    market_data_request_reject.md_req_id = req_id;
    Trace event_2{event.trace_info, market_data_request_reject};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.md_req_id;
  auto &mapping = subscriptions_.md_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (!remove_req_id(mapping, req_id))
      log::warn(R"(Internal error: md_req_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: md_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::MarketDataSnapshotFullRefresh> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    remove = !keep_alive;
    auto market_data_snapshot_full_refresh = event.value;
    market_data_snapshot_full_refresh.md_req_id = req_id;
    Trace event_2{event.trace_info, market_data_snapshot_full_refresh};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.md_req_id;
  auto &mapping = subscriptions_.md_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: md_req_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: md_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::MarketDataIncrementalRefresh> const &event) {
  auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
    auto market_data_incremental_refresh = event.value;
    market_data_incremental_refresh.md_req_id = req_id;
    Trace event_2{event.trace_info, market_data_incremental_refresh};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.md_req_id;
  auto &mapping = subscriptions_.md_req_id;
  find_req_id(mapping, req_id, dispatch);
  // note! delivery failure is valid (an unsubscribe request could already have removed md_req_id)
}

void Controller::operator()(Trace<roq::fix::codec::OrderCancelReject> const &event) {
  auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
    auto orig_cl_ord_id = get_client_cl_ord_id(event.value.orig_cl_ord_id);
    auto order_cancel_reject = event.value;
    order_cancel_reject.cl_ord_id = req_id;
    order_cancel_reject.orig_cl_ord_id = orig_cl_ord_id;
    Trace event_2{event.trace_info, order_cancel_reject};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.cl_ord_id;
  auto &mapping = subscriptions_.cl_ord_id;
  find_req_id(mapping, req_id, dispatch);
  if (!remove_req_id(mapping, req_id))
    log::warn(R"(Internal error: cl_ord_id="{}")"sv, req_id);
}

void Controller::operator()(Trace<roq::fix::codec::OrderMassCancelReport> const &event) {
  auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
    auto order_mass_cancel_report = event.value;
    order_mass_cancel_report.cl_ord_id = req_id;
    Trace event_2{event.trace_info, order_mass_cancel_report};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.cl_ord_id;
  auto &mapping = subscriptions_.mass_cancel_cl_ord_id;
  find_req_id(mapping, req_id, dispatch);
  if (!remove_req_id(mapping, req_id))
    log::warn(R"(Internal error: cl_ord_id="{}")"sv, req_id);
}

void Controller::operator()(Trace<roq::fix::codec::ExecutionReport> const &event) {
  auto execution_report = event.value;
  auto cl_ord_id = execution_report.cl_ord_id;
  auto orig_cl_ord_id = execution_report.orig_cl_ord_id;
  auto client_id = get_client_from_parties(execution_report);
  assert(!std::empty(client_id));
  log::debug("client_id={}"sv, client_id);
  execution_report.cl_ord_id = get_client_cl_ord_id(cl_ord_id);
  execution_report.orig_cl_ord_id = get_client_cl_ord_id(orig_cl_ord_id);
  auto has_ord_status_req_id = !std::empty(execution_report.ord_status_req_id);
  auto has_mass_status_req_id = !std::empty(execution_report.mass_status_req_id);
  assert(!(has_ord_status_req_id && has_mass_status_req_id));  // can't have both
  if (has_ord_status_req_id) {                                 // order status request
    if (execution_report.ord_status == roq::fix::OrdStatus::REJECTED) {
      // note! no order
    } else {
      assert(!is_order_complete(execution_report.ord_status));
    }
    assert(execution_report.last_rpt_requested);
    ensure_cl_ord_id(cl_ord_id, execution_report.ord_status);
    auto req_id = execution_report.ord_status_req_id;
    auto &mapping = subscriptions_.ord_status_req_id;
    auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
      assert(std::empty(execution_report.orig_cl_ord_id));
      execution_report.ord_status_req_id = req_id;
      Trace event_2{event.trace_info, execution_report};
      dispatch_to_client(event_2, session_id);
    };
    if (find_req_id(mapping, req_id, dispatch)) {
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: ord_status_req_id="{}")"sv, req_id);
    } else {
      log::warn(R"(Internal error: ord_status_req_id="{}")"sv, req_id);
    }
  } else if (has_mass_status_req_id) {  // order mass status request
    if (execution_report.ord_status == roq::fix::OrdStatus::REJECTED) {
      assert(execution_report.tot_num_reports == 0);
    } else {
      assert(!is_order_complete(execution_report.ord_status));
    }
    ensure_cl_ord_id(cl_ord_id, execution_report.ord_status);
    auto req_id = execution_report.mass_status_req_id;
    auto &mapping = subscriptions_.mass_status_req_id;
    auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
      assert(std::empty(execution_report.orig_cl_ord_id));
      execution_report.mass_status_req_id = req_id;
      Trace event_2{event.trace_info, execution_report};
      dispatch_to_client(event_2, session_id);
    };
    if (find_req_id(mapping, req_id, dispatch)) {
      if (execution_report.last_rpt_requested) {
        if (!remove_req_id(mapping, req_id))
          log::warn(R"(Internal error: mass_status_req_id="{}")"sv, req_id);
      }
    } else {
      log::warn(R"(Internal error: mass_status_req_id="{}")"sv, req_id);
    }
  } else {  // order action request
    auto req_id = cl_ord_id;
    auto &mapping = subscriptions_.cl_ord_id;
    auto pending = is_pending(execution_report.exec_type);
    if (execution_report.exec_type == roq::fix::ExecType::REJECTED) {
      log::debug(R"(REJECT req_id="{}")"sv, req_id);
      auto dispatch = [&](auto session_id, [[maybe_unused]] auto &req_id, [[maybe_unused]] auto keep_alive) {
        assert(execution_report.cl_ord_id == req_id);
        Trace event_2{event.trace_info, execution_report};
        dispatch_to_client(event_2, session_id);
      };
      if (find_req_id(mapping, req_id, dispatch)) {
      } else {
        log::warn(R"(Internal error: req_id="{}")"sv, req_id);  // note! created by another proxy?
      }
    } else {
      log::debug(R"(SUCCESS req_id="{}")"sv, req_id);
      auto done = is_order_complete(execution_report.ord_status);
      if (done) {
        remove_cl_ord_id(cl_ord_id);
      } else {
        if (pending)
          ensure_cl_ord_id(cl_ord_id, execution_report.ord_status);
      }
      if (!pending && !std::empty(orig_cl_ord_id))
        remove_cl_ord_id(orig_cl_ord_id);
      Trace event_2{event.trace_info, execution_report};
      broadcast(event_2, client_id);
    }
    if (!pending)
      remove_req_id(mapping, req_id);  // note! relaxed
  }
}

void Controller::operator()(Trace<roq::fix::codec::RequestForPositionsAck> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, [[maybe_unused]] auto keep_alive) {
    auto failure = event.value.pos_req_result != roq::fix::PosReqResult::VALID || event.value.pos_req_status == roq::fix::PosReqStatus::REJECTED;
    if (failure) {
      remove = true;
      total_num_pos_reports_ = {};
    } else {
      remove = event.value.total_num_pos_reports == 0;  // must await all reports before removing
      total_num_pos_reports_ = event.value.total_num_pos_reports;
      log::warn("Awaiting {} position reports..."sv, total_num_pos_reports_);
    }
    auto position_report = event.value;
    position_report.pos_req_id = req_id;
    Trace event_2{event.trace_info, position_report};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.pos_req_id;
  auto &mapping = subscriptions_.pos_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove) {
      log::info(R"(DEBUG removing pos_req_id="{}")"sv, req_id);
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: pos_req_id="{}")"sv, req_id);
    }
  } else {
    log::warn(R"(Internal error: pos_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::PositionReport> const &event) {
  if (total_num_pos_reports_)
    --total_num_pos_reports_;
  if (!total_num_pos_reports_)
    log::warn("... last position report!"sv);
  auto remove = false;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    auto failure = event.value.pos_req_result != roq::fix::PosReqResult::VALID;
    if (failure) {
      remove = true;
    } else if (!total_num_pos_reports_) {
      remove = !keep_alive;
    }
    auto position_report = event.value;
    position_report.pos_req_id = req_id;
    Trace event_2{event.trace_info, position_report};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.pos_req_id;
  auto &mapping = subscriptions_.pos_req_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    log::info("DEBUG remove={}"sv, remove);
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: pos_req_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: pos_req_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::TradeCaptureReportRequestAck> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    remove = !keep_alive;
    auto trade_capture_report_request_ack = event.value;
    trade_capture_report_request_ack.trade_request_id = req_id;
    Trace event_2{event.trace_info, trade_capture_report_request_ack};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.trade_request_id;
  auto &mapping = subscriptions_.trade_request_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: trade_request_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: trade_request_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::TradeCaptureReport> const &event) {
  auto remove = true;
  auto dispatch = [&](auto session_id, auto &req_id, auto keep_alive) {
    if (!event.value.last_rpt_requested)
      remove = false;
    else
      remove = !keep_alive;
    auto trade_capture_report = event.value;
    trade_capture_report.trade_request_id = req_id;
    Trace event_2{event.trace_info, trade_capture_report};
    dispatch_to_client(event_2, session_id);
  };
  auto req_id = event.value.trade_request_id;
  auto &mapping = subscriptions_.trade_request_id;
  if (find_req_id(mapping, req_id, dispatch)) {
    if (remove)
      if (!remove_req_id(mapping, req_id))
        log::warn(R"(Internal error: trade_request_id="{}")"sv, req_id);
  } else {
    log::warn(R"(Internal error: trade_request_id="{}")"sv, req_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::MassQuoteAck> const &) {
  // XXX FIXME TODO
}

void Controller::operator()(Trace<roq::fix::codec::QuoteStatusReport> const &) {
  // XXX FIXME TODO
}

// client::Session::Handler

void Controller::operator()(Trace<client::Session::Disconnected> const &event, uint64_t session_id) {
  auto unsubscribe_market_data = [&](auto &req_id) {
    if (!ready())
      return;
    auto market_data_request = roq::fix::codec::MarketDataRequest{
        .md_req_id = req_id,
        .subscription_request_type = roq::fix::SubscriptionRequestType::UNSUBSCRIBE,
        .market_depth = {},
        .md_update_type = {},
        .aggregated_book = {},
        .no_md_entry_types = {},  // note! non-standard -- fix-bridge will unsubscribe all
        .no_related_sym = {},
        .no_trading_sessions = {},
        .custom_type = {},
        .custom_value = {},
    };
    Trace event_2{event.trace_info, market_data_request};
    dispatch_to_server(event_2);
  };
  clear_req_ids(subscriptions_.security_req_id, session_id);         // note! subscriptions not yet supported
  clear_req_ids(subscriptions_.security_status_req_id, session_id);  // note! subscriptions not yet supported
  clear_req_ids(subscriptions_.trad_ses_req_id, session_id);         // note! subscriptions not yet supported
  clear_req_ids(subscriptions_.md_req_id, session_id, unsubscribe_market_data);
  clear_req_ids(subscriptions_.ord_status_req_id, session_id);
  clear_req_ids(subscriptions_.mass_status_req_id, session_id);
  clear_req_ids(subscriptions_.pos_req_id, session_id);        // note! subscriptions not yet supported
  clear_req_ids(subscriptions_.trade_request_id, session_id);  // note! subscriptions not yet supported
  clear_req_ids(subscriptions_.cl_ord_id, session_id);
  clear_req_ids(subscriptions_.mass_cancel_cl_ord_id, session_id);
  // user
  auto iter_2 = subscriptions_.user.session_to_client.find(session_id);
  if (iter_2 != std::end(subscriptions_.user.session_to_client)) {
    auto &username_2 = (*iter_2).second;
    if (ready()) {
      auto user_request_id = shared_.create_request_id();
      auto user_request = roq::fix::codec::UserRequest{
          .user_request_id = user_request_id,
          .user_request_type = roq::fix::UserRequestType::LOG_OFF_USER,
          .username = username_2,
          .password = {},
          .new_password = {},
      };
      Trace event_2{event.trace_info, user_request};
      dispatch_to_server(event_2);
      subscriptions_.user.server_to_client.try_emplace(user_request.user_request_id, session_id);
      subscriptions_.user.client_to_server.try_emplace(session_id, user_request.user_request_id);
    }
    // note!
    // there are two scenarios:
    //   we can't send ==> fix-bridge is disconnected so it doesn't matter
    //   we get a response => fix-bridge was connected and we expect it to do the right thing
    // therefore: release immediately to allow the client to reconnect
    log::debug(R"(USER REMOVE client_id="{}" <==> session_id={})"sv, username_2, session_id);
    subscriptions_.user.client_to_session.erase((*iter_2).second);
    subscriptions_.user.session_to_client.erase(iter_2);
  } else {
    log::debug("no user associated with session_id={}"sv, session_id);
  }
}

void Controller::operator()(Trace<roq::fix::codec::UserRequest> const &event, uint64_t session_id) {
  auto &user_request = event.value;
  switch (user_request.user_request_type) {
    using enum roq::fix::UserRequestType;
    case LOG_ON_USER:
      if (user_is_locked(user_request.username))
        throw NotReady{"locked"sv};
      break;
    case LOG_OFF_USER:
      break;
    default:
      log::fatal("Unexpected: user_request={}"sv, user_request);
  }
  auto &tmp = subscriptions_.user.client_to_server[session_id];
  if (std::empty(tmp)) {
    tmp = user_request.user_request_id;
    [[maybe_unused]] auto res = subscriptions_.user.server_to_client.try_emplace(user_request.user_request_id, session_id);
    assert(res.second);
    dispatch_to_server(event);
  } else {
    log::fatal("Unexpected"sv);
  }
}

void Controller::operator()(Trace<roq::fix::codec::SecurityListRequest> const &event, uint64_t session_id) {
  auto &security_list_request = event.value;
  auto req_id = security_list_request.security_req_id;
  auto reject = [&]() {
    auto request_id = shared_.create_request_id();
    auto security_list = roq::fix::codec::SecurityList{
        .security_req_id = req_id,
        .security_response_id = request_id,
        .security_request_result = roq::fix::SecurityRequestResult::INVALID_OR_UNSUPPORTED,
        .no_related_sym = {},
    };
    Trace event_2{event.trace_info, security_list};
    dispatch_to_client(event_2, session_id);
  };
  if (!security_list_request.is_valid()) {
    reject();
    return;
  }
  auto &mapping = subscriptions_.security_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  auto exists = iter != std::end(client_to_server);
  auto subscription_request_type = get_subscription_request_type(event);
  auto dispatch = [&](auto keep_alive) {
    auto request_id = shared_.create_request_id();
    auto security_list_request_2 = security_list_request;
    security_list_request_2.security_req_id = request_id;
    Trace event_2{event.trace_info, security_list_request_2};
    dispatch_to_server(event_2);
    // note! *after* request has been sent
    if (exists) {
      assert(subscription_request_type == roq::fix::SubscriptionRequestType::UNSUBSCRIBE);
      remove_req_id(mapping, request_id);  // note! protocol doesn't have an ack for unsubscribe
    } else {
      assert(
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT ||
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT_UPDATES);
      client_to_server.emplace(req_id, request_id);
      mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
    }
  };
  switch (subscription_request_type) {
    using enum roq::fix::SubscriptionRequestType;
    case UNDEFINED:
    case UNKNOWN:
      reject();
      break;
    case SNAPSHOT:
      if (exists) {
        reject();
      } else {
        dispatch(false);
      }
      break;
    case SNAPSHOT_UPDATES:
      if (exists) {
        reject();
      } else {
        dispatch(true);
      }
      break;
    case UNSUBSCRIBE:
      if (exists) {
        dispatch(false);
      } else {
        reject();
      }
      break;
  }
}

void Controller::operator()(Trace<roq::fix::codec::SecurityDefinitionRequest> const &event, uint64_t session_id) {
  auto &security_definition_request = event.value;
  auto req_id = security_definition_request.security_req_id;
  auto reject = [&]() {
    auto request_id = shared_.create_request_id();
    auto security_definition = roq::fix::codec::SecurityDefinition{
        .security_req_id = security_definition_request.security_req_id,
        .security_response_id = request_id,
        .security_response_type = roq::fix::SecurityResponseType::REJECT_SECURITY_PROPOSAL,
        .symbol = security_definition_request.symbol,
        .contract_multiplier = {},
        .security_exchange = security_definition_request.security_exchange,
        .trading_session_id = {},
        .min_trade_vol = {},
        .min_price_increment = {},
    };
    Trace event_2{event.trace_info, security_definition};
    dispatch_to_client(event_2, session_id);
  };
  if (!security_definition_request.is_valid()) {
    reject();
    return;
  }
  auto &mapping = subscriptions_.security_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  auto exists = iter != std::end(client_to_server);
  auto subscription_request_type = get_subscription_request_type(event);
  auto dispatch = [&](auto keep_alive) {
    auto request_id = shared_.create_request_id();
    auto security_definition_request_2 = security_definition_request;
    security_definition_request_2.security_req_id = request_id;
    Trace event_2{event.trace_info, security_definition_request_2};
    dispatch_to_server(event_2);
    // note! *after* request has been sent
    if (exists) {
      assert(subscription_request_type == roq::fix::SubscriptionRequestType::UNSUBSCRIBE);
      remove_req_id(mapping, request_id);  // note! protocol doesn't have an ack for unsubscribe
    } else {
      assert(
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT ||
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT_UPDATES);
      client_to_server.emplace(req_id, request_id);
      mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
    }
  };
  switch (subscription_request_type) {
    using enum roq::fix::SubscriptionRequestType;
    case UNDEFINED:
    case UNKNOWN:
      reject();
      break;
    case SNAPSHOT:
      if (exists) {
        reject();
      } else {
        dispatch(false);
      }
      break;
    case SNAPSHOT_UPDATES:
      if (exists) {
        reject();
      } else {
        dispatch(true);
      }
      break;
    case UNSUBSCRIBE:
      if (exists) {
        dispatch(false);
      } else {
        reject();
      }
      break;
  }
}

void Controller::operator()(Trace<roq::fix::codec::SecurityStatusRequest> const &event, uint64_t session_id) {
  auto &security_status_request = event.value;
  auto req_id = security_status_request.security_status_req_id;
  auto reject = [&]() {
    // note! protocol doesn't have a proper solution for reject
    auto security_status = roq::fix::codec::SecurityStatus{
        .security_status_req_id = security_status_request.security_status_req_id,
        .symbol = security_status_request.symbol,
        .security_exchange = security_status_request.security_exchange,
        .trading_session_id = {},
        .unsolicited_indicator = false,
        .security_trading_status = {},
    };
    Trace event_2{event.trace_info, security_status};
    dispatch_to_client(event_2, session_id);
  };
  if (!security_status_request.is_valid()) {
    reject();
    return;
  }
  auto &mapping = subscriptions_.security_status_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  auto exists = iter != std::end(client_to_server);
  auto subscription_request_type = get_subscription_request_type(event);
  auto dispatch = [&](auto keep_alive) {
    auto request_id = shared_.create_request_id();
    auto security_status_request_2 = security_status_request;
    security_status_request_2.security_status_req_id = request_id;
    Trace event_2{event.trace_info, security_status_request_2};
    dispatch_to_server(event_2);
    // note! *after* request has been sent
    if (exists) {
      assert(subscription_request_type == roq::fix::SubscriptionRequestType::UNSUBSCRIBE);
      remove_req_id(mapping, request_id);  // note! protocol doesn't have an ack for unsubscribe
    } else {
      assert(
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT ||
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT_UPDATES);
      client_to_server.emplace(req_id, request_id);
      mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
    }
  };
  switch (subscription_request_type) {
    using enum roq::fix::SubscriptionRequestType;
    case UNDEFINED:
    case UNKNOWN:
      reject();
      break;
    case SNAPSHOT:
      if (exists) {
        reject();
      } else {
        dispatch(false);
      }
      break;
    case SNAPSHOT_UPDATES:
      if (exists) {
        reject();
      } else {
        dispatch(true);
      }
      break;
    case UNSUBSCRIBE:
      if (exists) {
        dispatch(false);
      } else {
        reject();
      }
      break;
  }
}

void Controller::operator()(Trace<roq::fix::codec::MarketDataRequest> const &event, uint64_t session_id) {
  auto market_data_request = event.value;
  auto req_id = market_data_request.md_req_id;
  auto reject = [&](auto md_req_rej_reason, auto &text) {
    auto market_data_request_reject = roq::fix::codec::MarketDataRequestReject{
        .md_req_id = req_id,
        .md_req_rej_reason = md_req_rej_reason,
        .text = text,
    };
    Trace event_2{event.trace_info, market_data_request_reject};
    dispatch_to_client(event_2, session_id);
  };
  if (!market_data_request.is_valid()) {
    reject(
        roq::fix::MDReqRejReason::UNSUPPORTED_SCOPE,  // XXX FIXME what to use ???
        ERROR_VALIDATION);
    return;
  }
  auto &mapping = subscriptions_.md_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  auto exists = iter != std::end(client_to_server);
  auto dispatch = [&](auto keep_alive) {
    auto request_id = shared_.create_request_id();
    auto market_data_request_2 = market_data_request;
    market_data_request_2.md_req_id = request_id;
    Trace event_2{event.trace_info, market_data_request_2};
    dispatch_to_server(event_2);
    // note! *after* request has been sent
    if (exists) {
      assert(market_data_request.subscription_request_type == roq::fix::SubscriptionRequestType::UNSUBSCRIBE);
      remove_req_id(mapping, request_id);  // note! protocol doesn't have an ack for unsubscribe
    } else {
      assert(
          market_data_request.subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT ||
          market_data_request.subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT_UPDATES);
      client_to_server.emplace(req_id, request_id);
      mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
    }
  };
  switch (market_data_request.subscription_request_type) {
    using enum roq::fix::SubscriptionRequestType;
    case UNDEFINED:
    case UNKNOWN:
      reject(roq::fix::MDReqRejReason::UNSUPPORTED_SUBSCRIPTION_REQUEST_TYPE, ERROR_UNKNOWN_SUBSCRIPTION_REQUEST_TYPE);
      break;
    case SNAPSHOT:
      if (exists) {
        reject(roq::fix::MDReqRejReason::DUPLICATE_MD_REQ_ID, ERROR_DUPLICATE_MD_REQ_ID);
      } else {
        dispatch(false);
      }
      break;
    case SNAPSHOT_UPDATES:
      if (exists) {
        reject(roq::fix::MDReqRejReason::DUPLICATE_MD_REQ_ID, ERROR_DUPLICATE_MD_REQ_ID);
      } else {
        dispatch(true);
      }
      break;
    case UNSUBSCRIBE:
      if (exists) {
        dispatch(false);
      } else {
        reject(
            roq::fix::MDReqRejReason::UNSUPPORTED_SUBSCRIPTION_REQUEST_TYPE,  // XXX FIXME what to use ???
            ERROR_UNKNOWN_MD_REQ_ID);
      }
      break;
  }
}

void Controller::operator()(Trace<roq::fix::codec::OrderStatusRequest> const &event, uint64_t session_id) {
  auto &order_status_request = event.value;
  auto reject = [&](auto ord_rej_reason, auto &text) {
    auto request_id = shared_.create_request_id();
    auto execution_report = roq::fix::codec::ExecutionReport{
        .order_id = request_id,  // required
        .secondary_cl_ord_id = {},
        .cl_ord_id = order_status_request.cl_ord_id,
        .orig_cl_ord_id = {},
        .ord_status_req_id = order_status_request.ord_status_req_id,
        .mass_status_req_id = {},
        .tot_num_reports = 0,  // note!
        .last_rpt_requested = true,
        .no_party_ids = order_status_request.no_party_ids,
        .exec_id = request_id,                          // required
        .exec_type = roq::fix::ExecType::ORDER_STATUS,  // required
        .ord_status = roq::fix::OrdStatus::REJECTED,    // required
        .working_indicator = {},
        .ord_rej_reason = ord_rej_reason,  // note!
        .account = order_status_request.account,
        .account_type = {},
        .symbol = order_status_request.symbol,                        // required
        .security_exchange = order_status_request.security_exchange,  // required
        .side = order_status_request.side,                            // required
        .order_qty = {},
        .price = {},
        .stop_px = {},
        .currency = {},
        .time_in_force = {},
        .exec_inst = {},
        .last_qty = {},
        .last_px = {},
        .trading_session_id = {},
        .leaves_qty = {0.0, {}},  // required
        .cum_qty = {0.0, {}},     // required
        .avg_px = {0.0, {}},      // required
        .transact_time = {},
        .position_effect = {},
        .max_show = {},
        .text = text,
        .last_liquidity_ind = {},
    };
    Trace event_2{event.trace_info, execution_report};
    dispatch_to_client(event_2, session_id);
  };
  if (!order_status_request.is_valid()) {
    reject(roq::fix::OrdRejReason::OTHER, ERROR_VALIDATION);
    return;
  }
  auto req_id = order_status_request.ord_status_req_id;
  auto &mapping = subscriptions_.ord_status_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  if (!std::empty(req_id)) {  // note! optional
    auto iter = client_to_server.find(req_id);
    if (iter != std::end(client_to_server)) {
      reject(roq::fix::OrdRejReason::OTHER, ERROR_DUPLICATE_ORD_STATUS_REQ_ID);
      return;
    }
  }
  auto client_id = get_client_from_parties(order_status_request);
  auto request_id = create_request_id(client_id, req_id);
  auto cl_ord_id = create_request_id(client_id, order_status_request.cl_ord_id);
  auto order_status_request_2 = order_status_request;
  order_status_request_2.ord_status_req_id = request_id;
  order_status_request_2.cl_ord_id = cl_ord_id;
  Trace event_2{event.trace_info, order_status_request_2};
  dispatch_to_server(event_2);
  // note! *after* request has been sent
  if (!std::empty(req_id))  // note! optional
    client_to_server.emplace(req_id, request_id);
  mapping.server_to_client.try_emplace(request_id, session_id, req_id, false);
}

void Controller::operator()(Trace<roq::fix::codec::NewOrderSingle> const &event, uint64_t session_id) {
  auto &new_order_single = event.value;
  auto reject = [&](auto ord_rej_reason, auto &text) {
    log::warn(R"(DEBUG: REJECT ord_rej_reason={}, text="{}")"sv, ord_rej_reason, text);
    auto request_id = shared_.create_request_id();
    auto execution_report = roq::fix::codec::ExecutionReport{
        .order_id = request_id,  // required
        .secondary_cl_ord_id = {},
        .cl_ord_id = new_order_single.cl_ord_id,
        .orig_cl_ord_id = {},
        .ord_status_req_id = {},
        .mass_status_req_id = {},
        .tot_num_reports = {},
        .last_rpt_requested = {},
        .no_party_ids = new_order_single.no_party_ids,
        .exec_id = request_id,                          // required
        .exec_type = roq::fix::ExecType::ORDER_STATUS,  // required
        .ord_status = roq::fix::OrdStatus::REJECTED,    // required
        .working_indicator = {},
        .ord_rej_reason = ord_rej_reason,  // note!
        .account = new_order_single.account,
        .account_type = {},
        .symbol = new_order_single.symbol,                        // required
        .security_exchange = new_order_single.security_exchange,  // required
        .side = new_order_single.side,                            // required
        .order_qty = new_order_single.order_qty,
        .price = new_order_single.price,
        .stop_px = new_order_single.stop_px,
        .currency = {},
        .time_in_force = new_order_single.time_in_force,
        .exec_inst = new_order_single.exec_inst,
        .last_qty = {},
        .last_px = {},
        .trading_session_id = {},
        .leaves_qty = {0.0, {}},  // required
        .cum_qty = {0.0, {}},     // required
        .avg_px = {0.0, {}},      // required
        .transact_time = {},
        .position_effect = {},
        .max_show = {},
        .text = text,
        .last_liquidity_ind = {},
    };
    Trace event_2{event.trace_info, execution_report};
    dispatch_to_client(event_2, session_id);
  };
  if (!new_order_single.is_valid()) {
    reject(roq::fix::OrdRejReason::OTHER, ERROR_VALIDATION);
    return;
  }
  auto req_id = new_order_single.cl_ord_id;
  auto &mapping = subscriptions_.cl_ord_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto client_id = get_client_from_parties(new_order_single);
  auto iter = client_to_server.find(req_id);
  if (iter != std::end(client_to_server)) {
    reject(roq::fix::OrdRejReason::OTHER, ERROR_DUPLICATE_CL_ORD_ID);
    return;
  }
  auto request_id = create_request_id(client_id, new_order_single.cl_ord_id);
  auto new_order_single_2 = new_order_single;
  new_order_single_2.cl_ord_id = request_id;
  Trace event_2{event.trace_info, new_order_single_2};
  dispatch_to_server(event_2);
  // note! *after* request has been sent
  add_req_id(mapping, req_id, request_id, session_id, true);
}

void Controller::operator()(Trace<roq::fix::codec::OrderCancelReplaceRequest> const &event, uint64_t session_id) {
  auto &order_cancel_replace_request = event.value;
  auto reject = [&](auto &order_id, auto ord_status, auto cxl_rej_reason, auto &text) {
    log::warn(R"(DEBUG: REJECT order_id="{}", ord_status={}, cxl_rej_reason={}, text="{}")"sv, order_id, ord_status, cxl_rej_reason, text);
    auto order_cancel_reject = roq::fix::codec::OrderCancelReject{
        .order_id = order_id,  // required
        .secondary_cl_ord_id = {},
        .cl_ord_id = order_cancel_replace_request.cl_ord_id,            // required
        .orig_cl_ord_id = order_cancel_replace_request.orig_cl_ord_id,  // required
        .ord_status = ord_status,                                       // required
        .working_indicator = {},
        .account = order_cancel_replace_request.account,
        .cxl_rej_response_to = roq::fix::CxlRejResponseTo::ORDER_CANCEL_REPLACE_REQUEST,  // required
        .cxl_rej_reason = cxl_rej_reason,
        .text = text,
    };
    Trace event_2{event.trace_info, order_cancel_reject};
    dispatch_to_client(event_2, session_id);
  };
  if (!order_cancel_replace_request.is_valid()) {
    reject(ORDER_ID_NONE, roq::fix::OrdStatus::REJECTED, roq::fix::CxlRejReason::OTHER, ERROR_VALIDATION);
    return;
  }
  auto req_id = order_cancel_replace_request.cl_ord_id;
  auto &mapping = subscriptions_.cl_ord_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto client_id = get_client_from_parties(order_cancel_replace_request);
  auto iter = client_to_server.find(req_id);
  if (iter != std::end(client_to_server)) {
    reject(
        ORDER_ID_NONE,
        roq::fix::OrdStatus::REJECTED,  // XXX FIXME should be latest "known"
        roq::fix::CxlRejReason::DUPLICATE_CL_ORD_ID,
        ERROR_DUPLICATE_CL_ORD_ID);
    return;
  }
  auto request_id = create_request_id(client_id, req_id);
  auto orig_cl_ord_id = create_request_id(client_id, order_cancel_replace_request.orig_cl_ord_id);
  auto order_cancel_replace_request_2 = order_cancel_replace_request;
  order_cancel_replace_request_2.cl_ord_id = request_id;
  order_cancel_replace_request_2.orig_cl_ord_id = orig_cl_ord_id;
  Trace event_2{event.trace_info, order_cancel_replace_request_2};
  dispatch_to_server(event_2);
  // note! *after* request has been sent
  client_to_server.emplace(req_id, request_id);
  mapping.server_to_client.try_emplace(request_id, session_id, req_id, true);
}

void Controller::operator()(Trace<roq::fix::codec::OrderCancelRequest> const &event, uint64_t session_id) {
  auto &order_cancel_request = event.value;
  auto reject = [&](auto &order_id, auto ord_status, auto cxl_rej_reason, auto &text) {
    log::warn(R"(DEBUG: REJECT order_id="{}", ord_status={}, cxl_rej_reason={}, text="{}")"sv, order_id, ord_status, cxl_rej_reason, text);
    auto order_cancel_reject = roq::fix::codec::OrderCancelReject{
        .order_id = order_id,  // required
        .secondary_cl_ord_id = {},
        .cl_ord_id = order_cancel_request.cl_ord_id,            // required
        .orig_cl_ord_id = order_cancel_request.orig_cl_ord_id,  // required
        .ord_status = ord_status,                               // required
        .working_indicator = {},
        .account = order_cancel_request.account,
        .cxl_rej_response_to = roq::fix::CxlRejResponseTo::ORDER_CANCEL_REQUEST,  // required
        .cxl_rej_reason = cxl_rej_reason,
        .text = text,
    };
    Trace event_2{event.trace_info, order_cancel_reject};
    dispatch_to_client(event_2, session_id);
  };
  if (!order_cancel_request.is_valid()) {
    reject(ORDER_ID_NONE, roq::fix::OrdStatus::REJECTED, roq::fix::CxlRejReason::OTHER, ERROR_VALIDATION);
    return;
  }
  auto req_id = order_cancel_request.cl_ord_id;
  auto &mapping = subscriptions_.cl_ord_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto client_id = get_client_from_parties(order_cancel_request);
  auto iter = client_to_server.find(req_id);
  if (iter != std::end(client_to_server)) {
    reject(
        ORDER_ID_NONE,
        roq::fix::OrdStatus::REJECTED,  // XXX FIXME should be latest "known"
        roq::fix::CxlRejReason::DUPLICATE_CL_ORD_ID,
        ERROR_DUPLICATE_ORD_STATUS_REQ_ID);
    return;
  }
  auto request_id = create_request_id(client_id, order_cancel_request.cl_ord_id);
  auto orig_cl_ord_id = create_request_id(client_id, order_cancel_request.orig_cl_ord_id);
  auto order_cancel_request_2 = order_cancel_request;
  order_cancel_request_2.cl_ord_id = request_id;
  order_cancel_request_2.orig_cl_ord_id = orig_cl_ord_id;
  Trace event_2{event.trace_info, order_cancel_request_2};
  dispatch_to_server(event_2);
  // note! *after* request has been sent
  client_to_server.emplace(req_id, request_id);
  mapping.server_to_client.try_emplace(request_id, session_id, req_id, true);
}

void Controller::operator()(Trace<roq::fix::codec::OrderMassStatusRequest> const &event, uint64_t session_id) {
  auto &order_mass_status_request = event.value;
  auto reject = [&](auto ord_rej_reason, auto &text) {
    auto request_id = shared_.create_request_id();
    auto execution_report = roq::fix::codec::ExecutionReport{
        .order_id = request_id,  // required
        .secondary_cl_ord_id = {},
        .cl_ord_id = {},
        .orig_cl_ord_id = {},
        .ord_status_req_id = {},
        .mass_status_req_id = order_mass_status_request.mass_status_req_id,
        .tot_num_reports = 0,  // note!
        .last_rpt_requested = true,
        .no_party_ids = order_mass_status_request.no_party_ids,
        .exec_id = request_id,                          // required
        .exec_type = roq::fix::ExecType::ORDER_STATUS,  // required
        .ord_status = roq::fix::OrdStatus::REJECTED,    // required
        .working_indicator = {},
        .ord_rej_reason = ord_rej_reason,  // note!
        .account = order_mass_status_request.account,
        .account_type = {},
        .symbol = order_mass_status_request.symbol,                        // required
        .security_exchange = order_mass_status_request.security_exchange,  // required
        .side = order_mass_status_request.side,                            // required
        .order_qty = {},
        .price = {},
        .stop_px = {},
        .currency = {},
        .time_in_force = {},
        .exec_inst = {},
        .last_qty = {},
        .last_px = {},
        .trading_session_id = {},
        .leaves_qty = {0.0, {}},  // required
        .cum_qty = {0.0, {}},     // required
        .avg_px = {0.0, {}},      // required
        .transact_time = {},
        .position_effect = {},
        .max_show = {},
        .text = text,
        .last_liquidity_ind = {},
    };
    Trace event_2{event.trace_info, execution_report};
    dispatch_to_client(event_2, session_id);
  };
  if (!order_mass_status_request.is_valid()) {
    reject(roq::fix::OrdRejReason::OTHER, ERROR_VALIDATION);
    return;
  }
  auto req_id = order_mass_status_request.mass_status_req_id;
  auto &mapping = subscriptions_.mass_status_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  if (iter != std::end(client_to_server)) {
    reject(roq::fix::OrdRejReason::OTHER, ERROR_DUPLICATE_MASS_STATUS_REQ_ID);
    return;
  }
  auto client_id = get_client_from_parties(order_mass_status_request);
  auto request_id = create_request_id(client_id, req_id);
  auto order_mass_status_request_2 = order_mass_status_request;
  order_mass_status_request_2.mass_status_req_id = request_id;
  Trace event_2{event.trace_info, order_mass_status_request_2};
  dispatch_to_server(event_2);
  // note! *after* request has been sent
  client_to_server.emplace(req_id, request_id);
  mapping.server_to_client.try_emplace(request_id, session_id, req_id, false);
}

void Controller::operator()(Trace<roq::fix::codec::OrderMassCancelRequest> const &event, uint64_t session_id) {
  auto &order_mass_cancel_request = event.value;
  auto reject = [&](auto order_mass_reject_reason, auto &text) {
    auto order_mass_cancel_report = roq::fix::codec::OrderMassCancelReport{
        .cl_ord_id = order_mass_cancel_request.cl_ord_id,
        .order_id = order_mass_cancel_request.cl_ord_id,                                 // required
        .mass_cancel_request_type = order_mass_cancel_request.mass_cancel_request_type,  // required
        .mass_cancel_response = roq::fix::MassCancelResponse::CANCEL_REQUEST_REJECTED,   // required
        .mass_cancel_reject_reason = order_mass_reject_reason,
        .total_affected_orders = {},
        .symbol = order_mass_cancel_request.symbol,
        .security_exchange = order_mass_cancel_request.security_exchange,
        .side = order_mass_cancel_request.side,
        .text = text,
        .no_party_ids = order_mass_cancel_request.no_party_ids,
    };
    Trace event_2{event.trace_info, order_mass_cancel_report};
    dispatch_to_client(event_2, session_id);
  };
  if (!order_mass_cancel_request.is_valid()) {
    reject(roq::fix::MassCancelRejectReason::OTHER, ERROR_VALIDATION);
    return;
  }
  auto req_id = order_mass_cancel_request.cl_ord_id;
  auto &mapping = subscriptions_.mass_cancel_cl_ord_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  if (iter != std::end(client_to_server)) {
    reject(roq::fix::MassCancelRejectReason::OTHER, ERROR_DUPLICATE_CL_ORD_ID);
    return;
  }
  auto client_id = get_client_from_parties(order_mass_cancel_request);
  auto request_id = create_request_id(client_id, req_id);
  auto order_mass_cancel_request_2 = order_mass_cancel_request;
  order_mass_cancel_request_2.cl_ord_id = request_id;
  Trace event_2{event.trace_info, order_mass_cancel_request_2};
  dispatch_to_server(event_2);
  // note! *after* request has been sent
  client_to_server.emplace(req_id, request_id);
  mapping.server_to_client.try_emplace(request_id, session_id, req_id, false);
}

void Controller::operator()(Trace<roq::fix::codec::RequestForPositions> const &event, uint64_t session_id) {
  auto &request_for_positions = event.value;
  auto req_id = request_for_positions.pos_req_id;
  auto reject = [&](auto &text) {
    auto request_id = shared_.create_request_id();
    auto request_for_positions_ack = roq::fix::codec::RequestForPositionsAck{
        .pos_maint_rpt_id = request_id,  // required
        .pos_req_id = req_id,
        .total_num_pos_reports = {},
        .unsolicited_indicator = false,
        .pos_req_result = roq::fix::PosReqResult::INVALID_OR_UNSUPPORTED,  // required
        .pos_req_status = roq::fix::PosReqStatus::REJECTED,                // required
        .no_party_ids = request_for_positions.no_party_ids,                // required
        .account = request_for_positions.account,                          // required
        .account_type = request_for_positions.account_type,                // required
        .text = text,
    };
    Trace event_2{event.trace_info, request_for_positions_ack};
    dispatch_to_client(event_2, session_id);
  };
  if (!request_for_positions.is_valid()) {
    reject(ERROR_VALIDATION);
    return;
  }
  auto &mapping = subscriptions_.pos_req_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  auto exists = iter != std::end(client_to_server);
  auto subscription_request_type = get_subscription_request_type(event);
  auto dispatch = [&](auto keep_alive) {
    auto request_id = exists ? (*iter).second : shared_.create_request_id();
    auto request_for_positions_2 = request_for_positions;
    request_for_positions_2.pos_req_id = request_id;
    Trace event_2{event.trace_info, request_for_positions_2};
    dispatch_to_server(event_2);
    // note! *after* request has been sent
    if (exists) {
      assert(subscription_request_type == roq::fix::SubscriptionRequestType::UNSUBSCRIBE);  // see below
      auto iter = mapping.server_to_client.find(request_id);
      if (iter != std::end(mapping.server_to_client)) {
        auto &[session_id_2, req_id_2, keep_alive_2] = (*iter).second;
        keep_alive_2 = keep_alive;
      } else {
        log::fatal("Unexpected"sv);
      }
    } else {
      assert(
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT ||
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT_UPDATES);  // see below
      client_to_server.emplace(req_id, request_id);
      mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
    }
  };
  switch (subscription_request_type) {
    using enum roq::fix::SubscriptionRequestType;
    case UNDEFINED:
    case UNKNOWN:
      reject(ERROR_UNKNOWN_SUBSCRIPTION_REQUEST_TYPE);
      break;
    case SNAPSHOT:
      if (exists) {
        reject(ERROR_DUPLICATED_POS_REQ_ID);
      } else {
        dispatch(false);
      }
      break;
    case SNAPSHOT_UPDATES:
      if (exists) {
        reject(ERROR_DUPLICATED_POS_REQ_ID);
      } else {
        dispatch(true);
      }
      break;
    case UNSUBSCRIBE:
      if (exists) {
        dispatch(false);
      } else {
        reject(ERROR_UNKNOWN_POS_REQ_ID);
      }
      break;
  }
}

void Controller::operator()(Trace<roq::fix::codec::TradeCaptureReportRequest> const &event, uint64_t session_id) {
  auto &trade_capture_report_request = event.value;
  auto req_id = trade_capture_report_request.trade_request_id;
  auto reject = [&](auto &text) {
    auto request_id = shared_.create_request_id();
    auto trade_capture_report_request_ack = roq::fix::codec::TradeCaptureReportRequestAck{
        .trade_request_id = req_id,                                             // required
        .trade_request_type = trade_capture_report_request.trade_request_type,  // required
        .trade_request_result = roq::fix::TradeRequestResult::OTHER,            // required
        .trade_request_status = roq::fix::TradeRequestStatus::REJECTED,         // required
        .symbol = trade_capture_report_request.symbol,                          // required
        .security_exchange = trade_capture_report_request.security_exchange,    // required
        .text = text,
    };
    Trace event_2{event.trace_info, trade_capture_report_request_ack};
    dispatch_to_client(event_2, session_id);
  };
  if (!trade_capture_report_request.is_valid()) {
    reject(ERROR_VALIDATION);
    return;
  }
  auto &mapping = subscriptions_.trade_request_id;
  auto &client_to_server = mapping.client_to_server[session_id];
  auto iter = client_to_server.find(req_id);
  auto exists = iter != std::end(client_to_server);
  auto subscription_request_type = get_subscription_request_type(event);
  auto dispatch = [&](auto keep_alive) {
    auto client_id = get_client_from_parties(trade_capture_report_request);
    auto request_id = create_request_id(client_id, req_id);
    auto trade_capture_report_request_2 = trade_capture_report_request;
    trade_capture_report_request_2.trade_request_id = request_id;
    Trace event_2{event.trace_info, trade_capture_report_request_2};
    dispatch_to_server(event_2);
    // note! *after* request has been sent
    if (exists) {
      assert(subscription_request_type == roq::fix::SubscriptionRequestType::UNSUBSCRIBE);
      remove_req_id(mapping, request_id);  // note! protocol doesn't have an ack for unsubscribe
    } else {
      assert(
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT ||
          subscription_request_type == roq::fix::SubscriptionRequestType::SNAPSHOT_UPDATES);
      client_to_server.emplace(req_id, request_id);
      mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
    }
  };
  switch (subscription_request_type) {
    using enum roq::fix::SubscriptionRequestType;
    case UNDEFINED:
    case UNKNOWN:
      reject(ERROR_UNKNOWN_SUBSCRIPTION_REQUEST_TYPE);
      break;
    case SNAPSHOT:
      if (exists) {
        reject(ERROR_DUPLICATE_TRADE_REQUEST_ID);
      } else {
        dispatch(false);
      }
      break;
    case SNAPSHOT_UPDATES:
      if (exists) {
        reject(ERROR_DUPLICATE_TRADE_REQUEST_ID);
      } else {
        dispatch(true);
      }
      break;
    case UNSUBSCRIBE:
      if (exists) {
        dispatch(false);
      } else {
        reject(ERROR_UNKNOWN_TRADE_REQUEST_ID);
      }
      break;
  }
}

void Controller::operator()(Trace<roq::fix::codec::MassQuote> const &, [[maybe_unused]] uint64_t session_id) {
  // XXX FIXME TODO
}

void Controller::operator()(Trace<roq::fix::codec::QuoteCancel> const &, [[maybe_unused]] uint64_t session_id) {
  // XXX FIXME TODO
}

// utilities

template <typename... Args>
void Controller::dispatch(Args &&...args) {
  MessageInfo message_info;
  Event event{message_info, std::forward<Args>(args)...};
  if (static_cast<bool>(auth_session_))
    (*auth_session_)(event);
  server_session_(event);
  client_manager_(event);
}

template <typename T>
void Controller::dispatch_to_server(Trace<T> const &event) {
  server_session_(event);
}

template <typename T>
bool Controller::dispatch_to_client(Trace<T> const &event, uint64_t session_id) {
  auto success = false;
  client_manager_.find(session_id, [&](auto &session) {
    session(event);
    success = true;
  });
  if (!success)
    log::warn<0>("Undeliverable: session_id={}"sv, session_id);
  return success;
}

template <typename T>
void Controller::broadcast(Trace<T> const &event, std::string_view const &client_id) {
  auto iter = subscriptions_.user.client_to_session.find(client_id);
  if (iter == std::end(subscriptions_.user.client_to_session))
    return;
  auto session_id = (*iter).second;
  client_manager_.find(session_id, [&](auto &session) { session(event); });
}

// req_id

template <typename Callback>
bool Controller::find_req_id(auto &mapping, std::string_view const &req_id, Callback callback) {
  auto iter = mapping.server_to_client.find(req_id);
  if (iter == std::end(mapping.server_to_client))
    return false;
  auto &[session_id, client_req_id, keep_alive] = (*iter).second;
  callback(session_id, client_req_id, keep_alive);
  return true;
}

void Controller::add_req_id(auto &mapping, std::string_view const &req_id, std::string_view const &request_id, uint64_t session_id, bool keep_alive) {
  auto &client_to_server = mapping.client_to_server[session_id];
  client_to_server.emplace(req_id, request_id);
  mapping.server_to_client.try_emplace(request_id, session_id, req_id, keep_alive);
}

bool Controller::remove_req_id(auto &mapping, std::string_view const &req_id) {
  if (std::empty(req_id))
    return true;
  auto iter_1 = mapping.server_to_client.find(req_id);
  if (iter_1 == std::end(mapping.server_to_client))
    return false;
  auto &[session_id, client_req_id, keep_alive] = (*iter_1).second;
  auto iter_2 = mapping.client_to_server.find(session_id);
  if (iter_2 != std::end(mapping.client_to_server)) {
    log::warn(R"(DEBUG: REMOVE req_id(client)="{} <==> req_id(server)="{}")"sv, client_req_id, req_id);
    (*iter_2).second.erase(client_req_id);
    if (std::empty((*iter_2).second))
      mapping.client_to_server.erase(iter_2);
  }
  log::warn(R"(DEBUG: REMOVE req_id(server)="{}")"sv, req_id);
  mapping.server_to_client.erase(iter_1);
  return true;
}

template <typename Callback>
void Controller::clear_req_ids(auto &mapping, uint64_t session_id, Callback callback) {
  auto iter = mapping.client_to_server.find(session_id);
  if (iter == std::end(mapping.client_to_server))
    return;
  auto &tmp = (*iter).second;
  for (auto &[_, req_id] : tmp) {
    callback(req_id);
    mapping.server_to_client.erase(req_id);
  }
  mapping.client_to_server.erase(iter);
}

// cl_ord_id

void Controller::ensure_cl_ord_id(std::string_view const &cl_ord_id, roq::fix::OrdStatus ord_status) {
  if (std::empty(cl_ord_id))
    return;
  auto iter = cl_ord_id_.state.find(cl_ord_id);
  if (iter == std::end(cl_ord_id_.state)) {
    log::warn(R"(DEBUG: ADD cl_ord_id(server)="{}" ==> {})"sv, cl_ord_id, ord_status);
    [[maybe_unused]] auto res = cl_ord_id_.state.emplace(cl_ord_id, ord_status);
    assert(res.second);
  } else {
    if (utils::update((*iter).second, ord_status))
      log::warn(R"(DEBUG: UPDATE cl_ord_id(server)="{}" ==> {})"sv, cl_ord_id, ord_status);
  }
}

void Controller::remove_cl_ord_id(std::string_view const &cl_ord_id) {
  if (std::empty(cl_ord_id))
    return;
  if (shared_.settings.test.disable_remove_cl_ord_id)
    return;
  auto iter = cl_ord_id_.state.find(cl_ord_id);
  if (iter != std::end(cl_ord_id_.state)) {
    log::warn(R"(DEBUG: REMOVE cl_ord_id(server)="{}")"sv, cl_ord_id);
    cl_ord_id_.state.erase(iter);
  }
}

// user

void Controller::user_add(std::string_view const &username, uint64_t session_id) {
  log::info(R"(DEBUG: USER ADD client_id="{}" <==> session_id={})"sv, username, session_id);
  auto res_1 = subscriptions_.user.client_to_session.try_emplace(username, session_id).second;
  if (!res_1)
    log::fatal("Unexpected"sv);
  auto res_2 = subscriptions_.user.session_to_client.try_emplace(session_id, username).second;
  if (!res_2)
    log::fatal("Unexpected"sv);
}

void Controller::user_remove(std::string_view const &username, bool ready) {
  auto iter = subscriptions_.user.client_to_session.find(username);
  if (iter != std::end(subscriptions_.user.client_to_session)) {
    auto session_id = (*iter).second;
    log::info(R"(DEBUG: USER REMOVE client_id="{}" <==> session_id={})"sv, username, session_id);
    subscriptions_.user.session_to_client.erase(session_id);
    subscriptions_.user.client_to_session.erase(iter);
  } else if (ready) {
    // note! disconnect doesn't wait before cleaning up the resources
    log::fatal(R"(Unexpected: client_id="{}")"sv, username);
  }
}

bool Controller::user_is_locked(std::string_view const &username) const {
  auto iter = subscriptions_.user.client_to_session.find(username);
  return iter != std::end(subscriptions_.user.client_to_session);
}

}  // namespace fix
}  // namespace proxy
}  // namespace roq
