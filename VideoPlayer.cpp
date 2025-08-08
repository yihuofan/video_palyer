#include "VideoPlayer.h"
#include <iostream>
#include <stdexcept>
#include <functional>
#include <chrono>
#include <cstring>
#include <memory>


extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
}


#ifndef __APPLE__
#include <GL/glew.h>
#else
#include <OpenGL/gl3.h>
#endif


#define AV_SYNC_THRESHOLD 0.01
#define AV_NOSYNC_THRESHOLD 1.0



VideoPlayer::VideoPlayer(const std::string &file) : filename(file) {}
VideoPlayer::~VideoPlayer() { cleanup(); }


void VideoPlayer::render_video_frame()
{
    AVFrame *frame = video_frame_q.pop();
    if (!frame)
    {
        quit = true;
        return;
    }

    auto frame_deleter = [](AVFrame *f)
    { av_frame_free(&f); };
    std::unique_ptr<AVFrame, decltype(frame_deleter)> frame_ptr(frame, frame_deleter);

    double video_pts = (frame->best_effort_timestamp == AV_NOPTS_VALUE) ? 0 : frame->best_effort_timestamp;
    video_pts *= av_q2d(video_stream->time_base);
    if (video_pts == 0)
    {
        video_pts = frame_last_pts + frame_last_delay;
    }

    double frame_delay = video_pts - frame_last_pts;
    if (frame_delay <= 0 || frame_delay > 1.0)
    {
        double fps = av_q2d(video_stream->avg_frame_rate);
        frame_delay = (fps > 0) ? (1.0 / fps) : 0.040;
    }

    frame_last_delay = frame_delay;
    frame_last_pts = video_pts;

    double audio_pts = get_audio_clock();
    double diff = video_pts - audio_pts;

    if (diff < -AV_NOSYNC_THRESHOLD)
    {
        std::cout << "Video is too far behind audio (" << diff << "s). Dropping frame." << std::endl;
        return; 
    }

    double sync_delay = frame_delay + diff;
    if (sync_delay < AV_SYNC_THRESHOLD)
        sync_delay = AV_SYNC_THRESHOLD;

    frame_timer += sync_delay;
    double actual_delay = frame_timer - (double)av_gettime() / 1000000.0;
    if (actual_delay < 0.010)
        actual_delay = 0.010;

    SDL_Delay(static_cast<Uint32>(actual_delay * 1000 + 0.5));
    display_frame(frame);
}

int VideoPlayer::resample_audio_frame()
{
    AVFrame *frame = audio_frame_q.pop();
    if (!frame)
        return -1;

    
    auto frame_deleter = [](AVFrame *f)
    { av_frame_free(&f); };
    std::unique_ptr<AVFrame, decltype(frame_deleter)> frame_ptr(frame, frame_deleter);

    if (frame->best_effort_timestamp != AV_NOPTS_VALUE)
    {
        std::lock_guard<std::mutex> lock(audio_clock_mutex);
        audio_clock = frame->best_effort_timestamp * av_q2d(audio_stream->time_base);
    }

    uint8_t *out_buffer = audio_buf;
    int out_channels = 2;
    int max_out_samples = sizeof(audio_buf) / (out_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16));

    int converted_samples = swr_convert(swr_ctx,
                                        &out_buffer, max_out_samples,
                                        (const uint8_t **)frame->data, frame->nb_samples);
    if (converted_samples < 0)
    {
        std::cerr << "swr_convert failed" << std::endl;
        return -1;
    }
    return converted_samples * out_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
}

void VideoPlayer::open()
{
    avformat_network_init();
    if (avformat_open_input(&format_ctx, filename.c_str(), nullptr, nullptr) != 0)
    {
        throw std::runtime_error("Could not open file: " + filename);
    }
    if (avformat_find_stream_info(format_ctx, nullptr) < 0)
    {
        throw std::runtime_error("Could not find stream info.");
    }

    for (unsigned int i = 0; i < format_ctx->nb_streams; i++)
    {
        auto stream = format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_index == -1)
        {
            video_stream_index = i;
            video_stream = stream;
        }
        else if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_index == -1)
        {
            audio_stream_index = i;
            audio_stream = stream;
        }
    }
    if (video_stream_index == -1)
        throw std::runtime_error("No video stream found.");

    init_codec_context(video_stream_index, &video_codec_ctx, "video");
    if (audio_stream_index != -1)
    {
        init_codec_context(audio_stream_index, &audio_codec_ctx, "audio");
    }
}

