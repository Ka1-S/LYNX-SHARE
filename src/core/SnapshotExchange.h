#pragma once

// Hands immutable objects from the UI to the audio thread without locks or
// allocation/deallocation on the audio thread.
//
//  - UI:    publish(new)    → puts it in the inbox (replaces one not yet read)
//           collectGarbage() → frees the objects the audio thread returned
//  - Audio: acquire()       → takes the most recent, returns the previous one for the UI to free
//
// Only one UI thread and one audio thread may use the same instance.

#include <array>
#include <atomic>
#include <functional>
#include <memory>

namespace lynx
{
template <typename T>
class SnapshotExchange
{
public:
    SnapshotExchange() = default;
    SnapshotExchange (const SnapshotExchange&) = delete;
    SnapshotExchange& operator= (const SnapshotExchange&) = delete;

    ~SnapshotExchange()
    {
        dispose = nullptr;
        delete incoming.exchange (nullptr);
        delete active;
        collectGarbage();
    }

    // UI
    void publish (std::unique_ptr<const T> next)
    {
        collectGarbage();
        delete incoming.exchange (next.release(), std::memory_order_acq_rel); // an unread one is simply swapped
    }

    // UI
    void collectGarbage()
    {
        for (auto& slot : retired)
            if (const auto* old = slot.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (dispose)
                    dispose (old);
                else
                    delete old;
            }
    }

    // UI: set, the objects the audio thread returned go here instead of being deleted at once (the owner frees them
    // later, e.g. outside the timer callback that collected them: issue #36). Set before the first publish.
    std::function<void (const T*)> dispose;

    // Audio: returns the current snapshot (may be nullptr before the first publish).
    const T* acquire() noexcept
    {
        if (incoming.load (std::memory_order_relaxed) == nullptr)
            return active;

        // Needs a free slot to return the previous one; with no slot, keeps the current one
        // and tries again on the next block.
        std::atomic<const T*>* freeSlot = nullptr;

        if (active != nullptr)
        {
            for (auto& slot : retired)
                if (slot.load (std::memory_order_acquire) == nullptr)
                {
                    freeSlot = &slot;
                    break;
                }

            if (freeSlot == nullptr)
                return active;
        }

        auto* next = incoming.exchange (nullptr, std::memory_order_acq_rel);

        if (next == nullptr)
            return active;

        if (freeSlot != nullptr)
            freeSlot->store (active, std::memory_order_release);

        active = next;
        return active;
    }

private:
    std::atomic<const T*> incoming { nullptr };
    std::array<std::atomic<const T*>, 8> retired {};
    const T* active = nullptr; // only the audio thread touches it
};
} // namespace lynx
