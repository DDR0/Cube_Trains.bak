#include <algorithm>
#include <list>
#include <numeric>
#include <sstream>

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include "asserts.hpp"
#include "custom_object.hpp"
#include "custom_object_functions.hpp"
#include "custom_object_type.hpp"
#include "draw_scene.hpp"
#include "editor.hpp"
#include "filesystem.hpp"
#include "font.hpp"
#include "formatter.hpp"
#include "foreach.hpp"
#include "debug_console.hpp"
#include "decimal.hpp"
#include "foreach.hpp"
#include "level.hpp"
#include "load_level.hpp"
#include "player_info.hpp"
#include "preferences.hpp"
#include "raster.hpp"
#include "slider.hpp"
#include "text_entry_widget.hpp"

namespace debug_console
{

namespace {
int graph_cycle = 0;

struct SampleSet {
	SampleSet() : last_cycle(0) {}
	int last_cycle;
	std::vector<decimal> samples;
};

std::map<std::string, SampleSet> graphs;
typedef std::pair<const std::string, SampleSet> graph_pair;

int round_up_value(decimal value)
{
	if(value == decimal()) {
		return 0;
	}

	int result = 1;
	while(result > 0 && result < value) {
		result *= 10;
	}

	if(result < 0) {
		return value.as_int();
	}

	if(result/5 >= value) {
		return result/5;
	} else if(result/2 >= value) {
		return result/2;
	} else {
		return result;
	}
}
}

void add_graph_sample(const std::string& id, decimal value)
{
	SampleSet& s = graphs[id];
	if(graph_cycle - s.last_cycle >= 1000) {
		s.samples.clear();
	} else {
		for(; s.last_cycle < graph_cycle; ++s.last_cycle) {
			s.samples.push_back(decimal());
		}
	}

	if(s.samples.empty()) {
		s.samples.push_back(decimal());
	}

	s.last_cycle = graph_cycle;
	s.samples.back() += value;
}

void process_graph()
{
	++graph_cycle;
}

void draw_graph()
{
	decimal min_value, max_value;
	foreach(graph_pair& p, graphs) {
		if(p.second.last_cycle - p.second.last_cycle >= 1000) {
			p.second.samples.clear();
		}

		foreach(const decimal& value, p.second.samples) {
			if(value < min_value) {
				min_value = value;
			}

			if(value > max_value) {
				max_value = value;
			}
		}
	}

	if(max_value == min_value) {
		return;
	}

	max_value = decimal::from_int(round_up_value(max_value));
	min_value = decimal::from_int(-round_up_value(-min_value));

	const rect graph_area(50, 60, 500, 200);
	graphics::draw_rect(graph_area, graphics::color(255, 255, 255, 64));

	graphics::draw_rect(rect(graph_area.x(), graph_area.y(), graph_area.w(), 2), graphics::color(255,255,255,255));
	graphics::blit_texture(font::render_text_uncached(formatter() << max_value.as_int(), graphics::color_white(), 14), graph_area.x2() + 4, graph_area.y());

	graphics::draw_rect(rect(graph_area.x(), graph_area.y2(), graph_area.w(), 2), graphics::color(255,255,255,255));
	graphics::blit_texture(font::render_text_uncached(formatter() << min_value.as_int(), graphics::color_white(), 14), graph_area.x2() + 4, graph_area.y2() - 12);

	graphics::color GraphColors[] = {
		graphics::color(255,255,255,255),
		graphics::color(0,0,255,255),
		graphics::color(255,0,0,255),
		graphics::color(0,255,0,255),
		graphics::color(255,255,0,255),
		graphics::color(128,128,128,255),
	};

	int colors_index = 0;
	foreach(const graph_pair& p, graphs) {
		if(p.second.samples.empty()) {
			return;
		}

		const graphics::color& graph_color = GraphColors[colors_index%(sizeof(GraphColors)/sizeof(*GraphColors))];
		graph_color.set_as_current_color();

		const int gap = graph_cycle - p.second.last_cycle;
		int index = (gap + p.second.samples.size()) - 1000;
		int pos = 0;
		if(index < 0) {
			pos -= index;
			index = 0;
		}

		//collect the last 20 y samples to average for the label's position.
		std::vector<GLfloat> y_samples;

		std::vector<GLfloat> points;

		while(index < p.second.samples.size()) {
			decimal value = p.second.samples[index];

			const GLfloat xpos = graph_area.x() + (GLfloat(pos)*graph_area.w())/GLfloat(1000);

			const GLfloat value_ratio = ((value - min_value)/(max_value - min_value)).as_float();
			const GLfloat ypos = graph_area.y2() - graph_area.h()*value_ratio;
			points.push_back(xpos);
			points.push_back(ypos);
			y_samples.push_back(ypos);
			++index;
			++pos;
		}

		if(points.empty()) {
			continue;
		}

		if(y_samples.size() > 20) {
			y_samples.erase(y_samples.begin(), y_samples.end() - 20);
		}

		const GLfloat mean_ypos = std::accumulate(y_samples.begin(), y_samples.end(), 0.0)/y_samples.size();

		glDisable(GL_TEXTURE_2D);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);

		glVertexPointer(2, GL_FLOAT, 0, &points[0]);
		glDrawArrays(GL_LINE_STRIP, 0, points.size()/2);

		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glEnable(GL_TEXTURE_2D);

		graphics::blit_texture(font::render_text_uncached(p.first, graph_color.as_sdl_color(), 14), points[points.size()-2] + 4, mean_ypos - 6);

		++colors_index;
	}

