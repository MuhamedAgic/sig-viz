#include "live_window.hpp"

#include <algorithm>
#include <ctime>
#include <format>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "tips.hpp"
#include "ui.hpp"
#include "core/units.hpp"

namespace {

	using Clock = std::chrono::steady_clock;

	std::uint64_t next_live_id = 1;

	constexpr std::size_t max_recorded =
		200'000'000; // samples per channel (800 MB): then the recording stops by itself

	std::string time_of_day() {
		const std::time_t now = std::time(nullptr);
		std::tm           local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		return std::format("{:02}:{:02}:{:02}", local.tm_hour, local.tm_min, local.tm_sec);
	}

} // namespace

void LiveWindow::start(Workspace& ws) {
	error_.clear();
	input_ = source_ == Source::Microphone ? open_microphone(error_) : open_rtl_tcp(radio_, error_);
	if (!input_) {
		return;
	}
	i_.clear();
	q_.clear();
	live_id_ = next_live_id++;
	received_ = 0.0;
	started_ = Clock::now();

	// An empty live track to start with, shown right away.
	sig::Signal signal{
		.name = std::format("Live: {}", name(source_)),
		.description = source_ == Source::Microphone
		                   ? "The microphone, live."
		                   : std::format("rtl_tcp at {}, live.", sig::format_hz(radio_.frequency_hz)),
		.sample_rate = input_->sample_rate(),
		.default_view_seconds = source_ == Source::Microphone ? 0.02f : 0.002f,
		.layout = input_->iq() ? sig::Layout::IQ : sig::Layout::Real,
		.channels = std::vector<std::vector<float>>(input_->iq() ? 2 : 1, std::vector<float>(1, 0.0f)),
		.center_hz = source_ == Source::Radio ? radio_.frequency_hz : 0.0,
	};
	track_ = make_track(std::move(signal), false, {}, live_id_);
	ws.select(ws.library.add(track_));
	last_update_ = Clock::now();
}

void LiveWindow::stop() {
	input_.reset();
	track_.reset(); // the last update stays in the library as a normal signal
}

void LiveWindow::start_recording() {
	recorded_i_.clear();
	recorded_q_.clear();
	recording_ = true;
}

void LiveWindow::finish_recording(Workspace& ws) {
	if (!recording_) {
		return;
	}
	recording_ = false;
	if (recorded_i_.empty() || !input_) {
		return;
	}
	sig::Signal signal{
		.name = std::format("Recording {}", time_of_day()),
		.description = "",
		.sample_rate = input_->sample_rate(),
		.default_view_seconds = source_ == Source::Microphone ? 0.02f : 0.002f,
		.layout = input_->iq() ? sig::Layout::IQ : sig::Layout::Real,
		.channels = { std::move(recorded_i_) },
		.center_hz = source_ == Source::Radio ? radio_.frequency_hz : 0.0,
	};
	if (input_->iq()) {
		signal.channels.push_back(std::move(recorded_q_));
	}
	signal.description = std::format("{} seconds recorded from the {}.", signal.duration(), name(source_));
	ws.add_and_select(std::move(signal)); // in the viewer: Space plays it back
	recorded_i_ = {};
	recorded_q_ = {};
}

void LiveWindow::update(Workspace& ws) {
	if (const std::string error = input_->error(); !error.empty()) {
		error_ = error;
		finish_recording(ws);
		stop();
		return;
	}

	const std::size_t before = i_.size();
	input_->take(i_, q_);
	received_ += static_cast<double>(i_.size() - before);

	if (recording_) {
		const auto from = static_cast<std::ptrdiff_t>(before);
		recorded_i_.insert(recorded_i_.end(), i_.begin() + from, i_.end());
		if (!q_.empty()) {
			recorded_q_.insert(recorded_q_.end(), q_.begin() + from, q_.end());
		}
		if (recorded_i_.size() >= max_recorded) {
			finish_recording(ws);
			error_ = "The recording got too big and was stopped.";
		}
	}

	// Keep only the newest keep_seconds_.
	const auto keep = static_cast<std::size_t>(keep_seconds_ * input_->sample_rate());
	if (i_.size() > keep) {
		const auto drop = static_cast<std::ptrdiff_t>(i_.size() - keep);
		i_.erase(i_.begin(), i_.begin() + drop);
		if (!q_.empty()) {
			q_.erase(q_.begin(), q_.begin() + drop);
		}
	}

	// A new snapshot a few times a second: often enough to look live, rarely enough to stay cheap.
	const auto interval = std::chrono::milliseconds(input_->iq() ? 250 : 100);
	if (Clock::now() - last_update_ < interval || i_.empty()) {
		return;
	}
	last_update_ = Clock::now();

	sig::Signal signal = track_->signal();
	signal.channels = { i_ };
	if (input_->iq()) {
		signal.channels.push_back(q_);
	}
	TrackPtr update = make_track(std::move(signal), false, {}, live_id_);
	ws.update_live(track_, update);
	track_ = std::move(update);

	if (follow_ && ws.current == track_) {
		ws.playback.playing = false; // the newest sound is already heard live; nothing to play back
		ws.playback.position = std::max(0.0, static_cast<double>(track_->signal().duration()) - 0.05);
	}
}

