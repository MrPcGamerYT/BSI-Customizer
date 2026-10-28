#include "RootDisk.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <limits>
#ifndef _WIN32
#include <sys/types.h>
#endif

namespace RootDisk {

namespace {

// ---- little endian helpers ------------------------------------------------

std::uint16_t le16(const std::uint8_t *p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
        | (std::uint32_t(p[3]) << 24);
}
std::uint64_t le64(const std::uint8_t *p)
{
    return std::uint64_t(le32(p)) | (std::uint64_t(le32(p + 4)) << 32);
}

bool seekFile(std::FILE *file, std::uint64_t offset)
{
#ifdef _WIN32
    if (offset > std::uint64_t(std::numeric_limits<__int64>::max())) return false;
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    if (offset > std::uint64_t(std::numeric_limits<off_t>::max())) return false;
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool seekFileEnd(std::FILE *file)
{
#ifdef _WIN32
    return _fseeki64(file, 0, SEEK_END) == 0;
#else
    return fseeko(file, 0, SEEK_END) == 0;
#endif
}

bool fileLength(std::FILE *file, std::uint64_t *length)
{
#ifdef _WIN32
    const __int64 result = _ftelli64(file);
#else
    const off_t result = ftello(file);
#endif
    if (result < 0) return false;
    *length = std::uint64_t(result);
    return true;
}
std::uint64_t be32toh_(const std::uint8_t *p)
{
    return (std::uint64_t(p[0]) << 24) | (std::uint64_t(p[1]) << 16) | (std::uint64_t(p[2]) << 8)
        | std::uint64_t(p[3]);
}
std::uint64_t be64toh_(const std::uint8_t *p)
{
    return (be32toh_(p) << 32) | be32toh_(p + 4);
}
void put16(std::uint8_t *p, std::uint16_t v)
{
    p[0] = std::uint8_t(v & 0xFF);
    p[1] = std::uint8_t(v >> 8);
}
void put32(std::uint8_t *p, std::uint32_t v)
{
    p[0] = std::uint8_t(v & 0xFF);
    p[1] = std::uint8_t((v >> 8) & 0xFF);
    p[2] = std::uint8_t((v >> 16) & 0xFF);
    p[3] = std::uint8_t(v >> 24);
}
void putbe32(std::uint8_t *p, std::uint32_t v)
{
    p[0] = std::uint8_t((v >> 24) & 0xFF);
    p[1] = std::uint8_t((v >> 16) & 0xFF);
    p[2] = std::uint8_t((v >> 8) & 0xFF);
    p[3] = std::uint8_t(v & 0xFF);
}

// ---- CRC32C (Castagnoli), the checksum ext4 uses -------------------------

struct Crc32c
{
    std::uint32_t table[256];
    Crc32c()
    {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1) ? (0x82F63B78u ^ (value >> 1)) : (value >> 1);
            table[i] = value;
        }
    }
};

const Crc32c &crcTable()
{
    static const Crc32c table;
    return table;
}

std::uint32_t crc32c(std::uint32_t crc, const void *data, std::size_t length)
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    const Crc32c &table = crcTable();
    for (std::size_t i = 0; i < length; ++i)
        crc = table.table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

// ---- file image ----------------------------------------------------------

class FileImage final : public Image
{
public:
    FileImage(std::FILE *file, std::uint64_t size)
        : m_file(file), m_size(size) {}
    ~FileImage() override { std::fclose(m_file); }

    std::uint64_t size() const override { return m_size; }

    bool read(std::uint64_t offset, void *buffer, std::size_t length) override
    {
        if (offset > m_size || std::uint64_t(length) > m_size - offset) return false;
        if (!seekFile(m_file, offset)) return false;
        return std::fread(buffer, 1, length, m_file) == length;
    }

    bool write(std::uint64_t offset, const void *buffer, std::size_t length) override
    {
        if (offset > m_size || std::uint64_t(length) > m_size - offset) return false;
        if (!seekFile(m_file, offset)) return false;
        return std::fwrite(buffer, 1, length, m_file) == length;
    }

    bool flush() override { return std::fflush(m_file) == 0; }
    std::string kind() const override { return "raw image"; }
    bool writable() const override { return true; }

private:
    std::FILE *m_file;
    std::uint64_t m_size;
};

// ---- VHD -----------------------------------------------------------------

constexpr std::uint32_t kUnallocated = 0xFFFFFFFFu;

// Both kinds read through the same 512-byte footer; a dynamic disk turns a
// block address into a file offset through its BAT and allocates a block the
// first time it is written.
class VhdImage final : public Image
{
public:
    static std::unique_ptr<VhdImage> open(std::FILE *file, std::uint64_t fileSize,
                                          bool readOnly, std::string *error);

    std::uint64_t size() const override { return m_diskSize; }

    bool read(std::uint64_t offset, void *buffer, std::size_t length) override;
    bool write(std::uint64_t offset, const void *buffer, std::size_t length) override;
    bool flush() override { return std::fflush(m_file) == 0; }
    std::string kind() const override { return m_dynamic ? "dynamic VHD" : "fixed VHD"; }
    bool writable() const override { return !m_readOnly; }

private:
    VhdImage(std::FILE *file, bool readOnly)
        : m_file(file), m_readOnly(readOnly) {}

    bool fileRead(std::uint64_t offset, void *buffer, std::size_t length)
    {
        if (!seekFile(m_file, offset)) return false;
        return std::fread(buffer, 1, length, m_file) == length;
    }
    bool fileWrite(std::uint64_t offset, const void *buffer, std::size_t length)
    {
        if (m_readOnly) return false;
        if (!seekFile(m_file, offset)) return false;
        return std::fwrite(buffer, 1, length, m_file) == length;
    }
    bool writeFooter(std::uint64_t at);
    bool allocateBlock(std::uint32_t index);

