#pragma once

#include "core/Event.hpp"

#include <functional>
#include <mutex>
#include <vector>

namespace core
{

// Simple synchronous pub/sub bus. "Synchronous" is important to call out:
// Publish() invokes handlers on the calling thread (which, for this app,
// is always a background console/watcher thread - never the UI thread
// directly). Handlers must therefore do the minimum possible work and
// marshal anything UI-related over to the UI thread themselves (MainWindow
// does this via PostMessageW). This keeps EventDispatcher itself free of
// any Win32/UI dependency.
class EventDispatcher
{
public:
    using Handler = std::function<void(const AppEvent&)>;

    void Subscribe(Handler handler);
    void Publish(const AppEvent& event);

private:
    std::mutex mutex_;
    std::vector<Handler> handlers_;
};

} // namespace core
