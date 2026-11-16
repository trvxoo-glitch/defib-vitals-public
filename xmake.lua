-- Vitals: Defib's debug and logging F4SE plugin.
-- One DLL for Fallout 4 1.10.163 (Old-Gen), 1.10.980/984 (Next-Gen) and 1.11.137-1.11.240 (Anniversary).
-- The library is pinned: see lib/COMMONLIBF4_COMMIT and tools/fetch_commonlib.ps1 / .sh.
set_xmakever("3.0.0")

includes("lib/commonlibf4")

local plugin_name = "Vitals"
local plugin_version = "0.4.1"
local vmaj, vmin, vpat = plugin_version:match("^(%d+)%.(%d+)%.(%d+)$")

set_project(plugin_name)
set_version(plugin_version)
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.release", "mode.releasedbg")
add_defines("COMMONLIB_RUNTIMECOUNT=3")

target(plugin_name)
    add_rules("commonlibf4.plugin", {
        name = plugin_name,
        author = "Defib project",
        description = "Passive environment, address and Papyrus-native reporter for Defib ports",
        -- Our own version-data block: lists exactly the builds the library accepts,
        -- so F4SE refuses Vitals cleanly on a future patch instead of the library
        -- terminating the game. See res/vitals/commonlibf4-plugin.cpp.in.
        plugin_template = path.join(os.scriptdir(), "res/vitals/commonlibf4-plugin.cpp.in")
    })
    add_files("src/**.cpp")
    add_headerfiles("src/**.h", "include/**.h")
    add_includedirs("src", "include")
    set_pcxxheader("src/pch.h")
    add_defines(
        'PLUGIN_NAME="' .. plugin_name .. '"',
        "PLUGIN_VERSION_MAJOR=" .. vmaj,
        "PLUGIN_VERSION_MINOR=" .. vmin,
        "PLUGIN_VERSION_PATCH=" .. vpat
    )
