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

    target_sources(superbrummiekart PRIVATE platform_sdl.c)

    # Emscripten ships its own SDL2 / SDL2_image / SDL2_ttf ports.
    set(SDL_PORT_FLAGS
        -sUSE_SDL=2
        -sUSE_SDL_IMAGE=2
        -sSDL2_IMAGE_FORMATS=png
        -sUSE_SDL_TTF=2)

    target_compile_options(superbrummiekart PRIVATE ${SDL_PORT_FLAGS})
    target_link_options(superbrummiekart PRIVATE
        ${SDL_PORT_FLAGS}
        -sALLOW_MEMORY_GROWTH=1
        -sEXIT_RUNTIME=1)

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

elseif(CMAKE_SYSTEM_NAME STREQUAL "NintendoWii")
    # ------------------------------------------------------------------
    # Nintendo Wii build (devkitPro toolchain, see compile-wii.sh)
    # ------------------------------------------------------------------

    target_sources(superbrummiekart PRIVATE platform_wii.c)

    # SDL_main.h redefines main() as SDL_main() on the Wii, but the game runs its
    # own main loop and does not use SDL2main.
    target_compile_definitions(superbrummiekart PRIVATE SDL_MAIN_HANDLED)

    # devkitPro's portlibs provide CMake configs; the -static variants are the
    # ones that actually exist for the Wii targets.
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)
    # The wii-sdl2_ttf CMake config forgets the brotli libraries that its
    # bundled FreeType needs. Wrapping it in an interface library puts them after
    # FreeType on the link line, which static linking requires.
    add_library(sdl2_ttf_with_deps INTERFACE)
    target_link_libraries(sdl2_ttf_with_deps INTERFACE
        SDL2_ttf::SDL2_ttf-static
        brotlidec
        brotlicommon)
    target_link_libraries(superbrummiekart PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image-static
        sdl2_ttf_with_deps)

    # The console has no filesystem, so link every asset into the executable and
    # let the platform layer read it back from memory.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    set(EMBEDDED_ASSETS_NAME embedded_assets)
    set(EMBEDDED_ASSETS_DIR ${CMAKE_CURRENT_BINARY_DIR}/assets)
    set(EMBEDDED_ASSETS_BLOB ${EMBEDDED_ASSETS_DIR}/${EMBEDDED_ASSETS_NAME}.dat)

    set(EMBEDDED_ASSET_PATHS ${ASSET_FILES})
    foreach(dir IN LISTS ASSET_DIRS)
        file(GLOB_RECURSE dirFiles RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${dir}/*")
        list(APPEND EMBEDDED_ASSET_PATHS ${dirFiles})
    endforeach()

    # Recursive globs also report directories, which cannot be embedded.
    set(EMBEDDED_ASSETS "")
    foreach(asset IN LISTS EMBEDDED_ASSET_PATHS)
        if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
            list(APPEND EMBEDDED_ASSETS "${asset}")
        endif()
    endforeach()
    list(SORT EMBEDDED_ASSETS)
    list(REMOVE_DUPLICATES EMBEDDED_ASSETS)

    add_custom_command(
        OUTPUT ${EMBEDDED_ASSETS_BLOB}
               ${EMBEDDED_ASSETS_DIR}/${EMBEDDED_ASSETS_NAME}.c
               ${EMBEDDED_ASSETS_DIR}/${EMBEDDED_ASSETS_NAME}.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${EMBEDDED_ASSETS_DIR}
        COMMAND Python3::Interpreter
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed-assets.py
                --source-dir ${CMAKE_CURRENT_SOURCE_DIR}
                --output-dir ${EMBEDDED_ASSETS_DIR}
                --name ${EMBEDDED_ASSETS_NAME}
                ${EMBEDDED_ASSETS}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed-assets.py ${EMBEDDED_ASSETS}
        COMMENT "Embedding ${EMBEDDED_ASSETS_NAME} for the Wii"
        VERBATIM)

    target_sources(superbrummiekart PRIVATE
        ${EMBEDDED_ASSETS_DIR}/${EMBEDDED_ASSETS_NAME}.c)
    target_include_directories(superbrummiekart PRIVATE ${EMBEDDED_ASSETS_DIR})

    # bin2s turns the blob into an assembly file and a header declaring its
    # symbol; linking the object library adds that header's directory to the
    # include path. dkp_add_embedded_binary_library() refuses paths that do not
    # exist yet, so the blob is described by a target instead of a plain path.
    include(${DEVKITPRO}/cmake/dkp-embedded-binary.cmake)
    add_custom_target(superbrummiekart_assets_blob DEPENDS ${EMBEDDED_ASSETS_BLOB})
    dkp_set_target_file(superbrummiekart_assets_blob ${EMBEDDED_ASSETS_BLOB})
    dkp_add_embedded_binary_library(superbrummiekart_assets superbrummiekart_assets_blob)
    target_link_libraries(superbrummiekart PRIVATE superbrummiekart_assets)

    ogc_create_dol(superbrummiekart)

else()
    # ------------------------------------------------------------------
    # Desktop (native) build
    # ------------------------------------------------------------------

    target_sources(superbrummiekart PRIVATE platform_sdl.c)

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
