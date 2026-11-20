#pragma once

#include <optional>
#include <string>
#include <vector>

#include "background.hpp"
#include "file_browser.hpp"
#include "workspace.hpp"
#include "core/recipe.hpp"
#include "core/transforms.hpp"

// Applies an operation to the viewer's signal. The result is added to the library as a new signal
// and shown, so operations can be chained, and the result compared with the original.
//
// While you change the settings, the result is computed in the background (a live preview): the
// window shows the spectrum before and after, and you can listen to either (A/B). Apply then just
// keeps the result.
//
// Every result remembers its recipe (all steps from the original), which can be re-run on another
// signal, saved to a file and loaded again.
class TransformWindow {
public:
	static constexpr const char* title = "Transform";
	bool                         open = true;

	void draw(Workspace& ws);

	// Opens the window with this operation picked (command palette).
	void pick(sig::recipe::Op op) {
		open = true;
		step_.op = op;
		error_.clear();
	}

private:
	using Op = sig::recipe::Op;

	struct Preview {
		TrackPtr          input;
		sig::recipe::Step step;
		TrackPtr          result; // null if it failed
		std::string       error;
	};

	[[nodiscard]] static const char* explanation(Op op);

	void draw_lineage(Workspace& ws);
	void draw_parameters(const Workspace& ws);
	void update_preview(Workspace& ws);
	void draw_preview(Workspace& ws);
	void draw_before_after(const Workspace& ws);
	void draw_filter_response(const sig::Signal& signal) const;
	void keep_result(Workspace& ws, const Preview& preview);
	void draw_recipe(Workspace& ws);

	sig::recipe::Step step_;
	TrackPtr          other_; // B, for Combine
	std::string       error_;

	// Live preview
	Background<Preview>    preview_job_;
	std::optional<Preview> preview_;
	sig::recipe::Step      wanted_step_; // what the preview should show (changes while dragging a slider)
	TrackPtr               wanted_input_;
	std::optional<Preview> requested_;        // input and step of the running (or last) preview job
	double                 changed_at_ = 0.0; // ImGui time of the last change: wait until the slider rests
	bool                   apply_when_ready_ = false;
	bool                   hear_result_ = false; // A/B: hear the preview instead of the original

	// Recipes
	Background<sig::transform::Result> recipe_job_;
	TrackPtr                           recipe_target_; // what the running recipe is applied to
	std::vector<sig::recipe::Step>     recipe_steps_;
	std::vector<sig::recipe::Step>     loaded_recipe_;
	std::string                        loaded_name_;
	TrackPtr                           run_on_;
	FileBrowser                        browser_;
	enum class Pending { None, Save, Load } pending_ = Pending::None;
	std::string recipe_status_;
	bool        recipe_failed_ = false;
};
