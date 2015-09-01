#ifndef MOD_VALUE_H
#define MOD_VALUE_H

#include <initializer_list>
#include <list>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "CubicSpline.h"
#include "RageTypes.h"
#include "TimingData.h"

struct lua_State;
struct ModFunction;
struct ModifiableValue;

// invalid_modfunction_time exists so that the loading code can tell when a
// start or end time was provided.
static const double invalid_modfunction_time= -1000.0;

// The forward declaration for ModFunctionSpline exists because ModManager
// takes care of updating splines that need to be solved every frame.  No
// other part of the engine should care about them.
struct ModFunctionSpline;

struct ModManager
{
	size_t column;
	struct func_and_parent
	{
		ModFunction* func;
		ModifiableValue* parent;
		func_and_parent() {}
		func_and_parent(ModFunction* f, ModifiableValue* p)
			:func(f), parent(p)
		{}
	};
	ModManager()
		:m_prev_curr_second(invalid_modfunction_time)
	{}
	void update(double curr_beat, double curr_second);
	void update_splines(double curr_beat, double curr_second);
	void add_mod(ModFunction* func, ModifiableValue* parent);
	void remove_mod(ModFunction* func);
	void remove_all_mods(ModifiableValue* parent);

	void dump_list_status();

	void add_to_splines_if_its_a_spline(ModFunction* func);
	void remove_from_splines_if_its_a_spline(ModFunction* func);
	void add_spline(ModFunctionSpline* func);

private:
	void insert_into_past(ModFunction* func, ModifiableValue* parent);
	void insert_into_present(ModFunction* func, ModifiableValue* parent);
	void insert_into_future(ModFunction* func, ModifiableValue* parent);
#define INSERT_FAP(time_name) \
	void insert_into_##time_name(func_and_parent& fap) \
	{ insert_into_##time_name(fap.func, fap.parent); }
	INSERT_FAP(past);
	INSERT_FAP(present);
	INSERT_FAP(future);
#undef INSERT_FAP
	void remove_from_present(std::list<func_and_parent>::iterator fapi);

	double m_prev_curr_second;
	std::list<func_and_parent> m_past_funcs;
	std::list<func_and_parent> m_present_funcs;
	std::list<func_and_parent> m_future_funcs;

	std::unordered_set<ModFunctionSpline*> m_active_splines;
};

struct mod_val_inputs
{
	double const eval_beat;
	double const eval_second;
	double const music_beat;
	double const music_second;
	double const y_offset;
	mod_val_inputs(double const mb, double const ms)
		:eval_beat(mb), eval_second(ms), music_beat(mb), music_second(ms), y_offset(0.0)
	{}
	mod_val_inputs(double const eb, double const es, double const mb, double const ms)
		:eval_beat(eb), eval_second(es), music_beat(mb), music_second(ms), y_offset(0.0)
	{}
	mod_val_inputs(double const eb, double const es, double const mb, double const ms, double const yoff)
		:eval_beat(eb), eval_second(es), music_beat(mb), music_second(ms), y_offset(yoff)
	{}
};

struct mod_time_inputs
{
	double const start_dist_beat;
	double const start_dist_second;
	double const end_dist_beat;
	double const end_dist_second;
	// Use this constructor to set all to zero.
	mod_time_inputs(double zero)
		:start_dist_beat(zero), start_dist_second(zero),
		end_dist_beat(zero), end_dist_second(zero)
	{}
#define PART_DIFF(result, after, before, type) \
		result##_dist_##type(after##_##type - before##_##type)
#define BS_DIFF(result, after, before) \
		PART_DIFF(result, after, before, beat), \
		PART_DIFF(result, after, before, second)
	mod_time_inputs(double start_beat, double start_second, double curr_beat,
		double curr_second, double end_beat, double end_second)
		: BS_DIFF(start, curr, start), BS_DIFF(end, end, curr)
	{}
#undef BS_DIFF
#undef PART_DIFF
};

enum ModInputType
{
	MIT_Scalar,
	MIT_EvalBeat,
	MIT_EvalSecond,
	MIT_MusicBeat,
	MIT_MusicSecond,
	MIT_DistBeat,
	MIT_DistSecond,
	MIT_YOffset,
	MIT_StartDistBeat,
	MIT_StartDistSecond,
	MIT_EndDistBeat,
	MIT_EndDistSecond,
	NUM_ModInputType,
	ModInputType_Invalid
};
const RString& ModInputTypeToString(ModInputType fmt);
LuaDeclareType(ModInputType);

enum ModInputMetaType
{
	MIMT_Scalar,
	MIMT_PerFrame,
	MIMT_PerNote
};

struct ModInput
{
	ModInputType m_type;
	double m_scalar;
	double m_offset;

	// The input value can be passed through a couple of modifiers to change
	// its range.  These modifiers are applied before the scalar and offset.
	// So it works like this:
	//   result= apply_rep_mod(input)
	//   result= apply_phase_mod(result)
	//   return (result * scalar) + offset
	// These input modifiers are necessary for mods like beat and hidden,

