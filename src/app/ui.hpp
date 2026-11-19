#pragma once

// Small ImGui helpers shared by the windows.

#include <cstddef>
#include <span>
#include <string>
#include <type_traits>

#include <imgui.h>

#include "library.hpp"

namespace ui {

	// Width in multiples of the font size, so layouts scale with the font.
	[[nodiscard]] inline float em(float n) { return ImGui::GetFontSize() * n; }

	// Drop-down for an enum that has a name() function, e.g. enum_combo("Window", window, fft::all_windows).
	template <typename E>
	bool enum_combo(const char* label, E& value, std::type_identity_t<std::span<const E>> options) {
		bool changed = false;
		if (ImGui::BeginCombo(label, std::string(name(value)).c_str())) {
			for (const E option : options) {
				if (ImGui::Selectable(std::string(name(option)).c_str(), option == value)) {
					value = option;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	// Drop-down to pick one of the library's signals.
	inline bool track_combo(const char* label, std::span<const TrackPtr> tracks, TrackPtr& chosen) {
		bool changed = false;
		if (ImGui::BeginCombo(label, chosen ? chosen->name().c_str() : "(none)")) {
			for (std::size_t i = 0; i < tracks.size(); ++i) {
				ImGui::PushID(static_cast<int>(i)); // names can repeat
				const bool is_selected = tracks[i] == chosen;
				if (ImGui::Selectable(tracks[i]->name().c_str(), is_selected) && !is_selected) {
					chosen = tracks[i];
					changed = true;
				}
				if (is_selected) ImGui::SetItemDefaultFocus();
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	// A rectangle at the cursor that reacts to the mouse (an invisible button), to draw a plot in.
	struct Area {
		ImVec2 min;
		ImVec2 max;
		bool   activated; // mouse went down on it this frame
		bool   dragging;  // mouse is held down on it
		bool   hovered;
	};

	inline Area interactive_area(const char* id, ImVec2 size) {
		const ImVec2 min = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		return {
			.min = min,
			.max = { min.x + size.x, min.y + size.y },
			.activated = ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left),
			.dragging = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left),
			.hovered = ImGui::IsItemHovered(),
		};
	}

	inline void error_text(const std::string& text) {
		if (!text.empty()) ImGui::TextColored({ 1.0f, 0.45f, 0.45f, 1.0f }, "%s", text.c_str());
	}

	// Shows `text` in a tooltip while the previous item is hovered.
	inline void explain(const char* text) {
		if (ImGui::BeginItemTooltip()) {
			ImGui::PushTextWrapPos(em(28.0f));
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	// A greyed-out "(?)" next to the previous item, that explains it when hovered.
	inline void help(const char* text) {
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		explain(text);
	}

	// Greyed-out text that wraps.
	inline void hint(const char* text) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
		ImGui::TextWrapped("%s", text);
		ImGui::PopStyleColor();
	}

} // namespace ui
