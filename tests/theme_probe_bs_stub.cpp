// Linux stub for BsDetector: no BlueStacks install or process exists in the
// probe environment. Keeps the theme probe linkable without the Windows
// process/install detection chain (FpsPatcher et al.).
#include "BsDetector.h"

namespace BsDetector {

QVariantList findBlueStacks(const QStringList &programFolders, bool includeRegistry)
{
    Q_UNUSED(programFolders)
    Q_UNUSED(includeRegistry)
    return {};
}

bool isProcessRunning(const QString &exeName)
{
    Q_UNUSED(exeName)
    return false;
}

} // namespace BsDetector