    std::FILE *m_file = nullptr;
    bool m_readOnly = true;
    bool m_dynamic = false;
    std::uint64_t m_diskSize = 0;
    std::uint64_t m_footerOffset = 0;   // where the current footer starts
    std::uint64_t m_tableOffset = 0;
    std::uint32_t m_blockSize = 0;
    std::uint32_t m_maxEntries = 0;
    std::uint64_t m_bitmapOffset = 0;
    std::vector<std::uint32_t> m_bat;
    std::vector<std::uint8_t> m_footer; // the 512 bytes, kept to rewrite
};

std::unique_ptr<VhdImage> VhdImage::open(std::FILE *file, std::uint64_t fileSize, bool readOnly,
                                         std::string *error)
{
    auto fail = [&](const std::string &why) -> std::unique_ptr<VhdImage> {
        if (error) *error = why;
        std::fclose(file);
        return nullptr;
    };
    if (fileSize < 1024) return fail("The image is too small to be a VHD.");

    std::vector<std::uint8_t> footer(512);
    if (!seekFile(file, fileSize - 512)
        || std::fread(footer.data(), 1, 512, file) != 512)
        return fail("The end of the image could not be read.");
    if (std::memcmp(footer.data(), "conectix", 8) != 0) {
        // A dynamic disk may keep its footer at offset 0 only after a crash.
        return fail("not a VHD");
    }

    const std::uint64_t diskSize = be64toh_(footer.data() + 48);
    const std::uint32_t diskType = std::uint32_t(be32toh_(footer.data() + 60));
    if (diskSize == 0) return fail("The VHD reports a zero size.");

    auto image = std::unique_ptr<VhdImage>(new VhdImage(file, readOnly));
    image->m_footer = footer;
    image->m_footerOffset = fileSize - 512;
    image->m_diskSize = diskSize;

    if (diskType == 2) {                                  // fixed
        if (fileSize < diskSize + 512) return fail("The fixed VHD is shorter than its own size field.");
        return image;
    }
    if (diskType != 3) {                                  // dynamic / differencing
        if (diskType == 4) return fail("This is a differencing VHD; it cannot be edited directly.");
        return fail("The VHD type is not supported.");
    }

    image->m_dynamic = true;
    const std::uint64_t headerOffset = be64toh_(footer.data() + 16);
    std::vector<std::uint8_t> header(1024);
    if (!seekFile(file, headerOffset)
        || std::fread(header.data(), 1, 1024, file) != 1024)
        return fail("The dynamic VHD header could not be read.");
    if (std::memcmp(header.data(), "cxsparse", 8) != 0)
        return fail("The dynamic VHD header is missing.");

    image->m_tableOffset = be64toh_(header.data() + 16);
    image->m_maxEntries = std::uint32_t(be32toh_(header.data() + 28));
    image->m_blockSize = std::uint32_t(be32toh_(header.data() + 32));
    if (image->m_blockSize < 512 || image->m_blockSize > 256u * 1024u * 1024u)
        return fail("The VHD block size is not sane.");
    if (image->m_maxEntries == 0 || image->m_maxEntries > 0x1000000)
        return fail("The VHD block table is not sane.");
    image->m_bitmapOffset = image->m_tableOffset + std::uint64_t(image->m_maxEntries) * 4;
    image->m_bitmapOffset = (image->m_bitmapOffset + 511) & ~std::uint64_t(511);

    std::vector<std::uint8_t> bat(image->m_maxEntries * 4);
    if (!seekFile(file, image->m_tableOffset)
        || std::fread(bat.data(), 1, bat.size(), file) != bat.size())
        return fail("The VHD block table could not be read.");
    image->m_bat.resize(image->m_maxEntries);
    for (std::uint32_t i = 0; i < image->m_maxEntries; ++i)
        image->m_bat[i] = std::uint32_t(be32toh_(bat.data() + i * 4));
    return image;
}

bool VhdImage::read(std::uint64_t offset, void *buffer, std::size_t length)
{
    auto *out = static_cast<std::uint8_t *>(buffer);
    // A fixed disk is the data, in order, right after the start of the file.
    if (!m_dynamic) {
        if (offset + length > m_diskSize) return false;
        return fileRead(offset, buffer, length);
    }
    while (length > 0) {
        if (offset >= m_diskSize) return false;
        const std::uint64_t index = offset / m_blockSize;
        const std::uint64_t within = offset % m_blockSize;
        std::size_t chunk = std::size_t(std::min<std::uint64_t>(length, m_blockSize - within));
        if (!m_dynamic) {
            if (!fileRead(offset, out, chunk)) return false;
        } else {
            if (index >= m_bat.size()) return false;
            const std::uint32_t entry = m_bat[std::size_t(index)];
            if (entry == kUnallocated) std::memset(out, 0, chunk);   // never written
            else if (!fileRead(std::uint64_t(entry) * 512 + within, out, chunk)) return false;
        }
        offset += chunk;
        out += chunk;
        length -= chunk;
    }
    return true;
}

bool VhdImage::writeFooter(std::uint64_t at)
{
    return fileWrite(at, m_footer.data(), m_footer.size());
}

bool VhdImage::allocateBlock(std::uint32_t index)
{
    if (m_readOnly) return false;
    if (index >= m_bat.size() || m_bat[index] != kUnallocated) return false;
    // A new block goes where the footer is now; the footer moves behind it.
    if (m_footerOffset % 512 != 0) return false;
    std::vector<std::uint8_t> zeros(std::size_t(m_blockSize), 0);
    if (!fileWrite(m_footerOffset, zeros.data(), zeros.size())) return false;
    const std::uint64_t newFooter = m_footerOffset + m_blockSize;
    if (!writeFooter(newFooter)) return false;

    // Grow the file to the new footer: writing it is the growth, but a file
    // opened read-write still has to be positioned past the block.
    const std::uint64_t sector = m_footerOffset / 512;
    if (sector > 0xFFFFFFFFull) return false;
    m_bat[index] = std::uint32_t(sector);
    m_footerOffset = newFooter;

    std::uint8_t entry[4];
    putbe32(entry, m_bat[index]);
    if (!fileWrite(m_tableOffset + std::uint64_t(index) * 4, entry, 4)) return false;

    // The bitmap is bookkeeping for differencing disks; keep it truthful.
    if (m_bitmapOffset > 0) {
        std::uint8_t bit = 0;
        if (fileRead(m_bitmapOffset + index / 8, &bit, 1)) {
            bit = std::uint8_t(bit | (1u << (index % 8)));
            fileWrite(m_bitmapOffset + index / 8, &bit, 1);
        }
    }
    return true;
}

bool VhdImage::write(std::uint64_t offset, const void *buffer, std::size_t length)
{
    const auto *in = static_cast<const std::uint8_t *>(buffer);
    if (!m_dynamic) {
        if (offset + length > m_diskSize) return false;
        return fileWrite(offset, buffer, length);
    }
    while (length > 0) {
        if (offset >= m_diskSize) return false;
        const std::uint64_t index = offset / m_blockSize;
        const std::uint64_t within = offset % m_blockSize;
        std::size_t chunk = std::size_t(std::min<std::uint64_t>(length, m_blockSize - within));
        if (!m_dynamic) {
            if (!fileWrite(offset, in, chunk)) return false;
        } else {
            if (index >= m_bat.size()) return false;
            if (m_bat[std::size_t(index)] == kUnallocated && !allocateBlock(std::uint32_t(index)))
                return false;
            const std::uint64_t at = std::uint64_t(m_bat[std::size_t(index)]) * 512 + within;
            if (!fileWrite(at, in, chunk)) return false;
        }
        offset += chunk;
        in += chunk;
        length -= chunk;
    }
    return true;
}

// ---- superblock / descriptor accessors -----------------------------------

constexpr std::uint32_t kMagic = 0xEF53;

// incompat
constexpr std::uint32_t kIncompatFiletype = 0x2;
constexpr std::uint32_t kIncompatRecover = 0x4;
constexpr std::uint32_t kIncompatJournalDev = 0x8;
constexpr std::uint32_t kIncompatMetaBg = 0x10;
constexpr std::uint32_t kIncompatExtents = 0x40;
constexpr std::uint32_t kIncompat64Bit = 0x80;
constexpr std::uint32_t kIncompatMmp = 0x100;
constexpr std::uint32_t kIncompatFlexBg = 0x200;
constexpr std::uint32_t kIncompatEaInode = 0x400;
constexpr std::uint32_t kIncompatDirdata = 0x1000;
constexpr std::uint32_t kIncompatCsumSeed = 0x2000;
constexpr std::uint32_t kIncompatInlineData = 0x8000;
constexpr std::uint32_t kIncompatEncrypt = 0x10000;

// ro_compat
constexpr std::uint32_t kRoBigalloc = 0x200;
constexpr std::uint32_t kRoMetadataCsum = 0x400;
constexpr std::uint32_t kRoQuota = 0x100;
constexpr std::uint32_t kRoGdtCsum = 0x10;

constexpr std::uint32_t kExtentsFlag = 0x80000;
constexpr std::uint32_t kIndexFlag = 0x1000;
constexpr std::uint16_t kInodeDir = 0x4000;
constexpr std::uint16_t kInodeReg = 0x8000;

} // namespace

