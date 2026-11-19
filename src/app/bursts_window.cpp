#include "bursts_window.hpp"

#include <imgui.h>

#include "tips.hpp"
#include "ui.hpp"
#include "core/units.hpp"

void BurstsWindow::draw(Workspace& ws) {
	if (!open) return;
	ImGui::SetNextWindowSize({ 450.0f, 400.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	// Search again whenever the signal or the threshold changes.
	if (searched_ != ws.current || searched_db_ != threshold_db_) {
		searched_ = ws.current;
		searched_db_ = threshold_db_;
		bursts_.clear();
		job_.start([track = ws.current, threshold = threshold_db_](std::stop_token) {
			return std::optional{ sig::analysis::find_bursts(track->signal(), threshold) };
		});
	}
	if (auto found = job_.take()) bursts_ = std::move(*found);

	ImGui::SetNextItemWidth(ui::em(10.0f));
	ImGui::SliderFloat("Above the floor", &threshold_db_, 3.0f, 40.0f, "%.0f dB");
	ui::help(tips::bursts);
	if (job_.busy()) {
		ui::busy("searching");
		ImGui::End();
		return;
	}

	ImGui::Text("%zu burst(s)", bursts_.size());
	if (bursts_.size() > 1) {
		ImGui::SameLine();
		if (ImGui::SmallButton("Previous")) {
			for (auto it = bursts_.rbegin(); it != bursts_.rend(); ++it) {
				if (it->begin < ws.playback.position - 1e-6) {
					ws.selection = TimeRange{ it->begin, it->end };
					ws.playback.position = it->begin;
					break;
				}
			}
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Next")) {
			for (const auto& burst : bursts_) {
				if (burst.begin > ws.playback.position + 1e-6) {
					ws.selection = TimeRange{ burst.begin, burst.end };
					ws.playback.position = burst.begin;
					break;
				}
			}
		}
	}

	if (ImGui::BeginTable("##bursts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#");
		ImGui::TableSetupColumn("Starts");
		ImGui::TableSetupColumn("Length");
		ImGui::TableSetupColumn("Peak");
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper; // only the visible rows are drawn, however many bursts there are
		clipper.Begin(static_cast<int>(bursts_.size()));
		while (clipper.Step()) {
			for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
				const auto& burst = bursts_[static_cast<std::size_t>(i)];
				const bool selected = ws.selection && ws.selection->begin == burst.begin && ws.selection->end == burst.end;
				ImGui::TableNextColumn();
				if (ImGui::Selectable(std::to_string(i + 1).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
					ws.selection = TimeRange{ burst.begin, burst.end };
					ws.playback.position = burst.begin;
				}
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(sig::format_seconds(burst.begin).c_str());
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(sig::format_seconds(burst.end - burst.begin).c_str());
				ImGui::TableNextColumn();
				ImGui::Text("%.1f dBFS", burst.peak_db);
			}
		}
		ImGui::EndTable();
	}

	ImGui::End();
}
