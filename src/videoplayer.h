#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include "threadsafe_queue.h"

// 同步阈值
#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 1.0

// 前向声明 FFmpeg 和 SDL 结构体，避免在头文件中引入重量级头文件
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct SwrContext;
struct AVFrame;
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
typedef struct SDL_AudioSpec SDL_AudioSpec;
typedef unsigned int SDL_AudioDeviceID;
typedef unsigned char Uint8;

class VideoPlayer
{
public:
    VideoPlayer(const std::string &file);
    ~VideoPlayer();
    void open();
    void start();

private:
    // 初始化方法
    void init_codec_context(int stream_index, AVCodecContext **codec_ctx, const std::string &type);
    void init_sdl_video();
    void init_sdl_audio();

    // 线程函数
    void demux_thread_entry();
    void video_decode_thread_entry();
    void audio_decode_thread_entry();

    // 主循环和渲染
    void main_loop();
    void render_video_frame();
    void display_frame(AVFrame *frame);

    // 音频处理
    static void audio_callback(void *userdata, Uint8 *stream, int len);
    int resample_audio_frame();
    double get_audio_clock();

    // 清理资源
    void cleanup();

    // 成员变量
    std::string filename;
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *video_codec_ctx = nullptr;
    AVCodecContext *audio_codec_ctx = nullptr;
    AVStream *video_stream = nullptr;
    AVStream *audio_stream = nullptr;
    SwsContext *sws_ctx = nullptr;
    SwrContext *swr_ctx = nullptr;
    AVFrame *yuv_frame = nullptr;

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Texture *texture = nullptr;
    SDL_AudioDeviceID audio_device = 0;

    int video_stream_index = -1;
    int audio_stream_index = -1;

    std::thread demux_thread;
    std::thread video_decode_thread;
    std::thread audio_decode_thread;

    PacketQueue video_q;
    PacketQueue audio_q;
    FrameQueue video_frame_q;
    FrameQueue audio_frame_q;
    std::atomic<bool> quit{false};

    // 音视频同步
    double audio_clock = 0.0;
    std::mutex audio_clock_mutex;
    double frame_timer = 0.0;
    double frame_last_pts = 0.0;
    double frame_last_delay = 0.0;

    // 音频回调缓冲区
    uint8_t audio_buf[(192000 * 3) / 2];
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf_index = 0;
};