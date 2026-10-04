cmake_minimum_required(VERSION 3.24)
project(KitBasher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
option(KITBASHER_ENABLE_OPENXR "Build OpenXR headset support" ON)
option(KITBASHER_ENABLE_JOLT "Build Jolt Physics support" ON)
option(KITBASHER_ENABLE_LUA "Build Lua scripting support" ON)
option(KITBASHER_ENABLE_STEAM "Build Steamworks multiplayer support" ON)

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(TINYGLTF_HEADER_ONLY OFF CACHE BOOL "" FORCE)
set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_LOADER_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_GL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_VALIDATOR_EXAMPLE OFF CACHE BOOL "" FORCE)
set(TINYGLTF_BUILD_BUILDER_EXAMPLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(glad
    GIT_REPOSITORY https://github.com/Dav1dde/glad.git
    GIT_TAG        v0.1.36
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(tinygltf
    GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
    GIT_TAG        v2.9.3
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)

FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.21
    GIT_SHALLOW    TRUE
    SYSTEM
    EXCLUDE_FROM_ALL
)

if(KITBASHER_ENABLE_LUA)
    FetchContent_Declare(lua
        URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    FetchContent_Declare(luabridge
        GIT_REPOSITORY https://github.com/vinniefalco/LuaBridge.git
        GIT_TAG        2.9
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
endif()

if(KITBASHER_ENABLE_OPENXR)
    FetchContent_Declare(openxr
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK-Source.git
        GIT_TAG        release-1.1.46
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
endif()

if(KITBASHER_ENABLE_JOLT)
    set(USE_ASSERTS ON CACHE BOOL "" FORCE)
    set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
    set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(jolt
        GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
        GIT_TAG        v5.2.0
        GIT_SHALLOW    TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
endif()

FetchContent_MakeAvailable(glfw glm glad tinygltf imgui imguizmo stb miniaudio)

if(KITBASHER_ENABLE_LUA)
    FetchContent_MakeAvailable(lua luabridge)
    
    file(GLOB LUA_SOURCES CONFIGURE_DEPENDS
        ${lua_SOURCE_DIR}/src/*.c
    )
    list(REMOVE_ITEM LUA_SOURCES
        ${lua_SOURCE_DIR}/src/lua.c
        ${lua_SOURCE_DIR}/src/luac.c
    )
    add_library(lua STATIC ${LUA_SOURCES})
    target_include_directories(lua PUBLIC ${lua_SOURCE_DIR}/src)
    if(MSVC)
        target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

if(KITBASHER_ENABLE_OPENXR)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(openxr)
endif()

if(KITBASHER_ENABLE_JOLT)
    FetchContent_MakeAvailable(jolt)
    add_subdirectory(${jolt_SOURCE_DIR}/Build ${jolt_BINARY_DIR})
    if(MSVC AND TARGET Jolt)
        target_compile_options(Jolt PRIVATE /WX- /wd4865)
    endif()
endif()

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

add_library(imgui_backend STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_backend SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_include_directories(imgui_backend SYSTEM PRIVATE ${imguizmo_SOURCE_DIR}/src)

target_compile_definitions(imgui_backend PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
target_link_libraries(imgui_backend PUBLIC glad glfw OpenGL::GL)
if(MSVC)
    target_compile_options(imgui_backend PRIVATE /FI"glad/glad.h")
else()
    target_compile_options(imgui_backend PRIVATE -include glad/glad.h)
endif()

add_library(imguizmo_backend STATIC
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(imguizmo_backend SYSTEM PUBLIC
    ${imguizmo_SOURCE_DIR}/src
)
target_link_libraries(imguizmo_backend PUBLIC imgui_backend)

if(MSVC)
    target_compile_options(imguizmo_backend PRIVATE /wd4245)
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(KitBasher ${SOURCES})

target_include_directories(KitBasher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imguizmo_SOURCE_DIR}/src
    ${stb_SOURCE_DIR}
    ${miniaudio_SOURCE_DIR}
)
if(KITBASHER_ENABLE_LUA)
    target_include_directories(KitBasher PRIVATE ${luabridge_SOURCE_DIR}/Source)
endif()

target_compile_definitions(KitBasher PRIVATE 
    GLFW_INCLUDE_NONE 
    IMGUI_DEFINE_MATH_OPERATORS
    GLM_ENABLE_EXPERIMENTAL
)

if(KITBASHER_ENABLE_OPENXR)
    target_compile_definitions(KitBasher PRIVATE
        KITBASHER_ENABLE_OPENXR
        XR_USE_GRAPHICS_API_OPENGL
    )
    if(WIN32)
        target_compile_definitions(KitBasher PRIVATE XR_USE_PLATFORM_WIN32)
    endif()
    target_link_libraries(KitBasher PRIVATE openxr_loader)
endif()

if(KITBASHER_ENABLE_JOLT)
    target_compile_definitions(KitBasher PRIVATE KITBASHER_ENABLE_JOLT)
    target_include_directories(KitBasher PRIVATE ${jolt_SOURCE_DIR})
    target_link_libraries(KitBasher PRIVATE Jolt)
endif()

if(KITBASHER_ENABLE_STEAM)
    set(STEAMWORKS_ROOT ${CMAKE_SOURCE_DIR}/third_party/steamworks)
    target_compile_definitions(KitBasher PRIVATE KITBASHER_ENABLE_STEAM)
    target_include_directories(KitBasher PRIVATE
        ${STEAMWORKS_ROOT}/public
    )
    if(WIN32)
        target_link_libraries(KitBasher PRIVATE
            ${STEAMWORKS_ROOT}/redistributable_bin/win64/steam_api64.lib
        )
        add_custom_command(TARGET KitBasher POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STEAMWORKS_ROOT}/redistributable_bin/win64/steam_api64.dll"
            $<TARGET_FILE_DIR:KitBasher>
        )
    endif()
    file(GENERATE
        OUTPUT "$<TARGET_FILE_DIR:KitBasher>/steam_appid.txt"
        CONTENT "480"
    )
endif()

target_link_libraries(KitBasher PRIVATE
    glad
    glfw
    glm::glm
    tinygltf
    OpenGL::GL
    imgui_backend
    imguizmo_backend
)

if(WIN32)
    target_link_libraries(KitBasher PRIVATE winmm ole32 user32)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(KitBasher PRIVATE pthread dl m)
elseif(APPLE)
    target_link_libraries(KitBasher PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

if(KITBASHER_ENABLE_LUA)
    target_link_libraries(KitBasher PRIVATE lua)
    target_compile_definitions(KitBasher PRIVATE KITBASHER_ENABLE_LUA)
endif()

if(WIN32)
    target_compile_definitions(KitBasher PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
    )
endif()

if(MSVC)
    target_compile_options(KitBasher PRIVATE /W4 /permissive-)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                 PROPERTY VS_STARTUP_PROJECT KitBasher)
else()
    target_compile_options(KitBasher PRIVATE -Wall -Wextra)
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources)
    add_custom_command(TARGET KitBasher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            $<TARGET_FILE_DIR:KitBasher>/resources
    )
endif()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/game)
    add_custom_command(TARGET KitBasher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/game
            $<TARGET_FILE_DIR:KitBasher>/game
    )
endif()