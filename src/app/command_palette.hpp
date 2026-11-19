#pragma once

// Ctrl+P: type a few letters of what you want to do ("filter", "spectrum", "load") and press Enter.
// The quickest way to find any feature without knowing where it lives.

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

struct Command {
	std::string           name; // what is shown and searched, e.g. "Transform: Filter"
	std::function<void()> run;
};

class CommandPalette {
public:
	void open() {
		open_requested_ = true;
		query_.clear();
		chosen_ = 0;
	}

	// Call every frame. `commands` is only looked at while the palette is open.
	void draw(const std::vector<Command>& commands) {
		constexpr const char* id = "Commands###palette";
		if (open_requested_) {
			ImGui::OpenPopup(id);
			open_requested_ = false;
		}
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos({ viewport->GetCenter().x, viewport->WorkPos.y + viewport->WorkSize.y * 0.15f }, ImGuiCond_Always, { 0.5f, 0.0f });
		ImGui::SetNextWindowSize({ ImGui::GetFontSize() * 32.0f, 0.0f });
		if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize)) return;

		if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputTextWithHint("##query", "Type to search, Enter to run, Esc to close", &query_)) chosen_ = 0;

		std::vector<const Command*> found;
		for (const Command& c : commands)
			if (matches(c.name, query_)) found.push_back(&c);

		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) chosen_ = std::min(chosen_ + 1, static_cast<int>(found.size()) - 1);
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) chosen_ = std::max(chosen_ - 1, 0);

		const Command* run = nullptr;
		if (ImGui::IsKeyPressed(ImGuiKey_Enter) && chosen_ < static_cast<int>(found.size())) run = found[static_cast<std::size_t>(chosen_)];

		constexpr int shown = 14;
		for (int i = 0; i < std::min(static_cast<int>(found.size()), shown); ++i) {
			if (ImGui::Selectable(found[static_cast<std::size_t>(i)]->name.c_str(), i == chosen_)) run = found[static_cast<std::size_t>(i)];
		}
		if (found.size() > shown) ImGui::TextDisabled("... %zu more: type more letters", found.size() - shown);
		if (found.empty()) ImGui::TextDisabled("Nothing matches.");

		if (run || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		if (run) run->run(); // after the popup is closed: the command may open another one
	}

private:
	// Every word of the query appears somewhere in the name (ignoring case): "tr fil" finds "Transform: Filter".
	[[nodiscard]] static bool matches(const std::string& name, const std::string& query) {
		const auto lower = [](std::string s) {
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		};
		const std::string haystack = lower(name);
		std::istringstream words(lower(query));
		for (std::string word; words >> word;)
			if (haystack.find(word) == std::string::npos) return false;
		return true;
	}

	bool        open_requested_ = false;
	std::string query_;
	int         chosen_ = 0;
};
