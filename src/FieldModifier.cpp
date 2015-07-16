#include "global.h"
#include "EnumHelper.h"
#include "FieldModifier.h"
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
};
XToString(ModInputType);
LuaXType(ModInputType);

static const char* FieldModifierTypeNames[] = {
	"Constant",
	"Sine",
};
XToString(FieldModifierType);
LuaXType(FieldModifierType);

void ApproachingValue::add_to_update_list()
{
	if(parent != nullptr)
	{
		parent->add_to_list(this);
	}
}
void ApproachingValue::remove_from_update_list()
{
	if(parent != nullptr)
	{
		parent->remove_from_list(this);
	}
}


struct mod_input_picker
{
	ModInputType type;
	ApproachingValue scalar;
	mod_input_picker()
		:type(MIT_Scalar), scalar(0.0)
	{}
	void set_from_info(mod_input_info& info)
	{
		type= info.type;
		scalar.set_value_instant(info.scalar);
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
		return ret * scalar.get_value();
	}
	void set_manager(ModManager* man)
	{
		scalar.set_manager(man);
	}
};

struct FieldModifierConstant : FieldModifier
{
	FieldModifierConstant(ModManager* man, std::vector<mod_input_info>& params)
	{
		set_manager(man);
		set_from_params(params);
	}
	mod_input_picker value;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return value.pick(input);
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
	}
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		if(params.size() > 0)
		{
			value.set_from_info(params[0]);
		}
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		value.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 1);
	}
	virtual size_t num_inputs() { return 1; }
};

struct FieldModifierSine : FieldModifier
{
	FieldModifierSine(ModManager* man, std::vector<mod_input_info>& params)
	{
		set_manager(man);
		set_from_params(params);
	}
	mod_input_picker frequency;
	mod_input_picker phase;
	mod_input_picker amplitude;
	mod_input_picker offset;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return (RageFastSin(frequency.pick(input) + phase.pick(input)) *
			amplitude.pick(input)) + offset.pick(input);
	}
	virtual void set_manager(ModManager* man)
	{
		frequency.set_manager(man);
		phase.set_manager(man);
		amplitude.set_manager(man);
		offset.set_manager(man);
	}
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		for(size_t i= 0; i < params.size(); ++i)
		{
			switch(i)
			{
				case 0: frequency.set_from_info(params[i]); break;
				case 1: phase.set_from_info(params[i]); break;
				case 2: amplitude.set_from_info(params[i]); break;
				case 3: offset.set_from_info(params[i]); break;
				default: break;
			}
		}
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		frequency.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 1);
		phase.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 2);
		amplitude.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 3);
		offset.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 4);
	}
	virtual size_t num_inputs() { return 4; }
};

static FieldModifier* create_field_mod(ModManager* man, FieldModifierType type, vector<mod_input_info>& params)
{
	switch(type)
	{
		case FMT_Constant:
			return new FieldModifierConstant(man, params);
		case FMT_Sine:
			return new FieldModifierSine(man, params);
		default:
			return nullptr;
	}
	return nullptr;
}

ModifiableValue::~ModifiableValue()
{
	clear_mods();
}

void ModifiableValue::set_manager(ModManager* man)
{
	m_manager= man;
}

double ModifiableValue::evaluate(mod_val_inputs const& input)
{
	double sum= m_value.get_value();
	for(auto&& mod : m_mods)
	{
		sum+= mod->evaluate(input);
	}
	return sum;
}

void ModifiableValue::add_mod(FieldModifierType type, std::vector<mod_input_info>& params)
{
	FieldModifier* new_mod= create_field_mod(m_manager, type, params);
	if(new_mod == nullptr)
	{
		LuaHelpers::ReportScriptError("Problem creating modifier: unknown type.");
		return;
	}
	m_mods.push_back(new_mod);
}

FieldModifier* ModifiableValue::get_mod(size_t index)
{
	if(index < m_mods.size())
	{
		return m_mods[index];
	}
	return nullptr;
}

