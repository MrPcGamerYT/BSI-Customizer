#pragma once

// RootDisk — a byte-level view of a BlueStacks disk image (VHD, VHDX or raw)
// and of the ext4 filesystem inside it.
//
// This is the engine behind the hardcode debloat: it opens Root.vhd the way a
// 7-Zip window would, lists every folder and file, reads them out, and writes
// files, folders and deletions back in place.
//
// It is deliberately free of Qt so it can be compiled and exercised by itself,
// and it never writes to a volume it has not read and verified first: mounting
// validates the superblock, the group descriptors, the inode table and every
// checksum it relies on, and a volume with a dirty journal or a failed check
// is opened read-only.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace RootDisk {

// ---------------------------------------------------------------------------
// disk image
// ---------------------------------------------------------------------------

struct Image
{
    virtual ~Image() = default;
    virtual std::uint64_t size() const = 0;
    virtual bool read(std::uint64_t offset, void *buffer, std::size_t length) = 0;
    virtual bool write(std::uint64_t offset, const void *buffer, std::size_t length) = 0;
    virtual bool flush() = 0;
    // "fixed/dynamic VHD", "fixed/dynamic VHDX" or "raw image"
    virtual std::string kind() const = 0;
    virtual bool writable() const = 0;
};

// Opens fixed/dynamic VHD and VHDX images or a raw image. VHDX logs and
// differencing images are refused rather than interpreted incompletely.
std::unique_ptr<Image> openImage(const std::string &path, std::string *error);
#ifdef _WIN32
// Native UTF-16 overload for user-selected paths on Windows.
std::unique_ptr<Image> openImage(const std::wstring &path, std::string *error);
#endif

// ---------------------------------------------------------------------------
// ext4
// ---------------------------------------------------------------------------

struct Stat
{
    std::uint32_t inode = 0;
    std::uint16_t mode = 0;
    std::uint64_t size = 0;
    std::uint32_t links = 0;
    std::uint32_t blocks = 0;      // 512-byte sectors
    std::uint32_t uid = 0;
    std::uint32_t gid = 0;
    std::uint32_t mtime = 0;
    std::uint32_t flags = 0;
    bool dir() const { return (mode & 0xF000) == 0x4000; }
    bool regular() const { return (mode & 0xF000) == 0x8000; }
    bool symlink() const { return (mode & 0xF000) == 0xA000; }
};

struct DirEntry
{
    std::string name;
    std::uint32_t inode = 0;
    std::uint8_t type = 0;         // 1 regular, 2 dir, 7 symlink, 0 unknown
    bool dir = false;
};

// A directory as the file manager shows it, with the whole path resolved.
struct Listing
{
    std::uint32_t inode = 0;
    std::string path;
    std::vector<DirEntry> entries; // no "." or "..", sorted: folders, then files
    std::uint32_t parent = 0;      // 0 at the root
};

class Volume
{
public:
    // Probes the partition table (or a plain filesystem at offset 0) and mounts
    // what it finds. `error` explains a refusal in the user's terms.
    static std::unique_ptr<Volume> mount(std::unique_ptr<Image> image, std::string *error);

    ~Volume();
    Volume(const Volume &) = delete;
    Volume &operator=(const Volume &) = delete;

    std::uint64_t offset() const { return m_offset; }
    std::uint32_t blockSize() const { return m_blockSize; }
    std::uint64_t blockCount() const { return m_blocks; }
    std::uint64_t freeBlocks() const { return m_freeBlocks; }
    std::uint32_t inodeCount() const { return m_inodes; }
    std::uint32_t freeInodes() const { return m_freeInodes; }
    std::string features() const;
    // False when anything the engine relies on did not verify, when the
    // filesystem is marked dirty, or when the image itself is read-only.
    bool writable() const { return m_writable; }
    std::string readOnlyReason() const { return m_readOnlyReason; }

    Stat stat(std::uint32_t inode, std::string *error = nullptr) const;
    Listing list(const std::string &path, std::string *error);
    Listing list(std::uint32_t inode, std::string *error);

    // Resolves "system/bin/" to an inode; empty path is the root.
    std::uint32_t resolve(const std::string &path, std::string *error);

    // Reads a regular file or returns a symlink's target bytes.
    std::vector<std::uint8_t> readFile(std::uint32_t inode, std::string *error);

