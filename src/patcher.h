#pragma once
#include "theme.h"
#include <vector>
#include <string>

// Exact same function signature from original binary (mangled: _ZL15PatchToggleSvgs...)
// PatchToggleSvgs(vector<uchar>, ThemeRes&, AutoPreset&, ulong, string, int)
bool PatchToggleSvgs(std::vector<uint8_t>& fileData, const ThemeRes& themeRes, const AutoPreset& preset, uint32_t flags, const std::string& context, int mode);

// Core patcher class replicating original logic
class BlueStacksPatcher {
public:
    BlueStacksPatcher();

    // Load HD-Player.exe - returns ThemeRes
    ThemeRes LoadFile(const std::string& path, std::string& outError);

    // Decompress theme - zlib 1.3.2, handles Qt qCompress (4-byte BE size + zlib)
    bool DecompressTheme(ThemeRes& res, std::string& outError);

    // Parse QML colors
    bool ParseColors(ThemeRes& res);

    // Build patched QML from edited colors
    std::string BuildPatchedQML(const ThemeRes& res, const std::map<std::string, std::string>& overrides);

    // Compress theme - returns compressed bytes
    bool CompressTheme(const std::string& qml, std::vector<uint8_t>& outCompressed, std::string& outError);

    // Patch file - in-place or relocated
    // Returns message: "Patch complete. Full theme applied." or "Patch complete. Full theme relocated and applied."
    bool PatchFile(const std::string& exePath, const ThemeRes& original, const std::vector<uint8_t>& newCompressed, const AutoPreset* preset, std::string& outMessage, std::string& outError);

    // Verify patched theme
    bool VerifyPatched(const std::string& exePath, const std::vector<uint8_t>& expectedCompressed, std::string& outError);

    // Backup / Restore
    bool CreateBackup(const std::string& exePath);
    bool RestoreBackup(const std::string& exePath, std::string& outMessage);

    // Preset handling
    std::vector<AutoPreset> LoadPresets();
    void ApplyPreset(ThemeRes& res, const AutoPreset& preset);

private:
    // Helpers
    size_t FindQmlMarker(const std::vector<uint8_t>& data);
    std::vector<uint8_t> ExtractCompressedBlock(const std::vector<uint8_t>& data, size_t offset);
    bool IsQtCompressed(const std::vector<uint8_t>& data);
    // Search for SVG toggle patterns
    void FindAndPatchSvg(std::vector<uint8_t>& fileData, const std::string& svgName, const std::string& newColor);
};
