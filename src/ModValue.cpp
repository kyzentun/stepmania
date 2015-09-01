#include "global.h"
#include "CubicSpline.h"
#include "EnumHelper.h"
#include "ModValue.h"
#include "RageLog.h"
#include "RageMath.h"
#include "LuaBinding.h"

using std::vector;

/*
static const vector<string> mod_names= {
	"alternate", "beat", "blind", "blink", "boomerang", "boost", "brake",
	"bumpy", "centered", "confusion", "cover", "cross", "dark", "dizzy",
	"drunk", "expand", "flip", "hidden", "hidden_offset", "invert",
	"max_scroll_bpm", "mini", "no_attack", "pass_mark", "player_autoplayer",
	"rand_attack", "random_speed", "random_vanish", "reverse", "roll",
	"scroll_speed", "skew", "split", "stealth", "sudden", "sudden_offset",
	"tilt", "time_spacing", "tiny", "tipsy", "tornado", "twirl", "wave",
	"xmode"
};
*/

static const char* ModInputTypeNames[] = {
	"Scalar",
	"EvalBeat",
	"EvalSecond",
	"MusicBeat",
	"MusicSecond",
	"DistBeat",
	"DistSecond",
	"YOffset",
	"StartDistBeat",
	"StartDistSecond",
	"EndDistBeat",
	"EndDistSecond",
};
XToString(ModInputType);
LuaXType(ModInputType);

static const char* ModFunctionTypeNames[] = {
	"Constant",
	"Product",
	"Power",
	"Log",
	"Sine",
	"Tan",
	"Square",
	"Triangle",
	"Spline",
};
XToString(ModFunctionType);
LuaXType(ModFunctionType);

void ModManager::update(double curr_beat, double curr_second)
{
	double const time_diff= curr_second - m_prev_curr_second;
	if(time_diff == 0)
	{
		return;
	}
	if(time_diff > 0)
	{
		// Time is moving forwards.
		for(auto fap= m_present_funcs.begin(); fap != m_present_funcs.end();)
		{
			auto this_fap= fap++;
			if(this_fap->func->m_end_second < curr_second)
			{
				insert_into_past(*this_fap);
				remove_from_present(this_fap);
			}
			else
			{
				break;
			}
		}
		for(auto fap= m_future_funcs.begin(); fap != m_future_funcs.end();)
		{
			auto this_fap= fap++;
			if(this_fap->func->m_start_second <= curr_second)
			{
				if(this_fap->func->m_end_second < curr_second)
				{
					insert_into_past(*this_fap);
				}
				else
				{
					insert_into_present(*this_fap);
				}
				m_future_funcs.erase(this_fap);
			}
			else
			{
				break;
			}
		}
	}
	else
	{
		// Time is moving backwards.
		for(auto fap= m_present_funcs.begin(); fap != m_present_funcs.end();)
		{
			auto this_fap= fap++;
			if(this_fap->func->m_end_second < curr_second)
			{
				insert_into_past(*this_fap);
				remove_from_present(this_fap);
			}
			else if(this_fap->func->m_start_second > curr_second)
			{
				insert_into_future(*this_fap);
				remove_from_present(this_fap);
			}
		}
		for(auto fap= m_past_funcs.begin(); fap != m_past_funcs.end();)
		{
			auto this_fap= fap++;
			if(this_fap->func->m_end_second >= curr_second)
			{
				if(this_fap->func->m_start_second > curr_second)
				{
					insert_into_future(*this_fap);
				}
				else
				{
					insert_into_present(*this_fap);
				}
				m_past_funcs.erase(this_fap);
			}
			else
			{
				break;
			}
		}
	}
	m_prev_curr_second= curr_second;
	update_splines(curr_beat, curr_second);
}
// ModManager::update_splines has to be defined below all the different
// ModFunction types so that the compiler can see the full ModFunctionSpline
// declaration when processing it.

void ModManager::add_mod(ModFunction* func, ModifiableValue* parent)
{
	if(func->m_start_second > m_prev_curr_second)
	{
		insert_into_future(func, parent);
	}
	else if(func->m_end_second < m_prev_curr_second)
	{
		insert_into_past(func, parent);
	}
	else
	{
		insert_into_present(func, parent);
	}
}

void ModManager::remove_mod(ModFunction* func)
{
	for(auto&& managed_list : {&m_past_funcs, &m_present_funcs, &m_future_funcs})
	{
		for(auto fap= managed_list->begin(); fap != managed_list->end();)
		{
			auto this_fap= fap++;
			if(this_fap->func == func)
			{
				managed_list->erase(this_fap);
			}
		}
	}
}

