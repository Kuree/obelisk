//===- ProcessContext.h - Process runtime context locking -------*- C++ -*-===//

#ifndef TALOS_RUNTIME_LIB_PROCESSCONTEXT_H
#define TALOS_RUNTIME_LIB_PROCESSCONTEXT_H

#include <mutex>

struct talos_rt_context;

void initializeProcessContextLock(talos_rt_context *context,
                                  std::unique_lock<std::recursive_mutex> &lock);

class ContextMutexLock {
public:
  explicit ContextMutexLock(talos_rt_context *context) {
    initializeProcessContextLock(context, lock);
  }

private:
  std::unique_lock<std::recursive_mutex> lock;
};

#endif // TALOS_RUNTIME_LIB_PROCESSCONTEXT_H
