// The write half of the ext4 engine: allocation, inodes, extents, directory
// entries and deletion. Every structure it touches is checksummed the way
// e2fsprogs and the kernel expect, so the image stays mountable.
#include "RootDisk.h"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace RootDisk {

namespace {

std::uint16_t le16(const std::uint8_t *p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t le32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
        | (std::uint32_t(p[3]) << 24);
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

std::uint32_t crc32cImpl(std::uint32_t crc, const void *data, std::size_t length)
{
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1) ? (0x82F63B78u ^ (value >> 1)) : (value >> 1);
            table[i] = value;
        }
        ready = true;
    }
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    for (std::size_t i = 0; i < length; ++i)
        crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

constexpr std::uint32_t kExtentsFlag = 0x80000;
constexpr std::uint32_t kIndexFlag = 0x1000;
constexpr std::uint16_t kInodeDir = 0x4000;

std::uint32_t now()
{
    return std::uint32_t(std::time(nullptr));
}

struct GroupInfo
{
    std::uint32_t blockBitmap = 0;
    std::uint32_t inodeBitmap = 0;
    std::uint32_t inodeTable = 0;
    std::uint16_t freeBlocks = 0;
    std::uint16_t freeInodes = 0;
    std::uint16_t usedDirs = 0;
    std::uint16_t itableUnused = 0;
};

GroupInfo readGroup(const std::vector<std::uint8_t> &descriptor)
{
    GroupInfo info;
    info.blockBitmap = le32(descriptor.data() + 0x00);
    info.inodeBitmap = le32(descriptor.data() + 0x04);
    info.inodeTable = le32(descriptor.data() + 0x08);
    info.freeBlocks = le16(descriptor.data() + 0x0C);
    info.freeInodes = le16(descriptor.data() + 0x0E);
    info.usedDirs = le16(descriptor.data() + 0x10);
    info.itableUnused = le16(descriptor.data() + 0x1C);
    if (descriptor.size() >= 64) {
        info.blockBitmap |= le32(descriptor.data() + 0x20);
        info.inodeBitmap |= le32(descriptor.data() + 0x24);
        info.inodeTable |= le32(descriptor.data() + 0x28);
    }
    return info;
}

void applyGroup(const GroupInfo &info, std::vector<std::uint8_t> *descriptor)
{
    put32(descriptor->data() + 0x00, info.blockBitmap);
    put32(descriptor->data() + 0x04, info.inodeBitmap);
    put32(descriptor->data() + 0x08, info.inodeTable);
    put16(descriptor->data() + 0x0C, info.freeBlocks);
    put16(descriptor->data() + 0x0E, info.freeInodes);
    put16(descriptor->data() + 0x10, info.usedDirs);
    put16(descriptor->data() + 0x1C, info.itableUnused);
}

struct Run
{
    std::uint32_t logical;
    std::uint32_t length;
    std::uint64_t physical;
};

std::vector<Run> coalesce(const std::vector<std::uint32_t> &blocks)
{
    std::vector<Run> runs;
    for (std::uint32_t i = 0; i < blocks.size(); ++i) {
        if (!runs.empty() && runs.back().physical + runs.back().length == blocks[i]
            && runs.back().length < 32768) {
            runs.back().length++;
            continue;
        }
        runs.push_back({ i, 1, blocks[i] });
    }
    return runs;
}

void putExtentEntry(std::uint8_t *entry, const Run &run)
{
    put32(entry, run.logical);
    put16(entry + 4, std::uint16_t(run.length > 32768 ? 32768 : run.length));
    put16(entry + 6, std::uint16_t(run.physical >> 32));
    put32(entry + 8, std::uint32_t(run.physical & 0xFFFFFFFFu));
}

void putIndexEntry(std::uint8_t *entry, std::uint32_t logical, std::uint64_t block)
{
    put32(entry, logical);
    put32(entry + 4, std::uint32_t(block & 0xFFFFFFFFu));
    put16(entry + 8, std::uint16_t(block >> 32));
    put16(entry + 10, 0);
}

// The inline header plus up to four extents fit the inode's 60 i_block bytes.
void buildInlineExtents(std::uint8_t *block, const std::vector<Run> &runs, std::uint16_t depth,
                        std::uint64_t leaf)
{
    std::memset(block, 0, 60);
    put16(block, 0xF30A);
    put16(block + 4, 4);
    put16(block + 6, depth);
    put32(block + 8, 0);
    if (depth == 0) {
        put16(block + 2, std::uint16_t(runs.size()));
        for (std::size_t i = 0; i < runs.size() && i < 4; ++i)
            putExtentEntry(block + 12 + i * 12, runs[i]);
    } else {
        put16(block + 2, 1);
        putIndexEntry(block + 12, runs.empty() ? 0 : runs.front().logical, leaf);
    }
}

// Writes the fake last record and the block checksum a metadata_csum
// directory block needs.
void sealDirBlock(std::vector<std::uint8_t> *block, std::uint32_t seed, bool csum)
{
    if (!csum) return;
    std::uint8_t *tail = block->data() + block->size() - 12;
    put32(tail, 0);
    put16(tail + 4, 12);
    tail[6] = 0;
    tail[7] = 0xDE;
    put32(tail + 8, crc32cImpl(seed, block->data(), block->size() - 12));
}

// A leaf block: header, the extents, then the checksum in the last four bytes.
std::uint32_t leafCapacity(std::uint32_t blockSize, bool csum)
{
    return (blockSize - 12 - (csum ? 4 : 0)) / 12;
}

} // namespace

// ---------------------------------------------------------------------------
// superblock / counters
// ---------------------------------------------------------------------------

bool Volume::writeTimes(bool)
{
    put32(m_super.data() + 0x30, now());   // s_wtime
    if (m_hasCsum) put32(m_super.data() + 0x3FC, crc32cImpl(~0u, m_super.data(), 0x3FC));
    return m_image->write(m_offset + 1024, m_super.data(), 1024);
}

static void bumpSuperFree(std::vector<std::uint8_t> &super, int blocksDelta, int inodesDelta)
{
    const std::int64_t freeBlocks = std::int64_t(le32(super.data() + 0x0C)) + blocksDelta;
    put32(super.data() + 0x0C, std::uint32_t(freeBlocks < 0 ? 0 : freeBlocks));
    const std::int64_t freeInodes = std::int64_t(le32(super.data() + 0x10)) + inodesDelta;
    put32(super.data() + 0x10, std::uint32_t(freeInodes < 0 ? 0 : freeInodes));
}

