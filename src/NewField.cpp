#include "global.h"

#include "arch/Dialog/Dialog.h"
#include "ActorUtil.h"
#include "Game.h"
#include "GameManager.h"
#include "GameState.h"
#include "LuaBinding.h"
#include "NewField.h"
#include "RageDisplay.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageTextureManager.h"
#include "RageUtil.h"
#include "ScreenDimensions.h"
#include "SpecialFiles.h"
#include "Sprite.h"
#include "Steps.h"
#include "Style.h"
#include "ThemeManager.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"

using std::max;
using std::string;
using std::unordered_set;
using std::vector;

static const double note_size= 64.0;
static double speed_multiplier= 4.0;

static const char* NewSkinTapPartNames[] = {
	"Tap",
	"Mine",
	"Lift",
};
XToString(NewSkinTapPart);
LuaXType(NewSkinTapPart);

static const char* NewSkinTapOptionalPartNames[] = {
	"HoldHead",
	"HoldTail",
	"RollHead",
	"RollTail",
	"CheckpointHead",
	"CheckpointTail",
};
XToString(NewSkinTapOptionalPart);
LuaXType(NewSkinTapOptionalPart);

static const char* NewSkinHoldPartNames[] = {
	"Top",
	"Body",
	"Bottom",
};
XToString(NewSkinHoldPart);
LuaXType(NewSkinHoldPart);

static const char* TexCoordFlipModeNames[] = {
	"None",
	"X",
	"Y",
	"XY",
};
XToString(TexCoordFlipMode);
LuaXType(TexCoordFlipMode);

size_t get_table_len(lua_State* L, int index, size_t max_entries,
	string const& table_name, string& insanity_diagnosis)
{
	if(!lua_istable(L, index))
	{
		insanity_diagnosis= ssprintf("%s is not a table.", table_name.c_str());
		return 0;
	}
	size_t ret= lua_objlen(L, index);
	if(ret == 0)
	{
		insanity_diagnosis= ssprintf("The %s table is empty.",table_name.c_str());
		return 0;
	}
	if(ret > max_entries)
	{
		insanity_diagnosis= ssprintf("The %s table has over %zu entries.",
			table_name.c_str(), max_entries);
	}
	return ret;
}

template<typename el_type>
	bool load_simple_table(lua_State* L, int index, size_t max_entries,
		vector<el_type>& dest, el_type offset, el_type max_value,
		string const& table_name, string& insanity_diagnosis)
{
	size_t tab_size= get_table_len(L, index, max_entries, table_name, insanity_diagnosis);
	if(tab_size == 0)
	{
		return false;
	}
	dest.resize(tab_size);
	for(size_t i= 0; i < tab_size; ++i)
	{
		lua_rawgeti(L, index, i+1);
		el_type value= static_cast<el_type>(lua_tonumber(L, -1) - offset);
		lua_pop(L, 1);
		if(value >= max_value)
		{
			insanity_diagnosis= ssprintf("Entry %zu in the %s table is not valid.",
				i+1, table_name.c_str());
			return false;
		}
		dest[i]= value;
	}
	lua_pop(L, 1);
	return true;
}

bool load_string_table(lua_State* L, int index, size_t max_entries,
	vector<string>& dest, string const& table_name, string& insanity_diagnosis)
{
	size_t tab_size= get_table_len(L, index, max_entries, table_name, insanity_diagnosis);
	if(tab_size == 0)
	{
		return false;
	}
	dest.resize(tab_size);
	for(size_t i= 0; i < tab_size; ++i)
	{
		lua_rawgeti(L, index, i+1);
		if(!lua_isstring(L, -1))
		{
			insanity_diagnosis= ssprintf("Entry %zu in the %s table is not valid.",
				i+1, table_name.c_str());
			return false;
		}
		dest[i]= lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return true;
}

template<typename el_type, typename en_type>
	void load_enum_table(lua_State* L, int index, en_type begin, en_type end,
		vector<el_type>& dest, el_type offset, el_type max_value,
		el_type default_value)
{
	// To allow expansion later, a missing element is not an error.  Instead,
	// the default value is used.
	dest.resize(end - begin);
	for(auto&& el : dest)
	{
		el= default_value;
	}
	if(!lua_istable(L, index))
	{
		return;
	}
	for(el_type curr= begin; curr != end; ++curr)
	{
		Enum::Push(L, static_cast<en_type>(curr));
		lua_gettable(L, index);
		if(!lua_isnoneornil(L, -1))
		{
			el_type value= static_cast<el_type>(lua_tonumber(L, -1) - offset);
			if(value < max_value)
			{
				dest[curr-begin]= value;
			}
		}
		lua_pop(L, 1);
	}
}

bool QuantizedStateMap::load_from_lua(lua_State* L, int index, string& insanity_diagnosis)
{
	// Loading is atomic:  If a single error occurs during loading the data,
	// none of it is used.
	// Pop the table we're loading from off the stack when returning.
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	lua_getfield(L, index, "quanta");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No quanta found");
	}
	size_t num_quanta= get_table_len(L, -1, max_quanta, "quanta", insanity_diagnosis);
	if(num_quanta == 0)
	{
		RETURN_NOT_SANE(insanity_diagnosis);
	}
	int quanta_index= lua_gettop(L);
	vector<QuantizedStates> temp_quanta(num_quanta);
	for(size_t i= 0; i < temp_quanta.size(); ++i)
	{
		lua_rawgeti(L, quanta_index, i+1);
		if(!lua_istable(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Invalid quantum %zu.", i+1));
		}
		lua_getfield(L, -1, "per_beat");
		if(!lua_isnumber(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Invalid per_beat value in quantum %zu.", i+1));
		}
		temp_quanta[i].per_beat= lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_getfield(L, -1, "states");
		if(!lua_istable(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Invalid states in quantum %zu.", i+1));
		}
		if(!load_simple_table(L, lua_gettop(L), max_states,
				temp_quanta[i].states, static_cast<size_t>(1), max_states, "states",
				insanity_diagnosis))
		{
			RETURN_NOT_SANE(ssprintf("Invalid states in quantum %zu: %s", i+1, insanity_diagnosis.c_str()));
		}
		lua_pop(L, 1);
	}
	lua_getfield(L, index, "parts_per_beat");
	if(!lua_isnumber(L, -1))
	{
		RETURN_NOT_SANE("Invalid parts_per_beat.");
	}
	m_parts_per_beat= lua_tointeger(L, -1);
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	m_quanta.swap(temp_quanta);
	return true;
}

bool QuantizedTap::load_from_lua(lua_State* L, int index, string& insanity_diagnosis)
{
	// Pop the table we're loading from off the stack when returning.
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	lua_getfield(L, index, "state_map");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No state map found.");
	}
	QuantizedStateMap temp_map;
	if(!temp_map.load_from_lua(L, lua_gettop(L), insanity_diagnosis))
	{
		RETURN_NOT_SANE(insanity_diagnosis);
	}
	lua_getfield(L, index, "actor");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("Actor not found.");
	}
	std::unique_ptr<XNode> node(XmlFileUtil::XNodeFromTable(L));
	if(node.get() == nullptr)
	{
		RETURN_NOT_SANE("Actor not valid.");
	}
	Actor* act= ActorUtil::LoadFromNode(node.get(), nullptr);
	if(act == nullptr)
	{
		RETURN_NOT_SANE("Error loading actor.");
	}
	m_actor.Load(act);
	m_frame.AddChild(m_actor);
	lua_getfield(L, index, "vivid");
	m_vivid= lua_toboolean(L, -1);
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	m_state_map.swap(temp_map);
	return true;
}

bool QuantizedHold::load_from_lua(lua_State* L, int index, string const& load_dir, string& insanity_diagnosis)
{
	// Pop the table we're loading from off the stack when returning.
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	lua_getfield(L, index, "state_map");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No state map found.");
	}
	QuantizedStateMap temp_map;
	if(!temp_map.load_from_lua(L, lua_gettop(L), insanity_diagnosis))
	{
		RETURN_NOT_SANE(insanity_diagnosis);
	}
	lua_getfield(L, index, "textures");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No textures found.");
	}
	size_t num_tex= get_table_len(L, -1, max_hold_layers, "textures", insanity_diagnosis);
	if(num_tex == 0)
	{
		RETURN_NOT_SANE(insanity_diagnosis);
	}
	int texind= lua_gettop(L);
	vector<RageTexture*> temp_parts(num_tex);
	for(size_t part= 0; part < num_tex; ++part)
	{
		lua_rawgeti(L, texind, part+1);
		if(!lua_isstring(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Texture entry for layer %zu is not a string.",
					part+1));
		}
		string path= lua_tostring(L, -1);
		if(path.empty())
		{
			RETURN_NOT_SANE("Empty texture path is not valid.");
		}
		// Check to see if a texture is registered before trying to convert it to
		// a full path.  This allows someone to make an AFT and name the texture
		// of the AFT, then use that texture name in the part.
		RageTextureID as_id(path);
		RageTexture* as_tex= nullptr;
		if(TEXTUREMAN->IsTextureRegistered(as_id))
		{
			as_tex= TEXTUREMAN->LoadTexture(as_id);
		}
		else
		{
			bool is_relative= path[0] != '/';
			if(is_relative)
			{
				path= load_dir + path;
			}
			RString resolved= path;
			if(!ActorUtil::ResolvePath(resolved, "Noteskin hold texture", false) ||
				!FILEMAN->DoesFileExist(resolved))
			{
				as_tex= TEXTUREMAN->LoadTexture(TEXTUREMAN->GetDefaultTextureID());
			}
			else
			{
				as_tex= TEXTUREMAN->LoadTexture(resolved);
			}
		}
		temp_parts[part]= as_tex;
	}
	lua_getfield(L, index, "flip");
	m_flip= TCFM_None;
	if(lua_isstring(L, -1))
	{
		m_flip= Enum::Check<TexCoordFlipMode>(L, -1, true, true);
		if(m_flip >= NUM_TexCoordFlipMode)
		{
			LuaHelpers::ReportScriptErrorFmt("Invalid flip mode %s", lua_tostring(L, -1));
			m_flip= TCFM_None;
		}
	}
	lua_getfield(L, index, "vivid");
	m_vivid= lua_toboolean(L, -1);
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	m_state_map.swap(temp_map);
	m_parts.swap(temp_parts);
	return true;
}

