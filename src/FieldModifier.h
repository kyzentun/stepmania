#ifndef FIELD_MODIFIER_H
#define FIELD_MODIFIER_H

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
		:type(MIT_Scalar), scalar(0.0)
	{}
	ModInputType type;
	double scalar;
};

struct FieldModifier
{
	FieldModifier() {}
	FieldModifier(ModManager* man, std::vector<mod_input_info>& params)
	{
		set_manager(man);
		set_from_params(params);
	}
	virtual ~FieldModifier() {}
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
	virtual void push_inputs(lua_State* L, int table_index)
	{
		UNUSED(L);
		UNUSED(table_index);
	}
	virtual size_t num_inputs() { return 0; }
	virtual void PushSelf(lua_State* L);
};

enum FieldModifierType
{
	FMT_Constant,
	FMT_Sine,
	FMT_Square,
	FMT_Triangle,
	FMT_SawSine,
	FMT_SawSquare,
	FMT_SawTriangle,
	NUM_FieldModifierType,
	FieldModifierType_Invalid
};
const RString& FieldModifierTypeToString(FieldModifierType fmt);
LuaDeclareType(FieldModifierType);

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
	void add_mod(FieldModifierType type, std::vector<mod_input_info>& params);
	FieldModifier* get_mod(size_t index);
	size_t num_mods() { return m_mods.size(); }
	void remove_mod(size_t index);
	void clear_mods();

	ApproachingValue& get_value() { return m_value; }

	virtual void PushSelf(lua_State* L);
private:
	ModManager* m_manager;
	ApproachingValue m_value;
	std::vector<FieldModifier*> m_mods;
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

#endif
