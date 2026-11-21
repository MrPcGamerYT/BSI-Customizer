#pragma once
#include <mutex>

namespace BsiShaders {
// A bypassed translator/wrapper must not hide its later, state-setting native
// draw. Hold the recursion/mutex lease through the original ONLY while private
// bindings may be live (or while failing closed after an inspection exception).
// The caller must restore/forget its per-context pointer before releaseBypass().
class GlDrawScope {
public:
    static constexpr unsigned maximumDepth=8;
    GlDrawScope(std::mutex &mutex,bool &recursing)noexcept
        :level(++depth),lock(mutex,std::defer_lock),recursing(recursing){}
    ~GlDrawScope(){restore();--depth;}
    GlDrawScope(const GlDrawScope &)=delete;
    GlDrawScope &operator=(const GlDrawScope &)=delete;
    bool mayInspect()const noexcept{return !recursing&&level<=maximumDepth;}
    bool nested()const noexcept{return level>1;}
    bool ownsLock()const noexcept{return lock.owns_lock();}
    bool tryLock(){return lock.try_lock();}
    void guardInspection()noexcept{if(!guarding){previous=recursing;recursing=true;guarding=true;}}
    // Only on a normal result with ZERO replacement bindings. Never use this
    // to permit another override while the outer original still borrows ours.
    void releaseBypass()noexcept{restore();if(lock.owns_lock())lock.unlock();}
private:
    void restore()noexcept{if(guarding){recursing=previous;guarding=false;}}
    inline static thread_local unsigned depth=0;
    unsigned level;
    std::unique_lock<std::mutex> lock;
    bool &recursing;
    bool previous=false,guarding=false;
};
} // namespace BsiShaders
