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
	"Square",
	"Triangle",
};
XToString(ModFunctionType);
LuaXType(ModFunctionType);

void ModManager::update(double curr_second)
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
				this_fap->parent->remove_mod_from_active_list(this_fap->func);
				m_present_funcs.erase(this_fap);
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
				this_fap->parent->remove_mod_from_active_list(this_fap->func);
				m_present_funcs.erase(this_fap);
			}
			else if(this_fap->func->m_start_second > curr_second)
			{
				insert_into_future(*this_fap);
				this_fap->parent->remove_mod_from_active_list(this_fap->func);
				m_present_funcs.erase(this_fap);
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
}

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

void ModInput::clear()
{
	m_type= MIT_Scalar;
	m_scalar= 0.0;
	m_offset= 0.0;
	m_rep_enabled= false;
	m_rep_begin= 0.0;
	m_rep_end= 0.0;
	m_unce_enabled= false;
	m_unce_before= 0.0;
	m_unce_begin= 0.0;
	m_unce_during= 0.0;
	m_unce_end= 0.0;
	m_unce_after= 0.0;
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
		if(lua_istable(L, -1))
		{
			m_rep_enabled= true;
			lua_rawgeti(L, -1, 1);
			m_rep_begin= lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 2);
			m_rep_end= lua_tonumber(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		lua_getfield(L, index, "unce");
		if(lua_istable(L, -1))
		{
			m_unce_enabled= true;
			lua_rawgeti(L, -1, 1);
			m_unce_before= lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 2);
			m_unce_begin= lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 3);
			m_unce_during= lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 4);
			m_unce_end= lua_tonumber(L, -1);
			lua_pop(L, 1);
			lua_rawgeti(L, -1, 5);
			m_unce_after= lua_tonumber(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
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
		case MFT_Square:
			ret= new ModFunctionSquare(parent);
			break;
		case MFT_Triangle:
			ret= new ModFunctionTriangle(parent);
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
	GET_SET_BOOL_METHOD(unce_enabled, m_unce_enabled);
	static int get_rep(T* p, lua_State* L)
	{
		lua_createtable(L, 2, 0);
		lua_pushnumber(L, p->m_rep_begin);
		lua_rawseti(L, -2, 1);
		lua_pushnumber(L, p->m_rep_end);
		lua_rawseti(L, -2, 2);
		return 1;
	}
	static int set_rep(T* p, lua_State* L)
	{
		if(!lua_istable(L, 1))
		{
			luaL_error(L, "Arg for ModInput:set_rep must be a table of two numbers.");
		}
		lua_rawgeti(L, 1, 1);
		p->m_rep_begin= FArg(-1);
		lua_pop(L, 1);
		lua_rawgeti(L, 1, 2);
		p->m_rep_end= FArg(-1);
		lua_pop(L, 1);
		COMMON_RETURN_SELF;
	}
	static int get_unce(T* p, lua_State* L)
	{
		lua_createtable(L, 2, 0);
		lua_pushnumber(L, p->m_unce_before);
		lua_rawseti(L, -2, 1);
		lua_pushnumber(L, p->m_unce_begin);
		lua_rawseti(L, -2, 2);
		lua_pushnumber(L, p->m_unce_during);
		lua_rawseti(L, -2, 3);
		lua_pushnumber(L, p->m_unce_end);
		lua_rawseti(L, -2, 4);
		lua_pushnumber(L, p->m_unce_after);
		lua_rawseti(L, -2, 5);
		return 1;
	}
	static int set_unce(T* p, lua_State* L)
	{
		if(!lua_istable(L, 1))
		{
			luaL_error(L, "Arg for ModInput:set_unce must be a table of five numbers.");
		}
		lua_rawgeti(L, 1, 1);
		p->m_unce_before= FArg(-1);
		lua_pop(L, 1);
		lua_rawgeti(L, 1, 2);
		p->m_unce_begin= FArg(-1);
		lua_pop(L, 1);
		lua_rawgeti(L, 1, 3);
		p->m_unce_during= FArg(-1);
		lua_pop(L, 1);
		lua_rawgeti(L, 1, 4);
		p->m_unce_end= FArg(-1);
		lua_pop(L, 1);
		lua_rawgeti(L, 1, 5);
		p->m_unce_after= FArg(-1);
		lua_pop(L, 1);
		COMMON_RETURN_SELF;
	}
	LunaModInput()
	{
		ADD_GET_SET_METHODS(type);
		ADD_GET_SET_METHODS(rep_enabled);
		ADD_GET_SET_METHODS(unce_enabled);
		ADD_GET_SET_METHODS(scalar);
		ADD_GET_SET_METHODS(offset);
		ADD_GET_SET_METHODS(rep);
		ADD_GET_SET_METHODS(unce);
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
