/* Copyright (c) 2017-2026, Hans Erik Thrane */

#pragma once

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <ranges>

#include <string>
#include <string_view>

#include "roq/utils/container.hpp"

namespace roq {
namespace fix_proxy {

struct User final {
  std::string component;
  std::string username;
  std::string password;
  std::string accounts;  // XXX TODO
  uint32_t strategy_id = {};
};

struct Config final {
  Config(Config const &) = delete;

  static Config parse_file(std::string_view const &);
  static Config parse_text(std::string_view const &);

  utils::unordered_set<std::string> const symbols;
  utils::unordered_map<std::string, User> const users;

 protected:
  explicit Config(auto &node);
};

}  // namespace fix_proxy
}  // namespace roq

template <>
struct fmt::formatter<roq::fix_proxy::User> {
  constexpr auto parse(format_parse_context &context) { return std::begin(context); }
  auto format(roq::fix_proxy::User const &value, format_context &context) const {
    using namespace std::literals;
    return fmt::format_to(
        context.out(),
        R"({{)"
        R"(component="{}", )"
        R"(username="{}", )"
        R"(password="{}", )"
        R"(accounts="{}", )"
        R"(strategy_id={})"
        R"(}})"sv,
        value.component,
        value.username,
        value.password,
        value.accounts,
        value.strategy_id);
  }
};

template <>
struct fmt::formatter<roq::fix_proxy::Config> {
  constexpr auto parse(format_parse_context &context) { return std::begin(context); }
  auto format(roq::fix_proxy::Config const &value, format_context &context) const {
    using namespace std::literals;
    return fmt::format_to(
        context.out(),
        R"({{)"
        R"(symbols=[{}], )"
        R"(users=[{}])"
        R"(}})"sv,
        fmt::join(value.symbols, ", "sv),
        fmt::join(std::ranges::views::transform(value.users, [](auto &item) { return item.second; }), ","sv));
  }
};
