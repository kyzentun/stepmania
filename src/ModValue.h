#ifndef MOD_VALUE_H
#define MOD_VALUE_H

#include <initializer_list>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "RageTypes.h"

struct lua_State;
struct ModManager;

struct ApproachingValue
{
	ApproachingValue()
		:parent(nullptr), value(0.0), speed(0.0), goal(0.0)
	{}
	ApproachingValue(double v)
		:parent(nullptr), value(v), speed(0.0), goal(v)
	{}
	ApproachingValue(ModManager* man, double v)
		:parent(man), value(v), speed(0.0), goal(v)
	{}
	~ApproachingValue()
	{
		remove_from_update_list();
	}
	void update(double delta)
	{
		// not using rage's fapproach because it asserts on negative speed, and
		// I want that to be possible.
		if(value == goal)
		{
			remove_from_update_list();
			return;
		}
		double const dist= goal - value;
		double const sign= dist / fabsf(dist);
		double change= sign * speed * delta;
		if(fabsf(change) > fabsf(dist))
		{
			value= goal;
			remove_from_update_list();
			return;
		}
		value+= change;
	}
	void add_to_update_list();
	void remove_from_update_list();
#define GENERIC_GET_SET(member) \
	double get_##member() { return member; } \
	void set_##member(double v) { add_to_update_list(); member= v; }

	GENERIC_GET_SET(value);
	GENERIC_GET_SET(speed);
	GENERIC_GET_SET(goal);
#undef GENERIC_GET_SET
	void set_value_instant(double v)
	{
		value= goal= v;
		remove_from_update_list();
	}

	void set_manager(ModManager* man)
	{
		parent= man;
	}

	virtual void PushSelf(lua_State* L);

private:
	ModManager* parent;
	double value;
	double speed;
	double goal;
};

struct ModManager
{
	void update(double delta)
	{
		for(auto&& mod : m_mods_to_update)
		{
			mod->update(delta);
		}
		for(auto&& mod : m_delayed_remove_update_list)
		{
			auto entry= m_mods_to_update.find(mod);
			if(entry != m_mods_to_update.end())
			{
				m_mods_to_update.erase(entry);
			}
		}
		m_delayed_remove_update_list.clear();
	}
	void add_to_list(ApproachingValue* mod)
	{
		m_mods_to_update.insert(mod);
	}
	void remove_from_list(ApproachingValue* mod)
	{
		m_delayed_remove_update_list.push_back(mod);
	}
private:
	std::unordered_set<ApproachingValue*> m_mods_to_update;
	std::vector<ApproachingValue*> m_delayed_remove_update_list;
};

struct mod_val_inputs
{
	double const eval_beat;
	double const eval_second;
	double const music_beat;
	double const music_second;
	mod_val_inputs(double const mb, double const ms)
		:eval_beat(mb), eval_second(ms), music_beat(mb), music_second(ms)
	{}
	mod_val_inputs(double const eb, double const es, double const mb, double const ms)
		:eval_beat(eb), eval_second(es), music_beat(mb), music_second(ms)
	{}
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
	NUM_ModInputType,
	ModInputType_Invalid
};
const RString& ModInputTypeToString(ModInputType fmt);
LuaDeclareType(ModInputType);

struct ModInput
{
	ModInputType m_type;
	ApproachingValue m_scalar;
	ApproachingValue m_offset;

	// The input value can be passed through a couple of modifiers to change
	// its range.  These modifiers are applied before the scalar and offset.
	// So it works like this:
	//   result= apply_rep_mod(input)
	//   result= apply_unce_mod(result)
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
	ApproachingValue m_rep_begin;
	ApproachingValue m_rep_end;
	// The unce modifier. (I could not think of a name.  Send suggestions)
	// If input is less than unce_begin, it returns unce_before.
	// If input is equal to or greater than unce_begin and less than unce_end,
	//   it returns (input - unce_begin) * unce_during.
	// If input is equal to or greater than unce_end, it returns unce_after.
	// Example:
	//   unce_before is 0.
	//   unce_begin is 1.
	//   unce_during is 2.
	//   unce_end is 2.
	//   unce_after is 3.
	//     input is .9, result is 0 (unce_before)
	//     input is 1.1, result is .2 (1.1 minus 1 is .1, .1 times 2 is .2)
	//     input is 1.9, result is 1.8
	//     input is 2, result is 3.
	bool m_unce_enabled;
	ApproachingValue m_unce_before;
	ApproachingValue m_unce_begin;
	ApproachingValue m_unce_during;
	ApproachingValue m_unce_end;
	ApproachingValue m_unce_after;

