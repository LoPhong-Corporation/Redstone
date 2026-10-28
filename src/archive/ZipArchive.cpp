#include "archive/ZipArchive.hpp"

#include "core/Localize.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>

namespace archive
{
namespace
{

// ---------------------------------------------------------------- inflate
// Canonical-Huffman decoder after Mark Adler's public-domain "puff".

constexpr int kMaxBits = 15;
constexpr int kMaxLCodes = 286;
constexpr int kMaxDCodes = 30;
constexpr int kFixLCodes = 288;

struct Huffman
{
    std::array<short, kMaxBits + 1> count{};
    std::array<short, kFixLCodes> symbol{};
};

struct State
{
    const std::uint8_t* in;
    std::size_t inLen;
    std::size_t inPos = 0;
    std::uint32_t bitBuf = 0;
    int bitCnt = 0;
    std::vector<std::uint8_t>* out;
    std::uint64_t maxOut;
    bool error = false;
};

int Bits(State& s, int need)
{
    std::uint32_t val = s.bitBuf;
    while (s.bitCnt < need)
    {
        if (s.inPos >= s.inLen)
        {
            s.error = true;
            return 0;
        }
        val |= static_cast<std::uint32_t>(s.in[s.inPos++]) << s.bitCnt;
        s.bitCnt += 8;
    }
    s.bitBuf = val >> need;
    s.bitCnt -= need;
    return static_cast<int>(val & ((1U << need) - 1));
}

int Decode(State& s, const Huffman& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        code |= Bits(s, 1);
        if (s.error)
        {
            return -1;
        }
        const int count = h.count[static_cast<std::size_t>(len)];
        if (code - count < first)
        {
            return h.symbol[static_cast<std::size_t>(index + (code - first))];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

// Returns 0 for a complete code, >0 incomplete, <0 over-subscribed.
int Construct(Huffman& h, const short* length, int n)
{
    h.count.fill(0);
    for (int sym = 0; sym < n; ++sym)
    {
        ++h.count[static_cast<std::size_t>(length[sym])];
    }
    if (h.count[0] == n)
    {
        return 0;
    }
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len)
    {
        left <<= 1;
        left -= h.count[static_cast<std::size_t>(len)];
        if (left < 0)
        {
            return left;
        }
    }
    std::array<short, kMaxBits + 1> offs{};
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len)
    {
        offs[static_cast<std::size_t>(len + 1)] = static_cast<short>(offs[static_cast<std::size_t>(len)] + h.count[static_cast<std::size_t>(len)]);
    }
    for (int sym = 0; sym < n; ++sym)
    {
        if (length[sym] != 0)
        {
            h.symbol[static_cast<std::size_t>(offs[static_cast<std::size_t>(length[sym])]++)] = static_cast<short>(sym);
        }
    }
    return left;
}

bool Codes(State& s, const Huffman& lencode, const Huffman& distcode)
{
    static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    int symbol;
    do
    {
        symbol = Decode(s, lencode);
        if (symbol < 0)
        {
            return false;
        }
        if (symbol < 256)
        {
            if (s.out->size() >= s.maxOut)
            {
                return false;
            }
            s.out->push_back(static_cast<std::uint8_t>(symbol));
        }
        else if (symbol > 256)
        {
            symbol -= 257;
            if (symbol >= 29)
            {
                return false;
            }
            const int len = lens[symbol] + Bits(s, lext[symbol]);
            const int dsym = Decode(s, distcode);
            if (dsym < 0 || dsym >= 30)
            {
                return false;
            }
            const std::size_t dist = static_cast<std::size_t>(dists[dsym] + Bits(s, dext[dsym]));
            if (s.error || dist > s.out->size() || s.out->size() + static_cast<std::size_t>(len) > s.maxOut)
            {
                return false;
            }
            std::size_t from = s.out->size() - dist;
            for (int i = 0; i < len; ++i)
            {
                s.out->push_back((*s.out)[from++]); // may overlap: copies byte by byte on purpose
            }
        }
        if (s.error)
        {
            return false;
        }
    } while (symbol != 256);
    return true;
}

bool Stored(State& s)
{
    s.bitBuf = 0;
    s.bitCnt = 0;
    if (s.inPos + 4 > s.inLen)
    {
        return false;
    }
    unsigned len = s.in[s.inPos] | (static_cast<unsigned>(s.in[s.inPos + 1]) << 8);
    const unsigned nlen = s.in[s.inPos + 2] | (static_cast<unsigned>(s.in[s.inPos + 3]) << 8);
    s.inPos += 4;
    if (len != (~nlen & 0xFFFFU) || s.inPos + len > s.inLen || s.out->size() + len > s.maxOut)
    {
        return false;
    }
    s.out->insert(s.out->end(), s.in + s.inPos, s.in + s.inPos + len);
    s.inPos += len;
    return true;
}

bool Fixed(State& s)
{
    static Huffman lencode, distcode;
    static bool built = false;
    if (!built)
    {
        short lengths[kFixLCodes];
        int sym = 0;
        for (; sym < 144; ++sym) lengths[sym] = 8;
        for (; sym < 256; ++sym) lengths[sym] = 9;
        for (; sym < 280; ++sym) lengths[sym] = 7;
        for (; sym < kFixLCodes; ++sym) lengths[sym] = 8;
        Construct(lencode, lengths, kFixLCodes);
        for (sym = 0; sym < kMaxDCodes; ++sym) lengths[sym] = 5;
        Construct(distcode, lengths, kMaxDCodes);
        built = true;
    }
    return Codes(s, lencode, distcode);
}

bool Dynamic(State& s)
{
    static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    short lengths[kMaxLCodes + kMaxDCodes];
    Huffman lencode, distcode;

    const int nlen = Bits(s, 5) + 257;
    const int ndist = Bits(s, 5) + 1;
    const int ncode = Bits(s, 4) + 4;
    if (s.error || nlen > kMaxLCodes || ndist > kMaxDCodes)
    {
        return false;
    }

    int index = 0;
    for (; index < ncode; ++index)
    {
        lengths[order[index]] = static_cast<short>(Bits(s, 3));
    }
    for (; index < 19; ++index)
    {
        lengths[order[index]] = 0;
    }
    if (s.error || Construct(lencode, lengths, 19) != 0)
    {
        return false;
    }

    index = 0;
    while (index < nlen + ndist)
    {
        int symbol = Decode(s, lencode);
        if (symbol < 0)
        {
            return false;
        }
        if (symbol < 16)
        {
            lengths[index++] = static_cast<short>(symbol);
        }
        else
        {
            int len = 0;
            if (symbol == 16)
            {
                if (index == 0)
                {
                    return false;
                }
                len = lengths[index - 1];
                symbol = 3 + Bits(s, 2);
            }
            else if (symbol == 17)
            {
                symbol = 3 + Bits(s, 3);
            }
            else
            {
                symbol = 11 + Bits(s, 7);
            }
            if (s.error || index + symbol > nlen + ndist)
            {
                return false;
            }
            while (symbol--)
            {
                lengths[index++] = static_cast<short>(len);
            }
        }
    }
    if (lengths[256] == 0)
    {
        return false;
    }
    int err = Construct(lencode, lengths, nlen);
    if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
    {
        return false;
    }
    err = Construct(distcode, lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
    {
        return false;
    }
    return Codes(s, lencode, distcode);
}

// ------------------------------------------------------------- zip bits

std::uint16_t Rd16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t Rd32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) | (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

void SetError(std::string* error, const std::string& text)
{
    if (error != nullptr)
    {
        *error = text;
    }
}

} // namespace

bool Inflate(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out, std::uint64_t maxOutput)
{
    State s{data, size, 0, 0, 0, &out, maxOutput, false};
    int last;
    do
    {
        last = Bits(s, 1);
        const int type = Bits(s, 2);
        if (s.error)
        {
            return false;
        }
        bool ok = false;
        switch (type)
        {
            case 0: ok = Stored(s); break;
            case 1: ok = Fixed(s); break;
            case 2: ok = Dynamic(s); break;
            default: return false;
        }
        if (!ok)
        {
            return false;
        }
    } while (!last);
    return true;
}

std::uint32_t Crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed)
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = seed ^ 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i)
    {
        c = table[(c ^ data[i]) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

bool IsSafeRelativePath(const std::string& nameIn)
{
    if (nameIn.empty() || nameIn.size() > 1024 || nameIn.find('\0') != std::string::npos)
    {
        return false;
    }
    std::string name = nameIn;
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.front() == '/' || (name.size() >= 2 && name[1] == ':'))
    {
        return false;
    }
    std::size_t start = 0;
    while (start <= name.size())
    {
        const std::size_t end = name.find('/', start);
        const std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..")
        {
            return false;
        }
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return true;
}

bool ZipArchive::Open(const std::filesystem::path& path, std::string* error)
{
    entries_.clear();
    path_ = path;
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        SetError(error, core::LocalizeUtf8("Không mở được file zip."));
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::uint64_t fileSize = static_cast<std::uint64_t>(f.tellg());
    if (fileSize < 22)
    {
        SetError(error, core::LocalizeUtf8("File zip quá nhỏ."));
        return false;
    }

    // Find the End Of Central Directory record (it may be followed by a comment).
    const std::uint64_t tailLen = std::min<std::uint64_t>(fileSize, 22 + 65535);
    std::vector<std::uint8_t> tail(static_cast<std::size_t>(tailLen));
    f.seekg(static_cast<std::streamoff>(fileSize - tailLen));
    f.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tailLen));
    if (!f)
    {
        SetError(error, core::LocalizeUtf8("Không đọc được cuối file zip."));
        return false;
    }
    std::int64_t eocd = -1;
    for (std::int64_t i = static_cast<std::int64_t>(tail.size()) - 22; i >= 0; --i)
    {
        if (Rd32(&tail[static_cast<std::size_t>(i)]) == 0x06054b50U)
        {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
    {
        SetError(error, core::LocalizeUtf8("Không tìm thấy bản ghi kết thúc zip (file hỏng hoặc không phải zip)."));
        return false;
    }
    const std::uint8_t* e = &tail[static_cast<std::size_t>(eocd)];
    const std::uint32_t count = Rd16(e + 10);
    const std::uint32_t cdSize = Rd32(e + 12);
    const std::uint32_t cdOffset = Rd32(e + 16);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFFU || cdOffset == 0xFFFFFFFFU)
    {
        SetError(error, core::LocalizeUtf8("Zip64 chưa được hỗ trợ."));
        return false;
    }
    if (static_cast<std::uint64_t>(cdOffset) + cdSize > fileSize || cdSize > 64U * 1024 * 1024)
    {
        SetError(error, core::LocalizeUtf8("Thư mục trung tâm của zip không hợp lệ."));
        return false;
    }

    std::vector<std::uint8_t> cd(cdSize);
    f.seekg(cdOffset);
    f.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cdSize));
    if (!f)
    {
        SetError(error, core::LocalizeUtf8("Không đọc được thư mục trung tâm."));
        return false;
    }

    std::size_t pos = 0;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (pos + 46 > cd.size() || Rd32(&cd[pos]) != 0x02014b50U)
        {
            SetError(error, core::LocalizeUtf8("Bản ghi trong thư mục trung tâm bị hỏng."));
            return false;
        }
        const std::uint8_t* h = &cd[pos];
        ZipEntryInfo info;
        info.flags = Rd16(h + 8);
        info.method = Rd16(h + 10);
        info.crc32 = Rd32(h + 16);
        info.compressedSize = Rd32(h + 20);
        info.size = Rd32(h + 24);
        const std::size_t nameLen = Rd16(h + 28);
        const std::size_t extraLen = Rd16(h + 30);
        const std::size_t commentLen = Rd16(h + 32);
        info.localHeaderOffset = Rd32(h + 42);
        if (pos + 46 + nameLen + extraLen + commentLen > cd.size())
        {
            SetError(error, core::LocalizeUtf8("Tên entry vượt quá thư mục trung tâm."));
            return false;
        }
        info.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
        std::replace(info.name.begin(), info.name.end(), '\\', '/');
        entries_.push_back(std::move(info));
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

