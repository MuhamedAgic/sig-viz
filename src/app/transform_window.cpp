#include "transform_window.hpp"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <format>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/fft.hpp"
#include "core/units.hpp"

namespace transform = sig::transform;

const char* TransformWindow::explanation(Op op) {
	switch (op) {
	case Op::Gain:           return "Makes the signal louder or quieter. +6 dB doubles the amplitude.";
	case Op::Normalize:      return "Scales the signal so its loudest sample reaches full scale (1.0).";
	case Op::RemoveDc:       return "Subtracts the average, so the signal is centred on 0.";
	case Op::AddNoise:       return "Adds random (Gaussian) noise. SNR = how much stronger the signal is than the noise.";
	case Op::Filter:         return "Keeps some frequencies and removes others. Check the result in the Spectrum window.";
	case Op::ShiftFrequency: return "Moves every frequency up or down by the same amount (the result is I/Q). "
		"Use it to bring a carrier down to 0 Hz before decoding.";
	case Op::Resample:       return "Changes the sample rate. Going down, frequencies above half the new rate are removed first.";
	case Op::ToIq:           return "Makes a real signal I/Q (Hilbert transform): Q is the signal shifted by 90 degrees.";
	case Op::ToReal:         return "Keeps only I.";
	case Op::AmDemodulate:   return "Recovers the message of an AM signal: its envelope (how loud it is over time).";
	case Op::FmDemodulate:   return "Recovers the message of an FM signal: how its frequency changes over time.";
	case Op::Trim:           return "Keeps only the selected part (shift + drag in the viewer to select).";
	case Op::Combine:        return "Adds, subtracts or multiplies with another signal, sample by sample. "
		"Multiplying a message with a carrier is modulation.";
	}
	return "";
}

void TransformWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 450.0f, 600.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	ImGui::TextDisabled("Input (the viewer's signal):");
	ImGui::TextWrapped("%s", ws.current->name().c_str());
	ImGui::Spacing();

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ui::enum_combo("##operation", op_, all_ops)) error_.clear();
	ui::hint(explanation(op_));
	ImGui::Spacing();

	draw_parameters(ws);

	ImGui::Spacing();
	if (ImGui::Button("Apply", { -FLT_MIN, 0.0f })) {
		auto result = apply(ws);
		if (result) {
			const std::string what = label();
			result->description = std::format("{}, applied to {}.", what, ws.current->name());
			result->name = std::format("{} | {}", ws.current->name(), what);
			ws.add_and_select(std::move(*result));
			error_.clear();
		}
		else {
			error_ = result.error();
		}
	}
	ui::error_text(error_);

	if (op_ == Op::Filter) draw_filter_response(ws.signal());

	ImGui::End();
}

