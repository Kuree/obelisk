//===- DesignReflection.td - design database wire schema -----------------===//

class ReflectionEncoding<string cppName_, int width_> {
  string cppName = cppName_;
  int width = width_;
}

def Bytes8 : ReflectionEncoding<"Bytes", 8>;
def U16LE : ReflectionEncoding<"U16LE", 2>;
def U32LE : ReflectionEncoding<"U32LE", 4>;
def U64LE : ReflectionEncoding<"U64LE", 8>;
def I64LE : ReflectionEncoding<"I64LE", 8>;

class ReflectionField<string cppName_, int offset_,
                      ReflectionEncoding encoding_> {
  string cppName = cppName_;
  int offset = offset_;
  ReflectionEncoding encoding = encoding_;
}

class ReflectionLayout<string cppName_, int size_,
                       list<ReflectionField> fields_> {
  string cppName = cppName_;
  int size = size_;
  list<ReflectionField> fields = fields_;
}

class ReflectionRecordKind<string cppName_, int value_,
                           ReflectionLayout layout_> {
  string cppName = cppName_;
  int value = value_;
  ReflectionLayout layout = layout_;
}

def HeaderMagic : ReflectionField<"Magic", 0, Bytes8>;
def HeaderVersion : ReflectionField<"Version", 8, U32LE>;
def HeaderReserved : ReflectionField<"Reserved", 12, U32LE>;
def HeaderProfile : ReflectionField<"Profile", 16, U32LE>;
def HeaderSize : ReflectionField<"HeaderSize", 20, U32LE>;
def HeaderImageSize : ReflectionField<"ImageSize", 24, U64LE>;
def HeaderChecksum : ReflectionField<"Checksum", 32, U64LE>;
def HeaderRoot : ReflectionField<"Root", 40, U64LE>;
def HeaderScopeOffset : ReflectionField<"ScopeOffset", 48, U64LE>;
def HeaderScopeCount : ReflectionField<"ScopeCount", 56, U64LE>;
def HeaderObjectOffset : ReflectionField<"ObjectOffset", 64, U64LE>;
def HeaderObjectCount : ReflectionField<"ObjectCount", 72, U64LE>;
def HeaderTypeOffset : ReflectionField<"TypeOffset", 80, U64LE>;
def HeaderTypeCount : ReflectionField<"TypeCount", 88, U64LE>;
def HeaderStringOffset : ReflectionField<"StringOffset", 96, U64LE>;
def HeaderStringSize : ReflectionField<"StringSize", 104, U64LE>;
def HeaderIndexOffset : ReflectionField<"IndexOffset", 112, U64LE>;
def HeaderIndexCount : ReflectionField<"IndexCount", 120, U64LE>;
def HeaderStatementOffset : ReflectionField<"StatementOffset", 128, U64LE>;
def HeaderStatementCount : ReflectionField<"StatementCount", 136, U64LE>;
def HeaderStatementSiteOffset
    : ReflectionField<"StatementSiteOffset", 144, U64LE>;
def HeaderStatementSiteCount
    : ReflectionField<"StatementSiteCount", 152, U64LE>;
def Header : ReflectionLayout<"Header", 160,
  [HeaderMagic, HeaderVersion, HeaderReserved, HeaderProfile, HeaderSize,
   HeaderImageSize, HeaderChecksum, HeaderRoot, HeaderScopeOffset,
   HeaderScopeCount, HeaderObjectOffset, HeaderObjectCount, HeaderTypeOffset,
   HeaderTypeCount, HeaderStringOffset, HeaderStringSize, HeaderIndexOffset,
   HeaderIndexCount, HeaderStatementOffset, HeaderStatementCount,
   HeaderStatementSiteOffset, HeaderStatementSiteCount]>;

def ScopeKindField : ReflectionField<"Kind", 0, U32LE>;
def ScopeCaps : ReflectionField<"Caps", 4, U32LE>;
def ScopeID : ReflectionField<"ID", 8, U64LE>;
def ScopeParent : ReflectionField<"Parent", 16, U64LE>;
def ScopeFirstChild : ReflectionField<"FirstChild", 24, U64LE>;
def ScopeNextSibling : ReflectionField<"NextSibling", 32, U64LE>;
def ScopeName : ReflectionField<"Name", 40, U64LE>;
def ScopeSourceFile : ReflectionField<"SourceFile", 48, U64LE>;
def ScopeSourceLineColumn : ReflectionField<"SourceLineColumn", 56, U64LE>;
def Scope : ReflectionLayout<"Scope", 64,
  [ScopeKindField, ScopeCaps, ScopeID, ScopeParent, ScopeFirstChild,
   ScopeNextSibling, ScopeName, ScopeSourceFile, ScopeSourceLineColumn]>;

