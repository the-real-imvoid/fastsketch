set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.16)
project(FastSketch C CXX)

find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
find_package(Threads REQUIRED)

add_executable(FastSketch
    main.c

    src/drawops.c
    src/undo.c
    external/lz4/lz4.c
    src/rgba.c
    src/cursor.c
    src/shapes.c
    src/file.c
    src/stb_impl.c
    src/selection.c

    src/cui.cpp
)

target_sources(FastSketch PRIVATE
    external/imgui.cpp
    external/imgui_draw.cpp
    external/imgui_widgets.cpp
    external/imgui_tables.cpp

    external/imgui_impl_sdl2.cpp
    external/imgui_impl_sdlrenderer2.cpp
)

# Clean header paths (allows using <SDL.h> instead of <SDL2/SDL.h>)
target_include_directories(FastSketch PRIVATE
    include
    external
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

# Standard dynamic linking matching Void's native package layout
target_link_libraries(FastSketch PRIVATE
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${SDL2_LIBRARIES}
    m
    Threads::Threads
)