	glColor4f(1.0,1.0,1.0,1.0);
}

namespace {
std::list<graphics::texture>& messages() {
	static std::list<graphics::texture> message_queue;
	return message_queue;
}

std::set<console_dialog*> consoles_;

const std::string Prompt = "--> ";
}

void add_message(const std::string& msg)
{
	if(!preferences::debug()) {
		return;
	}

	if(!consoles_.empty()) {
		foreach(console_dialog* d, consoles_) {
			d->add_message(msg);
		}
		return;
	}

	if(msg.size() > 100) {
		std::string trunc_msg(msg.begin(), msg.begin() + 90);
		trunc_msg += "...";
		add_message(trunc_msg);
		return;
	}

	const SDL_Color col = {255, 255, 255, 255};
	try {
		messages().push_back(font::render_text_uncached(msg, col, 14));
	} catch(font::error& e) {

		std::cerr << "FAILED TO ADD MESSAGE DUE TO FONT RENDERING FAILURE\n";
		return;
	}
	if(messages().size() > 8) {
		messages().pop_front();
	}
}

void draw()
{
	if(messages().empty()) {
		return;
	}

	int ypos = 100;
	foreach(const graphics::texture& t, messages()) {
		const SDL_Rect area = {0, ypos-2, t.width() + 10, t.height() + 5};
		graphics::draw_rect(area, graphics::color_black(), 128);
		graphics::blit_texture(t, 5, ypos);
		ypos += t.height() + 5;
	}
}
	
#if !TARGET_IPHONE_SIMULATOR && !TARGET_OS_IPHONE
namespace {
class console
{
public:
	explicit console()
	  : history_pos_(0), invalidated_(false), history_length_(150) {
		entry_.set_font("door_label");
		entry_.set_loc(10, 300);
		entry_.set_dim(300, 20);
	}