void ModManager::remove_all_mods(ModifiableValue* parent)
{
	for(auto&& managed_list : {&m_past_funcs, &m_present_funcs, &m_future_funcs})
	{
		for(auto fap= managed_list->begin(); fap != managed_list->end();)
		{
			auto this_fap= fap++;
			if(this_fap->parent == parent)
			{
				managed_list->erase(this_fap);
			}
		}
	}
}

void ModManager::dump_list_status()
{
	LOG->Trace("ModManager::dump_list_status:");
	for(auto&& managed_list : {&m_past_funcs, &m_present_funcs, &m_future_funcs})
	{
		for(auto fap= managed_list->begin(); fap != managed_list->end(); ++fap)
		{
			LOG->Trace("%f, %f : %f, %f", fap->func->m_start_beat, fap->func->m_start_second, fap->func->m_end_beat, fap->func->m_end_second);
		}
		LOG->Trace("list over");
	}
}

void ModManager::insert_into_past(ModFunction* func, ModifiableValue* parent)
{
	// m_past_funcs is sorted in descending end second order.  Entries with the
	// same end second are sorted in undefined order.
	// This way, when time flows backwards, traversing from beginning to end
	// gives the entries that should go into present.
	// When time flows forwards, this ends up being inserting at the front.
	for(auto fap= m_past_funcs.begin(); fap != m_past_funcs.end(); ++fap)
	{
		if(fap->func->m_end_second < func->m_end_second)
		{
			m_past_funcs.insert(fap, func_and_parent(func, parent));
			return;
		}
	}
	m_past_funcs.push_back(func_and_parent(func, parent));
}

void ModManager::insert_into_present(ModFunction* func, ModifiableValue* parent)
{
	add_to_splines_if_its_a_spline(func);
	parent->add_mod_to_active_list(func);
	// m_present_funcs is sorted in ascending end second order.  Entries with
	// the same end second are sorted in ascending start second order.
	for(auto fap= m_present_funcs.begin(); fap != m_present_funcs.end(); ++fap)
	{
		if(fap->func->m_end_second > func->m_end_second ||
			(fap->func->m_end_second == func->m_end_second &&
				fap->func->m_start_second > func->m_start_second))
		{
			m_present_funcs.insert(fap, func_and_parent(func, parent));
			return;
		}
	}
	m_present_funcs.push_back(func_and_parent(func, parent));
}

void ModManager::insert_into_future(ModFunction* func, ModifiableValue* parent)
{
	// m_future_funcs is sorted in ascending start second order.  Entries with
	// the same start second are sorted in undefined order.
	for(auto fap= m_future_funcs.begin(); fap != m_future_funcs.end(); ++fap)
	{
		if(fap->func->m_start_second > func->m_start_second)
		{
			m_future_funcs.insert(fap, func_and_parent(func, parent));
			return;
		}
	}
	m_future_funcs.push_back(func_and_parent(func, parent));
}

void ModManager::remove_from_present(std::list<func_and_parent>::iterator fapi)
{
	remove_from_splines_if_its_a_spline(fapi->func);
	fapi->parent->remove_mod_from_active_list(fapi->func);
	m_present_funcs.erase(fapi);
}

void ModInput::clear()
{
	m_type= MIT_Scalar;
	m_scalar= 0.0;
	m_offset= 0.0;
	m_rep_enabled= false;
	m_rep_begin= 0.0;
	m_rep_end= 0.0;
	m_phases_enabled= false;
	m_phases.clear();
}

