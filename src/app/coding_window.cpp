#include "coding_window.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>
#include <functional>
#include <utility>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>

#include "plots.hpp"
#include "tips.hpp"
#include "ui.hpp"
#include "core/units.hpp"

namespace coding = sig::coding;

namespace {

	constexpr ImU32 ok_color = IM_COL32(70, 75, 90, 255);
	constexpr ImU32 wrong_color = IM_COL32(255, 80, 70, 255);
	constexpr ImU32 erased_color = IM_COL32(250, 200, 60, 255);
	constexpr ImU32 good_frame_color = IM_COL32(80, 200, 110, 255);
	constexpr ImVec4 good_text{ 0.5f, 0.9f, 0.5f, 1.0f };
	constexpr ImVec4 bad_text{ 1.0f, 0.45f, 0.45f, 1.0f };

	std::string describe(const coding::CodeSpec& code) {
		if (!code.enabled) return "no code";
		std::string text = std::format("polar ({}, {}) {}", code.n(), code.k, coding::name(code.decoder));
		if (code.decoder != coding::Decoder::SC) text += std::format(" L={}", code.list_size);
		if (code.crc != coding::Crc::None) text += std::format(", CRC {}", coding::crc_bits(code.crc));
		return text;
	}

	double percent(std::size_t part, std::size_t whole) { return whole ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0; }

	// One small square per bit, `columns` per row; rows that scroll out of view are not drawn.
	// row_marker: a colour strip left of each row (or 0 for none).
	void bit_grid(const char* id, std::size_t count, std::size_t columns, const std::function<ImU32(std::size_t)>& color,
		const std::function<ImU32(std::size_t)>& row_marker) {
		if (count == 0 || columns == 0) return;
		const std::size_t rows = (count + columns - 1) / columns;
		const float marker = row_marker ? ui::em(0.6f) : 0.0f;
		const float width = ImGui::GetContentRegionAvail().x - marker - ImGui::GetStyle().ScrollbarSize;
		const float cell = std::clamp(width / static_cast<float>(columns), 1.0f, ui::em(0.8f));
		const float row_height = std::max(cell, 3.0f);
		const float height = std::min(ui::em(12.0f), row_height * static_cast<float>(rows) + 4.0f);

		ImGui::BeginChild(id, { 0.0f, height }, false);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0.0f, 0.0f }); // rows exactly row_height apart (the clipper relies on it)
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(rows), row_height);
		while (clipper.Step()) {
			for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
				const ImVec2 origin = ImGui::GetCursorScreenPos();
				const auto row = static_cast<std::size_t>(r);
				if (row_marker) {
					if (const ImU32 c = row_marker(row)) draw->AddRectFilled(origin, { origin.x + marker * 0.7f, origin.y + row_height - 1.0f }, c);
				}
				const float gap = cell >= 4.0f ? 1.0f : 0.0f;
				for (std::size_t c = 0; c < columns; ++c) {
					const std::size_t i = row * columns + c;
					if (i >= count) break;
					const float x = origin.x + marker + static_cast<float>(c) * cell;
					draw->AddRectFilled({ x, origin.y }, { x + cell - gap, origin.y + row_height - gap }, color(i));
				}
				ImGui::Dummy({ marker + cell * static_cast<float>(columns), row_height });
			}
		}
		ImGui::PopStyleVar();
		ImGui::EndChild();
	}

	void legend_square(ImU32 color, const char* label) {
		const ImVec2 p = ImGui::GetCursorScreenPos();
		const float s = ImGui::GetTextLineHeight() * 0.7f;
		ImGui::GetWindowDrawList()->AddRectFilled({ p.x, p.y + s * 0.2f }, { p.x + s, p.y + s * 1.2f }, color);
		ImGui::Dummy({ s, s });
		ImGui::SameLine();
		ImGui::TextDisabled("%s", label);
		ImGui::SameLine(0.0f, ui::em(1.0f));
	}

} // namespace

// ---- running the chain ---------------------------------------------------------------------------------------