	void execute(level& lvl_state, entity& ob) const {
		history_.clear();
		history_pos_ = 0;

		boost::intrusive_ptr<level> level_obj;

		//The first time we do a 'prev' command we must go back twice
		//since we have a backup of the existing state to begin with.
		bool needs_double_prev = true;

		level* lvl = &lvl_state;

		while(lvl->cycle() < controls::local_controls_end()) {
			controls::control_backup_scope ctrl_backup;
			lvl->process();
		}

		entity_ptr context(&ob);
		std::string context_label = context->label();
		lvl->editor_select_object(context);

		bool show_shadows = false;

		bool done = false;
		while(!done) {
			int mousex, mousey;
			const unsigned int buttons = SDL_GetMouseState(&mousex, &mousey);

			entity_ptr selected = lvl->get_next_character_at_point(last_draw_position().x/100 + mousex, last_draw_position().y/100 + mousey, last_draw_position().x/100, last_draw_position().y/100);
			lvl->editor_clear_selection();
			lvl->editor_select_object(selected);

			SDL_Event event;
			while(SDL_PollEvent(&event)) {
				switch(event.type) {
				case SDL_MOUSEBUTTONDOWN: {
					if(selected) {
						context = selected;
						context_label = context->label();
						lvl->editor_clear_selection();
						lvl->editor_select_object(context);
						debug_console::add_message(formatter() << "Selected object: " << selected->debug_description());
					}
					break;
				}

				case SDL_KEYDOWN:
					done = event.key.keysym.sym == SDLK_ESCAPE;

					if(event.key.keysym.sym == SDLK_RETURN) {
						try {
							const std::string text = entry_.text();

							history_.push_back(entry_.text());
							history_pos_ = history_.size();
							entry_.set_text("");

							if(text == "next") {
								const controls::control_backup_scope ctrl_scope;
								needs_double_prev = true;
								lvl->process();
								lvl->process_draw();
								lvl->backup();
								break;
							} else if(text == "prev") {
								if(needs_double_prev) {
									lvl->reverse_one_cycle();
									needs_double_prev = false;
								}
								lvl->reverse_one_cycle();
								lvl->set_active_chars();
								lvl->process_draw();

								context = select_object(*lvl, context_label);

								break;
							} else if(text == "step") {
								context->process(*lvl);
								break;
							} else if(text.size() >= 7 && std::equal(text.begin(), text.begin()+7, "history")) {
								history_length_ = 150;
								if(text.size() > 7) {
									const int len = atoi(text.c_str()+7);
									if(len > 1) {
										history_length_ = len;
									}
								}

								shadows_from_the_past_ = lvl->predict_future(context, history_length_);
								show_shadows = true;
								context = select_object(*lvl, context_label);

								history_slider_.reset(new gui::slider(300, boost::bind(&console::history_slider_change, this, lvl, _1), 1.0));
								break;
							}

							game_logic::formula f(variant(text), &get_custom_object_functions_symbol_table());
							variant v = f.execute(*context);
							context->execute_command(v);
							debug_console::add_message(v.to_debug_string());

							if(show_shadows) {
								shadows_from_the_past_ = lvl->predict_future(context, history_length_);
							}

							context = select_object(*lvl, context_label);
						} catch(...) {
							debug_console::add_message("unknown error parsing formula");
						}
					} else if(event.key.keysym.sym == SDLK_UP) {
						if(history_pos_ > 0) {
							--history_pos_;
							ASSERT_LT(history_pos_, history_.size());
							entry_.set_text(history_[history_pos_]);
						}
					} else if(event.key.keysym.sym == SDLK_DOWN) {
						if(!history_.empty() && history_pos_ < history_.size() - 1) {
							++history_pos_;
							entry_.set_text(history_[history_pos_]);
						} else {
							history_pos_ = history_.size();
							entry_.set_text("");
						}
					}
					break;
				}

				entry_.process_event(event, false);

				if(history_slider_) {
					history_slider_->process_event(event, false);
				}
			}

			if(invalidated_) {
				context = select_object(*lvl, context_label);
				shadows_from_the_past_ = lvl->predict_future(context, history_length_);
				context = select_object(*lvl, context_label);
				invalidated_ = false;
			}

			if(show_shadows) {
				if(custom_object_type::reload_modified_code()) {
					shadows_from_the_past_ = lvl->predict_future(context, history_length_);
					context = select_object(*lvl, context_label);
				}
			}

			lvl->editor_clear_selection();
			lvl->editor_select_object(context);
			lvl->set_active_chars();

			std::vector<variant> alpha_values;
			foreach(entity_ptr e, shadows_from_the_past_) {
				alpha_values.push_back(e->query_value("alpha"));
				e->mutate_value("alpha", variant(32));
				lvl->add_draw_character(e);
			}

			draw(*lvl);

			int index = 0;
			foreach(entity_ptr e, shadows_from_the_past_) {
				e->mutate_value("alpha", alpha_values[index++]);
			}
			lvl->set_active_chars();
			SDL_Delay(20);
		}

		lvl_state.editor_clear_selection();
		lvl_state.set_as_current_level();

		while(lvl->cycle() < controls::local_controls_end()) {
			controls::control_backup_scope ctrl_backup;
			lvl->process();
		}

		controls::read_until(lvl_state.cycle());
	}
private:
	void draw(const level& lvl) const {
		draw_scene(lvl, last_draw_position(), &lvl.player()->get_entity());

		entry_.draw();
		if(history_slider_) {
			history_slider_->draw();
		}

		SDL_GL_SwapBuffers();
#if defined(__ANDROID__)
		graphics::reset_opengl_state();
#endif
	}