	// The rep modifier makes a sub-range repeat.  rep_begin is the beginning
	// of the range, rep_end is the end.  The result of the rep modifier will
	// never equal rep_end.
	// Example:
	//   rep_begin is 1.
	//   rep_end is 2.
	//     input is 2, result is 1.
	//     input is .25, result is 1.25.
	//     input is -.25, result is 1.75.
	bool m_rep_enabled;
	double m_rep_begin;
	double m_rep_end;
	// The phase modifier applies a multiplier and an offset in its range.
	// Equation: result= ((input - phase_start) * multiplier) + offset
	// The range includes the beginning, but not the end.
	// Input outside its range is not modified.
	// A ModInput can have multiple phases to simplify creating mods that need
	// multiple phases. (say, when beat ramps up the amplitude on a sine wave,
	// then ramps it down.  Between beats is one phase, ramp up is another, and
	// ramp down is a third.)
	// If two phases overlap, the one that is used is undefined.
	// Example:
	//   phase start is .5.
	//   phase finish is 1.
	//   phase mult is 2.
	//   phase offset is .5.
	//     input is .4, result is .4 (outside the phase).
	//     input is .5, result is .5 (.5 - .5 is 0, 0 * 2 is 0, 0 + .5 is .5)
	//     input is .6, result is .7 (.6 - .5 is .1, .1 * 2 is .2, .2 + .5 is .7)
	//     input is 1, result is 1 (outside the phase).
	bool m_phases_enabled;
	struct phase
	{
		phase()
			:start(0.0), finish(0.0), mult(1.0), offset(0.0)
		{}
		double start;
		double finish;
		double mult;
		double offset;
	};
	std::vector<phase> m_phases;
	phase m_default_phase;

	// Special stuff for ModInputType_Spline.
	CubicSpline m_spline;
	bool m_loop_spline;
	bool m_polygonal_spline;

	ModInput()
		:m_type(MIT_Scalar), m_scalar(0.0), m_offset(0.0),
		m_rep_enabled(false), m_rep_begin(0.0), m_rep_end(0.0),
		m_phases_enabled(false), m_loop_spline(false), m_polygonal_spline(false)
	{}
	ModInputMetaType get_meta_type()
	{
		switch(m_type)
		{
			case MIT_Scalar:
				return MIMT_Scalar;
			case MIT_MusicBeat:
			case MIT_MusicSecond:
			case MIT_StartDistBeat:
			case MIT_StartDistSecond:
			case MIT_EndDistBeat:
			case MIT_EndDistSecond:
				return MIMT_PerFrame;
			case MIT_EvalBeat:
			case MIT_EvalSecond:
			case MIT_DistBeat:
			case MIT_DistSecond:
			case MIT_YOffset:
				return MIMT_PerNote;
			default:
				break;
		}
		return MIMT_Scalar;
	}
	void clear();
	void push_phase(lua_State* L, size_t phase);
	void push_def_phase(lua_State* L);
	void load_rep(lua_State* L, int index);
	void load_one_phase(lua_State* L, int index, size_t phase);
	void load_def_phase(lua_State* L, int index);
	void load_phases(lua_State* L, int index);
	void load_from_lua(lua_State* L, int index);
	phase const* find_phase(double input);
	double apply_rep(double input)
	{
		if(!m_rep_enabled)
		{
			return input;
		}
		double const dist= m_rep_end - m_rep_begin;
		double const mod_res= fmod(input, dist);
		if(mod_res < 0.0)
		{
			return mod_res + dist + m_rep_begin;
		}
		return mod_res + m_rep_begin;
	}
	double apply_phase(double input)
	{
		if(!m_phases_enabled)
		{
			return input;
		}
		phase const* curr= find_phase(input);
		return ((input - curr->start) * curr->mult) + curr->offset;
	}
	double apply_spline(double input)
	{
		if(m_spline.empty())
		{
			return input;
		}
		return m_spline.evaluate(input, m_loop_spline);
	}
	double pick(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		double ret= 1.0;
		switch(m_type)
		{
			case MIT_Scalar:
				ret= 1.0;
				break;
			case MIT_EvalBeat:
				ret= input.eval_beat;
				break;
			case MIT_EvalSecond:
				ret= input.eval_second;
				break;
			case MIT_MusicBeat:
				ret= input.music_beat;
				break;
			case MIT_MusicSecond:
				ret= input.music_second;
				break;
			case MIT_DistBeat:
				ret= input.eval_beat - input.music_beat;
				break;
			case MIT_DistSecond:
				ret= input.eval_second - input.music_second;
				break;
			case MIT_YOffset:
				ret= input.y_offset;
				break;
			case MIT_StartDistBeat:
				ret= time.start_dist_beat;
				break;
			case MIT_StartDistSecond:
				ret= time.start_dist_second;
				break;
			case MIT_EndDistBeat:
				ret= time.end_dist_beat;
				break;
			case MIT_EndDistSecond:
				ret= time.end_dist_second;
				break;
			default:
				break;
		}
		ret= apply_phase(apply_rep(ret));
		ret= apply_spline(ret * m_scalar);
		return ret + m_offset;
	}
	virtual void PushSelf(lua_State* L);
};

enum ModFunctionType
{
	MFT_Constant,
	MFT_Product,
	MFT_Power,
	MFT_Log,
	MFT_Sine,
	MFT_Tan,
	MFT_Square,
	MFT_Triangle,
	MFT_Spline,
	NUM_ModFunctionType,
	ModFunctionType_Invalid
};
const RString& ModFunctionTypeToString(ModFunctionType fmt);
LuaDeclareType(ModFunctionType);

struct ModFunction
{
	ModFunction(ModifiableValue* parent)
		:m_start_beat(invalid_modfunction_time),
		m_start_second(invalid_modfunction_time),
		m_end_beat(invalid_modfunction_time),
		m_end_second(invalid_modfunction_time),
		m_parent(parent)
	{}
	virtual ~ModFunction() {}

