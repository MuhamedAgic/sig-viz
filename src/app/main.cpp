#include <concepts>
#include <utility>

#include <imgui.h>
#include <implot.h>
#include <raylib.h>
#include <rlImGui.h>

#include "app.hpp"
#include "screenshot.hpp"

namespace {

	// Runs `fn` at the end of the scope. Used to pair the libraries' C-style init / shutdown calls, so
	// that everything is torn down in reverse order of creation, even on an early return or exception.
	template <std::invocable Fn>
	class [[nodiscard]] OnExit {
	public:
		explicit OnExit(Fn fn) : fn_{ std::move(fn) } {}
		~OnExit() { fn_(); }

		OnExit(const OnExit&) = delete;
		OnExit& operator=(const OnExit&) = delete;

	private:
		Fn fn_;
	};

} // namespace

int main() {
	if constexpr (PRODUCTION_BUILD) SetTraceLogLevel(LOG_NONE);

	// VSync caps the frame rate to the monitor instead of spinning a CPU core at thousands of FPS.
	SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
	InitWindow(1600, 1000, "Signal Analyzer");
	const OnExit close_window{ [] { CloseWindow(); } };
	SetExitKey(KEY_NULL); // Esc should not close the app
	MaximizeWindow();     // lots of panels: use the whole screen

	InitAudioDevice(); // without a sound card the viewer simply stays silent
	const OnExit close_audio{ [] { CloseAudioDevice(); } };

	rlImGuiSetup(true); // dark theme
	const OnExit shutdown_imgui{ [] { rlImGuiShutdown(); } };

	ImPlot::CreateContext();
	const OnExit shutdown_implot{ [] { ImPlot::DestroyContext(); } };

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.FontGlobalScale = 2.0f;

	// Owns GPU textures and an audio stream, so it must go before the window and audio device:
	// declared after them, it is destroyed first.
	App app;

	while (!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(BLACK);
		rlImGuiBegin();
		app.draw();
		rlImGuiEnd();
		screenshot::process(); // the frame is drawn now: "Save PNG" can grab it
		EndDrawing();
	}
}
