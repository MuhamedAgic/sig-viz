#include "transform_window.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <numbers>
#include <cfloat>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include <imgui.h>
#include <implot.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/fft.hpp"
#include "core/units.hpp"

namespace transform = sig::transform;
namespace recipe = sig::recipe;

namespace {

	constexpr double preview_delay = 0.25; // seconds the settings must rest before the preview is recomputed
	constexpr const char* result_label = "the Transform window's result";

	// The recipe of `input` plus one more step.
	Lineage extend(const TrackPtr& input, const recipe::Step& step) {
		Lineage lineage{ .parent = input, .steps = input->lineage().steps };
		lineage.steps.push_back(step);
		return lineage;
	}

} // namespace

const char* TransformWindow::explanation(Op op) {
	switch (op) {
	case Op::Gain:           return "Makes the signal louder or quieter. +6 dB doubles the amplitude.";
	case Op::Normalize:      return "Scales the signal so its loudest sample reaches full scale (1.0).";
	case Op::RemoveDc:       return "Subtracts the average, so the signal is centred on 0.";
	case Op::AddNoise:       return "Adds random (Gaussian) noise. SNR = how much stronger the signal is than the noise.";
	case Op::Filter:         return "Keeps some frequencies and removes others. Compare before and after below.";
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

	draw_lineage(ws);

	ImGui::SetNextItemWidth(-FLT_MIN);
	if (ui::enum_combo("##operation", step_.op, recipe::all_ops)) error_.clear();
	ui::hint(explanation(step_.op));
	ImGui::Spacing();

	draw_parameters(ws);
	ImGui::Spacing();
	update_preview(ws);
	draw_preview(ws);
	draw_recipe(ws);

	ImGui::End();
}

// ---- where the input came from -------------------------------------------------------------------------

void TransformWindow::draw_lineage(Workspace& ws) {
	ImGui::TextDisabled("Input (the viewer's signal):");
	ImGui::TextWrapped("%s", ws.current->name().c_str());

	const Lineage& lineage = ws.current->lineage();
	if (lineage.parent) {
		ImGui::TextDisabled("Made from %s in %zu step(s).", ws.current->origin().name().c_str(), lineage.steps.size());
		ImGui::SameLine();
		if (ImGui::SmallButton("Step back")) ws.select(lineage.parent);
		ui::explain("Shows the signal this one was made from (like undo; this one stays in the list).");
	}
	ImGui::Spacing();
}

// ---- settings ---------------------------------------------------------------------------------------------

void TransformWindow::draw_parameters(const Workspace& ws) {
	const sig::Signal& signal = ws.signal();
	const float nyquist = signal.sample_rate / 2.0f;
	ImGui::PushItemWidth(ui::em(10.0f));

	switch (step_.op) {
	case Op::Gain:
		ImGui::SliderFloat("Gain (dB)", &step_.gain_db, -60.0f, 60.0f, "%+.1f dB");
		break;
	case Op::AddNoise: {
		ImGui::SliderFloat("SNR (dB)", &step_.snr_db, -20.0f, 80.0f, "%.1f dB");
		ui::help(tips::noise_snr);
		int seed = static_cast<int>(step_.seed);
		if (ImGui::InputInt("Seed", &seed)) step_.seed = static_cast<unsigned>(std::max(seed, 0));
		break;
	}
	case Op::Filter: {
		ui::enum_combo("Type", step_.filter.type, transform::all_filter_types);
		using enum transform::FilterType;
		if (step_.filter.type != LowPass)  ImGui::SliderFloat("From (Hz)", &step_.filter.low_hz, 0.0f, nyquist, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
		if (step_.filter.type != HighPass) ImGui::SliderFloat("To (Hz)", &step_.filter.high_hz, 0.0f, nyquist, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
		ImGui::SliderInt("Taps", &step_.filter.taps, 3, 4001, "%d", ImGuiSliderFlags_Logarithmic);
		ui::help(tips::taps);
		break;
	}
	case Op::ShiftFrequency:
		ImGui::SliderFloat("Shift (Hz)", &step_.shift_hz, -nyquist, nyquist, "%+.1f Hz");
		break;
	case Op::Resample:
		ImGui::InputFloat("New rate (Hz)", &step_.new_rate, 0.0f, 0.0f, "%.0f");
		for (const float preset : { 8'000.0f, 16'000.0f, 44'100.0f, 48'000.0f }) {
			if (ImGui::SmallButton(std::format("{:g}k", preset / 1000.0f).c_str())) step_.new_rate = preset;
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
		ui::enum_combo("Operation", step_.combine, transform::all_combines);
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

// ---- live preview -----------------------------------------------------------------------------------------

void TransformWindow::update_preview(Workspace& ws) {
	// The step as it would be applied now: with the current selection and signal B filled in.
	recipe::Step step = step_;
	if (step.op == Op::Trim && ws.selection) {
		step.trim_begin = ws.selection->begin;
		step.trim_end = ws.selection->end;
	}
	if (step.op == Op::Combine && other_) {
		step.other = Track::shared_signal(other_);
		step.other_name = other_->name();
	}

	const double now = ImGui::GetTime();
	if (!(step == wanted_step_) || ws.current != wanted_input_) {
		wanted_step_ = step;
		wanted_input_ = ws.current;
		changed_at_ = now;
	}

	const bool up_to_date = requested_ && requested_->input == wanted_input_ && requested_->step == wanted_step_;
	const bool can_run = !(step.op == Op::Trim && !ws.selection);
	if (!up_to_date && can_run && (now - changed_at_ > preview_delay || apply_when_ready_)) {
		requested_ = Preview{ .input = wanted_input_, .step = wanted_step_ };
		preview_job_.start([input = wanted_input_, step = wanted_step_](std::stop_token) -> std::optional<Preview> {
			Preview preview{ .input = input, .step = step };
			auto result = recipe::apply(input->signal(), step);
			if (!result) {
				preview.error = result.error();
				return preview;
			}
			result->name = std::format("{} | {}", input->name(), recipe::describe(step));
			result->description = std::format("{}, applied to {}.", recipe::describe(step), input->name());
			preview.result = make_track(std::move(*result), false, extend(input, step));
			return preview;
		});
	}

	if (auto done = preview_job_.take()) preview_ = std::move(*done);

	const bool current = preview_ && preview_->input == ws.current && preview_->step == wanted_step_;
	if (apply_when_ready_ && current) {
		apply_when_ready_ = false;
		keep_result(ws, *preview_);
	}

	// A/B listening: swap what the viewer plays.
	const bool ours = ws.audition && ws.audition->label == result_label;
	if (hear_result_ && current && preview_->result) {
		if (!ours || ws.audition->track != preview_->result) ws.audition = Audition{ ws.current, preview_->result, result_label };
	}
	else if (ours && !(hear_result_ && preview_job_.busy())) {
		ws.audition.reset();
	}
}

void TransformWindow::keep_result(Workspace& ws, const Preview& preview) {
	if (!preview.result) {
		error_ = preview.error;
		return;
	}
	error_.clear();
	hear_result_ = false;
	ws.select(ws.library.add(preview.result));
}

void TransformWindow::draw_preview(Workspace& ws) {
	const bool can_run = !(step_.op == Op::Trim && !ws.selection);
	ImGui::BeginDisabled(!can_run || apply_when_ready_);
	if (ImGui::Button("Apply", { -FLT_MIN, 0.0f })) {
		const bool ready = preview_ && preview_->input == ws.current && preview_->step == wanted_step_;
		if (ready) keep_result(ws, *preview_);
		else apply_when_ready_ = true; // as soon as the result is there
	}
	ImGui::EndDisabled();

	const bool current = preview_ && preview_->input == ws.current && preview_->step == wanted_step_;
	if (preview_job_.busy() || apply_when_ready_) ui::busy("computing");
	else if (!error_.empty()) ui::error_text(error_);
	else if (current && !preview_->error.empty()) ui::error_text(preview_->error);
	else if (!can_run) ImGui::TextDisabled("Select a part first: shift + drag in the viewer.");

	// A/B: hear the original or the result, switching back and forth to compare.
	ImGui::TextUnformatted("Hear:");
	ImGui::SameLine();
	if (ImGui::RadioButton("Original (A)", !hear_result_)) hear_result_ = false;
	ImGui::SameLine();
	ImGui::BeginDisabled(!(current && preview_->result));
	if (ImGui::RadioButton("Result (B)", hear_result_)) {
		hear_result_ = true;
		ws.playback.playing = true;
	}
	ImGui::EndDisabled();
	ui::help(tips::ab_listen);

	if (ImGui::BeginTabBar("##preview")) {
		if (ImGui::BeginTabItem("Before / after")) {
			draw_before_after(ws);
			ImGui::EndTabItem();
		}
		if (step_.op == Op::Filter && ImGui::BeginTabItem("Filter response")) {
			draw_filter_response(ws.signal());
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

// Spectrum around the playhead, of the input (grey) and of the preview result (colour).
void TransformWindow::draw_before_after(const Workspace& ws) {
	const auto spectrum_at = [&](const sig::Signal& signal, std::vector<float>& db, double& first_hz, double& bin_hz) {
		constexpr std::size_t size = 4096;
		sig::fft::Analyzer analyzer(size, sig::fft::Window::Blackman);
		const auto centre = static_cast<std::size_t>(std::clamp(ws.playback.position * signal.sample_rate, 0.0, static_cast<double>(signal.frames())));
		const std::size_t begin = centre > size / 2 ? centre - size / 2 : 0;
		const auto spectrum = analyzer.average(signal, begin, std::min(begin + size, signal.frames()));
		db.resize(spectrum.power.size());
		std::ranges::transform(spectrum.power, db.begin(), [](float p) { return static_cast<float>(sig::to_db(p)); });
		first_hz = spectrum.first_hz + signal.center_hz;
		bin_hz = spectrum.bin_hz;
	};

	const float height = std::clamp(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 6.0f, ui::em(10.0f), ui::em(16.0f));
	if (!ImPlot::BeginPlot("##before_after", { -1.0f, height })) return;
	ImPlot::SetupAxes("Frequency (Hz)", "dBFS");
	ImPlot::SetupAxisFormat(ImAxis_X1, plots::hz_formatter);
	ImPlot::SetupAxisLimits(ImAxis_Y1, -140.0, 10.0, ImPlotCond_Once);
	ImPlot::SetupLegend(ImPlotLocation_NorthEast);

	std::vector<float> db;
	double first_hz = 0.0, bin_hz = 1.0;
	spectrum_at(ws.signal(), db, first_hz, bin_hz);
	ImPlot::SetNextLineStyle({ 0.6f, 0.6f, 0.6f, 0.9f });
	ImPlot::PlotLine("before", db.data(), static_cast<int>(db.size()), bin_hz, first_hz);

	if (preview_ && preview_->input == ws.current && preview_->result) {
		spectrum_at(preview_->result->signal(), db, first_hz, bin_hz);
		ImPlot::PlotLine("after", db.data(), static_cast<int>(db.size()), bin_hz, first_hz);
	}
	ImPlot::EndPlot();
}

// How much the filter lets through at each frequency (the FFT of its taps), and how much it delays.
void TransformWindow::draw_filter_response(const sig::Signal& signal) const {
	const std::vector<float> taps = transform::design_filter(step_.filter, signal.sample_rate);
	std::vector<sig::fft::Complex> spectrum(std::max<std::size_t>(4096, std::bit_ceil(taps.size())));
	std::ranges::copy(taps, spectrum.begin());
	sig::fft::forward(spectrum);

	const std::size_t bins = spectrum.size() / 2 + 1;
	const double bin_hz = signal.sample_rate / static_cast<double>(spectrum.size());
	std::vector<float> db(bins), phase(bins);
	const double middle = static_cast<double>(taps.size() / 2);
	for (std::size_t k = 0; k < bins; ++k) {
		db[k] = static_cast<float>(sig::to_db(std::norm(spectrum[k])));
		// Phase as applied: the filter runs centred (its delay of `middle` samples is undone), so remove it.
		const auto undo_delay = std::polar(1.0f, static_cast<float>(sig::tau * static_cast<double>(k) * middle / static_cast<double>(spectrum.size())));
		phase[k] = std::arg(spectrum[k] * undo_delay) * 180.0f / std::numbers::pi_v<float>;
	}

	const float height = (ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 5.0f) / 2.0f;
	if (ImPlot::BeginPlot("##response", { -1.0f, std::max(height, ui::em(8.0f)) }, ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxes("Frequency (Hz)", "Gain (dB)");
		ImPlot::SetupAxisFormat(ImAxis_X1, plots::hz_formatter);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, signal.sample_rate / 2.0, ImPlotCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -100.0, 5.0, ImPlotCond_Once);
		ImPlot::PlotLine("##gain", db.data(), static_cast<int>(bins), bin_hz);
		ImPlot::EndPlot();
	}
	if (ImPlot::BeginPlot("##phase", { -1.0f, std::max(height, ui::em(8.0f)) }, ImPlotFlags_NoLegend)) {
		ImPlot::SetupAxes("Frequency (Hz)", "Phase (degrees)");
		ImPlot::SetupAxisFormat(ImAxis_X1, plots::hz_formatter);
		ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, signal.sample_rate / 2.0, ImPlotCond_Always);
		ImPlot::SetupAxisLimits(ImAxis_Y1, -200.0, 200.0, ImPlotCond_Once);
		ImPlot::PlotLine("##phase", phase.data(), static_cast<int>(bins), bin_hz);
		ImPlot::EndPlot();
	}
	ui::hint(std::format("Delay: none. A filter like this (symmetric taps) delays every frequency by the same {} samples ({}); "
		"the app takes that delay out, so the phase is 0 (or 180 where the gain dips through zero) and nothing shifts in time.",
		taps.size() / 2, sig::format_seconds(middle / signal.sample_rate)).c_str());
	if (signal.is_iq()) ui::hint("For I/Q signals the same response applies to negative frequencies.");
}

// ---- recipes ---------------------------------------------------------------------------------------------

void TransformWindow::draw_recipe(Workspace& ws) {
	if (auto result = recipe_job_.take()) {
		if (*result) {
			Lineage lineage{ .parent = recipe_target_, .steps = recipe_target_->lineage().steps };
			lineage.steps.insert(lineage.steps.end(), recipe_steps_.begin(), recipe_steps_.end());
			ws.add_and_select(std::move(**result), std::move(lineage));
			recipe_status_.clear();
		}
		else {
			recipe_status_ = result->error();
			recipe_failed_ = true;
		}
	}

	if (auto path = browser_.draw()) {
		if (pending_ == Pending::Save) {
			std::ofstream out(*path);
			out << recipe::to_text(ws.current->lineage().steps);
			recipe_failed_ = !out;
			recipe_status_ = out ? "Saved " + path->string() : "Could not write " + path->string();
		}
		else if (pending_ == Pending::Load) {
			std::ifstream in(*path);
			const std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
			auto steps = recipe::from_text(text);
			if (steps) {
				// Combine steps name their signal B; find it in the library.
				for (recipe::Step& step : *steps)
					if (step.op == Op::Combine)
						if (const TrackPtr b = ws.library.find(step.other_name)) step.other = Track::shared_signal(b);
				loaded_recipe_ = std::move(*steps);
				loaded_name_ = path->filename().string();
				recipe_status_.clear();
			}
			else {
				recipe_status_ = steps.error();
				recipe_failed_ = true;
			}
		}
		pending_ = Pending::None;
	}

	if (!ImGui::CollapsingHeader("Recipe")) return;
	ui::hint("Every result remembers the steps that made it. Run them again on another signal, or save them for later.");

	const auto list = [](const std::vector<recipe::Step>& steps) {
		for (std::size_t i = 0; i < steps.size(); ++i) ImGui::BulletText("%s", recipe::describe(steps[i]).c_str());
	};
	const auto run = [&](const TrackPtr& target, const std::vector<recipe::Step>& steps) {
		recipe_target_ = target;
		recipe_steps_ = steps;
		recipe_job_.start([target, steps](std::stop_token) -> std::optional<transform::Result> { return recipe::run(target->signal(), steps); });
	};

	const std::vector<recipe::Step>& steps = ws.current->lineage().steps;
	if (steps.empty()) {
		ImGui::TextDisabled("This signal was not made here, so it has no recipe yet.");
	}
	else {
		ImGui::Text("Steps from %s:", ws.current->origin().name().c_str());
		list(steps);
		if (!run_on_) run_on_ = ws.library.tracks().front();
		ImGui::SetNextItemWidth(ui::em(14.0f));
		ui::track_combo("##run_on", ws.library.tracks(), run_on_);
		ImGui::SameLine();
		ImGui::BeginDisabled(recipe_job_.busy());
		if (ImGui::Button("Run on it")) run(run_on_, steps);
		ImGui::EndDisabled();
		if (ImGui::Button("Save recipe...")) {
			pending_ = Pending::Save;
			browser_.open(FileBrowser::Mode::Save, { ".recipe" }, "A text file, one step per line.", "steps.recipe");
		}
		ImGui::SameLine();
	}
	if (ImGui::Button("Load recipe...")) {
		pending_ = Pending::Load;
		browser_.open(FileBrowser::Mode::Open, { ".recipe" });
	}

	if (!loaded_recipe_.empty()) {
		ImGui::SeparatorText(loaded_name_.c_str());
		list(loaded_recipe_);
		ImGui::BeginDisabled(recipe_job_.busy());
		if (ImGui::Button("Run on the viewer's signal")) run(ws.current, loaded_recipe_);
		ImGui::EndDisabled();
	}
	if (recipe_job_.busy()) ui::busy("running");
	if (recipe_failed_) ui::error_text(recipe_status_);
	else if (!recipe_status_.empty()) ui::hint(recipe_status_.c_str());
}