Actor* NewSkinColumn::get_tap_actor(NewSkinTapPart type, double quantization, double beat)
{
	const size_t type_index= type;
	ASSERT_M(type < m_taps.size(), "Invalid NewSkinTapPart type.");
	return m_taps[type_index].get_quantized(quantization, beat, m_rotations[type_index]);
}

Actor* NewSkinColumn::get_optional_actor(NewSkinTapOptionalPart type, double quantization, double beat)
{
	const size_t type_index= type;
	ASSERT_M(type < m_optional_taps.size(), "Invalid NewSkinTapOptionalPart type.");
	QuantizedTap* tap= m_optional_taps[type_index];
	if(tap == nullptr)
	{
		tap= m_optional_taps[type_index % 2];
	}
	if(tap == nullptr)
	{
		if(type_index % 2 == 0) // heads fallback to taps.
		{
			return get_tap_actor(NSTP_Tap, quantization, beat);
		}
		return nullptr;
	}
	return tap->get_quantized(quantization, beat, m_rotations[type_index]);
}

void NewSkinColumn::get_hold_render_data(TapNoteSubType sub_type, bool active, double quantization, double beat, QuantizedHoldRenderData& data)
{
	if(sub_type >= NUM_TapNoteSubType)
	{
		data.clear();
		return;
	}
	m_holds[sub_type][active].get_quantized(quantization, beat, data);
}

bool NewSkinColumn::load_from_lua(lua_State* L, int index, string const& load_dir, string& insanity_diagnosis)
{
	// Pop the table we're loading from off the stack when returning.
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	vector<QuantizedTap> temp_taps;
	vector<QuantizedTap*> temp_optionals(NUM_NewSkinTapOptionalPart, nullptr);
	vector<vector<QuantizedHold> > temp_holds;
	vector<double> temp_rotations;
	lua_getfield(L, index, "taps");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No taps given.");
	}
	int taps_index= lua_gettop(L);
	temp_taps.resize(NUM_NewSkinTapPart);
	string sub_sanity;
	for(size_t part= NSTP_Tap; part < NUM_NewSkinTapPart; ++part)
	{
		Enum::Push(L, static_cast<NewSkinTapPart>(part));
		lua_gettable(L, taps_index);
		if(!lua_istable(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Part %s not returned.",
					NewSkinTapPartToString(static_cast<NewSkinTapPart>(part)).c_str()));
		}
		if(!temp_taps[part].load_from_lua(L, lua_gettop(L),
				sub_sanity))
		{
			RETURN_NOT_SANE(ssprintf("Error loading part %s: %s",
					NewSkinTapPartToString(static_cast<NewSkinTapPart>(part)).c_str(),
					sub_sanity.c_str()));
		}
	}
	lua_settop(L, taps_index-1);
	lua_getfield(L, index, "optional_taps");
	int optional_taps_index= lua_gettop(L);
	// Leaving out the optional field is not an error.
	if(lua_istable(L, -1))
	{
		for(size_t part= NSTOP_HoldHead; part < NUM_NewSkinTapOptionalPart; ++part)
		{
			Enum::Push(L, static_cast<NewSkinTapOptionalPart>(part));
			lua_gettable(L, optional_taps_index);
			if(lua_istable(L, -1))
			{
				QuantizedTap* temp= new QuantizedTap;
				if(!temp->load_from_lua(L, lua_gettop(L), sub_sanity))
				{
					SAFE_DELETE(temp);
					temp= nullptr;
				}
				temp_optionals[part]= temp;
			}
		}
	}
	lua_settop(L, optional_taps_index-1);
	lua_getfield(L, index, "holds");
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No holds given.");
	}
	int holds_index= lua_gettop(L);
	temp_holds.resize(NUM_TapNoteSubType);
	for(size_t part= 0; part < NUM_TapNoteSubType; ++part)
	{
		Enum::Push(L, static_cast<TapNoteSubType>(part));
		lua_gettable(L, holds_index);
		if(!lua_istable(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Hold subtype %s not returned.", TapNoteSubTypeToString(static_cast<TapNoteSubType>(part)).c_str()));
		}
		int actives_index= lua_gettop(L);
		static const size_t num_active_states= 2;
		temp_holds[part].resize(num_active_states);
		for(size_t a= 0; a < num_active_states; ++a)
		{
			lua_rawgeti(L, actives_index, a+1);
			if(!lua_istable(L, -1))
			{
				RETURN_NOT_SANE(ssprintf("Hold info not given for active state %zu of subtype %s.", a, TapNoteSubTypeToString(static_cast<TapNoteSubType>(part)).c_str()));
			}
			if(!temp_holds[part][a].load_from_lua(L, lua_gettop(L), load_dir, sub_sanity))
			{
				RETURN_NOT_SANE(ssprintf("Error loading active state %zu of subtype %s: %s", a, TapNoteSubTypeToString(static_cast<TapNoteSubType>(part)).c_str(), sub_sanity.c_str()));
			}
		}
	}
	lua_settop(L, holds_index-1);
	lua_getfield(L, index, "rotations");
	load_enum_table(L, lua_gettop(L), NSTP_Tap, NUM_NewSkinTapPart,
		temp_rotations, 0.0, 1000.0, 0.0);
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	m_taps.swap(temp_taps);
	clear_optionals();
	m_optional_taps.swap(temp_optionals);
	m_holds.swap(temp_holds);
	m_rotations.swap(temp_rotations);
	return true;
}

