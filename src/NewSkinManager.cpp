#include "global.h"

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

	LoadSkins();
}

NewSkinManager::~NewSkinManager()
{
	// Unregister with Lua.
	LUA->UnsetGlobal("NEWSKIN");
}

void NewSkinManager::LoadSkins()
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

void NewSkinManager::GetSkinsForStepsType(StepsType type, std::vector<NewSkinLoader const*>& ret)
{
	for(auto&& skin : m_skins)
	{
		if(skin.supports_needed_buttons(type))
		{
			ret.push_back(&skin);
		}
	}
}

void NewSkinManager::GetSkinNamesForStepsType(StepsType type, std::vector<RString>& ret)
{
	for(auto&& skin : m_skins)
	{
		if(skin.supports_needed_buttons(type))
		{
			ret.push_back(skin.get_name());
		}
	}
}

std::vector<StepsType> const& NewSkinManager::GetSupportedStepsTypes()
{
	return m_supported_types;
}

NewSkinLoader const* NewSkinManager::GetLoaderForSkin(RString const& skin_name)
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

#include "LuaBinding.h"

struct LunaNewSkinManager: Luna<NewSkinManager>
{
	static int get_skin_names_for_stepstype(T* p, lua_State* L)
	{
		StepsType stype= Enum::Check<StepsType>(L, 1);
		vector<RString> names;
		p->GetSkinNamesForStepsType(stype, names);
		LuaHelpers::CreateTableFromArray(names, L);
		return 1;
	}

	LunaNewSkinManager()
	{
		ADD_METHOD(get_skin_names_for_stepstype);
	}
};

LUA_REGISTER_CLASS(NewSkinManager);