static void get_numbers(lua_State* L, int index, vector<double*> const& ret)
{
	for(size_t i= 0; i < ret.size(); ++i)
	{
		lua_rawgeti(L, index, i+1);
		(*ret[i]) = lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
}

static void push_numbers(lua_State* L, vector<double*> const& noms)
{
	lua_createtable(L, noms.size(), 0);
	for(size_t i= 0; i < noms.size(); ++i)
	{
		lua_pushnumber(L, *noms[i]);
		lua_rawseti(L, -2, i+1);
	}
}

void ModInput::push_phase(lua_State* L, size_t phase)
{
	push_numbers(L, {&m_phases[phase].start, &m_phases[phase].finish,
					&m_phases[phase].mult, &m_phases[phase].offset});
}

void ModInput::push_def_phase(lua_State* L)
{
	push_numbers(L, {&m_default_phase.start, &m_default_phase.finish,
					&m_default_phase.mult, &m_default_phase.offset});
}

void ModInput::load_rep(lua_State* L, int index)
{
	if(lua_istable(L, index))
	{
		m_rep_enabled= true;
		get_numbers(L, index, {&m_rep_begin, &m_rep_end});
	}
}

void ModInput::load_one_phase(lua_State* L, int index, size_t phase)
{
	if(lua_istable(L, index))
	{
		get_numbers(L, index, {&m_phases[phase].start, &m_phases[phase].finish,
					&m_phases[phase].mult, &m_phases[phase].offset});
	}
}

void ModInput::load_def_phase(lua_State* L, int index)
{
	if(lua_istable(L, index))
	{
		get_numbers(L, index, {&m_default_phase.start, &m_default_phase.finish,
					&m_default_phase.mult, &m_default_phase.offset});
	}
}

void ModInput::load_phases(lua_State* L, int index)
{
	if(lua_istable(L, index))
	{
		m_phases_enabled= true;
		lua_getfield(L, index, "default");
		load_def_phase(L, lua_gettop(L));
		lua_pop(L, 1);
		size_t num_phases= lua_objlen(L, index);
		m_phases.resize(num_phases);
		for(size_t i= 0; i < num_phases; ++i)
		{
			lua_rawgeti(L, index, i+1);
			load_one_phase(L, lua_gettop(L), i);
			lua_pop(L, 1);
		}
	}
}

void ModInput::load_from_lua(lua_State* L, int index)
{
	if(lua_isnumber(L, index))
	{
		m_type= MIT_Scalar;
		m_scalar= lua_tonumber(L, index);
		return;
	}
	if(lua_istable(L, index))
	{
		lua_rawgeti(L, index, 1);
		m_type= Enum::Check<ModInputType>(L, -1);
		lua_pop(L, 1);
		// The use of lua_tonumber is deliberate.  If the scalar or offset value
		// does not exist, lua_tonumber will return 0.
		lua_rawgeti(L, index, 2);
		m_scalar= lua_tonumber(L, -1);
		lua_pop(L, 1);
		lua_rawgeti(L, index, 3);
		m_offset= lua_tonumber(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, index, "rep");
		load_rep(L, lua_gettop(L));
		lua_pop(L, 1);
		lua_getfield(L, index, "phases");
		load_phases(L, lua_gettop(L));
		lua_pop(L, 1);
		lua_getfield(L, index, "spline");
		int spline_index= lua_gettop(L);
		if(lua_istable(L, spline_index))
		{
			m_loop_spline= get_optional_bool(L, spline_index, "loop");
			m_polygonal_spline= get_optional_bool(L, spline_index, "polygonal");
			size_t num_points= lua_objlen(L, spline_index);
			m_spline.resize(num_points);
			for(size_t p= 0; p < num_points; ++p)
			{
				lua_rawgeti(L, spline_index, p+1);
				m_spline.set_point(p, lua_tonumber(L, -1));
				lua_pop(L, 1);
			}
			m_spline.solve(m_loop_spline, m_polygonal_spline);
		}
		lua_pop(L, 1);
	}
}

ModInput::phase const* ModInput::find_phase(double input)
{
	if(m_phases.empty() || input < m_phases.front().start || input >= m_phases.back().finish)
	{
		return &m_default_phase;
	}
	// Every time I have to do a binary search, there's some odd wrinkle that
	// forces the implementation to be different.  In this case, input is not
	// guaranteed to be in phase.  For example, if the phase ranges are [0, 1),
	// [2, 3), and the input is 1.5, then no phase should be applied.
	size_t lower= 0;
	size_t upper= m_phases.size()-1;
	if(input < m_phases[lower].finish)
	{
		return &m_phases[lower];
	}
	if(input >= m_phases[upper].start)
	{
		return &m_phases[upper];
	}
	while(lower != upper)
	{
		size_t mid= (upper + lower) / 2;
		if(input < m_phases[mid].start)
		{
			if(mid > lower)
			{
				if(input >= m_phases[mid-1].finish)
				{
					return &m_default_phase;
				}
			}
			else
			{
				return &m_default_phase;
			}
			upper= mid;
		}
		else if(input >= m_phases[mid].finish)
		{
			// mid is mathematically guaranteed to be less than upper.
			if(input < m_phases[mid+1].start)
			{
				return &m_default_phase;
			}
			lower= mid;
		}
		else
		{
			return &m_phases[mid];
		}
	}
	return &m_phases[lower];
}

void ModFunction::load_inputs_from_lua(lua_State* L, int index,
		std::vector<ModInput*> inputs)
{
	// The lua table looks like this:
	// {
	//   name= "string",
	//   start_beat= 5,
	//   start_sec= 5,
	//   end_beat= 5,
	//   end_sec= 5,
	//   type, input, ...
	// }
	// name, and the start and end values are optional.
	// The ... is for the inputs after the first.
	// So the first input is at lua table index 2.
	lua_getfield(L, index, "name");
	if(lua_isstring(L, -1))
	{
		m_name= lua_tostring(L, -1);
	}
	else
	{
		m_name= unique_name("mod");
	}
	lua_pop(L, 1);
	m_start_beat= get_optional_double(L, index, "start_beat", invalid_modfunction_time);
	m_start_second= get_optional_double(L, index, "start_second", invalid_modfunction_time);
	m_end_beat= get_optional_double(L, index, "end_beat", invalid_modfunction_time);
	m_end_second= get_optional_double(L, index, "end_second", invalid_modfunction_time);
	size_t elements= lua_objlen(L, index);
	size_t limit= std::min(elements, inputs.size()+1);
	for(size_t el= 2; el <= limit; ++el)
	{
		lua_rawgeti(L, index, el);
		inputs[el-2]->load_from_lua(L, lua_gettop(L));
		lua_pop(L, 1);
	}
}

static void calc_timing_pair(TimingData const* timing, double& beat, double& second)
{
	bool beat_needed= (beat == invalid_modfunction_time);
	bool second_needed= (second == invalid_modfunction_time);
	if(beat_needed && !second_needed)
	{
		beat= timing->GetBeatFromElapsedTime(second);
	}
	else if(!beat_needed && second_needed)
	{
		second= timing->GetElapsedTimeFromBeat(beat);
	}
}

void ModFunction::calc_unprovided_times(TimingData const* timing)
{
	calc_timing_pair(timing, m_start_beat, m_start_second);
	calc_timing_pair(timing, m_end_beat, m_end_second);
}

struct ModFunctionConstant : ModFunction
{
	ModFunctionConstant(ModifiableValue* parent)
		:ModFunction(parent)
	{}
	ModInput value;
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		return value.pick(input, time);
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		load_inputs_from_lua(L, index, {&value});
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		push_inputs_internal(L, table_index, {&value});
	}
	virtual size_t num_inputs() { return 1; }
};

