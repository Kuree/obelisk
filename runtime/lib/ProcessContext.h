//===- ProcessContext.h - Process runtime context locking -------*- C++ -*-===//

#ifndef OBELISK_RUNTIME_LIB_PROCESSCONTEXT_H
#define OBELISK_RUNTIME_LIB_PROCESSCONTEXT_H

#include <mutex>

struct obelisk_rt_context;

void initializeProcessContextLock(obelisk_rt_context *context,
                                  std::unique_lock<std::recursive_mutex> &lock);

class ContextMutexLock {
public:
  explicit ContextMutexLock(obelisk_rt_context *context) {
    initializeProcessContextLock(context, lock);
  }

private:
  std::unique_lock<std::recursive_mutex> lock;
};

#endif // OBELISK_RUNTIME_LIB_PROCESSCONTEXT_H
