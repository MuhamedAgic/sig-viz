#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>

#include <imgui_internal.h> // DockBuilder API, viewport side bars
#include <implot.h>
#include <raylib.h>

#include "screenshot.hpp"
#include "session.hpp"
#include "ui.hpp"
#include "core/units.hpp"

void App::draw() {
	take_dropped_files();
	if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false)) palette_.open();

	draw_menu_bar();
	draw_status_bar(); // before the dock space, so the windows fit above it

	const ImGuiID dockspace = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
	if (check_layout_ || reset_layout_) {
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspace);
		if (reset_layout_ || node == nullptr || node->IsLeafNode()) {
			build_default_layout(dockspace);
			pick_tabs_in_ = 2; // the windows get docked during the next frame; then pick the tabs to show
		}
		check_layout_ = reset_layout_ = false;
	}

	// Live input first (it may update the signal), then the viewer: it advances the playhead the others follow.
	live_.draw(ws_);
	viewer_.draw(ws_);
	spectrum_.draw(ws_);
	spectrogram_.draw(ws_);
	compare_.draw(ws_);
	transform_.draw(ws_);
	generator_.draw(ws_);
	digital_.draw(ws_);
	guide_.draw(ws_);
	bursts_.draw(ws_);
	audio_.draw(ws_);
	coding_.draw(ws_);

	if (pick_tabs_in_ > 0 && --pick_tabs_in_ == 0) {
		ImGui::SetWindowFocus(GuideWindow::title);
		ImGui::SetWindowFocus(SpectrumWindow::title);
		ImGui::SetWindowFocus(SignalViewer::title);
	}

	palette_.draw(commands());
	draw_session_dialog();

	if (show_imgui_demo_) ImGui::ShowDemoWindow(&show_imgui_demo_);
	if (show_implot_demo_) ImPlot::ShowDemoWindow(&show_implot_demo_);

	ws_.end_frame();
}

void App::show(bool& window_open, const char* title) {
	window_open = true;
	ImGui::SetWindowFocus(title);
}

// ---- files ---------------------------------------------------------------------------------------------

// Files dragged from Explorer onto the window load like "Load...".
void App::take_dropped_files() {
	if (!IsFileDropped()) return;
	const FilePathList files = LoadDroppedFiles();
	for (unsigned i = 0; i < files.count; ++i) {
		const std::filesystem::path path{ reinterpret_cast<const char8_t*>(files.paths[i]) }; // raylib gives UTF-8
		if (path.extension() == ".sigviz") {
			const auto loaded = session::load(ws_, path);
			message_ = loaded ? "Loaded the session " + path.filename().string() : loaded.error();
		}
		else {
			viewer_.begin_load(ws_, path);
		}
	}
	UnloadDroppedFiles(files);
	show(viewer_.open, SignalViewer::title);
}

void App::draw_session_dialog() {
	const auto path = session_browser_.draw();
	if (!path) return;
	std::filesystem::path file = *path;
	if (file.extension() != ".sigviz") file += ".sigviz";
	if (session_action_ == SessionAction::Save) {
		const auto saved = session::save(ws_, file);
		message_ = saved ? "Saved the session to " + file.string() : saved.error();
	}
	else if (session_action_ == SessionAction::Load) {
		const auto loaded = session::load(ws_, file);
		message_ = loaded ? "Loaded the session " + file.filename().string() : loaded.error();
	}
	session_action_ = SessionAction::None;
}

// ---- menus, palette, status bar ---------------------------------------------------------------------

