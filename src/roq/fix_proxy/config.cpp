/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/config.hpp"

#include <toml++/toml.h>

#include "roq/exceptions.hpp"
#include "roq/logging.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {

// === HELPERS ===

namespace {
void check_empty(auto &node) {
  if (!node.is_table())
    return;
  auto &table = *node.as_table();
  auto error = false;
  for (auto &[key, value] : table) {
    log::warn(R"(key="{}")"sv, static_cast<std::string_view>(key));
    error = true;
  }
  if (error)
    log::fatal("Unexpected"sv);
}

template <typename Callback>
bool find_and_remove(auto &node, std::string_view const &key, Callback callback) {
  if (!node.is_table()) {
    log::warn("Unexpected: node is not a table"sv);
    return false;
  }
  auto &table = *node.as_table();
  auto iter = table.find(key);
  if (iter == table.end())
    return false;
  callback((*iter).second);
  table.erase(iter);
  assert(table.find(key) == std::end(table));
  return true;
}

template <typename R>
R parse_symbols(auto &node) {
  using result_type = std::remove_cvref<R>::type;
  result_type result;
  auto parse_helper = [&](auto &node) {
    using value_type = typename R::value_type;
    if (node.is_value()) {
      result.emplace(*node.template value<value_type>());
    } else if (node.is_array()) {
      auto &arr = *node.as_array();
      for (auto &node_2 : arr) {
        result.emplace(*node_2.template value<value_type>());
      }
    } else {
      log::fatal("Unexpected"sv);
    }
  };
  if (find_and_remove(node, "symbols"sv, parse_helper)) {
  } else {
    log::fatal(R"(Unexpected: did not find the "symbols" table)"sv);
  }
  return result;
}

auto parse_user(auto &node) {
  auto table = *node.as_table();
  User result;
  for (auto [key, value] : table) {
    if (key == "component"sv) {
      result.component = *value.template value<std::string>();
    } else if (key == "username"sv) {
      result.username = *value.template value<std::string>();
    } else if (key == "password"sv) {
      result.password = *value.template value<std::string>();
    } else if (key == "accounts"sv) {
      // XXX TODO
    } else if (key == "strategy_id"sv) {
      result.strategy_id = *value.template value<uint32_t>();
    } else {
      log::fatal(R"(Unexpected: user key="{}")"sv, key.str());
    }
  }
  return result;
}

template <typename R>
R parse_users(auto &node) {
  using result_type = std::remove_cvref<R>::type;
  result_type result;
  auto parse_helper = [&](auto &node) {
    if (node.is_table()) {
      auto &table = *node.as_table();
      for (auto [key, value] : table) {
        if (value.is_table()) {
          auto user = parse_user(value);
          result.emplace(key, std::move(user));
        } else {
          log::fatal(R"(Unexpected: "users.{}" must be a table)"sv, key.str());
        }
      }
    } else {
      log::fatal(R"(Unexpected: "users" must be a table)"sv);
    }
  };
  if (find_and_remove(node, "users"sv, parse_helper)) {
  } else {
    log::fatal(R"(Unexpected: did not find the "users" table)"sv);
  }
  return result;
}
}  // namespace

// === IMPLEMENTATION ===

Config Config::parse_file(std::string_view const &path) {
  auto root = toml::parse_file(path);
  return Config{root};
}

Config Config::parse_text(std::string_view const &text) {
  auto root = toml::parse(text);
  return Config{root};
}

Config::Config(auto &node) : symbols{parse_symbols<decltype(symbols)>(node)}, users{parse_users<decltype(users)>(node)} {
  check_empty(node);
}

}  // namespace fix_proxy
}  // namespace roq
