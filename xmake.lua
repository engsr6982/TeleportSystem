add_rules("mode.debug", "mode.release")

add_repositories("liteldev-repo https://github.com/LiteLDev/xmake-repo.git")

add_requires("levilamina 26.20.0", {configs = {target_type = "server"}})

add_requires("levibuildscript")
add_requires("exprtk 0.0.3")

if not has_config("vs_runtime") then
    set_runtimes("MD")
end

if is_plat("windows") then
    set_toolchains("clang-cl") -- windows allways use clang-cl
end

option("test")
    set_default(false)
    set_showmenu(true)
option_end()

rule("gen_version")
    before_build(function(target)
        import("scripts.gen_version")()
    end)

target("TeleportSystem")
    add_rules("gen_version")
    add_rules("@levibuildscript/linkrule")
    add_rules("@levibuildscript/modpacker")
    if is_plat("windows") then
        add_defines("NOMINMAX", "UNICODE")
        set_exceptions("cxx")
        add_cxflags("/utf-8", "/W4", "/w44265", "/w44289", "/w44296", "/w45263", "/w44738", "/w45204")
        add_cxflags(
            "/EHs",
            "-Wno-microsoft-cast",
            "-Wno-invalid-offsetof",
            "-Wno-c++2b-extensions",
            "-Wno-microsoft-include",
            "-Wno-overloaded-virtual",
            "-Wno-ignored-qualifiers",
            "-Wno-missing-field-initializers",
            "-Wno-potentially-evaluated-expression",
            "-Wno-pragma-system-header-outside-header",
            {tools = {"clang_cl"}}
        )
    end
    set_kind("shared")
    set_symbols("debug")
    set_languages("c++20")
    add_includedirs("src")
    add_headerfiles("src/**.h")
    add_files("src/**.cc")
    add_defines(
        "_HAS_CXX23=1",
        "TPS_EXPORTS",
        "LL_PLAT_S"
    )
    add_packages(
        "levilamina",
        "exprtk"
    )

    if is_mode("debug") then
        add_defines("TPS_DEBUG"--[[ , "LL_I18N_COLLECT_STRINGS" ]])
    end

    if has_config("test") then
        add_defines("TPS_TEST")
        add_includedirs("test")
        add_files("test/**.cc")
    end

    add_defines("MOD_NAME=\"TeleportSystem\"")
