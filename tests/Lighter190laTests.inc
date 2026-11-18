// Round 190la (0.1.30, "lighter than FL in every aspect"): platform/CvWait.h, the timed condition-variable wait that
// sleeps whole milliseconds rounded up instead of spinning its last one (libc++ on Windows). Its contract everywhere:
// true as soon as the predicate holds, false only once the timeout has passed (never early), and a notify wakes it.

#include "platform/CvWait.h"

#include <atomic>
#include <thread>

TEST (lighter190la_cvwait_predicate_already_true)
{
    std::mutex m;
    std::condition_variable cv;
    std::unique_lock<std::mutex> l (m);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK (lynx::cvwait::waitFor (cv, l, std::chrono::milliseconds (500), [] { return true; }));
    CHECK (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (100));
    CHECK (l.owns_lock());
}

TEST (lighter190la_cvwait_times_out_never_early)
{
    std::mutex m;
    std::condition_variable cv;
    for (const auto ms : { 1, 3, 17 })
    {
        std::unique_lock<std::mutex> l (m);
        const auto t0 = std::chrono::steady_clock::now();
        CHECK (! lynx::cvwait::waitFor (cv, l, std::chrono::milliseconds (ms), [] { return false; }));
        CHECK (std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds (ms));
        CHECK (l.owns_lock());
    }
    // (a fractional timeout too: the SampleSimilarity pause)
    std::unique_lock<std::mutex> l (m);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK (! lynx::cvwait::waitFor (cv, l, std::chrono::duration<double> (0.0025), [] { return false; }));
    CHECK (std::chrono::steady_clock::now() - t0 >= std::chrono::microseconds (2500));
}

TEST (lighter190la_cvwait_notify_wakes_it)
{
    std::mutex m;
    std::condition_variable cv;
    bool ready = false;
    std::thread other ([&] {
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
        {
            const std::lock_guard<std::mutex> g (m);
            ready = true;
        }
        cv.notify_all();
    });
    std::unique_lock<std::mutex> l (m);
    const auto t0 = std::chrono::steady_clock::now();
    CHECK (lynx::cvwait::waitFor (cv, l, std::chrono::seconds (10), [&] { return ready; }));
    CHECK (std::chrono::steady_clock::now() - t0 < std::chrono::seconds (5));
    l.unlock();
    other.join();
    // the overload without a predicate returns after at most its timeout (it may return early, as std's may)
    std::unique_lock<std::mutex> l2 (m);
    const auto t1 = std::chrono::steady_clock::now();
    lynx::cvwait::waitFor (cv, l2, std::chrono::milliseconds (5));
    CHECK (std::chrono::steady_clock::now() - t1 < std::chrono::seconds (5));
    CHECK (l2.owns_lock());
}
