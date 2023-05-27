/* Copyright (c) 2017-2025, Hans Erik Thrane */

#include "roq/fix_proxy/application.hpp"

#include <exception>
#include <vector>

#include "roq/exceptions.hpp"

#include "roq/logging.hpp"

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
    try {
      throw;
    } catch (SystemError &e) {
      log::error("Unhandled exception: {}"sv, e);
    } catch (Exception &e) {
      log::error("Unhandled exception: {}"sv, e);
    } catch (std::exception &e) {
      log::error(R"(Unhandled exception: type="{}", what="{}")"sv, typeid(e).name(), e.what());
    } catch (...) {
      auto e = std::current_exception();
      log::error(R"(Unhandled exception: type="{}")"sv, typeid(e).name());
    }
  }
  return EXIT_FAILURE;
}

}  // namespace fix_proxy
}  // namespace roq
