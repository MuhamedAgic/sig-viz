#include "compare_window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <vector>

#include <imgui.h>

#include "tips.hpp"
#include "core/transforms.hpp"
#include "core/units.hpp"

void CompareWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 900.0f, 700.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	// Start with something to compare: the viewer's signal against the next one in the library.
	const auto tracks = ws.library.tracks();
	if (!a_) a_ = ws.current;
	if (!b_) b_ = tracks.size() > 1 && tracks[0] == a_ ? tracks[1] : tracks[0];

	draw_controls(ws);
	if (same_rate()) update_difference();
	draw_panels(ws);

	ImGui::End();
}

std::ptrdiff_t CompareWindow::lag() const {
	return static_cast<std::ptrdiff_t>(std::llround(offset_ * a_->signal().sample_rate));
}

void CompareWindow::draw_controls(Workspace& ws) {
	const auto tracks = ws.library.tracks();

	ImGui::SetNextItemWidth(ui::em(14.0f));
	ui::track_combo("A", tracks, a_);
	ImGui::SameLine();
	if (ImGui::SmallButton("Viewer's##a")) a_ = ws.current;
	ImGui::SameLine(0.0f, ui::em(1.5f));
	ImGui::SetNextItemWidth(ui::em(14.0f));
	ui::track_combo("B", tracks, b_);
	ImGui::SameLine();
	if (ImGui::SmallButton("Viewer's##b")) b_ = ws.current;

	ImGui::SetNextItemWidth(ui::em(11.0f));
	ui::enum_combo("Layout", layout_, std::array{ Layout::Stacked, Layout::Overlay });
	ImGui::SameLine();
	ImGui::Checkbox("A - B", &show_difference_);
	ui::help(tips::difference);
	ImGui::SameLine();
	ImGui::Checkbox("Follow playhead", &follow_);

	// Shift of B.
	ImGui::SetNextItemWidth(ui::em(10.0f));
	const double step = 1.0 / a_->signal().sample_rate; // one sample
	ImGui::DragScalar("Shift B (s)", ImGuiDataType_Double, &offset_, static_cast<float>(window_seconds_ / 200.0), nullptr, nullptr, "%.6f");
	ImGui::SameLine();
	if (ImGui::ArrowButton("##earlier", ImGuiDir_Left)) offset_ -= step;
	ImGui::SameLine();
	if (ImGui::ArrowButton("##later", ImGuiDir_Right)) offset_ += step;
	ImGui::SameLine();
	if (ImGui::Button("Auto-align")) {
		if (!same_rate()) {
			message_ = "Auto-align needs equal sample rates; resample one of them first (Transform window).";
		}
		else {
			const std::ptrdiff_t found = sig::analysis::best_lag(a_->signal(), b_->signal());
			offset_ = static_cast<double>(found) / a_->signal().sample_rate;
			message_ = std::format("Best match with B shifted by {} samples ({}).", found, sig::format_seconds(offset_));
		}
	}
	ui::help(tips::auto_align);
	ImGui::SameLine();
	if (ImGui::Button("Reset##shift")) offset_ = 0.0;

	// View.
	ImGui::SetNextItemWidth(ui::em(10.0f));
	ImGui::SliderFloat("Window", &window_seconds_, 0.00001f, 10.0f, "%.5f s", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::SliderFloat("Gain", &gain_, 0.1f, 100.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
	ImGui::SameLine();
	if (ImGui::Button("Fit")) {
		const float peak = std::max(a_->peak(), b_->peak());
		if (peak > 0.0f) gain_ = std::clamp(1.0f / peak, 0.1f, 100.0f);
	}

	// Results.
	if (!same_rate()) {
		ui::error_text(std::format("A is {} and B is {}: the difference needs equal sample rates.",
			sig::format_hz(a_->signal().sample_rate), sig::format_hz(b_->signal().sample_rate)));
	}
	else if (similarity_.overlap > 0) {
		ImGui::Text("Correlation %.4f    RMS of A - B: %.1f dBFS    over %zu samples", similarity_.correlation,
			20.0 * std::log10(std::max(similarity_.rms_difference, 1e-12)), similarity_.overlap);
		ui::help(tips::correlation);
		ImGui::SameLine();
		if (difference_ && ImGui::SmallButton("Add A - B to library")) ws.add_and_select(difference_->signal());
	}
	else {
		ImGui::TextDisabled("A and B do not overlap with this shift.");
	}
	if (!message_.empty()) ImGui::TextDisabled("%s", message_.c_str());
}

void CompareWindow::update_difference() {
	const std::ptrdiff_t current_lag = lag();
	if (difference_ && a_ == difference_a_ && b_ == difference_b_ && current_lag == difference_lag_) return;

	difference_a_ = a_;
	difference_b_ = b_;
	difference_lag_ = current_lag;
	similarity_ = sig::analysis::similarity(a_->signal(), b_->signal(), current_lag);

	auto result = sig::transform::combine(a_->signal(), b_->signal(), sig::transform::Combine::Subtract, current_lag);
	if (!result) {
		difference_.reset();
		return;
	}
	result->name = std::format("{} - {}", a_->name(), b_->name());
	result->description = std::format("Difference: {} minus {} shifted by {}.", a_->name(), b_->name(), sig::format_seconds(offset_));
	difference_ = make_track(std::move(*result));
}

// Drag to scroll, wheel to zoom.
void CompareWindow::handle_mouse(const ui::Area& area, const waveform::Canvas& canvas, double& position, double duration) {
	const ImGuiIO& io = ImGui::GetIO();
	if (area.dragging) {
		const double delta = io.MouseDelta.x / canvas.width() * (canvas.t_end - canvas.t_begin);
		position = std::clamp(position - delta, 0.0, duration);
	}
	if (area.hovered && io.MouseWheel != 0.0f)
		window_seconds_ = std::clamp(window_seconds_ * std::pow(0.85f, io.MouseWheel), 0.00001f, 10.0f);
}

void CompareWindow::draw_panels(Workspace& ws) {
	enum class Shows { A, B, Both, Difference };
	struct Panel {
		const char* id;
		Shows       shows;
	};
	std::vector<Panel> panels;
	if (layout_ == Layout::Stacked) panels = { { "##a", Shows::A }, { "##b", Shows::B } };
	else                            panels = { { "##both", Shows::Both } };
	if (show_difference_ && difference_) panels.push_back({ "##difference", Shows::Difference });

	double& position = follow_ ? ws.playback.position : position_;
	const double duration = std::max(a_->signal().duration(), b_->signal().duration());
	const float spacing = ImGui::GetStyle().ItemSpacing.y;
	const float height = std::max((ImGui::GetContentRegionAvail().y - spacing * static_cast<float>(panels.size() - 1)) / static_cast<float>(panels.size()), ui::em(4.0f));

	ImDrawList& dl = *ImGui::GetWindowDrawList();
	const std::string label_a = "A: " + a_->name();
	const std::string label_b = "B: " + b_->name();

	for (const Panel& panel : panels) {
		const ui::Area area = ui::interactive_area(panel.id, { ImGui::GetContentRegionAvail().x, height });
		const double half = window_seconds_ / 2.0;
		const waveform::Canvas canvas{ area.min, area.max, position - half, position + half, gain_ };
		const waveform::Canvas canvas_b = canvas.shifted(offset_);

		switch (panel.shows) {
		case Shows::A:
			waveform::draw_track(dl, canvas, *a_, true);
			waveform::draw_label(dl, canvas, 1, label_a.c_str(), waveform::colors_a[0]);
			break;
		case Shows::B:
			waveform::draw_track(dl, canvas_b, *b_, true);
			waveform::draw_label(dl, canvas, 1, label_b.c_str(), waveform::colors_a[0]);
			break;
		case Shows::Both:
			waveform::draw_background(dl, canvas, a_->signal().duration(), true);
			waveform::draw_traces(dl, canvas, *a_, waveform::colors_a);
			waveform::draw_traces(dl, canvas_b, *b_, waveform::colors_b);
			waveform::draw_label(dl, canvas, 0, label_a.c_str(), waveform::colors_a[0]);
			waveform::draw_label(dl, canvas, 1, label_b.c_str(), waveform::colors_b[0]);
			waveform::draw_border(dl, canvas);
			break;
		case Shows::Difference:
			waveform::draw_track(dl, canvas, *difference_, true);
			waveform::draw_label(dl, canvas, 1, "A - B", waveform::colors_a[0]);
			break;
		}
		if (follow_) waveform::draw_playhead(dl, canvas, position);

		handle_mouse(area, canvas, position, duration);
	}
}
