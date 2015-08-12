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

struct mod_input_info
{
	mod_input_info()
		:type(MIT_Scalar), scalar(0.0), offset(0.0)
	{}
	ModInputType type;
	double scalar;
	double offset;
};

struct mod_input_picker
{
	ModInputType type;
	ApproachingValue scalar;
	ApproachingValue offset;
	mod_input_picker()
		:type(MIT_Scalar), scalar(0.0), offset(0.0)
	{}
	void set_from_info(mod_input_info& info)
	{
		type= info.type;
		scalar.set_value_instant(info.scalar);
		offset.set_value_instant(info.offset);
	}
	double pick(mod_val_inputs const& input)
	{
		double ret= 1.0;
		switch(type)
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
		return (ret * scalar.get_value()) + offset.get_value();
	}
	void set_manager(ModManager* man)
	{
		scalar.set_manager(man);
		offset.set_manager(man);
	}
	void push(lua_State* L, bool with_offset)
	{
		if(with_offset)
		{
			lua_createtable(L, 2, 0);
			scalar.PushSelf(L);
			lua_rawseti(L, -2, 1);
			offset.PushSelf(L);
			lua_rawseti(L, -2, 2);
		}
		else
		{
			scalar.PushSelf(L);
		}
	}
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
	ModFunction(ModManager* man, std::vector<mod_input_info>& params)
	{
		set_manager(man);
		set_from_params(params);
	}
	virtual ~ModFunction() {}
	virtual void update(double delta) { UNUSED(delta); }
	virtual double evaluate(mod_val_inputs const& input)
	{
		UNUSED(input);
		return 0.0;
	}
	virtual void set_manager(ModManager* man) { UNUSED(man); }
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		UNUSED(params);
	}
	virtual void push_inputs(lua_State* L, int table_index, bool with_offsets)
	{
		UNUSED(L);
		UNUSED(table_index);
		UNUSED(with_offsets);
	}
	void push_saw(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets,
			{&m_saw_begin, &m_saw_end});
	}
	void push_gap(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets,
			{&m_gap_begin, &m_gap_end, &m_gap_value});
	}
	virtual size_t num_inputs() { return 0; }
	virtual void PushSelf(lua_State* L);

	bool m_saw_enabled;
	bool m_gap_enabled;
protected:
	void push_inputs_internal(lua_State* L, int table_index, bool with_offsets,
		std::initializer_list<mod_input_picker*> inputs)
	{
		size_t i= 1;
		for(auto&& input : inputs)
		{
			input->push(L, with_offsets);
			lua_rawseti(L, table_index, i);
			++i;
		}
	}
	double apply_saw(double result, mod_val_inputs const& input)
	{
		if(!m_saw_enabled)
		{
			return result;
		}
		double const saw_begin= m_saw_begin.pick(input);
		double const saw_end= m_saw_end.pick(input);
		double const dist= saw_end - saw_begin;
		double const mod_res= fmod(result, dist);
		if(mod_res < 0.0)
		{
			return mod_res + dist + saw_begin;
		}
		return mod_res + saw_begin;
	}
	double apply_gap(double result, mod_val_inputs const& input)
	{
		if(!m_gap_enabled)
		{
			return result;
		}
		double const gap_begin= m_gap_begin.pick(input);
		if(result < gap_begin)
		{
			return result;
		}
		double const gap_end= m_gap_end.pick(input);
		double const dist= gap_end - gap_begin;
		if(result >= gap_end)
		{
			return result - dist;
		}
		return m_gap_value.pick(input);
	}

	mod_input_picker m_saw_begin;
	mod_input_picker m_saw_end;
	mod_input_picker m_gap_begin;
	mod_input_picker m_gap_end;
	mod_input_picker m_gap_value;
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
	void add_mod(ModFunctionType type, std::vector<mod_input_info>& params);
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
