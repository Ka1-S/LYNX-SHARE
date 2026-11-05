#pragma once

// An object a worker thread uses but another thread (the message thread) must create and delete, e.g. an export job
// whose plugins live on the message thread (ui/ExportDialog.cpp). Round 169g (crash reports): the stems and playlist-
// track exports asked the message thread to create / delete their job and waited for it. Cancel made the message
// thread wait in stopThread for the export thread, which waited for the message thread: after JUCE's 10 s timeout the
// export thread was killed and the queued call then ran on its freed locals (a 10 s "not responding", then an access
// violation in the queued lambda). Here the worker stops waiting as soon as it is asked to stop; the object lives in a
// shared state that the queued calls keep alive, and it is still created and deleted only by the posted calls.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>

namespace lynx
{
template <typename T>
class MessageThreadOwned
{
public:
    using Post = std::function<void (std::function<void()>)>; // runs a call on the owning thread, later

    MessageThreadOwned (Post p, std::function<bool()> stop) : post (std::move (p)), shouldStop (std::move (stop)) {}
    ~MessageThreadOwned() { release(); }
    MessageThreadOwned (const MessageThreadOwned&) = delete;
    MessageThreadOwned& operator= (const MessageThreadOwned&) = delete;

    // the new object, made by `make` on the owning thread; nullptr when the worker was asked to stop first (`make`
    // then never runs, or its object is deleted by release() on the owning thread)
    T* create (std::function<std::unique_ptr<T>()> make)
    {
        release();
        auto step = std::make_shared<Step>();
        post ([s = state, step, make = std::move (make)] {
            if (! step->abandoned.load())
                s->object = make();
            step->signal();
        });
        pending = true; // (from here on an object may exist: release() queues its deletion)
        if (! wait (*step))
            return nullptr;
        return state->object.get();
    }

    // deletes the object on the owning thread; waits for it unless the worker is asked to stop
    void release()
    {
        if (! pending)
            return;
        pending = false;
        auto step = std::make_shared<Step>();
        post ([s = state, step] {
            s->object.reset();
            step->signal();
        });
        wait (*step);
    }

private:
    struct State
    {
        std::unique_ptr<T> object;
    };
    struct Step
    {
        std::mutex lock;
        std::condition_variable cv;
        bool done = false;
        std::atomic<bool> abandoned { false };
        void signal()
        {
            {
                const std::lock_guard<std::mutex> g (lock);
                done = true;
            }
            cv.notify_all();
        }
    };

    bool wait (Step& step)
    {
        std::unique_lock<std::mutex> g (step.lock);
        while (! step.done)
        {
            if (shouldStop())
            {
                step.abandoned = true;
                return false;
            }
            step.cv.wait_for (g, std::chrono::milliseconds (20));
        }
        return true;
    }

    Post post;
    std::function<bool()> shouldStop;
    std::shared_ptr<State> state = std::make_shared<State>();
    bool pending = false;
};
} // namespace lynx
