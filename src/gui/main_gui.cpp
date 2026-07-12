#include <cstdlib>
#include <exception>

#include <spdlog/spdlog.h>

#include "gui/tray_app.h"

int main(int /*argc*/, char** /*argv*/) {
  // Top-level backstop (see CLI main.cpp for the rationale).
  try {
    return cusage::run_tray_app();
  } catch (const std::exception& e) {
    spdlog::critical("unhandled exception: {}", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    spdlog::critical("unhandled non-standard exception");
    return EXIT_FAILURE;
  }
}
