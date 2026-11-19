#pragma once

// Helpers for ImPlot plots. Call them between ImPlot::BeginPlot() and ImPlot::EndPlot().

#include <cmath>
#include <cstdio>

#include <imgui.h>
#include <implot.h>

#include "workspace.hpp"
#include "waveform.hpp"

namespace plots {

	// Axis labels like "500", "2k", "1.5M" instead of "1500000". Use with ImPlot::SetupAxisFormat.
	inline int hz_formatter(double value, char* buffer, int size, void*) {
		const double magnitude = std::abs(value);
		if (magnitude >= 1e6) return std::snprintf(buffer, static_cast<std::size_t>(size), "%gM", value / 1e6);
		if (magnitude >= 1e3) return std::snprintf(buffer, static_cast<std::size_t>(size), "%gk", value / 1e3);
		return std::snprintf(buffer, static_cast<std::size_t>(size), "%g", value);
	}

	// A vertical line across the whole plot at x.
	inline void vertical_line(double x, ImU32 color, float thickness = 1.5f) {
		const float px = ImPlot::PlotToPixels(x, 0.0).x;
		const ImVec2 pos = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize();
		ImPlot::PushPlotClipRect();
		ImPlot::GetPlotDrawList()->AddLine({ px, pos.y }, { px, pos.y + size.y }, color, thickness);
		ImPlot::PopPlotClipRect();
	}

	// A horizontal line across the whole plot at y.
	inline void horizontal_line(double y, ImU32 color, float thickness = 1.0f) {
		const float py = ImPlot::PlotToPixels(0.0, y).y;
		const ImVec2 pos = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize();
		ImPlot::PushPlotClipRect();
		ImPlot::GetPlotDrawList()->AddLine({ pos.x, py }, { pos.x + size.x, py }, color, thickness);
		ImPlot::PopPlotClipRect();
	}

	// The outline of a box, in plot coordinates.
	inline void rectangle(double x0, double y0, double x1, double y1, ImU32 color) {
		ImPlot::PushPlotClipRect();
		ImPlot::GetPlotDrawList()->AddRect(ImPlot::PlotToPixels(x0, y1), ImPlot::PlotToPixels(x1, y0), color, 0.0f, 0, 1.5f);
		ImPlot::PopPlotClipRect();
	}

	inline constexpr ImU32 hover_color = IM_COL32(255, 255, 255, 70);

	// Where the mouse points in another window: a faint line at that time (x axis = time) and/or
	// frequency (horizontal: frequency is the y axis, else the x axis).
	inline void hover_marks(const Workspace& ws, bool time_on_x, bool frequency_on_y) {
		if (time_on_x && ws.hover.seconds) vertical_line(*ws.hover.seconds, hover_color, 1.0f);
		if (ws.hover.hz) {
			if (frequency_on_y) horizontal_line(*ws.hover.hz, hover_color);
			else if (!time_on_x) vertical_line(*ws.hover.hz, hover_color, 1.0f);
		}
	}

	// The workspace's selection and cursors, for plots whose x axis is time in seconds.
	inline void time_marks(const Workspace& ws) {
		if (ws.selection) {
			const float x0 = ImPlot::PlotToPixels(ws.selection->begin, 0.0).x;
			const float x1 = ImPlot::PlotToPixels(ws.selection->end, 0.0).x;
			const ImVec2 pos = ImPlot::GetPlotPos(), size = ImPlot::GetPlotSize();
			ImPlot::PushPlotClipRect();
			ImPlot::GetPlotDrawList()->AddRectFilled({ x0, pos.y }, { x1, pos.y + size.y }, IM_COL32(120, 160, 255, 50));
			ImPlot::PopPlotClipRect();
		}
		for (const auto& cursor : ws.cursors)
			if (cursor) vertical_line(*cursor, waveform::cursor_color);
	}

} // namespace plots