void VideoPlayer::start()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        throw std::runtime_error("SDL_Init failed: " + std::string(SDL_GetError()));
    }

    init_sdl_video();
    if (audio_stream_index != -1)
    {
        init_sdl_audio();
    }

    frame_timer = (double)av_gettime() / 1000000.0;
    frame_last_delay = 40e-3;

    demux_thread = std::thread(&VideoPlayer::demux_thread_entry, this);
    video_decode_thread = std::thread(&VideoPlayer::video_decode_thread_entry, this);
    if (audio_stream_index != -1)
    {
        audio_decode_thread = std::thread(&VideoPlayer::audio_decode_thread_entry, this);
    }

    main_loop();
}

void VideoPlayer::init_codec_context(int stream_index, AVCodecContext **codec_ctx, const std::string &type)
{
    const AVCodec *codec = avcodec_find_decoder(format_ctx->streams[stream_index]->codecpar->codec_id);
    if (!codec)
        throw std::runtime_error("Unsupported " + type + " codec.");

    *codec_ctx = avcodec_alloc_context3(codec);
    if (!*codec_ctx)
        throw std::runtime_error("Could not allocate " + type + " codec context.");

    avcodec_parameters_to_context(*codec_ctx, format_ctx->streams[stream_index]->codecpar);

    if (codec->capabilities & AV_CODEC_CAP_FRAME_THREADS)
    {
        (*codec_ctx)->thread_type = FF_THREAD_FRAME;
    }
    else if (codec->capabilities & AV_CODEC_CAP_SLICE_THREADS)
    {
        (*codec_ctx)->thread_type = FF_THREAD_SLICE;
    }
    (*codec_ctx)->thread_count = 0; 

    if (avcodec_open2(*codec_ctx, codec, nullptr) < 0)
    {
        throw std::runtime_error("Could not open " + type + " codec.");
    }
}

GLuint VideoPlayer::compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        std::string what = std::string("Shader compile error: ") + buf;
        glDeleteShader(s);
        throw std::runtime_error(what);
    }
    return s;
}

GLuint VideoPlayer::link_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char buf[1024];
        glGetProgramInfoLog(p, sizeof(buf), nullptr, buf);
        std::string what = std::string("Program link error: ") + buf;
        glDeleteProgram(p);
        throw std::runtime_error(what);
    }
    return p;
}

void VideoPlayer::setup_shaders(int video_w, int video_h)
{
    const char *vertex_shader_src = R"(
        #version 330 core
        layout(location = 0) in vec2 aPos;
        layout(location = 1) in vec2 aTex;
        out vec2 TexCoord;
        void main()
        {
            gl_Position = vec4(aPos.xy, 0.0, 1.0);
            TexCoord = aTex;
        }
    )";

    const char *fragment_shader_src = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D tex_y;
        uniform sampler2D tex_u;
        uniform sampler2D tex_v;
        void main()
        {
            float y = texture(tex_y, TexCoord).r;
            float u = texture(tex_u, TexCoord).r - 0.5;
            float v = texture(tex_v, TexCoord).r - 0.5;
            float r = y + 1.402 * v;
            float g = y - 0.344136 * u - 0.714136 * v;
            float b = y + 1.772 * u;
            FragColor = vec4(r, g, b, 1.0);
        }
    )";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_src);
    shader_program = link_program(vs, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    float vertices[] = {
        -1.0f,
        1.0f,
        0.0f,
        0.0f,
        -1.0f,
        -1.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
        1.0f,
        0.0f,
        1.0f,
        -1.0f,
        1.0f,
        1.0f,
    };

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    glGenTextures(1, &tex_y);
    glGenTextures(1, &tex_u);
    glGenTextures(1, &tex_v);

    auto set_tex_params = []()
    {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };

    glBindTexture(GL_TEXTURE_2D, tex_y);
    set_tex_params();
    glBindTexture(GL_TEXTURE_2D, tex_u);
    set_tex_params();
    glBindTexture(GL_TEXTURE_2D, tex_v);
    set_tex_params();

    glUseProgram(shader_program);
    glUniform1i(glGetUniformLocation(shader_program, "tex_y"), 0);
    glUniform1i(glGetUniformLocation(shader_program, "tex_u"), 1);
    glUniform1i(glGetUniformLocation(shader_program, "tex_v"), 2);
    glUseProgram(0);
}

