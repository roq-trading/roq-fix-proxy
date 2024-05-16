/* Copyright (c) 2017-2024, Hans Erik Thrane */

#include "roq/proxy/fix/application.hpp"

#include <exception>
#include <vector>

#include "roq/exceptions.hpp"

#include "roq/logging.hpp"

#include "roq/io/engine/context_factory.hpp"

#include "roq/proxy/fix/config.hpp"
#include "roq/proxy/fix/controller.hpp"
#include "roq/proxy/fix/settings.hpp"

using namespace std::literals;

namespace roq {
namespace proxy {
namespace fix {

// === IMPLEMENTATION ===

int Application::main(args::Parser const &args) {
  auto params = args.params();
  auto settings = Settings::create(args);
  log::info("settings={}"sv, settings);
  auto config = Config::parse_file(settings.config_file);
  log::info("config={}"sv, config);
  auto context = io::engine::ContextFactory::create_libevent();
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

}  // namespace fix
}  // namespace proxy
}  // namespace roq