// ---------------------------------------------------------------------------
// allocation
// ---------------------------------------------------------------------------

std::uint32_t Volume::allocateInode(std::string *error)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return 0;
    }
    std::vector<std::uint8_t> descriptor(m_descSize);
    std::vector<std::uint8_t> bitmap;
    for (std::uint32_t group = 0; group < m_groups; ++group) {
        if (!groupDescriptor(int(group), &descriptor)) continue;
        GroupInfo info = readGroup(descriptor);
        if (info.freeInodes == 0) continue;
        // The bitmap always occupies one block; only its first
        // inodesPerGroup/8 bytes are bits, and only those are checksummed.
        const std::uint32_t used = (m_inodesPerGroup + 7) / 8;
        bitmap.assign(m_blockSize, 0);
        if (!readBlocks(info.inodeBitmap, bitmap.data(), 1)) continue;
        for (std::uint32_t index = 0; index < m_inodesPerGroup; ++index) {
            const std::uint32_t inode = group * m_inodesPerGroup + index + 1;
            if (inode < m_firstIno || inode > m_inodes) continue;
            if (bitmap[index / 8] & (1u << (index % 8))) continue;
            bitmap[index / 8] = std::uint8_t(bitmap[index / 8] | (1u << (index % 8)));
            if (!writeBlocks(info.inodeBitmap, bitmap.data(), 1)) {
                if (error) *error = "The inode bitmap could not be written.";
                return 0;
            }
            if (m_hasCsum) {
                const std::uint32_t csum = crc32cImpl(crc(), bitmap.data(), used);
                put16(descriptor.data() + 0x1A, std::uint16_t(csum & 0xFFFF));
                if (m_descSize >= 64) put16(descriptor.data() + 0x3A, std::uint16_t(csum >> 16));
            }
            info.freeInodes = std::uint16_t(info.freeInodes - 1);
            info.itableUnused = 0;
            applyGroup(info, &descriptor);
            storeGroupDescriptor(int(group), &descriptor);
            bumpSuperFree(m_super, 0, -1);
            if (m_freeInodes) --m_freeInodes;
            writeTimes(false);

            std::vector<std::uint8_t> zero(m_inodeSize, 0);
            m_image->write(m_offset + inodeOffset(inode), zero.data(), zero.size());
            return inode;
        }
    }
    if (error) *error = "The filesystem has no free inode left.";
    return 0;
}

std::uint32_t Volume::allocateBlocks(std::uint32_t count, std::vector<std::uint64_t> *out,
                                     std::string *error)
{
    out->clear();
    if (count == 0) return 0;
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return 0;
    }
    std::uint32_t remaining = count;
    std::vector<std::uint8_t> descriptor(m_descSize);
    std::vector<std::uint8_t> bitmap;
    for (std::uint32_t group = 0; group < m_groups && remaining > 0; ++group) {
        if (!groupDescriptor(int(group), &descriptor)) continue;
        GroupInfo info = readGroup(descriptor);
        if (info.freeBlocks == 0) continue;
        const std::uint32_t used = (m_blocksPerGroup + 7) / 8;
        bitmap.assign(m_blockSize, 0);
        if (!readBlocks(info.blockBitmap, bitmap.data(), 1)) continue;
        bool touched = false;
        for (std::uint32_t index = 0; index < m_blocksPerGroup && remaining > 0; ++index) {
            const std::uint64_t block = std::uint64_t(m_firstDataBlock)
                + std::uint64_t(group) * m_blocksPerGroup + index;
            if (block >= m_blocks) break;
            if (bitmap[index / 8] & (1u << (index % 8))) continue;
            bitmap[index / 8] = std::uint8_t(bitmap[index / 8] | (1u << (index % 8)));
            out->push_back(block);
            --remaining;
            info.freeBlocks = std::uint16_t(info.freeBlocks - 1);
            touched = true;
        }
        if (!touched) continue;
        if (!writeBlocks(info.blockBitmap, bitmap.data(), 1)) {
            if (error) *error = "The block bitmap could not be written.";
            return 0;
        }
        if (m_hasCsum) {
            const std::uint32_t csum = crc32cImpl(crc(), bitmap.data(), used);
            put16(descriptor.data() + 0x18, std::uint16_t(csum & 0xFFFF));
            if (m_descSize >= 64) put16(descriptor.data() + 0x38, std::uint16_t(csum >> 16));
        }
        applyGroup(info, &descriptor);
        storeGroupDescriptor(int(group), &descriptor);
    }
    if (remaining > 0) {
        if (error) *error = "The filesystem has no free block left.";
        return 0;
    }
    bumpSuperFree(m_super, -int(count), 0);
    m_freeBlocks = m_freeBlocks >= count ? m_freeBlocks - count : 0;
    writeTimes(false);
    return count;
}

bool Volume::freeBlocks(std::vector<std::uint32_t> blocks, std::string *error)
{
    if (blocks.empty()) return true;
    std::sort(blocks.begin(), blocks.end());
    blocks.erase(std::unique(blocks.begin(), blocks.end()), blocks.end());
    std::vector<std::uint8_t> descriptor(m_descSize);
    std::vector<std::uint8_t> bitmap;
    int freed = 0;
    std::size_t index = 0;
    while (index < blocks.size()) {
        const std::uint64_t block = blocks[index];
        if (block < m_firstDataBlock || block >= m_blocks) {
            ++index;
            continue;
        }
        const std::uint32_t group = std::uint32_t((block - m_firstDataBlock) / m_blocksPerGroup);
        if (!groupDescriptor(int(group), &descriptor)) {
            if (error) *error = "A group descriptor could not be read.";
            return false;
        }
        GroupInfo info = readGroup(descriptor);
        const std::uint32_t used = (m_blocksPerGroup + 7) / 8;
        bitmap.assign(m_blockSize, 0);
        if (!readBlocks(info.blockBitmap, bitmap.data(), 1)) {
            if (error) *error = "A block bitmap could not be read.";
            return false;
        }
        while (index < blocks.size()) {
            const std::uint64_t next = blocks[index];
            if (next < m_firstDataBlock || next >= m_blocks) {
                ++index;
                continue;
            }
            if (std::uint32_t((next - m_firstDataBlock) / m_blocksPerGroup) != group) break;
            const std::uint32_t bit = std::uint32_t((next - m_firstDataBlock) % m_blocksPerGroup);
            if (bitmap[bit / 8] & (1u << (bit % 8))) {
                bitmap[bit / 8] = std::uint8_t(bitmap[bit / 8] & ~(1u << (bit % 8)));
                info.freeBlocks = std::uint16_t(info.freeBlocks + 1);
                ++freed;
            }
            ++index;
        }
        if (!writeBlocks(info.blockBitmap, bitmap.data(), 1)) {
            if (error) *error = "A block bitmap could not be written.";
            return false;
        }
        if (m_hasCsum) {
            const std::uint32_t csum = crc32cImpl(crc(), bitmap.data(), used);
            put16(descriptor.data() + 0x18, std::uint16_t(csum & 0xFFFF));
            if (m_descSize >= 64) put16(descriptor.data() + 0x38, std::uint16_t(csum >> 16));
        }
        applyGroup(info, &descriptor);
        storeGroupDescriptor(int(group), &descriptor);
    }
    if (freed) {
        bumpSuperFree(m_super, freed, 0);
        m_freeBlocks += std::uint64_t(freed);
        writeTimes(false);
    }
    return true;
}