	void calc_unprovided_times(TimingData const* timing);

	std::string const& get_name() { return m_name; }
	// needs_per_frame_solve exists so that ModifiableValue can check after
	// creating a ModFunction to see if it needs to be added to the manager for
	// solving every frame.
	virtual bool needs_per_frame_solve() { return false; }

	virtual void update(double delta) { UNUSED(delta); }
	double evaluate(mod_val_inputs const& input)
	{
		return sub_evaluate(input, mod_time_inputs(0.0));
	}
	double evaluate_with_time(mod_val_inputs const& input)
	{
		return sub_evaluate(input, mod_time_inputs(m_start_beat, m_start_second,
				input.music_beat, input.music_second, m_end_beat, m_end_second));
	}
	virtual double sub_evaluate(mod_val_inputs const& input, mod_time_inputs const& time)
	{
		UNUSED(input);
		UNUSED(time);
		return 0.0;
	}
	virtual void load_from_lua(lua_State* L, int index)
	{
		UNUSED(L);
		UNUSED(index);
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		UNUSED(L);
		UNUSED(table_index);
	}
	virtual size_t num_inputs() { return 0; }
	virtual void PushSelf(lua_State* L);

	double m_start_beat;
	double m_start_second;
	double m_end_beat;
	double m_end_second;

protected:
	void push_inputs_internal(lua_State* L, int table_index,
		std::vector<ModInput*> inputs)
	{
		size_t i= 1;
		for(auto&& input : inputs)
		{
			input->PushSelf(L);
			lua_rawseti(L, table_index, i);
			++i;
		}
	}
	void load_inputs_from_lua(lua_State* L, int index,
		std::vector<ModInput*> inputs);

	std::string m_name;
	ModifiableValue* m_parent;
};

struct ModifiableValue
{
	ModifiableValue(ModManager* man, double value)
		:m_value(value), m_manager(man), m_timing(nullptr)
	{}
	~ModifiableValue();
	void set_timing(TimingData const* timing)
	{
		m_timing= timing;
	}
	double evaluate(mod_val_inputs const& input);
	ModFunction* add_mod(lua_State* L, int index);
	ModFunction* get_mod(std::string const& name);
	void remove_mod(std::string const& name);
	void clear_mods();

	ModFunction* add_managed_mod(lua_State* L, int index);
	ModFunction* get_managed_mod(std::string const& name);
	void remove_managed_mod(std::string const& name);
	void clear_managed_mods();
	void add_mod_to_active_list(ModFunction* mod);
	void remove_mod_from_active_list(ModFunction* mod);

	virtual void PushSelf(lua_State* L);

	double m_value;
private:
	ModFunction* add_mod_internal(lua_State* L, int index);

	ModManager* m_manager;
	TimingData const* m_timing;
	std::unordered_map<std::string, ModFunction*> m_mods;
	std::unordered_map<std::string, ModFunction*> m_managed_mods;
	std::unordered_set<ModFunction*> m_active_managed_mods;
};

struct ModifiableVector3
{
	ModifiableVector3(ModManager* man, double value)
		:x_mod(man, value), y_mod(man, value), z_mod(man, value)
	{}
	void evaluate(mod_val_inputs const& input, RageVector3& out)
	{
		out.x= x_mod.evaluate(input);
		out.y= y_mod.evaluate(input);
		out.z= z_mod.evaluate(input);
	}
	void set_timing(TimingData const* timing)
	{
		x_mod.set_timing(timing);
		y_mod.set_timing(timing);
		z_mod.set_timing(timing);
	}
	ModifiableValue x_mod;
	ModifiableValue y_mod;
	ModifiableValue z_mod;
};

struct ModifiableTransform
{
	ModifiableTransform(ModManager* man)
		:pos_mod(man, 0.0), rot_mod(man, 0.0), zoom_mod(man, 1.0)
	{}
	void set_timing(TimingData const* timing)
	{
		pos_mod.set_timing(timing);
		rot_mod.set_timing(timing);
		zoom_mod.set_timing(timing);
	}
	void evaluate(mod_val_inputs const& input, transform& out)
	{
		pos_mod.evaluate(input, out.pos);
		rot_mod.evaluate(input, out.rot);
		zoom_mod.evaluate(input, out.zoom);
	}
	ModifiableVector3 pos_mod;
	ModifiableVector3 rot_mod;
	ModifiableVector3 zoom_mod;
};

#endif