CodingWindow::Inputs CodingWindow::inputs(const Workspace& ws) const {
	return Inputs{
		.source = source_,
		.text = source_ == MessageSource::Text ? text_ : std::string{},
		.random_bits = random_bits_,
		.seed = seed_,
		.code = code_,
		.interleaver = interleaver_,
		.damage = damage_,
		.channel = channel_,
		.signal = channel_ == Channel::Signal ? ws.current : nullptr,
		.link = link_,
	};
}

CodingWindow::Outcome CodingWindow::run(Inputs in) {
	Outcome out;
	coding::Bits message = in.source == MessageSource::Text ? coding::text_to_bits(in.text) : coding::random_bits(static_cast<std::size_t>(in.random_bits), in.seed);

	auto tx = coding::transmit(message, in.code, in.interleaver);
	if (!tx) {
		out.error = tx.error();
		out.inputs = std::move(in);
		return out;
	}

	std::vector<float> soft;
	if (in.channel == Channel::Bits) {
		soft = coding::perfect_soft(tx->sent);
	}
	else {
		auto received = sig::link::demodulate(in.signal->signal(), in.link, tx->sent.size(), tx->sent);
		if (!received) {
			out.error = received.error();
			out.inputs = std::move(in);
			return out;
		}
		soft = std::move(received->soft);
		out.inverted = received->inverted;
	}

	auto rx = coding::receive(*tx, coding::apply(std::move(soft), in.damage));
	if (!rx) out.error = rx.error();
	else out.rx = std::move(*rx);

	// The same damage on the bare message, to show what the code bought.
	if (in.code.enabled && in.channel == Channel::Bits) {
		if (auto bare = coding::transmit(message, { .enabled = false }, { .kind = coding::Interleaver::None })) {
			if (auto plain = coding::receive(*bare, coding::apply(coding::perfect_soft(bare->sent), in.damage))) out.uncoded = std::move(*plain);
		}
	}
	out.tx = std::move(*tx);
	out.inputs = std::move(in);
	return out;
}

// ---- window -----------------------------------------------------------------------------------------------------

