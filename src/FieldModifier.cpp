#include "global.h"
#include "EnumHelper.h"
#include "FieldModifier.h"
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
};
XToString(ModInputType);
LuaXType(ModInputType);

static const char* FieldModifierTypeNames[] = {
	"Constant",
	"Sine",
	"Square",
	"Triangle",
	"SawSine",
	"SawSquare",
	"SawTriangle",
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

#define FIELD_MOD_CONSTRUCTOR(name) \
FieldModifier ## name(ModManager* man, std::vector<mod_input_info>& params) \
{ \
	set_manager(man); \
	set_from_params(params); \
}

struct FieldModifierConstant : FieldModifier
{
	FIELD_MOD_CONSTRUCTOR(Constant);
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

struct FieldModifierWave : FieldModifier
{
	FieldModifierWave() {}
	FIELD_MOD_CONSTRUCTOR(Wave);
	mod_input_picker angle;
	mod_input_picker phase;
	mod_input_picker amplitude;
	mod_input_picker offset;
	virtual void set_manager(ModManager* man)
	{
		angle.set_manager(man);
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
				case 0: angle.set_from_info(params[i]); break;
				case 1: phase.set_from_info(params[i]); break;
				case 2: amplitude.set_from_info(params[i]); break;
				case 3: offset.set_from_info(params[i]); break;
				default: break;
			}
		}
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		angle.scalar.PushSelf(L);
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

#define ZERO_AMP_RETURN_EARLY \
double amp= amplitude.pick(input); \
if(amp == 0.0) \
{ \
	return offset.pick(input); \
}

struct FieldModifierSine : FieldModifierWave
{
	FieldModifierSine() {}
	FIELD_MOD_CONSTRUCTOR(Sine);
	virtual double eval_internal(double const angle)
	{
		return RageFastSin(angle);
	}
	virtual double evaluate(mod_val_inputs const& input)
	{
		ZERO_AMP_RETURN_EARLY;
		return (eval_internal(angle.pick(input) + phase.pick(input)) * amp)
			+ offset.pick(input);
	}
};

#define WAVE_INPUT \
double wave_input= angle.pick(input) + phase.pick(input); \
double wave_result= fmod(wave_input, M_PI * 2.0); \
if(wave_result < 0.0) \
{ \
	wave_result+= M_PI * 2.0; \
}

struct FieldModifierSquare : FieldModifierWave
{
	FieldModifierSquare() {}
	FIELD_MOD_CONSTRUCTOR(Square);
	virtual double eval_internal(double const angle)
	{
		return angle >= M_PI ? -1.0 : 1.0;
	}
	virtual double evaluate(mod_val_inputs const& input)
	{
		ZERO_AMP_RETURN_EARLY;
		WAVE_INPUT;
		return eval_internal(wave_result) * amp + offset.pick(input);
	}
};

struct FieldModifierTriangle : FieldModifierWave
{
	FieldModifierTriangle() {}
	FIELD_MOD_CONSTRUCTOR(Triangle);
	virtual double eval_internal(double const angle)
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
	virtual double evaluate(mod_val_inputs const& input)
	{
		ZERO_AMP_RETURN_EARLY;
		WAVE_INPUT;
		return (eval_internal(wave_result) * amp) + offset.pick(input);
	}
};

double clip_wave_input_with_saw(double const angle, double const saw_begin,
	double const saw_end)
{
	double const dist= saw_end - saw_begin;
	double const mod_res= fmod(angle, dist);
	if(mod_res < 0.0)
	{
		return mod_res + dist + saw_begin;
	}
	return mod_res + saw_begin;
}

template<typename wave>
	struct FieldModifierSaw : wave
{
	FIELD_MOD_CONSTRUCTOR(Saw);
	mod_input_picker saw_begin;
	mod_input_picker saw_end;
	virtual void set_manager(ModManager* man)
	{
		wave::set_manager(man);
		saw_begin.set_manager(man);
		saw_end.set_manager(man);
	}
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		for(size_t i= 0; i < params.size(); ++i)
		{
			switch(i)
			{
				case 0: wave::angle.set_from_info(params[i]); break;
				case 1: wave::phase.set_from_info(params[i]); break;
				case 2: wave::amplitude.set_from_info(params[i]); break;
				case 3: wave::offset.set_from_info(params[i]); break;
				case 4: saw_begin.set_from_info(params[i]); break;
				case 5: saw_end.set_from_info(params[i]); break;
				default: break;
			}
		}
	}
	virtual void push_inputs(lua_State* L, int table_index)
	{
		wave::push_inputs(L, table_index);
		saw_begin.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 5);
		saw_end.scalar.PushSelf(L);
		lua_rawseti(L, table_index, 6);
	}
	virtual size_t num_inputs() { return 6; }
	virtual double evaluate(mod_val_inputs const& input)
	{
		double amp= wave::amplitude.pick(input);
		if(amp == 0.0)
		{
			return wave::offset.pick(input);
		}
		double wave_input= wave::angle.pick(input) + wave::phase.pick(input);
		double wave_result= fmod(wave_input, M_PI * 2.0);
		if(wave_result < 0.0)
		{
			wave_result+= M_PI * 2.0;
		}
		wave_result= clip_wave_input_with_saw(wave_result,
			saw_begin.pick(input), saw_end.pick(input));
		return (wave::eval_internal(wave_result) * amp) + wave::offset.pick(input);;
	}
};

static FieldModifier* create_field_mod(ModManager* man, FieldModifierType type, vector<mod_input_info>& params)
{
	switch(type)
	{
		case FMT_Constant:
			return new FieldModifierConstant(man, params);
		case FMT_Sine:
			return new FieldModifierSine(man, params);
		case FMT_Square:
			return new FieldModifierSquare(man, params);
		case FMT_Triangle:
			return new FieldModifierTriangle(man, params);
		case FMT_SawSine:
			return new FieldModifierSaw<FieldModifierSine>(man, params);
		case FMT_SawSquare:
			return new FieldModifierSaw<FieldModifierSquare>(man, params);
		case FMT_SawTriangle:
			return new FieldModifierSaw<FieldModifierTriangle>(man, params);
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
