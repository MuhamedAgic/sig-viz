#pragma once

// Saving a part of the screen (a plot) as a PNG.
//
// ImGui only builds the frame; it is drawn at the end of it. So a capture is requested now and taken
// after the frame is drawn (screenshot::process(), called from the main loop), a couple of frames later
// so that the file dialog that asked for it is gone.
//
//   PlotExport export_;                    // one per window
//   export_.button("spectrum.png");        // a "Save PNG..." button
//   ... ImPlot::EndPlot(); export_.remember_last_item();
//   export_.draw();                        // its file dialog

#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <imgui.h>
#include <raylib.h>

#include "file_browser.hpp"

namespace screenshot {

	struct Request {
		ImVec2                min;
		ImVec2                max;
		std::filesystem::path path;
		int                   frames_to_wait = 2;
	};

	inline std::optional<Request>& pending() {
		static std::optional<Request> request;
		return request;
	}

	inline std::string& last_message() { // shown in the status bar
		static std::string message;
		return message;
	}

	inline void request(ImVec2 min, ImVec2 max, std::filesystem::path path) { pending() = Request{ min, max, std::move(path) }; }

	// Call after the frame has been drawn (after rlImGuiEnd, before EndDrawing).
	inline void process() {
		auto& request = pending();
		if (!request || --request->frames_to_wait > 0) return;

		const Vector2 scale = GetWindowScaleDPI(); // screen pixels per ImGui unit
		Image image = LoadImageFromScreen();
		const Rectangle area{ request->min.x * scale.x, request->min.y * scale.y,
			(request->max.x - request->min.x) * scale.x, (request->max.y - request->min.y) * scale.y };
		ImageCrop(&image, area);
		const bool ok = ExportImage(image, request->path.string().c_str());
		UnloadImage(image);

		last_message() = ok ? "Saved " + request->path.string() : "Could not save " + request->path.string();
		request.reset();
	}

} // namespace screenshot

class PlotExport {
public:
	// A small "PNG" button; opens a save dialog.
	void button(std::string suggested_name) {
		if (ImGui::SmallButton("Save PNG...")) browser_.open(FileBrowser::Mode::Save, { ".png" }, "Saves the plot as it looks now.", suggested_name);
	}

	// Call right after the plot (ImPlot::EndPlot) to remember where it is on screen.
	void remember_last_item() {
		min_ = ImGui::GetItemRectMin();
		max_ = ImGui::GetItemRectMax();
	}

	void draw() {
		if (auto path = browser_.draw()) {
			if (path->extension() != ".png") *path += ".png";
			screenshot::request(min_, max_, std::move(*path));
		}
	}

private:
	FileBrowser browser_;
	ImVec2      min_{}, max_{};
};