void VideoPlayer::init_sdl_video()
{
    int video_width = video_codec_ctx->width;
    int video_height = video_codec_ctx->height;

    window = SDL_CreateWindow("OpenGL_播放器", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              video_width, video_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!window)
        throw std::runtime_error("SDL_CreateWindow failed: " + std::string(SDL_GetError()));

#ifndef __APPLE__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context)
        throw std::runtime_error("SDL_GL_CreateContext failed: " + std::string(SDL_GetError()));

    if (SDL_GL_MakeCurrent(window, gl_context) != 0)
    {
        throw std::runtime_error("SDL_GL_MakeCurrent failed: " + std::string(SDL_GetError()));
    }

    SDL_GL_SetSwapInterval(1);

#ifndef __APPLE__
    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        throw std::runtime_error(std::string("glewInit failed: ") + (const char *)glewGetErrorString(err));
    }
    while (glGetError() != GL_NO_ERROR)
    {
    }
#endif

    yuv_frame = av_frame_alloc();
    if (!yuv_frame)
        throw std::runtime_error("Could not allocate YUV frame.");
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video_width, video_height, 1);
    uint8_t *buffer = (uint8_t *)av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, buffer, AV_PIX_FMT_YUV420P, video_width, video_height, 1);

    setup_shaders(video_width, video_height);
}

void VideoPlayer::init_sdl_audio()
{
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = audio_codec_ctx->sample_rate;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.silence = 0;
    want.samples = 1024;
    want.callback = audio_callback;
    want.userdata = this;

    audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_device == 0)
    {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
    }
    else
    {
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 2);
        swr_alloc_set_opts2(&swr_ctx,
                            &out_ch_layout, AV_SAMPLE_FMT_S16, have.freq,
                            &audio_codec_ctx->ch_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
                            0, nullptr);
        swr_init(swr_ctx);
        SDL_PauseAudioDevice(audio_device, 0);
    }
}

void VideoPlayer::demux_thread_entry()
{
    while (!quit)
    {
        if (video_q.size() > video_q.max_size ||
            (audio_stream_index != -1 && audio_q.size() > audio_q.max_size) ||
            video_frame_q.queue.size() > video_frame_q.max_size ||
            (audio_stream_index != -1 && audio_frame_q.queue.size() > audio_frame_q.max_size))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        AVPacket *packet = av_packet_alloc();
        if (av_read_frame(format_ctx, packet) < 0)
        {
            av_packet_free(&packet);
            break;
        }

        if (packet->stream_index == video_stream_index)
        {
            video_q.push(packet);
        }
        else if (packet->stream_index == audio_stream_index)
        {
            audio_q.push(packet);
        }
        else
        {
            av_packet_free(&packet);
        }
    }
    video_q.push(nullptr);
    if (audio_stream_index != -1)
        audio_q.push(nullptr);
}

void VideoPlayer::video_decode_thread_entry()
{
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        std::cerr << "Failed to allocate frame in video decode thread" << std::endl;
        return;
    }

    while (!quit)
    {
        AVPacket *pkt = video_q.pop();
        if (!pkt)
            break;

        if (avcodec_send_packet(video_codec_ctx, pkt) != 0)
        {
            av_packet_free(&pkt);
            continue;
        }
        av_packet_free(&pkt);

        while (true)
        {
            int ret = avcodec_receive_frame(video_codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0)
            {
                std::cerr << "Video decode error!" << std::endl;
                break;
            }
            video_frame_q.push(av_frame_clone(frame));
        }
    }

    avcodec_send_packet(video_codec_ctx, nullptr);
    while (true)
    {
        int ret = avcodec_receive_frame(video_codec_ctx, frame);
        if (ret != 0)
            break;
        video_frame_q.push(av_frame_clone(frame));
    }

    av_frame_free(&frame);
    video_frame_q.push(nullptr);
}

void VideoPlayer::audio_decode_thread_entry()
{
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        std::cerr << "Failed to allocate frame in audio decode thread" << std::endl;
        return;
    }

    while (!quit)
    {
        AVPacket *pkt = audio_q.pop();
        if (!pkt)
            break;

        if (avcodec_send_packet(audio_codec_ctx, pkt) != 0)
        {
            av_packet_free(&pkt);
            continue;
        }
        av_packet_free(&pkt);

        while (true)
        {
            int ret = avcodec_receive_frame(audio_codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            else if (ret < 0)
            {
                std::cerr << "Audio decode error!" << std::endl;
                break;
            }
            audio_frame_q.push(av_frame_clone(frame));
        }
    }

    avcodec_send_packet(audio_codec_ctx, nullptr);
    while (true)
    {
        int ret = avcodec_receive_frame(audio_codec_ctx, frame);
        if (ret != 0)
            break;
        audio_frame_q.push(av_frame_clone(frame));
    }

    av_frame_free(&frame);
    audio_frame_q.push(nullptr);
}

