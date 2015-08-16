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
	"YOffset",
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

void ModInput::clear()
{
	m_type= MIT_Scalar;
	m_scalar.set_value_instant(0.0);
	m_offset.set_value_instant(0.0);
	m_rep_enabled= false;
	m_rep_begin.set_value_instant(0.0);
	m_rep_end.set_value_instant(0.0);
	m_unce_enabled= false;
	m_unce_before.set_value_instant(0.0);
	m_unce_begin.set_value_instant(0.0);
	m_unce_during.set_value_instant(0.0);
	m_unce_end.set_value_instant(0.0);
	m_unce_after.set_value_instant(0.0);
}

void ModInput::load_from_lua(lua_State* L, int index)
{
	if(lua_isnumber(L, index))
	{
		m_type= MIT_Scalar;
		m_scalar.set_value_instant(lua_tonumber(L, index));
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
		m_scalar.set_value_instant(lua_tonumber(L, -1));
		lua_pop(L, 1);
		lua_rawgeti(L, index, 3);
		m_offset.set_value_instant(lua_tonumber(L, -1));
		lua_pop(L, 1);
		lua_getfield(L, index, "rep");
		if(lua_istable(L, -1))
		{
			m_rep_enabled= true;
			lua_rawgeti(L, -1, 1);
			m_rep_begin.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 2);
			m_rep_end.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		lua_getfield(L, index, "unce");
		if(lua_istable(L, -1))
		{
			m_unce_enabled= true;
			lua_rawgeti(L, -1, 1);
			m_unce_before.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 2);
			m_unce_begin.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 3);
			m_unce_during.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 4);
			m_unce_end.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 5);
			m_unce_after.set_value_instant(lua_tonumber(L, -1));
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
}

#define MOD_FUNC_CONSTRUCTOR(name) \
ModFunction ## name(ModManager* man) \
{ \
	set_manager(man); \
}

struct ModFunctionConstant : ModFunction
{
	MOD_FUNC_CONSTRUCTOR(Constant);
	ModInput value;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return value.pick(input);
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
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
	MOD_FUNC_CONSTRUCTOR(Product);
	ModInput value;
	ModInput mult;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return value.pick(input) * mult.pick(input);
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
		mult.set_manager(man);
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
	MOD_FUNC_CONSTRUCTOR(Power);
	ModInput value;
	ModInput mult;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return pow(value.pick(input), mult.pick(input));
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
		mult.set_manager(man);
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
	MOD_FUNC_CONSTRUCTOR(Log);
	ModInput value;
	ModInput base;
	virtual double evaluate(mod_val_inputs const& input)
	{
		return log(value.pick(input)) / log(base.pick(input));
	}
	virtual void set_manager(ModManager* man)
	{
		value.set_manager(man);
		base.set_manager(man);
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
	ModFunctionWave() {}
	MOD_FUNC_CONSTRUCTOR(Wave);
	ModInput angle;
	ModInput phase;
	ModInput amplitude;
	ModInput offset;
	virtual double evaluate(mod_val_inputs const& input)
	{
		double amp= amplitude.pick(input);
		if(amp == 0.0)
		{
			return offset.pick(input);
		}
		double angle_res= angle.pick(input) + phase.pick(input);
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

static ModFunction* create_field_mod(ModManager* man, lua_State* L, int index)
{
	lua_rawgeti(L, index, 1);
	ModFunctionType type= Enum::Check<ModFunctionType>(L, -1);
	lua_pop(L, 1);
	ModFunction* ret= nullptr;
	switch(type)
	{
		case MFT_Constant:
			ret= new ModFunctionConstant(man);
			break;
		case MFT_Product:
			ret= new ModFunctionProduct(man);
			break;
		case MFT_Power:
			ret= new ModFunctionPower(man);
			break;
		case MFT_Log:
			ret= new ModFunctionLog(man);
			break;
		case MFT_Sine:
			ret= new ModFunctionSine(man);
			break;
		case MFT_Square:
			ret= new ModFunctionSquare(man);
			break;
		case MFT_Triangle:
			ret= new ModFunctionTriangle(man);
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

void ModifiableValue::add_mod(lua_State* L, int index)
{
	ModFunction* new_mod= create_field_mod(m_manager, L, index);
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
	static int get_scalar(T* p, lua_State* L)
	{
		p->m_scalar.PushSelf(L);
		return 1;
	}
	static int get_offset(T* p, lua_State* L)
	{
		p->m_offset.PushSelf(L);
		return 1;
	}
	GET_SET_BOOL_METHOD(rep_enabled, m_rep_enabled);
	GET_SET_BOOL_METHOD(unce_enabled, m_unce_enabled);
	static int get_rep(T* p, lua_State* L)
	{
		lua_createtable(L, 2, 0);
		p->m_rep_begin.PushSelf(L);
		lua_rawseti(L, -2, 1);
		p->m_rep_end.PushSelf(L);
		lua_rawseti(L, -2, 2);
		return 1;
	}
	static int get_unce(T* p, lua_State* L)
	{
		lua_createtable(L, 2, 0);
		p->m_unce_before.PushSelf(L);
		lua_rawseti(L, -2, 1);
		p->m_unce_begin.PushSelf(L);
		lua_rawseti(L, -2, 2);
		p->m_unce_during.PushSelf(L);
		lua_rawseti(L, -2, 3);
		p->m_unce_end.PushSelf(L);
		lua_rawseti(L, -2, 4);
		p->m_unce_after.PushSelf(L);
		lua_rawseti(L, -2, 5);
		return 1;
	}
	LunaModInput()
	{
		ADD_GET_SET_METHODS(type);
		ADD_GET_SET_METHODS(rep_enabled);
		ADD_GET_SET_METHODS(unce_enabled);
		ADD_METHOD(get_scalar);
		ADD_METHOD(get_offset);
		ADD_METHOD(get_rep);
		ADD_METHOD(get_unce);
	}
};
LUA_REGISTER_CLASS(ModInput);

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
