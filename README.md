# FluentPlayer (流畅播放器)

一个使用 C++, FFmpeg 和 SDL2 构建的简单视频播放器。该项目旨在演示一个多线程、支持音视频同步的媒体播放器核心逻辑。

---
## 🔮 未来计划

下一步的改进方向是引入 OpenGL 进行视频渲染，以替代当前基于 CPU 的 `SDL_Renderer` 方案。

- **使用 OpenGL 优化渲染**:
    - 将 YUV 到 RGB 的色彩空间转换工作交由 GPU 片段着色器处理，显著降低 CPU 占用。
    - 使用 GPU 进行硬件加速的纹理上传和渲染，提高高分辨率视频的播放性能。
- **实现高级视觉效果**:
    - 通过编写自定义着色器（Shader），可以轻松实现实时视频滤镜（如灰度、怀旧）、亮度/对比度调节等功能。
    - 实现更高质量的视频缩放和后处理效果。

## ✨ 功能特性

- **播放音视频**: 支持播放包含视频和音频流的媒体文件。
- **FFmpeg 驱动**: 使用强大的 FFmpeg 库进行解复用和解码。
- **SDL2 渲染**: 使用 SDL2 库进行视频渲染和音频播放。
- **多线程架构**:
    - 一个**解复用线程**，用于从文件中读取数据包。
    - 独立的**视频解码线程**和**音频解码线程**。
- **线程安全队列**: 使用自定义的线程安全队列在线程之间安全地传递数据包和数据帧。
- **音视频同步**: 实现基本的音视频同步机制，以音频时钟为主时钟，保证流畅播放。
- **可伸缩窗口**: 播放器窗口支持调整大小。

## ⚙️ 依赖环境

在编译和运行本项目之前，确保已安装以下依赖：

- `CMake` (版本 >= 3.10)
- 支持 C++17 的编译器 (如 g++, Clang)
- `pkg-config`
- FFmpeg 开发库 (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`)
- SDL2 开发库

#### 在 Debian/Ubuntu 上的安装命令

```bash
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config libavformat-dev libavcodec-dev libavutil-dev libswscale-dev libswresample-dev libsdl2-dev
```

## 🚀 如何编译

1.  **克隆或下载项目**
    项目下载到 `video_palyer` 目录。

    ```bash
    cd video_palyer
    ```

2.  **创建并进入 build 目录**

    ```bash
    mkdir build
    cd build
    ```

3.  **运行 CMake 生成构建文件**

    ```bash
    cmake ..
    ```

4.  **编译项目**

    ```bash
    make
    ```

编译成功后，将在 `build` 目录下生成名为 `Video_player` 的可执行文件。

## ▶️ 如何运行

在项目根目录下执行以下命令来播放视频文件：

```bash
./build/Video_player /path/to/your/video.mp4
```

请将 `/path/to/your/video.mp4` 替换为实际的视频文件路径。

## 📂 项目结构

```
video_palyer/
├── CMakeLists.txt         # CMake 构建配置文件
├── src/                   # 源代码目录
│   ├── main.cpp           # 程序主入口
│   ├── videoplayer.h      # VideoPlayer 类的头文件
│   ├── videoplayer.cpp    # VideoPlayer 类的实现
│   └── threadsafe_queue.h # 线程安全队列的实现
└── build/                 # 编译输出目录
```

## 📘 代码架构概览

本播放器采用多线程的生产者-消费者模型：

1.  **解复用线程 (`demux_thread_entry`)**:
    - **职责**: 作为生产者，从媒体文件中读取 `AVPacket`。
    - **流程**: 调用 `av_read_frame`，将读取到的视频和音频包分别推入对应的 `PacketQueue`。

2.  **解码线程 (`video_decode_thread_entry`, `audio_decode_thread_entry`)**:
    - **职责**: 作为消费者（对于 PacketQueue）和生产者（对于 FrameQueue）。
    - **流程**: 从 `PacketQueue` 中取出 `AVPacket`，解码成 `AVFrame`，然后将解码后的 `AVFrame` 推入对应的 `FrameQueue`。

3.  **主线程 (`main_loop`)**:
    - **职责**: 管理 SDL 窗口、处理用户事件和渲染视频。
    - **流程**: 从 `video_frame_q` 中取出视频帧，根据音频时钟计算正确的显示时间，执行延迟后，将视频帧渲染到屏幕上。

4.  **音频回调 (`audio_callback`)**:
    - **职责**: 向音频设备提供PCM数据。
    - **流程**: 此函数由 SDL 音频子系统在需要数据时自动调用。它从 `audio_frame_q` 中取出音频帧，进行重采样，然后将数据拷贝到 SDL 提供的缓冲区。此回调的执行频率驱动着整个播放器的**主时钟（音频时钟）**。