void CodingWindow::draw(Workspace& ws) {
	if (auto result = job_.take()) outcome_ = std::move(*result);
	if (auto points = curve_job_.take()) {
		curves_.push_back({ curve_label_, std::move(*points) });
		if (curves_.size() > 6) curves_.erase(curves_.begin());
	}
	if (!open) return;

	ImGui::SetNextWindowSize({ 600.0f, 700.0f }, ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &open)) {
		ImGui::End();
		return;
	}

	// Recompute when anything changed (in the background: big messages and list decoders take a moment).
	if (const Inputs wanted = inputs(ws); !requested_ || !(*requested_ == wanted) || (!outcome_ && !job_.busy())) {
		requested_ = wanted;
		job_.start([wanted](std::stop_token) -> std::optional<Outcome> { return run(wanted); });
	}

	ui::hint("Sends a message through an error-correcting code: encode, interleave, damage, deinterleave, decode. "
		"See which bits arrived wrong and whether the code repaired them.");

	ImGui::SeparatorText("Message");
	draw_message();
	ImGui::SeparatorText("Error correction");
	draw_code();
	ImGui::SeparatorText("Channel");
	draw_channel(ws);
	ImGui::SeparatorText("Result");

	if (outcome_) {
		draw_summary(*outcome_);
		if (ImGui::BeginTabBar("##views")) {
			if (ImGui::BeginTabItem("Error map")) {
				draw_error_map(*outcome_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Soft bits")) {
				draw_soft_bits(*outcome_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Message")) {
				draw_text(*outcome_);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Error-rate curve")) {
				draw_curve();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	else if (job_.busy()) {
		ui::busy("Decoding");
	}

	ImGui::End();
}

void CodingWindow::draw_message() {
	int source = static_cast<int>(source_);
	ImGui::RadioButton("Text", &source, static_cast<int>(MessageSource::Text));
	ImGui::SameLine();
	ImGui::RadioButton("Random bits", &source, static_cast<int>(MessageSource::Random));
	source_ = static_cast<MessageSource>(source);

	if (source_ == MessageSource::Text) {
		ImGui::InputTextMultiline("##text", &text_, { -FLT_MIN, ui::em(3.5f) });
		ImGui::TextDisabled("%zu characters = %zu bits", text_.size(), text_.size() * 8);
	}
	else {
		ImGui::SetNextItemWidth(ui::em(10.0f));
		ImGui::SliderInt("Bits", &random_bits_, 8, 200'000, "%d", ImGuiSliderFlags_Logarithmic);
		random_bits_ = std::max(random_bits_, 1);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(6.0f));
		int seed = static_cast<int>(seed_);
		if (ImGui::InputInt("Seed", &seed)) seed_ = static_cast<unsigned>(std::max(seed, 0));
	}
}

void CodingWindow::draw_code() {
	ImGui::Checkbox("Polar code", &code_.enabled);
	ui::help(tips::polar_code);
	ImGui::BeginDisabled(!code_.enabled);
	ImGui::SameLine(0.0f, ui::em(1.5f));
	ImGui::SetNextItemWidth(ui::em(5.0f));
	if (ImGui::BeginCombo("N", std::to_string(code_.n()).c_str())) {
		for (int l = 3; l <= 12; ++l) {
			if (ImGui::Selectable(std::to_string(1 << l).c_str(), l == code_.n_log2)) {
				code_.k = static_cast<int>(std::lround(static_cast<double>(code_.k) * (1 << l) / code_.n())); // keep the rate
				code_.n_log2 = l;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::SliderInt("K", &code_.k, coding::crc_bits(code_.crc) + 1, code_.n());
	code_.k = std::clamp(code_.k, coding::crc_bits(code_.crc) + 1, code_.n());
	ui::help(tips::polar_nk);

	ImGui::SetNextItemWidth(ui::em(10.0f));
	ui::enum_combo("Decoder", code_.decoder, coding::all_decoders);
	ui::help(tips::polar_decoder);
	if (code_.decoder != coding::Decoder::SC) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(5.0f));
		ImGui::SliderInt("L", &code_.list_size, 1, 32);
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ui::em(5.0f));
	ui::enum_combo("CRC", code_.crc, coding::all_crcs);
	ui::help(tips::crc);
	if (code_.decoder == coding::Decoder::CA_SCL && code_.crc == coding::Crc::None) code_.crc = coding::Crc::Crc8;

	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::SliderFloat("Designed for Eb/N0", &code_.design_db, -2.0f, 8.0f, "%.1f dB");
	ui::help(tips::polar_design);

	const std::size_t message_bits = source_ == MessageSource::Text ? text_.size() * 8 : static_cast<std::size_t>(random_bits_);
	if (code_.enabled) {
		const auto frames = coding::codewords_for(code_, message_bits);
		if (frames) {
			ui::hint(std::format("Rate {:.2f}: each codeword carries {} message bits + {} CRC bits + {} redundant bits. "
				"{} message bits -> {} codeword(s) = {} bits on the air.", code_.rate(), code_.message_bits(), coding::crc_bits(code_.crc),
				code_.n() - code_.k, message_bits, *frames, *frames * static_cast<std::size_t>(code_.n())).c_str());
		}
		else {
			ui::error_text(frames.error());
		}
	}
	ImGui::EndDisabled();

	ImGui::SetNextItemWidth(ui::em(12.0f));
	ui::enum_combo("Interleaver", interleaver_.kind, coding::all_interleavers);
	ui::help(tips::interleaver);
	if (interleaver_.kind == coding::Interleaver::Block) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(6.0f));
		ImGui::InputInt("Rows", &interleaver_.rows);
		interleaver_.rows = std::max(interleaver_.rows, 0);
		ui::help("0 = one row per codeword: a burst on the air then hits each codeword only once per row it crosses.");
	}
	else if (interleaver_.kind == coding::Interleaver::Random) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(6.0f));
		int seed = static_cast<int>(interleaver_.seed);
		if (ImGui::InputInt("Seed##interleaver", &seed)) interleaver_.seed = static_cast<unsigned>(std::max(seed, 0));
	}
}

void CodingWindow::draw_channel(Workspace& ws) {
	int channel = static_cast<int>(channel_);
	ImGui::RadioButton("Damage the bits directly", &channel, static_cast<int>(Channel::Bits));
	ImGui::SameLine();
	ImGui::RadioButton("Receive the viewer's signal", &channel, static_cast<int>(Channel::Signal));
	channel_ = static_cast<Channel>(channel);
	ui::help(tips::coding_channel);

	if (ImGui::TreeNode("Radio settings")) {
		ImGui::SetNextItemWidth(ui::em(8.0f));
		ui::enum_combo("Scheme", link_.scheme, sig::digital::all_schemes);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(6.0f));
		ImGui::InputFloat("Baud", &link_.symbol_rate, 0.0f, 0.0f, "%g");
		link_.symbol_rate = std::max(link_.symbol_rate, 1.0f);
		ImGui::SetNextItemWidth(ui::em(6.0f));
		ImGui::InputFloat("Carrier (Hz)", &link_.carrier_hz, 0.0f, 0.0f, "%g");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(7.0f));
		ImGui::InputFloat("Sample rate", &link_.sample_rate, 0.0f, 0.0f, "%g");
		link_.sample_rate = std::clamp(link_.sample_rate, 100.0f, 10e6f);
		ImGui::Checkbox("Follow carrier and timing", &link_.follow);
		ui::help(tips::follow);
		ImGui::TreePop();
	}

	const bool can_make = outcome_ && outcome_->tx;
	ImGui::BeginDisabled(!can_make);
	if (ImGui::Button("Make a signal from the sent bits") && can_make) {
		auto signal = sig::link::modulate(outcome_->tx->sent, link_);
		if (signal) {
			signal->name = std::format("Coded message, {}", sig::link::describe(link_));
			signal->description = std::format("{} bits ({}) sent as {}.", outcome_->tx->sent.size(), describe(outcome_->tx->code), sig::link::describe(link_));
			ws.add_and_select(std::move(*signal));
			channel_ = Channel::Signal;
			make_error_.clear();
		}
		else {
			make_error_ = signal.error();
		}
	}
	ImGui::EndDisabled();
	ui::explain("Puts the coded bits on a carrier as a real signal, shows it in the viewer (Space plays it) and decodes it from there. "
		"Damage it with other windows (Transform: Add noise, Filter, ...) and watch the result here.");
	ui::error_text(make_error_);
	if (channel_ == Channel::Signal)
		ui::hint(std::format("Decoding \"{}\" as {}. After changing the message, code or interleaver, make a new signal: "
			"the old one carries the old bits.", ws.current->name(), sig::link::describe(link_)).c_str());

	draw_damage();
}

