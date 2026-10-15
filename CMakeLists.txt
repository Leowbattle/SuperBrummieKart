cmake_minimum_required(VERSION 3.20)

project(SuperBrummieKart
    VERSION 1.0.0
    DESCRIPTION "Super Brummie Kart"
    LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Default to a Release build so a plain `cmake -B build` gives an optimised
# binary, matching the old compile.sh.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(superbrummiekart main.c)

if(NOT MSVC)
    target_compile_options(superbrummiekart PRIVATE -Wall -Wextra)
endif()

# Assets the game loads at runtime, relative to the working directory. They are
# bundled with the desktop build and preloaded into the Emscripten virtual
# filesystem at the same relative paths.
set(ASSET_FILES
    Blinker-Regular.ttf
    bowser.png
    brummie.png
    logo.png
    path_marker.png
    player_path_marker.png
    tree.png
    yoshi.png)

set(ASSET_DIRS
    skybox
    tracks)

if(EMSCRIPTEN)
    # ------------------------------------------------------------------
    # Web (Emscripten) build
    # ------------------------------------------------------------------

    # Emscripten ships its own SDL2 / SDL2_image / SDL2_ttf ports.
    set(SDL_PORT_FLAGS
        -sUSE_SDL=2
        -sUSE_SDL_IMAGE=2
        -sSDL2_IMAGE_FORMATS=png
        -sUSE_SDL_TTF=2)

    # shell.html replaces Emscripten's default shell, whose Fullscreen button
    # resizes the canvas to 0x0 (black screen) and requests pointer lock
    # unguarded (an uncaught exception on phones). It also supplies the
    # mobile-friendly layout and the touch controls.
    set(SHELL_HTML "${CMAKE_CURRENT_SOURCE_DIR}/shell.html")

    # Stamp every build with a hash of its inputs. The shell appends it to the
    # URLs of index.js, index.wasm and index.data so a browser can never pair a
    # cached file from one build with a freshly fetched file from another: the
    # module would otherwise fail to instantiate with "import requires a
    # callable". A content hash is used rather than a timestamp so unchanged
    # rebuilds keep the same URLs and stay cacheable.
    set(SHELL_STAMP_INPUTS
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/main.c"
        "${SHELL_HTML}")

    foreach(asset IN LISTS ASSET_FILES)
        list(APPEND SHELL_STAMP_INPUTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
    endforeach()

    foreach(dir IN LISTS ASSET_DIRS)
        file(GLOB_RECURSE dir_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*")
        list(APPEND SHELL_STAMP_INPUTS ${dir_files})
    endforeach()

    # Re-run configure when any of them changes, otherwise the stamp would go
    # stale and defeat the point.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${SHELL_STAMP_INPUTS})

    set(stamp_contents "")
    foreach(input IN LISTS SHELL_STAMP_INPUTS)
        file(SHA256 "${input}" input_hash)
        string(APPEND stamp_contents "${input_hash}")
    endforeach()

    string(SHA256 SBK_BUILD_STAMP "${stamp_contents}")
    string(SUBSTRING "${SBK_BUILD_STAMP}" 0 16 SBK_BUILD_STAMP)

    configure_file("${SHELL_HTML}"
        "${CMAKE_CURRENT_BINARY_DIR}/shell.generated.html"
        @ONLY)

    set(SHELL_HTML "${CMAKE_CURRENT_BINARY_DIR}/shell.generated.html")

    target_compile_options(superbrummiekart PRIVATE ${SDL_PORT_FLAGS})
    target_link_options(superbrummiekart PRIVATE
        ${SDL_PORT_FLAGS}
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1
        # The default shell's list is only applied when the default shell is
        # used, so it is repeated here for the properties shell.html sets.
        -sINCOMING_MODULE_JS_API=canvas,locateFile,monitorRunDependencies,onAbort,onExit,print,printErr,setStatus
        "SHELL:--shell-file ${SHELL_HTML}")

    # Relink the HTML when the shell changes.
    set_property(TARGET superbrummiekart APPEND PROPERTY
        LINK_DEPENDS "${SHELL_HTML}")

    # Preload every asset into the virtual FS at its relative path (e.g.
    # logo.png -> /logo.png) so the game's fopen()/IMG_Load() calls just work.
    foreach(asset IN LISTS ASSET_FILES)
        target_link_options(superbrummiekart PRIVATE
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/${asset}@/${asset}")
    endforeach()

    foreach(dir IN LISTS ASSET_DIRS)
        target_link_options(superbrummiekart PRIVATE
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/${dir}@/${dir}")
    endforeach()

    # Emit index.html + index.js + index.wasm + index.data.
    set_target_properties(superbrummiekart PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html")

else()
    # ------------------------------------------------------------------
    # Desktop (native) build
    # ------------------------------------------------------------------

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
    pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

    target_include_directories(superbrummiekart PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
        ${SDL2_TTF_INCLUDE_DIRS})
    target_compile_options(superbrummiekart PRIVATE
        ${SDL2_CFLAGS_OTHER}
        ${SDL2_IMAGE_CFLAGS_OTHER}
        ${SDL2_TTF_CFLAGS_OTHER})
    target_link_directories(superbrummiekart PRIVATE
        ${SDL2_LIBRARY_DIRS}
        ${SDL2_IMAGE_LIBRARY_DIRS}
        ${SDL2_TTF_LIBRARY_DIRS})
    target_link_libraries(superbrummiekart PRIVATE
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARIES}
        ${SDL2_TTF_LIBRARIES}
        m)

    # Install the binary next to its assets so the tarball is runnable as-is.
    install(TARGETS superbrummiekart RUNTIME DESTINATION .)
    install(FILES ${ASSET_FILES} DESTINATION .)
    install(DIRECTORY ${ASSET_DIRS} DESTINATION .)
endif()
