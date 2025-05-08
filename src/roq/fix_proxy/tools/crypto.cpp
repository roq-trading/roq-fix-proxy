/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/tools/crypto.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <magic_enum/magic_enum_format.hpp>

#include "roq/clock.hpp"

#include "roq/logging.hpp"

#include "roq/utils/enum.hpp"

#include "roq/utils/charconv/from_chars.hpp"

#include "roq/utils/codec/base64.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {
namespace tools {

// === HELPERS ===

namespace {
auto parse_method(auto &auth_method) {
  if (std::empty(auth_method)) {
    return Crypto::Method{};
  }
  return utils::parse_enum<Crypto::Method>(auth_method);
};
}  // namespace

// === IMPLEMENTATION ===

Crypto::Crypto(std::string_view const &method, std::chrono::nanoseconds timestamp_tolerance)
    : method_{parse_method(method)}, timestamp_tolerance_{timestamp_tolerance} {
  log::info("Using method={}"sv, method_);
}

template <>
bool Crypto::validate_helper<Crypto::Method::UNDEFINED>(
    std::string_view const &password, std::string_view const &secret, [[maybe_unused]] std::string_view const &raw_data) {
  return password == secret;
}

template <>
bool Crypto::validate_helper<Crypto::Method::HMAC_SHA256>(std::string_view const &password, std::string_view const &secret, std::string_view const &raw_data) {
  MAC mac{secret};  // alloc
  // mac.clear();
  mac.update(raw_data);
  auto digest = mac.final(digest_);
  std::string signature;
  utils::codec::Base64::encode(signature, digest, false, false);  // alloc
  auto result = signature == password;
  log::warn(R"(DEBUG computed="{}, received="{}")"sv, signature, password);
  return result;
}

template <>
bool Crypto::validate_helper<Crypto::Method::HMAC_SHA256_TS>(
    std::string_view const &password, std::string_view const &secret, std::string_view const &raw_data) {
  auto pos = raw_data.find_first_of('.');
  if (pos == raw_data.npos) {
    log::warn(R"(DEBUG no period in raw_data="{}")"sv, raw_data);
    return false;
  }
  auto tmp = utils::charconv::from_chars<int64_t>(raw_data.substr(0, pos));
  std::chrono::milliseconds sending_time_utc{tmp};
  auto now = clock::get_realtime();
  auto diff = sending_time_utc < now ? (now - sending_time_utc) : (sending_time_utc - now);
  if (diff > timestamp_tolerance_) {
    log::warn("DEBUG now={}, sending_time_utc={}"sv, now, sending_time_utc);
    return false;
  }
  MAC mac{secret};  // alloc
  // mac.clear();
  mac.update(raw_data);
  auto digest = mac.final(digest_);
  std::string signature;
  utils::codec::Base64::encode(signature, digest, false, false);  // alloc
  if (signature != password) {
    log::warn(R"(DEBUG computed="{}", password="{}")"sv, signature, password);
    return false;
  }
  return true;
}

bool Crypto::validate(std::string_view const &password, std::string_view const &secret, std::string_view const &raw_data) {
  switch (method_) {
    using enum Method;
    case UNDEFINED:
      return validate_helper<Method::UNDEFINED>(password, secret, raw_data);
    case HMAC_SHA256:
      return validate_helper<Method::HMAC_SHA256>(password, secret, raw_data);
    case HMAC_SHA256_TS:
      return validate_helper<Method::HMAC_SHA256_TS>(password, secret, raw_data);
  }
  log::fatal("Unexpected"sv);
}

}  // namespace tools
}  // namespace fix_proxy
}  // namespace roq
