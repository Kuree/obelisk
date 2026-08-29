//===- ProtectedEnvelopeTestProvider.h - Test-only provider ----*- C++ -*-===//

#ifndef OBELISK_TEST_SUPPORT_PROTECTEDENVELOPETESTPROVIDER_H
#define OBELISK_TEST_SUPPORT_PROTECTEDENVELOPETESTPROVIDER_H

#include <memory>

namespace obelisk::frontend {
class ProtectedEnvelopeProvider;
}

std::shared_ptr<const obelisk::frontend::ProtectedEnvelopeProvider>
createProtectedEnvelopeTestProvider();

#endif
