#pragma once

#include <string>
#include <vector>

#include <imgui.h>

#include "audio_window.hpp"
#include "bursts_window.hpp"
#include "coding_window.hpp"
#include "command_palette.hpp"
#include "compare_window.hpp"
#include "digital_window.hpp"
#include "file_browser.hpp"
#include "generator_window.hpp"
#include "guide_window.hpp"
#include "live_window.hpp"
#include "signal_viewer.hpp"
#include "spectrogram_window.hpp"
#include "spectrum_window.hpp"
#include "transform_window.hpp"
#include "workspace.hpp"

// The whole UI: the shared workspace, every window, the menu bar, the status bar, the command palette
// and the docking layout.
class App {
public:
	// Call once per frame, between rlImGuiBegin() and rlImGuiEnd().
	void draw();

private:
	void                               draw_menu_bar();
	void                               draw_status_bar();
	void                               take_dropped_files();
	void                               draw_session_dialog();
	[[nodiscard]] std::vector<Command> commands();
	void                               build_default_layout(ImGuiID dockspace);
	void show(bool& window_open, const char* title); // open a window and bring it to the front

	Workspace ws_;

	SignalViewer      viewer_;
	SpectrumWindow    spectrum_;
	SpectrogramWindow spectrogram_;
	CompareWindow     compare_;
	TransformWindow   transform_;
	GeneratorWindow   generator_;
	DigitalWindow     digital_;
	GuideWindow       guide_;
	BurstsWindow      bursts_;
	AudioWindow       audio_;
	LiveWindow        live_;
	CodingWindow      coding_;

	CommandPalette palette_;

	FileBrowser session_browser_;
	enum class SessionAction { None, Save, Load } session_action_ = SessionAction::None;
	std::string message_; // shown in the status bar

	bool check_layout_ = true;  // first frame: make the default layout, unless imgui.ini has one
	bool reset_layout_ = false; // the user asked for the default layout
	int  pick_tabs_in_ = 0; // frames until the viewer's and guide's tabs are brought to the front (after a new layout)
	bool show_imgui_demo_ = false;
	bool show_implot_demo_ = false;
};
