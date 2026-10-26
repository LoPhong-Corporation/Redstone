#include "core/EventDispatcher.hpp"

namespace core
{

void EventDispatcher::Subscribe(Handler handler)
{
    std::scoped_lock lock(mutex_);
    handlers_.push_back(std::move(handler));
}

void EventDispatcher::Publish(const AppEvent& event)
{
    // Copy the handler list under the lock, then invoke outside of it.
    // This avoids holding mutex_ while running arbitrary user code, which
    // would risk a deadlock if a handler ever tried to Subscribe() or
    // Publish() again re-entrantly.
    std::vector<Handler> handlersCopy;
    {
        std::scoped_lock lock(mutex_);
        handlersCopy = handlers_;
    }
    for (const auto& handler : handlersCopy)
    {
        handler(event);
    }
}

} // namespace core
