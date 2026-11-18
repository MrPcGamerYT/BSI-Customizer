#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// PE data only. Never loads a DLL, executes target code or invokes a helper.
// A normal import descriptor with an empty thunk list makes the DLL a startup
// dependency without requiring/calling an exported function, including DLLs
// with no exports. Normal DLL_PROCESS_ATTACH is exercised by Windows tests.
namespace DllStartupImport
{
using Bytes = std::vector<std::uint8_t>;
struct Plan {
    bool ok = false;
    bool alreadyApplied = false;
    std::string error;
    Bytes output;
    std::vector<std::string> dependencies;
    std::size_t importsBefore = 0;
    std::size_t importsAfter = 0;
};
Plan prepare(const Bytes &exe, const Bytes &dll, const std::string &dllName);
bool validName(const std::string &name);
} // namespace DllStartupImport