bool Volume::freeInode(std::uint32_t inode, std::string *error)
{
    if (inode < m_firstIno) {
        if (error) *error = "A reserved inode is never freed.";
        return false;
    }
    const std::uint32_t group = (inode - 1) / m_inodesPerGroup;
    const std::uint32_t index = (inode - 1) % m_inodesPerGroup;
    std::vector<std::uint8_t> descriptor(m_descSize);
    if (!groupDescriptor(int(group), &descriptor)) return false;
    GroupInfo info = readGroup(descriptor);
    const std::uint32_t used = (m_inodesPerGroup + 7) / 8;
    std::vector<std::uint8_t> bitmap(m_blockSize, 0);
    if (!readBlocks(info.inodeBitmap, bitmap.data(), 1)) return false;
    bitmap[index / 8] = std::uint8_t(bitmap[index / 8] & ~(1u << (index % 8)));
    if (!writeBlocks(info.inodeBitmap, bitmap.data(), 1)) return false;
    if (m_hasCsum) {
        const std::uint32_t csum = crc32cImpl(crc(), bitmap.data(), used);
        put16(descriptor.data() + 0x1A, std::uint16_t(csum & 0xFFFF));
        if (m_descSize >= 64) put16(descriptor.data() + 0x3A, std::uint16_t(csum >> 16));
    }
    info.freeInodes = std::uint16_t(info.freeInodes + 1);
    applyGroup(info, &descriptor);
    storeGroupDescriptor(int(group), &descriptor);
    bumpSuperFree(m_super, 0, 1);
    ++m_freeInodes;

    // A freed inode is zeroed and then checksummed like any other: a zero
    // checksum on an inode that is not in use would still be reported.
    std::vector<std::uint8_t> zero(m_inodeSize, 0);
    put32(zero.data() + 0x14, now());   // dtime
    storeInode(inode, &zero, nullptr);
    writeTimes(false);
    return true;
}

// ---------------------------------------------------------------------------
// inodes
// ---------------------------------------------------------------------------

bool Volume::storeInode(std::uint32_t inode, std::vector<std::uint8_t> *raw, std::string *error)
{
    if (raw->size() != m_inodeSize) raw->resize(m_inodeSize, 0);
    if (m_hasCsum) {
        const std::uint32_t csum = inodeChecksum(inode, *raw);
        put16(raw->data() + 0x7C, std::uint16_t(csum & 0xFFFF));
        if (m_inodeSize > 128) {
            const std::uint16_t extra = le16(raw->data() + 0x80);
            if (extra >= 4) put16(raw->data() + 0x82, std::uint16_t(csum >> 16));
        }
    }
    const std::uint64_t at = inodeOffset(inode);
    if (at == 0 || !m_image->write(m_offset + at, raw->data(), raw->size())) {
        if (error) *error = "The inode could not be written.";
        return false;
    }
    return true;
}

// Every block an inode owns: its data blocks plus the leaf blocks of its
// extent tree.
std::vector<std::uint32_t> Volume::ownedBlocks(std::uint32_t inode, std::string *error) const
{
    std::vector<std::uint8_t> raw;
    if (!loadInode(inode, &raw, error)) return {};
    std::vector<std::uint32_t> blocks;
    for (std::uint64_t block : extentMap(inode, raw, error)) blocks.push_back(std::uint32_t(block));
    if (!(le32(raw.data() + 0x20) & kExtentsFlag)) return blocks;

    const std::uint8_t *root = raw.data() + 0x28;
    if (le16(root) != 0xF30A) return blocks;
    std::vector<std::uint64_t> pending;
    if (le16(root + 6) > 0) {
        const std::uint16_t entries = le16(root + 2);
        for (std::uint16_t i = 0; i < entries; ++i) {
            const std::uint8_t *entry = root + 12 + std::size_t(i) * 12;
            pending.push_back((std::uint64_t(le16(entry + 8)) << 32) | le32(entry + 4));
        }
    }
    int guard = 0;
    while (!pending.empty() && ++guard < 100000) {
        const std::uint64_t block = pending.back();
        pending.pop_back();
        blocks.push_back(std::uint32_t(block));
        std::vector<std::uint8_t> node(m_blockSize);
        if (!m_image->read(m_offset + block * m_blockSize, node.data(), m_blockSize)) continue;
        if (le16(node.data()) != 0xF30A || le16(node.data() + 6) == 0) continue;
        const std::uint16_t entries = le16(node.data() + 2);
        for (std::uint16_t i = 0; i < entries; ++i) {
            const std::uint8_t *entry = node.data() + 12 + std::size_t(i) * 12;
            pending.push_back((std::uint64_t(le16(entry + 8)) << 32) | le32(entry + 4));
        }
    }
    return blocks;
}

