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
		RString skin_file= dir + "/noteskin.lua";
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

void NewSkinManager::get_skin_names_for_stepstype(StepsType type, std::vector<RString>& ret)
{
	for(auto&& skin : m_skins)
	{
		if(skin.supports_needed_buttons(type))
		{
			ret.push_back(skin.get_name());
		}
	}
}

std::vector<StepsType> const& NewSkinManager::get_supported_stepstypes()
{
	return m_supported_types;
}

NewSkinLoader const* NewSkinManager::get_loader_for_skin(RString const& skin_name)
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

std::string NewSkinManager::get_path_to_file_in_skin(
	NewSkinLoader const* skin, std::string file)
{
	if(skin == nullptr)
	{
		return "";
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
		RString resolved= next_path + file;
		next_path.clear();
		if(ActorUtil::ResolvePath(resolved, skin->get_name(), true))
		{
			found_path= resolved;
			break;
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
	return found_path;
}


#include "LuaBinding.h"

struct LunaNewSkinManager: Luna<NewSkinManager>
{
	static int get_skin_names_for_stepstype(T* p, lua_State* L)
	{
		StepsType stype= Enum::Check<StepsType>(L, 1);
		vector<RString> names;
		p->get_skin_names_for_stepstype(stype, names);
		LuaHelpers::CreateTableFromArray(names, L);
		return 1;
	}

	LunaNewSkinManager()
	{
		ADD_METHOD(get_skin_names_for_stepstype);
	}
};

LUA_REGISTER_CLASS(NewSkinManager);
