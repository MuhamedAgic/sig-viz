#include <concepts>
#include <utility>

#include <imgui.h>
#include <raylib.h>
#include <rlImGui.h>

#include "signal_viewer.hpp"

namespace {

	// Runs `fn` at the end of the scope. Used to pair raylib's C-style Init/Close calls, so that
	// everything is torn down in reverse order of creation, even on an early return or exception.
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
	InitWindow(1500, 1000, "Signal Analyzer");
	const OnExit close_window{ [] { CloseWindow(); } };
	SetExitKey(KEY_NULL); // Esc should not close the app

	InitAudioDevice(); // without a sound card the viewer simply stays silent
	const OnExit close_audio{ [] { CloseAudioDevice(); } };

	rlImGuiSetup(true); // dark theme
	const OnExit shutdown_imgui{ [] { rlImGuiShutdown(); } };

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.FontGlobalScale = 2.0f;

	SignalViewer viewer; // owns an audio stream, so it must die before the audio device: declared after it

	while (!WindowShouldClose()) {
		BeginDrawing();
		ClearBackground(BLACK);
		rlImGuiBegin();

		ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
		viewer.draw();

		rlImGuiEnd();
		EndDrawing();
	}
}
