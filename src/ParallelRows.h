// SPDX-License-Identifier: MIT
#pragma once
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace resolve_dlss5 {
namespace row_detail {
// A bounded private pool is shared by all effect instances. The caller also
// works; small images and nested work stay serial. Never allocate a host-sized
// set of threads for every frame or block the default Windows thread pool.
class Pool {
   public:
    PTP_POOL handle = nullptr;
    TP_CALLBACK_ENVIRON environment{};
    unsigned workers = 0;
    Pool() noexcept {
        const auto processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        workers = processors > 1 ? std::min(processors - 1, 7UL) : 0;
        if (!workers) return;
        handle = CreateThreadpool(nullptr);
        if (!handle) {
            workers = 0;
            return;
        }
        SetThreadpoolThreadMaximum(handle, workers);
        InitializeThreadpoolEnvironment(&environment);
        SetThreadpoolCallbackPool(&environment, handle);
    }
    ~Pool() {
        if (handle) {
            DestroyThreadpoolEnvironment(&environment);
            CloseThreadpool(handle);
        }
    }
};
inline thread_local bool active = false;
inline Pool& sharedPool() noexcept {
    static Pool pool;
    return pool;
}
}  // namespace row_detail

template <class F>
void parallelRows(int height, std::size_t pixels, F&& operation) {
    static_assert(noexcept(operation(0)), "Row callbacks must not throw");
    auto& pool = row_detail::sharedPool();
    if (pixels < 65536 || height < 2 || row_detail::active || !pool.workers) {
        for (int y = 0; y < height; ++y) operation(y);
        return;
    }
    struct Context {
        int height;
        std::atomic<int> next{0};
        std::remove_reference_t<F>& operation;
        void run() noexcept {
            const bool previous = row_detail::active;
            row_detail::active = true;
            for (;;) {
                const int begin = next.fetch_add(8, std::memory_order_relaxed);
                if (begin >= height) break;
                for (int y = begin; y < std::min(begin + 8, height); ++y) operation(y);
            }
            row_detail::active = previous;
        }
    } context{height, {}, operation};
    auto callback = [](PTP_CALLBACK_INSTANCE, void* argument, PTP_WORK) noexcept {
        static_cast<Context*>(argument)->run();
    };
    PTP_WORK work = CreateThreadpoolWork(callback, &context, &pool.environment);
    if (!work) {
        context.run();
        return;
    }
    for (unsigned i = 0; i < pool.workers; ++i) SubmitThreadpoolWork(work);
    context.run();
    WaitForThreadpoolWorkCallbacks(work, FALSE);
    CloseThreadpoolWork(work);
}
}  // namespace resolve_dlss5
