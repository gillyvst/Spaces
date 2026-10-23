#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <memory>

// Hands heap objects to the audio thread without locks or allocation there.
//
// A loader thread posts objects; the audio thread fetches the newest one.
// An older object the audio thread never saw is either deleted or, with
// post (object, merge), merged into the new one first (so a partial update
// doesn't lose the parts of an earlier one that haven't been applied). When the audio
// thread is done with an object it retires it, and the non-realtime side
// deletes retired objects in collectGarbage().
template <typename T>
class Mailbox
{
public:
    Mailbox() : retiredFifo (capacity) {}

    ~Mailbox()
    {
        delete pending.exchange (nullptr);
        collectGarbage();
    }

    // Non-realtime threads only.
    void post (std::unique_ptr<T> object)
    {
        delete pending.exchange (object.release());
        collectGarbage();
    }

    // merge (T& newer, T& older) is called if an unfetched older object was
    // still waiting. Only one thread may post to a Mailbox.
    template <typename Merge>
    void post (std::unique_ptr<T> object, Merge&& merge)
    {
        auto* newer = object.release();

        // Take back anything still pending, merge it in, then post. If the
        // audio thread grabs the old one first, exchange returns nullptr.
        if (std::unique_ptr<T> older { pending.exchange (nullptr) })
            merge (*newer, *older);

        delete pending.exchange (newer);
        collectGarbage();
    }

    // Audio thread: returns the newest object (now owned by the caller), or nullptr.
    // If the retired list is full the object stays queued until the next call.
    T* fetch() noexcept
    {
        if (retiredFifo.getFreeSpace() < 2)
            return nullptr;

        return pending.exchange (nullptr);
    }

    // Audio thread: give an object back for deletion elsewhere.
    void retire (T* object) noexcept
    {
        if (object == nullptr)
            return;

        const auto scope = retiredFifo.write (1);

        if (scope.blockSize1 > 0)
            retired[(size_t) scope.startIndex1] = object;
        else if (scope.blockSize2 > 0)
            retired[(size_t) scope.startIndex2] = object;
        else
            jassertfalse;   // fetch() guarantees space, so this can't happen
    }

    // Non-realtime threads only. Safe to call from several threads.
    void collectGarbage()
    {
        const juce::SpinLock::ScopedLockType lock (garbageLock);
        const auto scope = retiredFifo.read (retiredFifo.getNumReady());
        scope.forEach ([this] (int index)
        {
            delete retired[(size_t) index];
            retired[(size_t) index] = nullptr;
        });
    }

private:
    static constexpr int capacity = 32;

    std::atomic<T*> pending { nullptr };
    juce::AbstractFifo retiredFifo;
    std::array<T*, capacity> retired {};
    juce::SpinLock garbageLock;
};
