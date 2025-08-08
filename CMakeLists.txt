cmake_minimum_required(VERSION 3.10)
project(OpenGLPlayer CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(AV REQUIRED libavformat libavcodec libavutil libswscale libswresample)
pkg_check_modules(SDL2 REQUIRED sdl2)

if(NOT APPLE)
    find_package(GLEW REQUIRED)
endif()

add_executable(video_player
    main.cpp
    VideoPlayer.cpp
)

target_include_directories(video_player PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${AV_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
    $<$<BOOL:${GLEW_FOUND}>:${GLEW_INCLUDE_DIRS}>
)

target_link_libraries(video_player PRIVATE
    ${AV_LIBRARIES}
    ${SDL2_LIBRARIES}
    OpenGL::GL
    Threads::Threads
    $<$<BOOL:${GLEW_FOUND}>:${GLEW_LIBRARIES}>
)

# macOS 特殊处理
if(APPLE)
    target_link_libraries(player_opengl PRIVATE "-framework OpenGL")
endif()