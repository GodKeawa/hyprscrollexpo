cmake_minimum_required(VERSION 3.27)

project(hyprexpo
    DESCRIPTION "hyprexpo plugin for Hyprland"
    VERSION 0.1
)

set(CMAKE_CXX_STANDARD 23)

set(SRC
    main.cpp
    ExpoGesture.cpp
    OverviewPassElement.cpp
    overview.cpp
    scrollOverview.cpp
)

add_library(hyprexpo SHARED ${SRC})

target_compile_options(hyprexpo PRIVATE -std=c++2b -Wno-narrowing)

find_package(PkgConfig REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    libinput
    libudev
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)
target_link_libraries(hyprexpo PRIVATE rt PkgConfig::deps)

install(TARGETS hyprexpo)