    // ---- writes ----
    // Creates a file, or replaces one that already exists (its blocks are
    // reallocated and its entry is kept, so links and permissions survive).
    bool putFile(std::uint32_t dirInode, const std::string &name,
                 const std::vector<std::uint8_t> &data, std::uint16_t mode, std::string *error,
                 std::uint32_t uid = 0, std::uint32_t gid = 0, std::uint32_t mtime = 0);
    bool putSymlink(std::uint32_t dirInode, const std::string &name,
                    const std::vector<std::uint8_t> &target, std::string *error,
                    std::uint32_t uid = 0, std::uint32_t gid = 0, std::uint32_t mtime = 0);
    bool makeDir(std::uint32_t dirInode, const std::string &name, std::uint16_t mode,
                 std::string *error, std::uint32_t uid = 0, std::uint32_t gid = 0,
                 std::uint32_t mtime = 0);
    // Renames one entry without changing its inode or file contents.
    bool rename(std::uint32_t dirInode, const std::string &oldName,
                const std::string &newName, std::string *error);
    // Moves one entry between directories without copying its inode or contents.
    bool move(std::uint32_t sourceDir, const std::string &name,
              std::uint32_t destinationDir, const std::string &newName,
              std::string *error);
    // Removes a file, or a folder and everything below it.
    bool remove(std::uint32_t dirInode, const std::string &name, std::string *error);

    bool flush() { return m_image->flush(); }

    // Which filesystem (or partition table) was found and where, for the UI.
    std::string description() const;

private:
    Volume() = default;

    bool readBlocks(std::uint64_t block, void *buffer, std::uint32_t count) const;
    bool writeBlocks(std::uint64_t block, const void *buffer, std::uint32_t count);

    bool loadInode(std::uint32_t inode, std::vector<std::uint8_t> *raw, std::string *error) const;
    bool storeInode(std::uint32_t inode, std::vector<std::uint8_t> *raw, std::string *error);

    std::vector<std::uint64_t> extentMap(std::uint32_t inode, const std::vector<std::uint8_t> &raw,
                                         std::string *error) const;
    std::vector<std::uint32_t> fileBlocks(std::uint32_t inode, std::string *error) const;
    std::vector<std::uint32_t> ownedBlocks(std::uint32_t inode, std::string *error) const;

    std::uint32_t allocateBlocks(std::uint32_t count, std::vector<std::uint64_t> *out, std::string *error);
    std::uint32_t allocateInode(std::string *error);
    bool freeBlocks(std::vector<std::uint32_t> blocks, std::string *error);
    bool freeInode(std::uint32_t inode, std::string *error);

    bool addDirent(std::uint32_t dirInode, std::vector<std::uint8_t> *dirRaw,
                   const DirEntry &entry, std::string *error);
    bool removeDirent(std::uint32_t dirInode, std::vector<std::uint8_t> *dirRaw,
                      const std::string &name, std::string *error);
    bool changeDirentType(std::uint32_t dirInode, const std::string &name,
                          std::uint8_t type, std::string *error);
    bool writeTimes(bool created);

    std::uint32_t crc() const;
    // crc(s_csum_seed, ino) then the inode's generation: the seed every inode,
    // directory block and extent block checksum starts from.
    std::uint32_t inodeSeed(std::uint32_t inode, std::uint32_t generation) const;
    std::uint16_t superChecksum() const;
    std::uint16_t groupChecksum(int group, const std::vector<std::uint8_t> &descriptor) const;
    std::uint32_t inodeChecksum(std::uint32_t inode, const std::vector<std::uint8_t> &raw) const;
    std::uint32_t blockChecksum(std::uint32_t inode, std::uint32_t generation,
                                const std::uint8_t *data, std::size_t length) const;

    std::uint64_t inodeOffset(std::uint32_t inode) const;
    bool groupDescriptor(int group, std::vector<std::uint8_t> *out) const;
    bool storeGroupDescriptor(int group, std::vector<std::uint8_t> *raw);

    std::unique_ptr<Image> m_image;
    std::uint64_t m_offset = 0;
    std::uint32_t m_blockSize = 0;
    std::uint32_t m_blocks = 0;
    std::uint32_t m_inodes = 0;
    std::uint32_t m_inodesPerGroup = 0;
    std::uint32_t m_blocksPerGroup = 0;
    std::uint32_t m_groups = 0;
    std::uint32_t m_firstDataBlock = 0;
    std::uint32_t m_inodeSize = 0;
    std::uint32_t m_descSize = 0;
    std::uint32_t m_firstIno = 11;
    std::uint32_t m_compat = 0;
    std::uint32_t m_incompat = 0;
    std::uint32_t m_roCompat = 0;
    std::uint32_t m_csumSeed = 0;
    std::uint32_t m_freeBlocks = 0;
    std::uint32_t m_freeInodes = 0;
    bool m_hasCsum = false;
    bool m_hasSeed = false;
    bool m_hasJournal = false;
    bool m_writable = false;
    bool m_superDirty = false;
    std::string m_readOnlyReason;
    std::vector<std::uint8_t> m_super; // the 1024-byte superblock, live copy
};

// The small helpers a caller needs to describe a path: joining and splitting.
std::string joinPath(const std::string &base, const std::string &name);
std::vector<std::string> splitPath(const std::string &path);

} // namespace RootDisk
