#pragma once

// Small dependency-free file browser modal for ImGui (std::filesystem only).
// If you later want native OS dialogs, swap this for nativefiledialog-extended or tinyfiledialogs;
// the viewer only needs "give me a path".
//
//   browser.open(FileBrowser::Mode::Open, {".wav", ".cf32"});   // on a button press
//   if (auto path = browser.draw()) { /* user confirmed *path */ }   // every frame

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <imgui.h>

class FileBrowser {
public:
	enum class Mode { Open, Save };

	void open(
		Mode                     mode,
		std::vector<std::string> extensions,
		std::string              hint = {},
		std::string_view         suggested_name = {}
	) {
		mode_ = mode;
		extensions_ = std::move(extensions);
		hint_ = std::move(hint);
		show_all_ = false;
		title_ = std::format("{}###file_browser", mode == Mode::Open ? "Open file" : "Save file");
		assign(name_buf_, suggested_name);
		navigate(directory_); // refresh the listing (also clears the error)
		open_requested_ = true;
	}

	// Call every frame. Returns the chosen path in the frame the user confirms.
	[[nodiscard]] std::optional<std::filesystem::path> draw() {
		std::optional<std::filesystem::path> chosen;

		if (open_requested_) {
			ImGui::OpenPopup(title_.c_str());
			open_requested_ = false;
		}

		const float unit = ImGui::GetFontSize();
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
		ImGui::SetNextWindowSize({ unit * 40.0f, unit * 28.0f }, ImGuiCond_Appearing);

		bool keep_open = true;
		if (!ImGui::BeginPopupModal(title_.c_str(), &keep_open)) {
			return chosen;
		}

		// --- header: directory ---------------------------------------------------------
		if (ImGui::Button("Up") && directory_.has_parent_path()) {
			navigate(directory_.parent_path());
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::InputText("##directory", dir_buf_.data(), dir_buf_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
			navigate(dir_buf_.data());
		}

		if (!hint_.empty()) {
			ImGui::TextDisabled("%s", hint_.c_str());
		}
		if (!error_.empty()) {
			ImGui::TextColored({ 1.0f, 0.4f, 0.4f, 1.0f }, "%s", error_.c_str());
		}

		// --- listing -----------------------------------------------------------------------
		const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
		std::optional<std::filesystem::path> next_directory; // applied after the loop: navigate() rewrites entries_
		bool                                 confirm = false;

		if (ImGui::BeginListBox("##entries", { -FLT_MIN, -footer })) {
			for (const Entry& entry : entries_) {
				if (!entry.is_dir && !show_all_ && !entry.matches_filter) {
					continue;
				}

				const std::string label = entry.is_dir ? entry.name + "/" : entry.name;
				const bool        selected = !entry.is_dir && entry.name == name_buf_.data();

				if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
					const bool double_click = ImGui::IsMouseDoubleClicked(0);
					if (entry.is_dir) {
						if (double_click) {
							next_directory = directory_ / entry.name;
						}
					}
					else {
						assign(name_buf_, entry.name);
						confirm |= double_click;
					}
				}
			}
			ImGui::EndListBox();
		}

		// --- footer: file name and buttons ---------------------------------------------------
		ImGui::SetNextItemWidth(-unit * 7.0f); // leave room for the label
		if (ImGui::InputText("File name", name_buf_.data(), name_buf_.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
			confirm = true;
		}

		const bool has_name = name_buf_[0] != '\0';
		ImGui::BeginDisabled(!has_name);
		if (ImGui::Button(mode_ == Mode::Open ? "Open" : "Save", { unit * 5.0f, 0.0f })) {
			confirm = true;
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel", { unit * 5.0f, 0.0f })) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		ImGui::Checkbox("All files", &show_all_);

		if (mode_ == Mode::Save && has_name) {
			std::error_code ec;
			if (std::filesystem::exists(directory_ / name_buf_.data(), ec)) {
				ImGui::SameLine();
				ImGui::TextColored({ 1.0f, 0.75f, 0.3f, 1.0f }, "will overwrite");
			}
		}

		// --- apply what happened this frame ----------------------------------------------------
		if (next_directory) {
			navigate(*next_directory);
		}

		if (confirm && has_name) {
			const std::filesystem::path path = directory_ / name_buf_.data();
			std::error_code             ec;
			if (mode_ == Mode::Open && !std::filesystem::is_regular_file(path, ec)) {
				error_ = "That file does not exist.";
			}
			else {
				chosen = path;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::EndPopup();
		return chosen;
	}

private:
	struct Entry {
		std::string name;
		std::string sort_key; // lower case
		bool        is_dir;
		bool        matches_filter;
	};

	template <std::size_t N>
	static void assign(std::array<char, N>& buffer, std::string_view text) {
		const std::size_t n = std::min(text.size(), N - 1);
		std::copy_n(text.begin(), n, buffer.begin());
		buffer[n] = '\0';
	}

	[[nodiscard]] static std::string lower(std::string text) {
		std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	[[nodiscard]] bool matches_filter(const std::string& name) const {
		if (extensions_.empty()) {
			return true;
		}
		return std::ranges::contains(extensions_, lower(std::filesystem::path(name).extension().string()));
	}

	void navigate(const std::filesystem::path& directory) {
		error_.clear();

		std::error_code    ec;
		std::vector<Entry> found;
		for (std::filesystem::directory_iterator
		         it(directory, std::filesystem::directory_options::skip_permission_denied, ec),
		     end;
		     !ec && it != end;
		     it.increment(ec)) {
			std::string name = it->path().filename().string();
			if (name.starts_with('.')) {
				continue; // hidden
			}

			std::error_code type_ec;
			const bool      is_dir = it->is_directory(type_ec);
			found.push_back({ name, lower(name), is_dir, !is_dir && matches_filter(name) });
		}
		if (ec) {
			error_ = std::format("Cannot open '{}': {}", directory.string(), ec.message());
			return; // stay where we were
		}

		std::ranges::sort(found, [](const Entry& a, const Entry& b) {
			return a.is_dir != b.is_dir ? a.is_dir : a.sort_key < b.sort_key; // folders first
		});

		entries_ = std::move(found);
		directory_ = std::filesystem::absolute(directory, ec);
		assign(dir_buf_, directory_.string());
	}

	[[nodiscard]] static std::filesystem::path start_directory() {
		std::error_code ec;
		auto            path = std::filesystem::current_path(ec);
		return ec ? std::filesystem::path{ "." } : path;
	}

	Mode                     mode_ = Mode::Open;
	std::string              title_ = "Open file###file_browser";
	std::string              hint_;
	std::string              error_;
	std::vector<std::string> extensions_;
	bool                     show_all_ = false;
	bool                     open_requested_ = false;

	std::filesystem::path  directory_ = start_directory(); // remembered between openings
	std::vector<Entry>     entries_;
	std::array<char, 1024> dir_buf_{};
	std::array<char, 256>  name_buf_{};
};
