#include "global.h"

#include "ActorUtil.h"
#include "NewSkinManager.h"
#include "RageFileManager.h"
#include "SpecialFiles.h"

using std::vector;

NewSkinManager* NEWSKIN= nullptr; // global and accessible from anywhere in our program

NewSkinManager::NewSkinManager()
{
	// Register with Lua.
	Lua *L = LUA->Get();
	lua_pushstring(L, "NEWSKIN");
	PushSelf(L);
	lua_settable(L, LUA_GLOBALSINDEX);
	LUA->Release(L);

	load_skins();
}

NewSkinManager::~NewSkinManager()
{
	// Unregister with Lua.
	LUA->UnsetGlobal("NEWSKIN");
}

void NewSkinManager::load_skins()
{
	vector<RString> dirs;
	FILEMAN->GetDirListing(SpecialFiles::NEWSKINS_DIR, dirs, true, true);
	m_skins.clear();
	m_supported_types.clear();
	m_skins.reserve(dirs.size());
	for(auto&& dir : dirs)
	{
		std::string skin_file= dir + "/noteskin.lua";
		// If noteskin.lua doesn't exist, maybe the folder is for something else.
		// Ignore it.
		if(FILEMAN->DoesFileExist(skin_file))
		{
			NewSkinLoader loader;
			if(loader.load_from_file(skin_file))
			{
				m_skins.push_back(loader);
			}
		}
	}
	for(int st= 0; st < NUM_StepsType; ++st)
	{
		bool supported= false;
		for(auto&& skin : m_skins)
		{
			if(skin.supports_needed_buttons(static_cast<StepsType>(st)))
			{
				supported= true;
				break;
			}
		}
		if(supported)
		{
			m_supported_types.push_back(static_cast<StepsType>(st));
		}
	}
}

void NewSkinManager::get_skins_for_stepstype(StepsType type, std::vector<NewSkinLoader const*>& ret)
{
	for(auto&& skin : m_skins)
	{
		if(skin.supports_needed_buttons(type))
		{
			ret.push_back(&skin);
		}
	}
}

void NewSkinManager::get_all_skin_names(std::vector<std::string>& ret)
{
	for(auto&& skin : m_skins)
	{
		ret.push_back(skin.get_name());
	}
}

void NewSkinManager::get_skin_names_for_stepstype(StepsType type, std::vector<std::string>& ret)
{
	for(auto&& skin : m_skins)
	{
		if(skin.supports_needed_buttons(type))
		{
			ret.push_back(skin.get_name());
		}
	}
}

std::string NewSkinManager::get_first_skin_name_for_stepstype(StepsType type)
{
	for(auto&& skin : m_skins)
	{
		if(skin.supports_needed_buttons(type))
		{
			return skin.get_name();
		}
	}
	std::string stype_name= StepsTypeToString(type);
	LuaHelpers::ReportScriptError("No noteskin supports the stepstype " + stype_name);
	return "default";
}

std::vector<StepsType> const& NewSkinManager::get_supported_stepstypes()
{
	return m_supported_types;
}

bool NewSkinManager::skin_supports_stepstype(std::string const& skin, StepsType type)
{
	NewSkinLoader const* loader= get_loader_for_skin(skin);
	// This does not report an error when the skin is not found because it is
	// used by the profile to pick a skin to use, and the profile might have the
	// names of unknown skins in it.
	if(loader == nullptr)
	{
		return false;
	}
	return loader->supports_needed_buttons(type);
}

NewSkinLoader const* NewSkinManager::get_loader_for_skin(std::string const& skin_name)
{
	for(auto&& skin : m_skins)
	{
		if(skin.get_name() == skin_name)
		{
			return &skin;
		}
	}
	return nullptr;
}

std::string NewSkinManager::get_path(
	NewSkinLoader const* skin, std::string file)
{
	if(skin == nullptr)
	{
		return "";
	}
	// Check to see if the filename is already a valid path.
	RString resolved= file;
	if(ActorUtil::ResolvePath(resolved, skin->get_name(), true))
	{
		return resolved;
	}
	// Fallback loop cases are detected and silently ignored by storing each
	// fallback in used_fallbacks.  This allows skins to mutually fall back on
	// each other if someone really needs to do that.
	std::unordered_set<std::string> used_fallbacks;
	std::string next_path= skin->get_load_path();
	std::string next_fallback= skin->get_fallback_name();
	std::string found_path;
	while(!next_path.empty())
	{
		resolved= next_path + file;
		next_path.clear();
		if(ActorUtil::ResolvePath(resolved, skin->get_name(), true))
		{
			return resolved;
		}
		else if(!next_fallback.empty() &&
			used_fallbacks.find(next_fallback) == used_fallbacks.end())
		{
			used_fallbacks.insert(next_fallback);
			NewSkinLoader const* fallback= get_loader_for_skin(next_fallback);
			if(fallback != nullptr)
			{
				used_fallbacks.insert(next_fallback);
				next_path= fallback->get_load_path();
				next_fallback= fallback->get_fallback_name();
			}
		}
	}
	return "";
}

bool NewSkinManager::named_skin_exists(std::string const& skin_name)
{
	for(auto&& skin : m_skins)
	{
		if(skin_name == skin.get_name())
		{
			return true;
		}
	}
	return false;
}


#include "LuaBinding.h"

struct LunaNewSkinManager: Luna<NewSkinManager>
{
	static int get_all_skin_names(T* p, lua_State* L)
	{
		vector<std::string> names;
		p->get_all_skin_names(names);
		LuaHelpers::CreateTableFromArray(names, L);
		return 1;
	}
	static int get_skin_names_for_stepstype(T* p, lua_State* L)
	{
		StepsType stype= Enum::Check<StepsType>(L, 1);
		vector<std::string> names;
		p->get_skin_names_for_stepstype(stype, names);
		LuaHelpers::CreateTableFromArray(names, L);
		return 1;
	}
	static int get_path(T* p, lua_State* L)
	{
		std::string skin_name= SArg(1);
		std::string file_name= SArg(2);
		NewSkinLoader const* loader= p->get_loader_for_skin(skin_name);
		if(loader == nullptr)
		{
			luaL_error(L, "No such noteskin.");
		}
		std::string path= p->get_path(loader, file_name);
		if(path.empty())
		{
			lua_pushnil(L);
		}
		else
		{
			lua_pushstring(L, path.c_str());
		}
		return 1;
	}
	static int reload_skins(T* p, lua_State* L)
	{
		UNUSED(L);
		p->load_skins();
		COMMON_RETURN_SELF;
	}

	LunaNewSkinManager()
	{
		ADD_METHOD(get_all_skin_names);
		ADD_METHOD(get_skin_names_for_stepstype);
		ADD_METHOD(get_path);
		ADD_METHOD(reload_skins);
	}
};

LUA_REGISTER_CLASS(NewSkinManager);
