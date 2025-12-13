/* Copyright (c) 2017-2026, Hans Erik Thrane */

#include "roq/fix_proxy/application.hpp"

#include "roq/logging.hpp"

#include "roq/utils/exceptions/unhandled.hpp"

#include "roq/io/engine/context_factory.hpp"

#include "roq/fix_proxy/config.hpp"
#include "roq/fix_proxy/controller.hpp"
#include "roq/fix_proxy/settings.hpp"

using namespace std::literals;

namespace roq {
namespace fix_proxy {

// === IMPLEMENTATION ===

int Application::main(args::Parser const &args) {
  auto params = args.params();
  auto settings = Settings::create(args);
  log::info("settings={}"sv, settings);
  auto config = Config::parse_file(settings.config_file);
  log::info("config={}"sv, config);
  auto context = io::engine::ContextFactory::create();
  try {
    Controller{settings, config, *context, params}.run();
    return EXIT_SUCCESS;
  } catch (...) {
    utils::exceptions::Unhandled::terminate();
  }
  return EXIT_FAILURE;
}

}  // namespace fix_proxy
}  // namespace roq
