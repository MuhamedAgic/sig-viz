#include "signal_viewer.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>

#include <imgui.h>

#include "tips.hpp"
#include "core/transforms.hpp"
#include "core/units.hpp"

namespace {

	constexpr float min_view_seconds = 0.00001f;
	constexpr float max_view_seconds = 10.0f;

	[[nodiscard]] std::string suggested_file_name(const sig::Signal& signal) {
		std::string name = signal.name;
		std::ranges::replace_if(name, [](unsigned char c) { return !std::isalnum(c) && c != '-' && c != '_' && c != '.'; }, '_');
		return std::filesystem::path(name).stem().string() + ".wav";
	}

} // namespace

// ---- frame -------------------------------------------------------------------------------------------

void SignalViewer::on_signal_changed(const Workspace& ws) {
	shown_ = ws.current;
	view_.seconds = ws.signal().default_view_seconds;
	audio_.stop(); // restarts from the new playhead on the next frame

	// Keep the listen mode valid for the new signal (real signals only have "Signal").
	const auto options = sig::audio::listen_options(ws.signal().layout);
	if (!std::ranges::contains(options, sound_.listen)) sound_.listen = options.front();
}

void SignalViewer::draw(Workspace& ws) {
	ImGui::SetNextWindowSize({ 1000.0f, 700.0f }, ImGuiCond_FirstUseEver);
	if (!open || !ImGui::Begin(title, &open)) {
		audio_.stop(); // hidden: playback doesn't advance, so the sound must not either
		if (open) ImGui::End();
		return;
	}

	// These may add, remove or switch signals, so look at the current one only afterwards.
	draw_toolbar(ws);
	draw_file_dialogs(ws);
	if (ws.current != shown_) {
		if (same_source(ws.current, shown_)) shown_ = ws.current; // a live update: keep the view
		else on_signal_changed(ws);
	}

	const sig::Signal& signal = ws.signal();
	ws.playback.advance(ImGui::GetIO().DeltaTime, signal.duration(), ws.loop_range());

	if (!status_.text.empty()) {
		const ImVec4 color = status_.is_error ? ImVec4{ 1.0f, 0.45f, 0.45f, 1.0f } : ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f };
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s", status_.text.c_str());
		ImGui::PopStyleColor();
	}

	// Playing something else than the signal itself (one band of it, a transform's result)?
	if (ws.audition) {
		ImGui::TextColored({ 0.4f, 1.0f, 0.6f, 1.0f }, "Hearing: %s", ws.audition->label.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Hear the original")) ws.audition.reset();
	}

	ImGui::TextWrapped("%s", signal.description.c_str());
	ImGui::TextDisabled("%s", std::format("{}, {} sample rate, {} samples, {:.2f} s",
		signal.is_iq() ? "I/Q" : "real", sig::format_hz(signal.sample_rate), signal.frames(), signal.duration()).c_str());
	ImGui::Spacing();

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) ws.playback.toggle(signal.duration());
		if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
			ws.selection.reset();
			ws.cursors = {};
		}
	}

	draw_transport(ws);
	draw_sound_controls(ws);
	draw_marks(ws);

	// Everything that can seek reports whether it is being dragged, so playback pauses while scrubbing.
	bool scrubbing = false;

	const double zero = 0.0, duration = signal.duration();
	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::SliderScalar("##position", ImGuiDataType_Double, &ws.playback.position, &zero, &duration, "%.3f s");
	scrubbing |= ImGui::IsItemActive();

	ImGui::SeparatorText("Overview (normalised to the peak)");
	scrubbing |= draw_overview(ws);

	ImGui::SeparatorText("Scope");
	scrubbing |= draw_scope(ws);

	ws.playback.scrubbing = scrubbing;

	// What is audible: the signal, or what it was swapped for. A swap restarts the sound.
	const TrackPtr heard = ws.audition ? ws.audition->track : ws.current;
	if (heard != heard_) {
		heard_ = heard;
		audio_.stop();
	}

	if (sound_.enabled && ws.playback.playing && !scrubbing) {
		const TimeRange loop = ws.loop_range();
		const sig::audio::Params params{
			.listen = sound_.listen,
			.speed = ws.playback.speed,
			.loop = ws.playback.loop,
			.loop_begin = loop.begin,
			.loop_end = loop.end,
		};
		audio_.play(heard->signal(), ws.playback.position, params, sound_.volume);
	}
	else {
		audio_.stop();
	}

	ImGui::End();
}

