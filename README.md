# OpenGLPlayer - 基于 FFmpeg 和 OpenGL 的视频播放器

这是一个使用 C++17 编写的跨平台视频播放器项目。它利用 FFmpeg 库进行音视频的解封装和解码，使用 SDL2 进行窗口创建、事件处理和音频输出，并借助 OpenGL 实现高效的硬件加速视频渲染。

## 功能特性

- **多线程架构**: 采用独立的解封装线程、视频解码线程和音频解码线程，确保了数据处理的并行性，有效避免了IO阻塞和解码耗时操作影响UI响应，提升了播放的流畅度。
- **精确的音视频同步**: 以音频播放时间作为主时钟，视频帧的显示时间戳会与音频时钟进行比对，动态调整渲染延迟，实现了精准的音视频同步。
- **动态追帧/丢帧策略**: 当视频播放显著落后于音频时，播放器会自动丢弃部分视频帧，快速追赶上音频进度，保证用户的观看体验。
- **高效的 GPU 渲染**: 视频帧的 YUV 数据被上传到 GPU，通过自定义的 GLSL 着色器（Shader）在 GPU 端完成到 RGB 的色彩空间转换和渲染，充分利用硬件加速，显著降低 CPU 占用。
- **跨平台设计**: 代码中包含了对 macOS 和 Linux 平台的条件编译，能够轻松适配不同操作系统。

## 环境依赖

在编译和运行本项目之前，请确保您的系统已经安装了以下依赖库：

- **CMake** (>= 3.10)
- **C++17 编译器** (例如 GCC, Clang)
- **PkgConfig**
- **FFmpeg** (libavformat, libavcodec, libavutil, libswscale, libswresample)
- **SDL2**
- **OpenGL**
- **GLEW** (仅Linux)

#### 在 Debian/Ubuntu 上的安装命令:

```bash
sudo apt update
sudo apt install build-essential cmake pkg-config
sudo apt install libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev
sudo apt install libsdl2-dev libglew-dev libopengl-dev
```

#### 在 macOS 上的安装命令 (使用 Homebrew):

```bash
brew install cmake pkg-config ffmpeg sdl2
```
*macOS 不需要额外安装 GLEW 和 OpenGL，系统框架已提供支持。*

## 编译与运行

1.  **创建构建目录**
    进入项目根目录，创建一个 `build` 文件夹用于存放编译产物。
    ```bash
    mkdir build
    cd build
    ```

2.  **运行 CMake 生成构建系统**
    ```bash
    cmake ..
    ```

3.  **编译项目**
    ```bash
    make
    ```

4.  **运行播放器**
    编译成功后，`build` 目录下会生成名为 `video_player` 的可执行文件。通过命令行参数指定要播放的视频文件路径来运行它。
    ```bash
    ./video_player /path/to/your/video.mp4
    ```

## 📂 项目结构

```
video_player/
├── CMakeLists.txt         # 项目构建脚本
├── main.cpp               # 程序主入口，负责启动播放器
├── VideoPlayer.h          # 播放器核心类头文件
├── VideoPlayer.cpp        # 播放器核心类实现，包含所有逻辑
└── queue.h                # 线程安全的帧队列和包队列实现
```

- **`CMakeLists.txt`**: 定义了项目的依赖项、源文件、头文件路径和链接库，是项目构建的核心。
- **`main.cpp`**: 简单的程序入口，解析命令行传入的视频文件路径，并实例化 `VideoPlayer` 类来启动播放流程。
- **`VideoPlayer.h/.cpp`**: 项目的核心。这个类封装了所有与 FFmpeg、SDL 和 OpenGL 相关的复杂操作，包括：
    - 打开和解析视频文件。
    - 初始化解码器和硬件环境。
    - 创建和管理三个核心线程（解封装、音/视频解码）。
    - 实现音视频同步逻辑。
    - 管理 OpenGL 资源（纹理、着色器）并渲染视频帧。
    - 处理音频重采样和回调。
- **`queue.h`**: 提供了两个线程安全的队列：`PacketQueue` 用于存储解封装后的音视频包（AVPacket），`FrameQueue` 用于存储解码后的音视频帧（AVFrame）。它们是实现多线程生产者-消费者模型的关键。


