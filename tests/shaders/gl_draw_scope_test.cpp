// Production draw-lease policy without a window, driver or API double. The
// separate Windows regression executes nested originals through real MinHook.
#include "GlDrawScope.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
using namespace BsiShaders;
namespace {
void require(bool value,const char *message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
bool available(std::mutex &mutex){
    bool result=false;
    std::thread other([&]{result=mutex.try_lock();if(result)mutex.unlock();});
    other.join();return result;
}
void depthLimit(std::mutex &mutex,bool &recursing,unsigned level){
    GlDrawScope scope(mutex,recursing);
    require(scope.nested()==(level>1),"nested-call observation tracks the original callback stack, not applied draws");
    require(scope.mayInspect()==(level<=GlDrawScope::maximumDepth),"bypassed wrapper inspection has a fixed recursion bound");
    if(level<GlDrawScope::maximumDepth+3)depthLimit(mutex,recursing,level+1);
}
}
int main(){
    std::mutex mutex;bool recursing=false;
    {
        GlDrawScope outer(mutex,recursing);
        require(outer.mayInspect()&&!outer.nested()&&outer.tryLock(),"ordinary top-level draw can inspect");
        outer.guardInspection();
        require(recursing&&!available(mutex),"inspection itself remains guarded against driver-internal/reentrant GL work");
        // This is the former guard bug: the outer wrapper has no replacements,
        // but its original will set real scene state and issue an inner draw.
        outer.releaseBypass();
        require(!recursing&&!outer.ownsLock()&&available(mutex),"ZERO-binding wrapper must release its guard and render lock BEFORE the original, not only in its destructor");
        {
            GlDrawScope inner(mutex,recursing);
            require(inner.nested()&&inner.mayInspect()&&inner.tryLock(),"a later inner scene draw is not suppressed by a bypassed wrapper");
            inner.guardInspection();
            require(recursing&&!available(mutex),"qualified inner draw keeps the lease until its original finishes");
        }
        require(!recursing&&available(mutex),"inner restoration finishes even while the bypassed outer original is on the stack");
    }
    {
        GlDrawScope outer(mutex,recursing);require(outer.tryLock(),"take applied outer draw lease");outer.guardInspection();
        {
            GlDrawScope inner(mutex,recursing);
            require(inner.nested()&&!inner.mayInspect(),"a live private outer override cannot be overridden, restored early or double-counted by an inner callback");
        }
        require(recursing&&!available(mutex),"a skipped inner callback must not restore another scope's guard or lock");
    }
    require(!recursing&&available(mutex),"applied outer draw releases exactly once after its original");
    try {
        GlDrawScope scope(mutex,recursing);require(scope.tryLock(),"take exception-path lease");scope.guardInspection();
        scope.guardInspection(); // the failure handler must not capture its OWN true guard as the previous state
        throw std::runtime_error("test-owned diagnostic/inspection failure");
    }catch(const std::runtime_error&){}
    require(!recursing&&available(mutex),"an exception cannot leave the driver/presentation guard or mutex stuck");
    recursing=true;
    {GlDrawScope scope(mutex,recursing);require(!scope.mayInspect(),"presentation/inspection-internal draws stay excluded");}
    require(recursing,"a non-owning scope cannot clear the presentation guard");recursing=false;
    depthLimit(mutex,recursing,1);
    {GlDrawScope scope(mutex,recursing);require(scope.mayInspect()&&!scope.nested(),"recursion-limit bypasses unwind without stale nesting");}
    {
        GlDrawScope outer(mutex,recursing);
        bool independent=false;
        std::thread other([&]{bool busy=false;GlDrawScope scope(mutex,busy);independent=scope.mayInspect()&&!scope.nested();});
        other.join();require(independent,"nesting depth is thread-local, not another game thread's callback depth");
    }
    std::puts("PASS: production GL draw scope releases a zero-binding wrapper before its nested original, retains live private/exception/presentation guards, bounds recursion and isolates threads");
    std::puts("LIMIT: recursion/locking policy only, not game/driver or texture-pixel proof");
}
