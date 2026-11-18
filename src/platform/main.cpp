#include <iostream>
#include <raylib.h>

#include <imgui.h>
#include <rlImGui.h>

#include "signal_viewer.hpp"


int main(void) {

#if PRODUCTION_BUILD == 1
	SetTraceLogLevel(LOG_NONE); // no log output to the console by raylib
#endif

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(1500, 1000, "Signal Analyzer");
	InitAudioDevice(); // needed for the signal viewer's sound; if there is no sound card the viewer just stays silent
	SetExitKey(KEY_NULL); // Disable Esc from closing window
	// SetTargetFPS(240);

#pragma region imgui
	rlImGuiSetup(true);

	bool show_demo_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	ImGuiIO& io = ImGui::GetIO();
	(void)io;
	////io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
	////io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;       // Enable gamepad Controls
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
	io.FontGlobalScale = 2;
#pragma endregion

	{ // scope: the viewer owns an audio stream, so it must be destroyed before CloseAudioDevice()
		SignalViewer signal_viewer; // generates the example signals once, up front

		while (!WindowShouldClose()) {
			BeginDrawing();
			ClearBackground(BLACK);

#pragma region imgui
			rlImGuiBegin();

			//docking stuff
			ImGui::PushStyleColor(ImGuiCol_WindowBg, {});
			ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, {});
			ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
			ImGui::PopStyleColor(2);

			signal_viewer.draw();

			if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

#pragma endregion

#pragma region imgui
			rlImGuiEnd();

#pragma endregion

			EndDrawing();
		}
	}

	CloseAudioDevice();
	CloseWindow();

#pragma region imgui
	rlImGuiShutdown();
#pragma endregion


	return 0;
}