#pragma once

// Phase 3: auto-detects installed Java runtimes so the user does not have
// to hunt down and type a java.exe path by hand in AddServerDialog.
//
// Scope, deliberately kept simple: this scans JAVA_HOME, every directory
// on PATH, and the handful of well-known install roots used by the major
// JDK vendors (Oracle, Eclipse Temurin/Adoptium, Azul Zulu, Amazon
// Corretto, Microsoft Build of OpenJDK). It does NOT read the Windows
// registry (every vendor uses a different registry layout, and the
// filesystem scan already covers the overwhelming majority of real
// installs) and does NOT probe each candidate with `java -version` (that
// would mean spawning N processes just to populate a combo box) - so no
// version number is reported, only the discovered path.

#include <filesystem>
#include <optional>
#include <vector>

namespace javamanager
{

struct JavaInstallation
{
    std::filesystem::path path; // full path to a java.exe
};

std::vector<JavaInstallation> DetectInstallations();

// Runs `java -version` and parses the major version number (handles both
// the old "1.8.0_301" style and the modern "17.0.9" / "21" style).
// Meant to be called from a background thread - it blocks for as long as
// the java process takes to print its version and exit (normally well
// under a second, but never call this from the UI thread). Returns
// std::nullopt if the process could not be started or its output could
// not be parsed.
std::optional<int> ProbeJavaMajorVersion(const std::filesystem::path& javaExecutable);

} // namespace javamanager