void CodingWindow::draw_damage() {
	ImGui::TextDisabled("Damage, applied in order to the soft bits on the air:");
	ui::help(tips::damage);
	int remove = -1;
	for (std::size_t i = 0; i < damage_.size(); ++i) {
		coding::Damage& d = damage_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::SetNextItemWidth(ui::em(7.5f));
		ui::enum_combo("##effect", d.effect, coding::all_effects);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ui::em(8.0f));
		ui::enum_combo("##pattern", d.pattern, coding::all_patterns);
		ImGui::PushItemWidth(ui::em(5.0f));
		switch (d.pattern) {
		case coding::Pattern::All: break;
		case coding::Pattern::Random: {
			ImGui::SameLine();
			float p = d.probability * 100.0f;
			if (ImGui::SliderFloat("##p", &p, 0.01f, 50.0f, "%.2f %%", ImGuiSliderFlags_Logarithmic)) d.probability = p / 100.0f;
			break;
		}
		case coding::Pattern::EveryNth:
			ImGui::SameLine();
			ImGui::InputInt("n", &d.every, 0);
			d.every = std::max(d.every, 1);
			ImGui::SameLine();
			ImGui::InputInt("from", &d.offset, 0);
			d.offset = std::max(d.offset, 0);
			break;
		case coding::Pattern::Burst:
			ImGui::SameLine();
			ImGui::InputInt("bits from", &d.burst_length, 0);
			d.burst_length = std::max(d.burst_length, 0);
			ImGui::SameLine();
			ImGui::InputInt("##start", &d.burst_start, 0);
			d.burst_start = std::max(d.burst_start, 0);
			break;
		}
		if (d.effect == coding::Effect::Noise) {
			ImGui::SameLine();
			ImGui::SliderFloat("sigma", &d.sigma, 0.0f, 2.0f, "%.2f");
		}
		if (d.pattern == coding::Pattern::Random || d.effect == coding::Effect::Noise) {
			ImGui::SameLine();
			int seed = static_cast<int>(d.seed);
			if (ImGui::InputInt("seed", &seed, 0)) d.seed = static_cast<unsigned>(std::max(seed, 0));
		}
		ImGui::PopItemWidth();
		ImGui::SameLine();
		if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
		ImGui::PopID();
	}
	if (remove >= 0) damage_.erase(damage_.begin() + remove);
	if (ImGui::SmallButton("+ Add damage")) damage_.push_back({ .seed = static_cast<unsigned>(damage_.size() + 1) });
	if (damage_.empty()) {
		ImGui::SameLine();
		ImGui::TextDisabled("(none: every bit arrives as it was sent)");
	}
}