void LiveWindow::draw(Workspace& ws) {
	if (input_) {
		update(ws); // also while the window is hidden
	}
	if (!open) {
		return;
	}
	ImGui::SetNextWindowSize({ 450.0f, 400.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	ImGui::BeginDisabled(input_ != nullptr);
	ImGui::SetNextItemWidth(ui::em(12.0f));
	ui::enum_combo("Source", source_, std::array{ Source::Microphone, Source::Radio });
	if (source_ == Source::Radio) {
		ImGui::SetNextItemWidth(ui::em(10.0f));
		ImGui::InputText("Host", &radio_.host);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(5.0f));
		ImGui::InputInt("Port", &radio_.port, 0);
		int rate = static_cast<int>(radio_.sample_rate);
		ImGui::SetNextItemWidth(ui::em(10.0f));
		if (ImGui::BeginCombo("Sample rate", sig::format_hz(rate).c_str())) {
			for (const int r : { 250'000, 1'024'000, 1'536'000, 2'048'000, 2'400'000 }) {
				if (ImGui::Selectable(sig::format_hz(r).c_str(), r == rate)) {
					radio_.sample_rate = static_cast<unsigned>(r);
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SetNextItemWidth(ui::em(10.0f));
		ImGui::SliderFloat("Gain", &radio_.gain_db, 0.0f, 50.0f, radio_.gain_db > 0.0f ? "%.0f dB" : "automatic");
	}
	ImGui::EndDisabled();

	if (source_ == Source::Radio) {
		double mhz = radio_.frequency_hz / 1e6;
		ImGui::SetNextItemWidth(ui::em(10.0f));
		if (ImGui::InputDouble("Frequency (MHz)", &mhz, 0.1, 1.0, "%.4f", ImGuiInputTextFlags_EnterReturnsTrue)) {
			radio_.frequency_hz = mhz * 1e6;
			if (input_) {
				input_->tune(radio_.frequency_hz);
			}
		}
		ui::help(tips::rtl_tcp);
	}

	ImGui::SetNextItemWidth(ui::em(10.0f));
	ImGui::SliderFloat("Keep", &keep_seconds_, 0.5f, 30.0f, "%.1f s", ImGuiSliderFlags_Logarithmic);
	ui::help(
		"How many of the newest seconds the live signal holds. Longer shows more history in the spectrogram, "
		"but costs more memory and time."
	);
	ImGui::SameLine();
	ImGui::Checkbox("Follow", &follow_);
	ui::help("Keep the playhead at the newest samples, so the spectrum, tuner and decoder show what comes in now.");

	if (!input_) {
		if (ImGui::Button("Start", { ui::em(6.0f), 0.0f })) {
			start(ws);
		}
		ImGui::SameLine();
		if (ImGui::Button("Record", { ui::em(6.0f), 0.0f })) {
			start(ws);
			if (input_) {
				start_recording();
			}
		}
		ui::explain("Starts and records everything until you press Stop recording.");
	}
	else {
		if (ImGui::Button("Stop", { ui::em(6.0f), 0.0f })) {
			finish_recording(ws);
			stop();
		}
		ImGui::SameLine();
		if (!recording_) {
			if (ImGui::Button("Record", { ui::em(6.0f), 0.0f })) {
				start_recording();
			}
			ui::explain("Records everything from now on (not only the newest seconds) until you press Stop recording.");
		}
		else {
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.75f, 0.15f, 0.15f, 1.0f });
			const bool done = ImGui::Button(
				std::format(
					"Stop recording ({:.1f} s)",
					static_cast<double>(recorded_i_.size()) / input_->sample_rate()
				)
					.c_str()
			);
			ImGui::PopStyleColor();
			if (done) {
				finish_recording(ws);
			}
			ui::explain(
				"The recording becomes a signal in the list and is shown in the viewer: press Space to play it back."
			);
		}
		ImGui::SameLine();
		if (ImGui::Button("Keep a copy") && track_) {
			sig::Signal copy = track_->signal();
			copy.name = std::format("Recording {}", time_of_day());
			copy.description = std::format("{} seconds of {}.", copy.duration(), name(source_));
			ws.library.add(std::move(copy));
		}
		ui::explain(
			"Adds what the live signal holds now to the list, as a normal signal you can save or analyse at rest."
		);
		if (input_) {
			const double seconds = std::chrono::duration<double>(Clock::now() - started_).count();
			ImGui::TextDisabled(
				"Receiving %s samples per second.",
				sig::format_hz(seconds > 0.5 ? received_ / seconds : 0.0).c_str()
			);
		}
	}
	ui::error_text(error_);
	if (!input_ && error_.empty()) {
		ui::hint(
			source_ == Source::Microphone
				? "Start shows the microphone live in every window. Whistle and watch the spectrogram, or sing to the tuner (Audio window)."
				: "Needs an RTL-SDR stick and its rtl_tcp program running (on this or another computer)."
		);
	}

	ImGui::End();
}
