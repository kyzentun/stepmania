#include "global.h"
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
};
XToString(ModInputType);
LuaXType(ModInputType);

static const char* ModFunctionTypeNames[] = {
	"Constant",
	"Product",
	"Power",
	"Log",
	"Sine",
	"Square",
	"Triangle",
};
XToString(ModFunctionType);
LuaXType(ModFunctionType);

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


#define MOD_FUNC_CONSTRUCTOR(name) \
ModFunction ## name(ModManager* man, std::vector<mod_input_info>& params) \
{ \
	set_manager(man); \
	set_from_params(params); \
}

struct ModFunctionConstant : ModFunction
{
	MOD_FUNC_CONSTRUCTOR(Constant);
	mod_input_picker value;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return apply_gap(apply_saw(value.pick(input), input), input);
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
	virtual void push_inputs(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets, {&value});
	}
	virtual size_t num_inputs() { return 1; }
};

struct ModFunctionProduct : ModFunction
{
	MOD_FUNC_CONSTRUCTOR(Product);
	mod_input_picker value;
	mod_input_picker mult;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return apply_gap(apply_saw(value.pick(input), input), input) *
			mult.pick(input);
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
		mult.set_manager(man);
	}
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		for(size_t i= 0; i < params.size(); ++i)
		{
			switch(i)
			{
				case 0: value.set_from_info(params[i]); break;
				case 1: mult.set_from_info(params[i]); break;
				default: break;
			}
		}
	}
	virtual void push_inputs(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets, {&value, &mult});
	}
	virtual size_t num_inputs() { return 2; }
};

struct ModFunctionPower : ModFunction
{
	MOD_FUNC_CONSTRUCTOR(Power);
	mod_input_picker value;
	mod_input_picker mult;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return pow(apply_gap(apply_saw(value.pick(input), input), input),
			mult.pick(input));
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
		mult.set_manager(man);
	}
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		for(size_t i= 0; i < params.size(); ++i)
		{
			switch(i)
			{
				case 0: value.set_from_info(params[i]); break;
				case 1: mult.set_from_info(params[i]); break;
				default: break;
			}
		}
	}
	virtual void push_inputs(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets, {&value, &mult});
	}
	virtual size_t num_inputs() { return 2; }
};

struct ModFunctionLog : ModFunction
{
	MOD_FUNC_CONSTRUCTOR(Log);
	mod_input_picker value;
	mod_input_picker base;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return log(apply_gap(apply_saw(value.pick(input), input), input)) /
			log(base.pick(input));
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
		base.set_manager(man);
	}
	virtual void set_from_params(std::vector<mod_input_info>& params)
	{
		for(size_t i= 0; i < params.size(); ++i)
		{
			switch(i)
			{
				case 0: value.set_from_info(params[i]); break;
				case 1: base.set_from_info(params[i]); break;
				default: break;
			}
		}
	}
	virtual void push_inputs(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets, {&value, &base});
	}
	virtual size_t num_inputs() { return 2; }
};

struct ModFunctionWave : ModFunction
{
	ModFunctionWave() {}
	MOD_FUNC_CONSTRUCTOR(Wave);
	mod_input_picker angle;
	mod_input_picker phase;
	mod_input_picker amplitude;
	mod_input_picker offset;
	virtual double evaluate(mod_val_inputs const& input)
	{
		double amp= amplitude.pick(input);
		if(amp == 0.0)
		{
			return offset.pick(input);
		}
		double angle_res= apply_gap(apply_saw(angle.pick(input) +
				phase.pick(input), input), input);
		angle_res= fmod(angle_res, M_PI * 2.0);
		if(angle_res < 0.0)
		{
			angle_res+= M_PI * 2.0;
		}
		double const wave_res= eval_internal(angle_res);
		return (wave_res * amp) + offset.pick(input);
	}
	virtual double eval_internal(double const angle)
	{
		return angle;
	}
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
	virtual void push_inputs(lua_State* L, int table_index, bool with_offsets)
	{
		push_inputs_internal(L, table_index, with_offsets,
			{&angle, &phase, &amplitude, &offset});
	}
	virtual size_t num_inputs() { return 4; }
};

struct ModFunctionSine : ModFunctionWave
{
	ModFunctionSine() {}
	MOD_FUNC_CONSTRUCTOR(Sine);
	virtual double eval_internal(double const angle)
	{
		return RageFastSin(angle);
	}
};

struct ModFunctionSquare : ModFunctionWave
{
	ModFunctionSquare() {}
	MOD_FUNC_CONSTRUCTOR(Square);
	virtual double eval_internal(double const angle)
	{
		return angle >= M_PI ? -1.0 : 1.0;
	}
};

struct ModFunctionTriangle : ModFunctionWave
{
	ModFunctionTriangle() {}
	MOD_FUNC_CONSTRUCTOR(Triangle);
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
};

static ModFunction* create_field_mod(ModManager* man, ModFunctionType type, vector<mod_input_info>& params)
{
	switch(type)
	{
		case MFT_Constant:
			return new ModFunctionConstant(man, params);
		case MFT_Product:
			return new ModFunctionProduct(man, params);
		case MFT_Power:
			return new ModFunctionPower(man, params);
		case MFT_Log:
			return new ModFunctionLog(man, params);
		case MFT_Sine:
			return new ModFunctionSine(man, params);
		case MFT_Square:
			return new ModFunctionSquare(man, params);
		case MFT_Triangle:
			return new ModFunctionTriangle(man, params);
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

void ModifiableValue::add_mod(ModFunctionType type, std::vector<mod_input_info>& params)
{
	ModFunction* new_mod= create_field_mod(m_manager, type, params);
	if(new_mod == nullptr)
	{
		LuaHelpers::ReportScriptError("Problem creating modifier: unknown type.");
		return;
	}
	m_mods.push_back(new_mod);
}

ModFunction* ModifiableValue::get_mod(size_t index)
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

struct LunaModFunction : Luna<ModFunction>
{
	static int get_inputs(T* p, lua_State* L)
	{
		bool with_offsets= lua_toboolean(L, 1);
		lua_createtable(L, p->num_inputs(), 0);
		p->push_inputs(L, lua_gettop(L), with_offsets);
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
		ModFunctionType type= Enum::Check<ModFunctionType>(L, 1);
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
					// The use of lua_tonumber is deliberate.  If the scalar or offset
					// value does not exist, lua_tonumber will return 0.
					lua_rawgeti(L, -1, 2);
					info.scalar= lua_tonumber(L, -1);
					lua_pop(L, 1);
					lua_rawgeti(L, -1, 3);
					info.offset= lua_tonumber(L, -1);
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
		ModFunction* mod= p->get_mod(static_cast<size_t>(IArg(1)));
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