// ---------------------------------------------------------------------------
// image
// ---------------------------------------------------------------------------

std::unique_ptr<Image> openImage(const std::string &path, std::string *error)
{
    std::FILE *file = std::fopen(path.c_str(), "r+b");
    bool readOnly = false;
    if (!file) {
        file = std::fopen(path.c_str(), "rb");
        readOnly = true;
    }
    if (!file) {
        if (error) *error = "The image could not be opened: " + path;
        return nullptr;
    }
    std::uint64_t length = 0;
    if (!seekFileEnd(file) || !fileLength(file, &length) || !seekFile(file, 0)) {
        std::fclose(file);
        if (error) *error = "The image size could not be read.";
        return nullptr;
    }
    if (length == 0) {
        std::fclose(file);
        if (error) *error = "The image is empty.";
        return nullptr;
    }

    // The VHD footer is the last 512 bytes and starts with its own cookie.
    bool vhd = false;
    if (length > 512) {
        std::uint8_t cookie[8] = {};
        if (seekFile(file, length - 512) && std::fread(cookie, 1, 8, file) == 8)
            vhd = std::memcmp(cookie, "conectix", 8) == 0;
        if (!vhd && seekFile(file, 0) && std::fread(cookie, 1, 8, file) == 8)
            vhd = std::memcmp(cookie, "conectix", 8) == 0;
        seekFile(file, 0);
    }
    if (vhd) {
        std::string why;
        if (auto image = VhdImage::open(file, length, readOnly, &why))
            return image;
        if (why != "not a VHD") {
            if (error) *error = why;
            return nullptr;
        }
    }
    return std::unique_ptr<Image>(new FileImage(file, length));
}

std::string joinPath(const std::string &base, const std::string &name)
{
    if (name.empty()) return base;
    if (name.front() == '/') return name;
    if (base.empty() || base == "/") return "/" + name;
    return base + "/" + name;
}

std::vector<std::string> splitPath(const std::string &path)
{
    std::vector<std::string> parts;
    std::string part;
    for (char c : path) {
        if (c == '/') {
            if (!part.empty()) parts.push_back(part);
            part.clear();
        } else {
            part.push_back(c);
        }
    }
    if (!part.empty()) parts.push_back(part);
    return parts;
}

// ---------------------------------------------------------------------------
// mount
// ---------------------------------------------------------------------------

Volume::~Volume() = default;

