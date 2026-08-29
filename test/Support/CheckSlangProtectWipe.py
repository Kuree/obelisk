#!/usr/bin/env python3
"""Pin the FileData destructor to the directly tested inline wipe helper."""

from pathlib import Path
import re
import sys


if len(sys.argv) != 2:
    raise SystemExit("usage: CheckSlangProtectWipe.py SLANG_SOURCE")

root = Path(sys.argv[1])
header = (root / "include/slang/text/SourceManager.h").read_text()
source = (root / "source/text/SourceManager.cpp").read_text()

if "        SmallVector<char> mem;" not in header:
    raise SystemExit("FileData::mem must be mutable internal storage")
if "const SmallVector<char> mem;" in header:
    raise SystemExit("FileData::mem must not be a const subobject")

match = re.search(
    r"SourceManager::FileData::~FileData\(\)\s*\{(?P<body>.*?)\n\}",
    source,
    re.DOTALL,
)
if not match:
    raise SystemExit("FileData destructor not found")
body = match.group("body")
if "detail::secureWipeProtectedBuffer(mem);" not in body:
    raise SystemExit("FileData destructor no longer wipes protected storage")
if "const_cast" in body:
    raise SystemExit("FileData destructor must not cast away constness")
