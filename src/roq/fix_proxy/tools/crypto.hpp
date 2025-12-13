/* Copyright (c) 2017-2026, Hans Erik Thrane */

#pragma once

#include <chrono>
#include <string_view>

#include "roq/utils/hash/sha256.hpp"

#include "roq/utils/mac/hmac.hpp"

namespace roq {
namespace fix_proxy {
namespace tools {

struct Crypto final {
  using Hash = utils::hash::SHA256;
  using MAC = utils::mac::HMAC<utils::hash::SHA256>;

  Crypto(std::string_view const &method, std::chrono::nanoseconds timestamp_tolerance);

  Crypto(Crypto &&) = delete;
  Crypto(Crypto const &) = delete;

  bool validate(std::string_view const &password, std::string_view const &secret, std::string_view const &raw_data);

  enum class Method {
    UNDEFINED,
    HMAC_SHA256,
    HMAC_SHA256_TS,
  };

 protected:
  template <Method>
  bool validate_helper(std::string_view const &password, std::string_view const &secret, std::string_view const &raw_data);

 private:
  Method const method_;
  std::chrono::nanoseconds const timestamp_tolerance_;
  std::array<std::byte, MAC::DIGEST_LENGTH> digest_;
};

}  // namespace tools
}  // namespace fix_proxy
}  // namespace roq
