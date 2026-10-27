#ifndef TALOS_NATIVE_WORD_PREDICATION_H
#define TALOS_NATIVE_WORD_PREDICATION_H
#include "talos/Analysis/BoundedLoopAnalysis.h"
namespace talos {
/// Transactional if-conversion on a disposable copy of a bounded loop.
/// The caller must subsequently prove bounds, privacy and lane independence
/// before accepting any of the speculated accesses or masked stores.
bool predicateNativeWordLoop(analysis::BoundedLoop loop);
} // namespace talos
#endif
