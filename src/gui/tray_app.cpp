#include "gui/tray_app.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <expected>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <spdlog/spdlog.h>

#include "core/alerts.h"
#include "core/config.h"
#include "core/credentials.h"
#include "core/poller.h"
#include "core/timefmt.h"
#include "core/usage.h"
#include "core/usage_client.h"
#include "net/relay.h"
#include "gui/shared_state.h"

namespace cusage {

namespace {

// Nominal window lengths, used to draw "how far through the window we are".
constexpr long long kFiveHourSeconds = 5LL * 3600;
constexpr long long kWeeklySeconds = 7LL * 24 * 3600;

// Set by tray menu callbacks (which fire during SDL event pumping on the main
// thread) and consumed by the main loop.
struct MenuSignals {
  bool quit = false;
  bool open_details = false;
  bool copy_token = false;
};

void on_open(void* userdata, SDL_TrayEntry* /*entry*/) {
  static_cast<MenuSignals*>(userdata)->open_details = true;
}

void on_copy(void* userdata, SDL_TrayEntry* /*entry*/) {
  static_cast<MenuSignals*>(userdata)->copy_token = true;
}

void on_quit(void* userdata, SDL_TrayEntry* /*entry*/) {
  static_cast<MenuSignals*>(userdata)->quit = true;
}

// Polling worker: re-reads the token each poll (never writing it back) and
// publishes snapshots/alerts into shared state.
void poll_worker(std::stop_token stop, NotifyConfig config, SharedState* state) {
  const std::string relay_url = config.relay_url;
  const std::string relay_token = config.relay_token;
  UsageFetcher fetcher = []() -> std::expected<UsageSnapshot, Error> {
    auto path = default_credentials_path();
    if (!path) return std::unexpected(path.error());
    auto tokens = read_oauth_tokens(*path);
    if (!tokens) return std::unexpected(tokens.error());
    return fetch_usage(tokens->access_token_);
  };
  UsageMonitor monitor(std::move(config), std::move(fetcher));
  run_poll_loop(monitor, stop, [state, relay_url, relay_token](const PollResult& result) {
    {
      std::lock_guard lock(state->mutex);
      if (result.error) {
        state->last_error = result.error;
      } else {
        state->last_error.reset();
        state->snapshot = result.snapshot;
      }
      for (const auto& alert : result.alerts) {
        state->pending_alerts.push_back(alert);
        spdlog::warn("ALERT: {}", alert.message);
      }
      state->dirty = true;
    }
    if (!relay_url.empty() && result.snapshot) {
      if (auto pub = publish_usage(relay_url, relay_token, *result.snapshot);
          !pub) {
        spdlog::warn("relay publish failed: {}", pub.error().message);
      }
    }
  });
}

long long now_epoch_seconds() { return static_cast<long long>(std::time(nullptr)); }

// Fraction [0,1] of the way through a window: ~0 just after a reset, ~1 just
// before the next reset. Zero when the reset time is unknown.
double elapsed_fraction(const UsageWindow& window, long long duration_seconds) {
  if (!window.resets_at_) return 0.0;
  const auto reset = iso8601_utc_to_epoch_seconds(*window.resets_at_);
  if (!reset) return 0.0;
  const long long remaining = *reset - now_epoch_seconds();
  return std::clamp(
      1.0 - static_cast<double>(remaining) / static_cast<double>(duration_seconds),
      0.0, 1.0);
}

// Renders the tray icon: two columns (5-hour left, weekly right) on black. A
// green bar shows window progress (time elapsed — "good"); a translucent red bar
// shows usage ("bad") on top, so the overlap reads orange and usage outpacing
// the clock shows red poking above the green.
SDL_Surface* render_status_icon(const std::optional<UsageSnapshot>& snapshot) {
  constexpr int kSize = 32;
  SDL_Surface* surface = SDL_CreateSurface(kSize, kSize, SDL_PIXELFORMAT_RGBA32);
  if (!surface) return nullptr;
  SDL_FillSurfaceRect(surface, nullptr, SDL_MapSurfaceRGBA(surface, 0, 0, 0, 255));

  auto blend = [&](int x, int y, int r, int g, int b, float a) {
    if (x < 0 || x >= kSize || y < 0 || y >= kSize) return;
    auto* px = static_cast<Uint8*>(surface->pixels) + y * surface->pitch + x * 4;
    px[0] = static_cast<Uint8>(r * a + px[0] * (1.0f - a));
    px[1] = static_cast<Uint8>(g * a + px[1] * (1.0f - a));
    px[2] = static_cast<Uint8>(b * a + px[2] * (1.0f - a));
    px[3] = 255;
  };

  auto column = [&](int x0, int x1, double elapsed, double usage) {
    constexpr int top = 2;
    constexpr int bottom = 29;
    constexpr int span = bottom - top;  // 27 px of usable height.
    const int green = static_cast<int>(std::lround(std::clamp(elapsed, 0.0, 1.0) * span));
    const int red = static_cast<int>(std::lround(std::clamp(usage, 0.0, 1.0) * span));
    for (int x = x0; x <= x1; ++x) {
      for (int k = 0; k < span; ++k) {
        const bool in_green = k < green;
        const bool in_red = k < red;
        if (!in_green && !in_red) continue;
        // Overlap = reddish-orange; usage above the window line = solid red;
        // window ahead of usage = green.
        if (in_green && in_red) {
          blend(x, bottom - k, 200, 96, 62, 1.0f);
        } else if (in_red) {
          blend(x, bottom - k, 235, 70, 55, 1.0f);
        } else {
          blend(x, bottom - k, 60, 200, 90, 1.0f);
        }
      }
    }
  };

  double e5 = 0.0, u5 = 0.0, ew = 0.0, uw = 0.0;
  if (snapshot) {
    if (snapshot->five_hour_) {
      e5 = elapsed_fraction(*snapshot->five_hour_, kFiveHourSeconds);
      u5 = std::clamp(snapshot->five_hour_->utilization_ / 100.0, 0.0, 1.0);
    }
    if (snapshot->seven_day_) {
      ew = elapsed_fraction(*snapshot->seven_day_, kWeeklySeconds);
      uw = std::clamp(snapshot->seven_day_->utilization_ / 100.0, 0.0, 1.0);
    }
  }
  column(2, 11, e5, u5);    // Left: 5-hour (40% width).
  column(14, 29, ew, uw);   // Right: weekly (60% width, more important).
  return surface;
}

std::string summary_text(const std::optional<UsageSnapshot>& snapshot,
                         const std::optional<Error>& error) {
  if (snapshot) {
    auto pct = [](const std::optional<UsageWindow>& w) {
      return w ? std::to_string(w->percent()) + "%" : std::string("-");
    };
    return "5h " + pct(snapshot->five_hour_) + "  \xC2\xB7  wk " +
           pct(snapshot->seven_day_);
  }
  if (error) return "Error (open details)";
  return "Loading...";
}

// Owns the optional Dear ImGui + OpenGL details window. Created on demand and
// torn down when the user closes it; the tray keeps running either way.
class DetailsWindow {
 public:
  bool is_open() const { return window_ != nullptr; }
  SDL_WindowID window_id() const { return window_ ? SDL_GetWindowID(window_) : 0; }