// ---- toolbar -----------------------------------------------------------------------------------------

void SignalViewer::draw_toolbar(Workspace& ws) {
	ImGui::SetNextItemWidth(ui::em(16.0f));
	TrackPtr chosen = ws.current;
	if (ui::track_combo("Signal", ws.library.tracks(), chosen)) ws.select(chosen);

	ImGui::SameLine();
	if (ImGui::Button("Load...")) open_load_dialog();

	ImGui::SameLine();
	if (ImGui::Button("Save...")) {
		pending_ = Pending::Save;
		browser_.open(FileBrowser::Mode::Save, sig::io::all_extensions(),
			"The extension picks the format: .wav .cf32 .cs16 .cs8 .cu8 .f32", suggested_file_name(ws.signal()));
	}

	if (ws.selection) {
		ImGui::SameLine();
		if (ImGui::Button("Save selection...")) {
			pending_ = Pending::SaveSelection;
			browser_.open(FileBrowser::Mode::Save, sig::io::all_extensions(),
				"Saves only the selected part. The extension picks the format.", suggested_file_name(ws.signal()));
		}
	}

	// Examples stay; everything loaded or made can be removed.
	if (!ws.current->built_in()) {
		ImGui::SameLine();
		if (ImGui::Button("Remove")) {
			ws.library.remove(ws.current);
			ws.select(ws.library.tracks().front()); // the examples are always there
		}
	}
}

// ---- load / save -------------------------------------------------------------------------------------

void SignalViewer::open_load_dialog() {
	open = true;
	pending_ = Pending::Load;
	auto extensions = sig::io::all_extensions();
	extensions.insert(extensions.end(), { ".sigmf-meta", ".sigmf-data" });
	browser_.open(FileBrowser::Mode::Open, std::move(extensions),
		"Raw files (.cf32 .cs16 .cs8 .cu8 .f32) ask for a sample rate in the next step. SigMF knows its own. "
		"You can also drop files on the window.");
}

void SignalViewer::draw_file_dialogs(Workspace& ws) {
	if (const auto chosen = browser_.draw()) {
		switch (pending_) {
		case Pending::Load:          begin_load(ws, *chosen); break;
		case Pending::Save:          save_to(ws, *chosen, false); break;
		case Pending::SaveSelection: save_to(ws, *chosen, true); break;
		case Pending::None:          break;
		}
		pending_ = Pending::None;
	}
	draw_import_dialog(ws);
}

void SignalViewer::begin_load(Workspace& ws, const std::filesystem::path& path) {
	const auto guessed = sig::io::format_from_extension(path);

	// WAV knows its own sample rate, so it loads straight away. Raw files need more info first.
	if (guessed == sig::io::Format::Wav) {
		load_file(ws, path, { .format = sig::io::Format::Wav });
		return;
	}

	// SigMF says everything itself; the dialog still opens, to load just a part of a big recording.
	import_.sigmf = sig::io::is_sigmf(path);
	if (import_.sigmf) {
		const auto info = sig::io::read_sigmf(path);
		if (!info) {
			status_ = { info.error(), true };
			return;
		}
		import_.path = info->data;
		import_.format = info->format;
		import_.sample_rate = info->sample_rate;
		import_.center_hz = info->center_hz;
	}
	else {
		import_.path = path;
		import_.format = guessed.value_or(sig::io::Format::CF32);
	}
	std::error_code error;
	import_.file_bytes = std::filesystem::file_size(import_.path, error);
	import_.start_seconds = 0.0;
	import_.max_seconds = 0.0;
	import_.open_requested = true;
}