struct ModFunctionProduct : ModFunction
{
	ModFunctionProduct(ModifiableValue* parent)
		:ModFunction(parent)
	{}
	ModInput value;
	ModInput mult;
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		return value.pick(input, time) * mult.pick(input, time);
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		load_inputs_from_lua(L, index, {&value, &mult});
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		push_inputs_internal(L, table_index, {&value, &mult});
	}
	virtual size_t num_inputs() { return 2; }
};

struct ModFunctionPower : ModFunction
{
	ModFunctionPower(ModifiableValue* parent)
		:ModFunction(parent)
	{}
	ModInput value;
	ModInput mult;
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		return pow(value.pick(input, time), mult.pick(input, time));
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		load_inputs_from_lua(L, index, {&value, &mult});
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		push_inputs_internal(L, table_index, {&value, &mult});
	}
	virtual size_t num_inputs() { return 2; }
};

struct ModFunctionLog : ModFunction
{
	ModFunctionLog(ModifiableValue* parent)
		:ModFunction(parent)
	{}
	ModInput value;
	ModInput base;
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		return log(value.pick(input, time)) / log(base.pick(input, time));
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		load_inputs_from_lua(L, index, {&value, &base});
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		push_inputs_internal(L, table_index, {&value, &base});
	}
	virtual size_t num_inputs() { return 2; }
};

struct ModFunctionWave : ModFunction
{
	ModFunctionWave(ModifiableValue* parent)
		:ModFunction(parent)
	{}
	ModInput angle;
	ModInput phase;
	ModInput amplitude;
	ModInput offset;
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		double amp= amplitude.pick(input, time);
		if(amp == 0.0)
		{
			return offset.pick(input, time);
		}
		double angle_res= angle.pick(input, time) + phase.pick(input, time);
		angle_res= fmod(angle_res, M_PI * 2.0);
		if(angle_res < 0.0)
		{
			angle_res+= M_PI * 2.0;
		}
		double const wave_res= eval_wave(angle_res);
		return (wave_res * amp) + offset.pick(input, time);
	}
	virtual double eval_wave(double const angle)
	{
		return angle;
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		load_inputs_from_lua(L, index, {&angle, &phase, &amplitude, &offset});
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		push_inputs_internal(L, table_index,
			{&angle, &phase, &amplitude, &offset});
	}
	virtual size_t num_inputs() { return 4; }
};

struct ModFunctionSine : ModFunctionWave
{
	ModFunctionSine(ModifiableValue* parent)
		:ModFunctionWave(parent)
	{}
	virtual double eval_wave(double const angle)
	{
		return RageFastSin(angle);
	}
};

struct ModFunctionTan : ModFunctionWave
{
	ModFunctionTan(ModifiableValue* parent)
		:ModFunctionWave(parent)
	{}
	virtual double eval_wave(double const angle)
	{
		return tan(angle);
	}
};