	entity_ptr select_object(level& lvl, std::string& label) const {
		entity_ptr context = lvl.get_entity_by_label(label);
		if(!context) {
			context.reset(&lvl.player()->get_entity());
			label = context->label();
		}
		lvl.editor_clear_selection();
		lvl.editor_select_object(context);

		return context;
	}

	void history_slider_change(level* lvl, float value) const {
		if(shadows_from_the_past_.empty()) {
			return;
		}

		int index = value*(shadows_from_the_past_.size()+1);
		if(index < 0) {
			index = 0;
		}
		if(index >= shadows_from_the_past_.size()) {
			index = shadows_from_the_past_.size()-1;
		}

		const int endpoint = controls::local_controls_end();
		const int target_point = (endpoint - shadows_from_the_past_.size()) + index;
		if(endpoint == target_point) {
			return;
		}

		invalidated_ = true;

		std::cerr << "HISTORY SLIDER: " << value << " -> " << target_point << " WE ARE AT " << lvl->cycle() << "\n";
		const controls::control_backup_scope ctrl_scope;
		while(lvl->cycle() < target_point) {
			std::cerr << "FORWARDING: " << lvl->cycle() << " < " << target_point << "\n";
			lvl->process();
			lvl->process_draw();
			lvl->backup();
		}

		int max_iter = 5000;
		while(lvl->cycle() > target_point) {
			std::cerr << "REVERSING: " << lvl->cycle() << " > " << target_point << "\n";
			lvl->reverse_one_cycle();
			if(!--max_iter) {
				break;
			}
		}

		std::cerr << "DONE REVERSING\n";

		lvl->set_active_chars();
	}

	mutable gui::text_entry_widget entry_;
	mutable std::vector<std::string> history_;
	mutable int history_pos_;
	mutable gui::slider_ptr history_slider_;
	mutable std::vector<entity_ptr> shadows_from_the_past_;

	mutable bool invalidated_;
	mutable int history_length_;
};

}

void show_interactive_console(level& lvl, entity& obj)
{
	console().execute(lvl, obj);
}

#else
void show_interactive_console(level& lvl, entity& obj) {}
#endif

console_dialog::console_dialog(level& lvl, entity& obj)
   : dialog(0, graphics::screen_height() - 200, 600, 200), lvl_(&lvl), focus_(&obj),
     history_pos_(0)
{
	init();

	consoles_.insert(this);

	text_editor_->set_focus(true);
}

console_dialog::~console_dialog()
{
	consoles_.erase(this);
}

void console_dialog::init()
{
	using namespace gui;
	text_editor_ = new text_editor_widget(width() - 20, height() - 20);
	add_widget(widget_ptr(text_editor_), 10, 10);

	text_editor_->set_on_move_cursor_handler(boost::bind(&console_dialog::on_move_cursor, this));
	text_editor_->set_on_begin_enter_handler(boost::bind(&console_dialog::on_begin_enter, this));
	text_editor_->set_on_enter_handler(boost::bind(&console_dialog::on_enter, this));

	text_editor_->set_text(Prompt);
	text_editor_->set_cursor(0, Prompt.size());
}

