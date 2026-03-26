set_project("BetterTooltips")
set_version("1.0.0")

set_languages("cxx23")

add_rules("mode.release")

add_repositories(
    "xmake-repo https://github.com/xmake-io/xmake-repo.git",
    "nexcaise-repo https://github.com/nexcaise/xmake-repo.git"
)

add_requires("miniapi")

target("BetterTooltips")
    set_kind("shared")
    add_packages("miniapi")
    add_linkdirs("niseAPI/libs/arm64-v8a")
    add_linkdirs("libs/arm64-v8a")
    add_links("log","GlossHook")
    add_links("nise")

    add_files("src/*.cpp", "src/**/*.cpp", "niseAPI/deps/gamepwnage/src/*.c", "niseAPI/deps/gamepwnage/src/**/*.c")

    add_includedirs("include", "niseAPI/include", "niseAPI/deps/gamepwnage/includes", {public = true})

    add_cxflags("-O2", "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections", "-w")
    add_cflags("-O2", "-fvisibility=hidden", "-ffunction-sections", "-fdata-sections", "-w")
    add_ldflags("-Wl,--gc-sections,--strip-all", "-s")