struct ModFunctionSquare : ModFunctionWave
{
	ModFunctionSquare(ModifiableValue* parent)
		:ModFunctionWave(parent)
	{}
	virtual double eval_wave(double const angle)
	{
		return angle >= M_PI ? -1.0 : 1.0;
	}
};

struct ModFunctionTriangle : ModFunctionWave
{
	ModFunctionTriangle(ModifiableValue* parent)
		:ModFunctionWave(parent)
	{}
	virtual double eval_wave(double const angle)
	{
		double ret= angle * M_1_PI;
		if(ret < .5)
		{
			return ret * 2.0;
		}
		else if(ret < 1.5)
		{
			return 1.0 - ((ret - .5) * 2.0);
		}
		return -4.0 + (ret * 2.0);
	}
};

struct ModFunctionSpline : ModFunction
{
	ModFunctionSpline(ModifiableValue* parent)
		:ModFunction(parent)
	{}
	ModInput t_input;
	vector<ModInput> points;
	bool loop;
	bool polygonal;
	// Solving the spline for every note is probably expensive.  But it could
	// allow more flexibility if an input point uses EvalBeat or similar.
	// It's possible to look at the types of the input points and figure out
	// whether per-note solving is necessary, but forcing a flag to be set
	// makes the lua author more aware of the extra processing cost.
	virtual bool needs_per_frame_solve()
	{
		return !per_frame_points.empty();
	}
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		per_note_solve(input, time);
		double t= t_input.pick(input, time);
		return spline.evaluate(t, loop);
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		// The first element of the table is the type.  So the number of points
		// is one less than the size of the table.
		size_t num_points= lua_objlen(L, index) - 1;
		points.resize(num_points);
		vector<ModInput*> point_wrapper;
		point_wrapper.reserve(num_points);
		for(auto&& p : points)
		{
			point_wrapper.push_back(&p);
		}
		load_inputs_from_lua(L, index, point_wrapper);
		lua_getfield(L, index, "t");
		t_input.load_from_lua(L, lua_gettop(L));
		lua_pop(L, 1);
		loop= get_optional_bool(L, index, "loop");
		polygonal= get_optional_bool(L, index, "polygonal");
		// Now that the points are loaded, organize them by type and send all the
		// scalars to the spline now.
		spline.resize(points.size());
		mod_val_inputs scalar_input(0.0, 0.0);
		mod_time_inputs scalar_time(0.0);
		for(size_t p= 0; p < points.size(); ++p)
		{
			ModInputMetaType mt= points[p].get_meta_type();
			switch(mt)
			{
				case MIMT_Scalar:
					spline.set_point(p, points[p].pick(scalar_input, scalar_time));
					break;
				case MIMT_PerNote:
					per_note_points.push_back(p);
					// Per-note points are also put into the per-frame list so that
					// they will be set for the per-frame solving step.
				case MIMT_PerFrame:
					per_frame_points.push_back(p);
					break;
			}
		}
		if(per_frame_points.empty())
		{
			solve();
		}
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		vector<ModInput*> inputs= {&t_input};
		inputs.reserve(1 + points.size());
		for(auto&& p : points)
		{
			inputs.push_back(&p);
		}
		push_inputs_internal(L, table_index, inputs);
	}
	virtual size_t num_inputs() { return 1+points.size(); }

	void solve()
	{
		spline.solve(loop, polygonal);
	}
	void per_frame_solve(mod_val_inputs const& input)
	{
		if(per_frame_points.empty())
		{
			return;
		}
		mod_time_inputs time(m_start_beat, m_start_second,
			input.music_beat, input.music_second, m_end_beat, m_end_second);
		for(auto pindex : per_frame_points)
		{
			spline.set_point(pindex, points[pindex].pick(input, time));
		}
		solve();
	}
	void per_note_solve(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		if(per_note_points.empty())
		{
			return;
		}
		for(auto pindex : per_note_points)
		{
			spline.set_point(pindex, points[pindex].pick(input, time));
		}
		solve();
	}

private:
	CubicSpline spline;
	// All scalar inputs are sent to the spline on loading.  So the ones that
	// are not scalars are listed in per_note_points and per_frame_points so
	// those stages only send the ones they need to.
	// If all the input points are scalars, then they only need to be copied
	// into the spline once, and the spline only has to be solved once ever.
	vector<size_t> per_note_points;
	vector<size_t> per_frame_points;
};

// ModManager::update_splines has to be defined below all the different
// ModFunction types so that the compiler can see the full ModFunctionSpline
// declaration when processing it.
void ModManager::update_splines(double curr_beat, double curr_second)
{
	if(!m_active_splines.empty())
	{
		mod_val_inputs input(curr_beat, curr_second);
		for(auto&& spline : m_active_splines)
		{
			spline->per_frame_solve(input);
		}
	}
}