std::unique_ptr<Volume> Volume::mount(std::unique_ptr<Image> image, std::string *error)
{
    auto fail = [&](const std::string &why) -> std::unique_ptr<Volume> {
        if (error) *error = why;
        return nullptr;
    };
    if (!image) return fail("There is no image to open.");

    std::uint64_t offset = 0;
    std::uint8_t boot[512] = {};
    bool found = false;
    const auto probeExt4 = [&](std::uint64_t start) {
        if (start > image->size() || image->size() - start < 2048 + 0x3A)
            return false;
        std::uint8_t magic[2] = {};
        return image->read(start + 1024 + 0x38, magic, sizeof(magic))
            && le16(magic) == kMagic;
    };
    const auto acceptPartition = [&](std::uint64_t start) {
        if (!found && probeExt4(start)) {
            offset = start;
            found = true;
        }
    };

    // MBR primary partitions. BlueStacks images can use either a conventional
    // MBR or GPT; probing the superblock itself avoids trusting a partition
    // type byte that may be stale in vendor-built images.
    if (image->read(0, boot, sizeof(boot)) && boot[510] == 0x55 && boot[511] == 0xAA) {
        std::vector<std::uint64_t> extended;
        for (int index = 0; index < 4 && !found; ++index) {
            const std::uint8_t *entry = boot + 446 + index * 16;
            const std::uint8_t type = entry[4];
            const std::uint64_t start = std::uint64_t(le32(entry + 8)) * 512;
            if (type == 0x05 || type == 0x0F || type == 0x85)
                extended.push_back(start);
            else if (type != 0 && type != 0xEE)
                acceptPartition(start);
        }

        // Logical partitions in an extended MBR partition. The EBR chain is
        // bounded and visited-set protected so malformed tables cannot loop.
        for (const std::uint64_t base : extended) {
            std::uint64_t ebr = base;
            std::vector<std::uint64_t> seen;
            for (int n = 0; n < 128 && !found && ebr < image->size(); ++n) {
                if (std::find(seen.begin(), seen.end(), ebr) != seen.end()) break;
                seen.push_back(ebr);
                std::uint8_t sector[512] = {};
                if (!image->read(ebr, sector, sizeof(sector))
                    || sector[510] != 0x55 || sector[511] != 0xAA)
                    break;
                const std::uint8_t *logical = sector + 446;
                if (logical[4] != 0)
                    acceptPartition(ebr + std::uint64_t(le32(logical + 8)) * 512);
                const std::uint8_t *next = sector + 462;
                if (next[4] == 0 || le32(next + 8) == 0) break;
                ebr = base + std::uint64_t(le32(next + 8)) * 512;
            }
        }
    }

    // GPT: header at LBA 1, partition array normally at LBA 2. We accept the
    // standard 512-byte logical-sector layout used by VHDs and validate all
    // offsets/counts against the virtual disk before reading them.
    if (!found && image->read(512, boot, sizeof(boot))
        && std::memcmp(boot, "EFI PART", 8) == 0) {
        const std::uint64_t entriesLba = le64(boot + 72);
        const std::uint32_t entryCount = le32(boot + 80);
        const std::uint32_t entrySize = le32(boot + 84);
        if (entryCount > 0 && entrySize >= 128 && entrySize <= 4096
            && entriesLba <= image->size() / 512) {
            const std::uint32_t count = std::min(entryCount, 4096u);
            const std::uint64_t arrayStart = entriesLba * 512;
            for (std::uint32_t i = 0; i < count && !found; ++i) {
                const std::uint64_t at = arrayStart + std::uint64_t(i) * entrySize;
                if (at < arrayStart) break;
                if (at > image->size() || image->size() - at < entrySize) break;
                std::vector<std::uint8_t> entry(entrySize);
                if (!image->read(at, entry.data(), entry.size())) break;
                bool empty = true;
                for (int j = 0; j < 16; ++j) empty = empty && entry[j] == 0;
                if (empty) continue;
                const std::uint64_t firstLba = le64(entry.data() + 32);
                if (firstLba > image->size() / 512) continue;
                acceptPartition(firstLba * 512);
            }
        }
    }

    if (!found)
        acceptPartition(0);

    // A few vendor images omit a usable partition map while leaving the ext4
    // volume at a sector-aligned offset. Scan the first 64 MiB sequentially
    // (rather than issuing millions of random reads) for the ext4 superblock
    // magic and validate candidates at 512-byte boundaries.
    if (!found) {
        constexpr std::uint64_t kMagicOffset = 1024 + 0x38;
        constexpr std::uint64_t kProbeLimit = 64ull * 1024 * 1024;
        constexpr std::size_t kChunk = 4u * 1024 * 1024;
        const std::uint64_t limit = std::min(image->size(), kProbeLimit);
        std::vector<std::uint8_t> bytes(kChunk + 1);
        std::uint64_t readAt = 0;
        std::uint8_t carry = 0;
        bool haveCarry = false;
        while (!found && readAt < limit) {
            const std::size_t count = std::size_t(std::min<std::uint64_t>(kChunk, limit - readAt));
            std::uint64_t base = readAt;
            std::size_t searchable = count;
            if (haveCarry) {
                bytes[0] = carry;
                if (!image->read(readAt, bytes.data() + 1, count)) break;
                base = readAt - 1;
                searchable = count + 1;
            } else if (!image->read(readAt, bytes.data(), count)) {
                break;
            }
            for (std::size_t i = 0; i + 1 < searchable; ++i) {
                if (bytes[i] != 0x53 || bytes[i + 1] != 0xEF) continue;
                const std::uint64_t magicAt = base + i;
                if (magicAt < kMagicOffset) continue;
                const std::uint64_t candidate = magicAt - kMagicOffset;
                if ((candidate % 512) == 0)
                    acceptPartition(candidate);
                if (found) break;
            }
            carry = bytes[haveCarry ? count : count - 1];
            haveCarry = true;
            readAt += count;
        }
    }

    if (!found)
        return fail("No ext4 filesystem was found in this disk image. The editor checks the "
                    "image itself, MBR, extended-MBR and GPT partitions, plus sector-aligned "
                    "volumes in the first 64 MiB. Select a Root.vhd or engine package, not the "
                    "BlueStacks setup installer.");

    auto volume = std::unique_ptr<Volume>(new Volume());
    volume->m_image = std::move(image);
    volume->m_offset = offset;
    volume->m_super.assign(1024, 0);
    if (!volume->m_image->read(offset + 1024, volume->m_super.data(), 1024))
        return fail("The superblock could not be read.");

    const std::uint8_t *sb = volume->m_super.data();
    if (le16(sb + 0x38) != kMagic) return fail("The superblock is not an ext2/3/4 superblock.");
    volume->m_inodes = le32(sb + 0x00);
    volume->m_blocks = le32(sb + 0x04);
    volume->m_freeBlocks = le32(sb + 0x0C);
    volume->m_freeInodes = le32(sb + 0x10);
    volume->m_firstDataBlock = le32(sb + 0x14);
    const std::uint32_t logBlock = le32(sb + 0x18);
    if (logBlock > 6) return fail("The block size is not sane.");
    volume->m_blockSize = 1024u << logBlock;
    volume->m_blocksPerGroup = le32(sb + 0x20);
    volume->m_inodesPerGroup = le32(sb + 0x28);
    volume->m_inodeSize = le16(sb + 0x58);
    volume->m_firstIno = le32(sb + 0x54);
    volume->m_compat = le32(sb + 0x5C);
    volume->m_incompat = le32(sb + 0x60);
    volume->m_roCompat = le32(sb + 0x64);
    volume->m_descSize = (volume->m_incompat & kIncompat64Bit) ? le16(sb + 0xFE) : 32;
    volume->m_csumSeed = le32(sb + 0x270);
    volume->m_hasSeed = (volume->m_incompat & kIncompatCsumSeed) != 0;
    volume->m_hasCsum = (volume->m_roCompat & kRoMetadataCsum) != 0;
    const std::uint64_t blocksHigh = le32(sb + 0x150);
    volume->m_blocks = std::uint32_t(volume->m_blocks + std::uint32_t(blocksHigh << 0));
    if (blocksHigh)
        volume->m_readOnlyReason = "This filesystem is larger than 4 TiB; the editor works on "
                                   "player images only.";
    if (volume->m_inodeSize < 128 || volume->m_inodeSize > volume->m_blockSize
        || (volume->m_inodeSize & (volume->m_inodeSize - 1)) != 0)
        return fail("The inode size is not sane.");
    if (volume->m_blocksPerGroup == 0 || volume->m_inodesPerGroup == 0)
        return fail("The group geometry is empty.");
    if (volume->m_descSize != 32 && volume->m_descSize != 64)
        return fail("The group descriptor size is not supported.");
    volume->m_groups = std::uint32_t((volume->m_blocks - volume->m_firstDataBlock
                                      + volume->m_blocksPerGroup - 1)
                                     / volume->m_blocksPerGroup);
    if (volume->m_groups == 0 || volume->m_groups > 0x10000)
        return fail("The group count is not sane.");

    // Anything that changes how a plain inode is read or written is refused
    // rather than guessed at.
    // Note the two words are different: 0x200 is flex_bg in incompat but
    // bigalloc in ro_compat, so each feature is tested against its own word.
    const std::uint32_t unsupported =
        kIncompatMetaBg | kIncompatMmp | kIncompatInlineData | kIncompatEncrypt
        | kIncompatJournalDev;
    if ((volume->m_incompat & unsupported) || (volume->m_roCompat & kRoBigalloc))
        return fail("This filesystem uses a feature the editor does not change "
                    "(meta_bg, MMP, inline data, encryption or bigalloc).");
    if (!(volume->m_incompat & kIncompatExtents))
        return fail("This filesystem does not use extents; only ext4 with extents is edited.");

    // Superblock checksum: everything read below is only trusted if it passes.
    if (volume->m_hasCsum) {
        const std::uint32_t stored = le32(sb + 0x3FC);
        const std::uint32_t computed = crc32c(~0u, sb, 0x3FC);
        if (stored != computed) {
            volume->m_readOnlyReason = "The superblock checksum does not match; the image is "
                                       "opened read-only so it cannot be damaged further.";
        }
        // Group descriptors: the first one is enough to prove the geometry.
        std::vector<std::uint8_t> descriptor(volume->m_descSize);
        if (volume->groupDescriptor(0, &descriptor)
            && le16(descriptor.data() + 0x1E) != volume->groupChecksum(0, descriptor)
            && volume->m_readOnlyReason.empty())
            volume->m_readOnlyReason = "The first group descriptor does not verify; the image is "
                                       "opened read-only.";
    }

    volume->m_hasJournal = (volume->m_compat & 0x4) != 0;
    const std::uint16_t state = le16(sb + 0x3A);
    volume->m_superDirty = (state & 0x1) == 0 || (volume->m_incompat & kIncompatRecover) != 0;
    if (volume->m_superDirty && volume->m_readOnlyReason.empty())
        volume->m_readOnlyReason = "The filesystem was not cleanly unmounted (or its journal holds "
                                   "work). Start BlueStacks once and shut the instance down "
                                   "cleanly before editing the image.";

    volume->m_writable = volume->m_image->writable() && volume->m_readOnlyReason.empty();
    if (!volume->m_image->writable() && volume->m_readOnlyReason.empty())
        volume->m_readOnlyReason = "The image file could not be opened for writing. It may have the Windows Read-only attribute, be in use by BlueStacks, or lack Modify permission. Use Enable editing or close BlueStacks and check the file permissions.";
    return volume;
}