// ---- results --------------------------------------------------------------------------------------------------

void CodingWindow::draw_summary(const Outcome& out) {
	if (job_.busy()) ui::busy("Decoding");
	if (!out.error.empty() || !out.tx || !out.rx) {
		ui::error_text(out.error);
		return;
	}
	const coding::Transmission& tx = *out.tx;
	const coding::Reception& rx = *out.rx;

	ImGui::Text("On the air: %zu bits, %zu wrong (%.2f %%), %zu erased.", tx.sent.size(), rx.channel_errors, percent(rx.channel_errors, tx.sent.size()), rx.erased);
	const bool clean = rx.decoded_errors == 0;
	ImGui::TextColored(clean ? good_text : bad_text, "%s: %zu of %zu message bits wrong (%.2f %%), %zu of %zu codeword(s) failed.",
		tx.code.enabled ? "After decoding" : "Received", rx.decoded_errors, tx.message.size(), percent(rx.decoded_errors, tx.message.size()),
		rx.frame_errors, tx.frames);
	if (!rx.crc_failed.empty()) {
		const auto flagged = static_cast<std::size_t>(std::ranges::count(rx.crc_failed, true));
		std::size_t missed = 0;
		for (std::size_t f = 0; f < tx.frames; ++f) missed += rx.frame_wrong[f] && !rx.crc_failed[f];
		ImGui::TextDisabled("The CRC flagged %zu codeword(s) as broken%s.", flagged,
			missed ? std::format(", and missed {} that are", missed).c_str() : "");
	}
	if (out.uncoded) {
		ImGui::TextDisabled("Without error correction, the same damage leaves %zu message bits wrong (%.2f %%).",
			out.uncoded->decoded_errors, percent(out.uncoded->decoded_errors, tx.message.size()));
	}
	if (out.inverted) ImGui::TextDisabled("The receiver locked upside down (180 degrees off); its bits were flipped back.");
}