void NewSkinLayer::UpdateInternal(float delta)
{
	for(auto&& act : m_frames)
	{
		act.Update(delta);
	}
	ActorFrame::UpdateInternal(delta);
}

void NewSkinLayer::DrawPrimitives()
{
	for(auto&& act : m_frames)
	{
		act.Draw();
	}
	ActorFrame::DrawPrimitives();
}

bool NewSkinLayer::load_from_lua(lua_State* L, int index, size_t columns,
	std::string& insanity_diagnosis)
{
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	size_t num_columns= get_table_len(L, index, NewSkinData::max_columns, "layer actors", insanity_diagnosis);
	if(num_columns != columns)
	{
		RETURN_NOT_SANE(ssprintf("Invalid number of columns: %s", insanity_diagnosis.c_str()));
	}
	m_frames.resize(num_columns);
	m_actors.resize(num_columns);
	for(size_t c= 0; c < num_columns; ++c)
	{
		lua_rawgeti(L, index, c+1);
		if(!lua_istable(L, -1))
		{
			RETURN_NOT_SANE("Actor not found.");
		}
		std::unique_ptr<XNode> node(XmlFileUtil::XNodeFromTable(L));
		if(node.get() == nullptr)
		{
			RETURN_NOT_SANE("Actor not valid.");
		}
		Actor* act= ActorUtil::LoadFromNode(node.get(), nullptr);
		if(act == nullptr)
		{
			RETURN_NOT_SANE("Error loading actor.");
		}
		// The actors have to be wrapped inside of frames so that mod transforms
		// can be applied without stomping the rotation the noteskin supplies.
		m_actors[c].Load(act);
		m_frames[c].AddChild(m_actors[c]);
	}
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	return true;
}

void NewSkinLayer::transform_columns(vector<transform>& positions)
{
	for(size_t c= 0; c < m_frames.size() && c < positions.size(); ++c)
	{
		m_frames[c].set_transform(positions[c]);
	}
}

NewSkinData::NewSkinData()
	:m_loaded(false)
{
	
}

bool NewSkinData::load_layer_from_lua(lua_State* L, int index, bool under_notes, size_t columns, std::string& insanity_diagnosis)
{
	vector<NewSkinLayer>* container= under_notes ? &m_layers_below_notes : &m_layers_above_notes;
	container->resize(container->size()+1);
	if(!container->back().load_from_lua(L, index, columns, insanity_diagnosis))
	{
		container->pop_back();
		return false;
	}
	return true;
}

bool NewSkinData::load_taps_from_lua(lua_State* L, int index, size_t columns, string const& load_dir, string& insanity_diagnosis)
{
	//lua_pushvalue(L, index);
	//LuaHelpers::rec_print_table(L, "newskin_data", "");
	//lua_pop(L, 1);
	// Loading is atomic:  If a single error occurs during loading the data,
	// none of it is used.
	// Pop the table we're loading from off the stack when returning.
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	lua_getfield(L, index, "columns");
	size_t num_columns= get_table_len(L, -1, max_columns, "columns", insanity_diagnosis);
	if(num_columns != columns)
	{
		RETURN_NOT_SANE(ssprintf("Invalid number of columns: %s", insanity_diagnosis.c_str()));
	}
	vector<NewSkinColumn> temp_columns(num_columns);
	int columns_index= lua_gettop(L);
	string sub_sanity;
	for(size_t c= 0; c < num_columns; ++c)
	{
		lua_rawgeti(L, columns_index, c+1);
		if(!lua_istable(L, -1))
		{
			RETURN_NOT_SANE(ssprintf("Nothing given for column %zu.", c+1));
		}
		if(!temp_columns[c].load_from_lua(L, lua_gettop(L), load_dir, sub_sanity))
		{
			RETURN_NOT_SANE(ssprintf("Error loading column %zu: %s", c+1, sub_sanity.c_str()));
		}
	}
	lua_settop(L, columns_index-1);
	lua_getfield(L, index, "vivid_operation");
	if(lua_isboolean(L, -1))
	{
		bool vivid= lua_toboolean(L, -1);
		for(auto&& column : temp_columns)
		{
			column.vivid_operation(vivid);
		}
	}
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	m_columns.swap(temp_columns);
	m_loaded= true;
	return true;
}

bool NewSkinLoader::load_from_file(std::string const& path)
{
	if(!FILEMAN->IsAFile(path))
	{
		LuaHelpers::ReportScriptError("Noteskin '" + path + "' does not exist.");
		return false;
	}
	string temp_load_path= Dirname(path);
	RString skin_text;
	if(!GetFileContents(path, skin_text))
	{
		LuaHelpers::ReportScriptError("Could not load noteskin '" + path + "'.");
		return false;
	}
	RString error= "Error loading noteskin '" + path + "': ";
	Lua* L= LUA->Get();
	if(!LuaHelpers::RunScript(L, skin_text, "@" + path, error, 0, 1, true))
	{
		lua_settop(L, 0);
		LUA->Release(L);
		return false;
	}
	vector<RString> path_parts;
	split(path, "/", path_parts);
	size_t name_index= 0;
	if(path_parts.size() > 1)
	{
		name_index= path_parts.size() - 2;
	}
	string sanity;
	if(!load_from_lua(L, lua_gettop(L), path_parts[name_index], temp_load_path,
			sanity))
	{
		LuaHelpers::ReportScriptError("Error loading noteskin '" + path + "': "
			+ sanity);
		lua_settop(L, 0);
		LUA->Release(L);
		return false;
	}
	lua_settop(L, 0);
	LUA->Release(L);
	return true;
}