std::string Volume::description() const
{
    std::string text = "ext4, " + std::to_string(m_blockSize) + "-byte blocks, "
        + std::to_string(m_blocks) + " blocks, " + std::to_string(m_inodes) + " inodes";
    if (m_hasCsum) text += ", metadata checksums";
    if (m_offset) text += ", partition starts at " + std::to_string(m_offset) + " bytes";
    return text;
}

std::string Volume::features() const
{
    std::string text;
    const auto add = [&text](const char *name) {
        if (!text.empty()) text += " ";
        text += name;
    };
    if (m_hasJournal) add("journal");
    if (m_incompat & kIncompatExtents) add("extents");
    if (m_incompat & kIncompat64Bit) add("64bit");
    if (m_incompat & kIncompatFlexBg) add("flex_bg");
    if (m_roCompat & 0x4) add("dir_index");
    if (m_roCompat & 0x1) add("sparse_super");
    if (m_roCompat & 0x8) add("huge_file");
    if (m_roCompat & 0x20) add("dir_nlink");
    if (m_roCompat & 0x40) add("extra_isize");
    if (m_roCompat & kRoMetadataCsum) add("metadata_csum");
    if (m_hasSeed) add("csum_seed");
    return text;
}

bool Volume::readBlocks(std::uint64_t block, void *buffer, std::uint32_t count) const
{
    return m_image->read(m_offset + block * m_blockSize, buffer,
                         std::size_t(count) * m_blockSize);
}

