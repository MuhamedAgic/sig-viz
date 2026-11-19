#include "generator_window.hpp"

#include <array>
#include <cfloat>
#include <format>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "tips.hpp"
#include "ui.hpp"

namespace gen = sig::generator;

void GeneratorWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 450.0f, 600.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	gen::Settings& s = settings_;
	ImGui::PushItemWidth(ui::em(10.0f));

	if (ui::enum_combo("Source", s.source, gen::all_sources) && s.source == gen::Source::Digital)
		s.frequency = 0.0f; // start at baseband, which the Digital window decodes directly
	ImGui::Checkbox("I/Q (complex)", &s.iq);
	ui::help(tips::iq);

	ImGui::SeparatorText("Timing");
	ImGui::InputFloat("Sample rate (Hz)", &s.sample_rate, 0.0f, 0.0f, "%.0f");
	ImGui::InputFloat("Duration (s)", &s.seconds, 0.0f, 0.0f, "%.3f");
	ImGui::SliderFloat("Amplitude", &s.amplitude, 0.0f, 1.0f, "%.2f");

	const float nyquist = s.sample_rate / 2.0f;
	switch (s.source) {
	case gen::Source::Tone:
		ImGui::SeparatorText("Tone");
		ui::enum_combo("Waveform", s.waveform, gen::all_waveforms);
		ImGui::SliderFloat("Frequency (Hz)", &s.frequency, s.iq ? -nyquist : 0.0f, nyquist, "%.1f Hz");
		ui::enum_combo("Modulation", s.modulation, gen::all_modulations);
		ui::help(tips::modulation);
		switch (s.modulation) {
		case gen::Modulation::None:
			break;
		case gen::Modulation::AM:
			ImGui::SliderFloat("Message (Hz)", &s.message_hz, 0.1f, 1000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Depth", &s.am_depth, 0.0f, 1.0f, "%.2f");
			break;
		case gen::Modulation::FM:
			ImGui::SliderFloat("Message (Hz)", &s.message_hz, 0.1f, 1000.0f, "%.1f Hz", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Deviation (Hz)", &s.fm_deviation, 0.0f, nyquist / 2.0f, "+-%.0f Hz");
			break;
		case gen::Modulation::Sweep:
			ImGui::SliderFloat("Sweep to (Hz)", &s.sweep_to, s.iq ? -nyquist : 0.0f, nyquist, "%.1f Hz");
			break;
		}
		break;

	case gen::Source::Noise:
		ImGui::SeparatorText("Noise");
		ui::hint("Gaussian (white) noise: equal power at all frequencies.");
		break;

	case gen::Source::Digital: {
		ImGui::SeparatorText("Digital data");
		ui::enum_combo("Modulation", s.scheme, sig::digital::all_schemes);
		ui::help(tips::scheme);
		ImGui::InputFloat("Symbol rate (baud)", &s.symbol_rate, 0.0f, 0.0f, "%.1f");
		ui::help(tips::symbol_rate);
		ImGui::SliderFloat("Carrier (Hz)", &s.frequency, s.iq ? -nyquist : 0.0f, nyquist, "%.1f Hz");
		ui::help(tips::carrier);
		ImGui::SetNextItemWidth(-FLT_MIN);
		ImGui::InputText("##bits", &s.bits);
		ui::hint("Bits to send, repeated. Carrier 0 Hz = baseband, which the Digital window can decode directly.");
		break;
	}
	}

	ImGui::SeparatorText("Noise on top");
	ImGui::Checkbox("Add noise", &s.add_noise);
	if (s.add_noise) {
		ImGui::SliderFloat("SNR (dB)", &s.snr_db, -20.0f, 80.0f, "%.1f dB");
		ui::help(tips::noise_snr);
		int seed = static_cast<int>(s.seed);
		if (ImGui::InputInt("Seed", &seed)) s.seed = static_cast<unsigned>(seed);
	}
	ImGui::PopItemWidth();

	ImGui::Spacing();
	ImGui::TextDisabled("Makes:");
	ImGui::SameLine();
	ImGui::TextWrapped("%s", gen::describe(s).c_str());
	if (ImGui::Button("Generate", { -FLT_MIN, 0.0f })) {
		auto signal = gen::synthesize(s);
		if (signal) {
			ws.add_and_select(std::move(*signal));
			error_.clear();
		}
		else {
			error_ = signal.error();
		}
	}
	ui::error_text(error_);

	ImGui::End();
}