std::vector<Command> App::commands() {
	std::vector<Command> list;
	const auto add = [&](std::string name, std::function<void()> run) { list.push_back({ std::move(name), std::move(run) }); };

	add("Load a signal...", [this] { viewer_.open_load_dialog(); });
	add("Save session...", [this] {
		session_action_ = SessionAction::Save;
		session_browser_.open(FileBrowser::Mode::Save, { ".sigviz" }, "All signals, the playhead and the marks.", "session.sigviz");
	});
	add("Load session...", [this] {
		session_action_ = SessionAction::Load;
		session_browser_.open(FileBrowser::Mode::Open, { ".sigviz" });
	});
	add("Play / pause", [this] { ws_.playback.toggle(ws_.signal().duration()); });
	add("Clear selection and cursors", [this] {
		ws_.selection.reset();
		ws_.cursors = {};
	});
	add("Reset layout", [this] { reset_layout_ = true; });

	add("Show: Signal Viewer", [this] { show(viewer_.open, SignalViewer::title); });
	add("Show: Spectrum", [this] { show(spectrum_.open, SpectrumWindow::title); });
	add("Show: Spectrogram", [this] { show(spectrogram_.open, SpectrogramWindow::title); });
	add("Show: Compare", [this] { show(compare_.open, CompareWindow::title); });
	add("Show: Generator", [this] { show(generator_.open, GeneratorWindow::title); });
	add("Show: Digital (decode bits)", [this] { show(digital_.open, DigitalWindow::title); });
	add("Show: Bursts (find transmissions)", [this] { show(bursts_.open, BurstsWindow::title); });
	add("Show: Audio (tuner, loudness, levels)", [this] { show(audio_.open, AudioWindow::title); });
	add("Show: Live (microphone, radio)", [this] { show(live_.open, LiveWindow::title); });
	add("Show: Error correction (polar codes, interleaving)", [this] { show(coding_.open, CodingWindow::title); });
	add("Show: Guide", [this] { show(guide_.open, GuideWindow::title); });

	for (const auto op : sig::recipe::all_ops) {
		add(std::format("Transform: {}", sig::recipe::name(op)), [this, op] {
			transform_.pick(op);
			ImGui::SetWindowFocus(TransformWindow::title);
		});
	}
	const auto topics = GuideWindow::topics();
	for (int i = 0; i < static_cast<int>(topics.size()); ++i) add("Guide: " + topics[static_cast<std::size_t>(i)], [this, i] { guide_.show_topic(i); });
	const auto puzzles = GuideWindow::puzzles();
	for (int i = 0; i < static_cast<int>(puzzles.size()); ++i) add("Puzzle: " + puzzles[static_cast<std::size_t>(i)], [this, i] { guide_.show_puzzle(i); });
	for (const TrackPtr& track : ws_.library.tracks()) add("Signal: " + track->name(), [this, track] { ws_.select(track); });
	return list;
}

