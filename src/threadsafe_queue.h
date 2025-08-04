#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

// FFmpeg headers are needed because the queues store FFmpeg types.
extern "C"
{
#include <libavcodec/avcodec.h>
}

// 线程安全的Packet队列
struct PacketQueue
{
    std::queue<AVPacket *> queue;
    std::mutex mutex;
    std::condition_variable cond;
    int max_size = 300;
    std::atomic<bool> quit{false};

    void push(AVPacket *pkt)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]
                  { return queue.size() < max_size || quit; });
        if (quit)
        {
            if (pkt)
                av_packet_free(&pkt);
            return;
        }
        queue.push(pkt);
        lock.unlock();
        cond.notify_one();
    }

    AVPacket *pop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]
                  { return !queue.empty() || quit; });
        if (quit && queue.empty())
        {
            return nullptr;
        }
        AVPacket *pkt = queue.front();
        queue.pop();
        lock.unlock();
        cond.notify_one();
        return pkt;
    }

    size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }

    void abort()
    {
        quit = true;
        cond.notify_all();
    }

    void flush()
    {
        std::lock_guard<std::mutex> lock(mutex);
        while (!queue.empty())
        {
            AVPacket *pkt = queue.front();
            queue.pop();
            av_packet_free(&pkt);
        }
    }
};

// 线程安全的Frame队列
struct FrameQueue
{
    std::queue<AVFrame *> queue;
    std::mutex mutex;
    std::condition_variable cond;
    int max_size = 30;
    std::atomic<bool> quit{false};

    void push(AVFrame *frame)
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]
                  { return queue.size() < max_size || quit; });
        if (quit)
        {
            if (frame)
                av_frame_free(&frame);
            return;
        }
        queue.push(frame);
        lock.unlock();
        cond.notify_one();
    }

    AVFrame *pop()
    {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]
                  { return !queue.empty() || quit; });
        if (quit && queue.empty())
        {
            return nullptr;
        }
        AVFrame *frame = queue.front();
        queue.pop();
        lock.unlock();
        cond.notify_one();
        return frame;
    }

    AVFrame *peek()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty())
        {
            return nullptr;
        }
        return queue.front();
    }

    void abort()
    {
        quit = true;
        cond.notify_all();
    }

    void flush()
    {
        std::lock_guard<std::mutex> lock(mutex);
        while (!queue.empty())
        {
            AVFrame *frame = queue.front();
            queue.pop();
            av_frame_free(&frame);
        }
    }
};