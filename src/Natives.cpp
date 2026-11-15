#include "Vitals.h"

// Papyrus native function list (opt-in: bPapyrusNatives=1).
// Walks the Papyrus VM's loaded script types and lists every native function, with the DLL that
// registered it. The owner is found from the function object's vtable, which lives in the module
// that created it (Fallout4.exe for the game's own natives, f4se_*.dll for F4SE's, a mod's DLL for the mod's).
// On an old build this gives a dead mod's exact Papyrus interface, which a remake has to match.
// Read-only. Runs one task tick after kGameDataReady, so plugins that register natives in their own kGameDataReady
// handler are listed too. The VM's type lock is held only while names are copied as plain strings; the JSON is built
// and written after it is released, so script threads are not kept waiting.

namespace Vitals
{
	void ListPapyrusNatives()
	{
		Crumb("Vitals", "papyrus_natives");
		// VirtualMachine::GetSingleton goes through CommonLib's ID lookup, which ends the game on a missing ID.
		if (!LibraryIdResolves("GameScript::GameVM::Singleton")) {
			Summary("Papyrus natives skipped: the VM's Address Library ID does not resolve on this build");
			return;
		}
		auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm || !IsReadable(vm, sizeof(void*))) {
			Summary("Papyrus natives: VM not available, skipped");
			return;
		}

		std::map<std::string, std::size_t> perModule;
		std::size_t                        total = 0;

		auto typeName = [](RE::BSScript::TypeInfo::RawType a_t) -> const char* {
			using T = RE::BSScript::TypeInfo::RawType;
			switch (a_t) {
			case T::kNone: return "None";
			case T::kObject: return "Object";
			case T::kString: return "String";
			case T::kInt: return "Int";
			case T::kFloat: return "Float";
			case T::kBool: return "Bool";
			case T::kVar: return "Var";
			case T::kStruct: return "Struct";
			case T::kArrayObject: return "Object[]";
			case T::kArrayString: return "String[]";
			case T::kArrayInt: return "Int[]";
			case T::kArrayFloat: return "Float[]";
			case T::kArrayBool: return "Bool[]";
			case T::kArrayVar: return "Var[]";
			case T::kArrayStruct: return "Struct[]";
			default: return "?";
			}
		};

		// Copied under the VM's type lock: plain strings and enums only. Formatting and writing happen after it is
		// released, so script threads wait only for the copy.
		struct Row
		{
			void*                    vtbl{ nullptr };
			std::string              script, function, returns;
			bool                     isStatic{ false };
			std::vector<std::string> params, ptypes;
		};
		std::vector<Row> rows;

		// The function objects are reached through CommonLib's layout of the VM (virtual calls), not SafeRead. Their
		// vtable is checked first: it must be readable and its first slots must be code, or the object is skipped.
		auto plausible = [](RE::BSScript::IFunction* a_fn) {
			void* vtbl = nullptr;
			if (!a_fn || !SafeRead(a_fn, &vtbl, sizeof(vtbl)) || !vtbl)
				return false;
			void* slots[10]{};  // 00-09: every slot called below (GetName 01 ... GetIsStatic 09)
			if (!SafeRead(vtbl, slots, sizeof(slots)))
				return false;
			return std::ranges::all_of(slots, [](void* a_p) { return a_p && IsExecutable(a_p); });
		};

		auto copyFn = [&](const RE::BSFixedString& a_script, RE::BSScript::IFunction* a_fn) {
			if (!plausible(a_fn) || !a_fn->GetIsNative())
				return;
			void* vtbl = nullptr;
			SafeRead(a_fn, &vtbl, sizeof(vtbl));
			Row        r;
			const auto str = [](const RE::BSFixedString& a_s) { return std::string(a_s.c_str() ? a_s.c_str() : ""); };
			r.vtbl = vtbl;  // its module is looked up after the lock is released
			r.script = str(a_script);
			r.function = str(a_fn->GetName());
			r.isStatic = a_fn->GetIsStatic();
			r.returns = typeName(a_fn->GetReturnType().GetRawType());
			const auto count = std::min<std::uint32_t>(a_fn->GetParamCount(), 64);
			for (std::uint32_t i = 0; i < count; ++i) {
				RE::BSFixedString      pname;
				RE::BSScript::TypeInfo ptype;
				a_fn->GetParam(i, pname, ptype);
				r.params.push_back(str(pname));
				r.ptypes.emplace_back(typeName(ptype.GetRawType()));
			}
			rows.push_back(std::move(r));
		};

		{
			RE::BSAutoLock<RE::BSSpinLock> lock{ vm->typeInfoLock };
			for (auto& [name, type] : vm->objectTypeMap) {
				auto* t = type.get();
				if (!t || !IsReadable(t, 0x40))
					continue;
				if (auto* g = t->GetGlobalFuncIter())
					for (std::uint32_t i = 0; i < t->GetNumGlobalFuncs(); ++i)
						copyFn(name, g[i].func.get());
				if (auto* m = t->GetMemberFuncIter())
					for (std::uint32_t i = 0; i < t->GetNumMemberFuncs(); ++i)
						copyFn(name, m[i].func.get());
			}
		}

		for (const auto& r : rows) {
			const auto               module = ModuleNameForAddress(r.vtbl);
			std::vector<std::string> params, ptypes;
			for (const auto& p : r.params)
				params.push_back(Json::Quote(p));
			for (const auto& p : r.ptypes)
				ptypes.push_back(Json::Quote(p));
			Emit("papyrus_native", Json()
				.Str("module", module)
				.Str("script", r.script)
				.Str("function", r.function)
				.Bool("static", r.isStatic)
				.UNum("param_count", r.params.size())
				.Str("returns", r.returns)
				.Raw("params", Json::Array(params))
				.Raw("param_types", Json::Array(ptypes)));
			++perModule[module];
			++total;
		}
		Summary(std::format("Papyrus natives: {} total", total));
		for (const auto& [mod, n] : perModule)
			Summary(std::format("  {}: {}", mod, n));
	}
}