void ModManager::add_to_splines_if_its_a_spline(ModFunction* func)
{
	ModFunctionSpline* spline_func= dynamic_cast<ModFunctionSpline*>(func);
	if(spline_func != nullptr)
	{
		m_active_splines.insert(spline_func);
	}
}

void ModManager::remove_from_splines_if_its_a_spline(ModFunction* func)
{
	ModFunctionSpline* spline_func= dynamic_cast<ModFunctionSpline*>(func);
	if(spline_func != nullptr)
	{
		auto active_spline_entry= m_active_splines.find(spline_func);
		if(active_spline_entry != m_active_splines.end())
		{
			m_active_splines.erase(active_spline_entry);
		}
	}
}

void ModManager::add_spline(ModFunctionSpline* func)
{
	m_active_splines.insert(func);
}

static ModFunction* create_field_mod(ModifiableValue* parent, lua_State* L, int index)
{
	lua_rawgeti(L, index, 1);
	ModFunctionType type= Enum::Check<ModFunctionType>(L, -1);
	lua_pop(L, 1);
	ModFunction* ret= nullptr;
	switch(type)
	{
		case MFT_Constant:
			ret= new ModFunctionConstant(parent);
			break;
		case MFT_Product:
			ret= new ModFunctionProduct(parent);
			break;
		case MFT_Power:
			ret= new ModFunctionPower(parent);
			break;
		case MFT_Log:
			ret= new ModFunctionLog(parent);
			break;
		case MFT_Sine:
			ret= new ModFunctionSine(parent);
			break;
		case MFT_Tan:
			ret= new ModFunctionTan(parent);
			break;
		case MFT_Square:
			ret= new ModFunctionSquare(parent);
			break;
		case MFT_Triangle:
			ret= new ModFunctionTriangle(parent);
			break;
		case MFT_Spline:
			ret= new ModFunctionSpline(parent);
			break;
		default:
			return nullptr;
	}
	// FIXME: This leaks memory if there is an error in the lua.
	ret->load_from_lua(L, index);
	return ret;
}

ModifiableValue::~ModifiableValue()
{
	clear_mods();
	clear_managed_mods();
}

double ModifiableValue::evaluate(mod_val_inputs const& input)
{
	double sum= m_value;
	for(auto&& mod : m_mods)
	{
		sum+= mod.second->evaluate(input);
	}
	for(auto&& mod : m_active_managed_mods)
	{
		sum+= mod->evaluate_with_time(input);
	}
	return sum;
}

ModFunction* ModifiableValue::add_mod_internal(lua_State* L, int index)
{
	ModFunction* new_mod= create_field_mod(this, L, index);
	if(new_mod == nullptr)
	{
		LuaHelpers::ReportScriptError("Problem creating modifier: unknown type.");
	}
	return new_mod;
}

ModFunction* ModifiableValue::add_mod(lua_State* L, int index)
{
	ModFunction* new_mod= add_mod_internal(L, index);
	if(new_mod == nullptr)
	{
		return nullptr;
	}
	ModFunction* ret= nullptr;
	auto mod= m_mods.find(new_mod->get_name());
	if(mod == m_mods.end())
	{
		m_mods.insert(make_pair(new_mod->get_name(), new_mod));
		ret= new_mod;
	}
	else
	{
		(*(mod->second)) = (*new_mod);
		delete new_mod;
		ret= mod->second;
	}
	if(ret->needs_per_frame_solve())
	{
		ModFunctionSpline* spline= dynamic_cast<ModFunctionSpline*>(ret);
		if(spline != nullptr)
		{
			m_manager->add_spline(spline);
		}
	}
	return ret;
}

ModFunction* ModifiableValue::get_mod(std::string const& name)
{
	auto mod= m_mods.find(name);
	if(mod != m_mods.end())
	{
		return mod->second;
	}
	return nullptr;
}

void ModifiableValue::remove_mod(std::string const& name)
{
	auto mod= m_mods.find(name);
	if(mod != m_mods.end())
	{
		delete mod->second;
		m_mods.erase(mod);
	}
}

void ModifiableValue::clear_mods()
{
	for(auto&& mod : m_mods)
	{
		delete mod.second;
	}
	m_mods.clear();
}

ModFunction* ModifiableValue::add_managed_mod(lua_State* L, int index)
{
	ModFunction* new_mod= add_mod_internal(L, index);
	if(new_mod == nullptr)
	{
		return nullptr;
	}
	new_mod->calc_unprovided_times(m_timing);
	ModFunction* ret= nullptr;
	auto mod= m_managed_mods.find(new_mod->get_name());
	if(mod == m_managed_mods.end())
	{
		m_managed_mods.insert(make_pair(new_mod->get_name(), new_mod));
		ret= new_mod;
	}
	else
	{
		(*(mod->second)) = (*new_mod);
		delete new_mod;
		ret= mod->second;
	}
	m_manager->add_mod(ret, this);
	//m_manager->dump_list_status();
	return ret;
}

