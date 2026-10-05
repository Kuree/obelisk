#ifndef OBELISK_NATIVE_WORD_PREDICATION_H
#define OBELISK_NATIVE_WORD_PREDICATION_H
#include "obelisk/Analysis/BoundedLoopAnalysis.h"
namespace obelisk {
/// Transactional if-conversion on a disposable copy of a bounded loop.
/// The caller must subsequently prove bounds, privacy and lane independence
/// before accepting any of the speculated accesses or masked stores.
bool predicateNativeWordLoop(analysis::BoundedLoop loop);
} // namespace obelisk
#endif
