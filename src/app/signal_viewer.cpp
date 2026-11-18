#include "signal_viewer.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <format>

#include <imgui.h>

#include "waveform.hpp"
#include "core/examples.hpp"

namespace {

	constexpr float min_view_seconds = 0.00001f;
	constexpr float max_view_seconds = 10.0f;

	// An invisible button of `size` at the cursor: a rectangle to draw in that reacts to the mouse.
	struct Area {
		ImVec2 min;
		ImVec2 max;
		bool   dragging;
		bool   hovered;
	};

	Area interactive_area(const char* id, ImVec2 size) {
		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(id, size);
		return { min, { min.x + size.x, min.y + size.y }, ImGui::IsItemActive(), ImGui::IsItemHovered() };
	}

} // namespace

// ---- playback ----------------------------------------------------------------------------------------

void SignalViewer::Playback::advance(float dt, float duration) {
	if (!playing || scrubbing) return;

	position += dt * speed;
	if (position < duration) return;

	if (loop) {
		position = std::fmod(position, duration);
	}
	else {
		position = duration;
		playing = false;
	}
}

void SignalViewer::Playback::toggle(float duration) {
	if (!playing && position >= duration) position = 0.0f; // replay from the start
	playing = !playing;
}

// ---- frame -------------------------------------------------------------------------------------------

SignalViewer::SignalViewer()
	: signals_{ sig::make_example_signals() }, example_count_{ signals_.size() } {
	select(0);
}

void SignalViewer::select(std::size_t index) {
	selected_ = index;
	const sig::Signal& signal = signals_[index];

	playback_.position = 0.0f;
	view_.seconds = signal.default_view_seconds;
	audio_.stop(); // restarts from the new playhead on the next frame

	// One pass over the samples now, so drawing never has to touch all of them.
	peaks_.clear();
	sig::MinMax extremes;
	for (const auto& channel : signal.channels) {
		extremes.add(peaks_.emplace_back(channel).all());
	}
	peak_ = extremes.peak();

	// Keep the listen mode valid for the new signal (real signals only have "Signal").
	const auto options = sig::audio::listen_options(signal.layout);
	if (!std::ranges::contains(options, sound_.listen)) sound_.listen = options.front();
}

void SignalViewer::draw() {
	ImGui::SetNextWindowSize({ 1000.0f, 700.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Signal Viewer")) {
		audio_.stop(); // collapsed or hidden tab: playback doesn't advance, so the sound must not either
		ImGui::End();
		return;
	}

	// These may add, remove or switch signals, so look up the current one only afterwards.
	draw_toolbar();
	draw_file_dialogs();

	const sig::Signal& signal = current();
	const float        duration = signal.duration();

	playback_.advance(ImGui::GetIO().DeltaTime, duration);

	if (!status_.text.empty()) {
		const ImVec4 color = status_.is_error ? ImVec4{ 1.0f, 0.45f, 0.45f, 1.0f } : ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f };
		ImGui::PushStyleColor(ImGuiCol_Text, color);
		ImGui::TextWrapped("%s", status_.text.c_str());
		ImGui::PopStyleColor();
	}

	ImGui::TextWrapped("%s", signal.description.c_str());
	ImGui::TextDisabled("%s", std::format("{}, {:.0f} Hz sample rate, {} samples, {:.2f} s",
		signal.layout == sig::Layout::IQ ? "I/Q" : "real", signal.sample_rate, signal.frames(), duration).c_str());
	ImGui::Spacing();

	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Space, false))
		playback_.toggle(duration);

	draw_transport(duration);
	draw_sound_controls(signal);

	// Everything that can seek reports whether it is being dragged, so playback pauses while scrubbing.
	bool scrubbing = false;

	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::SliderFloat("##position", &playback_.position, 0.0f, duration, "%.3f s");
	scrubbing |= ImGui::IsItemActive();

	ImGui::SeparatorText("Overview (click / drag to seek, normalised to the peak)");
	scrubbing |= draw_overview(signal);

	ImGui::SeparatorText("Scope");
	scrubbing |= draw_scope(signal);

	playback_.scrubbing = scrubbing;

	if (sound_.enabled && playback_.playing && !scrubbing) {
		const sig::audio::Params params{ .listen = sound_.listen, .speed = playback_.speed, .loop = playback_.loop };
		audio_.play(signal, playback_.position, params, sound_.volume);
	}
	else {
		audio_.stop();
	}

	ImGui::End();
}

// ---- toolbar -----------------------------------------------------------------------------------------

