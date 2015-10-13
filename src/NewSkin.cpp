#include "global.h"

#include "ActorUtil.h"
#include "NewSkin.h"
#include "NewSkinManager.h"
#include "RageFileManager.h"
#include "RageTextureManager.h"
#include "XmlFile.h"
#include "XmlFileUtil.h"

using std::max;
using std::string;
using std::unordered_set;
using std::vector;

static const double default_column_width= 64.0;
static const double default_column_padding= 0.0;

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

static size_t get_table_len(lua_State* L, int index, size_t max_entries,
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
static bool load_simple_table(lua_State* L, int index, size_t max_entries,
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

static bool load_string_table(lua_State* L, int index, size_t max_entries,
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
static void load_enum_table(lua_State* L, int index, en_type begin, en_type end,
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
	for(int curr= begin; curr != end; ++curr)
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

bool QuantizedHold::load_from_lua(lua_State* L, int index, NewSkinLoader const* load_skin, string& insanity_diagnosis)
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
			RString resolved= NEWSKIN->get_path_to_file_in_skin(load_skin, path);
			if(resolved.empty())
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
	lua_getfield(L, index, "length_data");
	if(lua_istable(L, -1))
	{
		int length_data_index= lua_gettop(L);
		m_part_lengths.start_note_offset= get_optional_double(L, length_data_index, "start_note_offset", -.5);
		m_part_lengths.end_note_offset= get_optional_double(L, length_data_index, "end_note_offset", .5);
		m_part_lengths.head_pixs= get_optional_double(L, length_data_index, "head_pixs", 32.0);
		m_part_lengths.body_pixs= get_optional_double(L, length_data_index, "body_pixs", 64.0);
		m_part_lengths.tail_pixs= get_optional_double(L, length_data_index, "tail_pixs", 32.0);
	}
	else
	{
		m_part_lengths.start_note_offset= -.5;
		m_part_lengths.end_note_offset= .5;
		m_part_lengths.head_pixs= 32.0;
		m_part_lengths.body_pixs= 64.0;
		m_part_lengths.tail_pixs= 32.0;
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

void NewSkinColumn::get_hold_render_data(TapNoteSubType sub_type,
	bool active, bool reverse, double quantization, double beat,
	QuantizedHoldRenderData& data)
{
	if(sub_type >= NUM_TapNoteSubType)
	{
		data.clear();
		return;
	}
	if(!reverse)
	{
		m_holds[sub_type][active].get_quantized(quantization, beat, data);
	}
	else
	{
		m_reverse_holds[sub_type][active].get_quantized(quantization, beat, data);
	}
}

bool NewSkinColumn::load_holds_from_lua(lua_State* L, int index,
	std::vector<std::vector<QuantizedHold> >& holder,
	std::string const& holds_name,
	NewSkinLoader const* load_skin, std::string& insanity_diagnosis)
{
	string sub_sanity;
	int original_top= lua_gettop(L);
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	lua_getfield(L, index, holds_name.c_str());
	if(!lua_istable(L, -1))
	{
		RETURN_NOT_SANE("No " + holds_name + " given.");
	}
	int holds_index= lua_gettop(L);
	holder.resize(NUM_TapNoteSubType);
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
		holder[part].resize(num_active_states);
		for(size_t a= 0; a < num_active_states; ++a)
		{
			lua_rawgeti(L, actives_index, a+1);
			if(!lua_istable(L, -1))
			{
				RETURN_NOT_SANE(ssprintf("Hold info not given for active state %zu of subtype %s.", a, TapNoteSubTypeToString(static_cast<TapNoteSubType>(part)).c_str()));
			}
			if(!holder[part][a].load_from_lua(L, lua_gettop(L), load_skin, sub_sanity))
			{
				RETURN_NOT_SANE(ssprintf("Error loading active state %zu of subtype %s: %s", a, TapNoteSubTypeToString(static_cast<TapNoteSubType>(part)).c_str(), sub_sanity.c_str()));
			}
		}
	}
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	return true;
}

bool NewSkinColumn::load_from_lua(lua_State* L, int index, NewSkinLoader const* load_skin, string& insanity_diagnosis)
{
	// Pop the table we're loading from off the stack when returning.
	int original_top= lua_gettop(L) - 1;
#define RETURN_NOT_SANE(message) lua_settop(L, original_top); insanity_diagnosis= message; return false;
	vector<QuantizedTap> temp_taps;
	vector<QuantizedTap*> temp_optionals(NUM_NewSkinTapOptionalPart, nullptr);
	vector<vector<QuantizedHold> > temp_holds;
	vector<vector<QuantizedHold> > temp_reverse_holds;
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
	if(!load_holds_from_lua(L, index, temp_holds, "holds", load_skin,
			insanity_diagnosis))
	{
		RETURN_NOT_SANE(insanity_diagnosis);
	}
	if(!load_holds_from_lua(L, index, temp_reverse_holds, "reverse_holds", load_skin,
			insanity_diagnosis))
	{
		RETURN_NOT_SANE(insanity_diagnosis);
	}
	lua_getfield(L, index, "rotations");
	load_enum_table(L, lua_gettop(L), NSTP_Tap, NUM_NewSkinTapPart,
		temp_rotations, 0.0, 1000.0, 0.0);
	m_width= get_optional_double(L, index, "width", default_column_width);
	m_padding= get_optional_double(L, index, "padding", default_column_padding);
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	m_taps.swap(temp_taps);
	clear_optionals();
	m_optional_taps.swap(temp_optionals);
	m_holds.swap(temp_holds);
	m_reverse_holds.swap(temp_reverse_holds);
	m_rotations.swap(temp_rotations);
	return true;
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
		m_actors[c]= act;
	}
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	return true;
}

NewSkinData::NewSkinData()
	:m_loaded(false)
{
	
}

bool NewSkinData::load_taps_from_lua(lua_State* L, int index, size_t columns, NewSkinLoader const* load_skin, string& insanity_diagnosis)
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
		if(!temp_columns[c].load_from_lua(L, lua_gettop(L), load_skin, sub_sanity))
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
	lua_getfield(L, index, "fallback");
	if(lua_isstring(L, -1))
	{
		m_fallback_skin_name= lua_tostring(L, -1);
	}
	else
	{
		m_fallback_skin_name.clear();
	}
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
	{"UpLeft", "Down", "UpRight"},
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
	{"ParaLeft", "ParaUpLeft", "ParaUp", "ParaUpRight", "ParaRight"},
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

bool NewSkinLoader::supports_needed_buttons(StepsType stype)
{
	if(m_supports_all_buttons)
	{
		return true;
	}
	std::vector<std::string> const& button_list= button_lists[stype];
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
	string found_path= NEWSKIN->get_path_to_file_in_skin(this, loader);
	if(found_path.empty())
	{
		LuaHelpers::ReportScriptError("Noteskin " + m_skin_name + " points to a"
			" loader file that does not exist: " + loader);
		return false;
	}
	RString script_text;
	if(!GetFileContents(found_path, script_text))
	{
		LuaHelpers::ReportScriptError("Noteskin " + m_skin_name + " points to a"
			" loader file " + found_path + " could not be loaded.");
		return false;
	}
	RString error= "Error loading " + found_path + ": ";
	if(!LuaHelpers::RunScript(L, script_text, "@" + found_path, error, 0, 1, true))
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

bool NewSkinLoader::load_into_data(StepsType stype,
	NewSkinData& dest, string& insanity_diagnosis)
{
	vector<string> const& button_list= button_lists[stype];
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
	if(!dest.load_taps_from_lua(L, lua_gettop(L), button_list.size(), this, sub_sanity))
	{
		RETURN_NOT_SANE("Invalid data from loader: " + sub_sanity);
	}
	if(!load_layer_set_into_data(L, button_list_index, button_list.size(), m_below_loaders,
			dest.m_layers_below_notes, sub_sanity))
	{
		RETURN_NOT_SANE("Error running layer below loaders: " + sub_sanity);
	}
	if(!load_layer_set_into_data(L, button_list_index, button_list.size(), m_above_loaders,
			dest.m_layers_above_notes, sub_sanity))
	{
		RETURN_NOT_SANE("Error running layer above loaders: " + sub_sanity);
	}
#undef RETURN_NOT_SANE
	lua_settop(L, original_top);
	LUA->Release(L);
	return true;
}