bool Volume::putFile(std::uint32_t dirInode, const std::string &name,
                     const std::vector<std::uint8_t> &data, std::uint16_t mode, std::string *error,
                     std::uint32_t uid, std::uint32_t gid, std::uint32_t mtime)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return false;
    }
    if (name.empty() || name == "." || name == ".." || name.size() > 255
        || name.find('/') != std::string::npos) {
        if (error) *error = "That is not a file name.";
        return false;
    }

    std::uint32_t inode = 0;
    std::uint16_t existingMode = 0;
    std::uint16_t existingLinks = 0;
    bool existingFile = false;
    {
        const Listing listing = list(dirInode, nullptr);
        for (const auto &entry : listing.entries)
            if (entry.name == name) {
                inode = entry.inode;
                break;
            }
    }
    if (inode) {
        existingFile = true;
        const Stat info = stat(inode, nullptr);
        if (!info.regular()) {
            if (error) *error = "“" + name + "” is not a regular file.";
            return false;
        }
        existingMode = std::uint16_t(info.mode & 0x0FFF);
        existingLinks = std::uint16_t(info.links);
        // The old content goes first, so its blocks are reusable. For a
        // hard-linked file this intentionally changes the shared inode's
        // contents, just like writing through either hard link would.
        if (!freeBlocks(ownedBlocks(inode, error), error)) return false;
    }

    const std::uint32_t blockCount = std::uint32_t((data.size() + m_blockSize - 1) / m_blockSize);
    std::vector<std::uint64_t> blocks;
    if (blockCount && !allocateBlocks(blockCount, &blocks, error)) return false;
    std::vector<std::uint32_t> physical;
    physical.reserve(blocks.size());
    for (auto block : blocks) physical.push_back(std::uint32_t(block));

    std::vector<std::uint8_t> buffer(m_blockSize, 0);
    for (std::uint32_t i = 0; i < blockCount; ++i) {
        std::fill(buffer.begin(), buffer.end(), 0);
        const std::size_t done = std::size_t(i) * m_blockSize;
        const std::size_t chunk = std::min<std::size_t>(m_blockSize, data.size() - done);
        if (chunk) std::memcpy(buffer.data(), data.data() + done, chunk);
        if (!writeBlocks(blocks[i], buffer.data(), 1)) {
            if (error) *error = "A data block could not be written.";
            return false;
        }
    }
    writeTimes(false);

    std::vector<Run> runs = coalesce(physical);
    std::uint64_t treeBlocks = 0;

    std::vector<std::uint8_t> raw;
    if (inode) {
        if (!loadInode(inode, &raw, error)) return false;
    } else {
        inode = allocateInode(error);
        if (inode == 0) return false;
        raw.assign(m_inodeSize, 0);
    }

    if (runs.size() <= 4) {
        buildInlineExtents(raw.data() + 0x28, runs, 0, 0);
    } else {
        const std::uint32_t capacity = leafCapacity(m_blockSize, m_hasCsum);
        if (runs.size() > capacity) {
            if (error) *error = "The file is too fragmented for this image's block size.";
            return false;
        }
        std::vector<std::uint64_t> leaf;
        if (!allocateBlocks(1, &leaf, error)) return false;
        std::vector<std::uint8_t> node(m_blockSize, 0);
        put16(node.data(), 0xF30A);
        put16(node.data() + 2, std::uint16_t(runs.size()));
        put16(node.data() + 4, std::uint16_t(capacity));
        put16(node.data() + 6, 0);
        put32(node.data() + 8, 0);
        for (std::size_t i = 0; i < runs.size(); ++i)
            putExtentEntry(node.data() + 12 + i * 12, runs[i]);
        if (m_hasCsum) {
            const std::uint32_t generation = le32(raw.data() + 0x64);
            put32(node.data() + m_blockSize - 4,
                  crc32cImpl(inodeSeed(inode, generation), node.data(), m_blockSize - 4));
        }
        if (!writeBlocks(leaf[0], node.data(), 1)) {
            if (error) *error = "The extent block could not be written.";
            return false;
        }
        buildInlineExtents(raw.data() + 0x28, runs, 1, leaf[0]);
        treeBlocks = 1;
    }

    // A replacement keeps the file's own permissions; a new file gets the
    // requested ones, or 0644.
    std::uint16_t permissions = existingMode;
    if (!inode || permissions == 0) permissions = std::uint16_t(mode ? (mode & 0x0FFF) : 0644);
    put16(raw.data() + 0x00, std::uint16_t(0x8000 | permissions));
    if (!existingFile) {
        put16(raw.data() + 0x02, std::uint16_t(uid & 0xFFFF));
        put16(raw.data() + 0x18, std::uint16_t(gid & 0xFFFF));
        put16(raw.data() + 0x78, std::uint16_t(uid >> 16));
        put16(raw.data() + 0x7A, std::uint16_t(gid >> 16));
    }
    put16(raw.data() + 0x1A, existingLinks ? existingLinks : 1);
    put32(raw.data() + 0x04, std::uint32_t(data.size() & 0xFFFFFFFFu));
    put32(raw.data() + 0x6C, std::uint32_t(std::uint64_t(data.size()) >> 32));
    const std::uint64_t sectors = (std::uint64_t(blockCount) + treeBlocks) * (m_blockSize / 512);
    put32(raw.data() + 0x1C, std::uint32_t(sectors & 0xFFFFFFFFu));
    put16(raw.data() + 0x74, std::uint16_t(sectors >> 32));
    put32(raw.data() + 0x20, kExtentsFlag);
    const std::uint32_t stamp = now();
    put32(raw.data() + 0x0C, stamp);
    put32(raw.data() + 0x10, mtime && !existingFile ? mtime : stamp);
    put32(raw.data() + 0x14, 0);
    if (m_inodeSize > 128) {
        if (le16(raw.data() + 0x80) == 0)
            put16(raw.data() + 0x80, std::uint16_t(std::min<std::uint32_t>(32, m_inodeSize - 128)));
        put32(raw.data() + 0x90, stamp);
    }
    if (!storeInode(inode, &raw, error)) return false;

    const Listing listing = list(dirInode, nullptr);
    for (const auto &entry : listing.entries)
        if (entry.name == name && entry.inode == inode) return true;   // replaced in place

    std::vector<std::uint8_t> dirRaw;
    if (!loadInode(dirInode, &dirRaw, error)) return false;
    DirEntry entry;
    entry.name = name;
    entry.inode = inode;
    entry.type = 1;
    return addDirent(dirInode, &dirRaw, entry, error);
}