def ObjectKindField : ReflectionField<"Kind", 0, U32LE>;
def ObjectCaps : ReflectionField<"Caps", 4, U32LE>;
def ObjectID : ReflectionField<"ID", 8, U64LE>;
def ObjectOwner : ReflectionField<"Owner", 16, U64LE>;
def ObjectNextSibling : ReflectionField<"NextSibling", 24, U64LE>;
def ObjectSourceFile : ReflectionField<"SourceFile", 32, U64LE>;
def ObjectName : ReflectionField<"Name", 40, U64LE>;
def ObjectType : ReflectionField<"Type", 48, U64LE>;
def ObjectWidth : ReflectionField<"Width", 56, U64LE>;
def ObjectLeft : ReflectionField<"Left", 64, I64LE>;
def ObjectRight : ReflectionField<"Right", 72, I64LE>;
def ObjectStateOffset : ReflectionField<"StateOffset", 80, U64LE>;
def ObjectSourceLineColumn : ReflectionField<"SourceLineColumn", 88, U64LE>;
def Object : ReflectionLayout<"Object", 96,
  [ObjectKindField, ObjectCaps, ObjectID, ObjectOwner, ObjectNextSibling,
   ObjectSourceFile, ObjectName, ObjectType, ObjectWidth, ObjectLeft,
   ObjectRight, ObjectStateOffset, ObjectSourceLineColumn]>;

def TypeRecordKind : ReflectionField<"RecordKind", 0, U32LE>;
def TypeKindAndFlags : ReflectionField<"KindAndFlags", 4, U32LE>;
def TypeWidth : ReflectionField<"Width", 8, U64LE>;
def TypeLeft : ReflectionField<"Left", 16, I64LE>;
def TypeRight : ReflectionField<"Right", 24, I64LE>;
def TypeElement : ReflectionField<"Element", 32, U64LE>;
def TypeFirstChild : ReflectionField<"FirstChild", 40, U64LE>;
def TypeChildCount : ReflectionField<"ChildCount", 48, U64LE>;
def TypeOrdinal : ReflectionField<"Ordinal", 56, U64LE>;
def TypePackedOffset : ReflectionField<"PackedOffset", 64, U64LE>;
def TypeName : ReflectionField<"Name", 72, U64LE>;
def Type : ReflectionLayout<"Type", 80,
  [TypeRecordKind, TypeKindAndFlags, TypeWidth, TypeLeft, TypeRight,
   TypeElement, TypeFirstChild, TypeChildCount, TypeOrdinal, TypePackedOffset,
   TypeName]>;

def IndexHash : ReflectionField<"Hash", 0, U64LE>;
def IndexName : ReflectionField<"Name", 8, U64LE>;
def IndexRecord : ReflectionField<"Record", 16, U64LE>;
def Index : ReflectionLayout<"Index", 24,
  [IndexHash, IndexName, IndexRecord]>;

// Compact immutable source identities. Indices and string offsets are 32-bit
// offsets within their respective tables; UINT32_MAX is the absent parent.
// Executable tier/fragment/PC bindings deliberately live in a later side
// table and never alter these semantic identities.
def StatementID : ReflectionField<"ID", 0, U64LE>;
def StatementOwnerObjectIndex
    : ReflectionField<"OwnerObjectIndex", 8, U32LE>;
def StatementScopeIndex : ReflectionField<"ScopeIndex", 12, U32LE>;
def StatementParentIndex : ReflectionField<"ParentIndex", 16, U32LE>;
def StatementSourceFile : ReflectionField<"SourceFile", 20, U32LE>;
def StatementName : ReflectionField<"Name", 24, U32LE>;
def StatementSourceLine : ReflectionField<"SourceLine", 28, U32LE>;
def StatementSourceColumn : ReflectionField<"SourceColumn", 32, U32LE>;
def StatementVPIKind : ReflectionField<"VPIKind", 36, U16LE>;
def StatementFlags : ReflectionField<"Flags", 38, U16LE>;
def Statement : ReflectionLayout<"Statement", 40,
  [StatementID, StatementOwnerObjectIndex, StatementScopeIndex,
   StatementParentIndex, StatementSourceFile, StatementName,
   StatementSourceLine, StatementSourceColumn, StatementVPIKind,
   StatementFlags]>;

def StatementSiteID : ReflectionField<"ID", 0, U64LE>;
def StatementSiteStatementIndex
    : ReflectionField<"StatementIndex", 8, U32LE>;
def StatementSitePhase : ReflectionField<"Phase", 12, U16LE>;
def StatementSiteFlags : ReflectionField<"Flags", 14, U16LE>;
def StatementSite : ReflectionLayout<"StatementSite", 16,
  [StatementSiteID, StatementSiteStatementIndex, StatementSitePhase,
   StatementSiteFlags]>;

def ScopeRecord : ReflectionRecordKind<"Scope", 1, Scope>;
def StorageRecord : ReflectionRecordKind<"Storage", 2, Object>;
def NetRecord : ReflectionRecordKind<"Net", 3, Object>;
def DriverRecord : ReflectionRecordKind<"Driver", 4, Object>;
def ProcessRecord : ReflectionRecordKind<"Process", 5, Object>;
def TypeRecord : ReflectionRecordKind<"Type", 6, Type>;
def FunctionRecord : ReflectionRecordKind<"Function", 7, Object>;
def PortRecord : ReflectionRecordKind<"Port", 8, Object>;
