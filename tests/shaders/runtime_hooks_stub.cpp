// Routing-unit-test link seam only. No renderer simulation or fake game pixels.
#include "Runtime.h"
#ifdef _WIN32
namespace BsiShaders {
void startPresentationHooks(){}
void stopPresentationHooks(){}
}
#endif