void App::draw_menu_bar() {
	if (!ImGui::BeginMainMenuBar()) return;

	if (ImGui::BeginMenu("File")) {
		if (ImGui::MenuItem("Load a signal...")) viewer_.open_load_dialog();
		ImGui::TextDisabled("  (or drop files on the window)");
		ImGui::Separator();
		if (ImGui::MenuItem("Save session...")) {
			session_action_ = SessionAction::Save;
			session_browser_.open(FileBrowser::Mode::Save, { ".sigviz" }, "All signals, the playhead and the marks.", "session.sigviz");
		}
		if (ImGui::MenuItem("Load session...")) {
			session_action_ = SessionAction::Load;
			session_browser_.open(FileBrowser::Mode::Open, { ".sigviz" });
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Find a command...", "Ctrl+P")) palette_.open();
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Windows")) {
		ImGui::MenuItem(SignalViewer::title, nullptr, &viewer_.open);
		ImGui::MenuItem(SpectrumWindow::title, nullptr, &spectrum_.open);
		ImGui::MenuItem(SpectrogramWindow::title, nullptr, &spectrogram_.open);
		ImGui::MenuItem(CompareWindow::title, nullptr, &compare_.open);
		ImGui::MenuItem(TransformWindow::title, nullptr, &transform_.open);
		ImGui::MenuItem(GeneratorWindow::title, nullptr, &generator_.open);
		ImGui::MenuItem(DigitalWindow::title, nullptr, &digital_.open);
		ImGui::MenuItem(BurstsWindow::title, nullptr, &bursts_.open);
		ImGui::MenuItem(AudioWindow::title, nullptr, &audio_.open);
		ImGui::MenuItem(LiveWindow::title, nullptr, &live_.open);
		ImGui::MenuItem(CodingWindow::title, nullptr, &coding_.open);
		ImGui::MenuItem(GuideWindow::title, nullptr, &guide_.open);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset layout")) {
			viewer_.open = spectrum_.open = spectrogram_.open = compare_.open = true;
			transform_.open = generator_.open = digital_.open = guide_.open = true;
			bursts_.open = audio_.open = live_.open = coding_.open = true;
			reset_layout_ = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help")) {
		if (ImGui::MenuItem("Guide: what is all this?")) show(guide_.open, GuideWindow::title);
		if (ImGui::MenuItem("Find a command...", "Ctrl+P")) palette_.open();
		ImGui::SeparatorText("Viewer");
		ImGui::TextUnformatted("Drag            move the playhead");
		ImGui::TextUnformatted("Shift + drag    select a range");
		ImGui::TextUnformatted("Right-click     place cursor A / B");
		ImGui::TextUnformatted("Mouse wheel     zoom the scope");
		ImGui::TextUnformatted("Space           play / pause");
		ImGui::TextUnformatted("Esc             clear selection and cursors");
		ImGui::SeparatorText("Spectrum");
		ImGui::TextUnformatted("Right-click     marker on the nearest peak");
		ImGui::SeparatorText("Spectrogram");
		ImGui::TextUnformatted("Shift + drag    mark a band (listen / extract)");
		ImGui::SeparatorText("Plots");
		ImGui::TextUnformatted("Drag / wheel    pan / zoom");
		ImGui::TextUnformatted("Double-click    fit to data");
		ImGui::Separator();
		ImGui::MenuItem("ImGui demo", nullptr, &show_imgui_demo_);
		ImGui::MenuItem("ImPlot demo", nullptr, &show_implot_demo_);
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

// One line along the bottom: what the mouse points at, the current signal, and the latest message.
void App::draw_status_bar() {
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	if (!ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
		ImGui::End();
		return;
	}
	if (ImGui::BeginMenuBar()) {
		const sig::Signal& signal = ws_.signal();
		std::string text = std::format("{}  |  {}, {}, {:.2f} s", ws_.current->name(), signal.is_iq() ? "I/Q" : "real",
			sig::format_hz(signal.sample_rate), signal.duration());
		if (signal.center_hz != 0.0) text += ", tuned to " + sig::format_hz(signal.center_hz);
		ImGui::TextUnformatted(text.c_str());

		// What the mouse points at, in whichever window.
		if (const auto& t = ws_.hover.seconds; t && *t >= 0.0 && *t < signal.duration()) {
			const auto z = signal.complex_at(static_cast<std::size_t>(*t * signal.sample_rate));
			ImGui::SameLine(0.0f, ui::em(2.0f));
			ImGui::TextDisabled("t %s", sig::format_seconds(*t).c_str());
			ImGui::SameLine();
			if (signal.is_iq()) ImGui::TextDisabled("I %+.4f  Q %+.4f  |z| %.4f", z.real(), z.imag(), std::abs(z));
			else ImGui::TextDisabled("value %+.4f", z.real());
		}
		if (const auto& hz = ws_.hover.hz) {
			ImGui::SameLine(0.0f, ui::em(2.0f));
			ImGui::TextDisabled("f %s", sig::format_hz(*hz).c_str());
		}

		const std::string& note = !screenshot::last_message().empty() ? screenshot::last_message() : message_;
		const std::string right = note.empty() ? "Ctrl+P: find a command" : note;
		ImGui::SameLine(std::max(ImGui::GetCursorPosX() + ui::em(2.0f), ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x - ui::em(1.0f)));
		ImGui::TextDisabled("%s", right.c_str());
		ImGui::EndMenuBar();
	}
	ImGui::End();
}

// Guide, Generator, Transform, Live and Bursts on the left; the viewer (with Compare, Digital and Error
// correction as tabs)
// in the middle; Spectrum (and Audio) above Spectrogram on the right.
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
	ImGui::DockBuilderDockWindow(LiveWindow::title, left);
	ImGui::DockBuilderDockWindow(BurstsWindow::title, left);
	ImGui::DockBuilderDockWindow(GuideWindow::title, left);
	ImGui::DockBuilderDockWindow(CompareWindow::title, centre);
	ImGui::DockBuilderDockWindow(DigitalWindow::title, centre);
	ImGui::DockBuilderDockWindow(CodingWindow::title, centre);
	ImGui::DockBuilderDockWindow(SignalViewer::title, centre);
	ImGui::DockBuilderDockWindow(SpectrumWindow::title, right);
	ImGui::DockBuilderDockWindow(AudioWindow::title, right);
	ImGui::DockBuilderDockWindow(SpectrogramWindow::title, right_bottom);
	ImGui::DockBuilderFinish(dockspace);
}
