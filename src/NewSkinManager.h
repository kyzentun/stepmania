#ifndef NEW_SKIN_MANAGER_H
#define NEW_SKIN_MANAGER_H

#include "NewSkin.h"

struct NewSkinManager
{
	NewSkinManager();
	~NewSkinManager();

	void LoadSkins();
	void GetSkinsForStepsType(StepsType type, std::vector<NewSkinLoader const*>& ret);
	void GetSkinNamesForStepsType(StepsType type, std::vector<RString>& ret);
	std::vector<StepsType> const& GetSupportedStepsTypes();
	NewSkinLoader const* GetLoaderForSkin(RString const& skin_name);

	void PushSelf(lua_State* L);

private:
	std::vector<NewSkinLoader> m_skins;
	std::vector<StepsType> m_supported_types;
};

extern NewSkinManager* NEWSKIN;	// global and accessible from anywhere in our program

#endif
