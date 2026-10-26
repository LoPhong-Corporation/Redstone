#include "javamanager/JavaManager.hpp"

#include "process/Process.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <sstream>

namespace javamanager
{
namespace
{

std::wstring GetEnvVar(const wchar_t* name)
{
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0)
    {
        return L"";
    }
    std::wstring buffer(needed, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buffer.data(), needed);
    if (written == 0)
    {
        return L"";
    }
    buffer.resize(written); // drop the trailing null GetEnvironmentVariableW counted
    return buffer;
}

std::vector<std::wstring> SplitPathVariable(const std::wstring& pathVar)
{
    std::vector<std::wstring> parts;
    std::wstringstream stream(pathVar);
    std::wstring part;
    while (std::getline(stream, part, L';'))
    {
        if (!part.empty())
        {
            parts.push_back(part);
        }
    }
    return parts;
}

std::wstring ToLowerCopy(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

} // namespace

std::vector<JavaInstallation> DetectInstallations()
{
    std::vector<JavaInstallation> results;
    std::set<std::wstring> seenKeys; // lower-cased absolute path, to de-duplicate

    auto tryAdd = [&](const std::filesystem::path& candidate) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec))
        {
            return;
        }
        std::error_code canonicalEc;
        const auto resolved = std::filesystem::weakly_canonical(candidate, canonicalEc);
        const std::wstring key = ToLowerCopy(canonicalEc ? candidate.wstring() : resolved.wstring());
        if (!seenKeys.insert(key).second)
        {
            return; // already found via another route (e.g. both PATH and JAVA_HOME)
        }
        JavaInstallation install;
        install.path = canonicalEc ? candidate : resolved;
        results.push_back(std::move(install));
    };

    // 1. JAVA_HOME
    if (const std::wstring javaHome = GetEnvVar(L"JAVA_HOME"); !javaHome.empty())
    {
        tryAdd(std::filesystem::path(javaHome) / L"bin" / L"java.exe");
    }

    // 2. Every directory on PATH
    if (const std::wstring pathVar = GetEnvVar(L"PATH"); !pathVar.empty())
    {
        for (const auto& dir : SplitPathVariable(pathVar))
        {
            tryAdd(std::filesystem::path(dir) / L"java.exe");
        }
    }

    // 3. Well-known per-vendor install roots. Each one contains a
    // sub-directory per installed version (e.g.
    // "C:\Program Files\Java\jdk-17.0.9\bin\java.exe"), so every
    // immediate child directory is checked.
    static const std::vector<std::wstring> vendorRoots = {
        L"C:\\Program Files\\Java",
        L"C:\\Program Files (x86)\\Java",
        L"C:\\Program Files\\Eclipse Adoptium",
        L"C:\\Program Files\\Zulu",
        L"C:\\Program Files\\Amazon Corretto",
        L"C:\\Program Files\\Microsoft",
        L"C:\\Program Files\\BellSoft",
    };
    for (const auto& root : vendorRoots)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec))
        {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(root, ec))
        {
            if (!entry.is_directory())
            {
                continue;
            }
            tryAdd(entry.path() / L"bin" / L"java.exe");
        }
    }

    return results;
}

std::optional<int> ProbeJavaMajorVersion(const std::filesystem::path& javaExecutable)
{
    process::Process proc;
    process::Process::StartOptions options;
    options.executablePath = javaExecutable;
    options.arguments = L"-version";
    options.workingDirectory = javaExecutable.parent_path();

    if (!proc.Start(options))
    {
        return std::nullopt;
    }

    // `java -version` writes its one line of output to stderr (a long-
    // standing JVM quirk), and the output is tiny, so a direct blocking
    // ReadFile loop here is simpler and just as correct as spinning up a
    // console::ConsoleController thread for it - this function is itself
    // always called from a background thread (never the UI thread), so
    // blocking here is fine.
    std::string output;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (ReadFile(proc.GetStderrReadHandle(), buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
    {
        output.append(buffer, bytesRead);
    }
    proc.Wait(5000);

    const auto markerPos = output.find("version \"");
    if (markerPos == std::string::npos)
    {
        return std::nullopt;
    }
    const auto valueStart = markerPos + 9; // length of "version \""
    const auto valueEnd = output.find('"', valueStart);
    if (valueEnd == std::string::npos)
    {
        return std::nullopt;
    }
    const std::string versionString = output.substr(valueStart, valueEnd - valueStart);

    try
    {
        if (versionString.rfind("1.", 0) == 0)
        {
            // Old-style "1.8.0_301" -> the SECOND component is the real
            // major version (Java 8, in this example).
            const auto secondDot = versionString.find('.', 2);
            const std::string majorPart = versionString.substr(2, secondDot == std::string::npos ? std::string::npos : secondDot - 2);
            return std::stoi(majorPart);
        }
        // Modern style: "17.0.9" or bare "21" -> the FIRST component.
        const auto firstDot = versionString.find('.');
        const std::string majorPart = versionString.substr(0, firstDot);
        return std::stoi(majorPart);
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
}

} // namespace javamanager