void TransformWindow::draw_parameters(const Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	const float nyquist = signal.sample_rate / 2.0f;
	ImGui::PushItemWidth(ui::em(10.0f));

	switch (op_) {
	case Op::Gain:
		ImGui::SliderFloat("Gain (dB)", &gain_db_, -60.0f, 60.0f, "%+.1f dB");
		break;
	case Op::AddNoise:
		ImGui::SliderFloat("SNR (dB)", &snr_db_, -20.0f, 80.0f, "%.1f dB");
		ui::help(tips::noise_snr);
		ImGui::InputInt("Seed", &seed_);
		break;
	case Op::Filter: {
		ui::enum_combo("Type", filter_.type, transform::all_filter_types);
		using enum transform::FilterType;
		if (filter_.type != LowPass)  ImGui::SliderFloat("From (Hz)", &filter_.low_hz, 0.0f, nyquist, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
		if (filter_.type != HighPass) ImGui::SliderFloat("To (Hz)", &filter_.high_hz, 0.0f, nyquist, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderInt("Taps", &filter_.taps, 3, 1001, "%d", ImGuiSliderFlags_Logarithmic);
		ui::help(tips::taps);
		break;
	}
	case Op::ShiftFrequency:
		ImGui::SliderFloat("Shift (Hz)", &shift_hz_, -nyquist, nyquist, "%+.1f Hz");
		break;
	case Op::Resample:
		ImGui::InputFloat("New rate (Hz)", &new_rate_, 0.0f, 0.0f, "%.0f");
		for (const float preset : { 8'000.0f, 16'000.0f, 44'100.0f, 48'000.0f }) {
			if (ImGui::SmallButton(std::format("{:g}k", preset / 1000.0f).c_str())) new_rate_ = preset;
			ImGui::SameLine();
		}
		ImGui::NewLine();
		break;
	case Op::Trim:
		if (ws.selection)
			ImGui::Text("Keeps %s - %s", sig::format_seconds(ws.selection->begin).c_str(), sig::format_seconds(ws.selection->end).c_str());
		else
			ImGui::TextDisabled("Nothing selected yet.");
		break;
	case Op::Combine:
		ui::enum_combo("Operation", combine_, transform::all_combines);
		if (!other_) other_ = ws.current;
		ui::track_combo("B", ws.library.tracks(), other_);
		break;
	case Op::Normalize:
	case Op::RemoveDc:
	case Op::ToIq:
	case Op::ToReal:
	case Op::AmDemodulate:
	case Op::FmDemodulate:
		break; // nothing to set
	}

	ImGui::PopItemWidth();
}

transform::Result TransformWindow::apply(const Workspace& ws) const {
	const sig::Signal& signal = ws.signal();
	switch (op_) {
	case Op::Gain:           return transform::gain(signal, gain_db_);
	case Op::Normalize:      return transform::normalize(signal);
	case Op::RemoveDc:       return transform::remove_dc(signal);
	case Op::AddNoise:       return transform::add_noise(signal, snr_db_, static_cast<unsigned>(seed_));
	case Op::Filter:         return transform::filter(signal, filter_);
	case Op::ShiftFrequency: return transform::shift_frequency(signal, shift_hz_);
	case Op::Resample:       return transform::resample(signal, new_rate_);
	case Op::ToIq:           return transform::to_iq(signal);
	case Op::ToReal:         return transform::to_real(signal);
	case Op::AmDemodulate:   return transform::am_demodulate(signal);
	case Op::FmDemodulate:   return transform::fm_demodulate(signal);
	case Op::Trim:
		if (!ws.selection) return std::unexpected("Select a part first: shift + drag in the viewer.");
		return transform::trim(signal, ws.selection->begin, ws.selection->end);
	case Op::Combine:
		if (!other_) return std::unexpected("Pick signal B.");
		return transform::combine(signal, other_->signal(), combine_);
	}
	std::unreachable();
}

std::string TransformWindow::label() const {
	using enum transform::FilterType;
	switch (op_) {
	case Op::Gain:           return std::format("gain {:+g} dB", gain_db_);
	case Op::Normalize:      return "normalised";
	case Op::RemoveDc:       return "DC removed";
	case Op::AddNoise:       return std::format("noise, SNR {:g} dB", snr_db_);
	case Op::Filter:
		switch (filter_.type) {
		case LowPass:  return "low-pass " + sig::format_hz(filter_.high_hz);
		case HighPass: return "high-pass " + sig::format_hz(filter_.low_hz);
		case BandPass: return std::format("band-pass {} - {}", sig::format_hz(filter_.low_hz), sig::format_hz(filter_.high_hz));
		case BandStop: return std::format("band-stop {} - {}", sig::format_hz(filter_.low_hz), sig::format_hz(filter_.high_hz));
		}
		break;
	case Op::ShiftFrequency: return std::format("shifted {}{}", shift_hz_ >= 0.0f ? "+" : "", sig::format_hz(shift_hz_));
	case Op::Resample:       return "resampled to " + sig::format_hz(new_rate_);
	case Op::ToIq:           return "I/Q";
	case Op::ToReal:         return "real";
	case Op::AmDemodulate:   return "AM demodulated";
	case Op::FmDemodulate:   return "FM demodulated";
	case Op::Trim:           return "trimmed";
	case Op::Combine:        return std::format("{} {}", name(combine_).substr(2, 1), other_ ? other_->name() : ""); // e.g. "+ Sine 440 Hz"
	}
	return "";
}

// How much the filter lets through at each frequency: the FFT of its coefficients.
void TransformWindow::draw_filter_response(const sig::Signal& signal) const {
	ImGui::SeparatorText("Filter response");

	const std::vector<float> taps = transform::design_filter(filter_, signal.sample_rate);
	std::vector<sig::fft::Complex> spectrum(std::max<std::size_t>(4096, std::bit_ceil(taps.size())));
	std::ranges::copy(taps, spectrum.begin());
	sig::fft::forward(spectrum);

	const std::size_t bins = spectrum.size() / 2 + 1;
	std::vector<float> db(bins);
	for (std::size_t k = 0; k < bins; ++k) db[k] = static_cast<float>(sig::to_db(std::norm(spectrum[k])));

	if (ImPlot::BeginPlot("##response", { -1.0f, -1.0f }, ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxes("Frequency (Hz)", "Gain (dB)");
		ImPlot::SetupAxisFormat(ImAxis_X1, plots::hz_formatter);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, signal.sample_rate / 2.0, ImPlotCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -100.0, 5.0, ImPlotCond_Once);
		ImPlot::PlotLine("##gain", db.data(), static_cast<int>(bins), signal.sample_rate / static_cast<double>(spectrum.size()));
		ImPlot::EndPlot();
	}
	if (signal.is_iq()) ui::hint("For I/Q signals the same response applies to negative frequencies.");
}