bool Volume::writeBlocks(std::uint64_t block, const void *buffer, std::uint32_t count)
{
    if (!m_writable) return false;
    return m_image->write(m_offset + block * m_blockSize, buffer,
                          std::size_t(count) * m_blockSize);
}

bool Volume::groupDescriptor(int group, std::vector<std::uint8_t> *out) const
{
    const std::uint64_t first = std::uint64_t(m_firstDataBlock) + 1;  // block of the GDT
    const std::uint64_t index = std::uint64_t(group) * m_descSize;
    const std::uint64_t block = first + index / m_blockSize;
    const std::uint32_t within = std::uint32_t(index % m_blockSize);
    if (within + m_descSize > m_blockSize) {
        // A descriptor never crosses a block: the table is block aligned.
        return false;
    }
    out->assign(m_descSize, 0);
    return m_image->read(m_offset + block * m_blockSize + within, out->data(), m_descSize);
}

bool Volume::storeGroupDescriptor(int group, std::vector<std::uint8_t> *raw)
{
    const std::uint64_t first = std::uint64_t(m_firstDataBlock) + 1;
    const std::uint64_t index = std::uint64_t(group) * m_descSize;
    const std::uint64_t block = first + index / m_blockSize;
    const std::uint32_t within = std::uint32_t(index % m_blockSize);
    if (m_hasCsum) put16(raw->data() + 0x1E, groupChecksum(group, *raw));
    return m_image->write(m_offset + block * m_blockSize + within, raw->data(), m_descSize);
}

std::uint32_t Volume::crc() const
{
    if (m_hasSeed) return m_csumSeed;
    const std::uint32_t base = crc32c(~0u, m_super.data() + 0x68, 16);   // s_uuid
    if (m_hasCsum) return base;
    return base;
}

std::uint16_t Volume::superChecksum() const
{
    return std::uint16_t(crc32c(~0u, m_super.data(), 0x3FC) & 0xFFFF);
}

std::uint16_t Volume::groupChecksum(int group, const std::vector<std::uint8_t> &descriptor) const
{
    if (!m_hasCsum) return 0;
    std::uint32_t value = crc32c(crc(), &group, sizeof(group));
    std::vector<std::uint8_t> copy = descriptor;
    put16(copy.data() + 0x1E, 0);
    value = crc32c(value, copy.data(), copy.size());
    return std::uint16_t(value & 0xFFFF);
}

std::uint64_t Volume::inodeOffset(std::uint32_t inode) const
{
    const std::uint64_t index = std::uint64_t(inode - 1);
    const std::uint32_t group = std::uint32_t(index / m_inodesPerGroup);
    const std::uint32_t within = std::uint32_t(index % m_inodesPerGroup);
    std::vector<std::uint8_t> descriptor(m_descSize);
    if (!groupDescriptor(int(group), &descriptor)) return 0;
    const std::uint64_t table = le32(descriptor.data() + 0x8)
        | (m_descSize >= 64 ? std::uint64_t(le32(descriptor.data() + 0x28)) << 32 : 0);
    return table * m_blockSize + std::uint64_t(within) * m_inodeSize;
}

bool Volume::loadInode(std::uint32_t inode, std::vector<std::uint8_t> *raw, std::string *error) const
{
    if (inode == 0 || inode > m_inodes) {
        if (error) *error = "The inode number is out of range.";
        return false;
    }
    const std::uint64_t at = inodeOffset(inode);
    if (at == 0) {
        if (error) *error = "The inode table could not be located.";
        return false;
    }
    raw->assign(m_inodeSize, 0);
    if (!m_image->read(m_offset + at, raw->data(), m_inodeSize)) {
        if (error) *error = "The inode could not be read.";
        return false;
    }
    return true;
}

std::uint32_t Volume::inodeSeed(std::uint32_t inode, std::uint32_t generation) const
{
    std::uint32_t value = crc32c(crc(), &inode, sizeof(inode));
    return crc32c(value, &generation, sizeof(generation));
}