void SignalViewer::draw_toolbar() {
	const float unit = ImGui::GetFontSize();

	ImGui::SetNextItemWidth(unit * 16.0f);
	if (ImGui::BeginCombo("Signal", current().name.c_str())) {
		for (std::size_t i = 0; i < signals_.size(); ++i) {
			const bool is_selected = i == selected_;
			if (ImGui::Selectable(signals_[i].name.c_str(), is_selected) && !is_selected) select(i);
			if (is_selected) ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine();
	if (ImGui::Button("Load...")) {
		pending_ = Pending::Load;
		browser_.open(FileBrowser::Mode::Open, sig::io::all_extensions(),
			"Raw files (.cf32 .cs16 .cs8 .cu8 .f32) ask for a sample rate in the next step.");
	}

	ImGui::SameLine();
	if (ImGui::Button("Save...")) {
		pending_ = Pending::Save;
		browser_.open(FileBrowser::Mode::Save, sig::io::all_extensions(),
			"The extension picks the format: .wav .cf32 .cs16 .cs8 .cu8 .f32", suggested_file_name());
	}

	// Only signals that came from a file can be removed; the built-in examples stay.
	if (selected_ >= example_count_) {
		ImGui::SameLine();
		if (ImGui::Button("Remove")) {
			signals_.erase(signals_.begin() + static_cast<std::ptrdiff_t>(selected_));
			select(std::min(selected_, signals_.size() - 1));
		}
	}
}

std::string SignalViewer::suggested_file_name() const {
	std::string name = current().name;
	std::ranges::replace_if(name, [](unsigned char c) { return !std::isalnum(c) && c != '-' && c != '_' && c != '.'; }, '_');
	return std::filesystem::path(name).stem().string() + ".wav";
}

// ---- load / save -------------------------------------------------------------------------------------

void SignalViewer::draw_file_dialogs() {
	if (const auto chosen = browser_.draw()) {
		switch (pending_) {
		case Pending::Load: begin_load(*chosen); break;
		case Pending::Save: save_to(*chosen);    break;
		case Pending::None: break;
		}
		pending_ = Pending::None;
	}
	draw_import_dialog();
}

void SignalViewer::begin_load(const std::filesystem::path& path) {
	const auto guessed = sig::io::format_from_extension(path);

	// WAV knows its own sample rate, so it loads straight away. Raw files need more info first.
	if (guessed == sig::io::Format::Wav) {
		load_file(path, { .format = sig::io::Format::Wav });
		return;
	}
	import_.path = path;
	import_.format = guessed.value_or(sig::io::Format::CF32);
	import_.open_requested = true;
}

void SignalViewer::draw_import_dialog() {
	constexpr const char* title = "Import raw samples";

	if (import_.open_requested) {
		ImGui::OpenPopup(title);
		import_.open_requested = false;
	}
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
	if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

	const float unit = ImGui::GetFontSize();

	ImGui::TextUnformatted(import_.path.filename().string().c_str());
	ImGui::TextDisabled("Raw files have no header, so say how to read them.");
	ImGui::Spacing();

	const sig::io::FormatInfo& selected = sig::io::info(import_.format);
	ImGui::SetNextItemWidth(unit * 8.0f);
	if (ImGui::BeginCombo("Format", std::string(selected.name).c_str())) {
		for (const sig::io::FormatInfo& f : sig::io::formats) {
			if (f.self_describing) continue; // WAV never gets here
			if (ImGui::Selectable(std::string(f.name).c_str(), f.format == import_.format)) import_.format = f.format;
		}
		ImGui::EndCombo();
	}
	ImGui::TextDisabled("%s", std::string(selected.description).c_str());

	ImGui::SetNextItemWidth(unit * 12.0f);
	ImGui::InputFloat("Sample rate (Hz)", &import_.sample_rate, 0.0f, 0.0f, "%.0f");

	for (const float preset : { 48'000.0f, 250'000.0f, 1'024'000.0f, 2'048'000.0f, 2'400'000.0f }) {
		if (ImGui::SmallButton(std::format("{:g}k", preset / 1000.0f).c_str())) import_.sample_rate = preset;
		ImGui::SameLine();
	}
	ImGui::NewLine();
	ImGui::Spacing();

	if (ImGui::Button("Load", { unit * 5.0f, 0.0f })) {
		load_file(import_.path, { .format = import_.format, .sample_rate = import_.sample_rate });
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", { unit * 5.0f, 0.0f })) ImGui::CloseCurrentPopup();

	ImGui::EndPopup();
}

void SignalViewer::load_file(const std::filesystem::path& path, const sig::io::LoadOptions& options) {
	auto result = sig::io::load(path, options);
	if (!result) {
		status_ = { std::move(result.error()), true };
		return;
	}
	signals_.push_back(std::move(*result));
	select(signals_.size() - 1);
	status_ = { std::format("Loaded {}", current().name), false };
}

void SignalViewer::save_to(std::filesystem::path path) {
	auto format = sig::io::format_from_extension(path);
	if (!format) { // no / unknown extension: default to WAV
		path += ".wav";
		format = sig::io::Format::Wav;
	}

	if (const auto result = sig::io::save(current(), path, *format); !result)
		status_ = { result.error(), true };
	else
		status_ = { std::format("Saved {}", path.string()), false };
}

// ---- transport and sound -----------------------------------------------------------------------------

void SignalViewer::draw_transport(float duration) {
	const float unit = ImGui::GetFontSize();

	if (ImGui::Button(playback_.playing ? "Pause" : "Play", { unit * 4.0f, 0.0f })) playback_.toggle(duration);
	ImGui::SameLine();
	if (ImGui::Button("Stop", { unit * 4.0f, 0.0f })) {
		playback_.playing = false;
		playback_.position = 0.0f;
	}
	ImGui::SameLine();
	ImGui::Checkbox("Loop", &playback_.loop);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(unit * 10.0f);
	ImGui::SliderFloat("Speed", &playback_.speed, 0.05f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
}

void SignalViewer::draw_sound_controls(const sig::Signal& signal) {
	const float unit = ImGui::GetFontSize();

	ImGui::BeginDisabled(!audio_.available());
	ImGui::Checkbox("Sound", &sound_.enabled);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(unit * 8.0f);
	ImGui::SliderFloat("Volume", &sound_.volume, 0.0f, 1.0f, "%.2f");
	ImGui::EndDisabled();

	if (!audio_.available()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(no audio device)");
		return;
	}

	// I/Q signals have no single "sound": pick what to listen to.
	const auto options = sig::audio::listen_options(signal.layout);
	if (options.size() < 2) return;

	ImGui::SameLine();
	ImGui::SetNextItemWidth(unit * 12.0f);
	if (ImGui::BeginCombo("Listen to", std::string(sig::audio::name(sound_.listen)).c_str())) {
		for (const sig::audio::Listen option : options) {
			if (ImGui::Selectable(std::string(sig::audio::name(option)).c_str(), option == sound_.listen))
				sound_.listen = option;
		}
		ImGui::EndCombo();
	}
}

// ---- plots -------------------------------------------------------------------------------------------

// The whole signal at a glance.
bool SignalViewer::draw_overview(const sig::Signal& signal) {
	const float duration = signal.duration();
	const Area  area = interactive_area("##overview", { ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight() * 5.0f });

	if (area.dragging) {
		const float u = (ImGui::GetIO().MousePos.x - area.min.x) / (area.max.x - area.min.x);
		playback_.position = std::clamp(u, 0.0f, 1.0f) * duration;
	}

	const waveform::Canvas canvas{
		.min = area.min,
		.max = area.max,
		.t_begin = 0.0,
		.t_end = duration,
		.gain = peak_ > 0.0f ? 1.0f / peak_ : 1.0f,
	};

	ImDrawList& dl = *ImGui::GetWindowDrawList();
	waveform::draw_signal(dl, canvas, signal, peaks_, false);

	// Highlight the part the scope is showing.
	const double half = view_.seconds * 0.5;
	const float  x0 = std::clamp(canvas.x_at(playback_.position - half), canvas.min.x, canvas.max.x);
	const float  x1 = std::clamp(canvas.x_at(playback_.position + half), canvas.min.x, canvas.max.x);
	dl.AddRectFilled({ x0, canvas.min.y }, { std::max(x1, x0 + 2.0f), canvas.max.y }, IM_COL32(255, 255, 255, 35));

	waveform::draw_playhead(dl, canvas, playback_.position);
	return area.dragging;
}

// Zoomed view centred on the playhead.
bool SignalViewer::draw_scope(const sig::Signal& signal) {
	const float unit = ImGui::GetFontSize();

	ImGui::SetNextItemWidth(unit * 12.0f);
	ImGui::SliderFloat("Window", &view_.seconds, min_view_seconds, max_view_seconds, "%.5f s", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(unit * 8.0f);
	ImGui::SliderFloat("Gain", &view_.gain, 0.1f, 100.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	if (ImGui::Button("Fit") && peak_ > 0.0f) view_.gain = std::clamp(1.0f / peak_, 0.1f, 100.0f);
	ImGui::TextDisabled("Drag the scope to scrub, mouse wheel to zoom, Space to play/pause.");

	const ImVec2   avail = ImGui::GetContentRegionAvail();
	const Area     area = interactive_area("##scope", { avail.x, std::max(avail.y, 150.0f) });
	const ImGuiIO& io = ImGui::GetIO();

	if (area.dragging) {
		const float delta = io.MouseDelta.x / (area.max.x - area.min.x) * view_.seconds;
		playback_.position = std::clamp(playback_.position - delta, 0.0f, signal.duration());
	}
	if (area.hovered && io.MouseWheel != 0.0f)
		view_.seconds = std::clamp(view_.seconds * std::pow(0.85f, io.MouseWheel), min_view_seconds, max_view_seconds);

	const double           half = view_.seconds * 0.5;
	const waveform::Canvas canvas{
		.min = area.min,
		.max = area.max,
		.t_begin = playback_.position - half,
		.t_end = playback_.position + half,
		.gain = view_.gain,
	};

	ImDrawList& dl = *ImGui::GetWindowDrawList();
	waveform::draw_signal(dl, canvas, signal, peaks_, true);
	waveform::draw_playhead(dl, canvas, playback_.position);
	return area.dragging;
}