ModFunction* ModifiableValue::get_managed_mod(std::string const& name)
{
	auto mod= m_managed_mods.find(name);
	if(mod != m_managed_mods.end())
	{
		return mod->second;
	}
	return nullptr;
}

void ModifiableValue::remove_managed_mod(std::string const& name)
{
	auto mod= m_managed_mods.find(name);
	if(mod != m_managed_mods.end())
	{
		m_manager->remove_mod(mod->second);
		remove_mod_from_active_list(mod->second);
		delete mod->second;
		m_managed_mods.erase(mod);
	}
}

void ModifiableValue::clear_managed_mods()
{
	m_manager->remove_all_mods(this);
	for(auto&& mod : m_managed_mods)
	{
		delete mod.second;
	}
	m_managed_mods.clear();
}

void ModifiableValue::add_mod_to_active_list(ModFunction* mod)
{
	m_active_managed_mods.insert(mod);
}

void ModifiableValue::remove_mod_from_active_list(ModFunction* mod)
{
	auto iter= m_active_managed_mods.find(mod);
	if(iter != m_active_managed_mods.end())
	{
		m_active_managed_mods.erase(iter);
	}
}


// lua start
struct LunaModInput : Luna<ModInput>
{
	static int get_type(T* p, lua_State* L)
	{
		Enum::Push(L, p->m_type);
		return 1;
	}
	static int set_type(T* p, lua_State* L)
	{
		p->m_type= Enum::Check<ModInputType>(L, 1);
		COMMON_RETURN_SELF;
	}
	GET_SET_FLOAT_METHOD(scalar, m_scalar);
	GET_SET_FLOAT_METHOD(offset, m_offset);
	GET_SET_BOOL_METHOD(rep_enabled, m_rep_enabled);
	GET_SET_BOOL_METHOD(phases_enabled, m_phases_enabled);
	static int get_rep(T* p, lua_State* L)
	{
		push_numbers(L, {&p->m_rep_begin, &p->m_rep_end});
		return 1;
	}
	static int set_rep(T* p, lua_State* L)
	{
		if(!lua_istable(L, 1))
		{
			luaL_error(L, "Arg for ModInput:set_rep must be a table of two numbers.");
		}
		p->load_rep(L, 1);
		COMMON_RETURN_SELF;
	}
	static int get_all_phases(T* p, lua_State* L)
	{
		lua_createtable(L, p->m_phases.size(), 0);
		for(size_t i= 0; i < p->m_phases.size(); ++i)
		{
			p->push_phase(L, i);
			lua_rawseti(L, -2, i+1);
		}
		return 1;
	}
	static int get_phase(T* p, lua_State* L)
	{
		size_t phase= static_cast<size_t>(IArg(1)) - 1;
		if(phase >= p->m_phases.size())
		{
			lua_pushnil(L);
		}
		else
		{
			p->push_phase(L, phase);
		}
		return 1;
	}
	static int get_default_phase(T* p, lua_State* L)
	{
		p->push_def_phase(L);
		return 1;
	}
	static int get_num_phases(T* p, lua_State* L)
	{
		lua_pushnumber(L, p->m_phases.size());
		return 1;
	}
	static int set_all_phases(T* p, lua_State* L)
	{
		p->load_phases(L, 1);
		COMMON_RETURN_SELF;
	}
	static int set_phase(T* p, lua_State* L)
	{
		size_t phase= static_cast<size_t>(IArg(1)) - 1;
		if(phase >= p->m_phases.size() || !lua_istable(L, 2))
		{
			luaL_error(L, "Args to ModInput:set_phase must be an index and a table.");
		}
		p->load_one_phase(L, 2, phase);
		COMMON_RETURN_SELF;
	}
	static int set_default_phase(T* p, lua_State* L)
	{
		p->load_def_phase(L, 1);
		COMMON_RETURN_SELF;
	}
	static int remove_phase(T* p, lua_State* L)
	{
		size_t phase= static_cast<size_t>(IArg(1)) - 1;
		if(phase < p->m_phases.size())
		{
			p->m_phases.erase(p->m_phases.begin() + phase);
		}
		COMMON_RETURN_SELF;
	}
	static int clear_phases(T* p, lua_State* L)
	{
		(void)L;
		p->m_phases.clear();
		p->m_phases_enabled= false;
		COMMON_RETURN_SELF;
	}
	LunaModInput()
	{
		ADD_GET_SET_METHODS(type);
		ADD_GET_SET_METHODS(rep_enabled);
		ADD_GET_SET_METHODS(phases_enabled);
		ADD_GET_SET_METHODS(scalar);
		ADD_GET_SET_METHODS(offset);
		ADD_GET_SET_METHODS(rep);
		ADD_GET_SET_METHODS(all_phases);
		ADD_GET_SET_METHODS(phase);
		ADD_GET_SET_METHODS(default_phase);
		ADD_METHOD(get_num_phases);
		ADD_METHOD(remove_phase);
		ADD_METHOD(clear_phases);
	}
};
LUA_REGISTER_CLASS(ModInput);

