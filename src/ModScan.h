#pragma once
// What the player's mod manager knows about the F4SE DLLs that are not running: which mod's copy the game sees,
// which other installed mods carry their own copy of the same DLL, and the mod manager's own Nexus update check.
// No Nexus page and no network: Vortex's vortex.deployment.json and state backups, MO2's ModOrganizer.ini,
// modlist.txt and meta.ini. The parsing lives in include/Vitals/ModManagers.h (tested); this file does the file
// access, once, with a time budget, because it runs before the start-up box.

#include "Vitals/ModManagers.h"

namespace Vitals::ModScan
{
	struct Copy
	{
		std::string           mod;   // folder name in the Vortex staging folder or the MO2 mods folder
		std::filesystem::path file;  // ...\<mod>\F4SE\Plugins\<dll>
		bool                  enabled{ true };  // MO2: on in the selected profile (Vortex: always true)
	};

	struct DllCopies
	{
		std::string       installedBy;  // the mod whose copy the game sees (empty: not known)
		std::vector<Copy> others;       // other mods whose copy is NOT byte-identical to the one the game sees
	};

	struct Deployed
	{
		std::string                      name;   // file name in Data\F4SE\Plugins
		const std::vector<std::uint8_t>* bytes;  // the deployed file, already read by the doctor
	};

	struct Result
	{
		std::string                      manager;  // "Vortex", "MO2" or empty
		std::map<std::string, DllCopies> dlls;     // lower-case DLL name -> what was found
		std::vector<std::string>         notes;    // lines for the report's top section
		std::map<std::string, ModManagers::VortexModInfo> vortexMods;  // lower-case staging folder name -> versions
		std::map<std::string, ModManagers::MetaInfo>      mo2Meta;     // lower-case mod name -> meta.ini (only mods named above)

		const DllCopies* For(const std::string& a_dll) const;
		std::string      NewerOnNexus(const std::string& a_mod) const;  // " Vortex says a newer version ..." or empty
		std::string      ModVersion(const std::string& a_mod) const;    // the mod's own version, as the mod manager has it
	};

	// One pass over the mods folder for all of a_dlls together. Never throws for a missing or unreadable file.
	Result Find(const std::vector<Deployed>& a_dlls);
}