  void open() {
    if (window_) return;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    window_ = SDL_CreateWindow("Claude Usage", 400, 320,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) {
      spdlog::error("failed to create details window: {}", SDL_GetError());
      return;
    }
    gl_ = SDL_GL_CreateContext(window_);
    if (!gl_) {
      spdlog::error("failed to create GL context: {}", SDL_GetError());
      SDL_DestroyWindow(window_);
      window_ = nullptr;
      return;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    SDL_GL_SetSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window_, gl_);
    ImGui_ImplOpenGL3_Init("#version 150");
  }

  void close() {
    if (!window_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl_);
    SDL_DestroyWindow(window_);
    gl_ = nullptr;
    window_ = nullptr;
  }

  void render(SharedState& state) {
    if (!window_) return;
    SDL_GL_MakeCurrent(window_, gl_);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("usage", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    {
      std::lock_guard lock(state.mutex);
      if (state.last_error) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Error: %s",
                           state.last_error->message.c_str());
      }
      if (state.snapshot) {
        for (const auto& [name, window] : enumerate_windows(*state.snapshot)) {
          draw_window_bar(name, window);
        }
        if (state.snapshot->extra_usage_ &&
            state.snapshot->extra_usage_->is_enabled_) {
          ImGui::Separator();
          ImGui::TextUnformatted("Extra usage enabled");
        }
      } else if (!state.last_error) {
        ImGui::TextUnformatted("Loading...");
      }
    }
    ImGui::End();
    ImGui::Render();

    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
  }

 private:
  static void draw_window_bar(const std::string& name, const UsageWindow& window) {
    const float fraction =
        static_cast<float>(std::clamp(window.utilization_ / 100.0, 0.0, 1.0));
    std::string reset_note;
    if (window.resets_at_) {
      if (const auto epoch = iso8601_utc_to_epoch_seconds(*window.resets_at_)) {
        reset_note = "resets in " + humanize_duration(*epoch - now_epoch_seconds());
      }
    }
    ImGui::Spacing();
    ImGui::Text("%s", name.c_str());
    const ImVec4 bar_color = window.percent() >= 80
                                 ? ImVec4(0.90f, 0.35f, 0.30f, 1.0f)
                                 : ImVec4(0.30f, 0.65f, 0.95f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar_color);
    const std::string overlay = std::to_string(window.percent()) + "%";
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
    ImGui::PopStyleColor();
    if (!reset_note.empty()) {
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", reset_note.c_str());
    }
  }

