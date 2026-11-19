#pragma once

#include <imgui.h>

#include "compare_window.hpp"
#include "digital_window.hpp"
#include "generator_window.hpp"
#include "guide_window.hpp"
#include "signal_viewer.hpp"
#include "spectrogram_window.hpp"
#include "spectrum_window.hpp"
#include "transform_window.hpp"
#include "workspace.hpp"

// The whole UI: the shared workspace, every window, the menu bar and the docking layout.
class App {
public:
	// Call once per frame, between rlImGuiBegin() and rlImGuiEnd().
	void draw();

private:
	void draw_menu_bar();
	void build_default_layout(ImGuiID dockspace);

	Workspace ws_;

	SignalViewer      viewer_;
	SpectrumWindow    spectrum_;
	SpectrogramWindow spectrogram_;
	CompareWindow     compare_;
	TransformWindow   transform_;
	GeneratorWindow   generator_;
	DigitalWindow     digital_;
	GuideWindow       guide_;

	bool check_layout_ = true; // first frame: make the default layout, unless imgui.ini has one
	bool reset_layout_ = false; // the user asked for the default layout
	int  pick_tabs_in_ = 0;    // frames until the viewer's and guide's tabs are brought to the front (after a new layout)
	bool show_imgui_demo_ = false;
	bool show_implot_demo_ = false;
};
