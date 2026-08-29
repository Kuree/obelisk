#!/usr/bin/env python3
"""Apply Obelisk's narrowly scoped protected-envelope hook to pinned slang.

The release archive remains immutable and offline builds need no git or patch
utility. Every replacement checks its exact v11.0 context and is idempotent;
an unexpected upstream source fails configuration instead of being guessed at.
"""

from pathlib import Path
import shutil
import sys


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"expected one pinned-slang context in {path}, found {count}")
    path.write_text(text.replace(old, new, 1))


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("usage: ApplySlangProtectHook.py SLANG_SOURCE OVERLAY_DIR")
    source = Path(sys.argv[1])
    overlay = Path(sys.argv[2])
    if not (source / "CMakeLists.txt").is_file():
        raise RuntimeError(f"not a slang source tree: {source}")

    shutil.copyfile(overlay / "ProtectEnvelope.h",
                    source / "include/slang/parsing/ProtectEnvelope.h")
    shutil.copyfile(overlay / "SecureBuffer.h",
                    source / "include/slang/util/SecureBuffer.h")

    replace_once(
        source / "include/slang/parsing/Preprocessor.h",
        '#include "slang/parsing/Lexer.h"\n',
        '#include "slang/parsing/Lexer.h"\n#include "slang/parsing/ProtectEnvelope.h"\n')
    replace_once(
        source / "include/slang/parsing/Preprocessor.h",
        "    /// If true, the preprocessor will assume that a missing end of scope token for a\n",
        "    /// Optional host implementation for Clause 34 decryption.\n"
        "    std::shared_ptr<const ProtectEnvelopeDecryptor> protectEnvelopeDecryptor;\n\n"
        "    /// Maximum recursively substituted protected-source depth. IEEE 1800-2017\n"
        "    /// 34.2 requires implementations to accept at least eight.\n"
        "    uint32_t maxProtectEnvelopeDepth = 64;\n\n"
        "    /// Hard resource bounds, checked before retaining decrypted text.\n"
        "    uint64_t maxProtectEnvelopeBytes = 64 * 1024 * 1024;\n"
        "    uint32_t maxProtectEnvelopeCount = 4096;\n\n"
        "    /// If true, the preprocessor will assume that a missing end of scope token for a\n")
    replace_once(
        source / "include/slang/parsing/Preprocessor.h",
        "    void resetProtectState();\n\n    // Pragma protect handlers\n",
        "    void resetProtectState();\n"
        "    void recordProtectExpression(Token keyword,\n"
        "                                 const syntax::PragmaExpressionSyntax* args);\n"
        "    void pushPendingProtectedSource();\n\n"
        "    // Pragma protect handlers\n")
    replace_once(
        source / "include/slang/parsing/Preprocessor.h",
        "    ProtectEncoding protectEncoding = ProtectEncoding::Raw;\n\n"
        "    // Parser for numeric literals in pragma expressions.\n",
        "    ProtectEncoding protectEncoding = ProtectEncoding::Raw;\n"
        "    // IEEE 1800-2017 34.4: directive state is lexical and therefore\n"
        "    // intentionally survives source-stack changes such as `include.\n"
        "    SmallVector<ProtectEnvelopeRecord> protectEnvelopePrefix;\n"
        "    std::optional<ProtectEnvelope> activeProtectEnvelope;\n"
        "    std::optional<ProtectEnvelopeResult> pendingProtectResult;\n"
        "    SourceLocation pendingProtectLocation;\n"
        "    const SourceLibrary* pendingProtectLibrary = nullptr;\n"
        "    uint32_t protectedSourceDepth = 0;\n"
        "    uint32_t protectEnvelopeCount = 0;\n"
        "    uint64_t protectedSourceCount = 0;\n"
        "    bool discardProtectEnvelope = false;\n\n"
        "    // Parser for numeric literals in pragma expressions.\n")
    replace_once(
        source / "include/slang/driver/Driver.h",
        '#include "slang/parsing/LexerFacts.h"\n',
        '#include "slang/parsing/LexerFacts.h"\n#include "slang/parsing/ProtectEnvelope.h"\n')
    replace_once(
        source / "include/slang/driver/Driver.h",
        "        /// If true, the preprocessor will support legacy protected envelope directives,\n",
        "        /// Optional host implementation for standard protected envelopes.\n"
        "        std::shared_ptr<const parsing::ProtectEnvelopeDecryptor> protectEnvelopeDecryptor;\n\n"
        "        uint32_t maxProtectEnvelopeDepth = 64;\n"
        "        uint64_t maxProtectEnvelopeBytes = 64 * 1024 * 1024;\n"
        "        uint32_t maxProtectEnvelopeCount = 4096;\n\n"
        "        /// If true, the preprocessor will support legacy protected envelope directives,\n")
    replace_once(
        source / "source/driver/Driver.cpp",
        "    ppoptions.keywordMapping = options.keywordMapping;\n",
        "    ppoptions.keywordMapping = options.keywordMapping;\n"
        "    ppoptions.protectEnvelopeDecryptor = options.protectEnvelopeDecryptor;\n"
        "    ppoptions.maxProtectEnvelopeDepth = options.maxProtectEnvelopeDepth;\n"
        "    ppoptions.maxProtectEnvelopeBytes = options.maxProtectEnvelopeBytes;\n"
        "    ppoptions.maxProtectEnvelopeCount = options.maxProtectEnvelopeCount;\n")

    replace_once(
        source / "scripts/diagnostics.txt",
        'warning protected-envelope ProtectedEnvelope "protected envelopes cannot be decrypted and will be skipped entirely"\n',
        'warning protected-envelope ProtectedEnvelope "protected envelopes cannot be decrypted and will be skipped entirely"\n'
        'error ProtectedEnvelopeProviderUnavailable "protected envelope rejected (provider unavailable)"\n'
        'error ProtectedEnvelopeRejected "protected envelope rejected (provider policy)"\n'
        'error ProtectedEnvelopeInvalidData "protected envelope rejected (invalid data)"\n'
        'error ProtectedEnvelopeResourceLimit "protected envelope rejected (resource limit)"\n'
        'error ProtectedEnvelopeProviderFailure "protected envelope rejected (provider failure)"\n'
        'error ProtectedEnvelopeDepthExceeded "protected envelope rejected (nesting limit)"\n'
        'error ProtectedSourceDiagnostic "diagnostic in protected source (details suppressed)"\n')

    replace_once(
        source / "include/slang/text/SourceManager.h",
        "    SourceBuffer assignBuffer(std::string_view path, SmallVector<char>&& buffer,\n"
        "                              SourceLocation includedFrom = SourceLocation(),\n"
        "                              const SourceLibrary* library = nullptr);\n",
        "    SourceBuffer assignBuffer(std::string_view path, SmallVector<char>&& buffer,\n"
        "                              SourceLocation includedFrom = SourceLocation(),\n"
        "                              const SourceLibrary* library = nullptr);\n\n"
        "    /// Move protected source into an in-memory buffer that is wiped at destruction.\n"
        "    SourceBuffer assignProtectedBuffer(std::string_view path, SmallVector<char>&& buffer,\n"
        "                                         SourceLocation includedFrom = SourceLocation(),\n"
        "                                         const SourceLibrary* library = nullptr);\n")
    replace_once(
        source / "include/slang/text/SourceManager.h",
        "    /// Gets the kind for the given buffer. Returns BufferKind::Macro or BufferKind::MacroArg\n",
        "    /// Mark ciphertext so diagnostics emitted after encoded-text lexing redact it.\n"
        "    void markProtected(SourceRange range);\n\n"
        "    /// Returns true for decrypted source or a marked ciphertext range.\n"
        "    bool isProtected(SourceLocation location) const;\n\n"
        "    /// Gets the kind for the given buffer. Returns BufferKind::Macro or BufferKind::MacroArg\n")
    replace_once(
        source / "include/slang/text/SourceManager.h",
        "    // map from buffer to diagnostic directive lists\n"
        "    flat_hash_map<BufferID, std::vector<DiagnosticDirectiveInfo>> diagDirectives;\n",
        "    // map from buffer to diagnostic directive lists\n"
        "    flat_hash_map<BufferID, std::vector<DiagnosticDirectiveInfo>> diagDirectives;\n\n"
        "    flat_hash_map<BufferID, std::vector<std::pair<size_t, size_t>>> protectedRanges;\n"
        "    std::atomic<bool> hasProtectedLocations = false;\n")
    replace_once(
        source / "include/slang/text/SourceManager.h",
        "        const SmallVector<char> mem;                  // file contents\n",
        "        SmallVector<char> mem;                        // file contents\n")
    replace_once(
        source / "include/slang/text/SourceManager.h",
        "        const std::filesystem::path fullPath;         // full path to the file\n\n"
        "        FileData(const std::filesystem::path* directory, std::string name, SmallVector<char>&& data,\n"
        "                 std::filesystem::path fullPath) :\n"
        "            name(std::move(name)), mem(std::move(data)), directory(directory),\n"
        "            fullPath(std::move(fullPath)) {}\n",
        "        const std::filesystem::path fullPath;         // full path to the file\n"
        "        const bool protectedSource;\n\n"
        "        FileData(const std::filesystem::path* directory, std::string name, SmallVector<char>&& data,\n"
        "                 std::filesystem::path fullPath, bool protectedSource = false) :\n"
        "            name(std::move(name)), mem(std::move(data)), directory(directory),\n"
        "            fullPath(std::move(fullPath)), protectedSource(protectedSource) {}\n"
        "        ~FileData();\n")
    replace_once(
        source / "include/slang/text/SourceManager.h",
        "                             uint64_t sortKey, SmallVector<char>&& buffer);\n",
        "                             uint64_t sortKey, SmallVector<char>&& buffer,\n"
        "                             bool protectedSource = false);\n")
    replace_once(
        source / "include/slang/text/SourceManager.h",
        "    template<IsLock TLock>\n"
        "    bool isMacroLocImpl(SourceLocation location, TLock& lock) const;\n",
        "    template<IsLock TLock>\n"
        "    bool isProtectedImpl(SourceLocation location, TLock& lock) const;\n\n"
        "    template<IsLock TLock>\n"
        "    bool isMacroLocImpl(SourceLocation location, TLock& lock) const;\n")

    replace_once(
        source / "source/text/SourceManager.cpp",
        '#include "slang/util/OS.h"\n',
        '#include "slang/util/OS.h"\n#include "slang/util/SecureBuffer.h"\n')
    replace_once(
        source / "source/text/SourceManager.cpp",
        "static const fs::path emptyPath;\n",
        "static const fs::path emptyPath;\n\n"
        "struct SecureBufferGuard {\n"
        "    SmallVectorBase<char>& buffer;\n"
        "    ~SecureBufferGuard() { detail::secureWipeProtectedBuffer(buffer); }\n"
        "};\n\n"
        "SourceManager::FileData::~FileData() {\n"
        "    if (!protectedSource)\n"
        "        return;\n"
        "    // IEEE 1800-2017 34.3.2 plaintext can reside in inline or heap\n"
        "    // storage; volatile stores wipe the full capacity of either directly.\n"
        "    detail::secureWipeProtectedBuffer(mem);\n"
        "}\n")
    replace_once(
        source / "source/text/SourceManager.cpp",
        "SourceBuffer SourceManager::assignBuffer(std::string_view bufferPath, SmallVector<char>&& buffer,\n"
        "                                         SourceLocation includedFrom,\n"
        "                                         const SourceLibrary* library) {\n",
        "SourceBuffer SourceManager::assignBuffer(std::string_view bufferPath, SmallVector<char>&& buffer,\n"
        "                                         SourceLocation includedFrom,\n"
        "                                         const SourceLibrary* library) {\n")
    # Insert the secure overload after the ordinary assignBuffer implementation.
    replace_once(
        source / "source/text/SourceManager.cpp",
        "    return cacheBuffer(std::move(path), std::move(pathStr), includedFrom, library, UINT64_MAX,\n"
        "                       std::move(buffer));\n"
        "}\n\nSourceManager::BufferOrError SourceManager::readSource",
        "    return cacheBuffer(std::move(path), std::move(pathStr), includedFrom, library, UINT64_MAX,\n"
        "                       std::move(buffer));\n"
        "}\n\n"
        "SourceBuffer SourceManager::assignProtectedBuffer(std::string_view bufferPath,\n"
        "                                                    SmallVector<char>&& buffer,\n"
        "                                                    SourceLocation includedFrom,\n"
        "                                                    const SourceLibrary* library) {\n"
        "    SecureBufferGuard guard{buffer};\n"
        "    if (buffer.empty() || buffer.back() != '\\0')\n"
        "        buffer.push_back('\\0');\n"
        "    hasProtectedLocations.store(true, std::memory_order_relaxed);\n"
        "    fs::path path(bufferPath);\n"
        "    auto pathStr = getU8Str(path);\n"
        "    return cacheBuffer(std::move(path), std::move(pathStr), includedFrom, library,\n"
        "                       UINT64_MAX, std::move(buffer), true);\n"
        "}\n\nSourceManager::BufferOrError SourceManager::readSource")
    replace_once(
        source / "source/text/SourceManager.cpp",
        "SourceManager::BufferKind SourceManager::getBufferKind(BufferID buffer) const {\n",
        "void SourceManager::markProtected(SourceRange range) {\n"
        "    if (!range.start().valid() || range.start().buffer() != range.end().buffer())\n"
        "        return;\n"
        "    std::unique_lock<std::shared_mutex> lock(mutex);\n"
        "    protectedRanges[range.start().buffer()].emplace_back(range.start().offset(),\n"
        "                                                         range.end().offset());\n"
        "    hasProtectedLocations.store(true, std::memory_order_relaxed);\n"
        "}\n\n"
        "bool SourceManager::isProtected(SourceLocation location) const {\n"
        "    if (!hasProtectedLocations.load(std::memory_order_relaxed) || !location.valid())\n"
        "        return false;\n"
        "    std::shared_lock<std::shared_mutex> lock(mutex);\n"
        "    return isProtectedImpl(location, lock);\n"
        "}\n\n"
        "template<IsLock TLock>\n"
        "bool SourceManager::isProtectedImpl(SourceLocation location, TLock& lock) const {\n"
        "    SmallVector<SourceLocation, 8> pending;\n"
        "    SmallVector<std::pair<BufferID, size_t>, 8> visited;\n"
        "    pending.push_back(location);\n"
        "    while (!pending.empty()) {\n"
        "        auto current = pending.back();\n"
        "        pending.pop_back();\n"
        "        if (!current.valid())\n"
        "            continue;\n"
        "        bool seen = false;\n"
        "        for (auto [buffer, offset] : visited) {\n"
        "            if (buffer == current.buffer() && offset == current.offset()) {\n"
        "                seen = true;\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "        if (seen)\n"
        "            continue;\n"
        "        visited.emplace_back(current.buffer(), current.offset());\n"
        "        auto info = getFileInfo(current.buffer(), lock);\n"
        "        if (info && info->data && info->data->protectedSource)\n"
        "            return true;\n"
        "        if (auto it = protectedRanges.find(current.buffer());\n"
        "            it != protectedRanges.end()) {\n"
        "            for (auto [start, end] : it->second) {\n"
        "                if (current.offset() >= start && current.offset() <= end)\n"
        "                    return true;\n"
        "            }\n"
        "        }\n"
        "        if (!isMacroLocImpl(current, lock))\n"
        "            continue;\n"
        "        // IEEE 1800-2017 34.3.2 preprocesses replacement text normally.\n"
        "        // Follow both spelling and expansion provenance: a macro defined in\n"
        "        // protected source remains protected when invoked from public source.\n"
        "        pending.push_back(getOriginalLocImpl(current, lock));\n"
        "        auto expansion = getExpansionRangeImpl(current, lock);\n"
        "        pending.push_back(expansion.start());\n"
        "        pending.push_back(expansion.end());\n"
        "    }\n"
        "    return false;\n"
        "}\n\nSourceManager::BufferKind SourceManager::getBufferKind(BufferID buffer) const {\n")
    replace_once(
        source / "source/text/SourceManager.cpp",
        "SourceLocation SourceManager::createExpansionLoc(SourceLocation originalLoc,\n"
        "                                                 SourceRange expansionRange, bool isMacroArg) {\n"
        "    std::unique_lock<std::shared_mutex> lock(mutex);\n\n"
        "    bufferEntries.emplace_back(ExpansionInfo(originalLoc, expansionRange, isMacroArg));\n"
        "    return SourceLocation(BufferID((uint32_t)(bufferEntries.size() - 1), \"\"sv), 0);\n"
        "}\n",
        "SourceLocation SourceManager::createExpansionLoc(SourceLocation originalLoc,\n"
        "                                                 SourceRange expansionRange, bool isMacroArg) {\n"
        "    std::unique_lock<std::shared_mutex> lock(mutex);\n\n"
        "    bool protectedExpansion = hasProtectedLocations.load(std::memory_order_relaxed) &&\n"
        "                              (isProtectedImpl(originalLoc, lock) ||\n"
        "                               isProtectedImpl(expansionRange.start(), lock) ||\n"
        "                               isProtectedImpl(expansionRange.end(), lock));\n"
        "    bufferEntries.emplace_back(ExpansionInfo(originalLoc, expansionRange, isMacroArg));\n"
        "    SourceLocation result(BufferID((uint32_t)(bufferEntries.size() - 1), \"\"sv), 0);\n"
        "    if (protectedExpansion) {\n"
        "        protectedRanges[result.buffer()].emplace_back(0, size_t(-1));\n"
        "        if (expansionRange.start().buffer() == expansionRange.end().buffer())\n"
        "            protectedRanges[expansionRange.start().buffer()].emplace_back(\n"
        "                expansionRange.start().offset(), expansionRange.end().offset());\n"
        "    }\n"
        "    return result;\n"
        "}\n")
    replace_once(
        source / "source/text/SourceManager.cpp",
        "SourceLocation SourceManager::createExpansionLoc(SourceLocation originalLoc,\n"
        "                                                 SourceRange expansionRange,\n"
        "                                                 std::string_view macroName) {\n"
        "    std::unique_lock<std::shared_mutex> lock(mutex);\n\n"
        "    bufferEntries.emplace_back(ExpansionInfo(originalLoc, expansionRange, macroName));\n"
        "    return SourceLocation(BufferID((uint32_t)(bufferEntries.size() - 1), macroName), 0);\n"
        "}\n",
        "SourceLocation SourceManager::createExpansionLoc(SourceLocation originalLoc,\n"
        "                                                 SourceRange expansionRange,\n"
        "                                                 std::string_view macroName) {\n"
        "    std::unique_lock<std::shared_mutex> lock(mutex);\n\n"
        "    bool protectedExpansion = hasProtectedLocations.load(std::memory_order_relaxed) &&\n"
        "                              (isProtectedImpl(originalLoc, lock) ||\n"
        "                               isProtectedImpl(expansionRange.start(), lock) ||\n"
        "                               isProtectedImpl(expansionRange.end(), lock));\n"
        "    bufferEntries.emplace_back(ExpansionInfo(originalLoc, expansionRange, macroName));\n"
        "    SourceLocation result(\n"
        "        BufferID((uint32_t)(bufferEntries.size() - 1), macroName), 0);\n"
        "    if (protectedExpansion) {\n"
        "        protectedRanges[result.buffer()].emplace_back(0, size_t(-1));\n"
        "        if (expansionRange.start().buffer() == expansionRange.end().buffer())\n"
        "            protectedRanges[expansionRange.start().buffer()].emplace_back(\n"
        "                expansionRange.start().offset(), expansionRange.end().offset());\n"
        "    }\n"
        "    return result;\n"
        "}\n")
    replace_once(
        source / "source/text/SourceManager.cpp",
        "SourceBuffer SourceManager::cacheBuffer(fs::path&& path, std::string&& pathStr,\n"
        "                                        SourceLocation includedFrom, const SourceLibrary* library,\n"
        "                                        uint64_t sortKey, SmallVector<char>&& buffer) {\n",
        "SourceBuffer SourceManager::cacheBuffer(fs::path&& path, std::string&& pathStr,\n"
        "                                        SourceLocation includedFrom, const SourceLibrary* library,\n"
        "                                        uint64_t sortKey, SmallVector<char>&& buffer,\n"
        "                                        bool protectedSource) {\n"
        "    std::optional<SecureBufferGuard> secureGuard;\n"
        "    if (protectedSource)\n"
        "        secureGuard.emplace(buffer);\n")
    replace_once(
        source / "source/text/SourceManager.cpp",
        "    auto fd = std::make_unique<FileData>(directory, std::move(name), std::move(buffer),\n"
        "                                         std::move(path));\n",
        "    auto fd = std::make_unique<FileData>(directory, std::move(name), std::move(buffer),\n"
        "                                         std::move(path), protectedSource);\n")

    replace_once(
        source / "source/diagnostics/DiagnosticEngine.cpp",
        '#include "slang/diagnostics/MetaDiags.h"\n',
        '#include "slang/diagnostics/MetaDiags.h"\n'
        '#include "slang/diagnostics/PreprocessorDiags.h"\n')
    replace_once(
        source / "source/diagnostics/DiagnosticEngine.cpp",
        "    std::string message = formatMessage(diagnostic);\n",
        "    bool protectedDiagnostic = sourceManager.isProtected(loc);\n"
        "    if (!protectedDiagnostic) {\n"
        "        for (auto range : diagnostic.ranges) {\n"
        "            if (sourceManager.isProtected(range.start()) ||\n"
        "                sourceManager.isProtected(range.end())) {\n"
        "                protectedDiagnostic = true;\n"
        "                break;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    if (protectedDiagnostic) {\n"
        "        // Central redaction protects every DiagnosticClient (including JSON and\n"
        "        // third-party clients) from args, source ranges, symbols, and stacks.\n"
        "        Diagnostic redacted(diag::ProtectedSourceDiagnostic, SourceLocation::NoLocation);\n"
        "        ReportedDiagnostic report(redacted);\n"
        "        report.location = SourceLocation::NoLocation;\n"
        "        report.severity = severity;\n"
        "        report.formattedMessage =\n"
        "            \"diagnostic in protected source (details suppressed)\"sv;\n"
        "        for (auto& client : clients)\n"
        "            client->report(report);\n"
        "        return true;\n"
        "    }\n\n"
        "    std::string message = formatMessage(diagnostic);\n")

    # Preprocessor behavior is patched last; a failed earlier context leaves no
    # apparently functional half-hook.
    replace_once(
        source / "source/parsing/Preprocessor_pragmas.cpp",
        "    auto handle = [&](Token keyword, const PragmaExpressionSyntax* args) {\n"
        "        auto text = keyword.valueText();\n"
        "        if (auto it = pragmaProtectHandlers.find(text); it != pragmaProtectHandlers.end())\n"
        "            (this->*(it->second))(keyword, args, skippedTokens);\n",
        "    auto handle = [&](Token keyword, const PragmaExpressionSyntax* args) {\n"
        "        auto text = keyword.valueText();\n"
        "        if (auto it = pragmaProtectHandlers.find(text); it != pragmaProtectHandlers.end()) {\n"
        "            // IEEE 1800-2017 34.2 evaluates expressions left-to-right, even\n"
        "            // when begin_protected shares a directive with its descriptors.\n"
        "            if (text == \"begin_protected\"sv) {\n"
        "                (this->*(it->second))(keyword, args, skippedTokens);\n"
        "                recordProtectExpression(keyword, args);\n"
        "            }\n"
        "            else {\n"
        "                recordProtectExpression(keyword, args);\n"
        "                (this->*(it->second))(keyword, args, skippedTokens);\n"
        "            }\n"
        "        }\n")
    replace_once(
        source / "source/parsing/Preprocessor_pragmas.cpp",
        "    protectBytes = 0;\n"
        "    protectEncoding = ProtectEncoding::Raw;\n"
        "}\n\nstd::optional<uint32_t> Preprocessor::requireUInt32",
        "    protectBytes = 0;\n"
        "    protectEncoding = ProtectEncoding::Raw;\n"
        "    protectEnvelopePrefix.clear();\n"
        "}\n\nstd::optional<uint32_t> Preprocessor::requireUInt32")
    replace_once(
        source / "source/parsing/Preprocessor_pragmas.cpp",
        "void Preprocessor::handleProtectBegin(Token keyword, const PragmaExpressionSyntax* args,\n",
        "void Preprocessor::recordProtectExpression(Token keyword,\n"
        "                                           const PragmaExpressionSyntax* args) {\n"
        "    ProtectEnvelopeRecord expression;\n"
        "    expression.recordKind = ProtectRecordKind::Expression;\n"
        "    expression.name = std::string(keyword.valueText());\n"
        "    expression.value = args ? args->toString() : std::string();\n"
        "    expression.range = args ? args->sourceRange() : keyword.range();\n"
        "    auto name = keyword.valueText();\n"
        "    bool isBoundary = name == \"begin_protected\"sv || name == \"end_protected\"sv;\n"
        "    bool isBlock = name == \"data_block\"sv || name == \"digest_block\"sv ||\n"
        "                   name == \"key_block\"sv || name == \"data_public_key\"sv ||\n"
        "                   name == \"data_decrypt_key\"sv || name == \"digest_public_key\"sv ||\n"
        "                   name == \"digest_decrypt_key\"sv || name == \"key_public_key\"sv;\n"
        "    if (!isBoundary && !isBlock)\n"
        "        protectEnvelopePrefix.push_back(expression);\n"
        "    if (activeProtectEnvelope && !isBlock)\n"
        "        activeProtectEnvelope->records.push_back(std::move(expression));\n"
        "}\n\n"
        "void Preprocessor::handleProtectBegin(Token keyword, const PragmaExpressionSyntax* args,\n")
    replace_once(
        source / "source/parsing/Preprocessor_pragmas.cpp",
        "    ensureNoPragmaArgs(keyword, args);\n    protectDecryptDepth++;\n}\n\n"
        "void Preprocessor::handleProtectEndProtected",
        "    ensureNoPragmaArgs(keyword, args);\n"
        "    if (!protectDecryptDepth) {\n"
        "        discardProtectEnvelope = false;\n"
        "        if (protectedSourceDepth >= options.maxProtectEnvelopeDepth) {\n"
        "            addDiag(diag::ProtectedEnvelopeDepthExceeded, keyword.range());\n"
        "            discardProtectEnvelope = true;\n"
        "        }\n"
        "        else if (protectEnvelopeCount >= options.maxProtectEnvelopeCount) {\n"
        "            addDiag(diag::ProtectedEnvelopeResourceLimit, keyword.range());\n"
        "            discardProtectEnvelope = true;\n"
        "        }\n"
        "        else {\n"
        "            protectEnvelopeCount++;\n"
        "            activeProtectEnvelope.emplace();\n"
        "            activeProtectEnvelope->records = protectEnvelopePrefix;\n"
        "            activeProtectEnvelope->range = keyword.range();\n"
        "        }\n"
        "    }\n"
        "    protectDecryptDepth++;\n"
        "}\n\n"
        "void Preprocessor::handleProtectEndProtected")
    replace_once(
        source / "source/parsing/Preprocessor_pragmas.cpp",
        "    if (protectDecryptDepth)\n        protectDecryptDepth--;\n"
        "    else\n        addDiag(diag::ExtraProtectEnd, keyword.range()) << keyword.valueText();\n}\n\n"
        "void Preprocessor::handleProtectSingleArgIgnore",
        "    if (!protectDecryptDepth) {\n"
        "        addDiag(diag::ExtraProtectEnd, keyword.range()) << keyword.valueText();\n"
        "        return;\n"
        "    }\n"
        "    protectDecryptDepth--;\n"
        "    if (protectDecryptDepth)\n"
        "        return;\n"
        "    if (discardProtectEnvelope) {\n"
        "        discardProtectEnvelope = false;\n"
        "        activeProtectEnvelope.reset();\n"
        "        return;\n"
        "    }\n"
        "    if (!activeProtectEnvelope)\n"
        "        return;\n"
        "    activeProtectEnvelope->range =\n"
        "        SourceRange(activeProtectEnvelope->range.start(), keyword.range().end());\n"
        "    ProtectEnvelopeResult result;\n"
        "    try {\n"
        "        if (options.protectEnvelopeDecryptor)\n"
        "            result = options.protectEnvelopeDecryptor->decrypt(*activeProtectEnvelope);\n"
        "    }\n"
        "    catch (...) {\n"
        "        activeProtectEnvelope.reset();\n"
        "        addDiag(diag::ProtectedEnvelopeProviderFailure, keyword.range());\n"
        "        return;\n"
        "    }\n"
        "    activeProtectEnvelope.reset();\n"
        "    if (result.source.size() > options.maxProtectEnvelopeBytes)\n"
        "        result.status = ProtectEnvelopeStatus::ResourceLimit;\n"
        "    if (result.status == ProtectEnvelopeStatus::Success && result.source.empty())\n"
        "        result.status = ProtectEnvelopeStatus::InvalidData;\n"
        "    switch (result.status) {\n"
        "        case ProtectEnvelopeStatus::Success:\n"
        "            break;\n"
        "        case ProtectEnvelopeStatus::ProviderUnavailable:\n"
        "            addDiag(diag::ProtectedEnvelopeProviderUnavailable, keyword.range());\n"
        "            return;\n"
        "        case ProtectEnvelopeStatus::Rejected:\n"
        "            addDiag(diag::ProtectedEnvelopeRejected, keyword.range());\n"
        "            return;\n"
        "        case ProtectEnvelopeStatus::InvalidData:\n"
        "            addDiag(diag::ProtectedEnvelopeInvalidData, keyword.range());\n"
        "            return;\n"
        "        case ProtectEnvelopeStatus::ResourceLimit:\n"
        "            addDiag(diag::ProtectedEnvelopeResourceLimit, keyword.range());\n"
        "            return;\n"
        "    }\n"
        "    pendingProtectResult = std::move(result);\n"
        "    pendingProtectLocation = keyword.location();\n"
        "    pendingProtectLibrary = getCurrentLibrary();\n"
        "}\n\n"
        "void Preprocessor::handleProtectSingleArgIgnore")
    replace_once(
        source / "source/parsing/Preprocessor_pragmas.cpp",
        "    hasProtectedCode = true;\n    addDiag(diag::ProtectedEnvelope, token.location());\n\n"
        "    skippedTokens.push_back(token);\n",
        "    hasProtectedCode = true;\n"
        "    sourceManager.markProtected(token.range());\n"
        "    if (activeProtectEnvelope) {\n"
        "        ProtectBlockKind kind = ProtectBlockKind::Data;\n"
        "        auto name = keyword.valueText();\n"
        "        if (name == \"digest_block\"sv) kind = ProtectBlockKind::Digest;\n"
        "        else if (name == \"key_block\"sv) kind = ProtectBlockKind::Key;\n"
        "        else if (name == \"data_public_key\"sv) kind = ProtectBlockKind::DataPublicKey;\n"
        "        else if (name == \"data_decrypt_key\"sv) kind = ProtectBlockKind::DataDecryptKey;\n"
        "        else if (name == \"digest_public_key\"sv) kind = ProtectBlockKind::DigestPublicKey;\n"
        "        else if (name == \"digest_decrypt_key\"sv) kind = ProtectBlockKind::DigestDecryptKey;\n"
        "        else if (name == \"key_public_key\"sv) kind = ProtectBlockKind::KeyPublicKey;\n"
        "        ProtectEnvelopeRecord record;\n"
        "        record.recordKind = ProtectRecordKind::EncodedBlock;\n"
        "        record.name = std::string(name);\n"
        "        record.blockKind = kind;\n"
        "        record.encoding = protectEncoding;\n"
        "        record.expectedBytes = protectBytes;\n"
        "        record.encodedText = token.rawText();\n"
        "        record.range = token.range();\n"
        "        activeProtectEnvelope->records.push_back(std::move(record));\n"
        "    }\n"
        "    else if (!discardProtectEnvelope) {\n"
        "        addDiag(diag::ProtectedEnvelope, token.location());\n"
        "    }\n\n"
        "    skippedTokens.push_back(token);\n")

    replace_once(
        source / "source/parsing/Preprocessor.cpp",
        "bool Preprocessor::popSource() {\n    auto prevIncludeDepth = includeDepth;\n",
        "bool Preprocessor::popSource() {\n"
        "    bool wasProtected = protectedSourceDepth && sourceManager.isProtected(\n"
        "        SourceLocation(lexerStack.back()->getBufferId(), 0));\n"
        "    auto prevIncludeDepth = includeDepth;\n")
    replace_once(
        source / "source/parsing/Preprocessor.cpp",
        "    lexerStack.pop_back();\n    if (options.bufferChangeCB && !lexerStack.empty())\n",
        "    lexerStack.pop_back();\n"
        "    if (wasProtected && protectedSourceDepth)\n"
        "        protectedSourceDepth--;\n"
        "    if (options.bufferChangeCB && !lexerStack.empty())\n")
    replace_once(
        source / "source/parsing/Preprocessor.cpp",
        "Token Preprocessor::nextProcessed() {\n",
        "void Preprocessor::pushPendingProtectedSource() {\n"
        "    if (!pendingProtectResult)\n"
        "        return;\n"
        "    auto path = sourceManager.getFullPath(pendingProtectLocation.buffer());\n"
        "    std::string synthetic = path.empty()\n"
        "                                ? std::string(sourceManager.getRawFileName(\n"
        "                                      pendingProtectLocation.buffer()))\n"
        "                                : getU8Str(path);\n"
        "    synthetic += \".__protected_\" + std::to_string(protectedSourceCount++);\n"
        "    auto buffer = sourceManager.assignProtectedBuffer(\n"
        "        synthetic, std::move(pendingProtectResult->source), pendingProtectLocation,\n"
        "        pendingProtectLibrary);\n"
        "    pendingProtectResult.reset();\n"
        "    // IEEE 1800-2017 34.3.2 requires the replacement text to undergo\n"
        "    // ordinary macro processing and recursive envelope substitution.\n"
        "    includeDepth++;\n"
        "    protectedSourceDepth++;\n"
        "    pushSource(buffer);\n"
        "}\n\nToken Preprocessor::nextProcessed() {\n")
    replace_once(
        source / "source/parsing/Preprocessor.cpp",
        "                        trivia.push_back(directive);\n"
        "                        if (skipped)\n"
        "                            trivia.push_back(skipped);\n"
        "                        break;\n"
        "                    }\n                    case SyntaxKind::UnconnectedDriveDirective:",
        "                        trivia.push_back(directive);\n"
        "                        if (skipped)\n"
        "                            trivia.push_back(skipped);\n"
        "                        pushPendingProtectedSource();\n"
        "                        break;\n"
        "                    }\n                    case SyntaxKind::UnconnectedDriveDirective:")


if __name__ == "__main__":
    main()
