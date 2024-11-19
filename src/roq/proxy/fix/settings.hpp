/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <chrono>
#include <string_view>

#include "roq/args/parser.hpp"

#include "roq/proxy/fix/flags/auth.hpp"
#include "roq/proxy/fix/flags/client.hpp"
#include "roq/proxy/fix/flags/server.hpp"

namespace roq {
namespace proxy {
namespace fix {

struct Settings final {
  // note! dependency on args::Parser to enforce correct sequencing
  static Settings create(args::Parser const &);

  std::string_view config_file;

  struct {
    std::chrono::nanoseconds connection_timeout = {};
    bool tls_validate_certificate = {};
  } net;

  flags::Auth auth;
  flags::Server server;
  flags::Client client;

  struct {
    bool enable_order_mass_cancel = {};
    bool disable_remove_cl_ord_id = {};
    bool fix_debug = {};
  } test;
};

}  // namespace fix
}  // namespace proxy
}  // namespace roq

template <>
struct fmt::formatter<roq::proxy::fix::Settings> {
  constexpr auto parse(format_parse_context &context) { return std::begin(context); }
  auto format(roq::proxy::fix::Settings const &value, format_context &context) const {
    using namespace std::literals;
    return fmt::format_to(
        context.out(),
        R"({{)"
        R"(config_file="{}", )"
        R"(net={{)"
        R"(connection_timeout={}, )"
        R"(tls_validate_certificate={})"
        R"(}})"
        R"(auth={}, )"
        R"(server={}, )"
        R"(client={}, )"
        R"(test={{)"
        R"(enable_order_mass_cancel={}, )"
        R"(disable_remove_cl_ord_id={})"
        R"(}})"
        R"(}})"sv,
        value.config_file,
        value.net.connection_timeout,
        value.net.tls_validate_certificate,
        value.auth,
        value.server,
        value.client,
        value.test.enable_order_mass_cancel,
        value.test.disable_remove_cl_ord_id);
  }
};
