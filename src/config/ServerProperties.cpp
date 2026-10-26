#include "config/ServerProperties.hpp"

#include "core/StringUtil.hpp"

#include <fstream>
#include <sstream>
#include <vector>

namespace config
{
namespace
{

std::filesystem::path PropertiesPath(const core::ServerConfig& server)
{
    return server.directory / L"server.properties";
}

std::wstring Trim(const std::wstring& text)
{
    const size_t start = text.find_first_not_of(L" \t");
    if (start == std::wstring::npos)
    {
        return L"";
    }
    const size_t end = text.find_last_not_of(L" \t");
    return text.substr(start, end - start + 1);
}

// Reads the whole file as UTF-8 and splits it into lines, stripping any
// trailing \r left over from CRLF endings. Returns an empty vector (not
// an error) if the file does not exist yet.
std::vector<std::wstring> ReadLines(const std::filesystem::path& path)
{
    std::vector<std::wstring> lines;
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open())
    {
        return lines;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::wstring text = core::Utf8ToWide(buffer.str());

    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == L'\r')
        {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

} // namespace

std::map<std::wstring, std::wstring> ReadServerProperties(const core::ServerConfig& server)
{
    std::map<std::wstring, std::wstring> result;
    for (const auto& rawLine : ReadLines(PropertiesPath(server)))
    {
        const std::wstring line = Trim(rawLine);
        if (line.empty() || line.front() == L'#')
        {
            continue;
        }
        const auto eq = line.find(L'=');
        if (eq == std::wstring::npos)
        {
            continue;
        }
        result[Trim(line.substr(0, eq))] = line.substr(eq + 1);
    }
    return result;
}

bool WriteServerProperties(const core::ServerConfig& server, const std::map<std::wstring, std::wstring>& updates)
{
    const auto path = PropertiesPath(server);
    std::vector<std::wstring> lines = ReadLines(path);

    std::map<std::wstring, std::wstring> remaining = updates;
    for (auto& line : lines)
    {
        const std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == L'#')
        {
            continue;
        }
        const auto eq = trimmed.find(L'=');
        if (eq == std::wstring::npos)
        {
            continue;
        }
        const std::wstring key = Trim(trimmed.substr(0, eq));
        if (auto it = remaining.find(key); it != remaining.end())
        {
            line = key + L"=" + it->second;
            remaining.erase(it);
        }
    }
    // Anything left in `remaining` was not already present in the file -
    // append it as a new line (this only really happens the very first
    // time a given setting is changed through the UI, before the server
    // has ever written its own default for that key).
    for (const auto& [key, value] : remaining)
    {
        lines.push_back(key + L"=" + value);
    }

    std::error_code ec;
    std::filesystem::create_directories(server.directory, ec);

    std::wstring joined;
    for (const auto& line : lines)
    {
        joined += line;
        joined += L"\n";
    }
    const std::string utf8 = core::WideToUtf8(joined);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open())
    {
        return false;
    }
    out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return true;
}

} // namespace config