const ZipEntryInfo* ZipArchive::Find(const std::string& name) const
{
    for (const auto& e : entries_)
    {
        if (e.name == name)
        {
            return &e;
        }
    }
    return nullptr;
}

bool ZipArchive::ReadToMemory(const ZipEntryInfo& entry, std::vector<std::uint8_t>& out, std::uint64_t maxBytes, std::string* error) const
{
    out.clear();
    if (entry.flags & 1U)
    {
        SetError(error, core::LocalizeUtf8("Entry bị mã hoá: ") + entry.name);
        return false;
    }
    if (entry.size > maxBytes)
    {
        SetError(error, core::LocalizeUtf8("Entry quá lớn (") + std::to_string(entry.size) + " byte): " + entry.name);
        return false;
    }
    if (entry.method != 0 && entry.method != 8)
    {
        SetError(error, core::LocalizeUtf8("Kiểu nén chưa hỗ trợ: ") + entry.name);
        return false;
    }

    std::ifstream f(path_, std::ios::binary);
    std::uint8_t local[30];
    f.seekg(static_cast<std::streamoff>(entry.localHeaderOffset));
    f.read(reinterpret_cast<char*>(local), sizeof(local));
    if (!f || Rd32(local) != 0x04034b50U)
    {
        SetError(error, core::LocalizeUtf8("Local header hỏng: ") + entry.name);
        return false;
    }
    const std::uint64_t dataStart = entry.localHeaderOffset + 30 + Rd16(local + 26) + Rd16(local + 28);

    std::vector<std::uint8_t> raw(static_cast<std::size_t>(entry.compressedSize));
    f.seekg(static_cast<std::streamoff>(dataStart));
    f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (!f)
    {
        SetError(error, core::LocalizeUtf8("File zip bị cắt cụt: ") + entry.name);
        return false;
    }

    if (entry.method == 0)
    {
        out = std::move(raw);
    }
    else
    {
        out.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(entry.size, maxBytes)));
        if (!Inflate(raw.data(), raw.size(), out, std::min(maxBytes, entry.size)))
        {
            SetError(error, core::LocalizeUtf8("Giải nén lỗi (dữ liệu hỏng hoặc vượt kích thước khai báo): ") + entry.name);
            return false;
        }
    }
    if (out.size() != entry.size || Crc32(out.data(), out.size()) != entry.crc32)
    {
        SetError(error, core::LocalizeUtf8("Sai kích thước/CRC-32: ") + entry.name);
        return false;
    }
    return true;
}

bool ZipArchive::ExtractToFile(const ZipEntryInfo& entry, const std::filesystem::path& destination, std::uint64_t maxBytes, std::string* error) const
{
    std::vector<std::uint8_t> data;
    if (!ReadToMemory(entry, data, maxBytes, error))
    {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    const std::filesystem::path temp = destination.string() + ".part";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!out)
        {
            SetError(error, core::LocalizeUtf8("Không ghi được file: ") + destination.string());
            return false;
        }
    }
    std::filesystem::rename(temp, destination, ec);
    if (ec)
    {
        std::filesystem::remove(destination, ec);
        std::filesystem::rename(temp, destination, ec);
        if (ec)
        {
            SetError(error, core::LocalizeUtf8("Không đổi tên được file: ") + destination.string());
            return false;
        }
    }
    return true;
}

} // namespace archive
