/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/settings.hpp"

#include "roq/fix_proxy/flags/flags.hpp"

using namespace std::chrono_literals;  // NOLINT

namespace roq {
namespace fix_proxy {

// === CONSTANTS ===

namespace {
auto CONNECTION_TIMEOUT = 5s;
auto TLS_VALIDATE_CERTIFICATE = false;
}  // namespace

// === IMPLEMENTATION ===

Settings Settings::create(args::Parser const &) {
  auto flags = flags::Flags::create();
  return {
      .config_file = flags.config_file,
      .net{
          .connection_timeout = CONNECTION_TIMEOUT,
          .tls_validate_certificate = TLS_VALIDATE_CERTIFICATE,
      },
      .auth = flags::Auth::create(),
      .server = flags::Server::create(),
      .client = flags::Client::create(),
      .test{
          .enable_order_mass_cancel = flags.enable_order_mass_cancel,
          .disable_remove_cl_ord_id = flags.disable_remove_cl_ord_id,
          .fix_debug = flags.fix_debug,
      },
  };
}

}  // namespace fix_proxy
}  // namespace roq