bool Volume::putSymlink(std::uint32_t dirInode, const std::string &name,
                        const std::vector<std::uint8_t> &target, std::string *error,
                        std::uint32_t uid, std::uint32_t gid, std::uint32_t mtime)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return false;
    }
    if (name.empty() || name == "." || name == ".." || name.size() > 255
        || name.find('/') != std::string::npos || target.empty() || target.size() > 4095
        || std::find(target.begin(), target.end(), std::uint8_t(0)) != target.end()) {
        if (error) *error = "That symbolic link name or target is not valid.";
        return false;
    }
    const Listing current = list(dirInode, error);
    if (current.inode == 0) return false;
    for (const auto &entry : current.entries)
        if (entry.name == name) {
            if (error) *error = "“" + name + "” is already there.";
            return false;
        }

    if (target.size() > 60) {
        // Long symlink targets use ordinary extents; create the data blocks
        // through the already-tested file writer, then mark the inode/link.
        if (!putFile(dirInode, name, target, 0777, error, uid, gid, mtime)) return false;
        const Listing after = list(dirInode, error);
        std::uint32_t inode = 0;
        for (const auto &entry : after.entries)
            if (entry.name == name) { inode = entry.inode; break; }
        if (inode == 0) {
            if (error) *error = "The symbolic link inode could not be reopened.";
            return false;
        }
        std::vector<std::uint8_t> raw;
        if (!loadInode(inode, &raw, error)) return false;
        put16(raw.data() + 0x00, std::uint16_t(0xA000 | 0777));
        if (!storeInode(inode, &raw, error)) return false;
        return changeDirentType(dirInode, name, 7, error);
    }

    const std::uint32_t inode = allocateInode(error);
    if (inode == 0) return false;
    std::vector<std::uint8_t> raw(m_inodeSize, 0);
    put16(raw.data() + 0x00, std::uint16_t(0xA000 | 0777));
    put16(raw.data() + 0x02, std::uint16_t(uid & 0xFFFF));
    put16(raw.data() + 0x18, std::uint16_t(gid & 0xFFFF));
    put16(raw.data() + 0x78, std::uint16_t(uid >> 16));
    put16(raw.data() + 0x7A, std::uint16_t(gid >> 16));
    put16(raw.data() + 0x1A, 1);
    put32(raw.data() + 0x04, std::uint32_t(target.size()));
    std::memcpy(raw.data() + 0x28, target.data(), target.size());
    const std::uint32_t stamp = now();
    put32(raw.data() + 0x08, mtime ? mtime : stamp);
    put32(raw.data() + 0x0C, stamp);
    put32(raw.data() + 0x10, mtime ? mtime : stamp);
    if (m_inodeSize > 128) {
        put16(raw.data() + 0x80, std::uint16_t(std::min<std::uint32_t>(32, m_inodeSize - 128)));
        put32(raw.data() + 0x90, stamp);
    }
    if (!storeInode(inode, &raw, error)) return false;

    std::vector<std::uint8_t> dirRaw;
    if (!loadInode(dirInode, &dirRaw, error)) return false;
    DirEntry entry;
    entry.name = name;
    entry.inode = inode;
    entry.type = 7;
    return addDirent(dirInode, &dirRaw, entry, error);
}

bool Volume::makeDir(std::uint32_t dirInode, const std::string &name, std::uint16_t mode,
                     std::string *error, std::uint32_t uid, std::uint32_t gid,
                     std::uint32_t mtime)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return false;
    }
    if (name.empty() || name == "." || name == ".." || name.size() > 255
        || name.find('/') != std::string::npos) {
        if (error) *error = "That is not a folder name.";
        return false;
    }
    const Listing parent = list(dirInode, nullptr);
    for (const auto &entry : parent.entries)
        if (entry.name == name) {
            if (error) *error = "“" + name + "” is already there.";
            return false;
        }

    const std::uint32_t inode = allocateInode(error);
    if (inode == 0) return false;
    std::vector<std::uint64_t> blocks;
    if (!allocateBlocks(1, &blocks, error)) return false;

    std::vector<std::uint8_t> block(m_blockSize, 0);
    const std::uint32_t generation = 0;
    const std::uint32_t limit = m_blockSize - (m_hasCsum ? 12 : 0);
    put32(block.data() + 0, inode);
    put16(block.data() + 4, 12);
    block[6] = 1;
    block[7] = 2;
    block[8] = '.';
    put32(block.data() + 12, dirInode);
    put16(block.data() + 16, std::uint16_t(limit - 12));
    block[18] = 2;
    block[19] = 2;
    block[20] = '.';
    block[21] = '.';
    sealDirBlock(&block, inodeSeed(inode, generation), m_hasCsum);
    if (!writeBlocks(blocks[0], block.data(), 1)) {
        if (error) *error = "The folder's first block could not be written.";
        return false;
    }

    std::vector<std::uint8_t> raw(m_inodeSize, 0);
    const std::vector<Run> runs { { 0, 1, blocks[0] } };
    buildInlineExtents(raw.data() + 0x28, runs, 0, 0);
    put16(raw.data() + 0x00, std::uint16_t(kInodeDir | (mode ? (mode & 0x0FFF) : 0755)));
    put16(raw.data() + 0x02, std::uint16_t(uid & 0xFFFF));
    put16(raw.data() + 0x18, std::uint16_t(gid & 0xFFFF));
    put16(raw.data() + 0x78, std::uint16_t(uid >> 16));
    put16(raw.data() + 0x7A, std::uint16_t(gid >> 16));
    put16(raw.data() + 0x1A, 2);
    put32(raw.data() + 0x04, m_blockSize);
    put32(raw.data() + 0x1C, m_blockSize / 512);
    put32(raw.data() + 0x20, kExtentsFlag);
    const std::uint32_t stamp = now();
    put32(raw.data() + 0x0C, stamp);
    put32(raw.data() + 0x10, mtime ? mtime : stamp);
    if (m_inodeSize > 128) {
        put16(raw.data() + 0x80, std::uint16_t(std::min<std::uint32_t>(32, m_inodeSize - 128)));
        put32(raw.data() + 0x90, stamp);
    }
    if (!storeInode(inode, &raw, error)) return false;

    std::vector<std::uint8_t> parentRaw;
    if (!loadInode(dirInode, &parentRaw, error)) return false;
    DirEntry entry;
    entry.name = name;
    entry.inode = inode;
    entry.type = 2;
    if (!addDirent(dirInode, &parentRaw, entry, error)) return false;

    put16(parentRaw.data() + 0x1A, std::uint16_t(le16(parentRaw.data() + 0x1A) + 1));
    storeInode(dirInode, &parentRaw, nullptr);

    const std::uint32_t group = (inode - 1) / m_inodesPerGroup;
    std::vector<std::uint8_t> descriptor(m_descSize);
    if (groupDescriptor(int(group), &descriptor)) {
        GroupInfo info = readGroup(descriptor);
        info.usedDirs = std::uint16_t(info.usedDirs + 1);
        applyGroup(info, &descriptor);
        storeGroupDescriptor(int(group), &descriptor);
    }
    return true;
}

