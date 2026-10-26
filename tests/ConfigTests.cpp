// Minimal, dependency-free smoke tests (spec section 46: "Configuration
// serialization / Configuration loading"). Not a substitute for a real
// framework long-term, but enough to catch regressions in the hand-rolled
// JSON parser/serializer and ConfigManager without adding a build
// dependency for the MVP.

#include "config/ConfigManager.hpp"
#include "core/Json.hpp"

#include <cstdio>
#include <filesystem>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* description)
{
    if (!condition)
    {
        std::printf("FAILED: %s\n", description);
        ++g_failures;
    }
    else
    {
        std::printf("ok: %s\n", description);
    }
}

void TestJsonRoundTrip()
{
    core::json::JsonValue obj = core::json::JsonValue::MakeObject();
    obj.Set(L"name", core::json::JsonValue::MakeString(L"Survival Server"));
    obj.Set(L"port", core::json::JsonValue::MakeNumber(25565));
    obj.Set(L"autoRestart", core::json::JsonValue::MakeBool(true));

    const std::wstring serialized = core::json::Serialize(obj);
    auto parsed = core::json::Parse(serialized);

    Check(parsed.has_value(), "JSON: parses what it just serialized");
    if (!parsed) return;

    Check(parsed->Find(L"name") != nullptr, "JSON: 'name' key present after round-trip");
    Check(parsed->Find(L"name")->AsString() == L"Survival Server", "JSON: 'name' value preserved");
    Check(static_cast<int>(parsed->Find(L"port")->AsNumber()) == 25565, "JSON: 'port' value preserved");
    Check(parsed->Find(L"autoRestart")->AsBool() == true, "JSON: 'autoRestart' value preserved");
}

void TestConfigManagerRoundTrip()
{
    const std::filesystem::path testPath = L"data/_test_servers.json";
    std::filesystem::remove(testPath);

    config::ConfigManager manager(testPath);

    core::ServerConfig server;
    server.id = L"srv-test-1";
    server.name = L"Test Server";
    server.directory = L"C:\\Servers\\Test";
    server.serverJar = L"server.jar";
    server.javaExecutable = L"java.exe";
    server.minMemoryMB = 1024;
    server.maxMemoryMB = 4096;
    server.port = 25566;
    server.autoRestart = true;

    Check(manager.Save({server}), "ConfigManager: Save() succeeds");

    auto loaded = manager.Load();
    Check(loaded.size() == 1, "ConfigManager: Load() returns exactly one server");
    if (loaded.size() == 1)
    {
        Check(loaded[0].id == server.id, "ConfigManager: id round-trips");
        Check(loaded[0].name == server.name, "ConfigManager: name round-trips");
        Check(loaded[0].minMemoryMB == server.minMemoryMB, "ConfigManager: minMemoryMB round-trips");
        Check(loaded[0].maxMemoryMB == server.maxMemoryMB, "ConfigManager: maxMemoryMB round-trips");
        Check(loaded[0].port == server.port, "ConfigManager: port round-trips");
        Check(loaded[0].autoRestart == server.autoRestart, "ConfigManager: autoRestart round-trips");
    }

    std::filesystem::remove(testPath);
}

} // namespace

int main()
{
    TestJsonRoundTrip();
    TestConfigManagerRoundTrip();

    if (g_failures == 0)
    {
        std::printf("\nAll tests passed.\n");
        return 0;
    }
    std::printf("\n%d test(s) failed.\n", g_failures);
    return 1;
}
