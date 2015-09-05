#ifndef NEW_SKIN_MANAGER_H
#define NEW_SKIN_MANAGER_H

#include "NewSkin.h"

struct NewSkinManager
{
	NewSkinManager();
	~NewSkinManager();

	void load_skins();
	void get_skins_for_stepstype(StepsType type, std::vector<NewSkinLoader const*>& ret);
	void get_skin_names_for_stepstype(StepsType type, std::vector<RString>& ret);
	std::vector<StepsType> const& get_supported_stepstypes();
	NewSkinLoader const* get_loader_for_skin(RString const& skin_name);
	std::string get_path_to_file_in_skin(NewSkinLoader const* skin,
		std::string file);

	void PushSelf(lua_State* L);

private:
	std::vector<NewSkinLoader> m_skins;
	std::vector<StepsType> m_supported_types;
};

extern NewSkinManager* NEWSKIN;	// global and accessible from anywhere in our program

#endif