bool NewSkinLoader::load_from_lua(lua_State* L, int index, string const& name,
	string const& path, string& insanity_diagnosis)
{
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	if(!lua_istable(L, index))
	{
		RETURN_NOT_SANE("Noteskin data is not a table.");
	}
	unordered_set<string> temp_supported_buttons;
	lua_getfield(L, index, "buttons");
	// If there is no buttons table, it's not an error because a noteskin that
	// supports all buttons can consider it more convenient to just use the
	// supports_all_buttons flag.
	if(lua_istable(L, -1))
	{
		size_t num_buttons= lua_objlen(L, -1);
		for(size_t b= 0; b < num_buttons; ++b)
		{
			lua_rawgeti(L, -1, b+1);
			string button_name= lua_tostring(L, -1);
			temp_supported_buttons.insert(button_name);
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	vector<string> temp_below_loaders;
	lua_getfield(L, index, "layers_below_notes");
	if(lua_istable(L, -1))
	{
		string sub_sanity;
		if(!load_string_table(L, lua_gettop(L), max_layers, temp_below_loaders,
				"layers_below_notes", sub_sanity))
		{
			RETURN_NOT_SANE("Error in layers_below_notes table: " + sub_sanity);
		}
	}
	vector<string> temp_above_loaders;
	lua_getfield(L, index, "layers_above_notes");
	if(lua_istable(L, -1))
	{
		string sub_sanity;
		if(!load_string_table(L, lua_gettop(L), max_layers, temp_above_loaders,
				"layers_above_notes", sub_sanity))
		{
			RETURN_NOT_SANE("Error in layers_above_notes table: " + sub_sanity);
		}
	}
	lua_getfield(L, index, "notes");
	if(!lua_isstring(L, -1))
	{
		RETURN_NOT_SANE("No notes loader found.");
	}
	m_notes_loader= lua_tostring(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "supports_all_buttons");
	m_supports_all_buttons= lua_toboolean(L, -1);
	lua_settop(L, original_top);
#undef RETURN_NOT_SANE
	m_skin_name= name;
	m_load_path= path;
	m_below_loaders.swap(temp_below_loaders);
	m_above_loaders.swap(temp_above_loaders);
	m_supported_buttons.swap(temp_supported_buttons);
	return true;
}

bool NewSkinLoader::supports_needed_buttons(vector<string> const& button_list)
{
	if(m_supports_all_buttons)
	{
		return true;
	}
	for(auto&& button_name : button_list)
	{
		if(m_supported_buttons.find(button_name) == m_supported_buttons.end())
		{
			return false;
		}
	}
	return true;
}

bool NewSkinLoader::push_loader_function(lua_State* L, string const& loader)
{
	if(loader.empty())
	{
		return false;
	}
	string file= m_load_path + loader;
	if(!FILEMAN->IsAFile(file))
	{
		LuaHelpers::ReportScriptError("Noteskin " + m_skin_name + " points to a"
			" loader file that does not exist: " + file);
		return false;
	}
	RString script_text;
	if(!GetFileContents(file, script_text))
	{
		LuaHelpers::ReportScriptError("Noteskin " + m_skin_name + " points to a"
			" loader file " + file + " could not be loaded.");
		return false;
	}
	RString error= "Error loading " + file + ": ";
	if(!LuaHelpers::RunScript(L, script_text, "@" + file, error, 0, 1, true))
	{
		return false;
	}
	return true;
}

bool NewSkinLoader::load_layer_set_into_data(lua_State* L,
	int button_list_index, size_t columns, vector<string> const& loader_set,
	vector<NewSkinLayer>& dest, string& insanity_diagnosis)
{
	int original_top= lua_gettop(L);
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	vector<NewSkinLayer> temp_dest(loader_set.size());
	string sub_sanity;
	for(size_t i= 0; i < loader_set.size(); ++i)
	{
		if(!push_loader_function(L, loader_set[i]))
		{
			RETURN_NOT_SANE("Could not load loader " + loader_set[i]);
		}
		RString error= "Error running " + m_load_path + loader_set[i] + ": ";
		lua_pushvalue(L, button_list_index);
		if(!LuaHelpers::RunScriptOnStack(L, error, 1, 1, true))
		{
			RETURN_NOT_SANE("Error running loader " + loader_set[i]);
		}
		if(!temp_dest[i].load_from_lua(L, lua_gettop(L), columns, sub_sanity))
		{
			RETURN_NOT_SANE("Error in layer data: " + sub_sanity);
		}
	}
#undef RETURN_NOT_SANE
	dest.swap(temp_dest);
	lua_settop(L, original_top);
	return true;
}

bool NewSkinLoader::load_into_data(vector<string> const& button_list,
	NewSkinData& dest, string& insanity_diagnosis)
{
	Lua* L= LUA->Get();
	int original_top= lua_gettop(L);
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); LUA->Release(L); insanity_diagnosis= message; return false;
	lua_createtable(L, button_list.size(), 0);
	for(size_t b= 0; b < button_list.size(); ++b)
	{
		lua_pushstring(L, button_list[b].c_str());
		lua_rawseti(L, -2, b+1);
	}
	int button_list_index= lua_gettop(L);
	if(!push_loader_function(L, m_notes_loader))
	{
		RETURN_NOT_SANE("Could not load tap loader.");
	}
	RString error= "Error running " + m_load_path + m_notes_loader + ": ";
	lua_pushvalue(L, button_list_index);
	if(!LuaHelpers::RunScriptOnStack(L, error, 1, 1, true))
	{
		RETURN_NOT_SANE("Error running loader for notes.");
	}
	string sub_sanity;
	if(!dest.load_taps_from_lua(L, lua_gettop(L), button_list.size(), m_load_path, sub_sanity))
	{
		RETURN_NOT_SANE("Invalid data from loader: " + sub_sanity);
	}
	if(!load_layer_set_into_data(L, button_list_index, button_list.size(), m_below_loaders,
			dest.m_layers_below_notes, sub_sanity))
	{
		RETURN_NOT_SANE("Error running below loaders: " + sub_sanity);
	}
	if(!load_layer_set_into_data(L, button_list_index, button_list.size(), m_above_loaders,
			dest.m_layers_above_notes, sub_sanity))
	{
		RETURN_NOT_SANE("Error running above loaders: " + sub_sanity);
	}
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	LUA->Release(L);
	return true;
}


REGISTER_ACTOR_CLASS(NewFieldColumn);

NewFieldColumn::NewFieldColumn()
	:m_quantization_multiplier(&m_mod_manager, 1.0),
	 m_quantization_offset(&m_mod_manager, 0.0),
	 m_pos_mod(&m_mod_manager, 0.0), m_rot_mod(&m_mod_manager, 0.0),
	 m_zoom_mod(&m_mod_manager, 1.0),
	 m_curr_beat(0.0f), m_pixels_visible_before_beat(128.0f),
	 m_pixels_visible_after_beat(1024.0f),
	 m_newskin(nullptr), m_note_data(nullptr), m_timing_data(nullptr)
{
	m_quantization_multiplier.get_value().set_value_instant(1.0);
}

NewFieldColumn::~NewFieldColumn()
{}

void NewFieldColumn::set_column_info(size_t column, NewSkinColumn* newskin,
	const NoteData* note_data, const TimingData* timing_data, double x)
{
	m_column= column;
	m_newskin= newskin;
	m_note_data= note_data;
	m_timing_data= timing_data;
	SetX(x);
	m_use_game_music_beat= true;
}

void NewFieldColumn::update_displayed_beat(double beat)
{
	if(m_use_game_music_beat)
	{
		m_curr_beat= beat;
	}
}

double NewFieldColumn::calc_y_offset_for_beat(double beat)
{
	return beat * note_size * speed_multiplier;
}

double NewFieldColumn::calc_beat_for_y_offset(double y_offset)
{
	return y_offset / (speed_multiplier * note_size);
}

void NewFieldColumn::calc_transform_for_beat(double beat, transform& trans)
{
	mod_val_inputs input(beat, 0.0, m_curr_beat, 0.0);
	m_pos_mod.evaluate(input, trans.pos);
	m_rot_mod.evaluate(input, trans.rot);
	m_zoom_mod.evaluate(input, trans.zoom);
}

void NewFieldColumn::UpdateInternal(float delta)
{
	if(!m_use_game_music_beat)
	{
		m_curr_beat+= delta;
	}
	m_mod_manager.update(delta);
	ActorFrame::UpdateInternal(delta);
}

struct strip_buffer
{
	enum { size= 512 };
	RageSpriteVertex* buf;
	RageSpriteVertex* v;
	strip_buffer()
	{
		buf= (RageSpriteVertex*) malloc(size * sizeof(RageSpriteVertex));
		init();
	}
	~strip_buffer()
	{
		free(buf);
	}
	void init()
	{
		v= buf;
	}
	void rollback()
	{
		// The buffer is full and has just been drawn, and more verts need to be
		// added to draw.  Move the last three verts to the beginning of the
		// buffer so that the vert calculating loop doesn't have to redo the work
		// for them.
		if(used() > 3)
		{
			buf[0]= v[-3];
			buf[1]= v[-2];
			buf[2]= v[-1];
			v= buf + 3;
		}
	}
	void draw()
	{
		DISPLAY->DrawSymmetricQuadStrip(buf, v-buf);
	}
	int used() const { return v - buf; }
	int avail() const { return size - used(); }
	void add_vert(RageVector3 const& pos, RageColor const& color, RageVector2 const& texcoord)
	{
		v->p= pos;  v->c= color;  v->t= texcoord;
		v+= 1;
	}
};

enum hold_tex_phase
{
	HTP_Top,
	HTP_Body,
	HTP_Bottom,
	HTP_Done
};

struct hold_texture_handler
{
	// Things that would be const if the calculations didn't force them to be
	// non-const.
	double tex_top;
	double tex_bottom;
	double tex_rect_h;
	double tex_cap_height;
	double tex_body_height;
	double tex_cap_end;
	double tex_body_end;
	double tex_per_y;
	double start_y;
	double body_start_y;
	double body_end_y;
	double end_y;
	// Things that will be changed/updated each time.
	double prev_partial;
	int prev_phase;
	bool started_bottom;
	hold_texture_handler(double const note_size, double const y, double const len, double const tex_t, double const tex_b)
	{
		tex_top= tex_t;
		tex_bottom= tex_b;
		tex_rect_h= tex_bottom - tex_top;
		tex_cap_height= tex_rect_h / 6.0;
		tex_body_height= tex_rect_h / 3.0;
		tex_cap_end= tex_top + tex_cap_height;
		tex_body_end= tex_bottom - tex_cap_height;
		tex_per_y= tex_body_height / note_size;
		start_y= y - (note_size * .5);
		body_start_y= y;
		body_end_y= y + (len * note_size);
		end_y= body_end_y + (note_size * .5);
		// constants go above this line.
		prev_partial= 2.0;
		prev_phase= HTP_Top;
		started_bottom= false;
	}
	// curr_y will be modified on the transition to HTP_Bottom to make sure the
	// entire bottom is drawn.
	// The hold is drawn in several phases.  Each phase must be drawn in full,
	// so when transitioning from one phase to the next, two texture coords are
	// calculated, one with the previous phase and one with the current.  This
	// compresses the seam between phases to zero, making it invisible.
	// The texture coords are bottom aligned so that the end of the last body
	// lines up with the start of the bottom cap.
	int calc_tex_y(double& curr_y, vector<double>& ret_texc)
	{
		int phase= HTP_Top;
		if(curr_y >= end_y)
		{
			curr_y= end_y;
			ret_texc.push_back(tex_bottom);
			phase= HTP_Done;
		}
		else if(curr_y >= body_end_y)
		{
			if(started_bottom)
			{
				phase= HTP_Bottom;
			}
			else
			{
				curr_y= body_end_y;
				phase= HTP_Bottom;
				started_bottom= true;
			}
		}
		else if(curr_y >= body_start_y)
		{
			phase= HTP_Body;
		}
		if(phase != HTP_Done)
		{
			if(phase != prev_phase)
			{
				internal_calc_tex_y(prev_phase, curr_y, ret_texc);
			}
			internal_calc_tex_y(phase, curr_y, ret_texc);
			prev_phase= phase;
		}
		return phase;
	}
private:
	void internal_calc_tex_y(int phase, double& curr_y, vector<double>& ret_texc)
	{
		switch(phase)
		{
			case HTP_Top:
				ret_texc.push_back(tex_top + ((curr_y - start_y) * tex_per_y));
				break;
			case HTP_Body:
				// In the body phase, the first half of the body section of the
				// texture is repeated over the length of the hold.
				{
					double const tex_distance= (body_end_y - curr_y) * tex_per_y;
					// bodies_left decreases as more of the hold is drawn.
					double const bodies_left= tex_distance / tex_body_height;
					double const floor_left= floor(bodies_left);
					// Renge-chan of bodies_left - floor_left: (1.0, 0.0]
					// Renge-chan that we need: [0.0, 1.0)
					double partial= 1.0 - (bodies_left - floor_left);
					if(partial == 1.0)
					{
						partial= 0.0;
					}
					double curr_tex_y= tex_cap_end + (partial * tex_body_height);
					// When rendering in the body phase, detect when the body is being
					// repeated and insert an extra tex coord to cover the seam.
					if(partial < prev_partial)
					{
						ret_texc.push_back(curr_tex_y + tex_body_height);
					}
					ret_texc.push_back(curr_tex_y);
					prev_partial= partial;
				}
				break;
			case HTP_Bottom:
				ret_texc.push_back(((curr_y-body_end_y) * tex_per_y) + tex_body_end);
				break;
		}
	}
};

static void add_vert_strip(float const tex_y, strip_buffer& verts_to_draw,
	RageVector3 const& left, RageVector3 const& center,
	RageVector3 const& right, RageColor const& color,
	float const tex_left, double const tex_center, float const tex_right)
{
	verts_to_draw.add_vert(left, color, RageVector2(tex_left, tex_y));
	verts_to_draw.add_vert(center, color, RageVector2(tex_center, tex_y));
	verts_to_draw.add_vert(right, color, RageVector2(tex_right, tex_y));
}

void NewFieldColumn::draw_hold(QuantizedHoldRenderData& data, double x, double y, double len)
{
	static strip_buffer verts_to_draw;
	verts_to_draw.init();
	static const double y_step= 4.0;
	double tex_top= data.rect->top;
	double tex_bottom= data.rect->bottom;
	double tex_left= data.rect->left;
	double tex_right= data.rect->right;
	switch(data.flip)
	{
		case TCFM_X:
			std::swap(tex_left, tex_right);
			break;
		case TCFM_XY:
			std::swap(tex_left, tex_right);
			std::swap(tex_top, tex_bottom);
			break;
		case TCFM_Y:
			std::swap(tex_top, tex_bottom);
			break;
		default:
			break;
	}
	hold_texture_handler tex_handler(note_size, y, len, tex_top, tex_bottom);
	double const tex_center= (tex_left + tex_right) * .5;
	DISPLAY->ClearAllTextures();
	bool last_vert_set= false;
	vector<double> tex_coords;
	for(double curr_y= tex_handler.start_y; !last_vert_set; curr_y+= y_step)
	{
		tex_coords.clear();
		int phase= tex_handler.calc_tex_y(curr_y, tex_coords);
		if(phase == HTP_Done)
		{
			last_vert_set= true;
		}

		transform trans;
		calc_transform_for_beat(m_curr_beat + calc_beat_for_y_offset(curr_y), trans);

		const RageVector3 left_vert(
			x - (note_size * .5) + trans.pos.x, curr_y + trans.pos.y,
			0 + trans.pos.z);
		const RageVector3 center_vert(x + trans.pos.x, curr_y + trans.pos.y, 0 + trans.pos.z);
		const RageVector3 right_vert(x + (note_size * .5) + trans.pos.x, curr_y + trans.pos.y, 0 + trans.pos.z);
		const RageColor color(1.0, 1.0, 1.0, 1.0);
#define add_vert_strip_args verts_to_draw, left_vert, center_vert, right_vert, color, tex_left, tex_center, tex_right
		for(size_t i= 0; i < tex_coords.size(); ++i)
		{
			add_vert_strip(tex_coords[i], add_vert_strip_args);
		}
#undef add_vert_strip_args
		if(verts_to_draw.avail() < 9 || last_vert_set)
		{
			for(size_t t= 0; t < data.parts.size(); ++t)
			{
				DISPLAY->SetTexture(TextureUnit_1, data.parts[t]->GetTexHandle());
				DISPLAY->SetBlendMode(t == 0 ? BLEND_NORMAL : BLEND_ADD);
				DISPLAY->SetCullMode(CULL_NONE);
				DISPLAY->SetTextureWrapping(TextureUnit_1, false);
				verts_to_draw.draw();
			}
			verts_to_draw.rollback();
		}
	}
}

bool NewFieldColumn::EarlyAbortDraw() const
{
	return m_newskin == nullptr || m_note_data == nullptr || m_timing_data == nullptr;
}

void NewFieldColumn::update_upcoming(int row, double dist_factor)
{
	double const dist= (NoteRowToBeat(row) - m_curr_beat) * dist_factor;
	if(dist > 0 && dist < m_status.dist_to_upcoming_arrow)
	{
		m_status.dist_to_upcoming_arrow= dist;
	}
}

void NewFieldColumn::update_active_hold(TapNote const& tap)
{
	if(tap.subType != TapNoteSubType_Invalid && tap.HoldResult.bActive)
	{
		m_status.active_hold= &tap;
	}
}

double NewFieldColumn::get_hold_draw_beat(TapNote const& tap, double const hold_beat)
{
	double const last_held= tap.HoldResult.GetLastHeldBeat();
	if(last_held > hold_beat)
	{
		if(fabs(last_held - m_curr_beat) < .01)
		{
			return m_curr_beat;
		}
		return last_held;
	}
	return hold_beat;
}

void NewFieldColumn::DrawPrimitives()
{
	m_status.dist_to_upcoming_arrow= 1000.0;
	m_status.prev_active_hold= m_status.active_hold;
	m_status.active_hold= nullptr;
	// Holds and taps are put into different lists because they have to be
	// rendered in different phases.  All hold bodies must be drawn first, then
	// all taps, so the taps appear on top of the hold bodies and are not
	// obscured.
	vector<NoteData::TrackMap::const_iterator> holds;
	vector<NoteData::TrackMap::const_iterator> taps;
	NoteData::TrackMap::const_iterator begin, end;
	double first_beat= m_curr_beat - calc_beat_for_y_offset(m_pixels_visible_before_beat);
	double last_beat= m_curr_beat + calc_beat_for_y_offset(m_pixels_visible_after_beat);
	double dist_factor= 1.0 / (last_beat - m_curr_beat);
	m_note_data->GetTapNoteRangeInclusive(m_column, BeatToNoteRow(first_beat),
		BeatToNoteRow(last_beat), begin, end);
	for(; begin != end; ++begin)
	{
		const TapNote& tn= begin->second;
		switch(tn.type)
		{
			case TapNoteType_Empty:
				continue;
			case TapNoteType_Tap:
			case TapNoteType_Mine:
			case TapNoteType_Lift:
			case TapNoteType_Attack:
			case TapNoteType_AutoKeysound:
			case TapNoteType_Fake:
				if(!tn.result.bHidden)
				{
					taps.push_back(begin);
				}
				break;
			case TapNoteType_HoldHead:
				if(tn.HoldResult.hns != HNS_Held)
				{
					// Hold heads are added to the tap list to take care of rendering
					// heads and tails in the same phase as taps.
					taps.push_back(begin);
					holds.push_back(begin);
				}
				break;
			default:
				break;
		}
	}
	double const beat= m_curr_beat - floor(m_curr_beat);
	for(auto&& holdit : holds)
	{
		// The hold loop does not need to call update_upcoming or
		// update_active_hold beccause the tap loop handles them when drawing
		// heads.
		int hold_row= holdit->first;
		TapNote const& tn= holdit->second;
		double const hold_beat= NoteRowToBeat(hold_row);
		double const quantization= quantization_for_beat(hold_beat);
		bool active= tn.HoldResult.bActive && tn.HoldResult.fLife > 0.0f;
		QuantizedHoldRenderData data;
		m_newskin->get_hold_render_data(tn.subType, active, quantization, beat, data);
		double hold_draw_beat= get_hold_draw_beat(tn, hold_beat);
		double passed_amount= hold_draw_beat - hold_beat;
		if(!data.parts.empty())
		{
			double y= calc_y_offset_for_beat(hold_draw_beat - m_curr_beat);
			draw_hold(data, 0.0, y, (NoteRowToBeat(tn.iDuration) - passed_amount)
				* speed_multiplier);
		}
	}
	for(auto&& tapit : taps)
	{
		int tap_row= tapit->first;
		TapNote const& tn= tapit->second;
		update_upcoming(tap_row, dist_factor);
		update_active_hold(tn);
		double const tap_beat= NoteRowToBeat(tap_row);
		double const quantization= quantization_for_beat(tap_beat);
		NewSkinTapPart part= NSTP_Tap;
		NewSkinTapOptionalPart head_part= NewSkinTapOptionalPart_Invalid;
		NewSkinTapOptionalPart tail_part= NewSkinTapOptionalPart_Invalid;
		double head_beat;
		double tail_beat;
		switch(tn.type)
		{
			case TapNoteType_Mine:
				part= NSTP_Mine;
				head_beat= tap_beat;
				break;
			case TapNoteType_Lift:
				part= NSTP_Lift;
				head_beat= tap_beat;
				break;
			case TapNoteType_HoldHead:
				part= NewSkinTapPart_Invalid;
				head_beat= get_hold_draw_beat(tn, tap_beat);
				tail_beat= m_curr_beat + NoteRowToBeat(tn.iDuration);
				switch(tn.subType)
				{
					case TapNoteSubType_Hold:
						head_part= NSTOP_HoldHead;
						tail_part= NSTOP_HoldTail;
						break;
					case TapNoteSubType_Roll:
						head_part= NSTOP_RollHead;
						tail_part= NSTOP_RollTail;
						break;
						/* TODO: Implement checkpoint holds as a subtype.  This code
							 makes the noteskin support it, but support is needed in other
							 areas.
					case TapNoteSubType_Checkpoint:
						head_part= NSTOP_CheckpointHead;
						tail_part= NSTOP_CheckpointTail;
						break;
						*/
					default:
						break;
				}
				break;
			default:
				part= NSTP_Tap;
				head_beat= tap_beat;
				break;
		}
		double draw_beat= head_beat;
		vector<Actor*> acts;
		if(part != NewSkinTapPart_Invalid)
		{
			acts.push_back(m_newskin->get_tap_actor(part, quantization, beat));
		}
		else
		{
			// Put tails on the list first because they need to be under the heads.
			draw_beat= tail_beat;
			acts.push_back(m_newskin->get_optional_actor(tail_part, quantization, beat));
			acts.push_back(m_newskin->get_optional_actor(head_part, quantization, beat));
		}
		for(auto&& act : acts)
		{
			// Tails are optional, get_optional_actor returns nullptr if the
			// noteskin doesn't have them.
			if(act != nullptr)
			{
				transform trans;
				calc_transform_for_beat(draw_beat, trans);
				trans.pos.y+= calc_y_offset_for_beat(draw_beat - m_curr_beat);
				act->set_transform(trans);
				act->Draw();
			}
			draw_beat= head_beat;
		}
	}
	ActorFrame::DrawPrimitives();
}

REGISTER_ACTOR_CLASS(NewField);

NewField::NewField()
	:m_own_note_data(false), m_note_data(nullptr), m_timing_data(nullptr)
{
	m_skin_walker.load_from_file(SpecialFiles::NEWSKINS_DIR + "default/noteskin.lua");
}

NewField::~NewField()
{
	if(m_own_note_data && m_note_data != nullptr)
	{
		SAFE_DELETE(m_note_data);
	}
}

void NewField::UpdateInternal(float delta)
{
	m_newskin.update_all_layers(delta);
	for(auto&& col : m_columns)
	{
		col.Update(delta);
	}
	ActorFrame::UpdateInternal(delta);
}

bool NewField::EarlyAbortDraw() const
{
	return m_note_data == nullptr || m_note_data->IsEmpty() || m_timing_data == nullptr || m_columns.empty() || !m_newskin.loaded_successfully() || ActorFrame::EarlyAbortDraw();
}

void NewField::DrawPrimitives()
{
	vector<transform> column_trans(m_columns.size());
	for(size_t c= 0; c < m_columns.size(); ++c)
	{
		m_columns[c].calc_transform_for_head(column_trans[c]);
	}
	m_newskin.transform_columns(column_trans);
	draw_layer_set(m_newskin.m_layers_below_notes);
	for(size_t c= 0; c < m_columns.size(); ++c)
	{
		m_columns[c].Draw();
		set_note_upcoming(c, m_columns[c].m_status.dist_to_upcoming_arrow);
		// The hold status should be updated if there is a currently active hold
		// or if there was one last frame.
		if(m_columns[c].m_status.active_hold != nullptr ||
			m_columns[c].m_status.prev_active_hold != nullptr)
		{
			bool curr_is_null= m_columns[c].m_status.active_hold == nullptr;
			bool prev_is_null= m_columns[c].m_status.prev_active_hold == nullptr;
			TapNote const* pass= curr_is_null ? m_columns[c].m_status.prev_active_hold : m_columns[c].m_status.active_hold;
			set_hold_status(c, pass, prev_is_null, curr_is_null);
		}
	}
	draw_layer_set(m_newskin.m_layers_above_notes);
	ActorFrame::DrawPrimitives();
}

void NewField::draw_layer_set(std::vector<NewSkinLayer>& layers)
{
	for(auto&& lay : layers)
	{
		lay.Draw();
	}
}

void NewField::push_columns_to_lua(lua_State* L)
{
	lua_createtable(L, m_columns.size(), 0);
	for(size_t c= 0; c < m_columns.size(); ++c)
	{
		m_columns[c].PushSelf(L);
		lua_rawseti(L, -2, c+1);
	}
}

void NewField::clear_steps()
{
	if(m_own_note_data)
	{
		m_note_data->ClearAll();
	}
	m_note_data= nullptr;
	m_timing_data= nullptr;
	m_columns.clear();
}

void NewField::set_steps(Steps* data)
{
	if(data == nullptr)
	{
		clear_steps();
		return;
	}
	// TODO:  Remove the dependence on the current game.  A notefield should be
	// able to show steps of any stepstype.
	const Game* curr_game= GAMESTATE->GetCurrentGame();
	const Style* curr_style= GAMEMAN->GetFirstCompatibleStyle(curr_game, 1, data->m_StepsType);
	if(curr_style == nullptr)
	{
		curr_style= GAMEMAN->GetFirstCompatibleStyle(curr_game, 2, data->m_StepsType);
	}
	if(curr_style == nullptr)
	{
		clear_steps();
		return;
	}
	NoteData* note_data= new NoteData;
	data->GetNoteData(*note_data);
	set_note_data(note_data, data->GetTimingData(), curr_style);
	m_own_note_data= true;
}

// TODO:  Move the button lists for stepstypes to lua data files.
// This hardcoded list is just temporary so that noteskins can be made and
// tested while other areas are under construction.  My plan is to get rid of
// styles and move all stepstype data to lua files to be loaded at startup.
static vector<vector<string> > button_lists = {
// StepsType_dance_single,
	{"Left", "Down", "Up", "Right"},
// StepsType_dance_double,
	{"Left", "Down", "Up", "Right", "Left", "Down", "Up", "Right"},
// StepsType_dance_couple,
	{"Left", "Down", "Up", "Right", "Left", "Down", "Up", "Right"},
// StepsType_dance_solo,
	{"Left", "UpLeft", "Down", "Up", "UpRight", "Right"},
// StepsType_dance_threepanel,
	{"Left", "Down", "Right"},
// StepsType_dance_routine,
	{"Left", "Down", "Up", "Right", "Left", "Down", "Up", "Right"},
// StepsType_pump_single,
	{"DownLeft", "UpLeft", "Center", "UpRight", "DownRight"},
// StepsType_pump_halfdouble,
	{"Center", "UpRight", "DownRight", "DownLeft", "UpLeft", "Center"},
// StepsType_pump_double,
	{"DownLeft", "UpLeft", "Center", "UpRight", "DownRight", "DownLeft", "UpLeft", "Center", "UpRight", "DownRight"},
// StepsType_pump_couple,
	{"DownLeft", "UpLeft", "Center", "UpRight", "DownRight", "DownLeft", "UpLeft", "Center", "UpRight", "DownRight"},
// StepsType_pump_routine,
	{"DownLeft", "UpLeft", "Center", "UpRight", "DownRight", "DownLeft", "UpLeft", "Center", "UpRight", "DownRight"},
// StepsType_kb7_single,
	{"Key1", "Key2", "Key3", "Key4", "Key5", "Key6", "Key7"},
	// ez2 buttons are probably wrong because the button mapping logic in Style
	// is too convoluted.
// StepsType_ez2_single,
	{"FootUpLeft", "HandUpLeft", "FootDown", "HandUpRight", "FootUpRight"},
// StepsType_ez2_double,
	{"FootUpLeft", "HandUpLeft", "FootDown", "HandUpRight", "FootUpRight", "FootUpLeft", "HandUpLeft", "FootDown", "HandUpRight", "FootUpRight"},
// StepsType_ez2_real,
	{"FootUpLeft", "HandLrLeft", "HandUpLeft", "FootDown", "HandUpRight", "HandLrRight", "FootUpRight"},
// StepsType_para_single,
	{"Left", "UpLeft", "Up", "UpRight", "Right"},
// StepsType_ds3ddx_single,
	{"HandLeft", "FootDownLeft", "FootUpLeft", "HandUp", "HandDown", "FootUpRight", "FootDownRight", "HandRight"},
// StepsType_beat_single5,
	{"Scratch up", "Key1", "Key2", "Key3", "Key4", "Key5"},
// StepsType_beat_versus5,
	{"Scratch up", "Key1", "Key2", "Key3", "Key4", "Key5"},
// StepsType_beat_double5,
	{"Scratch up", "Key1", "Key2", "Key3", "Key4", "Key5", "Key5", "Key4", "Key3", "Key2", "Key1", "Scratch up"},
// StepsType_beat_single7,
	{"Scratch up", "Key1", "Key2", "Key3", "Key4", "Key5", "Key6", "Key7"},
// StepsType_beat_versus7,
	{"Scratch up", "Key1", "Key2", "Key3", "Key4", "Key5", "Key6", "Key7"},
// StepsType_beat_double7,
	{"Scratch up", "Key1", "Key2", "Key3", "Key4", "Key5", "Key6", "Key7", "Key7", "Key6", "Key5", "Key4", "Key3", "Key2", "Key1", "Scratch up"},
// StepsType_maniax_single,
	{"HandLrLeft", "HandUpLeft", "HandUpRight", "HandLrRight"},
// StepsType_maniax_double,
	{"HandLrLeft", "HandUpLeft", "HandUpRight", "HandLrRight", "HandLrLeft", "HandUpLeft", "HandUpRight", "HandLrRight"},
// StepsType_techno_single4,
	{"Left", "Down", "Up", "Right"},
// StepsType_techno_single5,
	{"DownLeft", "UpLeft", "Center", "UpRight", "DownRight"},
// StepsType_techno_single8,
	{"DownLeft", "Left", "UpLeft", "Down", "Up", "UpRight", "Right", "DownRight"},
// StepsType_techno_double4,
	{"Left", "Down", "Up", "Right", "Left", "Down", "Up", "Right"},
// StepsType_techno_double5,
	{"DownLeft", "UpLeft", "Center", "UpRight", "DownRight", "DownLeft", "UpLeft", "Center", "UpRight", "DownRight"},
// StepsType_techno_double8,
	{"DownLeft", "Left", "UpLeft", "Down", "Up", "UpRight", "Right", "DownRight", "DownLeft", "Left", "UpLeft", "Down", "Up", "UpRight", "Right", "DownRight"},
// StepsType_popn_five,
	{"Left Green", "Left Blue", "Red", "Right Blue", "Right Green"},
// StepsType_popn_nine,
	{"Left White", "Left Yellow", "Left Green", "Left Blue", "Red", "Right Blue", "Right Green", "Right Yellow", "Right White"},
// StepsType_lights_cabinet,
	{"MarqueeUpLeft", "MarqueeUpRight", "MarqueeLrLeft", "MarqueeLrRight", "ButtonsLeft", "ButtonsRight", "BassLeft", "BassRight"},
// StepsType_kickbox_human,
	{"LeftFoot", "LeftFist", "RightFist", "RightFoot"},
// StepsType_kickbox_quadarm,
	{"UpLeftFist", "DownLeftFist", "DownRightFist", "UpRightFist"},
// StepsType_kickbox_insect,
	{"LeftFoot", "UpLeftFist", "DownLeftFist", "DownRightFist", "UpRightFist", "RightFoot"},
// StepsType_kickbox_arachnid,
	{"DownLeftFoot", "UpLeftFoot", "UpLeftFist", "DownLeftFist", "DownRightFist", "UpRightFist", "UpRightFoot", "DownRightFoot"}
};

void NewField::set_note_data(NoteData* note_data, TimingData const* timing, Style const* curr_style)
{
	m_note_data= note_data;
	m_own_note_data= false;
	StepsType button_stype= curr_style->m_StepsType;
	// TODO:  Prevent or handle better the case of a style without a button list.
	if(button_stype >= button_lists.size())
	{
		button_stype= StepsType_dance_single;
	}
	vector<string>& button_list= button_lists[button_stype];
	if(!m_skin_walker.supports_needed_buttons(button_list))
	{
		LuaHelpers::ReportScriptError("The noteskin does not support the required buttons.");
		return;
	}
	string insanity;
	if(!m_skin_walker.load_into_data(button_list, m_newskin, insanity))
	{
		LuaHelpers::ReportScriptError("Error loading noteskin: " + insanity);
		return;
	}
	const Style::ColumnInfo* colinfo= curr_style->m_ColumnInfo[1];
	m_timing_data= timing;
	m_columns.clear();
	m_columns.resize(m_note_data->GetNumTracks());
	// Temporary until styles are removed.
	for(size_t i= 0; i < m_columns.size(); ++i)
	{
		m_columns[i].set_column_info(i, m_newskin.get_column(i), m_note_data,
			m_timing_data, colinfo[i].fXOffset);
	}
}

void NewField::update_displayed_beat(double beat)
{
	for(auto&& col : m_columns)
	{
		col.update_displayed_beat(beat);
	}
}

static Message create_did_message(size_t column, bool bright)
{
	Message msg("ColumnJudgment");
	msg.SetParam("column", column);
	msg.SetParam("bright", bright);
	return msg;
}

void NewField::did_tap_note(size_t column, TapNoteScore tns, bool bright)
{
	Message msg(create_did_message(column, bright));
	msg.SetParam("tap_note_score", tns);
	m_newskin.pass_message_to_all_layers(column, msg);
}

void NewField::did_hold_note(size_t column, HoldNoteScore hns, bool bright)
{
	Message msg(create_did_message(column, bright));
	msg.SetParam("hold_note_score", hns);
	m_newskin.pass_message_to_all_layers(column, msg);
}

void NewField::set_hold_status(size_t column, TapNote const* tap, bool start, bool end)
{
	Message msg("Hold");
	if(tap != nullptr)
	{
		msg.SetParam("type", tap->subType);
		msg.SetParam("life", tap->HoldResult.fLife);
		msg.SetParam("start", start);
		msg.SetParam("finished", end);
	}
	m_newskin.pass_message_to_all_layers(column, msg);
}

void NewField::set_pressed(size_t column, bool on)
{
	Message msg("Pressed");
	msg.SetParam("on", on);
	m_newskin.pass_message_to_all_layers(column, msg);
}

void NewField::set_note_upcoming(size_t column, double distance)
{
	Message msg("Upcoming");
	msg.SetParam("distance", distance);
	m_newskin.pass_message_to_all_layers(column, msg);
}


// lua start
struct LunaNewFieldColumn : Luna<NewFieldColumn>
{
	static int get_quantization_multiplier(T* p, lua_State* L)
	{
		p->m_quantization_multiplier.PushSelf(L);
		return 1;
	}
	static int get_quantization_offset(T* p, lua_State* L)
	{
		p->m_quantization_offset.PushSelf(L);
		return 1;
	}
#define GET_MOD(dim, part) \
	static int get_##dim##_##part##_mod(T* p, lua_State* L) \
	{ \
		p->m_##part##_mod.dim##_mod.PushSelf(L); \
		return 1; \
	}
#define GET_MOD_SET(part) GET_MOD(x, part); GET_MOD(y, part); GET_MOD(z, part);
	GET_MOD_SET(pos);
	GET_MOD_SET(rot);
	GET_MOD_SET(zoom);
#undef GET_MOD_SET
#undef GET_MOD
#define ADD_MOD_SET(part) ADD_METHOD(get_x_##part##_mod); ADD_METHOD(get_y_##part##_mod); ADD_METHOD(get_z_##part##_mod);
	LunaNewFieldColumn()
	{
		ADD_METHOD(get_quantization_multiplier);
		ADD_METHOD(get_quantization_offset);
		ADD_MOD_SET(pos);
		ADD_MOD_SET(rot);
		ADD_MOD_SET(zoom);
	}
#undef ADD_MOD_SET
};
LUA_REGISTER_DERIVED_CLASS(NewFieldColumn, ActorFrame);

struct LunaNewField : Luna<NewField>
{
	static int get_curr_beat(T* p, lua_State* L)
	{
		return 1;
	}
	static int set_curr_beat(T* p, lua_State* L)
	{
		COMMON_RETURN_SELF;
	}
	static int set_speed(T* p, lua_State* L)
	{
		speed_multiplier= FArg(1);
		COMMON_RETURN_SELF;
	}
	static int set_steps(T* p, lua_State* L)
	{
		Steps* data= Luna<Steps>::check(L, 1);
		p->set_steps(data);
		COMMON_RETURN_SELF;
	}
	static int get_columns(T* p, lua_State* L)
	{
		p->push_columns_to_lua(L);
		return 1;
	}
	LunaNewField()
	{
		ADD_METHOD(set_speed);
		ADD_GET_SET_METHODS(curr_beat);
		ADD_METHOD(set_steps);
		ADD_METHOD(get_columns);
	}
};
LUA_REGISTER_DERIVED_CLASS(NewField, ActorFrame);