// The kernel's ext4_inode_csum, byte for byte: the checksum fields are fed as
// two zero bytes, and the high field only exists when i_extra_isize is large
// enough to hold it.
std::uint32_t Volume::inodeChecksum(std::uint32_t inode, const std::vector<std::uint8_t> &raw) const
{
    const std::uint32_t generation = le32(raw.data() + 0x64);
    std::uint32_t value = inodeSeed(inode, generation);
    static const std::uint8_t zeros[2] = { 0, 0 };
    value = crc32c(value, raw.data(), 0x7C);
    value = crc32c(value, zeros, 2);
    value = crc32c(value, raw.data() + 0x7E, 128 - 0x7E);
    if (m_inodeSize > 128) {
        value = crc32c(value, raw.data() + 128, 2);   // i_extra_isize
        const std::uint16_t extra = le16(raw.data() + 0x80);
        if (extra >= 4) {
            value = crc32c(value, zeros, 2);
            value = crc32c(value, raw.data() + 0x84, raw.size() - 0x84);
        } else {
            value = crc32c(value, raw.data() + 0x82, raw.size() - 0x82);
        }
    }
    return value;
}

std::uint32_t Volume::blockChecksum(std::uint32_t inode, std::uint32_t generation,
                                    const std::uint8_t *data, std::size_t length) const
{
    return crc32c(inodeSeed(inode, generation), data, length);
}

Stat Volume::stat(std::uint32_t inode, std::string *error) const
{
    Stat info;
    std::vector<std::uint8_t> raw;
    if (!loadInode(inode, &raw, error)) return info;
    info.inode = inode;
    info.mode = le16(raw.data() + 0x00);
    info.uid = le16(raw.data() + 0x02) | (std::uint32_t(le16(raw.data() + 0x78)) << 16);
    info.size = le32(raw.data() + 0x04) | (std::uint64_t(le32(raw.data() + 0x6C)) << 32);
    info.gid = le16(raw.data() + 0x18) | (std::uint32_t(le16(raw.data() + 0x7A)) << 16);
    info.links = le16(raw.data() + 0x1A);
    info.blocks = le32(raw.data() + 0x1C) | (std::uint32_t(le16(raw.data() + 0x74)) << 16);
    info.flags = le32(raw.data() + 0x20);
    info.mtime = le32(raw.data() + 0x10);
    return info;
}

// ---------------------------------------------------------------------------
// extents
// ---------------------------------------------------------------------------

namespace {

struct Extent
{
    std::uint32_t logical = 0;
    std::uint32_t length = 0;
    std::uint64_t physical = 0;
    bool initialized = true;
};

struct ExtentNode
{
    std::uint16_t depth = 0;
    std::vector<Extent> extents;
    struct Child
    {
        std::uint32_t logical;
        std::uint64_t block;
    };
    std::vector<Child> children;
};

bool parseExtentNode(const std::uint8_t *data, std::size_t size, ExtentNode *node)
{
    if (size < 12 || le16(data) != 0xF30A) return false;
    const std::uint16_t entries = le16(data + 2);
    const std::uint16_t max = le16(data + 4);
    node->depth = le16(data + 6);
    if (entries > max) return false;
    const std::size_t entrySize = 12;
    if (12 + std::size_t(entries) * entrySize > size) return false;
    for (std::uint16_t i = 0; i < entries; ++i) {
        const std::uint8_t *entry = data + 12 + std::size_t(i) * entrySize;
        if (node->depth == 0) {
            Extent extent;
            extent.logical = le32(entry);
            const std::uint16_t rawLength = le16(entry + 4);
            extent.initialized = rawLength <= 32768;
            extent.length = extent.initialized ? rawLength : std::uint32_t(rawLength - 32768);
            extent.physical = (std::uint64_t(le16(entry + 6)) << 32) | le32(entry + 8);
            if (extent.length == 0) continue;
            node->extents.push_back(extent);
        } else {
            const std::uint32_t logical = le32(entry);
            const std::uint64_t block = (std::uint64_t(le16(entry + 8)) << 32) | le32(entry + 4);
            node->children.push_back({ logical, block });
        }
    }
    return true;
}

} // namespace

std::vector<std::uint64_t> Volume::extentMap(std::uint32_t, const std::vector<std::uint8_t> &raw,
                                             std::string *error) const
{
    std::vector<std::uint64_t> map;
    if (!(le32(raw.data() + 0x20) & kExtentsFlag)) {
        // Classic direct/indirect map: only the 12 direct pointers are read,
        // which covers every small file; anything bigger is refused.
        for (int i = 0; i < 12; ++i) {
            const std::uint32_t block = le32(raw.data() + 0x28 + i * 4);
            if (block == 0) break;
            map.push_back(block);
        }
        return map;
    }

    std::vector<std::pair<std::uint32_t, std::uint64_t>> pending;
    struct Frame
    {
        std::uint64_t block;
        std::uint32_t logicalStart;
    };
    ExtentNode root;
    if (!parseExtentNode(raw.data() + 0x28, 60, &root)) {
        if (error) *error = "The inode's extent header is not valid.";
        return map;
    }
    if (root.depth == 0) {
        for (const auto &extent : root.extents)
            for (std::uint32_t i = 0; i < extent.length; ++i)
                map.push_back(extent.physical + i);
        return map;
    }

    std::vector<Frame> stack;
    for (const auto &child : root.children) stack.push_back({ child.block, child.logical });
    int guard = 0;
    while (!stack.empty() && ++guard < 100000) {
        const Frame frame = stack.back();
        stack.pop_back();
        std::vector<std::uint8_t> block(m_blockSize);
        if (!m_image->read(m_offset + frame.block * m_blockSize, block.data(), m_blockSize)) {
            if (error) *error = "An extent block could not be read.";
            return map;
        }
        ExtentNode node;
        if (!parseExtentNode(block.data(), m_blockSize, &node)) {
            if (error) *error = "An extent block is not valid.";
            return map;
        }
        if (node.depth == 0) {
            for (const auto &extent : node.extents)
                for (std::uint32_t i = 0; i < extent.length; ++i)
                    map.push_back(extent.physical + i);
        } else {
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
                stack.push_back({ it->block, it->logical });
        }
    }
    return map;
}