struct LunaModFunction : Luna<ModFunction>
{
	static int get_inputs(T* p, lua_State* L)
	{
		lua_createtable(L, p->num_inputs(), 0);
		p->push_inputs(L, lua_gettop(L));
		return 1;
	}
	LunaModFunction()
	{
		ADD_METHOD(get_inputs);
	}
};
LUA_REGISTER_CLASS(ModFunction);

struct LunaModifiableValue : Luna<ModifiableValue>
{
	static int add_mod(T* p, lua_State* L)
	{
		p->add_mod(L, lua_gettop(L));
		COMMON_RETURN_SELF;
	}
	static int add_get_mod(T* p, lua_State* L)
	{
		ModFunction* mod= p->add_mod(L, lua_gettop(L));
		if(mod == nullptr)
		{
			lua_pushnil(L);
		}
		else
		{
			mod->PushSelf(L);
		}
		return 1;
	}
	static int get_mod(T* p, lua_State* L)
	{
		ModFunction* mod= p->get_mod(SArg(1));
		if(mod == nullptr)
		{
			lua_pushnil(L);
		}
		else
		{
			mod->PushSelf(L);
		}
		return 1;
	}
	static int remove_mod(T* p, lua_State* L)
	{
		p->remove_mod(SArg(1));
		COMMON_RETURN_SELF;
	}
	static int clear_mods(T* p, lua_State* L)
	{
		p->clear_mods();
		COMMON_RETURN_SELF;
	}
	static int add_managed_mod(T* p, lua_State* L)
	{
		p->add_managed_mod(L, lua_gettop(L));
		COMMON_RETURN_SELF;
	}
	static int add_managed_mod_set(T* p, lua_State* L)
	{
		if(!lua_istable(L, 1))
		{
			luaL_error(L, "Arg for add_managed_mod_set must be a table of ModFunctins.");
		}
		size_t num_mods= lua_objlen(L, 1);
		for(size_t m= 0; m < num_mods; ++m)
		{
			lua_rawgeti(L, 1, m+1);
			p->add_managed_mod(L, lua_gettop(L));
			lua_pop(L, 1);
		}
		COMMON_RETURN_SELF;
	}
	static int add_get_managed_mod(T* p, lua_State* L)
	{
		ModFunction* mod= p->add_managed_mod(L, lua_gettop(L));
		if(mod == nullptr)
		{
			lua_pushnil(L);
		}
		else
		{
			mod->PushSelf(L);
		}
		return 1;
	}
	static int get_managed_mod(T* p, lua_State* L)
	{
		ModFunction* mod= p->get_managed_mod(SArg(1));
		if(mod == nullptr)
		{
			lua_pushnil(L);
		}
		else
		{
			mod->PushSelf(L);
		}
		return 1;
	}
	static int remove_managed_mod(T* p, lua_State* L)
	{
		p->remove_managed_mod(SArg(1));
		COMMON_RETURN_SELF;
	}
	static int clear_managed_mods(T* p, lua_State* L)
	{
		p->clear_managed_mods();
		COMMON_RETURN_SELF;
	}
	static int get_value(T* p, lua_State* L)
	{
		lua_pushnumber(L, p->m_value);
		return 1;
	}
	static int set_value(T* p, lua_State* L)
	{
		p->m_value= FArg(1);
		COMMON_RETURN_SELF;
	}
	LunaModifiableValue()
	{
		ADD_METHOD(add_mod);
		ADD_METHOD(add_get_mod);
		ADD_METHOD(get_mod);
		ADD_METHOD(remove_mod);
		ADD_METHOD(clear_mods);
		ADD_METHOD(add_managed_mod);
		ADD_METHOD(add_managed_mod_set);
		ADD_METHOD(add_get_managed_mod);
		ADD_METHOD(get_managed_mod);
		ADD_METHOD(remove_managed_mod);
		ADD_METHOD(clear_managed_mods);
		ADD_GET_SET_METHODS(value);
	}
};
LUA_REGISTER_CLASS(ModifiableValue);