// ---------------------------------------------------------------------------
// directory entries
// ---------------------------------------------------------------------------


bool Volume::addDirent(std::uint32_t dirInode, std::vector<std::uint8_t> *dirRaw,
                       const DirEntry &entry, std::string *error)
{
    const Stat info = stat(dirInode, nullptr);
    const bool indexed = (info.flags & kIndexFlag) != 0;
    if (indexed) {
        if (error)
            *error = "This folder uses ext4 hash indexing. Adding or renaming entries there is "
                     "disabled to protect the image; use a non-indexed folder instead.";
        return false;
    }
    const std::uint32_t generation = dirRaw->size() >= 0x68 ? le32(dirRaw->data() + 0x64) : 0;
    const std::uint32_t seed = inodeSeed(dirInode, generation);
    const std::uint32_t limit = m_blockSize - (m_hasCsum ? 12 : 0);
    const std::uint32_t aligned = (8 + std::uint32_t(entry.name.size()) + 3) & ~3u;

    std::vector<std::uint32_t> blocks = fileBlocks(dirInode, error);
    std::vector<std::uint8_t> buffer(m_blockSize);

    for (std::size_t index = blocks.size(); index-- > 0;) {
        if (indexed && index == 0) continue;
        if (!readBlocks(blocks[index], buffer.data(), 1)) continue;
        std::uint32_t offset = 0;
        while (offset + 8 <= limit) {
            std::uint8_t *record = buffer.data() + offset;
            const std::uint32_t recordInode = le32(record);
            const std::uint16_t recordLength = le16(record + 4);
            if (recordLength < 8 || offset + recordLength > limit) break;
            const std::uint32_t used = recordInode == 0
                ? 0
                : (8 + std::uint32_t(record[6]) + 3) & ~3u;
            if (recordLength >= used + aligned) {
                std::uint8_t *slot = record + used;
                if (recordInode != 0) put16(record + 4, std::uint16_t(used));
                put32(slot, entry.inode);
                put16(slot + 4, std::uint16_t(recordLength - used));
                slot[6] = std::uint8_t(entry.name.size());
                slot[7] = entry.type;
                std::memcpy(slot + 8, entry.name.data(), entry.name.size());
                sealDirBlock(&buffer, seed, m_hasCsum);
                if (!writeBlocks(blocks[index], buffer.data(), 1)) {
                    if (error) *error = "A directory block could not be written.";
                    return false;
                }
                return true;
            }
            offset += recordLength;
        }
    }

    if (indexed) {
        if (error)
            *error = "This folder's entries are indexed and its last block is full; adding here "
                     "would need the hash tree rebuilt, which this editor does not do. Pick "
                     "another folder.";
        return false;
    }
    std::vector<std::uint64_t> added;
    if (!allocateBlocks(1, &added, error)) return false;
    std::vector<std::uint8_t> fresh(m_blockSize, 0);
    put32(fresh.data(), entry.inode);
    put16(fresh.data() + 4, std::uint16_t(limit));
    fresh[6] = std::uint8_t(entry.name.size());
    fresh[7] = entry.type;
    std::memcpy(fresh.data() + 8, entry.name.data(), entry.name.size());
    sealDirBlock(&fresh, seed, m_hasCsum);
    if (!writeBlocks(added[0], fresh.data(), 1)) {
        if (error) *error = "The directory's new block could not be written.";
        return false;
    }

    std::vector<std::uint32_t> physical;
    physical.reserve(blocks.size() + 1);
    for (std::uint32_t block : blocks) physical.push_back(block);
    physical.push_back(std::uint32_t(added[0]));
    const std::vector<Run> runs = coalesce(physical);
    if (runs.size() > 4) {
        if (error) *error = "The folder is too fragmented to grow safely; use another folder.";
        return false;
    }
    buildInlineExtents(dirRaw->data() + 0x28, runs, 0, 0);
    const std::uint64_t size = std::uint64_t(blocks.size() + 1) * m_blockSize;
    put32(dirRaw->data() + 0x04, std::uint32_t(size & 0xFFFFFFFFu));
    put32(dirRaw->data() + 0x6C, std::uint32_t(size >> 32));
    const std::uint64_t sectors = size / 512;
    put32(dirRaw->data() + 0x1C, std::uint32_t(sectors & 0xFFFFFFFFu));
    put16(dirRaw->data() + 0x74, std::uint16_t(sectors >> 32));
    put32(dirRaw->data() + 0x10, now());
    return storeInode(dirInode, dirRaw, error);
}

bool Volume::changeDirentType(std::uint32_t dirInode, const std::string &name,
                              std::uint8_t type, std::string *error)
{
    const Stat info = stat(dirInode, error);
    const bool indexed = (info.flags & kIndexFlag) != 0;
    const std::uint32_t generation = [&]() {
        std::vector<std::uint8_t> raw;
        return loadInode(dirInode, &raw, nullptr) && raw.size() >= 0x68
            ? le32(raw.data() + 0x64) : 0;
    }();
    const std::uint32_t seed = inodeSeed(dirInode, generation);
    const std::vector<std::uint32_t> blocks = fileBlocks(dirInode, error);
    std::vector<std::uint8_t> block(m_blockSize);
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (indexed && i == 0) continue;
        if (!readBlocks(blocks[i], block.data(), 1)) continue;
        for (std::uint32_t offset = 0; offset + 8 <= m_blockSize;) {
            std::uint8_t *entry = block.data() + offset;
            const std::uint16_t length = le16(entry + 4);
            if (length < 8 || offset + length > m_blockSize) break;
            const std::uint8_t nameLength = entry[6];
            if (le32(entry) && nameLength == name.size()
                && std::memcmp(entry + 8, name.data(), nameLength) == 0) {
                entry[7] = type;
                sealDirBlock(&block, seed, m_hasCsum);
                if (!writeBlocks(blocks[i], block.data(), 1)) {
                    if (error) *error = "The directory entry type could not be updated.";
                    return false;
                }
                return true;
            }
            offset += length;
        }
    }
    if (error) *error = "“" + name + "” was not found in that folder.";
    return false;
}

