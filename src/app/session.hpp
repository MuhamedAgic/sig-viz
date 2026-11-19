#pragma once

// Saving and loading the whole workspace: every signal you loaded or made, which one is shown, the
// playhead, the selection and the cursors. (The window layout is saved by ImGui itself, in imgui.ini.)
//
// A session is a small text file (name.sigviz) plus a folder next to it (name_signals/) with one WAV
// file per signal. Both are easy to look into or share. Recipes are not stored: loaded signals start
// fresh.

#include <expected>
#include <filesystem>
#include <string>

#include "workspace.hpp"

namespace session {

	[[nodiscard]] std::expected<void, std::string> save(const Workspace& ws, const std::filesystem::path& file);

	// Adds the session's signals to the workspace (the examples are always there) and restores the view.
	[[nodiscard]] std::expected<void, std::string> load(Workspace& ws, const std::filesystem::path& file);

} // namespace session
