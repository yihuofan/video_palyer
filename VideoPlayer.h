// VideoPlayer.h
#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include "queue.h"

// --- FIX: Include SDL header directly to avoid type conflicts ---
#include <SDL2/SDL.h>

// Forward declarations for FFmpeg and OpenGL types
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;
struct SwrContext;
struct AVFrame;
typedef unsigned int GLuint;

class VideoPlayer
{
public:
    VideoPlayer(const std::string &file);
    ~VideoPlayer();

    void open();
    void start();

private:
    void cleanup();

    // Initialization
    void init_codec_context(int stream_index, AVCodecContext **codec_ctx, const std::string &type);
    void init_sdl_video();
    void init_sdl_audio();
    void setup_shaders(int video_w, int video_h);

    // Threading
    void demux_thread_entry();
    void video_decode_thread_entry();
    void audio_decode_thread_entry();

    // Main Loop & Rendering
    void main_loop();
    void render_video_frame();
    void display_frame(AVFrame *frame);

    // Audio
    static void audio_callback(void *userdata, Uint8 *stream, int len);
    int resample_audio_frame();

    // Sync
    double get_audio_clock();

    // Helper for shaders
    static GLuint compile_shader(unsigned int type, const char *src);
    static GLuint link_program(GLuint vs, GLuint fs);

    // --- Member Variables ---
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
    SDL_GLContext gl_context = nullptr;
    SDL_AudioDeviceID audio_device = 0;

    GLuint tex_y = 0, tex_u = 0, tex_v = 0;
    GLuint shader_program = 0;
    GLuint vao = 0, vbo = 0;

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

    // Sync
    double audio_clock = 0.0;
    std::mutex audio_clock_mutex;
    double frame_timer = 0.0;
    double frame_last_pts = 0.0;
    double frame_last_delay = 0.0;

    // Audio Buffer
    uint8_t audio_buf[(192000 * 3) / 2];
    unsigned int audio_buf_size = 0;
    unsigned int audio_buf_index = 0;
};