  SDL_Window* window_ = nullptr;
  SDL_GLContext gl_ = nullptr;
};

// Tray icon, menu, and tooltip. The menu is a single (disabled) summary line
// plus the actions, which avoids any dynamic-entry-count fragility; the detailed
// per-window breakdown lives in the icon and the details window.
class TrayUi {
 public:
  TrayUi(SDL_Tray* tray, MenuSignals* signals) : tray_(tray) {
    menu_ = SDL_CreateTrayMenu(tray);
    summary_ = SDL_InsertTrayEntryAt(menu_, -1, "Loading...",
                                     SDL_TRAYENTRY_BUTTON | SDL_TRAYENTRY_DISABLED);
    SDL_InsertTrayEntryAt(menu_, -1, nullptr, 0);  // Separator.
    SDL_TrayEntry* open =
        SDL_InsertTrayEntryAt(menu_, -1, "Open details", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(open, &on_open, signals);
    SDL_TrayEntry* copy = SDL_InsertTrayEntryAt(menu_, -1, "Copy token (for phone)",
                                                SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(copy, &on_copy, signals);
    SDL_TrayEntry* exit =
        SDL_InsertTrayEntryAt(menu_, -1, "Exit", SDL_TRAYENTRY_BUTTON);
    SDL_SetTrayEntryCallback(exit, &on_quit, signals);
  }

  ~TrayUi() {
    if (icon_) SDL_DestroySurface(icon_);
  }

  void refresh(SharedState& state) {
    std::optional<UsageSnapshot> snapshot;
    std::optional<Error> error;
    {
      std::lock_guard lock(state.mutex);
      snapshot = state.snapshot;
      error = state.last_error;
    }

    const std::string text = summary_text(snapshot, error);
    if (summary_) SDL_SetTrayEntryLabel(summary_, text.c_str());
    SDL_SetTrayTooltip(tray_, text.c_str());

    if (SDL_Surface* next = render_status_icon(snapshot)) {
      SDL_SetTrayIcon(tray_, next);
      if (icon_) SDL_DestroySurface(icon_);
      icon_ = next;  // Kept alive until replaced, in case SDL references it.
    }
  }

 private:
  SDL_Tray* tray_;
  SDL_TrayMenu* menu_ = nullptr;
  SDL_TrayEntry* summary_ = nullptr;
  SDL_Surface* icon_ = nullptr;
};

}  // namespace

int run_tray_app() {
  SDL_SetMainReady();
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    spdlog::critical("SDL_Init failed: {}", SDL_GetError());
    return 1;
  }

  NotifyConfig config;
  if (auto config_path = default_config_path()) {
    config = load_config(*config_path);
  }

  SharedState state;
  std::jthread worker(poll_worker, config, &state);

  SDL_Surface* initial_icon = render_status_icon(std::nullopt);
  SDL_Tray* tray = SDL_CreateTray(initial_icon, "Claude usage");
  if (initial_icon) SDL_DestroySurface(initial_icon);
  if (!tray) {
    spdlog::critical("SDL_CreateTray failed: {}", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  MenuSignals signals;
  TrayUi ui(tray, &signals);
  DetailsWindow details;

  using namespace std::chrono_literals;
  auto last_refresh = std::chrono::steady_clock::time_point{};
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (details.is_open()) ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT) running = false;
      if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && details.is_open() &&
          event.window.windowID == details.window_id()) {
        details.close();
      }
    }

    if (signals.quit) running = false;
    if (signals.open_details) {
      signals.open_details = false;
      details.open();
    }
    if (signals.copy_token) {
      signals.copy_token = false;
      std::string message;
      auto path = default_credentials_path();
      if (!path) {
        message = "Could not locate credentials: " + path.error().message;
      } else if (auto tokens = read_oauth_tokens(*path)) {
        SDL_SetClipboardText(tokens->access_token_.c_str());
        message =
            "Current token copied to clipboard.\n\nIt expires in a few hours. For a "
            "durable phone token, run `claude setup-token` in a terminal.";
      } else {
        message = "Could not read token: " + tokens.error().message;
      }
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Claude usage",
                               message.c_str(), nullptr);
    }

    // Refresh icon + summary on new poll data, and at least every 30s so the
    // time bar and countdowns advance between (10-minute) network polls.
    const auto now = std::chrono::steady_clock::now();
    bool due = (now - last_refresh >= 30s);
    {
      std::lock_guard lock(state.mutex);
      if (state.dirty) {
        due = true;
        state.dirty = false;
      }
    }
    if (due) {
      ui.refresh(state);
      last_refresh = now;
    }

    // Alarm: surface drop/reset/threshold alerts as a popup (they are also
    // logged). Modal, but that suits the headline "capacity freed" event.
    std::vector<Alert> alerts;
    {
      std::lock_guard lock(state.mutex);
      alerts.swap(state.pending_alerts);
    }
    if (!alerts.empty()) {
      std::string message;
      for (const auto& alert : alerts) {
        message += alert.message;
        message += '\n';
      }
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Claude usage",
                               message.c_str(), nullptr);
    }

    if (details.is_open()) {
      details.render(state);  // vsync paces this branch.
    } else {
      SDL_Delay(100);  // Idle politely while only the tray is live.
    }
  }

  details.close();
  worker.request_stop();
  SDL_DestroyTray(tray);
  SDL_Quit();
  return 0;
}

}  // namespace cusage