std::vector<std::uint32_t> Volume::fileBlocks(std::uint32_t inode, std::string *error) const
{
    std::vector<std::uint8_t> raw;
    if (!loadInode(inode, &raw, error)) return {};
    std::vector<std::uint32_t> blocks;
    for (std::uint64_t block : extentMap(inode, raw, error)) blocks.push_back(std::uint32_t(block));
    return blocks;
}

std::vector<std::uint8_t> Volume::readFile(std::uint32_t inode, std::string *error)
{
    const Stat info = stat(inode, error);
    std::vector<std::uint8_t> data;
    if (!info.regular() && !info.symlink()) {
        if (error) *error = "Only regular files and symbolic-link targets can be read.";
        return data;
    }
    if (info.symlink() && info.blocks == 0) {
        if (info.size > 60) {
            if (error) *error = "The inline symbolic-link target is larger than ext4 allows.";
            return data;
        }
        std::vector<std::uint8_t> raw;
        if (!loadInode(inode, &raw, error) || raw.size() < 0x28 + info.size) return data;
        data.assign(raw.begin() + 0x28, raw.begin() + 0x28 + std::size_t(info.size));
        return data;
    }
    if (info.size == 0) return data;
    std::vector<std::uint32_t> blocks = fileBlocks(inode, error);
    const std::uint64_t needed = (info.size + m_blockSize - 1) / m_blockSize;
    if (blocks.size() < needed) {
        if (error) *error = "The file's block map is incomplete.";
        return {};
    }
    data.resize(std::size_t(info.size));
    std::vector<std::uint8_t> block(m_blockSize);
    for (std::uint64_t i = 0; i < needed; ++i) {
        if (!readBlocks(blocks[std::size_t(i)], block.data(), 1)) {
            if (error) *error = "A data block could not be read.";
            return {};
        }
        const std::uint64_t done = i * m_blockSize;
        const std::size_t chunk = std::size_t(std::min<std::uint64_t>(m_blockSize, info.size - done));
        std::memcpy(data.data() + done, block.data(), chunk);
    }
    return data;
}

// ---------------------------------------------------------------------------
// directories
// ---------------------------------------------------------------------------

namespace {

std::uint32_t direntChecksumSeed(std::uint32_t crc, std::uint32_t inode, std::uint32_t generation)
{
    crc = crc32c(crc, &inode, sizeof(inode));
    return crc32c(crc, &generation, sizeof(generation));
}

} // namespace

Listing Volume::list(std::uint32_t inode, std::string *error)
{
    Listing listing;
    listing.inode = inode;
    std::string why;
    const Stat info = stat(inode, &why);
    if (!info.dir()) {
        if (error) *error = "That inode is not a folder.";
        return listing;
    }
    std::vector<std::uint32_t> blocks = fileBlocks(inode, error);
    if (blocks.empty() && info.size > 0) return listing;
    const bool indexed = (info.flags & kIndexFlag) != 0;

    std::vector<std::uint8_t> raw;
    loadInode(inode, &raw, &why);
    const std::uint32_t generation = raw.size() >= 0x68 ? le32(raw.data() + 0x64) : 0;

    std::vector<std::uint8_t> block(m_blockSize);
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        // An indexed directory keeps its hash tree in the first block; the
        // entries themselves are in the blocks after it.
        if (indexed && index == 0) continue;
        if (!readBlocks(blocks[index], block.data(), 1)) {
            if (error) *error = "A directory block could not be read.";
            return listing;
        }
        if (m_hasCsum) {
            const std::uint8_t *tail = block.data() + m_blockSize - 12;
            if (le32(tail) == 0 && tail[6] == 0 && tail[7] == 0xDE) {
                std::uint32_t value = direntChecksumSeed(crc(), inode, generation);
                value = crc32c(value, block.data(), m_blockSize - 12);
                if (le32(tail + 8) != value && error && error->empty())
                    *error = "A directory block failed its checksum.";
            }
        }
        std::uint32_t offset = 0;
        while (offset + 8 <= m_blockSize) {
            const std::uint8_t *entry = block.data() + offset;
            const std::uint32_t entryInode = le32(entry);
            const std::uint16_t recordLength = le16(entry + 4);
            const std::uint8_t nameLength = entry[6];
            if (recordLength < 8 || offset + recordLength > m_blockSize) break;
            if (entryInode != 0 && nameLength > 0 && 8 + nameLength <= recordLength) {
                const std::string name(reinterpret_cast<const char *>(entry + 8), nameLength);
                if (name != "." && name != "..") {
                    DirEntry item;
                    item.name = name;
                    item.inode = entryInode;
                    item.type = entry[7];
                    if (item.type == 0) {
                        Stat child = stat(entryInode, nullptr);
                        item.dir = child.dir();
                    } else {
                        item.dir = item.type == 2;
                    }
                    listing.entries.push_back(item);
                }
            }
            offset += recordLength;
        }
    }

    std::sort(listing.entries.begin(), listing.entries.end(),
              [](const DirEntry &a, const DirEntry &b) {
                  if (a.dir != b.dir) return a.dir;
                  return a.name < b.name;
              });
    return listing;
}

std::uint32_t Volume::resolve(const std::string &path, std::string *error)
{
    std::uint32_t inode = 2;   // the root
    for (const std::string &part : splitPath(path)) {
        const Listing listing = list(inode, error);
        bool found = false;
        for (const auto &entry : listing.entries) {
            if (entry.name == part) {
                inode = entry.inode;
                found = true;
                break;
            }
        }
        if (!found) {
            if (error) *error = "“" + part + "” is not in " + (path.empty() ? "/" : path);
            return 0;
        }
    }
    return inode;
}

Listing Volume::list(const std::string &path, std::string *error)
{
    const std::uint32_t inode = resolve(path, error);
    if (inode == 0) return Listing();
    Listing listing = list(inode, error);
    listing.path = path.empty() ? "/" : path;
    return listing;
}

} // namespace RootDisk
