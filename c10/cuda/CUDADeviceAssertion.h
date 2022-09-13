#pragma once

#include <c10/cuda/CUDAException.h>
#include <c10/macros/Macros.h>

namespace c10 {
namespace cuda {

#ifdef TORCH_USE_CUDA_DSA
// Copy string from `src` to `dst`
static __device__ void dstrcpy(char* const dst, const char* const src) {
  int i = 0;
  // Copy string from source to destination, ensuring that it
  // isn't longer than `C10_CUDA_DEVICE_SIDE_MAX_STR_LEN-1`
  for (; src[i] != '\0' && i < C10_CUDA_DEVICE_SIDE_MAX_STR_LEN - 1; i++) {
    dst[i] = src[i];
  }
  // If the loop terminated before `i` meets the condition below, then
  // `dst` ends with a null-terminator from `src`. Otherwise, the loop
  // reached maximum iterations and we add our own null-terminator to
  // `dst`
  if (i == C10_CUDA_DEVICE_SIDE_MAX_STR_LEN - 1) {
    *dst = '\0';
  }
}

__device__ __noinline__ void dsa_add_new_assertion_failure(
    DeviceAssertionsData* assertions_data,
    const char* assertion_msg0,
    const char* filename0,
    const char* function_name0,
    const int line_number0,
    const uint32_t caller0,
    const dim3 block_id,
    const dim3 thread_id) {
  // `assertions_data` may be nullptr if device-side assertion checking
  // is disabled at run-time. If it is disabled at compile time this
  // function will never be called
  if (!assertions_data) {
    return;
  }

  // Atomically increment so other threads can fail at the same time
  const auto nid = atomicAdd(&(assertions_data->assertion_count), 1);

  if (nid > C10_CUDA_DEVICE_SIDE_ASSERTION_COUNT) {
    // At this point we're ran out of assertion buffer space
    // we could print a message about this, but that'd get
    // spammy if a lot of threads did it, so we just silently
    // ignore any other assertion failures. In most cases the
    // failures will all probably analogous anyway.

    // Problematically, if we return right away, then thousands
    // of threads will hit `__trap()` all at once before the
    // 10s of threads writing errors to UVM finish doing so. The
    // result is that we can't actually see error messages.
    // Therefore, we delay until those threads are done writing.
    while (assertions_data->assertion_failure_written <
           assertions_data->assertion_count) {
    }
    // Okay, all data is written. Let's start blowing things up.
    return;
  }

  auto& self = assertions_data->assertions[nid];
  dstrcpy(self.assertion_msg, assertion_msg0);
  dstrcpy(self.filename, filename0);
  dstrcpy(self.function_name, function_name0);
  self.line_number = line_number0;
  self.caller = caller0;
  self.block_id[0] = block_id.x;
  self.block_id[1] = block_id.y;
  self.block_id[2] = block_id.z;
  self.thread_id[0] = thread_id.x;
  self.thread_id[1] = thread_id.y;
  self.thread_id[2] = thread_id.z;

  // Atomically increment the number of errors written so
  // other threads can exit their spin locks
  atomicAdd(&(assertions_data->assertion_failure_written), 1);
}

// Emulates a kernel assertion. The assertion won't stop the kernel's progress,
// so you should assume everything the kernel produces is garbage if there's an
// assertion failure.
// NOTE: This assumes that `assertions_data` and  `assertion_caller_id` are
//       arguments of the kernel and therefore accessible.
// NOTE: `condition` is evaluated twice if the condition fails.
#define CUDA_KERNEL_ASSERT2(condition)                                     \
  do {                                                                     \
    if (C10_UNLIKELY(!(condition))) {                                      \
      /* Has an atomic element so threads can fail at the same time */     \
      c10::cuda::dsa_add_new_assertion_failure(                            \
          assertions_data,                                                 \
          C10_STRINGIZE(condition),                                        \
          __FILE__,                                                        \
          __FUNCTION__,                                                    \
          __LINE__,                                                        \
          assertion_caller_id,                                             \
          blockIdx,                                                        \
          threadIdx);                                                      \
                                                                           \
      /* We could evaluate `condition` a second time so CUDA prints a nice \
         message; however, if `condition` has side-effects this could be   \
         problematic. Instead, we just fail. */                            \
      __trap();                                                            \
    }                                                                      \
  } while (false)
#else
#define CUDA_KERNEL_ASSERT2(condition) assert(condition)
#endif

} // namespace cuda
} // namespace c10