void VideoPlayer::main_loop()
{
    SDL_Event event;
    while (!quit)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
                quit = true;
            else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                glViewport(0, 0, event.window.data1, event.window.data2);
            }
        }
        if (quit)
            break;
        render_video_frame();
    }
}

void VideoPlayer::display_frame(AVFrame *frame)
{
    if (!sws_ctx)
    {
        sws_ctx = sws_getContext(video_codec_ctx->width, video_codec_ctx->height, video_codec_ctx->pix_fmt,
                                 video_codec_ctx->width, video_codec_ctx->height, AV_PIX_FMT_YUV420P,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    }
    sws_scale(sws_ctx, (const uint8_t *const *)frame->data, frame->linesize, 0, video_codec_ctx->height,
              yuv_frame->data, yuv_frame->linesize);

    int w = video_codec_ctx->width, h = video_codec_ctx->height;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_y);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, yuv_frame->data[0]);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_u);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w / 2, h / 2, 0, GL_RED, GL_UNSIGNED_BYTE, yuv_frame->data[1]);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex_v);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w / 2, h / 2, 0, GL_RED, GL_UNSIGNED_BYTE, yuv_frame->data[2]);

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_y);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex_u);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex_v);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);
    SDL_GL_SwapWindow(window);
}

void VideoPlayer::audio_callback(void *userdata, Uint8 *stream, int len)
{
    VideoPlayer *player = static_cast<VideoPlayer *>(userdata);
    SDL_memset(stream, 0, len);

    while (len > 0)
    {
        if (player->quit)
            return;

        if (player->audio_buf_index >= player->audio_buf_size)
        {
            int audio_size = player->resample_audio_frame();
            if (audio_size < 0)
            {
                player->audio_buf_size = 1024;
                memset(player->audio_buf, 0, player->audio_buf_size);
            }
            else
            {
                player->audio_buf_size = audio_size;
            }
            player->audio_buf_index = 0;
        }

        int len_to_copy = player->audio_buf_size - player->audio_buf_index;
        if (len_to_copy > len)
            len_to_copy = len;

        SDL_MixAudioFormat(stream, player->audio_buf + player->audio_buf_index, AUDIO_S16SYS, len_to_copy, SDL_MIX_MAXVOLUME);

        len -= len_to_copy;
        stream += len_to_copy;
        player->audio_buf_index += len_to_copy;
    }
}

double VideoPlayer::get_audio_clock()
{
    if (audio_stream_index == -1)
        return (double)av_gettime() / 1000000.0;

    std::lock_guard<std::mutex> lock(audio_clock_mutex);
    double pts = audio_clock;
    int hw_buf_size = audio_buf_size - audio_buf_index;
    int bytes_per_sec = 0;
    if (audio_stream)
    {
        bytes_per_sec = audio_codec_ctx->sample_rate * 2 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    }
    if (bytes_per_sec > 0)
    {
        pts -= (double)hw_buf_size / bytes_per_sec;
    }
    return pts;
}

void VideoPlayer::cleanup()
{
    quit = true;

    audio_q.abort();
    video_q.abort();
    video_frame_q.abort();
    if (audio_stream_index != -1)
        audio_frame_q.abort();

    if (demux_thread.joinable())
        demux_thread.join();
    if (video_decode_thread.joinable())
        video_decode_thread.join();
    if (audio_decode_thread.joinable())
        audio_decode_thread.join();

    audio_q.flush();
    video_q.flush();
    video_frame_q.flush();
    if (audio_stream_index != -1)
        audio_frame_q.flush();

    if (audio_device)
        SDL_CloseAudioDevice(audio_device);

    if (yuv_frame)
    {
        av_freep(&yuv_frame->data[0]);
        av_frame_free(&yuv_frame);
    }

    if (shader_program)
        glDeleteProgram(shader_program);
    if (vao)
        glDeleteVertexArrays(1, &vao);
    if (vbo)
        glDeleteBuffers(1, &vbo);
    if (tex_y)
        glDeleteTextures(1, &tex_y);
    if (tex_u)
        glDeleteTextures(1, &tex_u);
    if (tex_v)
        glDeleteTextures(1, &tex_v);

    if (gl_context)
        SDL_GL_DeleteContext(gl_context);
    if (window)
        SDL_DestroyWindow(window);
    SDL_Quit();

    if (sws_ctx)
        sws_freeContext(sws_ctx);
    if (swr_ctx)
        swr_free(&swr_ctx);
    if (video_codec_ctx)
        avcodec_free_context(&video_codec_ctx);
    if (audio_codec_ctx)
        avcodec_free_context(&audio_codec_ctx);
    if (format_ctx)
        avformat_close_input(&format_ctx);
}