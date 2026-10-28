#include "core/Localize.hpp"

#include <atomic>

namespace core
{
namespace
{
std::atomic<LocalizerFn> g_localizer{nullptr};
std::atomic<LocalizerUtf8Fn> g_localizerUtf8{nullptr};
}

void SetLocalizer(LocalizerFn fn, LocalizerUtf8Fn fnUtf8)
{
    g_localizer.store(fn);
    g_localizerUtf8.store(fnUtf8);
}

std::wstring Localize(const wchar_t* source)
{
    const LocalizerFn fn = g_localizer.load();
    return fn != nullptr ? fn(source) : std::wstring(source);
}

std::string LocalizeUtf8(const char* utf8Source)
{
    const LocalizerUtf8Fn fn = g_localizerUtf8.load();
    return fn != nullptr ? fn(utf8Source) : std::string(utf8Source);
}

} // namespace core
