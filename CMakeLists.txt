# 设置CMake最低版本要求
cmake_minimum_required(VERSION 3.10.0)
# 设置项目名称和版本
project(Blueprint VERSION 0.1.0 LANGUAGES C CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

cmake_policy(SET CMP0077 NEW)
set(UUID_SYSTEM_GENERATOR ON)
add_subdirectory(libs/stduuid)
add_subdirectory(libs/spdlog)

add_library(glad libs/glad/src/glad.c)
target_include_directories(glad PUBLIC libs/glad/include)

# 添加imgui库
add_library(imgui
    # imgui源文件
    libs/imgui/imgui.cpp
    libs/imgui/imgui_draw.cpp
    libs/imgui/imgui_tables.cpp
    libs/imgui/imgui_widgets.cpp
    # imgui后端源文件
    libs/imgui/backends/imgui_impl_glfw.cpp
    libs/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC libs/imgui)
target_include_directories(imgui PUBLIC libs/glfw/include)
target_link_libraries(imgui PUBLIC glad)
if(WIN32)
    target_link_libraries(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw/lib-mingw-w64/glfw3.dll
        opengl32.lib
    )
elseif(APPLE)
    target_link_libraries(imgui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw/lib-mingw-w64/libglfw3.a
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )
elseif(UNIX)
    target_link_libraries(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/libs/glfw/lib-mingw-w64/libglfw3.a
        GL
        Xrandr Xxf86vm Xi Xcursor Xinerama pthread dl rt
    )
endif()


# 添加可执行文件
add_executable(${PROJECT_NAME} main.cpp
    src/VSE_Core.cpp
    src/VSE_Logger.cpp
)

# 设置可执行文件的包含目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    libs/json/include
    libs/sol2/include
    libs/glfw/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE imgui)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
target_link_libraries(${PROJECT_NAME} PRIVATE stduuid)
