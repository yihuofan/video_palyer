cmake_minimum_required(VERSION 3.10)

project(FluentPlayer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖项
find_package(PkgConfig REQUIRED)

# 查找 SDL2
pkg_check_modules(SDL2 REQUIRED sdl2)
if(NOT SDL2_FOUND)
    message(FATAL_ERROR "SDL2 not found! Please install libsdl2-dev or equivalent.")
endif()

# 查找 FFmpeg 库
pkg_check_modules(FFMPEG REQUIRED libavformat libavcodec libavutil libswscale libswresample)
if(NOT FFMPEG_FOUND)
    message(FATAL_ERROR "FFmpeg libraries not found! Please install ffmpeg development packages.")
endif()

# 设置源文件
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(SOURCES
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/videoplayer.cpp
)

# 创建可执行文件
add_executable(Video_player ${SOURCES})

# 设置头文件搜索路径
target_include_directories(Video_player PUBLIC
    ${SRC_DIR}
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(Video_player PUBLIC
    ${SDL2_LIBRARIES}
    ${FFMPEG_LIBRARIES}
)