void CodingWindow::draw_error_map(const Outcome& out) {
	if (!out.tx || !out.rx) return;
	const coding::Transmission& tx = *out.tx;
	const coding::Reception& rx = *out.rx;
	const std::size_t columns = tx.code.enabled ? tx.frame_size : std::min<std::size_t>(tx.sent.size(), 128);

	legend_square(ok_color, "arrived right");
	legend_square(wrong_color, "arrived wrong");
	legend_square(erased_color, "erased");
	if (tx.code.enabled) legend_square(good_frame_color, "codeword repaired");
	ImGui::NewLine();

	ImGui::TextDisabled("On the air, in the order sent (%zu bits per row):", columns);
	bit_grid("##air", tx.sent.size(), columns, [&](std::size_t i) {
		if (rx.soft[i] == 0.0f) return erased_color;
		return (rx.soft[i] < 0.0f) != (tx.sent[i] != 0) ? wrong_color : ok_color;
	}, nullptr);

	ImGui::TextDisabled(tx.code.enabled ? "After deinterleaving: one codeword per row (left: repaired or not):" : "After deinterleaving:");
	ui::help(tips::error_map);
	bit_grid("##codewords", tx.codewords.size(), columns, [&](std::size_t i) {
		if (rx.deinterleaved[i] == 0.0f) return erased_color;
		return rx.received[i] != tx.codewords[i] ? wrong_color : ok_color;
	}, tx.code.enabled ? std::function<ImU32(std::size_t)>([&](std::size_t row) { return row < rx.frame_wrong.size() && rx.frame_wrong[row] ? wrong_color : good_frame_color; })
		: nullptr);
}

void CodingWindow::draw_soft_bits(const Outcome& out) {
	if (!out.tx || !out.rx) return;
	const coding::Transmission& tx = *out.tx;
	const coding::Reception& rx = *out.rx;
	ImGui::TextDisabled("What is this? (?)");
	ui::explain(tips::soft_bits);

	// At most ~40000 dots; wrong ones always shown.
	const std::size_t step = std::max<std::size_t>(1, rx.soft.size() / 40000);
	std::vector<float> rx_x, rx_y, wx, wy;
	for (std::size_t i = 0; i < rx.soft.size(); ++i) {
		const bool wrong = (rx.soft[i] < 0.0f) != (tx.sent[i] != 0);
		if (wrong) {
			wx.push_back(static_cast<float>(i));
			wy.push_back(rx.soft[i]);
		}
		else if (i % step == 0) {
			rx_x.push_back(static_cast<float>(i));
			rx_y.push_back(rx.soft[i]);
		}
	}
	if (!ImPlot::BeginPlot("##soft", { -1.0f, std::max(ui::em(14.0f), ImGui::GetContentRegionAvail().y) })) return;
	ImPlot::SetupAxes("Bit (in the order sent)", "Soft value (+ = 0, - = 1)");
	ImPlot::SetupAxesLimits(0.0, std::min<double>(512.0, static_cast<double>(rx.soft.size())), -2.0, 2.0, ImPlotCond_Once);
	plots::horizontal_line(0.0, IM_COL32(255, 255, 255, 90));
	ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 2.0f, { 0.4f, 0.7f, 1.0f, 0.7f }, 0.0f);
	ImPlot::PlotScatter("right", rx_x.data(), rx_y.data(), static_cast<int>(rx_x.size()));
	ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f, { 1.0f, 0.3f, 0.25f, 1.0f }, 0.0f);
	ImPlot::PlotScatter("wrong side", wx.data(), wy.data(), static_cast<int>(wx.size()));
	ImPlot::EndPlot();
}

void CodingWindow::draw_text(const Outcome& out) {
	if (!out.tx || !out.rx) return;
	const bool text = out.inputs.source == MessageSource::Text;
	const auto show = [&](const char* label, const coding::Bits& bits, const coding::Bits* reference) {
		ImGui::TextDisabled("%s", label);
		std::string body = text ? coding::bits_to_text(bits) : coding::to_string(std::span(bits).first(std::min<std::size_t>(bits.size(), 1024)));
		if (!text && bits.size() > 1024) body += " ...";
		if (reference) {
			std::size_t wrong = 0;
			for (std::size_t i = 0; i < std::min(bits.size(), reference->size()); ++i) wrong += bits[i] != (*reference)[i];
			ImGui::PushStyleColor(ImGuiCol_Text, wrong ? bad_text : good_text);
		}
		ImGui::TextWrapped("%s", body.c_str());
		if (reference) ImGui::PopStyleColor();
		ImGui::Spacing();
	};
	show("Sent:", out.tx->message, nullptr);
	show(out.tx->code.enabled ? "Received, after error correction:" : "Received:", out.rx->decoded, &out.tx->message);
	if (out.uncoded) show("The same damage without error correction:", out.uncoded->decoded, &out.tx->message);
}

