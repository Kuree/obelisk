//===- Options.td - Obelisk compiler driver options ---------*- tablegen -*-===//

include "llvm/Option/OptParser.td"

def Action_Group : OptionGroup<"Actions">;
def Output_Group : OptionGroup<"Output options">;
def Preprocessor_Group : OptionGroup<"Preprocessor options">;
def Compilation_Group : OptionGroup<"Compilation options">;
def Diagnostic_Group : OptionGroup<"Diagnostic options">;
def Internal_Group : OptionGroup<"Internal options">;

def help : Flag<["--"], "help">,
  HelpText<"Display available options">;
def h : Flag<["-"], "h">, Alias<help>, Flags<[HelpHidden]>;
def help_hidden : Flag<["--"], "help-hidden">,
  HelpText<"Display all options, including internal options">;
def version : Flag<["--"], "version">,
  HelpText<"Display the Obelisk and frontend versions">;

def emit_slang : Flag<["-"], "emit-slang">, Group<Action_Group>,
  HelpText<"Emit elaborated Slang dialect MLIR after SystemVerilog import">;
def E : Flag<["-"], "E">, Group<Action_Group>,
  HelpText<"Run only the SystemVerilog preprocessor">;
def preprocess : Flag<["--"], "preprocess">, Alias<E>,
  Flags<[HelpHidden]>;
def emit_obelisk : Flag<["-"], "emit-obelisk">, Group<Action_Group>,
  HelpText<"Emit Obelisk dialect MLIR">;
def emit_sim : Flag<["-"], "emit-sim">, Group<Action_Group>,
  HelpText<"Emit executable obelisk_sim SSA MLIR">;
def emit_schedule : Flag<["-"], "emit-schedule">, Group<Action_Group>,
  HelpText<"Emit deterministic compute-graph and event-region diagnostics">;
def c : Flag<["-"], "c">, Group<Action_Group>,
  HelpText<"Emit a relocatable object for the selected target">;
def emit_llvm : Flag<["-"], "emit-llvm">, Group<Action_Group>,
  HelpText<"Emit textual LLVM IR for the selected target">;
def emit_dpi_header : Flag<["--"], "emit-dpi-header">, Group<Action_Group>,
  HelpText<"Emit exact C prototypes for DPI imports">;
def print_resource_dir : Flag<["--"], "print-resource-dir">, Group<Action_Group>,
  HelpText<"Print the Obelisk resource directory and exit">;

def o : JoinedOrSeparate<["-"], "o">, Group<Output_Group>,
  MetaVarName<"<file>">,
  HelpText<"Write output to <file> instead of standard output">;
def mlir_print_debuginfo : Flag<["--"], "mlir-print-debuginfo">,
  Group<Output_Group>,
  HelpText<"Include source locations in emitted MLIR">;
def sysroot_EQ : Joined<["--"], "sysroot=">, Group<Output_Group>,
  MetaVarName<"<dir>">,
  HelpText<"Use <dir> for target C and C++ link inputs">;
def target_EQ : Joined<["--"], "target=">, Group<Output_Group>,
  MetaVarName<"<native|wasm64>">,
  HelpText<"Select the code-generation target">;

def threads_EQ : Joined<["--"], "threads=">, Group<Compilation_Group>,
  MetaVarName<"<count>">,
  HelpText<"Set generated simulator worker-lane count">;
def compile_threads_EQ : Joined<["--"], "compile-threads=">,
  Group<Compilation_Group>, MetaVarName<"<count>">,
  HelpText<"Set MLIR, LLD, and Full-LTO code-generation thread count">;
def vpi_EQ : Joined<["--"], "vpi=">, Group<Compilation_Group>,
  MetaVarName<"<off|read|full>">, Values<"off,read,full">,
  HelpText<"Select VPI observability and mutation capability">;
def execution_tier_EQ : Joined<["--"], "execution-tier=">,
  Group<Compilation_Group>, MetaVarName<"<native|bytecode>">,
  Values<"native,bytecode">,
  HelpText<"Select generated simulator execution tier (default: native)">;
def native_scheduler_EQ : Joined<["--"], "native-scheduler=">,
  Group<Compilation_Group>, MetaVarName<"<auto|generic|aot|eval>">,
  Values<"auto,generic,aot,eval">,
  HelpText<"Select native scheduler generation (default: hybrid auto)">;
def static_specialization_EQ : Joined<["--"], "static-specialization=">,
  Group<Compilation_Group>, MetaVarName<"<auto|off|on>">,
  Values<"auto,off,on">,
  HelpText<"Select direct static-state and narrow-NBA specialization">;
def fno_lto : Flag<["-"], "fno-lto">, Group<Compilation_Group>,
  HelpText<"Link against the prebuilt runtime instead of running full LTO">;
def flto : Flag<["-"], "flto">, Group<Compilation_Group>, Flags<[HelpHidden]>,
  HelpText<"Run full LTO over the runtime (default above -O0)">;