void ModifiableValue::remove_mod(size_t index)
{
	if(index < m_mods.size())
	{
		delete m_mods[index];
		m_mods.erase(m_mods.begin() + index);
	}
}

void ModifiableValue::clear_mods()
{
	for(auto&& mod : m_mods)
	{
		delete mod;
	}
	m_mods.clear();
}


// lua start
#define LUA_GET_SET_FLOAT(member) \
static int get_ ## member(T* p, lua_State* L) \
{ \
	lua_pushnumber(L, p->get_ ## member()); \
	return 1; \
} \
static int set_ ## member(T* p, lua_State* L) \
{ \
	p->set_ ## member(FArg(1)); \
	COMMON_RETURN_SELF; \
}

struct LunaApproachingValue : Luna<ApproachingValue>
{
	LUA_GET_SET_FLOAT(value);
	LUA_GET_SET_FLOAT(speed);
	LUA_GET_SET_FLOAT(goal);
	static int set_value_instant(T* p, lua_State* L)
	{
		p->set_value_instant(FArg(1));
		COMMON_RETURN_SELF;
	}
	LunaApproachingValue()
	{
		ADD_GET_SET_METHODS(value);
		ADD_GET_SET_METHODS(speed);
		ADD_GET_SET_METHODS(goal);
		ADD_METHOD(set_value_instant);
	}
};
LUA_REGISTER_CLASS(ApproachingValue);

struct LunaFieldModifier : Luna<FieldModifier>
{
	static int get_inputs(T* p, lua_State* L)
	{
		lua_createtable(L, p->num_inputs(), 0);
		p->push_inputs(L, lua_gettop(L));
		return 1;
	}
	LunaFieldModifier()
	{
		ADD_METHOD(get_inputs);
	}
};
LUA_REGISTER_CLASS(FieldModifier);

struct LunaModifiableValue : Luna<ModifiableValue>
{
	static int add_mod(T* p, lua_State* L)
	{
		FieldModifierType type= Enum::Check<FieldModifierType>(L, 1);
		vector<mod_input_info> params;
		if(lua_istable(L, 2))
		{
			size_t param_count= lua_objlen(L, 2);
			for(size_t p= 1; p <= param_count; ++p)
			{
				mod_input_info info;
				lua_rawgeti(L, 2, p);
				if(lua_isnumber(L, -1))
				{
					info.type= MIT_Scalar;
					info.scalar= lua_tonumber(L, -1);
				}
				else if(lua_istable(L, -1))
				{
					lua_rawgeti(L, -1, 1);
					info.type= Enum::Check<ModInputType>(L, -1);
					lua_pop(L, 1);
					lua_rawgeti(L, -1, 2);
					info.scalar= lua_tonumber(L, -1);
					lua_pop(L, 1);
				}
				lua_pop(L, 1);
				params.push_back(info);
			}
		}
		p->add_mod(type, params);
		COMMON_RETURN_SELF;
	}
	static int get_mod(T* p, lua_State* L)
	{
		FieldModifier* mod= p->get_mod(static_cast<size_t>(IArg(1)));
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
	static int num_mods(T* p, lua_State* L)
	{
		lua_pushnumber(L, p->num_mods());
		return 1;
	}
	static int remove_mod(T* p, lua_State* L)
	{
		p->remove_mod(static_cast<size_t>(IArg(1)));
		COMMON_RETURN_SELF;
	}
	static int clear_mods(T* p, lua_State* L)
	{
		p->clear_mods();
		COMMON_RETURN_SELF;
	}
	static int get_value(T* p, lua_State* L)
	{
		p->get_value().PushSelf(L);
		return 1;
	}
	LunaModifiableValue()
	{
		ADD_METHOD(add_mod);
		ADD_METHOD(get_mod);
		ADD_METHOD(num_mods);
		ADD_METHOD(remove_mod);
		ADD_METHOD(clear_mods);
		ADD_METHOD(get_value);
	}
};
LUA_REGISTER_CLASS(ModifiableValue);
