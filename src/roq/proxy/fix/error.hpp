/* Copyright (c) 2017-2024, Hans Erik Thrane */

#pragma once

#include <string_view>

namespace roq {
namespace proxy {
namespace fix {

struct Error final {
  static std::string_view const NOT_READY;
  static std::string_view const SUCCESS;
  static std::string_view const NOT_LOGGED_ON;
  static std::string_view const ALREADY_LOGGED_ON;
  static std::string_view const INVALID_PASSWORD;
  static std::string_view const INVALID_USERNAME;
  static std::string_view const INVALID_COMPONENT;
};

}  // namespace fix
}  // namespace proxy
}  // namespace roq