void console_dialog::on_move_cursor()
{
	if(text_editor_->cursor_row() < text_editor_->get_data().size()-1) {
		text_editor_->set_cursor(text_editor_->get_data().size()-1, text_editor_->cursor_col());
	}

	if(text_editor_->cursor_col() < Prompt.size()) {
		text_editor_->set_cursor(text_editor_->get_data().size()-1, Prompt.size());
	}
}

bool console_dialog::on_begin_enter()
{
	if(lvl_->editor_selection().empty() == false) {
		focus_ = lvl_->editor_selection().front();
	}

	std::vector<std::string> data = text_editor_->get_data();

	std::string ffl(text_editor_->get_data().back());
	while(ffl.size() < Prompt.size() || std::equal(Prompt.begin(), Prompt.end(), ffl.begin()) == false) {
		data.pop_back();
		ASSERT_LOG(data.empty() == false, "No prompt found in debug console: " << ffl);
		ffl = data.back() + ffl;
	}

	ffl.erase(ffl.begin(), ffl.begin() + Prompt.size());
	text_editor_->set_text(text_editor_->text() + "\n" + Prompt);
	text_editor_->set_cursor(text_editor_->get_data().size()-1, Prompt.size());
	if(!ffl.empty()) {
		history_.push_back(ffl);
		history_pos_ = history_.size();

		assert_recover_scope recover_from_assert;
		try {
			std::cerr << "EVALUATING: " << ffl << "\n";
			variant ffl_variant(ffl);
			std::string filename = "(debug console)";
			variant::debug_info info;
			info.filename = &filename;
			info.line = info.column = 0;
			ffl_variant.set_debug_info(info);

			static custom_object_callable custom_object_definition;

			game_logic::formula f(ffl_variant, &get_custom_object_functions_symbol_table(), &custom_object_definition);
			variant v = f.execute(*focus_);
			focus_->execute_command(v);
			debug_console::add_message(v.to_debug_string());
			std::cerr << "OUTPUT: " << v.to_debug_string() << std::endl;
		} catch(validation_failure_exception& e) {
			debug_console::add_message("error parsing formula: " + e.msg);
		} catch(type_error& e) {
			debug_console::add_message("error executing formula: " + e.message);
		}
	}

	return false;
}

void console_dialog::on_enter()
{
}

bool console_dialog::has_keyboard_focus() const
{
	return text_editor_->has_focus();
}

void console_dialog::add_message(const std::string& msg)
{
	std::string m;
	for(std::vector<std::string>::const_iterator i = text_editor_->get_data().begin(); i != text_editor_->get_data().end()-1; ++i) {
		m += *i + "\n";
	}

	m += msg + "\n";
	m += text_editor_->get_data().back();

	int col = text_editor_->cursor_col();
	text_editor_->set_text(m);
	text_editor_->set_cursor(text_editor_->get_data().size()-1, col);
}

bool console_dialog::handle_event(const SDL_Event& event, bool claimed)
{
	if(!claimed && has_keyboard_focus()) {
		switch(event.type) {
		case SDL_KEYDOWN:
			if((event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_DOWN) && !history_.empty()) {
				if(event.key.keysym.sym == SDLK_UP) {
					--history_pos_;
				} else {
					++history_pos_;
				}

				if(history_pos_ < 0) {
					history_pos_ = history_.size();
				} else if(history_pos_ > history_.size()) {
					history_pos_ = history_.size();
				}

				load_history();
				return true;
			}
			break;
		}
	}

	return dialog::handle_event(event, claimed);
}

void console_dialog::load_history()
{
	std::string str;
	if(history_pos_ < history_.size()) {
		str = history_[history_pos_];
	}

	std::string m;
	for(std::vector<std::string>::const_iterator i = text_editor_->get_data().begin(); i != text_editor_->get_data().end()-1; ++i) {
		m += *i + "\n";
	}

	m += Prompt + str;
	text_editor_->set_text(m);

	text_editor_->set_cursor(text_editor_->get_data().size()-1, text_editor_->get_data().back().size());
}

void console_dialog::set_focus(entity_ptr e)
{
	focus_ = e;
	text_editor_->set_focus(true);
	add_message(formatter() << "Selected object: " << e->debug_description());
}

}