void SignalViewer::draw_import_dialog(Workspace& ws) {
	constexpr const char* dialog_title = "Import raw samples";

	if (import_.open_requested) {
		ImGui::OpenPopup(dialog_title);
		import_.open_requested = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
	if (!ImGui::BeginPopupModal(dialog_title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

	ImGui::TextUnformatted(import_.path.filename().string().c_str());
	if (import_.sigmf) ImGui::TextDisabled("SigMF: format, sample rate and frequency come from the .sigmf-meta file.");
	else ImGui::TextDisabled("Raw files have no header, so say how to read them.");
	ImGui::Spacing();

	ImGui::BeginDisabled(import_.sigmf);
	const sig::io::FormatInfo& selected = sig::io::info(import_.format);
	ImGui::SetNextItemWidth(ui::em(8.0f));
	if (ImGui::BeginCombo("Format", std::string(selected.name).c_str())) {
		for (const sig::io::FormatInfo& f : sig::io::formats) {
			if (f.self_describing) continue; // WAV never gets here
			if (ImGui::Selectable(std::string(f.name).c_str(), f.format == import_.format)) import_.format = f.format;
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("%s", std::string(selected.description).c_str());

	ImGui::SetNextItemWidth(ui::em(12.0f));
	ImGui::InputFloat("Sample rate (Hz)", &import_.sample_rate, 0.0f, 0.0f, "%.0f");

	for (const float preset : { 48'000.0f, 250'000.0f, 1'024'000.0f, 2'048'000.0f, 2'400'000.0f }) {
		if (ImGui::SmallButton(std::format("{:g}k", preset / 1000.0f).c_str())) import_.sample_rate = preset;
		ImGui::SameLine();
	}
	ImGui::NewLine();
	ImGui::EndDisabled();

	ImGui::SetNextItemWidth(ui::em(12.0f));
	ImGui::InputDouble("Tuned to (Hz)", &import_.center_hz, 0.0, 0.0, "%.0f");
	ui::help("The frequency the receiver was tuned to (e.g. 433920000). Frequency axes then show the real radio "
		"frequency instead of the offset from the middle. Leave 0 if you don't know.");

	// How long the file is, and which part of it to load.
	const std::size_t frame = sig::io::frame_bytes(import_.format);
	const double total_seconds = frame > 0 && import_.sample_rate > 0.0f
		? static_cast<double>(import_.file_bytes / frame) / import_.sample_rate : 0.0;
	ImGui::Text("The file holds %s (%.0f MB).", sig::format_seconds(total_seconds).c_str(), static_cast<double>(import_.file_bytes) / 1e6);
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::InputDouble("Start (s)", &import_.start_seconds, 0.0, 0.0, "%.3f");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::InputDouble("Length (s)", &import_.max_seconds, 0.0, 0.0, "%.3f");
	ui::help("0 = everything. A big recording can be looked at piece by piece: load 10 s at a time.");
	import_.start_seconds = std::clamp(import_.start_seconds, 0.0, total_seconds);
	import_.max_seconds = std::max(import_.max_seconds, 0.0);
	const double loading = import_.max_seconds > 0.0 ? std::min(import_.max_seconds, total_seconds - import_.start_seconds) : total_seconds - import_.start_seconds;
	const double megabytes = loading * import_.sample_rate * (import_.format == sig::io::Format::F32 ? 4.0 : 8.0) / 1e6;
	if (megabytes > 2000.0) ui::error_text(std::format("That needs {:.0f} MB of memory: load a shorter part.", megabytes));
	ImGui::Spacing();

	if (ImGui::Button("Load", { ui::em(5.0f), 0.0f })) {
		load_file(ws, import_.path, { .format = import_.format, .sample_rate = import_.sample_rate, .center_hz = import_.center_hz,
			.start_seconds = import_.start_seconds, .max_seconds = import_.max_seconds });
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", { ui::em(5.0f), 0.0f })) ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

void SignalViewer::load_file(Workspace& ws, const std::filesystem::path& path, const sig::io::LoadOptions& options) {
	auto result = sig::io::load(path, options);
	if (!result) {
		status_ = { std::move(result.error()), true };
		return;
	}
	ws.add_and_select(std::move(*result));
	status_ = { std::format("Loaded {}", ws.current->name()), false };
}

void SignalViewer::save_to(const Workspace& ws, std::filesystem::path path, bool selection_only) {
	auto format = sig::io::format_from_extension(path);
	if (!format) { // no / unknown extension: default to WAV
		path += ".wav";
		format = sig::io::Format::Wav;
	}

	sig::io::Result result;
	if (selection_only && ws.selection) {
		const auto part = sig::transform::trim(ws.signal(), ws.selection->begin, ws.selection->end);
		result = part ? sig::io::save(*part, path, *format) : std::unexpected(part.error());
	}
	else {
		result = sig::io::save(ws.signal(), path, *format);
	}

	if (!result) status_ = { result.error(), true };
	else         status_ = { std::format("Saved {}", path.string()), false };
}

// ---- transport, sound, marks ---------------------------------------------------------------------------

void SignalViewer::draw_transport(Workspace& ws) {
	Playback& playback = ws.playback;
	const double duration = ws.signal().duration();

	if (ImGui::Button(playback.playing ? "Pause" : "Play", { ui::em(4.0f), 0.0f })) playback.toggle(duration);
	ImGui::SameLine();
	if (ImGui::Button("Stop", { ui::em(4.0f), 0.0f })) {
		playback.playing = false;
		playback.position = ws.selection ? ws.selection->begin : 0.0;
	}
	ImGui::SameLine();
	ImGui::Checkbox(ws.selection ? "Loop selection" : "Loop", &playback.loop);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(10.0f));
	ImGui::SliderFloat("Speed", &playback.speed, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
}

void SignalViewer::draw_sound_controls(const Workspace& ws) {
	ImGui::BeginDisabled(!audio_.available());
	ImGui::Checkbox("Sound", &sound_.enabled);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::SliderFloat("Volume", &sound_.volume, 0.0f, 1.0f, "%.2f");
	ImGui::EndDisabled();

	if (!audio_.available()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(no audio device)");
		return;
	}

	// I/Q signals have no single "sound": pick what to listen to.
	const auto options = sig::audio::listen_options(ws.signal().layout);
	if (options.size() < 2) return;

	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(12.0f));
	ui::enum_combo("Listen to", sound_.listen, options);
	ui::help(tips::listen);
}

// The selection and cursor readouts, with buttons to clear them.
void SignalViewer::draw_marks(Workspace& ws) {
	bool any = false;

	if (ws.selection) {
		const TimeRange& s = *ws.selection;
		ImGui::Text("Selection: %s - %s (%s)", sig::format_seconds(s.begin).c_str(), sig::format_seconds(s.end).c_str(),
			sig::format_seconds(s.length()).c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##selection")) ws.selection.reset();
		any = true;
	}

	const auto& [a, b] = ws.cursors;
	if (a || b) {
		if (any) ImGui::SameLine(0.0f, ui::em(2.0f));
		std::string text = "Cursors:";
		if (a) text += " A " + sig::format_seconds(*a);
		if (b) text += "  B " + sig::format_seconds(*b);
		if (a && b) {
			const double dt = std::abs(*b - *a);
			text += "  |  dt " + sig::format_seconds(dt);
			if (dt > 0.0) text += " = " + sig::format_hz(1.0 / dt);
		}
		ImGui::TextUnformatted(text.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Clear##cursors")) ws.cursors = {};
		any = true;
	}

	if (!any) ImGui::TextDisabled("Shift + drag to select, right-click to place cursors, Esc clears them.");
}

// ---- plots -------------------------------------------------------------------------------------------

// Left-drag scrubs (or selects, with shift held when it started), right-click places a cursor.
//   grab: true  = the plot follows the mouse like paper being pulled (scope)
//         false = the playhead jumps to the mouse (overview)
// Returns true while scrubbing.
bool SignalViewer::handle_mouse(Workspace& ws, const ui::Area& area, const waveform::Canvas& canvas, bool grab) {
	const ImGuiIO& io = ImGui::GetIO();
	const double duration = ws.signal().duration();
	const double mouse_t = std::clamp(canvas.t_at(io.MousePos.x), 0.0, duration);

	if (area.activated) {
		drag_ = io.KeyShift ? Drag::Select : Drag::Scrub;
		select_anchor_ = mouse_t;
	}

	if (area.dragging) {
		if (drag_ == Drag::Select) {
			if (mouse_t != select_anchor_) ws.selection = TimeRange{ std::min(mouse_t, select_anchor_), std::max(mouse_t, select_anchor_) };
		}
		else if (grab) {
			const double delta = io.MouseDelta.x / canvas.width() * (canvas.t_end - canvas.t_begin);
			ws.playback.position = std::clamp(ws.playback.position - delta, 0.0, duration);
		}
		else {
			ws.playback.position = mouse_t;
		}
	}

	if (area.hovered) ws.next_hover.seconds = mouse_t;

	if (area.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
		ws.cursors[static_cast<std::size_t>(next_cursor_)] = mouse_t;
		next_cursor_ ^= 1;
	}

	return area.dragging && drag_ == Drag::Scrub;
}

void SignalViewer::draw_marks_on(ImDrawList& dl, const waveform::Canvas& canvas, const Workspace& ws) const {
	if (ws.selection) waveform::draw_selection(dl, canvas, ws.selection->begin, ws.selection->end);
	if (ws.cursors[0]) waveform::draw_cursor(dl, canvas, *ws.cursors[0], "A");
	if (ws.cursors[1]) waveform::draw_cursor(dl, canvas, *ws.cursors[1], "B");
	if (ws.hover.seconds) { // where the mouse points, here or in another window
		const float x = canvas.x_at(*ws.hover.seconds);
		if (x >= canvas.min.x && x <= canvas.max.x) dl.AddLine({ x, canvas.min.y }, { x, canvas.max.y }, IM_COL32(255, 255, 255, 70));
	}
	waveform::draw_playhead(dl, canvas, ws.playback.position);
}

// The whole signal at a glance.
bool SignalViewer::draw_overview(Workspace& ws) {
	const Track&   track = *ws.current;
	const ui::Area area = ui::interactive_area("##overview", { ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() * 5.0f });

	const waveform::Canvas canvas{
		.min = area.min,
		.max = area.max,
		.t_begin = 0.0,
		.t_end = track.signal().duration(),
		.gain = track.peak() > 0.0f ? 1.0f / track.peak() : 1.0f,
	};
	const bool scrubbing = handle_mouse(ws, area, canvas, false);

	ImDrawList& dl = *ImGui::GetWindowDrawList();
	waveform::draw_track(dl, canvas, track, false);

	// Highlight the part the scope is showing.
	const double half = view_.seconds * 0.5;
	const float  x0 = std::clamp(canvas.x_at(ws.playback.position - half), canvas.min.x, canvas.max.x);
	const float  x1 = std::clamp(canvas.x_at(ws.playback.position + half), canvas.min.x, canvas.max.x);
	dl.AddRectFilled({ x0, canvas.min.y }, { std::max(x1, x0 + 2.0f), canvas.max.y }, IM_COL32(255, 255, 255, 35));

	draw_marks_on(dl, canvas, ws);
	return scrubbing;
}

// Zoomed view centred on the playhead.
bool SignalViewer::draw_scope(Workspace& ws) {
	const Track& track = *ws.current;

	ImGui::SetNextItemWidth(ui::em(12.0f));
	ImGui::SliderFloat("Window", &view_.seconds, min_view_seconds, max_view_seconds, "%.5f s", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::SliderFloat("Gain", &view_.gain, 0.1f, 100.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	if (ImGui::Button("Fit") && track.peak() > 0.0f) view_.gain = std::clamp(1.0f / track.peak(), 0.1f, 100.0f);
	ImGui::SameLine();
	ImGui::TextDisabled("Wheel: zoom   Space: play/pause");

	const ImVec2   avail = ImGui::GetContentRegionAvail();
	const ui::Area area = ui::interactive_area("##scope", { avail.x, std::max(avail.y, 150.0f) });

	const double half = view_.seconds * 0.5;
	const waveform::Canvas canvas{
		.min = area.min,
		.max = area.max,
		.t_begin = ws.playback.position - half,
		.t_end = ws.playback.position + half,
		.gain = view_.gain,
	};
	const bool scrubbing = handle_mouse(ws, area, canvas, true);

	const float wheel = ImGui::GetIO().MouseWheel;
	if (area.hovered && wheel != 0.0f)
		view_.seconds = std::clamp(view_.seconds * std::pow(0.85f, wheel), min_view_seconds, max_view_seconds);

	ImDrawList& dl = *ImGui::GetWindowDrawList();
	waveform::draw_track(dl, canvas, track, true);
	draw_marks_on(dl, canvas, ws);
	return scrubbing;
}