bool Volume::removeDirent(std::uint32_t dirInode, std::vector<std::uint8_t> *dirRaw,
                          const std::string &name, std::string *error)
{
    const Stat info = stat(dirInode, nullptr);
    const bool indexed = (info.flags & kIndexFlag) != 0;
    const std::uint32_t generation = dirRaw->size() >= 0x68 ? le32(dirRaw->data() + 0x64) : 0;
    const std::uint32_t seed = inodeSeed(dirInode, generation);
    std::vector<std::uint32_t> blocks = fileBlocks(dirInode, error);
    std::vector<std::uint8_t> buffer(m_blockSize);

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (indexed && index == 0) continue;
        if (!readBlocks(blocks[index], buffer.data(), 1)) continue;
        std::uint32_t offset = 0;
        std::uint32_t previous = 0xFFFFFFFFu;
        while (offset + 8 <= m_blockSize) {
            std::uint8_t *record = buffer.data() + offset;
            const std::uint32_t recordInode = le32(record);
            const std::uint16_t recordLength = le16(record + 4);
            const std::uint8_t nameLength = record[6];
            if (recordLength < 8 || offset + recordLength > m_blockSize) break;
            if (recordInode != 0 && nameLength == name.size()
                && std::memcmp(record + 8, name.data(), nameLength) == 0) {
                if (previous != 0xFFFFFFFFu) {
                    std::uint8_t *before = buffer.data() + previous;
                    put16(before + 4, std::uint16_t(le16(before + 4) + recordLength));
                } else {
                    put32(record, 0);
                }
                sealDirBlock(&buffer, seed, m_hasCsum);
                if (!writeBlocks(blocks[index], buffer.data(), 1)) {
                    if (error) *error = "A directory block could not be written.";
                    return false;
                }
                put32(dirRaw->data() + 0x10, now());
                return storeInode(dirInode, dirRaw, error);
            }
            previous = offset;
            offset += recordLength;
        }
    }
    if (error) *error = "“" + name + "” was not found in that folder.";
    return false;
}

bool Volume::rename(std::uint32_t dirInode, const std::string &oldName,
                    const std::string &newName, std::string *error)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return false;
    }
    if (newName.empty() || newName == "." || newName == ".." || newName.size() > 255
        || newName.find('/') != std::string::npos) {
        if (error) *error = "That is not a valid new name.";
        return false;
    }
    const Listing listing = list(dirInode, error);
    if (listing.inode == 0) return false;
    const DirEntry *source = nullptr;
    for (const auto &entry : listing.entries) {
        if (entry.name == oldName) source = &entry;
        if (entry.name == newName && oldName != newName) {
            if (error) *error = "“" + newName + "” is already in that folder.";
            return false;
        }
    }
    if (!source) {
        if (error) *error = "“" + oldName + "” was not found in that folder.";
        return false;
    }
    if (oldName == newName) return true;
    if (source->inode < m_firstIno) {
        if (error) *error = "A reserved inode is never renamed.";
        return false;
    }

    // Add first, then remove the old name. The inode and all its metadata stay
    // untouched; the caller's journal makes the two directory edits atomic.
    std::vector<std::uint8_t> dirRaw;
    if (!loadInode(dirInode, &dirRaw, error)) return false;
    DirEntry renamed = *source;
    renamed.name = newName;
    if (!addDirent(dirInode, &dirRaw, renamed, error)) return false;
    if (!loadInode(dirInode, &dirRaw, error)) return false;
    return removeDirent(dirInode, &dirRaw, oldName, error);
}

