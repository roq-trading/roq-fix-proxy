/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/shared.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {

// === IMPLEMENTATION ===

Shared::Shared(Settings const &settings, fix::proxy::Manager &proxy) : settings{settings}, proxy{proxy} {
}

}  // namespace fix_proxy
}  // namespace roq