	ModInput()
		:m_type(MIT_Scalar), m_scalar(0.0), m_offset(0.0), m_rep_enabled(false),
		m_rep_begin(0.0), m_rep_end(0.0), m_unce_enabled(false),
		m_unce_before(0.0), m_unce_begin(0.0), m_unce_during(0.0),
		m_unce_end(0.0), m_unce_after(0.0)
	{}
	void clear();
	void load_from_lua(lua_State* L, int index);
	double apply_rep(double input)
	{
		if(!m_rep_enabled)
		{
			return input;
		}
		double const rep_begin= m_rep_begin.get_value();
		double const rep_end= m_rep_end.get_value();
		double const dist= rep_end - rep_begin;
		double const mod_res= fmod(input, dist);
		if(mod_res < 0.0)
		{
			return mod_res + dist + rep_begin;
		}
		return mod_res + rep_begin;
	}
	double apply_unce(double input)
	{
		if(!m_unce_enabled)
		{
			return input;
		}
		double const unce_begin= m_unce_begin.get_value();
		if(input < unce_begin)
		{
			return m_unce_before.get_value();
		}
		if(input < m_unce_end.get_value())
		{
			return (input - unce_begin) * m_unce_during.get_value();
		}
		return m_unce_after.get_value();
	}
	double pick(mod_val_inputs const& input)
	{
		double ret= 1.0;
		switch(m_type)
		{
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
			case MIT_Scalar:
			default:
				break;
		}
		ret= apply_unce(apply_rep(ret));
		return (ret * m_scalar.get_value()) + m_offset.get_value();
	}
	void set_manager(ModManager* man)
	{
		m_scalar.set_manager(man);
		m_offset.set_manager(man);
		m_rep_begin.set_manager(man);
		m_rep_end.set_manager(man);
		m_unce_before.set_manager(man);
		m_unce_begin.set_manager(man);
		m_unce_during.set_manager(man);
		m_unce_end.set_manager(man);
		m_unce_after.set_manager(man);
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
	MFT_Square,
	MFT_Triangle,
	NUM_ModFunctionType,
	ModFunctionType_Invalid
};
const RString& ModFunctionTypeToString(ModFunctionType fmt);
LuaDeclareType(ModFunctionType);

struct ModFunction
{
	ModFunction() {}
	ModFunction(ModManager* man)
	{
		set_manager(man);
	}
	virtual ~ModFunction() {}
	virtual void update(double delta) { UNUSED(delta); }
	virtual double evaluate(mod_val_inputs const& input)
	{
		UNUSED(input);
		return 0.0;
	}
	virtual void set_manager(ModManager* man) { UNUSED(man); }
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

protected:
	void push_inputs_internal(lua_State* L, int table_index,
		std::initializer_list<ModInput*> inputs)
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
		std::vector<ModInput*> inputs)
	{
		// The lua table looks like this:
		// {type, input, ...}
		// The ... is for the inputs after the first.
		// So the first input is at lua table index 2.
		size_t elements= lua_objlen(L, index);
		size_t limit= std::min(elements, inputs.size()+2);
		for(size_t el= 2; el <= limit; ++el)
		{
			lua_rawgeti(L, index, el);
			inputs[el-2]->load_from_lua(L, lua_gettop(L));
			lua_pop(L, 1);
		}
	}
};

struct ModifiableValue
{
	ModifiableValue()
		:m_manager(nullptr)
	{}
	ModifiableValue(ModManager* man, double value)
		:m_manager(man), m_value(man, value)
	{}
	~ModifiableValue();
	void set_manager(ModManager* man);
	double evaluate(mod_val_inputs const& input);
	void add_mod(lua_State* L, int index);
	ModFunction* get_mod(size_t index);
	size_t num_mods() { return m_mods.size(); }
	void remove_mod(size_t index);
	void clear_mods();

	ApproachingValue& get_value() { return m_value; }

	virtual void PushSelf(lua_State* L);
private:
	ModManager* m_manager;
	ApproachingValue m_value;
	std::vector<ModFunction*> m_mods;
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
	ModifiableValue x_mod;
	ModifiableValue y_mod;
	ModifiableValue z_mod;
};

struct ModifiableTransform
{
	ModifiableTransform(ModManager* man)
		:pos_mod(man, 0.0), rot_mod(man, 0.0), zoom_mod(man, 1.0)
	{}
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