bool Volume::move(std::uint32_t sourceDir, const std::string &name,
                  std::uint32_t destinationDir, const std::string &newName,
                  std::string *error)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return false;
    }
    if (newName.empty() || newName == "." || newName == ".." || newName.size() > 255
        || newName.find('/') != std::string::npos) {
        if (error) *error = "That is not a valid destination name.";
        return false;
    }
    if (sourceDir == destinationDir) return rename(sourceDir, name, newName, error);

    const Listing sourceListing = list(sourceDir, error);
    const Listing destinationListing = list(destinationDir, error);
    if (sourceListing.inode == 0 || destinationListing.inode == 0) return false;
    const DirEntry *source = nullptr;
    for (const auto &entry : sourceListing.entries)
        if (entry.name == name) { source = &entry; break; }
    if (!source) {
        if (error) *error = "“" + name + "” was not found in the source folder.";
        return false;
    }
    for (const auto &entry : destinationListing.entries)
        if (entry.name == newName) {
            if (error) *error = "“" + newName + "” is already in the destination folder.";
            return false;
        }
    if (source->inode < m_firstIno) {
        if (error) *error = "A reserved inode is never moved.";
        return false;
    }

    std::uint16_t sourceParentLinks = 0;
    std::uint16_t destinationParentLinks = 0;
    if (source->dir) {
        std::vector<std::uint8_t> sourceParentRaw;
        std::vector<std::uint8_t> destinationParentRaw;
        if (!loadInode(sourceDir, &sourceParentRaw, error)
            || !loadInode(destinationDir, &destinationParentRaw, error))
            return false;
        sourceParentLinks = le16(sourceParentRaw.data() + 0x1A);
        destinationParentLinks = le16(destinationParentRaw.data() + 0x1A);
        if (sourceParentLinks <= 2 || destinationParentLinks >= 65000) {
            if (error) *error = "The source or destination folder's link count cannot be updated safely.";
            return false;
        }
        const Stat directory = stat(source->inode, error);
        if (directory.inode == 0) return false;
        if (directory.flags & kIndexFlag) {
            if (error) *error = "This folder uses ext4 hash indexing; moving it is disabled to "
                                 "protect its directory checksum and index.";
            return false;
        }

        // A directory cannot be moved into itself or one of its descendants.
        // Follow each destination's on-disk ".." entry up to the root.
        std::uint32_t ancestor = destinationDir;
        bool reachedRoot = false;
        for (std::uint32_t depth = 0; depth <= m_inodes; ++depth) {
            if (ancestor == source->inode) {
                if (error) *error = "A folder cannot be moved inside itself.";
                return false;
            }
            if (ancestor == 2) {
                reachedRoot = true;
                break;
            }
            std::vector<std::uint32_t> blocks = fileBlocks(ancestor, error);
            if (blocks.empty()) {
                if (error && error->empty()) *error = "The destination's parent entry is unreadable.";
                return false;
            }
            std::vector<std::uint8_t> block(m_blockSize);
            if (!readBlocks(blocks.front(), block.data(), 1)) {
                if (error) *error = "The destination's parent entry could not be read.";
                return false;
            }
            std::uint32_t parent = 0;
            for (std::uint32_t offset = 0; offset + 8 <= m_blockSize;) {
                const std::uint8_t *entry = block.data() + offset;
                const std::uint16_t length = le16(entry + 4);
                const std::uint8_t nameLength = entry[6];
                if (length < 8 || offset + length > m_blockSize) break;
                if (le32(entry) && nameLength == 2 && entry[8] == '.' && entry[9] == '.') {
                    parent = le32(entry);
                    break;
                }
                offset += length;
            }
            if (parent == 0 || parent == ancestor) {
                if (error) *error = "The destination folder has an invalid parent chain.";
                return false;
            }
            ancestor = parent;
        }
        if (!reachedRoot) {
            if (error) *error = "The destination folder's parent chain exceeded the safe depth.";
            return false;
        }
    }

    DirEntry moved = *source;
    moved.name = newName;
    std::vector<std::uint8_t> destinationRaw;
    if (!loadInode(destinationDir, &destinationRaw, error)) return false;
    if (!addDirent(destinationDir, &destinationRaw, moved, error)) return false;

    std::vector<std::uint8_t> sourceRaw;
    if (!loadInode(sourceDir, &sourceRaw, error)) return false;
    if (!removeDirent(sourceDir, &sourceRaw, name, error)) return false;

    if (source->dir) {
        put16(sourceRaw.data() + 0x1A, std::uint16_t(sourceParentLinks - 1));
        if (!storeInode(sourceDir, &sourceRaw, error)) return false;

        if (!loadInode(destinationDir, &destinationRaw, error)) return false;
        put16(destinationRaw.data() + 0x1A, std::uint16_t(destinationParentLinks + 1));
        if (!storeInode(destinationDir, &destinationRaw, error)) return false;

        std::vector<std::uint8_t> movedRaw;
        if (!loadInode(source->inode, &movedRaw, error)) return false;
        const std::uint32_t generation = movedRaw.size() >= 0x68
            ? le32(movedRaw.data() + 0x64) : 0;
        const std::vector<std::uint32_t> blocks = fileBlocks(source->inode, error);
        if (blocks.empty()) {
            if (error && error->empty()) *error = "The moved folder has no data block.";
            return false;
        }
        std::vector<std::uint8_t> block(m_blockSize);
        if (!readBlocks(blocks.front(), block.data(), 1)) {
            if (error) *error = "The moved folder's parent entry could not be read.";
            return false;
        }
        bool updated = false;
        for (std::uint32_t offset = 0; offset + 8 <= m_blockSize;) {
            std::uint8_t *entry = block.data() + offset;
            const std::uint16_t length = le16(entry + 4);
            const std::uint8_t nameLength = entry[6];
            if (length < 8 || offset + length > m_blockSize) break;
            if (le32(entry) && nameLength == 2 && entry[8] == '.' && entry[9] == '.') {
                put32(entry, destinationDir);
                updated = true;
                break;
            }
            offset += length;
        }
        if (!updated) {
            if (error) *error = "The moved folder's '..' entry could not be found.";
            return false;
        }
        sealDirBlock(&block, inodeSeed(source->inode, generation), m_hasCsum);
        if (!writeBlocks(blocks.front(), block.data(), 1)) {
            if (error) *error = "The moved folder's parent entry could not be updated.";
            return false;
        }
        const std::uint32_t stamp = now();
        put32(movedRaw.data() + 0x0C, stamp);
        put32(movedRaw.data() + 0x10, stamp);
        if (!storeInode(source->inode, &movedRaw, error)) return false;
    }
    return true;
}

bool Volume::remove(std::uint32_t dirInode, const std::string &name, std::string *error)
{
    if (!m_writable) {
        if (error) *error = m_readOnlyReason.empty() ? "The image is read-only." : m_readOnlyReason;
        return false;
    }
    const Listing listing = list(dirInode, nullptr);
    std::uint32_t target = 0;
    bool isDir = false;
    for (const auto &entry : listing.entries)
        if (entry.name == name) {
            target = entry.inode;
            isDir = entry.dir;
            break;
        }
    if (target == 0) {
        if (error) *error = "“" + name + "” is not there.";
        return false;
    }
    if (target < m_firstIno) {
        if (error) *error = "A reserved inode is never removed.";
        return false;
    }

    if (isDir) {
        const Listing inside = list(target, error);
        if (inside.inode == 0) return false;
        for (const auto &child : inside.entries)
            if (!remove(target, child.name, error)) return false;
    } else {
        const Stat targetInfo = stat(target, error);
        if (targetInfo.inode == 0) return false;
        if (targetInfo.links > 1) {
            // Unlink only this name. Other hard links still reference the
            // inode and its data blocks; freeing either here would corrupt
            // every other path to the same file.
            std::vector<std::uint8_t> dirRaw;
            if (!loadInode(dirInode, &dirRaw, error)) return false;
            if (!removeDirent(dirInode, &dirRaw, name, error)) return false;
            std::vector<std::uint8_t> targetRaw;
            if (!loadInode(target, &targetRaw, error)) return false;
            put16(targetRaw.data() + 0x1A, std::uint16_t(targetInfo.links - 1));
            return storeInode(target, &targetRaw, error);
        }
    }

    if (!freeBlocks(ownedBlocks(target, error), error)) return false;

    std::vector<std::uint8_t> dirRaw;
    if (!loadInode(dirInode, &dirRaw, error)) return false;
    if (!removeDirent(dirInode, &dirRaw, name, error)) return false;
    if (isDir) {
        put16(dirRaw.data() + 0x1A, std::uint16_t(le16(dirRaw.data() + 0x1A) - 1));
        storeInode(dirInode, &dirRaw, nullptr);
        const std::uint32_t group = (target - 1) / m_inodesPerGroup;
        std::vector<std::uint8_t> descriptor(m_descSize);
        if (groupDescriptor(int(group), &descriptor)) {
            GroupInfo group_ = readGroup(descriptor);
            if (group_.usedDirs) group_.usedDirs = std::uint16_t(group_.usedDirs - 1);
            applyGroup(group_, &descriptor);
            storeGroupDescriptor(int(group), &descriptor);
        }
    }
    return freeInode(target, error);
}

} // namespace RootDisk
