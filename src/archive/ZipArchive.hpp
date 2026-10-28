#pragma once

// General-purpose ZIP reader (STORE + DEFLATE, central directory with comment,
// no encryption, no ZIP64) used to unpack Modrinth .mrpack modpacks. Unlike
// backup::ZipReader (which only reads this app's own uncompressed backups) it
// is written to be pointed at UNTRUSTED downloaded archives:
//  - every entry name is validated (no "..", absolute paths, drive letters)
//  - decompressed size is capped per entry and in total (zip-bomb guard)
//  - CRC-32 of every extracted entry is verified
//  - corrupt/truncated data fails cleanly, never reads out of bounds
// Whole entries are inflated in memory, which is fine for modpack content
// (configs, scripts, small resource files); the per-entry cap bounds memory.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace archive
{

struct ZipEntryInfo
{
    std::string name; // forward slashes, UTF-8
    std::uint16_t method = 0; // 0 = stored, 8 = deflate
    std::uint16_t flags = 0;
    std::uint32_t crc32 = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t size = 0;
    std::uint64_t localHeaderOffset = 0;
    [[nodiscard]] bool IsDirectory() const { return !name.empty() && name.back() == '/'; }
};

// Raw DEFLATE (RFC 1951) decoder. Returns false on malformed input or if the
// output would exceed `maxOutput` bytes.
bool Inflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::uint64_t maxOutput);

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed = 0);

// True for a normal relative path that stays inside its base directory.
bool IsSafeRelativePath(const std::string& name);

class ZipArchive
{
public:
    // Reads and validates the central directory. On failure returns false and
    // fills `error`.
    bool Open(const std::filesystem::path& path, std::string* error);

    [[nodiscard]] const std::vector<ZipEntryInfo>& Entries() const { return entries_; }
    [[nodiscard]] const ZipEntryInfo* Find(const std::string& name) const;

    bool ReadToMemory(const ZipEntryInfo& entry, std::vector<std::uint8_t>& out, std::uint64_t maxBytes, std::string* error) const;

    // Extracts to `destination` (parent directories created). Writes to a
    // temp file and renames, so a failed extraction never leaves a half file.
    bool ExtractToFile(const ZipEntryInfo& entry, const std::filesystem::path& destination, std::uint64_t maxBytes, std::string* error) const;

private:
    std::filesystem::path path_;
    std::vector<ZipEntryInfo> entries_;
};

} // namespace archive