def O0 : Flag<["-"], "O0">, Group<Compilation_Group>,
  HelpText<"Disable simulation and LLVM optimization">;
def O1 : Flag<["-"], "O1">, Group<Compilation_Group>,
  HelpText<"Enable conservative optimization">;
def O2 : Flag<["-"], "O2">, Group<Compilation_Group>,
  HelpText<"Enable balanced optimization">;
def O3 : Flag<["-"], "O3">, Group<Compilation_Group>,
  HelpText<"Enable aggressive optimization (default)">;

def I : JoinedOrSeparate<["-"], "I">, Group<Preprocessor_Group>,
  MetaVarName<"<dir>">,
  HelpText<"Add a directory to the include search path">;
def include_directory : Separate<["--"], "include-directory">, Alias<I>,
  Flags<[HelpHidden]>;
def isystem : JoinedOrSeparate<["-"], "isystem">,
  Group<Preprocessor_Group>, MetaVarName<"<dir>">,
  HelpText<"Add a directory to the system include search path">;
def D : JoinedOrSeparate<["-"], "D">, Group<Preprocessor_Group>,
  MetaVarName<"<macro[=value]>">,
  HelpText<"Define a preprocessor macro">;
def U : JoinedOrSeparate<["-"], "U">, Group<Preprocessor_Group>,
  MetaVarName<"<macro>">,
  HelpText<"Undefine a preprocessor macro">;
def max_include_depth_EQ : Joined<["--"], "max-include-depth=">,
  Group<Preprocessor_Group>, MetaVarName<"<depth>">,
  HelpText<"Set the maximum nested include depth">;

def f : Separate<["-"], "f">, Group<Compilation_Group>,
  MetaVarName<"<file>">,
  HelpText<"Read additional compilation arguments from a command file">;
def filelist : Separate<["--"], "filelist">, Alias<f>,
  Flags<[HelpHidden]>;
def y : JoinedOrSeparate<["-"], "y">, Group<Compilation_Group>,
  MetaVarName<"<dir>">,
  HelpText<"Add a directory to the module library search path">;
def Y : JoinedOrSeparate<["-"], "Y">, Group<Compilation_Group>,
  MetaVarName<"<extension>">,
  HelpText<"Add a module library filename extension">;
def l : JoinedOrSeparate<["-"], "l">, Group<Compilation_Group>,
  MetaVarName<"<file>">,
  HelpText<"Add a SystemVerilog library file">;
def single_unit : Flag<["--"], "single-unit">, Group<Compilation_Group>,
  HelpText<"Treat all primary sources as one compilation unit">;
def libraries_inherit_macros :
  Flag<["--"], "libraries-inherit-macros">, Group<Compilation_Group>,
  HelpText<"Make library files inherit primary-source macro definitions">;
def top_EQ : Joined<["--"], "top=">, Group<Compilation_Group>,
  MetaVarName<"<module>">,
  HelpText<"Select a top-level module; may be specified more than once">;
def G : JoinedOrSeparate<["-"], "G">, Group<Compilation_Group>,
  MetaVarName<"<name=value>">,
  HelpText<"Override a top-level module parameter">;
def std_EQ : Joined<["--"], "std=">, Group<Compilation_Group>,
  MetaVarName<"<1800-2017|1800-2023>">,
  Values<"1800-2017,1800-2023">,
  HelpText<"Select the SystemVerilog language revision">;
def timescale_EQ : Joined<["--"], "timescale=">, Group<Compilation_Group>,
  MetaVarName<"<base/precision>">,
  HelpText<"Set the default time scale">;
def allow_use_before_declare :
  Flag<["--"], "allow-use-before-declare">, Group<Compilation_Group>,
  HelpText<"Allow declarations to be referenced before they appear">;
def ignore_unknown_modules :
  Flag<["--"], "ignore-unknown-modules">, Group<Compilation_Group>,
  HelpText<"Continue when referenced modules cannot be found">;

def W : Joined<["-"], "W">, Group<Diagnostic_Group>,
  MetaVarName<"<warning>">,
  HelpText<"Configure a frontend warning">;
def error_limit_EQ : Joined<["--"], "error-limit=">,
  Group<Diagnostic_Group>, MetaVarName<"<count>">,
  HelpText<"Stop after this many frontend errors">;
def suppress_warnings_EQ : Joined<["--"], "suppress-warnings=">,
  Group<Diagnostic_Group>, MetaVarName<"<path>">,
  HelpText<"Suppress frontend warnings originating below a path">;

def Xslang : Separate<["-"], "Xslang">, Group<Internal_Group>,
  MetaVarName<"<arg>">, Flags<[HelpHidden]>,
  HelpText<"Pass one argument directly to the slang frontend">;

def _DASH_DASH : Option<["--"], "", KIND_REMAINING_ARGS>,
  Flags<[HelpHidden]>;
