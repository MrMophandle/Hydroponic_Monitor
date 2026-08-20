"""
embed_web_assets.py — PlatformIO extra_script that embeds the Phase 6
dashboard assets (src/web/*) into the esp32-s3-devkitm-1 firmware image.

Why this exists instead of `idf_component_register(... EMBED_TXTFILES ...)`
or `board_build.embed_txtfiles`:

  - `idf_component_register`'s own EMBED_TXTFILES (tried first, per upstream
    ESP-IDF docs) is not correctly driven by this PlatformIO version's
    espidf integration for a non-"main"-named component ("src" here): the
    generated `_binary_*` assembly source is described in the component's
    CMake project metadata but PlatformIO's own build driver never actually
    runs the underlying CMake custom command, producing a "source not
    found" build failure at compile time.

  - `board_build.embed_txtfiles` (PlatformIO's own documented project
    option, tried second) DOES correctly generate the `.S` assembly file
    for each entry (via the identical `data_file_embed_asm.cmake` script,
    same `_binary_<basename>_start`/`_end` symbol convention), but for the
    `espidf` framework it only registers an ordering dependency
    (`env.Requires($BUILD_DIR/$PROGNAME.elf, ...)`) on the generated `.S`
    file — it never compiles that `.S` into an object file, and never adds
    it to the link, so the linker fails with "undefined reference to
    `_binary_index_html_start`" etc.

This script closes that gap: it reuses the same ESP-IDF
`data_file_embed_asm.cmake` script (so the symbol names src/http_api.c
already declares — `_binary_index_html_start/_end`, `_binary_style_css_
start/_end`, `_binary_app_js_start/_end`,
`_binary_dashboard_logic_js_start/_end` — are unchanged), then explicitly
compiles the resulting `.S` to an object and appends it to `PIOBUILDFILES`
so it actually participates in the final link.
"""

from os.path import basename, join

Import("env")

WEB_ASSETS = [
    "src/web/index.html",
    "src/web/style.css",
    "src/web/app.js",
    "src/web/dashboard-logic.js",
]


def _embed_one(project_dir, asset_relpath):
    data_file = join(project_dir, asset_relpath)
    name = basename(asset_relpath)
    asm_file = join("$BUILD_DIR", name + ".S")

    asm_target = env.Command(
        asm_file,
        data_file,
        env.VerboseAction(
            " ".join(
                [
                    join(
                        env.PioPlatform().get_package_dir("tool-cmake") or "",
                        "bin",
                        "cmake",
                    ),
                    "-D",
                    "DATA_FILE=$SOURCE",
                    "-D",
                    "SOURCE_FILE=$TARGET",
                    "-D",
                    "FILE_TYPE=TEXT",
                    "-P",
                    join(
                        env.PioPlatform().get_package_dir("framework-espidf") or "",
                        "tools",
                        "cmake",
                        "scripts",
                        "data_file_embed_asm.cmake",
                    ),
                ]
            ),
            "Embedding %s" % asset_relpath,
        ),
    )

    obj_target = env.Object(asm_target)

    # PIOBUILDFILES is a plain list snapshotted by the framework's own
    # env.Append(PIOBUILDFILES=...) call and by env.BuildProgram() at a
    # point in the SCons DAG-construction pass that this extra_script (which
    # PlatformIO loads at a different point in that same pass) cannot
    # reliably precede — appending here compiles the object (confirmed by
    # the "Compiling .../<name>.o" build step) but silently drops it from
    # the actual link. LINKFLAGS is read live by the linker's command-line
    # generator at build-execution time (not snapshotted at DAG-construction
    # time), so appending the object's own path there — plus an explicit
    # dependency on the firmware ELF — reliably gets it onto the final
    # `ld` invocation regardless of script/framework load order.
    env.Append(LINKFLAGS=[obj_target[0].get_abspath()])
    env.Depends(join("$BUILD_DIR", "${PROGNAME}.elf"), obj_target)


for _asset in WEB_ASSETS:
    _embed_one(env.subst("$PROJECT_DIR"), _asset)
