/* Copyright (c) 2017-2025, Hans Erik Thrane */

#pragma once

#include <span>
#include <string_view>

#include "roq/service.hpp"

namespace roq {
namespace fix_proxy {

struct Application final : public Service {
  using Service::Service;  // inherit constructors

 protected:
  int main(args::Parser const &) override;
};

}  // namespace fix_proxy
}  // namespace roq
