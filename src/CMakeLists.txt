# Hand-maintained starting Phase 6 (sensor-monitoring-dashboard): src/web/
# holds browser assets (HTML/CSS/JS), not C sources, so the glob below is
# scoped to *.c/*.h explicitly rather than *.* — the web assets are picked
# up for embedding by the `extra_scripts = embed_web_assets.py` entry in
# platformio.ini, never compiled as C here.
#
# NOTE: two more "standard" embed mechanisms were tried first and BOTH
# failed under this platform/version's PlatformIO espidf integration:
#   1. idf_component_register's own EMBED_TXTFILES argument (what upstream
#      ESP-IDF documents) — PlatformIO's build driver never actually runs
#      the generated CMake custom command for a non-"main"-named component,
#      producing a "source not found" build failure.
#   2. PlatformIO's own `board_build.embed_txtfiles` project option — this
#      DOES generate the `_binary_*` assembly via the identical
#      `data_file_embed_asm.cmake` script, but only registers an *ordering*
#      dependency, never actually compiling/linking the resulting object
#      into firmware.elf, producing "undefined reference to `_binary_*`"
#      at link time.
# `embed_web_assets.py` reimplements the missing compile+link step directly
# (see its module docstring for the full investigation and the SCons
# mechanism used). Do not "simplify" this back to EMBED_TXTFILES or
# board_build.embed_txtfiles without re-verifying a clean rebuild links —
# see memory-bank/tasks/sensor-monitoring-dashboard.md Execution State.
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c ${CMAKE_SOURCE_DIR}/src/*.h)

idf_component_register(SRCS ${app_sources})
