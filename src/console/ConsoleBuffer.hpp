#pragma once

// Thread-safe store for one server's console history. Written to from a
// background ConsoleController thread, read from the UI thread when
// repainting the console view.

#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace console
{

class ConsoleBuffer
{
public:
    explicit ConsoleBuffer(size_t maxLines = 5000)
        : maxLines_(maxLines)
    {
    }

    void Append(const std::wstring& line)
    {
        std::scoped_lock lock(mutex_);
        lines_.push_back(line);
        // deque: dropping the oldest line is O(1). (The previous vector
        // erase-from-front shifted up to 5000 strings on EVERY line once
        // the buffer was full - costly for chatty servers.)
        while (lines_.size() > maxLines_)
        {
            lines_.pop_front();
        }
    }

    [[nodiscard]] std::vector<std::wstring> GetAll() const
    {
        std::scoped_lock lock(mutex_);
        return std::vector<std::wstring>(lines_.begin(), lines_.end());
    }

    void Clear()
    {
        std::scoped_lock lock(mutex_);
        lines_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::wstring> lines_;
    size_t maxLines_;
};

} // namespace console
