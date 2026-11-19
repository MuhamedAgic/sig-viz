#include "app.hpp"

#include <imgui_internal.h> // DockBuilder API
#include <implot.h>

void App::draw() {
	draw_menu_bar();

	const ImGuiID dockspace = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	if (check_layout_ || reset_layout_) {
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
		if (reset_layout_ || node == nullptr || node->IsLeafNode()) {
			build_default_layout(dockspace);
			pick_tabs_in_ = 2; // the windows get docked during the next frame; then pick the tabs to show
		}
		check_layout_ = reset_layout_ = false;
	}

	// The viewer goes first: it advances the playhead the others follow.
	viewer_.draw(ws_);
	spectrum_.draw(ws_);
	spectrogram_.draw(ws_);
	compare_.draw(ws_);
	transform_.draw(ws_);
	generator_.draw(ws_);
	digital_.draw(ws_);
	guide_.draw(ws_);

	if (pick_tabs_in_ > 0 && --pick_tabs_in_ == 0) {
		ImGui::SetWindowFocus(GuideWindow::title);
		ImGui::SetWindowFocus(SignalViewer::title);
	}

	if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);
	if (show_implot_demo_) ImPlot::ShowDemoWindow(&show_implot_demo_);
}

void App::draw_menu_bar() {
	if (!ImGui::BeginMainMenuBar()) return;

	if (ImGui::BeginMenu("Windows")) {
		ImGui::MenuItem(SignalViewer::title, nullptr, &viewer_.open);
		ImGui::MenuItem(SpectrumWindow::title, nullptr, &spectrum_.open);
		ImGui::MenuItem(SpectrogramWindow::title, nullptr, &spectrogram_.open);
		ImGui::MenuItem(CompareWindow::title, nullptr, &compare_.open);
		ImGui::MenuItem(TransformWindow::title, nullptr, &transform_.open);
		ImGui::MenuItem(GeneratorWindow::title, nullptr, &generator_.open);
		ImGui::MenuItem(DigitalWindow::title, nullptr, &digital_.open);
		ImGui::MenuItem(GuideWindow::title, nullptr, &guide_.open);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset layout")) {
			viewer_.open = spectrum_.open = spectrogram_.open = compare_.open = true;
			transform_.open = generator_.open = digital_.open = guide_.open = true;
			reset_layout_ = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("Guide: what is all this?")) {
			guide_.open = true;
			ImGui::SetWindowFocus(GuideWindow::title);
		}
		ImGui::SeparatorText("Viewer");
		ImGui::TextUnformatted("Drag            move the playhead");
		ImGui::TextUnformatted("Shift + drag    select a range");
		ImGui::TextUnformatted("Right-click     place cursor A / B");
		ImGui::TextUnformatted("Mouse wheel     zoom the scope");
		ImGui::TextUnformatted("Space           play / pause");
		ImGui::TextUnformatted("Esc             clear selection and cursors");
		ImGui::SeparatorText("Plots (spectrum, spectrogram, ...)");
		ImGui::TextUnformatted("Drag / wheel    pan / zoom");
		ImGui::TextUnformatted("Double-click    fit to data");
		ImGui::TextUnformatted("Right-click     plot options");
		ImGui::Separator();
		ImGui::MenuItem("ImGui demo", nullptr, &show_imgui_demo_);
		ImGui::MenuItem("ImPlot demo", nullptr, &show_implot_demo_);
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

// Guide, Generator and Transform on the left, the viewer (with Compare and Digital as tabs) in the
// middle, Spectrum above Spectrogram on the right.
void App::build_default_layout(ImGuiID dockspace) {
	ImGui::DockBuilderRemoveNode(dockspace);
	ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);

	ImGuiID centre = dockspace;
	const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.22f, nullptr, &centre);
	ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.38f, nullptr, &centre);
	const ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, nullptr, &right);

	ImGui::DockBuilderDockWindow(TransformWindow::title, left);
	ImGui::DockBuilderDockWindow(GeneratorWindow::title, left);
	ImGui::DockBuilderDockWindow(GuideWindow::title, left);
	ImGui::DockBuilderDockWindow(CompareWindow::title, centre);
	ImGui::DockBuilderDockWindow(DigitalWindow::title, centre);
	ImGui::DockBuilderDockWindow(SignalViewer::title, centre);
	ImGui::DockBuilderDockWindow(SpectrumWindow::title, right);
	ImGui::DockBuilderDockWindow(SpectrogramWindow::title, right_bottom);
	ImGui::DockBuilderFinish(dockspace);
}