void CodingWindow::draw_curve() {
	ui::hint("How often bits still come out wrong after decoding, over a radio channel with Gaussian noise (BPSK), from very noisy "
		"(left) to clean (right). The grey line is sending without a code. Compare decoders, list sizes and code rates.");
	ImGui::PushItemWidth(ui::em(4.0f));
	ImGui::DragFloat("from", &curve_from_, 0.1f, -5.0f, 15.0f, "%.1f");
	ImGui::SameLine();
	ImGui::DragFloat("to", &curve_to_, 0.1f, -5.0f, 15.0f, "%.1f");
	ImGui::SameLine();
	ImGui::DragFloat("step (dB Eb/N0)", &curve_step_, 0.05f, 0.1f, 5.0f, "%.2f");
	ImGui::PopItemWidth();
	ui::help(tips::eb_n0);
	ImGui::SetNextItemWidth(ui::em(8.0f));
	ImGui::SliderInt("Codewords per point", &curve_frames_, 100, 100'000, "%d", ImGuiSliderFlags_Logarithmic);

	if (curve_job_.busy()) {
		if (ImGui::Button("Stop")) curve_job_.cancel();
		ImGui::SameLine();
		ui::busy("Simulating");
	}
	else {
		ImGui::BeginDisabled(!code_.enabled);
		if (ImGui::Button("Add a curve for these code settings")) {
			curve_label_ = describe(code_);
			const auto code = code_;
			const double from = curve_from_, to = std::max(curve_to_, curve_from_), step = std::max(curve_step_, 0.1f);
			const auto frames = static_cast<std::size_t>(curve_frames_);
			curve_job_.start([=](std::stop_token stop) -> std::optional<std::vector<coding::CurvePoint>> {
				auto points = coding::ber_curve(code, from, to, step, frames, stop);
				if (points.empty()) return std::nullopt;
				return points;
			});
		}
		ImGui::EndDisabled();
		if (!curves_.empty()) {
			ImGui::SameLine();
			if (ImGui::Button("Clear")) curves_.clear();
		}
	}

	if (!ImPlot::BeginPlot("##ber", { -1.0f, std::max(ui::em(16.0f), ImGui::GetContentRegionAvail().y) })) return;
	ImPlot::SetupAxes("Eb/N0 (dB)", "Bit error rate");
	ImPlot::SetupAxisScale(ImAxis_Y1, ImPlotScale_Log10);
	ImPlot::SetupAxesLimits(curve_from_, curve_to_, 1e-6, 1.0, ImPlotCond_Once);

	std::vector<double> ux, uy; // without a code, in theory
	for (double db = std::min(curve_from_, 0.0f); db <= std::max(curve_to_, 10.0f); db += 0.1) {
		ux.push_back(db);
		uy.push_back(0.5 * std::erfc(std::sqrt(std::pow(10.0, db / 10.0))));
	}
	ImPlot::SetNextLineStyle({ 0.8f, 0.8f, 0.8f, 0.7f }, 1.5f);
	ImPlot::PlotLine("no code (BPSK)", ux.data(), uy.data(), static_cast<int>(ux.size()));

	for (std::size_t c = 0; c < curves_.size(); ++c) {
		std::vector<double> x, y;
		for (const auto& p : curves_[c].points) {
			if (p.ber <= 0.0) continue; // not measurable with this many codewords
			x.push_back(p.eb_n0_db);
			y.push_back(p.ber);
		}
		ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3.0f);
		ImPlot::PlotLine(std::format("{}##{}", curves_[c].label, c).c_str(), x.data(), y.data(), static_cast<int>(x.size()));
	}
	ImPlot::EndPlot();
}
