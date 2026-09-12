//===- obelisk-cov.cpp - Obelisk coverage reporting tool ----------------===//

#include "obelisk/Coverage/CoverageDatabase.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

using namespace obelisk::coverage;

namespace {

#include "CoverageReport.css.inc"
#include "CoverageReport.html.inc"
#include "CoverageReport.js.inc"

struct Options {
  std::string command;
  std::string output;
  std::string format = "text";
  std::string test = "**";
  std::string sourceRoot;
  bool discard = false;
  std::map<MetricKind, double> thresholds;
  std::vector<std::string> inputs;
};

struct Summary {
  bool available = false;
  uint64_t covered = 0;
  uint64_t total = 0;
  llvm::APInt exactCovered = llvm::APInt(1, 0);
  llvm::APInt exactTotal = llvm::APInt(1, 0);
  bool hasExactCounts = false;
  uint64_t partial = 0;
  double calculated = -1.0;
  double percentage() const {
    return calculated >= 0.0 ? calculated
           : total           ? 100.0 * static_cast<double>(covered) /
                         static_cast<double>(total)
                   : 0.0;
  }
};

struct ReportData {
  using CounterKey = std::tuple<UUID, MetricKind, uint64_t, uint64_t, uint32_t>;
  struct Contribution {
    UUID run{};
    uint64_t count = 0;
    bool overflow = false;
  };
  struct FunctionalBin {
    uint64_t id = 0;
    uint64_t templateBin = 0;
    std::string name;
    std::string hierarchy;
    FunctionalBinKind kind = FunctionalBinKind::State;
    uint32_t flags = 0;
    uint32_t ordinal = 0;
    uint32_t expansionOrdinal = 0;
    uint64_t atLeast = 1;
    uint64_t count = 0;
    bool overflow = false;
    bool excluded = false;
    bool covered = false;
    std::vector<Contribution> contributors;
  };
  struct FunctionalItem {
    struct AutomaticBin {
      std::string name;
      std::vector<uint64_t> components;
      std::vector<std::string> componentNames;
      uint64_t atLeast = 1;
      uint64_t count = 0;
      bool covered = false;
      bool missing = false;
      bool overflow = false;
      std::vector<Contribution> contributors;
    };
    uint64_t id = 0;
    uint64_t templateItem = 0;
    std::string name;
    std::string comment;
    std::string typeComment;
    std::string hierarchy;
    FunctionalItemKind kind = FunctionalItemKind::Coverpoint;
    uint64_t goal = 100;
    uint32_t weight = 1;
    uint64_t typeGoal = 100;
    uint64_t typeWeight = 1;
    uint64_t atLeast = 1;
    uint32_t ordinal = 0;
    std::vector<FunctionalBin> bins;
    uint64_t covered = 0;
    uint64_t total = 0;
    uint64_t automaticCovered = 0;
    llvm::APInt automaticCoveredWide = llvm::APInt(1, 0);
    llvm::APInt automaticTotal = llvm::APInt(1, 0);
    llvm::APInt coveredWide = llvm::APInt(1, 0);
    llvm::APInt totalWide = llvm::APInt(1, 0);
    std::vector<AutomaticBin> automaticBins;
    uint64_t automaticAtLeast = 1;
    uint64_t crossNumPrintMissing = 0;
    std::tuple<uint64_t, Digest, uint64_t, uint64_t> automaticRoot;
    double percentage = 0.0;
    bool aggregating = true;
    bool contributes = false;
  };
  struct FunctionalInstance {
    struct ItemOptions {
      std::optional<uint64_t> goal;
      std::optional<uint64_t> weight;
      std::optional<uint64_t> atLeast;
      std::optional<uint64_t> crossNumPrintMissing;
      std::optional<std::string> comment;
    };
    struct SparseCount {
      uint64_t count = 0;
      bool overflow = false;
      std::vector<Contribution> contributors;
    };
    uint64_t type = 0;
    std::string name;
    std::string comment;
    Digest configuration{};
    std::vector<std::pair<UUID, uint64_t>> sources;
    std::map<uint64_t, SparseCount> binCounts;
    std::map<uint64_t, std::map<std::vector<uint64_t>, SparseCount>>
        sparseCrossCounts;
    std::vector<FunctionalItem> items;
    std::map<uint64_t, ItemOptions> itemOptions;
    uint64_t goal = 100;
    uint64_t weight = 1;
    uint64_t atLeast = 1;
    uint64_t groupCrossNumPrintMissing = 0;
    bool hasMutableGoal = false;
    bool hasMutableWeight = false;
    bool hasMutableAtLeast = false;
    bool hasMutableCrossNumPrintMissing = false;
    bool hasMutableComment = false;
    bool perInstance = false;
    uint64_t covered = 0;
    uint64_t total = 0;
    llvm::APInt coveredWide = llvm::APInt(1, 0);
    llvm::APInt totalWide = llvm::APInt(1, 0);
    double percentage = 0.0;
    bool contributes = false;
  };
  struct FunctionalType {
    uint64_t type = 0;
    std::vector<size_t> instances;
    std::string comment;
    uint64_t goal = 100;
    uint64_t weight = 1;
    bool mergeInstances = false;
    bool hasProfile = false;
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, std::string>>
        itemOptions;
    double percentage = 0.0;
    bool contributes = false;
  };
  std::set<UUID> runs;
  std::set<std::pair<uint64_t, Digest>> configurations;
  std::map<CounterKey, uint64_t> counts;
  std::set<CounterKey> overflowedCounters;
  std::vector<FunctionalInstance> functionalInstances;
  std::map<uint64_t, FunctionalType> functionalTypes;
  Summary line;
  Summary toggle;
  Summary functional;
  std::string error;
};

std::string digestText(const Digest &digest) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (uint8_t value : digest) {
    result.push_back(hex[value >> 4]);
    result.push_back(hex[value & 15]);
  }
  return result;
}

void usage(llvm::raw_ostream &out) {
  out << "usage:\n"
         "  obelisk-cov merge -o OUT.obcov [--discard-test-detail] INPUT...\n"
         "  obelisk-cov report --format=text|html|json|lcov [-o OUT]\n"
         "      [--source-root DIR] [--test GLOB] [--fail-under METRIC=N] "
         "INPUT...\n"
         "  obelisk-cov inspect INPUT\n";
}

bool wildcard(const char *pattern, const char *text) {
  const char *star = nullptr, *retry = nullptr;
  while (*text) {
    if (*pattern == '?' || *pattern == *text) {
      ++pattern;
      ++text;
    } else if (*pattern == '*') {
      while (*pattern == '*')
        ++pattern;
      star = pattern;
      retry = text;
    } else if (star) {
      pattern = star;
      text = ++retry;
    } else {
      return false;
    }
  }
  while (*pattern == '*')
    ++pattern;
  return !*pattern;
}

bool parseDouble(const std::string &text, double &value) {
  return !llvm::StringRef(text).getAsDouble(value) && std::isfinite(value) &&
         value >= 0.0 && value <= 100.0;
}

bool takeValue(int &index, int argc, char **argv, const std::string &arg,
               const char *option, std::string &value) {
  std::string prefix = std::string(option) + "=";
  if (arg.compare(0, prefix.size(), prefix) == 0) {
    value = arg.substr(prefix.size());
    return !value.empty();
  }
  if (arg == option && index + 1 < argc) {
    value = argv[++index];
    return true;
  }
  return false;
}

bool parseOptions(int argc, char **argv, Options &options) {
  if (argc < 2)
    return false;
  options.command = argv[1];
  if (options.command != "merge" && options.command != "report" &&
      options.command != "inspect")
    return false;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i], value;
    if (arg == "--discard-test-detail") {
      options.discard = true;
    } else if (takeValue(i, argc, argv, arg, "-o", value) ||
               takeValue(i, argc, argv, arg, "--output", value)) {
      options.output = value;
    } else if (takeValue(i, argc, argv, arg, "--format", value)) {
      options.format = value;
    } else if (takeValue(i, argc, argv, arg, "--test", value)) {
      options.test = value;
    } else if (takeValue(i, argc, argv, arg, "--source-root", value)) {
      options.sourceRoot = value;
    } else if (takeValue(i, argc, argv, arg, "--fail-under", value)) {
      size_t equal = value.find('=');
      MetricKind metric;
      double threshold = 0;
      if (equal == std::string::npos ||
          !parseMetric(value.substr(0, equal), metric) ||
          (metric != MetricKind::Line && metric != MetricKind::Toggle &&
           metric != MetricKind::Functional) ||
          !parseDouble(value.substr(equal + 1), threshold))
        return false;
      options.thresholds[metric] = threshold;
    } else if (arg == "--help" || arg == "-h") {
      usage(llvm::outs());
      std::exit(0);
    } else if (!arg.empty() && arg[0] == '-') {
      return false;
    } else {
      options.inputs.push_back(arg);
    }
  }
  if (options.inputs.empty() ||
      (options.command == "inspect" && options.inputs.size() != 1))
    return false;
  if (options.command == "merge" && options.output.empty())
    return false;
  static const std::set<std::string> formats = {"text", "html", "json", "lcov"};
  return options.command != "report" || formats.count(options.format);
}

void printDiagnostic(const std::string &path, const Diagnostic &diagnostic) {
  llvm::errs() << "obelisk-cov: " << path << ": "
               << statusName(diagnostic.status);
  if (diagnostic.field)
    llvm::errs() << " in " << diagnostic.field;
  if (!diagnostic.detail.empty())
    llvm::errs() << ": " << diagnostic.detail;
  if (diagnostic.offset)
    llvm::errs() << " (offset " << diagnostic.offset << ')';
  llvm::errs() << '\n';
}

bool load(const Options &options, Database &database) {
  Diagnostic diagnostic;
  if (readFile(options.inputs.front(), database, {}, &diagnostic) !=
      Status::Ok) {
    printDiagnostic(options.inputs.front(), diagnostic);
    return false;
  }
  for (size_t i = 1; i != options.inputs.size(); ++i) {
    Database next;
    if (readFile(options.inputs[i], next, {}, &diagnostic) != Status::Ok) {
      printDiagnostic(options.inputs[i], diagnostic);
      return false;
    }
    Status status = merge(database, next, false, &diagnostic);
    if (status != Status::Ok) {
      printDiagnostic(options.inputs[i], diagnostic);
      return false;
    }
  }
  return true;
}

uint64_t saturatingAdd(uint64_t left, uint64_t right) {
  return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

double unsignedRatio(const llvm::APInt &numerator,
                     const llvm::APInt &denominator) {
  if (numerator.isZero() || denominator.isZero())
    return 0.0;
  const unsigned numeratorBits = numerator.getActiveBits();
  const unsigned denominatorBits = denominator.getActiveBits();
  const unsigned numeratorShift = numeratorBits > 53 ? numeratorBits - 53 : 0;
  const unsigned denominatorShift =
      denominatorBits > 53 ? denominatorBits - 53 : 0;
  const uint64_t numeratorMantissa =
      numerator.lshr(numeratorShift).getZExtValue();
  const uint64_t denominatorMantissa =
      denominator.lshr(denominatorShift).getZExtValue();
  return (static_cast<double>(numeratorMantissa) /
          static_cast<double>(denominatorMantissa)) *
         std::ldexp(1.0, static_cast<int>(numeratorShift) -
                             static_cast<int>(denominatorShift));
}

// Keep symbolic automatic-cross arithmetic bounded independently of the
// physical input size. The deliberately deep cross regression has a unit
// denominator, so it tests traversal depth independently of this one-Kibit
// arithmetic limit.
constexpr unsigned maxReporterAPIntBits = 1024;
static_assert(maxReporterAPIntBits <= ParseLimits{}.maxRecords);

std::string integerText(const llvm::APInt &value) {
  llvm::SmallString<32> text;
  value.toString(text, 10, false, false);
  return std::string(text);
}

std::optional<llvm::APInt> automaticBinCount(const Database &database,
                                             const ResolvedCrossPlan &plan) {
  if (!plan.automaticBinCountLimbCount)
    return llvm::APInt(1, 0);
  if (plan.automaticBinCountLimbCount > maxReporterAPIntBits / 64 ||
      plan.automaticBinCountLimbCount >
          std::numeric_limits<unsigned>::max() / 64)
    return std::nullopt;
  return llvm::APInt(
      static_cast<unsigned>(plan.automaticBinCountLimbCount * 64),
      llvm::ArrayRef<uint64_t>(database.resolvedCrossAutomaticBinCountLimbs)
          .slice(plan.firstAutomaticBinCountLimb,
                 plan.automaticBinCountLimbCount));
}

llvm::APInt addUnsigned(const llvm::APInt &left, const llvm::APInt &right) {
  unsigned width = std::max(left.getActiveBits(), right.getActiveBits()) + 1;
  width = std::max(width, 1u);
  return left.zextOrTrunc(width) + right.zextOrTrunc(width);
}

llvm::APInt addUnsigned(const llvm::APInt &left, uint64_t right) {
  return addUnsigned(left, llvm::APInt(64, right));
}

std::optional<llvm::APInt> checkedAddUnsigned(const llvm::APInt &left,
                                              const llvm::APInt &right) {
  if (left.getActiveBits() > maxReporterAPIntBits ||
      right.getActiveBits() > maxReporterAPIntBits)
    return std::nullopt;
  llvm::APInt lhs = left.zextOrTrunc(maxReporterAPIntBits);
  llvm::APInt rhs = right.zextOrTrunc(maxReporterAPIntBits);
  llvm::APInt maximum = llvm::APInt::getAllOnes(maxReporterAPIntBits);
  if (lhs.ugt(maximum - rhs))
    return std::nullopt;
  llvm::APInt result = lhs + rhs;
  return result.trunc(std::max(1u, result.getActiveBits()));
}

std::optional<llvm::APInt> checkedMultiplyUnsigned(const llvm::APInt &left,
                                                   uint64_t right) {
  if (!right || left.isZero())
    return llvm::APInt(1, 0);
  if (left.getActiveBits() > maxReporterAPIntBits)
    return std::nullopt;
  llvm::APInt lhs = left.zextOrTrunc(maxReporterAPIntBits);
  llvm::APInt rhs(maxReporterAPIntBits, right);
  llvm::APInt maximum = llvm::APInt::getAllOnes(maxReporterAPIntBits);
  if (lhs.ugt(maximum.udiv(rhs)))
    return std::nullopt;
  llvm::APInt result = lhs * rhs;
  return result.trunc(std::max(1u, result.getActiveBits()));
}

using AutomaticNodeKey = std::tuple<uint64_t, Digest, uint64_t, uint64_t>;

std::optional<llvm::APInt>
countAutomaticGraphUnion(const Database &database,
                         std::vector<AutomaticNodeKey> roots) {
  std::map<AutomaticNodeKey, const ResolvedCrossAutomaticNode *> nodes;
  for (const auto &node : database.resolvedCrossAutomaticNodes)
    nodes.emplace(
        AutomaticNodeKey{node.type, node.configuration, node.cross, node.id},
        &node);
  std::map<std::tuple<uint64_t, Digest, uint64_t>, std::string> binNames;
  for (const auto &bin : database.resolvedFunctionalBins)
    binNames.emplace(std::make_tuple(bin.type, bin.configuration, bin.id),
                     bin.name);
  std::sort(roots.begin(), roots.end());
  roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
  struct Successor {
    bool accepts = false;
    std::vector<AutomaticNodeKey> nodes;
    bool operator<(const Successor &other) const {
      return std::tie(accepts, nodes) < std::tie(other.accepts, other.nodes);
    }
  };
  using State = std::vector<AutomaticNodeKey>;
  using Transitions = std::vector<std::pair<Successor, uint64_t>>;
  std::map<State, Transitions> graph;
  std::vector<State> worklist;
  std::vector<State> states;
  if (!roots.empty())
    worklist.push_back(roots);
  const uint64_t limit = ParseLimits{}.maxRecords;
  uint64_t transitionCount = 0;
  while (!worklist.empty()) {
    State active = std::move(worklist.back());
    worklist.pop_back();
    if (graph.count(active))
      continue;
    if (graph.size() >= limit || active.size() > limit)
      return std::nullopt;
    std::map<std::string, Successor> byName;
    for (const AutomaticNodeKey &key : active) {
      auto foundNode = nodes.find(key);
      if (foundNode == nodes.end())
        return std::nullopt;
      const auto *node = foundNode->second;
      for (uint32_t ordinal = 0; ordinal != node->edgeCount; ++ordinal) {
        const auto &edge =
            database.resolvedCrossAutomaticEdges[node->firstEdge + ordinal];
        auto foundName =
            binNames.find({node->type, node->configuration, edge.bin});
        if (foundName == binNames.end())
          return std::nullopt;
        auto &successor = byName[foundName->second];
        if (edge.child)
          successor.nodes.emplace_back(node->type, node->configuration,
                                       node->cross, edge.child);
        else
          successor.accepts = true;
      }
    }
    std::map<Successor, uint64_t> equivalentNames;
    for (auto &[name, successor] : byName) {
      std::sort(successor.nodes.begin(), successor.nodes.end());
      successor.nodes.erase(
          std::unique(successor.nodes.begin(), successor.nodes.end()),
          successor.nodes.end());
      ++equivalentNames[successor];
    }
    if (equivalentNames.size() > limit - transitionCount)
      return std::nullopt;
    transitionCount += equivalentNames.size();
    Transitions transitions(equivalentNames.begin(), equivalentNames.end());
    for (const auto &[successor, multiplicity] : transitions) {
      (void)multiplicity;
      if (!successor.accepts)
        worklist.push_back(successor.nodes);
    }
    states.push_back(active);
    graph.emplace(std::move(active), std::move(transitions));
  }
  std::sort(states.begin(), states.end(), [&](const State &a, const State &b) {
    return nodes.at(a.front())->targetOrdinal >
           nodes.at(b.front())->targetOrdinal;
  });
  std::map<State, llvm::APInt> counts;
  for (const State &state : states) {
    llvm::APInt total(1, 0);
    for (const auto &[successor, multiplicity] : graph.at(state)) {
      llvm::APInt suffix(1, 1);
      if (!successor.accepts) {
        auto found = counts.find(successor.nodes);
        if (found == counts.end())
          return std::nullopt;
        suffix = found->second;
      }
      auto product = checkedMultiplyUnsigned(suffix, multiplicity);
      if (!product)
        return std::nullopt;
      auto sum = checkedAddUnsigned(total, *product);
      if (!sum)
        return std::nullopt;
      total = std::move(*sum);
    }
    counts.emplace(state, std::move(total));
  }
  if (roots.empty())
    return llvm::APInt(1, 0);
  auto result = counts.find(roots);
  return result == counts.end() ? std::nullopt
                                : std::optional<llvm::APInt>(result->second);
}

bool automaticGraphContains(const Database &database, AutomaticNodeKey root,
                            const std::vector<std::string> &components) {
  for (size_t dimension = 0; dimension != components.size(); ++dimension) {
    auto node = std::find_if(
        database.resolvedCrossAutomaticNodes.begin(),
        database.resolvedCrossAutomaticNodes.end(), [&](const auto &value) {
          return AutomaticNodeKey{value.type, value.configuration, value.cross,
                                  value.id} == root;
        });
    if (node == database.resolvedCrossAutomaticNodes.end())
      return false;
    const ResolvedCrossAutomaticEdge *matching = nullptr;
    for (uint32_t ordinal = 0; ordinal != node->edgeCount; ++ordinal) {
      const auto &edge =
          database.resolvedCrossAutomaticEdges[node->firstEdge + ordinal];
      auto bin = std::find_if(
          database.resolvedFunctionalBins.begin(),
          database.resolvedFunctionalBins.end(), [&](const auto &value) {
            return value.type == node->type &&
                   value.configuration == node->configuration &&
                   value.id == edge.bin;
          });
      if (bin != database.resolvedFunctionalBins.end() &&
          bin->name == components[dimension]) {
        matching = &edge;
        break;
      }
    }
    if (!matching)
      return false;
    if (dimension + 1 == components.size())
      return matching->child == 0;
    if (!matching->child)
      return false;
    std::get<3>(root) = matching->child;
  }
  return false;
}

bool appendMissingAutomaticBins(
    const Database &database, AutomaticNodeKey root,
    const std::set<std::vector<uint64_t>> &materialized, uint64_t limit,
    uint64_t atLeast,
    std::vector<ReportData::FunctionalItem::AutomaticBin> &bins,
    std::string &error) {
  if (!limit)
    return true;
  if (limit > ParseLimits{}.maxRecords) {
    error = "cross_num_print_missing exceeds reporter record limit";
    return false;
  }

  std::map<AutomaticNodeKey, const ResolvedCrossAutomaticNode *> nodes;
  for (const auto &node : database.resolvedCrossAutomaticNodes)
    nodes.emplace(
        AutomaticNodeKey{node.type, node.configuration, node.cross, node.id},
        &node);
  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalBin *>
      resolvedBins;
  for (const auto &bin : database.resolvedFunctionalBins)
    resolvedBins.emplace(std::make_tuple(bin.type, bin.configuration, bin.id),
                         &bin);

  struct Frame {
    AutomaticNodeKey key;
    uint32_t nextEdge = 0;
  };
  std::vector<Frame> stack{{root, 0}};
  std::vector<uint64_t> components;
  std::vector<std::string> componentNames;
  uint64_t visitedEdges = 0;
  uint64_t appended = 0;
  while (!stack.empty() && appended != limit) {
    Frame &frame = stack.back();
    auto foundNode = nodes.find(frame.key);
    if (foundNode == nodes.end()) {
      error = "missing automatic cross node while enumerating missing bins";
      return false;
    }
    const auto *node = foundNode->second;
    if (frame.nextEdge == node->edgeCount) {
      stack.pop_back();
      if (!stack.empty()) {
        components.pop_back();
        componentNames.pop_back();
      }
      continue;
    }
    if (visitedEdges == ParseLimits{}.maxRecords) {
      error = "automatic cross missing-bin traversal exceeds reporter limit";
      return false;
    }
    const auto &edge =
        database
            .resolvedCrossAutomaticEdges[node->firstEdge + frame.nextEdge++];
    ++visitedEdges;
    auto foundBin =
        resolvedBins.find({node->type, node->configuration, edge.bin});
    if (foundBin == resolvedBins.end()) {
      error = "missing automatic cross component while enumerating missing "
              "bins";
      return false;
    }
    components.push_back(edge.bin);
    componentNames.push_back(foundBin->second->name);
    if (edge.child) {
      stack.push_back({AutomaticNodeKey{node->type, node->configuration,
                                        node->cross, edge.child},
                       0});
      continue;
    }
    if (!materialized.count(components)) {
      ReportData::FunctionalItem::AutomaticBin bin;
      bin.name = "<";
      for (size_t ordinal = 0; ordinal != componentNames.size(); ++ordinal) {
        if (ordinal)
          bin.name += ',';
        bin.name += componentNames[ordinal];
      }
      bin.name += '>';
      bin.components = components;
      bin.componentNames = componentNames;
      bin.atLeast = atLeast;
      bin.missing = true;
      bins.push_back(std::move(bin));
      ++appended;
    }
    components.pop_back();
    componentNames.pop_back();
  }
  return true;
}

std::string summaryCountText(const Summary &summary, bool covered) {
  if (!summary.hasExactCounts)
    return std::to_string(covered ? summary.covered : summary.total);
  return integerText(covered ? summary.exactCovered : summary.exactTotal);
}

using ConfigurationKey = std::pair<uint64_t, Digest>;

uint64_t configurationOption(const Database &database,
                             const ConfigurationKey &configuration,
                             uint64_t owner,
                             FunctionalConfigurationOptionOwnerKind ownerKind,
                             FunctionalOptionScopeKind scope,
                             FunctionalConfigurationOptionKind option,
                             uint64_t fallback) {
  for (const FunctionalConfigurationOption &value :
       database.functionalConfigurationOptions)
    if (value.type == configuration.first &&
        value.configuration == configuration.second && value.owner == owner &&
        value.ownerKind == ownerKind && value.scope == scope &&
        value.option == option)
      return value.value;
  return fallback;
}

std::string configurationStringOption(
    const Database &database, const ConfigurationKey &configuration,
    uint64_t owner, FunctionalConfigurationOptionOwnerKind ownerKind,
    FunctionalOptionScopeKind scope, FunctionalConfigurationOptionKind option) {
  for (const FunctionalConfigurationOption &value :
       database.functionalConfigurationOptions)
    if (value.type == configuration.first &&
        value.configuration == configuration.second && value.owner == owner &&
        value.ownerKind == ownerKind && value.scope == scope &&
        value.option == option)
      return value.stringValue;
  return {};
}

double applyGoal(double percentage, uint64_t goal) {
  if (!goal)
    return 100.0;
  return std::min(100.0, percentage * 100.0 / static_cast<double>(goal));
}

bool functionalBinContributes(const ResolvedFunctionalBin &bin,
                              const std::set<uint64_t> &excludedTemplates) {
  constexpr uint32_t nonContributing =
      FunctionalBinDefault | FunctionalBinDefaultSequence |
      FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
  return !(bin.flags & nonContributing) &&
         (!bin.templateBin || !excludedTemplates.count(bin.templateBin));
}

ReportData analyze(const Database &database, const std::string &test) {
  ReportData report;
  for (const Run &run : database.runs)
    if (wildcard(test.c_str(), run.name.c_str()))
      report.runs.insert(run.uuid);
  for (const Counter &counter : database.counters)
    if (report.runs.count(counter.run)) {
      auto key = std::make_tuple(counter.run, counter.metric, counter.entity,
                                 counter.instance, counter.subindex);
      uint64_t &value = report.counts[key];
      if (counter.value > UINT64_MAX - value || (counter.flags & 1))
        report.overflowedCounters.insert(key);
      value = saturatingAdd(value, counter.value);
    }
  std::set<std::pair<MetricKind, uint64_t>> excluded;
  for (const Exclusion &value : database.exclusions)
    excluded.emplace(value.metric, value.entity);
  auto aggregate = [&](MetricKind metric, uint64_t entity, uint32_t subindex) {
    uint64_t result = 0;
    for (const UUID &run : report.runs) {
      auto found = report.counts.find(
          std::make_tuple(run, metric, entity, uint64_t{0}, subindex));
      if (found != report.counts.end())
        result = saturatingAdd(result, found->second);
    }
    return result;
  };

  report.line.available = !database.linePoints.empty();
  std::map<std::pair<uint64_t, uint32_t>, std::vector<const LinePoint *>> lines;
  for (const LinePoint &point : database.linePoints)
    if (!excluded.count({MetricKind::Line, point.id}))
      lines[{point.file, point.line}].push_back(&point);
  for (const auto &line : lines) {
    uint64_t hit = 0;
    for (const LinePoint *point : line.second)
      hit += aggregate(MetricKind::Line, point->id, 0) != 0;
    ++report.line.total;
    if (hit == line.second.size())
      ++report.line.covered;
    else if (hit)
      ++report.line.partial;
  }

  report.toggle.available = !database.toggleObjects.empty();
  for (const ToggleObject &object : database.toggleObjects)
    if (!excluded.count({MetricKind::Toggle, object.id}))
      for (uint64_t bit = 0; bit != object.bitWidth; ++bit)
        for (uint32_t direction = 0; direction != 2; ++direction) {
          ++report.toggle.total;
          if (aggregate(MetricKind::Toggle, object.id,
                        static_cast<uint32_t>(bit * 4 + direction)))
            ++report.toggle.covered;
        }

  report.functional.available =
      !database.functionalTypes.empty() &&
      llvm::any_of(database.runs, [](const Run &run) {
        return (run.flags & RunContainsFunctional) != 0;
      });
  for (const auto &type : database.functionalTypes)
    report.functionalTypes[type.id].type = type.id;

  std::map<ConfigurationKey, std::vector<const ResolvedFunctionalItem *>>
      resolvedItems;
  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           std::vector<const ResolvedFunctionalBin *>>
      resolvedBins;
  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalBin *>
      resolvedBinsByID;
  std::map<ConfigurationKey, std::set<uint64_t>> resolvedBinIDs;
  for (const ResolvedFunctionalItem &item : database.resolvedFunctionalItems)
    resolvedItems[{item.type, item.configuration}].push_back(&item);
  for (const ResolvedFunctionalBin &bin : database.resolvedFunctionalBins) {
    ConfigurationKey key{bin.type, bin.configuration};
    resolvedBins[{bin.type, bin.configuration, bin.item}].push_back(&bin);
    resolvedBinsByID[{bin.type, bin.configuration, bin.id}] = &bin;
    resolvedBinIDs[key].insert(bin.id);
  }
  std::set<uint64_t> excludedFunctionalTemplates;
  for (const Exclusion &value : database.exclusions)
    if (value.metric == MetricKind::Functional)
      excludedFunctionalTemplates.insert(value.entity);

  struct TypeOptionProfile {
    uint64_t goal = 100;
    uint64_t weight = 1;
    bool mergeInstances = false;
    std::string comment;
    std::map<uint64_t, uint64_t> itemGoals;
    std::map<uint64_t, uint64_t> itemWeights;
    std::map<uint64_t, std::string> itemComments;
  };
  std::map<std::pair<UUID, uint64_t>, TypeOptionProfile> runTypeOptions;
  std::map<uint64_t, TypeOptionProfile> typeOptions;
  auto findTemplateItem = [&](uint64_t id) -> const FunctionalItem * {
    auto found = std::find_if(database.functionalItems.begin(),
                              database.functionalItems.end(),
                              [&](const auto &item) { return item.id == id; });
    return found == database.functionalItems.end() ? nullptr : &*found;
  };
  auto conflict = [&](llvm::StringRef what, uint64_t type) {
    report.error = "conflicting " + what.str() +
                   " for functional type " + std::to_string(type);
  };

  // A procedural type_option update is run-local execution state. Seed an
  // effective profile for every selected {run,type} from that run's resolved
  // static configuration before applying its overrides. This prevents an
  // override from one retained test from being silently applied to another
  // test which retained the static default.
  for (const ResolvedInstance &instance : database.resolvedInstances) {
    if (!report.runs.count(instance.run))
      continue;
    ConfigurationKey configuration{instance.type, instance.configuration};
    TypeOptionProfile profile;
    profile.goal = configurationOption(
        database, configuration, instance.type,
        FunctionalConfigurationOptionOwnerKind::Group,
        FunctionalOptionScopeKind::Type,
        FunctionalConfigurationOptionKind::Goal, 100);
    profile.weight = configurationOption(
        database, configuration, instance.type,
        FunctionalConfigurationOptionOwnerKind::Group,
        FunctionalOptionScopeKind::Type,
        FunctionalConfigurationOptionKind::Weight, 1);
    profile.mergeInstances =
        configurationOption(database, configuration, instance.type,
                            FunctionalConfigurationOptionOwnerKind::Group,
                            FunctionalOptionScopeKind::Type,
                            FunctionalConfigurationOptionKind::MergeInstances,
                            0) != 0;
    profile.comment = configurationStringOption(
        database, configuration, instance.type,
        FunctionalConfigurationOptionOwnerKind::Group,
        FunctionalOptionScopeKind::Type,
        FunctionalConfigurationOptionKind::Comment);
    for (const ResolvedFunctionalItem *item : resolvedItems[configuration]) {
      profile.itemGoals[item->templateItem] = configurationOption(
          database, configuration, item->id,
          FunctionalConfigurationOptionOwnerKind::Item,
          FunctionalOptionScopeKind::Type,
          FunctionalConfigurationOptionKind::Goal, 100);
      profile.itemWeights[item->templateItem] = configurationOption(
          database, configuration, item->id,
          FunctionalConfigurationOptionOwnerKind::Item,
          FunctionalOptionScopeKind::Type,
          FunctionalConfigurationOptionKind::Weight, 1);
      profile.itemComments[item->templateItem] = configurationStringOption(
          database, configuration, item->id,
          FunctionalConfigurationOptionOwnerKind::Item,
          FunctionalOptionScopeKind::Type,
          FunctionalConfigurationOptionKind::Comment);
    }
    auto [position, inserted] =
        runTypeOptions.emplace(std::make_pair(instance.run, instance.type),
                               profile);
    if (!inserted &&
        (position->second.goal != profile.goal ||
         position->second.weight != profile.weight ||
         position->second.mergeInstances != profile.mergeInstances ||
         position->second.comment != profile.comment ||
         position->second.itemGoals != profile.itemGoals ||
         position->second.itemWeights != profile.itemWeights ||
         position->second.itemComments != profile.itemComments)) {
      report.error = "inconsistent static type options for functional type " +
                     std::to_string(instance.type);
      return report;
    }
  }
  for (const ResolvedInstanceOption &option :
       database.resolvedInstanceOptions) {
    if (option.instance || !report.runs.count(option.run))
      continue;
    const FunctionalItem *item =
        option.ownerKind == FunctionalConfigurationOptionOwnerKind::Item
            ? findTemplateItem(option.owner)
            : nullptr;
    const uint64_t typeID = item ? item->type : option.owner;
    auto profilePosition = runTypeOptions.find({option.run, typeID});
    if (profilePosition == runTypeOptions.end())
      continue;
    TypeOptionProfile &profile = profilePosition->second;
    if (item) {
      if (option.option == FunctionalConfigurationOptionKind::Goal)
        profile.itemGoals[option.owner] = option.value;
      else if (option.option == FunctionalConfigurationOptionKind::Weight)
        profile.itemWeights[option.owner] = option.value;
      else if (option.option == FunctionalConfigurationOptionKind::Comment)
        profile.itemComments[option.owner] = option.stringValue;
    } else if (option.option == FunctionalConfigurationOptionKind::Goal) {
      profile.goal = option.value;
    } else if (option.option == FunctionalConfigurationOptionKind::Weight) {
      profile.weight = option.value;
    } else if (option.option ==
               FunctionalConfigurationOptionKind::MergeInstances) {
      profile.mergeInstances = option.value != 0;
    } else if (option.option == FunctionalConfigurationOptionKind::Comment) {
      profile.comment = option.stringValue;
    }
  }
  for (const auto &[runType, profile] : runTypeOptions) {
    const uint64_t typeID = runType.second;
    auto [position, inserted] = typeOptions.emplace(typeID, profile);
    if (inserted)
      continue;
    const TypeOptionProfile &prior = position->second;
    if (prior.goal != profile.goal)
      conflict("type goal", typeID);
    else if (prior.weight != profile.weight)
      conflict("type weight", typeID);
    else if (prior.mergeInstances != profile.mergeInstances)
      conflict("type merge_instances", typeID);
    else if (prior.comment != profile.comment)
      conflict("type comment", typeID);
    else if (prior.itemGoals != profile.itemGoals)
      conflict("type item goal", typeID);
    else if (prior.itemWeights != profile.itemWeights)
      conflict("type item weight", typeID);
    else if (prior.itemComments != profile.itemComments)
      conflict("type item comment", typeID);
    if (!report.error.empty())
      return report;
  }

  using InstanceGroupKey =
      std::tuple<bool, uint64_t, std::string, Digest, UUID, uint64_t>;
  std::map<InstanceGroupKey, size_t> groups;
  std::map<std::pair<UUID, uint64_t>, size_t> instanceGroups;
  const UUID noRun{};
  for (const ResolvedInstance &instance : database.resolvedInstances) {
    if (!report.runs.count(instance.run))
      continue;
    report.configurations.emplace(instance.type, instance.configuration);
    const bool named = !instance.name.empty() &&
                       !(instance.flags & ResolvedInstanceGeneratedName);
    InstanceGroupKey key = std::make_tuple(
        named, instance.type, named ? instance.name : std::string{},
        named ? instance.configuration : Digest{}, named ? noRun : instance.run,
        named ? 0 : instance.id);
    auto [position, inserted] = groups.emplace(key, groups.size());
    if (inserted) {
      ReportData::FunctionalInstance group;
      group.type = instance.type;
      group.name = instance.name;
      group.configuration = instance.configuration;
      ConfigurationKey configuration{instance.type, instance.configuration};
      group.comment = configurationStringOption(
          database, configuration, instance.type,
          FunctionalConfigurationOptionOwnerKind::Group,
          FunctionalOptionScopeKind::Instance,
          FunctionalConfigurationOptionKind::Comment);
      group.goal =
          configurationOption(database, configuration, instance.type,
                              FunctionalConfigurationOptionOwnerKind::Group,
                              FunctionalOptionScopeKind::Instance,
                              FunctionalConfigurationOptionKind::Goal, 100);
      group.weight =
          configurationOption(database, configuration, instance.type,
                              FunctionalConfigurationOptionOwnerKind::Group,
                              FunctionalOptionScopeKind::Instance,
                              FunctionalConfigurationOptionKind::Weight, 1);
      group.atLeast = configurationOption(
          database, configuration, instance.type,
          FunctionalConfigurationOptionOwnerKind::Group,
          FunctionalOptionScopeKind::Instance,
                              FunctionalConfigurationOptionKind::AtLeast, 1);
      group.groupCrossNumPrintMissing = configurationOption(
          database, configuration, instance.type,
          FunctionalConfigurationOptionOwnerKind::Group,
          FunctionalOptionScopeKind::Instance,
          FunctionalConfigurationOptionKind::CrossNumPrintMissing, 0);
      group.perInstance =
          configurationOption(database, configuration, instance.type,
                              FunctionalConfigurationOptionOwnerKind::Group,
                              FunctionalOptionScopeKind::Instance,
                              FunctionalConfigurationOptionKind::PerInstance,
                              0) != 0;
      report.functionalInstances.push_back(std::move(group));
    }
    size_t index = position->second;
    report.functionalInstances[index].sources.emplace_back(instance.run,
                                                           instance.id);
    instanceGroups.emplace(std::make_pair(instance.run, instance.id), index);
  }
  for (const ResolvedInstanceOption &option :
       database.resolvedInstanceOptions) {
    if (!option.instance || !report.runs.count(option.run))
      continue;
    auto group = instanceGroups.find({option.run, option.instance});
    if (group == instanceGroups.end())
      continue;
    auto &instance = report.functionalInstances[group->second];
    if (option.ownerKind ==
        FunctionalConfigurationOptionOwnerKind::Group) {
      if (option.option == FunctionalConfigurationOptionKind::Goal) {
        instance.goal = instance.hasMutableGoal
                            ? std::max(instance.goal, option.value)
                            : option.value;
        instance.hasMutableGoal = true;
      } else if (option.option == FunctionalConfigurationOptionKind::Weight) {
        instance.weight = instance.hasMutableWeight
                              ? std::max(instance.weight, option.value)
                              : option.value;
        instance.hasMutableWeight = true;
      } else if (option.option == FunctionalConfigurationOptionKind::AtLeast) {
        instance.atLeast = instance.hasMutableAtLeast
                               ? std::max(instance.atLeast, option.value)
                               : option.value;
        instance.hasMutableAtLeast = true;
      } else if (option.option ==
                 FunctionalConfigurationOptionKind::CrossNumPrintMissing) {
        instance.groupCrossNumPrintMissing =
            instance.hasMutableCrossNumPrintMissing
                ? std::max(instance.groupCrossNumPrintMissing, option.value)
                : option.value;
        instance.hasMutableCrossNumPrintMissing = true;
      } else if (option.option == FunctionalConfigurationOptionKind::Comment) {
        instance.comment = instance.hasMutableComment
                               ? std::max(instance.comment, option.stringValue)
                               : option.stringValue;
        instance.hasMutableComment = true;
      }
      continue;
    }
    auto &item = instance.itemOptions[option.owner];
    if (option.option == FunctionalConfigurationOptionKind::Goal)
      item.goal = item.goal ? std::max(*item.goal, option.value) : option.value;
    else if (option.option == FunctionalConfigurationOptionKind::Weight)
      item.weight = item.weight ? std::max(*item.weight, option.value)
                                : option.value;
    else if (option.option == FunctionalConfigurationOptionKind::AtLeast)
      item.atLeast = item.atLeast ? std::max(*item.atLeast, option.value)
                                 : option.value;
    else if (option.option ==
             FunctionalConfigurationOptionKind::CrossNumPrintMissing)
      item.crossNumPrintMissing =
          item.crossNumPrintMissing
              ? std::max(*item.crossNumPrintMissing, option.value)
              : option.value;
    else if (option.option == FunctionalConfigurationOptionKind::Comment)
      item.comment = item.comment
                         ? std::max(*item.comment, option.stringValue)
                         : option.stringValue;
  }
  for (const ResolvedCrossPlan &plan : database.resolvedCrossPlans)
    if (report.configurations.count({plan.type, plan.configuration}) &&
        !automaticBinCount(database, plan)) {
      report.error = "automatic cross denominator exceeds APInt width limit";
      return report;
    }
  for (const Counter &counter : database.counters) {
    if (counter.metric != MetricKind::Functional ||
        !report.runs.count(counter.run))
      continue;
    auto group = instanceGroups.find({counter.run, counter.instance});
    if (group == instanceGroups.end())
      continue;
    const auto &instance = report.functionalInstances[group->second];
    if (!resolvedBinIDs[{instance.type, instance.configuration}].count(
            counter.entity))
      continue;
    auto &count =
        report.functionalInstances[group->second].binCounts[counter.entity];
    if (counter.value > UINT64_MAX - count.count || (counter.flags & 1))
      count.overflow = true;
    count.count = saturatingAdd(count.count, counter.value);
    count.contributors.push_back(
        {counter.run, counter.value, (counter.flags & 1) != 0});
  }
  for (const SparseCrossTuple &tuple : database.sparseCrossTuples) {
    if (!report.runs.count(tuple.run))
      continue;
    auto group = instanceGroups.find({tuple.run, tuple.instance});
    if (group == instanceGroups.end())
      continue;
    std::vector<uint64_t> components(
        database.sparseCrossTupleComponents.begin() + tuple.firstComponent,
        database.sparseCrossTupleComponents.begin() + tuple.firstComponent +
            tuple.componentCount);
    auto &count = report.functionalInstances[group->second]
                      .sparseCrossCounts[tuple.cross][components];
    if (tuple.hits > UINT64_MAX - count.count ||
        (tuple.flags & SparseCrossTupleOverflow))
      count.overflow = true;
    count.count = saturatingAdd(count.count, tuple.hits);
    count.contributors.push_back(
        {tuple.run, tuple.hits, (tuple.flags & SparseCrossTupleOverflow) != 0});
  }

  for (size_t instanceIndex = 0;
       instanceIndex != report.functionalInstances.size(); ++instanceIndex) {
    ReportData::FunctionalInstance &instance =
        report.functionalInstances[instanceIndex];
    ConfigurationKey configuration{instance.type, instance.configuration};
    double weighted = 0.0;
    uint64_t totalWeight = 0;
    for (const ResolvedFunctionalItem *schema : resolvedItems[configuration]) {
      ReportData::FunctionalItem item;
      item.id = schema->id;
      item.templateItem = schema->templateItem;
      item.name = schema->name;
      item.comment = configurationStringOption(
          database, configuration, schema->id,
          FunctionalConfigurationOptionOwnerKind::Item,
          FunctionalOptionScopeKind::Instance,
          FunctionalConfigurationOptionKind::Comment);
      item.typeComment = configurationStringOption(
          database, configuration, schema->id,
          FunctionalConfigurationOptionOwnerKind::Item,
          FunctionalOptionScopeKind::Type,
          FunctionalConfigurationOptionKind::Comment);
      item.hierarchy = schema->hierarchy;
      item.kind = schema->kind;
      item.aggregating = (schema->flags & FunctionalItemNonAggregating) == 0;
      item.goal = schema->goal;
      item.weight = schema->weight;
      item.typeGoal =
          configurationOption(database, configuration, schema->id,
                              FunctionalConfigurationOptionOwnerKind::Item,
                              FunctionalOptionScopeKind::Type,
                              FunctionalConfigurationOptionKind::Goal, 100);
      item.typeWeight =
          configurationOption(database, configuration, schema->id,
                              FunctionalConfigurationOptionOwnerKind::Item,
                              FunctionalOptionScopeKind::Type,
                              FunctionalConfigurationOptionKind::Weight, 1);
      if (auto typeProfile = typeOptions.find(instance.type);
          typeProfile != typeOptions.end()) {
        if (auto goal = typeProfile->second.itemGoals.find(item.templateItem);
            goal != typeProfile->second.itemGoals.end())
          item.typeGoal = goal->second;
        if (auto weight =
                typeProfile->second.itemWeights.find(item.templateItem);
            weight != typeProfile->second.itemWeights.end())
          item.typeWeight = weight->second;
        if (auto comment =
                typeProfile->second.itemComments.find(item.templateItem);
            comment != typeProfile->second.itemComments.end())
          item.typeComment = comment->second;
      }
      item.ordinal = schema->ordinal;
      auto mutableOptions = instance.itemOptions.find(item.templateItem);
      if (mutableOptions != instance.itemOptions.end()) {
        if (mutableOptions->second.goal)
          item.goal = *mutableOptions->second.goal;
        if (mutableOptions->second.weight)
          item.weight = static_cast<uint32_t>(*mutableOptions->second.weight);
        if (mutableOptions->second.comment)
          item.comment = *mutableOptions->second.comment;
      }
      const uint64_t configuredGroupAtLeast = configurationOption(
          database, configuration, instance.type,
          FunctionalConfigurationOptionOwnerKind::Group,
          FunctionalOptionScopeKind::Instance,
          FunctionalConfigurationOptionKind::AtLeast, 1);
      const uint64_t configuredItemAtLeast = configurationOption(
          database, configuration, schema->id,
          FunctionalConfigurationOptionOwnerKind::Item,
          FunctionalOptionScopeKind::Instance,
          FunctionalConfigurationOptionKind::AtLeast,
          configuredGroupAtLeast);
      const bool hasConfiguredItemAtLeast = std::any_of(
          database.functionalConfigurationOptions.begin(),
          database.functionalConfigurationOptions.end(),
          [&](const FunctionalConfigurationOption &option) {
            return option.type == instance.type &&
                   option.configuration == instance.configuration &&
                   option.owner == schema->id &&
                   option.ownerKind ==
                       FunctionalConfigurationOptionOwnerKind::Item &&
                   option.scope == FunctionalOptionScopeKind::Instance &&
                   option.option ==
                       FunctionalConfigurationOptionKind::AtLeast;
          });
      const uint64_t instanceAtLeast =
          mutableOptions != instance.itemOptions.end() &&
                  mutableOptions->second.atLeast
              ? *mutableOptions->second.atLeast
          : instance.atLeast != configuredGroupAtLeast &&
                  !hasConfiguredItemAtLeast
              ? instance.atLeast
              : configuredItemAtLeast;
      item.atLeast = instanceAtLeast;
      const bool hasMutableAtLeast =
          (mutableOptions != instance.itemOptions.end() &&
           mutableOptions->second.atLeast) ||
          instance.atLeast != configuredGroupAtLeast;
      for (const ResolvedFunctionalBin *binSchema :
           resolvedBins[{instance.type, instance.configuration, schema->id}]) {
        ReportData::FunctionalBin bin;
        bin.id = binSchema->id;
        bin.templateBin = binSchema->templateBin;
        bin.name = binSchema->name;
        bin.hierarchy = binSchema->hierarchy;
        bin.kind = binSchema->kind;
        bin.flags = binSchema->flags;
        bin.ordinal = binSchema->ordinal;
        bin.expansionOrdinal = binSchema->expansionOrdinal;
        bin.atLeast = binSchema->atLeast;
        if (hasMutableAtLeast)
          bin.atLeast = instanceAtLeast;
        auto count = instance.binCounts.find(bin.id);
        if (count != instance.binCounts.end()) {
          bin.count = count->second.count;
          bin.overflow = count->second.overflow;
          bin.contributors = count->second.contributors;
        }
        bin.excluded =
            !functionalBinContributes(*binSchema, excludedFunctionalTemplates);
        bin.covered = !bin.excluded && bin.count >= bin.atLeast;
        if (!bin.excluded) {
          item.covered += bin.covered;
          ++item.total;
        }
        item.bins.push_back(std::move(bin));
      }
      item.coveredWide = llvm::APInt(64, item.covered);
      item.totalWide = llvm::APInt(64, item.total);
      if (item.kind == FunctionalItemKind::Cross) {
        auto staticPlan =
            std::find_if(database.crossPlans.begin(), database.crossPlans.end(),
                         [&](const CrossPlan &plan) {
                           return plan.item == item.templateItem;
                         });
        auto resolvedPlan = std::find_if(database.resolvedCrossPlans.begin(),
                                         database.resolvedCrossPlans.end(),
                                         [&](const ResolvedCrossPlan &plan) {
                                           return plan.type == instance.type &&
                                                  plan.configuration ==
                                                      instance.configuration &&
                                                  plan.cross == item.id;
                                         });
        if (staticPlan == database.crossPlans.end() ||
            resolvedPlan == database.resolvedCrossPlans.end()) {
          report.error = "missing resolved cross plan for functional item " +
                         std::to_string(item.id);
          return report;
        }
        if (resolvedPlan->retainAutoPolicy == CrossRetainAutoPolicy::Retain) {
          item.automaticTotal = *automaticBinCount(database, *resolvedPlan);
          item.automaticRoot = {instance.type, instance.configuration, item.id,
                                resolvedPlan->rootNode};
          uint64_t atLeast = instanceAtLeast;
          item.automaticAtLeast = atLeast;
          item.crossNumPrintMissing = configurationOption(
              database, configuration, item.id,
              FunctionalConfigurationOptionOwnerKind::Item,
              FunctionalOptionScopeKind::Instance,
              FunctionalConfigurationOptionKind::CrossNumPrintMissing,
              instance.groupCrossNumPrintMissing);
          if (mutableOptions != instance.itemOptions.end() &&
              mutableOptions->second.crossNumPrintMissing)
            item.crossNumPrintMissing =
                *mutableOptions->second.crossNumPrintMissing;
          std::set<std::vector<uint64_t>> materialized;
          auto tuples = instance.sparseCrossCounts.find(item.templateItem);
          if (tuples != instance.sparseCrossCounts.end())
            for (const auto &[components, count] : tuples->second) {
              materialized.insert(components);
              ReportData::FunctionalItem::AutomaticBin bin;
              bin.components = components;
              bin.atLeast = atLeast;
              bin.count = count.count;
              bin.covered = count.count >= atLeast;
              bin.overflow = count.overflow;
              bin.contributors = count.contributors;
              bin.name = "<";
              for (size_t ordinal = 0; ordinal != components.size();
                   ++ordinal) {
                auto component = resolvedBinsByID.find({instance.type,
                                                        instance.configuration,
                                                        components[ordinal]});
                if (component == resolvedBinsByID.end()) {
                  report.error =
                      "unknown sparse cross component for functional item " +
                      std::to_string(item.id);
                  return report;
                }
                if (ordinal)
                  bin.name += ',';
                bin.componentNames.push_back(component->second->name);
                bin.name += component->second->name;
              }
              bin.name += '>';
              item.automaticCovered += bin.covered;
              item.automaticBins.push_back(std::move(bin));
            }
          unsigned compareWidth =
              std::max(64u, item.automaticTotal.getBitWidth());
          item.automaticCoveredWide =
              atLeast ? llvm::APInt(compareWidth, item.automaticCovered)
                      : item.automaticTotal.zextOrTrunc(compareWidth);
          if (item.automaticTotal.zextOrTrunc(compareWidth)
                  .ult(item.automaticCoveredWide)) {
            report.error = "sparse cross hits exceed automatic denominator "
                           "for functional item " +
                           std::to_string(item.id);
            return report;
          }
          llvm::APInt materializedCount(compareWidth, materialized.size());
          llvm::APInt automaticTotal =
              item.automaticTotal.zextOrTrunc(compareWidth);
          if (automaticTotal.ult(materializedCount)) {
            report.error = "sparse cross tuples exceed automatic denominator "
                           "for functional item " +
                           std::to_string(item.id);
            return report;
          }
          if (atLeast && automaticTotal.ugt(materializedCount)) {
            uint64_t printMissing = item.crossNumPrintMissing;
            llvm::APInt missingCount = automaticTotal - materializedCount;
            if (missingCount.getActiveBits() <= 64)
              printMissing =
                  std::min(printMissing, missingCount.getZExtValue());
            if (!appendMissingAutomaticBins(database, item.automaticRoot,
                                            materialized, printMissing, atLeast,
                                            item.automaticBins, report.error))
              return report;
          }
          unsigned totalWidth =
              std::max(65u, item.automaticTotal.getBitWidth() + 1);
          if (!atLeast)
            item.automaticCovered = item.automaticTotal.getLimitedValue();
          item.coveredWide = item.coveredWide.zext(totalWidth) +
                             item.automaticCoveredWide.zext(totalWidth);
          item.covered = item.coveredWide.getLimitedValue();
          item.totalWide = item.automaticTotal.zext(totalWidth) +
                           llvm::APInt(totalWidth, item.total);
          item.total = item.totalWide.getLimitedValue();
        }
      }
      if (item.aggregating) {
        instance.covered = saturatingAdd(instance.covered, item.covered);
        instance.total = saturatingAdd(instance.total, item.total);
        instance.coveredWide =
            addUnsigned(instance.coveredWide, item.coveredWide);
        instance.totalWide = addUnsigned(instance.totalWide, item.totalWide);
      }
      if (!item.totalWide.isZero()) {
        double raw = 100.0 * unsignedRatio(item.coveredWide, item.totalWide);
        item.percentage = applyGoal(raw, item.goal);
        item.contributes = item.aggregating && item.weight != 0;
        if (item.contributes) {
          weighted += item.percentage * item.weight;
          totalWeight += item.weight;
        }
      } else {
        item.percentage = item.weight ? 0.0 : 100.0;
      }
      instance.items.push_back(std::move(item));
    }
    if (totalWeight) {
      instance.percentage =
          applyGoal(weighted / static_cast<double>(totalWeight), instance.goal);
      instance.contributes = true;
    } else
      instance.percentage = instance.weight ? 0.0 : 100.0;
    report.functional.covered =
        saturatingAdd(report.functional.covered, instance.covered);
    report.functional.total =
        saturatingAdd(report.functional.total, instance.total);
    report.functional.hasExactCounts = true;
    report.functional.exactCovered =
        addUnsigned(report.functional.exactCovered, instance.coveredWide);
    report.functional.exactTotal =
        addUnsigned(report.functional.exactTotal, instance.totalWide);
    auto &type = report.functionalTypes[instance.type];
    type.type = instance.type;
    type.instances.push_back(instanceIndex);
    uint64_t typeGoal =
        configurationOption(database, configuration, instance.type,
                            FunctionalConfigurationOptionOwnerKind::Group,
                            FunctionalOptionScopeKind::Type,
                            FunctionalConfigurationOptionKind::Goal, 100);
    uint64_t typeWeight =
        configurationOption(database, configuration, instance.type,
                            FunctionalConfigurationOptionOwnerKind::Group,
                            FunctionalOptionScopeKind::Type,
                            FunctionalConfigurationOptionKind::Weight, 1);
    bool mergeInstances =
        configurationOption(database, configuration, instance.type,
                            FunctionalConfigurationOptionOwnerKind::Group,
                            FunctionalOptionScopeKind::Type,
                            FunctionalConfigurationOptionKind::MergeInstances,
                            0) != 0;
    std::string typeComment =
        configurationStringOption(database, configuration, instance.type,
                                  FunctionalConfigurationOptionOwnerKind::Group,
                                  FunctionalOptionScopeKind::Type,
                                  FunctionalConfigurationOptionKind::Comment);
    if (auto typeProfile = typeOptions.find(instance.type);
        typeProfile != typeOptions.end()) {
      typeGoal = typeProfile->second.goal;
      typeWeight = typeProfile->second.weight;
      mergeInstances = typeProfile->second.mergeInstances;
      typeComment = typeProfile->second.comment;
    }
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, std::string>>
        itemOptions;
    for (const auto &item : instance.items)
      if (item.aggregating)
        itemOptions.emplace_back(item.templateItem, item.typeGoal,
                                 item.typeWeight, item.typeComment);
    std::sort(itemOptions.begin(), itemOptions.end());
    if (!type.hasProfile) {
      type.goal = typeGoal;
      type.weight = typeWeight;
      type.mergeInstances = mergeInstances;
      type.comment = std::move(typeComment);
      type.itemOptions = std::move(itemOptions);
      type.hasProfile = true;
    } else if (type.goal != typeGoal || type.weight != typeWeight ||
               type.mergeInstances != mergeInstances ||
               type.comment != typeComment || type.itemOptions != itemOptions) {
      report.error = "conflicting type options for functional type " +
                     std::to_string(instance.type);
      return report;
    }
  }

  double global = 0.0;
  uint64_t globalWeight = 0;
  for (auto &[id, type] : report.functionalTypes) {
    double weighted = 0.0;
    uint64_t totalWeight = 0;
    if (!type.mergeInstances) {
      std::map<uint64_t, uint64_t> cumulativeAtLeast;
      for (size_t instanceIndex : type.instances)
        for (const auto &item : report.functionalInstances[instanceIndex].items) {
          cumulativeAtLeast[item.templateItem] =
              std::max(cumulativeAtLeast[item.templateItem], item.atLeast);
        }
      for (size_t instanceIndex : type.instances) {
        const auto &instance = report.functionalInstances[instanceIndex];
        double itemWeighted = 0.0;
        uint64_t itemWeight = 0;
        for (const auto &item : instance.items) {
          if (!item.aggregating || !item.weight)
            continue;
          const uint64_t atLeast = cumulativeAtLeast[item.templateItem];
          uint64_t explicitCovered = 0;
          for (const auto &bin : item.bins)
            if (!bin.excluded && bin.count >= atLeast)
              explicitCovered = saturatingAdd(explicitCovered, 1);
          llvm::APInt covered(64, explicitCovered);
          if (atLeast) {
            uint64_t automaticCovered = 0;
            for (const auto &bin : item.automaticBins)
              if (bin.count >= atLeast)
                automaticCovered = saturatingAdd(automaticCovered, 1);
            covered = addUnsigned(covered, automaticCovered);
          } else
            covered = addUnsigned(covered, item.automaticTotal);
          llvm::APInt total =
              addUnsigned(item.automaticTotal,
                          static_cast<uint64_t>(std::count_if(
                              item.bins.begin(), item.bins.end(),
                              [](const auto &bin) { return !bin.excluded; })));
          if (total.isZero())
            continue;
          const double percentage =
              applyGoal(100.0 * unsignedRatio(covered, total), item.goal);
          itemWeighted += percentage * static_cast<double>(item.weight);
          itemWeight = saturatingAdd(itemWeight, item.weight);
        }
        if (!itemWeight || !instance.weight)
          continue;
        const double instancePercentage = applyGoal(
            itemWeighted / static_cast<double>(itemWeight), instance.goal);
        weighted += instancePercentage * static_cast<double>(instance.weight);
        totalWeight = saturatingAdd(totalWeight, instance.weight);
      }
    } else {
      struct MergedBin {
        uint64_t count = 0;
      };
      struct AutomaticProfile {
        AutomaticNodeKey root;
        uint64_t atLeast = 1;
        std::vector<ReportData::FunctionalItem::AutomaticBin> bins;
      };
      struct MergedItem {
        uint64_t goal = 100;
        uint64_t weight = 1;
        uint64_t atLeast = 0;
        std::map<std::string, MergedBin> bins;
        std::vector<AutomaticProfile> automaticProfiles;
      };
      std::map<std::pair<uint64_t, std::string>, MergedItem> items;
      for (size_t instanceIndex : type.instances) {
        const auto &instance = report.functionalInstances[instanceIndex];
        for (const auto &item : instance.items) {
          if (!item.aggregating)
            continue;
          auto &merged = items[{item.templateItem, item.name}];
          merged.goal = item.typeGoal;
          merged.weight = item.typeWeight;
          for (const auto &bin : item.bins) {
            if (bin.excluded)
              continue;
            auto &mergedBin = merged.bins[bin.name];
            mergedBin.count = saturatingAdd(mergedBin.count, bin.count);
          }
          merged.atLeast = std::max(merged.atLeast, item.atLeast);
          if (!item.automaticTotal.isZero())
            merged.automaticProfiles.push_back({item.automaticRoot,
                                                item.automaticAtLeast,
                                                item.automaticBins});
        }
      }
      for (const auto &[key, item] : items) {
        llvm::APInt automaticTotal(1, 0);
        llvm::APInt positiveAutomaticTotal(1, 0);
        if (!item.automaticProfiles.empty()) {
          std::vector<AutomaticNodeKey> roots;
          std::vector<AutomaticNodeKey> positiveRoots;
          for (const auto &profile : item.automaticProfiles)
            roots.push_back(profile.root);
          if (item.atLeast)
            positiveRoots = roots;
          auto unionCount =
              countAutomaticGraphUnion(database, std::move(roots));
          if (!unionCount) {
            report.error = "automatic cross union exceeds traversal limits";
            return report;
          }
          automaticTotal = std::move(*unionCount);
          auto positiveUnionCount =
              countAutomaticGraphUnion(database, std::move(positiveRoots));
          if (!positiveUnionCount) {
            report.error = "automatic cross union exceeds traversal limits";
            return report;
          }
          positiveAutomaticTotal = std::move(*positiveUnionCount);
        }

        std::map<std::string, MergedBin> automaticBins;
        std::map<std::string, std::vector<std::string>> automaticComponents;
        for (const auto &profile : item.automaticProfiles)
          for (const auto &bin : profile.bins) {
            auto &merged = automaticBins[bin.name];
            merged.count = saturatingAdd(merged.count, bin.count);
            automaticComponents.emplace(bin.name, bin.componentNames);
          }
        for (auto &[name, bin] : automaticBins) {
          const auto &components = automaticComponents.at(name);
          bool contained = false;
          for (const auto &profile : item.automaticProfiles) {
            if (automaticGraphContains(database, profile.root, components)) {
              contained = true;
            }
          }
          if (!contained) {
            report.error = "sparse cross tuple is outside its automatic "
                           "universe for item " +
                           std::to_string(key.first);
            return report;
          }
        }

        llvm::APInt total = addUnsigned(automaticTotal, item.bins.size());
        if (total.isZero() || !item.weight)
          continue;
        uint64_t materializedCovered = 0;
        for (const auto &[name, bin] : item.bins)
          materializedCovered += bin.count >= item.atLeast;
        for (const auto &[name, bin] : automaticBins)
          materializedCovered +=
              item.atLeast && bin.count >= item.atLeast;
        unsigned automaticWidth = std::max(
            automaticTotal.getBitWidth(), positiveAutomaticTotal.getBitWidth());
        automaticWidth = std::max(automaticWidth, 1u);
        llvm::APInt automaticCovered =
            automaticTotal.zextOrTrunc(automaticWidth) -
            positiveAutomaticTotal.zextOrTrunc(automaticWidth);
        llvm::APInt covered =
            addUnsigned(automaticCovered, materializedCovered);
        unsigned compareWidth =
            std::max(total.getBitWidth(), covered.getBitWidth());
        if (total.zextOrTrunc(compareWidth)
                .ult(covered.zextOrTrunc(compareWidth))) {
          report.error = "merged functional hits exceed denominator for item " +
                         std::to_string(key.first);
          return report;
        }
        double percentage =
            applyGoal(100.0 * unsignedRatio(covered, total), item.goal);
        weighted += percentage * static_cast<double>(item.weight);
        totalWeight = saturatingAdd(totalWeight, item.weight);
      }
    }
    if (totalWeight) {
      type.percentage =
          applyGoal(weighted / static_cast<double>(totalWeight), type.goal);
      type.contributes = true;
      if (type.weight) {
        global += type.percentage * static_cast<double>(type.weight);
        globalWeight = saturatingAdd(globalWeight, type.weight);
      }
    } else
      type.percentage = type.weight ? 0.0 : 100.0;
  }
  report.functional.calculated =
      globalWeight ? global / static_cast<double>(globalWeight)
                   : (report.functional.available ? 100.0 : 0.0);
  return report;
}

std::string json(const std::string &input) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : input) {
    switch (c) {
    case '"':
      out << "\\\"";
      break;
    case '\\':
      out << "\\\\";
      break;
    case '\b':
      out << "\\b";
      break;
    case '\f':
      out << "\\f";
      break;
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      if (c < 0x20)
        out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << unsigned(c) << std::dec;
      else
        out << c;
    }
  }
  out << '"';
  return out.str();
}

std::string stringifyJsonIntegers(llvm::StringRef input) {
  std::string result;
  result.reserve(input.size());
  bool inString = false;
  bool escaped = false;
  for (size_t index = 0; index != input.size();) {
    const char character = input[index];
    if (inString) {
      result += character;
      ++index;
      if (escaped)
        escaped = false;
      else if (character == '\\')
        escaped = true;
      else if (character == '"')
        inString = false;
      continue;
    }
    if (character == '"') {
      inString = true;
      result += character;
      ++index;
      continue;
    }
    if (character != '-' && (character < '0' || character > '9')) {
      result += character;
      ++index;
      continue;
    }
    size_t end = index + (character == '-');
    while (end != input.size() && input[end] >= '0' && input[end] <= '9')
      ++end;
    bool integer = true;
    if (end != input.size() && input[end] == '.') {
      integer = false;
      ++end;
      while (end != input.size() && input[end] >= '0' && input[end] <= '9')
        ++end;
    }
    if (end != input.size() && (input[end] == 'e' || input[end] == 'E')) {
      integer = false;
      ++end;
      if (end != input.size() && (input[end] == '+' || input[end] == '-'))
        ++end;
      while (end != input.size() && input[end] >= '0' && input[end] <= '9')
        ++end;
    }
    if (integer)
      result += '"';
    result.append(input.data() + index, end - index);
    if (integer)
      result += '"';
    index = end;
  }
  return result;
}

std::string htmlEscape(llvm::StringRef input) {
  std::string out;
  const auto *cursor = reinterpret_cast<const llvm::UTF8 *>(input.data());
  const auto *end = cursor + input.size();
  auto appendReplacement = [&] { out.append("\xef\xbf\xbd"); };
  while (cursor != end) {
    const llvm::UTF8 *sequence = cursor;
    llvm::UTF32 codepoint = 0;
    if (llvm::convertUTF8Sequence(&cursor, end, &codepoint,
                                  llvm::strictConversion) !=
        llvm::conversionOK) {
      // Database validation normally makes this unreachable. Preserve the
      // HTML writer's totality if it is called on an independently constructed
      // report string.
      cursor = sequence + 1;
      appendReplacement();
      continue;
    }

    // Keep control and Unicode noncharacters out of generated HTML text.
    // Replace them deterministically so malformed metadata cannot make an
    // otherwise useful report unparseable.
    const bool validCharacter = codepoint == 0x9 || codepoint == 0xa ||
                                codepoint == 0xd ||
                                (codepoint >= 0x20 && codepoint <= 0xd7ff) ||
                                (codepoint >= 0xe000 && codepoint <= 0xfffd) ||
                                (codepoint >= 0x10000 && codepoint <= 0x10ffff);
    const bool noncharacter = (codepoint >= 0xfdd0 && codepoint <= 0xfdef) ||
                              (codepoint & 0xffff) == 0xfffe ||
                              (codepoint & 0xffff) == 0xffff;
    if (!validCharacter || noncharacter) {
      appendReplacement();
      continue;
    }
    if (codepoint > 0x7f) {
      out.append(reinterpret_cast<const char *>(sequence), cursor - sequence);
      continue;
    }
    switch (static_cast<char>(codepoint)) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '\"':
      out += "&quot;";
      break;
    case '\'':
      out += "&apos;";
      break;
    default:
      out += static_cast<char>(codepoint);
    }
  }
  return out;
}

std::string cspHash(llvm::StringRef input) {
  llvm::SHA256 hasher;
  hasher.update(input);
  return llvm::encodeBase64(hasher.final());
}

uint64_t runCount(const ReportData &report, const UUID &run, MetricKind metric,
                  uint64_t entity, uint32_t subindex) {
  auto found = report.counts.find(
      std::make_tuple(run, metric, entity, uint64_t{0}, subindex));
  return found == report.counts.end() ? 0 : found->second;
}

struct AggregatedCounter {
  uint64_t value = 0;
  bool overflow = false;
};

AggregatedCounter aggregateCounter(const ReportData &report, MetricKind metric,
                                   uint64_t entity, uint32_t subindex) {
  AggregatedCounter result;
  for (const UUID &run : report.runs) {
    ReportData::CounterKey key =
        std::make_tuple(run, metric, entity, uint64_t{0}, subindex);
    auto found = report.counts.find(key);
    if (found == report.counts.end())
      continue;
    if (found->second > UINT64_MAX - result.value ||
        report.overflowedCounters.count(key))
      result.overflow = true;
    result.value = saturatingAdd(result.value, found->second);
  }
  return result;
}

void jsonContributors(llvm::json::OStream &out, const Database &database,
                      const ReportData &report, MetricKind metric,
                      uint64_t entity, uint32_t subindex) {
  out.array([&] {
    for (const Run &run : database.runs) {
      if (!report.runs.count(run.uuid))
        continue;
      uint64_t count = runCount(report, run.uuid, metric, entity, subindex);
      if (!count)
        continue;
      out.object([&] {
        out.attribute("uuid", formatUUID(run.uuid));
        out.attribute("name", run.name);
        out.attribute("count", count);
      });
    }
  });
}

const Run *findRun(const Database &database, const UUID &uuid) {
  auto found = std::lower_bound(
      database.runs.begin(), database.runs.end(), uuid,
      [](const Run &run, const UUID &value) { return run.uuid < value; });
  return found != database.runs.end() && found->uuid == uuid ? &*found
                                                             : nullptr;
}

void jsonFunctionalContributors(
    llvm::json::OStream &out, const Database &database,
    llvm::ArrayRef<ReportData::Contribution> contributors) {
  std::map<UUID, AggregatedCounter> grouped;
  for (const ReportData::Contribution &contributor : contributors) {
    if (!contributor.count)
      continue;
    auto &aggregate = grouped[contributor.run];
    if (contributor.count > UINT64_MAX - aggregate.value ||
        contributor.overflow)
      aggregate.overflow = true;
    aggregate.value = saturatingAdd(aggregate.value, contributor.count);
  }
  out.array([&] {
    for (const auto &[run, contributor] : grouped) {
      const Run *metadata = findRun(database, run);
      out.object([&] {
        out.attribute("uuid", formatUUID(run));
        out.attribute("name", metadata ? metadata->name : std::string{});
        out.attribute("count", contributor.value);
        out.attribute("overflow", contributor.overflow);
      });
    }
  });
}

const char *toggleDimensionKindName(ToggleDimensionKind kind) {
  switch (kind) {
  case ToggleDimensionKind::Root:
    return "root";
  case ToggleDimensionKind::Scalar:
    return "scalar";
  case ToggleDimensionKind::Enum:
    return "enum";
  case ToggleDimensionKind::PackedArray:
    return "packed_array";
  case ToggleDimensionKind::UnpackedArray:
    return "unpacked_array";
  case ToggleDimensionKind::PackedStruct:
    return "packed_struct";
  case ToggleDimensionKind::UnpackedStruct:
    return "unpacked_struct";
  case ToggleDimensionKind::PackedUnion:
    return "packed_union";
  case ToggleDimensionKind::UnpackedUnion:
    return "unpacked_union";
  case ToggleDimensionKind::Field:
    return "field";
  case ToggleDimensionKind::Tag:
    return "tag";
  }
  return "unknown";
}

std::string declaredIndexText(int64_t start, uint64_t offset, bool increasing) {
  llvm::APInt value(65, static_cast<uint64_t>(start), true);
  llvm::APInt delta(65, offset);
  value = increasing ? value + delta : value - delta;
  llvm::SmallString<32> text;
  value.toString(text, 10, true, false);
  return std::string(text);
}

std::vector<std::string> toggleBitPaths(const Database &database,
                                        const ToggleObject &object,
                                        uint64_t bit) {
  struct Frame {
    uint32_t node;
    uint64_t bit;
    std::string path;
  };
  std::vector<Frame> stack{{object.typeRoot, bit, object.name}};
  std::vector<std::string> paths;
  while (!stack.empty()) {
    Frame frame = std::move(stack.back());
    stack.pop_back();
    const ToggleDimension &node = database.toggleDimensions[frame.node];
    std::string path = std::move(frame.path);
    uint64_t childBit = frame.bit;

    if (node.kind == ToggleDimensionKind::Field) {
      path += '.';
      path += node.name;
    } else if (node.kind == ToggleDimensionKind::Tag) {
      path += '.';
      path += node.name;
    } else if (node.kind == ToggleDimensionKind::PackedArray ||
               node.kind == ToggleDimensionKind::UnpackedArray) {
      const uint64_t difference = node.left >= node.right
                                      ? static_cast<uint64_t>(node.left) -
                                            static_cast<uint64_t>(node.right)
                                      : static_cast<uint64_t>(node.right) -
                                            static_cast<uint64_t>(node.left);
      if (difference == UINT64_MAX || node.bitWidth % (difference + 1) != 0) {
        paths.push_back(path + '[' + std::to_string(frame.bit) + ']');
        continue;
      }
      const uint64_t cardinality = difference + 1;
      const uint64_t elementWidth = node.bitWidth / cardinality;
      if (!elementWidth) {
        paths.push_back(path + '[' + std::to_string(frame.bit) + ']');
        continue;
      }
      const uint64_t element = (frame.bit - node.bitOffset) / elementWidth;
      const bool packed = node.kind == ToggleDimensionKind::PackedArray;
      const int64_t start = packed ? node.right : node.left;
      const bool increasing =
          packed ? node.left >= node.right : node.right >= node.left;
      path += '[' + declaredIndexText(start, element, increasing) + ']';
      childBit = node.bitOffset + (frame.bit - node.bitOffset) % elementWidth;
    }

    if (!node.childCount) {
      if (node.kind == ToggleDimensionKind::Scalar ||
          node.kind == ToggleDimensionKind::Tag) {
        if (node.bitWidth > 1)
          path += '[' +
                  declaredIndexText(node.right, frame.bit - node.bitOffset,
                                    node.left >= node.right) +
                  ']';
      }
      paths.push_back(std::move(path));
      continue;
    }

    std::vector<std::pair<uint32_t, uint64_t>> children;
    const uint64_t end = uint64_t{frame.node} + 1 + node.childCount;
    for (uint64_t index = frame.node + 1; index != end; ++index) {
      const ToggleDimension &child = database.toggleDimensions[index];
      if (child.parent != frame.node)
        continue;
      uint64_t candidateBit = childBit;
      if (node.kind == ToggleDimensionKind::PackedArray ||
          node.kind == ToggleDimensionKind::UnpackedArray)
        candidateBit = child.bitOffset + childBit - node.bitOffset;
      if (candidateBit >= child.bitOffset &&
          candidateBit - child.bitOffset < child.bitWidth)
        children.emplace_back(static_cast<uint32_t>(index), candidateBit);
    }
    if (children.empty()) {
      paths.push_back(path + '[' + std::to_string(frame.bit) + ']');
      continue;
    }
    for (auto child = children.rbegin(); child != children.rend(); ++child)
      stack.push_back({child->first, child->second, path});
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  return paths;
}

void textReport(llvm::raw_ostream &out, const Database &database,
                const ReportData &report,
                const std::map<MetricKind, double> &thresholds) {
  out << "Obelisk coverage\n"
      << "schema: " << digestText(computeSchemaFingerprint(database)) << '\n'
      << "tests: " << report.runs.size() << '\n';
  auto print = [&](MetricKind metric, const Summary &summary) {
    out << metricName(metric) << ": ";
    if (!summary.available) {
      out << "unavailable\n";
      return;
    }
    out << llvm::format("%.2f", summary.percentage()) << "% ("
        << summaryCountText(summary, true) << '/'
        << summaryCountText(summary, false) << ')';
    if (metric == MetricKind::Line)
      out << ", partial " << summary.partial;
    auto threshold = thresholds.find(metric);
    if (threshold != thresholds.end())
      out << " ["
          << (summary.percentage() >= threshold->second ? "PASS" : "FAIL")
          << " >= " << llvm::format("%.2f", threshold->second) << "%]";
    out << '\n';
  };
  print(MetricKind::Line, report.line);
  print(MetricKind::Toggle, report.toggle);
  print(MetricKind::Functional, report.functional);
  if (report.functional.available) {
    std::map<uint64_t, std::string> functionalExclusions;
    for (const Exclusion &exclusion : database.exclusions)
      if (exclusion.metric == MetricKind::Functional)
        functionalExclusions.emplace(exclusion.entity, exclusion.reason);
    std::unordered_map<uint64_t, const FunctionalType *> types;
    for (const FunctionalType &type : database.functionalTypes)
      types[type.id] = &type;
    out << "functional detail:\n";
    for (const auto &[typeID, typeReport] : report.functionalTypes) {
      auto type = types.find(typeID);
      if (type == types.end())
        continue;
      out << "  type " << type->second->name << " (" << typeID
          << "): " << llvm::format("%.2f", typeReport.percentage) << "%\n";
      out << "    type options: goal " << typeReport.goal << ", weight "
          << typeReport.weight << ", merge_instances "
          << (typeReport.mergeInstances ? "true" : "false") << '\n';
      if (!typeReport.comment.empty())
        out << "    type comment: " << json(typeReport.comment) << '\n';
      for (size_t instanceIndex : typeReport.instances) {
        const auto &instance = report.functionalInstances[instanceIndex];
        out << "    instance "
            << (instance.name.empty() ? "<unnamed>" : instance.name) << ": "
            << llvm::format("%.2f", instance.percentage) << "% ("
            << integerText(instance.coveredWide) << '/'
            << integerText(instance.totalWide)
            << ") [per_instance=" << (instance.perInstance ? "true" : "false")
            << "]\n";
        if (!instance.comment.empty())
          out << "      comment: " << json(instance.comment) << '\n';
        for (const auto &item : instance.items) {
          out << "      "
              << (item.kind == FunctionalItemKind::Cross ? "cross "
                                                         : "coverpoint ")
              << item.name << ": " << integerText(item.coveredWide) << '/'
              << integerText(item.totalWide) << " ("
              << llvm::format("%.2f", item.percentage) << "%)";
          if (!item.aggregating)
            out << " [non-aggregating inherited item]";
          out << '\n';
          if (!item.comment.empty())
            out << "        comment: " << json(item.comment) << '\n';
          if (!item.typeComment.empty())
            out << "        type comment: " << json(item.typeComment) << '\n';
          if (item.kind == FunctionalItemKind::Cross)
            out << "        automatic: "
                << integerText(item.automaticCoveredWide) << '/'
                << integerText(item.automaticTotal) << '\n';
          for (const auto &bin : item.automaticBins) {
            out << "        auto bin " << bin.name << ": " << bin.count << " ["
                << (bin.covered ? "covered" : "uncovered") << ", at_least "
                << bin.atLeast;
            if (bin.overflow)
              out << ", saturated";
            out << "]\n";
          }
          for (const auto &bin : item.bins) {
            out << "        bin " << bin.name << ": " << bin.count << " ["
                << (bin.excluded  ? "excluded"
                    : bin.covered ? "covered"
                                  : "uncovered")
                << ']';
            std::vector<std::string> roles;
            if (bin.flags & FunctionalBinDefault)
              roles.emplace_back("default");
            if (bin.flags & FunctionalBinDefaultSequence)
              roles.emplace_back("default sequence");
            if (bin.flags & FunctionalBinIgnore)
              roles.emplace_back("ignore");
            if (bin.flags & FunctionalBinIllegal)
              roles.emplace_back("illegal");
            if (bin.flags & FunctionalBinWildcard)
              roles.emplace_back("wildcard");
            if (bin.flags & FunctionalBinAutomatic)
              roles.emplace_back("automatic");
            if (bin.flags & FunctionalBinEmpty)
              roles.emplace_back("empty");
            auto exclusion = functionalExclusions.find(bin.templateBin);
            if (exclusion != functionalExclusions.end())
              roles.push_back("excluded: " + exclusion->second);
            if (!roles.empty()) {
              out << " {";
              for (size_t role = 0; role != roles.size(); ++role) {
                if (role)
                  out << ", ";
                out << roles[role];
              }
              out << '}';
            }
            if (bin.atLeast != 1)
              out << " {at_least " << bin.atLeast << '}';
            if (bin.overflow)
              out << " {saturated}";
            out << '\n';
          }
        }
      }
    }
  }
  if (!database.exclusions.empty()) {
    out << "exclusions:\n";
    for (const Exclusion &value : database.exclusions)
      out << "  " << metricName(value.metric) << ' ' << value.entity << ": "
          << value.reason << '\n';
  }
  struct IllegalDiagnosticRow {
    const IllegalBinDiagnostic *diagnostic;
    std::string hierarchy;
    std::string testName;
  };
  std::map<UUID, const Run *> runsByID;
  for (const Run &run : database.runs)
    runsByID.emplace(run.uuid, &run);
  std::map<std::pair<UUID, uint64_t>, const ResolvedInstance *> instancesByID;
  for (const ResolvedInstance &instance : database.resolvedInstances)
    instancesByID.emplace(std::make_pair(instance.run, instance.id), &instance);
  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalBin *>
      binsByID;
  for (const ResolvedFunctionalBin &bin : database.resolvedFunctionalBins)
    binsByID.emplace(std::make_tuple(bin.type, bin.configuration, bin.id),
                     &bin);
  std::vector<IllegalDiagnosticRow> illegalRows;
  for (const IllegalBinDiagnostic &diagnostic :
       database.illegalBinDiagnostics) {
    if (!report.runs.count(diagnostic.run))
      continue;
    std::string hierarchy = std::to_string(diagnostic.bin);
    auto instance =
        instancesByID.find(std::make_pair(diagnostic.run, diagnostic.instance));
    if (instance != instancesByID.end()) {
      auto bin = binsByID.find(std::make_tuple(instance->second->type,
                                               instance->second->configuration,
                                               diagnostic.bin));
      if (bin != binsByID.end())
        hierarchy = bin->second->hierarchy;
    }
    auto run = runsByID.find(diagnostic.run);
    illegalRows.push_back(
        {&diagnostic, std::move(hierarchy),
         run == runsByID.end() ? std::string{} : run->second->name});
  }
  std::sort(
      illegalRows.begin(), illegalRows.end(),
      [](const IllegalDiagnosticRow &left, const IllegalDiagnosticRow &right) {
        return std::tie(left.testName, left.hierarchy,
                        left.diagnostic->instance,
                        left.diagnostic->simulationTime,
                        left.diagnostic->message, left.diagnostic->count) <
               std::tie(right.testName, right.hierarchy,
                        right.diagnostic->instance,
                        right.diagnostic->simulationTime,
                        right.diagnostic->message, right.diagnostic->count);
      });
  if (!illegalRows.empty())
    out << "illegal-bin diagnostics:\n";
  for (const IllegalDiagnosticRow &row : illegalRows) {
    const auto &diagnostic = *row.diagnostic;
    out << "  " << row.hierarchy << " instance " << diagnostic.instance
        << " test " << json(row.testName) << ": " << diagnostic.count
        << " at time " << diagnostic.simulationTime << " - "
        << json(diagnostic.message) << '\n';
  }
}

void jsonRawAttribute(llvm::json::OStream &out, llvm::StringRef key,
                      llvm::StringRef value) {
  out.attributeBegin(key);
  out.rawValue(value);
  out.attributeEnd();
}

void jsonAPIntAttribute(llvm::json::OStream &out, llvm::StringRef key,
                        const llvm::APInt &value) {
  jsonRawAttribute(out, key, integerText(value));
}

void jsonFixedAttribute(llvm::json::OStream &out, llvm::StringRef key,
                        double value) {
  llvm::SmallString<32> text;
  llvm::raw_svector_ostream stream(text);
  stream << llvm::format("%.6f", value);
  jsonRawAttribute(out, key, text);
}

void jsonSummary(llvm::json::OStream &out, MetricKind metric,
                 const Summary &summary,
                 const std::map<MetricKind, double> &thresholds) {
  out.attributeObject(metricName(metric), [&] {
    out.attribute("available", summary.available);
    jsonRawAttribute(out, "covered", summaryCountText(summary, true));
    jsonRawAttribute(out, "total", summaryCountText(summary, false));
    out.attribute("partial", summary.partial);
    jsonFixedAttribute(out, "percent", summary.percentage());
    auto threshold = thresholds.find(metric);
    if (threshold != thresholds.end()) {
      jsonFixedAttribute(out, "threshold", threshold->second);
      out.attribute("threshold_pass",
                    summary.percentage() >= threshold->second);
    }
  });
}

void jsonReport(llvm::raw_ostream &stream, const Database &database,
                const ReportData &report,
                const std::map<MetricKind, double> &thresholds) {
  std::map<std::pair<MetricKind, uint64_t>, std::string> exclusionReasons;
  for (const Exclusion &exclusion : database.exclusions)
    exclusionReasons.emplace(std::make_pair(exclusion.metric, exclusion.entity),
                             exclusion.reason);
  llvm::json::OStream out(stream);
  out.object([&] {
    out.attribute("format", "obelisk-coverage-report-v1");
    out.attribute("codec_version", CodecVersion);
    out.attribute("schema_fingerprint",
                  digestText(computeSchemaFingerprint(database)));
    out.attributeObject("metrics", [&] {
      jsonSummary(out, MetricKind::Line, report.line, thresholds);
      jsonSummary(out, MetricKind::Toggle, report.toggle, thresholds);
      jsonSummary(out, MetricKind::Functional, report.functional, thresholds);
    });
    out.attributeArray("runs", [&] {
      for (const Run &run : database.runs)
        if (report.runs.count(run.uuid))
          out.object([&] {
            out.attribute("uuid", formatUUID(run.uuid));
            out.attribute("name", run.name);
            out.attribute("status", run.status);
            out.attribute("simulation_time", run.simulationTime);
            out.attribute("timestamp", run.timestamp);
            out.attribute("seed", run.seed);
            out.attributeObject("tags", [&] {
              for (const auto &tag : run.tags)
                out.attribute(tag.first, tag.second);
            });
          });
    });
    out.attributeArray("files", [&] {
      for (const SourceFile &file : database.sourceFiles)
        out.object([&] {
          out.attribute("id", file.id);
          out.attribute("path", file.path);
          out.attribute("digest", digestText(file.digest));
        });
    });
    out.attributeArray("scopes", [&] {
      for (const Scope &scope : database.scopes)
        out.object([&] {
          out.attribute("id", scope.id);
          out.attribute("parent", scope.parent);
          out.attribute("name", scope.name);
          out.attribute("kind", scope.kind);
          out.attribute("definition", scope.definition);
        });
    });
    out.attributeArray("line_points", [&] {
      for (const LinePoint &point : database.linePoints) {
        auto exclusion = exclusionReasons.find({MetricKind::Line, point.id});
        const AggregatedCounter count =
            aggregateCounter(report, MetricKind::Line, point.id, 0);
        out.object([&] {
          out.attribute("id", point.id);
          out.attribute("file", point.file);
          out.attribute("end_file", point.endFile);
          out.attribute("scope", point.scope);
          out.attribute("macro", point.macroName);
          out.attribute("line", point.line);
          out.attribute("column", point.column);
          out.attribute("end_line", point.endLine);
          out.attribute("end_column", point.endColumn);
          out.attribute("semantic_phase", point.semanticPhase);
          out.attribute("flags", point.flags);
          out.attribute("count", count.value);
          out.attribute("overflow", count.overflow);
          out.attribute("covered", count.value != 0);
          out.attribute("excluded", exclusion != exclusionReasons.end());
          out.attribute("exclusion_reason", exclusion == exclusionReasons.end()
                                                ? std::string{}
                                                : exclusion->second);
          out.attributeBegin("contributors");
          jsonContributors(out, database, report, MetricKind::Line, point.id,
                           0);
          out.attributeEnd();
        });
      }
    });
    out.attributeArray("toggles", [&] {
      for (const ToggleObject &object : database.toggleObjects) {
        auto exclusion = exclusionReasons.find({MetricKind::Toggle, object.id});
        out.object([&] {
          out.attribute("id", object.id);
          out.attribute("scope", object.scope);
          out.attribute("file", object.file);
          out.attribute("name", object.name);
          out.attribute("width", object.bitWidth);
          out.attribute("line", object.line);
          out.attribute("column", object.column);
          out.attribute("end_line", object.endLine);
          out.attribute("end_column", object.endColumn);
          out.attribute("four_state",
                        (object.flags & ToggleObjectFourState) != 0);
          out.attribute("excluded", exclusion != exclusionReasons.end());
          out.attribute("exclusion_reason", exclusion == exclusionReasons.end()
                                                ? std::string{}
                                                : exclusion->second);
          out.attributeArray("dimensions", [&] {
            const uint64_t dimensionEnd =
                uint64_t{object.typeRoot} +
                database.toggleDimensions[object.typeRoot].childCount + 1;
            for (uint64_t index = object.typeRoot; index != dimensionEnd;
                 ++index) {
              const ToggleDimension &dimension =
                  database.toggleDimensions[index];
              out.object([&] {
                out.attribute("index", index);
                out.attribute("parent", dimension.parent);
                out.attribute("kind", toggleDimensionKindName(dimension.kind));
                out.attribute("left", dimension.left);
                out.attribute("right", dimension.right);
                out.attribute("bit_offset", dimension.bitOffset);
                out.attribute("bit_width", dimension.bitWidth);
                out.attribute("name", dimension.name);
              });
            }
          });
          out.attributeArray("bits", [&] {
            for (uint64_t bit = 0; bit != object.bitWidth; ++bit) {
              const uint32_t base = static_cast<uint32_t>(bit * 4);
              const AggregatedCounter zeroToOne =
                  aggregateCounter(report, MetricKind::Toggle, object.id, base);
              const AggregatedCounter oneToZero = aggregateCounter(
                  report, MetricKind::Toggle, object.id, base + 1);
              const AggregatedCounter toUnknown = aggregateCounter(
                  report, MetricKind::Toggle, object.id, base + 2);
              const AggregatedCounter fromUnknown = aggregateCounter(
                  report, MetricKind::Toggle, object.id, base + 3);
              out.object([&] {
                out.attribute("index", bit);
                out.attribute("paths", toggleBitPaths(database, object, bit));
                auto transition = [&](llvm::StringRef name,
                                      const AggregatedCounter &count,
                                      uint32_t subindex, bool covered) {
                  out.attributeObject(name, [&] {
                    out.attribute("count", count.value);
                    if (covered)
                      out.attribute("covered", count.value != 0);
                    out.attribute("overflow", count.overflow);
                    out.attributeBegin("contributors");
                    jsonContributors(out, database, report, MetricKind::Toggle,
                                     object.id, subindex);
                    out.attributeEnd();
                  });
                };
                transition("zero_to_one", zeroToOne, base, true);
                transition("one_to_zero", oneToZero, base + 1, true);
                transition("to_unknown", toUnknown, base + 2, false);
                transition("from_unknown", fromUnknown, base + 3, false);
              });
            }
          });
        });
      }
    });
    out.attributeArray("functional_types", [&] {
      for (const FunctionalType &type : database.functionalTypes) {
        auto computed = report.functionalTypes.find(type.id);
        out.object([&] {
          out.attribute("id", type.id);
          out.attribute("scope", type.scope);
          out.attribute("name", type.name);
          out.attribute("flags", type.flags);
          out.attribute("comment", computed == report.functionalTypes.end()
                                       ? std::string{}
                                       : computed->second.comment);
          out.attribute("hierarchy", type.hierarchy);
          out.attribute("language_version", type.languageVersion);
          jsonFixedAttribute(out, "percent",
                             computed == report.functionalTypes.end()
                                 ? 0.0
                                 : computed->second.percentage);
          out.attribute("goal", computed == report.functionalTypes.end()
                                    ? uint64_t{100}
                                    : computed->second.goal);
          out.attribute("weight", computed == report.functionalTypes.end()
                                      ? uint64_t{1}
                                      : computed->second.weight);
          out.attribute("merge_instances",
                        computed != report.functionalTypes.end() &&
                            computed->second.mergeInstances);
        });
      }
    });
    out.attributeArray("functional_formals", [&] {
      for (const FunctionalFormal &formal : database.functionalFormals)
        out.object([&] {
          out.attribute("id", formal.id);
          out.attribute("type", formal.type);
          out.attribute("name", formal.name);
          out.attribute("kind", static_cast<uint32_t>(formal.kind));
          out.attribute("direction", static_cast<uint32_t>(formal.direction));
          out.attribute("result_kind",
                        static_cast<uint32_t>(formal.resultKind));
          out.attribute("flags", formal.flags);
          out.attribute("bit_width", formal.bitWidth);
          out.attribute("signedness", static_cast<uint32_t>(formal.signedness));
          out.attribute("ordinal", formal.ordinal);
          out.attribute("default_expression", formal.defaultExpression);
        });
    });
    out.attributeArray("functional_item_templates", [&] {
      for (const FunctionalItem &item : database.functionalItems)
        out.object([&] {
          out.attribute("id", item.id);
          out.attribute("type", item.type);
          out.attribute("name", item.name);
          out.attribute("hierarchy", item.hierarchy);
          out.attribute("kind", static_cast<uint32_t>(item.kind));
          out.attribute("flags", item.flags);
          out.attribute("ordinal", item.ordinal);
          out.attribute("goal", item.goal);
          out.attribute("weight", item.weight);
        });
    });
    out.attributeArray("functional_bin_templates", [&] {
      for (const FunctionalBin &bin : database.functionalBins)
        out.object([&] {
          out.attribute("id", bin.id);
          out.attribute("item", bin.item);
          out.attribute("name", bin.name);
          out.attribute("hierarchy", bin.hierarchy);
          out.attribute("kind", static_cast<uint32_t>(bin.kind));
          out.attribute("flags", bin.flags);
          out.attribute("ordinal", bin.ordinal);
          out.attribute("at_least", bin.atLeast);
        });
    });
    out.attributeArray("transition_programs", [&] {
      for (const TransitionProgram &program : database.transitionPrograms)
        out.object([&] {
          out.attribute("bin", program.bin);
          out.attribute("item", program.item);
          out.attribute("alternative_count", program.alternativeCount);
          out.attribute("flags", program.flags);
        });
    });
    out.attributeArray("transition_alternatives", [&] {
      for (const TransitionAlternative &alternative :
           database.transitionAlternatives)
        out.object([&] {
          out.attribute("bin", alternative.bin);
          out.attribute("terminal_value_set", alternative.terminalValueSet);
          out.attribute("step_count", alternative.stepCount);
          out.attribute("ordinal", alternative.ordinal);
          out.attribute("flags", alternative.flags);
        });
    });
    out.attributeArray("transition_steps", [&] {
      for (const TransitionStep &step : database.transitionSteps)
        out.object([&] {
          out.attribute("bin", step.bin);
          out.attribute("value_set", step.valueSet);
          out.attribute("lower_expression", step.lowerExpression);
          out.attribute("upper_expression", step.upperExpression);
          out.attribute("lower_bound", step.lowerBound);
          out.attribute("upper_bound", step.upperBound);
          out.attribute("alternative_ordinal", step.alternativeOrdinal);
          out.attribute("ordinal", step.ordinal);
          out.attribute("repetition", static_cast<uint32_t>(step.repetition));
          out.attribute("flags", step.flags);
        });
    });
    out.attributeArray("cross_plans", [&] {
      for (const CrossPlan &plan : database.crossPlans)
        out.object([&] {
          out.attribute("item", plan.item);
          out.attribute("target_count", plan.targetCount);
          out.attribute("bin_count", plan.binCount);
          out.attribute("retain_auto_policy",
                        static_cast<uint32_t>(plan.retainAutoPolicy));
          out.attribute("iff_expression", plan.iffExpression);
          out.attribute("tuple_element_type", plan.tupleElementType);
          out.attribute("tuple_provenance_span", plan.tupleProvenanceSpan);
          out.attribute("tuple_flags", plan.tupleFlags);
          out.attribute("flags", plan.flags);
        });
    });
    out.attributeArray("cross_targets", [&] {
      for (const CrossTarget &target : database.crossTargets)
        out.object([&] {
          out.attribute("cross", target.cross);
          out.attribute("target", target.target);
          out.attribute("ordinal", target.ordinal);
          out.attribute("tuple_bit_offset", target.tupleBitOffset);
          out.attribute("tuple_bit_width", target.tupleBitWidth);
          out.attribute("tuple_result_kind",
                        static_cast<uint32_t>(target.tupleResultKind));
          out.attribute("tuple_signedness",
                        static_cast<uint32_t>(target.tupleSignedness));
          out.attribute("tuple_flags", target.tupleFlags);
        });
    });
    out.attributeArray("cross_bin_plans", [&] {
      for (const CrossBinPlan &plan : database.crossBins)
        out.object([&] {
          out.attribute("bin", plan.bin);
          out.attribute("cross", plan.cross);
          out.attribute("root_selector", plan.rootSelector);
          out.attribute("flags", plan.flags);
        });
    });
    out.attributeArray("cross_selector_nodes", [&] {
      for (const CrossSelectorNode &node : database.crossSelectorNodes)
        out.object([&] {
          out.attribute("id", node.id);
          out.attribute("cross", node.cross);
          out.attribute("target", node.target);
          out.attribute("bin", node.bin);
          out.attribute("value_set", node.valueSet);
          out.attribute("with_expression", node.withExpression);
          out.attribute("construction_expression", node.constructionExpression);
          out.attribute("tuple_set", node.tupleSet);
          out.attribute("matches_expression", node.matchesExpression);
          out.attribute("operand_count", node.operandCount);
          out.attribute("kind", static_cast<uint32_t>(node.kind));
          out.attribute("ordinal", node.ordinal);
          out.attribute("matches_policy",
                        static_cast<uint32_t>(node.matchesPolicy));
          out.attribute("matches_count", node.matchesCount);
          out.attribute("flags", node.flags);
        });
    });
    out.attributeArray("cross_selector_operands", [&] {
      for (const CrossSelectorOperand &operand : database.crossSelectorOperands)
        out.object([&] {
          out.attribute("node", operand.node);
          out.attribute("operand", operand.operand);
          out.attribute("ordinal", operand.ordinal);
        });
    });
    out.attributeArray("functional_tuple_sets", [&] {
      for (const FunctionalTupleSet &set : database.functionalTupleSets)
        out.object([&] {
          out.attribute("id", set.id);
          out.attribute("cross", set.cross);
          out.attribute("selector", set.selector);
          out.attribute("tuple_count", set.tupleCount);
          out.attribute("element_mode", static_cast<uint32_t>(set.elementMode));
          out.attribute("flags", set.flags);
        });
    });
    out.attributeArray("functional_tuples", [&] {
      for (const FunctionalTuple &tuple : database.functionalTupleSetTuples)
        out.object([&] {
          out.attribute("tuple_set", tuple.tupleSet);
          out.attribute("id", tuple.id);
          out.attribute("component_count", tuple.componentCount);
          out.attribute("ordinal", tuple.ordinal);
          out.attribute("flags", tuple.flags);
        });
    });
    out.attributeArray("functional_tuple_components", [&] {
      for (const FunctionalTupleComponent &component :
           database.functionalTupleSetComponents)
        out.object([&] {
          out.attribute("tuple", component.tuple);
          out.attribute("target", component.target);
          out.attribute("bin", component.bin);
          out.attribute("value_set", component.valueSet);
          out.attribute("ordinal", component.ordinal);
          out.attribute("flags", component.flags);
        });
    });
    out.attributeArray("functional_configurations", [&] {
      for (const FunctionalConfiguration &configuration :
           database.functionalConfigurations) {
        if (!report.configurations.count(
                {configuration.type, configuration.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", configuration.type);
          out.attribute("configuration",
                        digestText(configuration.configuration));
          out.attribute("flags", configuration.flags);
          out.attributeArray("options", [&] {
            for (const FunctionalConfigurationOption &option :
                 database.functionalConfigurationOptions) {
              if (option.type != configuration.type ||
                  option.configuration != configuration.configuration)
                continue;
              out.object([&] {
                out.attribute("owner", option.owner);
                out.attribute("owner_kind",
                              static_cast<uint32_t>(option.ownerKind));
                out.attribute("scope", static_cast<uint32_t>(option.scope));
                out.attribute("option", static_cast<uint32_t>(option.option));
                out.attribute("value_kind",
                              static_cast<uint32_t>(option.valueKind));
                out.attribute("value", option.value);
                out.attribute("string_value", option.stringValue);
              });
            }
          });
        });
      }
    });
    out.attributeArray("resolved_functional_items", [&] {
      for (const ResolvedFunctionalItem &item :
           database.resolvedFunctionalItems) {
        if (!report.configurations.count({item.type, item.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", item.type);
          out.attribute("configuration", digestText(item.configuration));
          out.attribute("id", item.id);
          out.attribute("template_item", item.templateItem);
          out.attribute("name", item.name);
          out.attribute("hierarchy", item.hierarchy);
          out.attribute("kind", static_cast<uint32_t>(item.kind));
          out.attribute("flags", item.flags);
          out.attribute("ordinal", item.ordinal);
          out.attribute("goal", item.goal);
          out.attribute("weight", item.weight);
        });
      }
    });
    out.attributeArray("resolved_functional_bins", [&] {
      for (const ResolvedFunctionalBin &bin : database.resolvedFunctionalBins) {
        if (!report.configurations.count({bin.type, bin.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", bin.type);
          out.attribute("configuration", digestText(bin.configuration));
          out.attribute("id", bin.id);
          out.attribute("template_bin", bin.templateBin);
          out.attribute("item", bin.item);
          out.attribute("name", bin.name);
          out.attribute("hierarchy", bin.hierarchy);
          out.attribute("kind", static_cast<uint32_t>(bin.kind));
          out.attribute("flags", bin.flags);
          out.attribute("ordinal", bin.ordinal);
          out.attribute("expansion_ordinal", bin.expansionOrdinal);
          out.attribute("at_least", bin.atLeast);
        });
      }
    });
    out.attributeArray("resolved_transition_alternatives", [&] {
      for (const ResolvedTransitionAlternative &alternative :
           database.resolvedTransitionAlternatives) {
        if (!report.configurations.count(
                {alternative.type, alternative.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", alternative.type);
          out.attribute("configuration", digestText(alternative.configuration));
          out.attribute("id", alternative.id);
          out.attribute("bin", alternative.bin);
          out.attribute("template_alternative_ordinal",
                        alternative.templateAlternativeOrdinal);
          out.attribute("expansion_ordinal", alternative.expansionOrdinal);
          out.attribute("step_count", alternative.stepCount);
          out.attribute("ordinal", alternative.ordinal);
          out.attribute("flags", alternative.flags);
        });
      }
    });
    out.attributeArray("resolved_transition_expansion_groups", [&] {
      for (const ResolvedTransitionExpansionGroup &group :
           database.resolvedTransitionExpansionGroups) {
        if (!report.configurations.count({group.type, group.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", group.type);
          out.attribute("configuration", digestText(group.configuration));
          out.attribute("item", group.item);
          out.attribute("template_bin", group.templateBin);
          out.attribute("template_alternative_ordinal",
                        group.templateAlternativeOrdinal);
          out.attribute("alternative_count", group.alternativeCount);
          out.attribute("expansion_count", group.expansionCount);
          out.attribute("flags", group.flags);
        });
      }
    });
    out.attributeArray("resolved_transition_steps", [&] {
      for (const ResolvedTransitionStep &step :
           database.resolvedTransitionSteps) {
        if (!report.configurations.count({step.type, step.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", step.type);
          out.attribute("configuration", digestText(step.configuration));
          out.attribute("bin", step.bin);
          out.attribute("alternative", step.alternative);
          out.attribute("value_set", step.valueSet);
          out.attribute("lower_bound", step.lowerBound);
          out.attribute("upper_bound", step.upperBound);
          out.attribute("ordinal", step.ordinal);
          out.attribute("flags", step.flags);
        });
      }
    });
    out.attributeArray("resolved_functional_value_sets", [&] {
      for (const ResolvedFunctionalValueSet &set :
           database.resolvedFunctionalValueSets) {
        if (!report.configurations.count({set.type, set.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", set.type);
          out.attribute("configuration", digestText(set.configuration));
          out.attribute("id", set.id);
          out.attribute("template_value_set", set.templateValueSet);
          out.attribute("item", set.item);
          out.attribute("bit_width", set.bitWidth);
          out.attribute("kind", static_cast<uint32_t>(set.kind));
          out.attribute("signedness", static_cast<uint32_t>(set.signedness));
          out.attribute("owner_bin", set.ownerBin);
          out.attribute("owner_selector", set.ownerSelector);
          out.attribute("owner_ordinal", set.ownerOrdinal);
          out.attribute("owner_subordinal", set.ownerSubordinal);
          out.attribute("role", static_cast<uint32_t>(set.role));
          out.attribute("flags", set.flags);
          out.attributeArray("atoms", [&] {
            for (uint32_t atomOrdinal = 0; atomOrdinal != set.atomCount;
                 ++atomOrdinal) {
              const ResolvedFunctionalValueAtom &atom =
                  database.resolvedFunctionalValueAtoms[set.firstAtom +
                                                        atomOrdinal];
              out.object([&] {
                out.attribute("ordinal", atom.ordinal);
                out.attribute("kind", static_cast<uint32_t>(atom.kind));
                out.attribute("real_low_bits", atom.realLowBits);
                out.attribute("real_high_bits", atom.realHighBits);
                out.attribute("flags", atom.flags);
                out.attributeArray("limbs", [&] {
                  for (uint32_t limbOrdinal = 0; limbOrdinal != atom.limbCount;
                       ++limbOrdinal) {
                    const ResolvedFunctionalValueLimb &limb =
                        database.resolvedFunctionalValueLimbs[atom.firstLimb +
                                                              limbOrdinal];
                    out.object([&] {
                      out.attribute("ordinal", limb.ordinal);
                      out.attribute("low_aval", limb.lowAval);
                      out.attribute("low_bval", limb.lowBval);
                      out.attribute("high_aval", limb.highAval);
                      out.attribute("high_bval", limb.highBval);
                      out.attribute("wildcard_mask", limb.wildcardMask);
                    });
                  }
                });
              });
            }
          });
        });
      }
    });
    out.attributeArray("resolved_cross_selector_bindings", [&] {
      for (const ResolvedCrossSelectorBinding &binding :
           database.resolvedCrossSelectorBindings) {
        if (!report.configurations.count({binding.type, binding.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", binding.type);
          out.attribute("configuration", digestText(binding.configuration));
          out.attribute("cross", binding.cross);
          out.attribute("node", binding.node);
          out.attribute("value_set", binding.valueSet);
          out.attribute("with_expression", binding.withExpression);
          out.attribute("tuple_set", binding.tupleSet);
          out.attribute("matches_policy",
                        static_cast<uint32_t>(binding.matchesPolicy));
          out.attribute("matches_count", binding.matchesCount);
          out.attribute("flags", binding.flags);
        });
      }
    });
    std::set<uint32_t> selectedResolvedTuples;
    std::set<uint32_t> selectedResolvedTupleComponents;
    for (const ResolvedFunctionalTupleSet &set :
         database.resolvedFunctionalTupleSets) {
      if (!report.configurations.count({set.type, set.configuration}))
        continue;
      for (uint32_t ordinal = 0; ordinal != set.tupleCount; ++ordinal) {
        const uint32_t tupleIndex = set.firstTuple + ordinal;
        selectedResolvedTuples.insert(tupleIndex);
        const ResolvedFunctionalTuple &tuple =
            database.resolvedFunctionalTupleSetTuples[tupleIndex];
        for (uint32_t component = 0; component != tuple.componentCount;
             ++component)
          selectedResolvedTupleComponents.insert(tuple.firstComponent +
                                                 component);
      }
    }
    out.attributeArray("resolved_functional_tuple_sets", [&] {
      for (const ResolvedFunctionalTupleSet &set :
           database.resolvedFunctionalTupleSets) {
        if (!report.configurations.count({set.type, set.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", set.type);
          out.attribute("configuration", digestText(set.configuration));
          out.attribute("id", set.id);
          out.attribute("template_tuple_set", set.templateTupleSet);
          out.attribute("cross", set.cross);
          out.attribute("selector", set.selector);
          out.attribute("tuple_count", set.tupleCount);
          out.attribute("element_mode", static_cast<uint32_t>(set.elementMode));
          out.attribute("flags", set.flags);
        });
      }
    });
    out.attributeArray("resolved_functional_tuples", [&] {
      for (uint32_t index = 0;
           index != database.resolvedFunctionalTupleSetTuples.size(); ++index) {
        if (!selectedResolvedTuples.count(index))
          continue;
        const ResolvedFunctionalTuple &tuple =
            database.resolvedFunctionalTupleSetTuples[index];
        out.object([&] {
          out.attribute("tuple_set", tuple.tupleSet);
          out.attribute("id", tuple.id);
          out.attribute("component_count", tuple.componentCount);
          out.attribute("ordinal", tuple.ordinal);
          out.attribute("flags", tuple.flags);
        });
      }
    });
    out.attributeArray("resolved_functional_tuple_components", [&] {
      for (uint32_t index = 0;
           index != database.resolvedFunctionalTupleSetComponents.size();
           ++index) {
        if (!selectedResolvedTupleComponents.count(index))
          continue;
        const ResolvedFunctionalTupleComponent &component =
            database.resolvedFunctionalTupleSetComponents[index];
        out.object([&] {
          out.attribute("tuple", component.tuple);
          out.attribute("target", component.target);
          out.attribute("bin", component.bin);
          out.attribute("value_set", component.valueSet);
          out.attribute("ordinal", component.ordinal);
          out.attribute("flags", component.flags);
        });
      }
    });
    out.attributeArray("resolved_cross_plans", [&] {
      for (const ResolvedCrossPlan &plan : database.resolvedCrossPlans) {
        if (!report.configurations.count({plan.type, plan.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", plan.type);
          out.attribute("configuration", digestText(plan.configuration));
          out.attribute("cross", plan.cross);
          out.attribute("retain_auto_policy",
                        static_cast<uint32_t>(plan.retainAutoPolicy));
          out.attribute("root_node", plan.rootNode);
          jsonAPIntAttribute(out, "automatic_bin_count",
                             *automaticBinCount(database, plan));
        });
      }
    });
    out.attributeArray("resolved_cross_automatic_nodes", [&] {
      for (const ResolvedCrossAutomaticNode &node :
           database.resolvedCrossAutomaticNodes) {
        if (!report.configurations.count({node.type, node.configuration}))
          continue;
        out.object([&] {
          out.attribute("type", node.type);
          out.attribute("configuration", digestText(node.configuration));
          out.attribute("cross", node.cross);
          out.attribute("id", node.id);
          out.attribute("target_ordinal", node.targetOrdinal);
          out.attribute("edge_count", node.edgeCount);
          out.attribute("flags", node.flags);
        });
      }
    });
    out.attributeArray("resolved_cross_automatic_edges", [&] {
      for (const ResolvedCrossAutomaticNode &node :
           database.resolvedCrossAutomaticNodes) {
        if (!report.configurations.count({node.type, node.configuration}))
          continue;
        for (uint32_t ordinal = 0; ordinal != node.edgeCount; ++ordinal) {
          const ResolvedCrossAutomaticEdge &edge =
              database.resolvedCrossAutomaticEdges[node.firstEdge + ordinal];
          out.object([&] {
            out.attribute("type", node.type);
            out.attribute("configuration", digestText(node.configuration));
            out.attribute("cross", node.cross);
            out.attribute("node", edge.node);
            out.attribute("bin", edge.bin);
            out.attribute("child", edge.child);
            out.attribute("ordinal", edge.ordinal);
            out.attribute("flags", edge.flags);
          });
        }
      }
    });
    out.attributeArray("sparse_cross_tuples", [&] {
      for (const SparseCrossTuple &tuple : database.sparseCrossTuples) {
        if (!report.runs.count(tuple.run))
          continue;
        out.object([&] {
          out.attribute("run", formatUUID(tuple.run));
          out.attribute("instance", tuple.instance);
          out.attribute("cross", tuple.cross);
          out.attribute("hits", tuple.hits);
          out.attribute("overflow",
                        (tuple.flags & SparseCrossTupleOverflow) != 0);
          out.attributeArray("components", [&] {
            for (uint32_t index = 0; index != tuple.componentCount; ++index)
              out.value(
                  database.sparseCrossTupleComponents[tuple.firstComponent +
                                                      index]);
          });
        });
      }
    });
    out.attributeArray("functional_source_ranges", [&] {
      for (const FunctionalSourceRange &range :
           database.functionalSourceRanges) {
        out.object([&] {
          out.attribute("bin", range.bin);
          out.attribute("role", static_cast<uint32_t>(range.role));
          out.attribute("ordinal", range.ordinal);
          out.attribute("file", range.file);
          out.attribute("end_file", range.endFile);
          out.attribute("macro", range.macroName);
          out.attribute("line", range.line);
          out.attribute("column", range.column);
          out.attribute("end_line", range.endLine);
          out.attribute("end_column", range.endColumn);
        });
      }
    });
    out.attributeArray("resolved_instances", [&] {
      for (const ResolvedInstance &instance : database.resolvedInstances) {
        if (!report.runs.count(instance.run))
          continue;
        out.object([&] {
          out.attribute("run", formatUUID(instance.run));
          out.attribute("id", instance.id);
          out.attribute("type", instance.type);
          out.attribute("name", instance.name);
          out.attribute("flags", instance.flags);
          out.attribute("configuration", digestText(instance.configuration));
        });
      }
    });
    out.attributeArray("resolved_instance_options", [&] {
      for (const ResolvedInstanceOption &option :
           database.resolvedInstanceOptions) {
        if (!report.runs.count(option.run))
          continue;
        out.object([&] {
          out.attribute("run", formatUUID(option.run));
          out.attribute("instance", option.instance);
          out.attribute("owner", option.owner);
          out.attribute("owner_kind", static_cast<uint32_t>(option.ownerKind));
          out.attribute("option", static_cast<uint32_t>(option.option));
          out.attribute("value_kind", static_cast<uint32_t>(option.valueKind));
          out.attribute("value", option.value);
          out.attribute("string_value", option.stringValue);
        });
      }
    });
    out.attributeArray("functional_instance_groups", [&] {
      for (const auto &instance : report.functionalInstances) {
        out.object([&] {
          out.attribute("type", instance.type);
          out.attribute("name", instance.name);
          out.attribute("configuration", digestText(instance.configuration));
          out.attribute("comment", instance.comment);
          out.attribute("goal", instance.goal);
          out.attribute("weight", instance.weight);
          out.attribute("per_instance", instance.perInstance);
          out.attribute("cross_num_print_missing",
                        instance.groupCrossNumPrintMissing);
          jsonAPIntAttribute(out, "covered", instance.coveredWide);
          jsonAPIntAttribute(out, "total", instance.totalWide);
          jsonFixedAttribute(out, "percent", instance.percentage);
          out.attributeArray("sources", [&] {
            for (const auto &[run, id] : instance.sources)
              out.object([&] {
                out.attribute("run", formatUUID(run));
                out.attribute("id", id);
              });
          });
          out.attributeArray("items", [&] {
            for (const auto &item : instance.items)
              out.object([&] {
                out.attribute("id", item.id);
                out.attribute("template_item", item.templateItem);
                out.attribute("name", item.name);
                out.attribute("comment", item.comment);
                out.attribute("hierarchy", item.hierarchy);
                out.attribute("kind", static_cast<uint32_t>(item.kind));
                out.attribute("ordinal", item.ordinal);
                out.attribute("goal", item.goal);
                out.attribute("weight", item.weight);
                out.attribute("type_goal", item.typeGoal);
                out.attribute("type_weight", item.typeWeight);
                out.attribute("at_least", item.atLeast);
                out.attribute("aggregating", item.aggregating);
                out.attribute("type_comment", item.typeComment);
                jsonAPIntAttribute(out, "covered", item.coveredWide);
                jsonAPIntAttribute(out, "total", item.totalWide);
                jsonAPIntAttribute(out, "automatic_covered",
                                   item.automaticCoveredWide);
                jsonAPIntAttribute(out, "automatic_total", item.automaticTotal);
                out.attribute("automatic_at_least", item.automaticAtLeast);
                out.attribute("cross_num_print_missing",
                              item.crossNumPrintMissing);
                out.attribute("automatic_root_node",
                              std::get<3>(item.automaticRoot));
                jsonFixedAttribute(out, "percent", item.percentage);
                out.attributeArray("targets", [&] {
                  if (item.kind != FunctionalItemKind::Cross)
                    return;
                  auto plan = std::find_if(
                      database.crossPlans.begin(), database.crossPlans.end(),
                      [&](const CrossPlan &value) {
                        return value.item == item.templateItem;
                      });
                  if (plan == database.crossPlans.end())
                    return;
                  for (uint32_t ordinal = 0; ordinal != plan->targetCount;
                       ++ordinal) {
                    const CrossTarget &target =
                        database.crossTargets[plan->firstTarget + ordinal];
                    auto resolvedTarget = std::find_if(
                        instance.items.begin(), instance.items.end(),
                        [&](const ReportData::FunctionalItem &value) {
                          return value.templateItem == target.target;
                        });
                    out.object([&] {
                      out.attribute("template_item", target.target);
                      out.attribute("id", resolvedTarget == instance.items.end()
                                              ? uint64_t{0}
                                              : resolvedTarget->id);
                      out.attribute("name",
                                    resolvedTarget == instance.items.end()
                                        ? std::string{}
                                        : resolvedTarget->name);
                      out.attribute("ordinal", target.ordinal);
                    });
                  }
                });
                out.attributeArray("bins", [&] {
                  for (const auto &bin : item.bins) {
                    uint64_t crossSelector = 0;
                    if (bin.kind == FunctionalBinKind::Cross) {
                      auto plan = std::find_if(
                          database.crossBins.begin(), database.crossBins.end(),
                          [&](const CrossBinPlan &value) {
                            return value.bin == bin.templateBin;
                          });
                      if (plan != database.crossBins.end())
                        crossSelector = plan->rootSelector;
                    }
                    auto exclusion = exclusionReasons.find(
                        {MetricKind::Functional, bin.templateBin});
                    out.object([&] {
                      out.attribute("id", bin.id);
                      out.attribute("template_bin", bin.templateBin);
                      out.attribute("name", bin.name);
                      out.attribute("hierarchy", bin.hierarchy);
                      out.attribute("kind", static_cast<uint32_t>(bin.kind));
                      out.attribute("flags", bin.flags);
                      out.attribute("ordinal", bin.ordinal);
                      out.attribute("expansion_ordinal", bin.expansionOrdinal);
                      out.attribute("at_least", bin.atLeast);
                      out.attribute("count", bin.count);
                      out.attribute("overflow", bin.overflow);
                      out.attribute("excluded", bin.excluded);
                      out.attribute("contributing", !bin.excluded);
                      out.attribute("cross_selector", crossSelector);
                      out.attribute("exclusion_reason",
                                    exclusion == exclusionReasons.end()
                                        ? std::string{}
                                        : exclusion->second);
                      out.attribute("covered", bin.covered);
                      out.attributeBegin("contributors");
                      jsonFunctionalContributors(out, database,
                                                 bin.contributors);
                      out.attributeEnd();
                    });
                  }
                });
                out.attributeArray("automatic_bins", [&] {
                  for (const auto &bin : item.automaticBins)
                    out.object([&] {
                      out.attribute("name", bin.name);
                      out.attribute("count", bin.count);
                      out.attribute("overflow", bin.overflow);
                      out.attribute("at_least", bin.atLeast);
                      out.attribute("covered", bin.covered);
                      out.attribute("missing", bin.missing);
                      out.attributeArray("components", [&] {
                        for (size_t ordinal = 0;
                             ordinal != bin.components.size(); ++ordinal)
                          out.object([&] {
                            out.attribute("id", bin.components[ordinal]);
                            out.attribute("name", bin.componentNames[ordinal]);
                          });
                      });
                      out.attributeBegin("contributors");
                      jsonFunctionalContributors(out, database,
                                                 bin.contributors);
                      out.attributeEnd();
                    });
                });
              });
          });
        });
      }
    });
    out.attributeArray("functional_counters", [&] {
      for (const Counter &counter : database.counters) {
        if (counter.metric != MetricKind::Functional ||
            !report.runs.count(counter.run))
          continue;
        out.object([&] {
          out.attribute("run", formatUUID(counter.run));
          out.attribute("instance", counter.instance);
          out.attribute("bin", counter.entity);
          out.attribute("count", counter.value);
          out.attribute("overflow", (counter.flags & 1) != 0);
        });
      }
    });
    out.attributeArray("illegal_bin_diagnostics", [&] {
      std::map<std::pair<UUID, uint64_t>, const ResolvedInstance *>
          resolvedInstancesByID;
      for (const ResolvedInstance &instance : database.resolvedInstances)
        if (report.runs.count(instance.run))
          resolvedInstancesByID.emplace(
              std::make_pair(instance.run, instance.id), &instance);
      std::map<std::tuple<uint64_t, Digest, uint64_t>,
               const ResolvedFunctionalBin *>
          resolvedBinsByID;
      for (const ResolvedFunctionalBin &bin : database.resolvedFunctionalBins)
        if (report.configurations.count({bin.type, bin.configuration}))
          resolvedBinsByID.emplace(
              std::make_tuple(bin.type, bin.configuration, bin.id), &bin);
      for (const IllegalBinDiagnostic &diagnostic :
           database.illegalBinDiagnostics) {
        if (!report.runs.count(diagnostic.run))
          continue;
        const Run *run = findRun(database, diagnostic.run);
        auto instance = resolvedInstancesByID.find(
            std::make_pair(diagnostic.run, diagnostic.instance));
        std::string hierarchy;
        if (instance != resolvedInstancesByID.end()) {
          auto bin = resolvedBinsByID.find(
              std::make_tuple(instance->second->type,
                              instance->second->configuration, diagnostic.bin));
          if (bin != resolvedBinsByID.end())
            hierarchy = bin->second->hierarchy;
        }
        out.object([&] {
          out.attribute("run", formatUUID(diagnostic.run));
          out.attribute("test", run ? run->name : std::string{});
          out.attribute("instance", diagnostic.instance);
          out.attribute("bin", diagnostic.bin);
          out.attribute("hierarchy", hierarchy);
          out.attribute("simulation_time", diagnostic.simulationTime);
          out.attribute("count", diagnostic.count);
          out.attribute("message", diagnostic.message);
          out.attribute("overflow",
                        (diagnostic.flags & IllegalBinDiagnosticOverflow) != 0);
        });
      }
    });
    out.attributeArray("exclusions", [&] {
      for (const Exclusion &value : database.exclusions)
        out.object([&] {
          out.attribute("entity", value.entity);
          out.attribute("metric", metricName(value.metric));
          out.attribute("reason", value.reason);
        });
    });
  });
  stream << '\n';
}

std::string lcovTestName(const std::string &input) {
  std::string result;
  result.reserve(input.size());
  for (unsigned char character : input) {
    const bool asciiAlphanumeric = (character >= 'A' && character <= 'Z') ||
                                   (character >= 'a' && character <= 'z') ||
                                   (character >= '0' && character <= '9');
    result.push_back(asciiAlphanumeric || character == '_' ? character : '_');
  }
  return result;
}

std::string lcovPathField(const std::string &input) {
  std::string result = input;
  std::replace_if(
      result.begin(), result.end(),
      [](char character) { return character == '\r' || character == '\n'; },
      '_');
  return result;
}

void lcovReport(llvm::raw_ostream &out, const Database &database,
                const ReportData &report) {
  std::unordered_map<uint64_t, const SourceFile *> files;
  for (const auto &file : database.sourceFiles)
    files[file.id] = &file;
  std::set<uint64_t> excluded;
  for (const Exclusion &value : database.exclusions)
    if (value.metric == MetricKind::Line)
      excluded.insert(value.entity);
  std::set<std::string> versionedFiles;
  for (const Run &run : database.runs)
    if (report.runs.count(run.uuid)) {
      std::map<uint64_t, std::map<uint32_t, uint64_t>> lines;
      for (const LinePoint &point : database.linePoints)
        if (!excluded.count(point.id))
          lines[point.file][point.line] = std::max(
              lines[point.file][point.line],
              runCount(report, run.uuid, MetricKind::Line, point.id, 0));
      for (const auto &fileLines : lines) {
        auto found = files.find(fileLines.first);
        if (found == files.end())
          continue;
        out << "TN:" << lcovTestName(run.name)
            << "\nSF:" << lcovPathField(found->second->path) << '\n';
        if (versionedFiles.insert(found->second->path).second)
          out << "VER:" << digestText(found->second->digest) << '\n';
        uint64_t hit = 0;
        for (const auto &line : fileLines.second) {
          out << "DA:" << line.first << ',' << line.second << '\n';
          hit += line.second != 0;
        }
        out << "LF:" << fileLines.second.size() << "\nLH:" << hit
            << "\nend_of_record\n";
      }
    }
}

void htmlReport(llvm::raw_ostream &out, const Database &database,
                const ReportData &report, const std::string &sourceRoot,
                const std::map<MetricKind, double> &thresholds) {
  llvm::SmallString<0> payload;
  llvm::raw_svector_ostream payloadStream(payload);
  jsonReport(payloadStream, database, report, thresholds);
  std::string rawPayload = stringifyJsonIntegers(payload);
  std::string escaped;
  escaped.reserve(rawPayload.size());
  for (char character : rawPayload) {
    if (character == '<')
      escaped += "\\u003c";
    else
      escaped += character;
  }
  llvm::StringRef html = CoverageReportHtml;
  auto emitUntil = [&](llvm::StringRef marker) {
    size_t position = html.find(marker);
    if (position == llvm::StringRef::npos)
      llvm::report_fatal_error("invalid embedded coverage HTML template");
    out << html.take_front(position);
    html = html.drop_front(position + marker.size());
  };
  emitUntil("@OBELISK_COVERAGE_CSS_HASH@");
  out << cspHash(CoverageReportCss);
  emitUntil("@OBELISK_COVERAGE_DATA_HASH@");
  out << cspHash(escaped);
  emitUntil("@OBELISK_COVERAGE_JAVASCRIPT_HASH@");
  out << cspHash(CoverageReportJavaScript);
  emitUntil("@OBELISK_COVERAGE_CSS@");
  out << CoverageReportCss;
  emitUntil("@OBELISK_COVERAGE_DATA@");
  out << escaped;
  emitUntil("@OBELISK_COVERAGE_JAVASCRIPT@");
  out << CoverageReportJavaScript;
  emitUntil("@OBELISK_COVERAGE_SOURCES@");

  std::map<uint64_t, llvm::StringRef> lineExclusions;
  for (const Exclusion &value : database.exclusions)
    if (value.metric == MetricKind::Line)
      lineExclusions.try_emplace(value.entity, value.reason);
  std::map<std::pair<uint64_t, uint32_t>, std::vector<const LinePoint *>>
      pointsByLine;
  for (const LinePoint &point : database.linePoints)
    pointsByLine[{point.file, point.line}].push_back(&point);
  out << "<h2 id=\"sources-section\">Sources</h2>";
  for (const SourceFile &file : database.sourceFiles) {
    std::vector<std::string> candidates;
    if (!sourceRoot.empty()) {
      llvm::SmallString<256> rootedPath(sourceRoot);
      llvm::sys::path::append(rootedPath, file.path);
      candidates.push_back(rootedPath.str().str());
    }
    candidates.push_back(file.path);
    std::unique_ptr<llvm::MemoryBuffer> contents;
    std::string selected, mismatched;
    for (const std::string &candidate : candidates) {
      auto buffer = llvm::MemoryBuffer::getFile(candidate, false, false);
      if (!buffer)
        continue;
      llvm::StringRef candidateContents = (*buffer)->getBuffer();
      Digest actual =
          sha256(reinterpret_cast<const uint8_t *>(candidateContents.data()),
                 candidateContents.size());
      if (actual == file.digest) {
        contents = std::move(*buffer);
        selected = candidate;
        break;
      }
      if (mismatched.empty())
        mismatched = candidate;
    }
    if (selected.empty()) {
      if (mismatched.empty()) {
        llvm::errs() << "obelisk-cov: warning: source unavailable: "
                     << file.path << '\n';
        out << "<section id=\"source-" << file.id << "\" class=\"section\"><h3>"
            << htmlEscape(file.path)
            << "</h3><p class=\"muted\">Source unavailable</p></section>";
      } else {
        llvm::errs() << "obelisk-cov: warning: source digest mismatch: "
                     << mismatched << '\n';
        out << "<section id=\"source-" << file.id << "\" class=\"section\"><h3>"
            << htmlEscape(file.path)
            << "</h3><p class=\"muted\">Source digest mismatch</p></section>";
      }
      continue;
    }
    out << "<section id=\"source-" << file.id << "\" class=\"section\"><h3>"
        << htmlEscape(file.path) << "</h3><pre>";
    uint32_t lineNumber = 1;
    for (llvm::line_iterator lines(*contents, false), end; lines != end;
         ++lines) {
      auto found = pointsByLine.find({file.id, lineNumber});
      llvm::ArrayRef<const LinePoint *> points =
          found == pointsByLine.end()
              ? llvm::ArrayRef<const LinePoint *>{}
              : llvm::ArrayRef<const LinePoint *>(found->second);
      uint64_t hit = 0;
      uint64_t included = 0;
      std::vector<AggregatedCounter> counts;
      for (const LinePoint *point : points) {
        AggregatedCounter count =
            aggregateCounter(report, MetricKind::Line, point->id, 0);
        counts.push_back(count);
        if (!lineExclusions.count(point->id)) {
          ++included;
          hit += count.value != 0;
        }
      }
      const char *style = points.empty() ? ""
                          : !included    ? "excluded-line"
                          : hit == included ? "full"
                          : hit             ? "partial"
                                            : "uncovered";
      out << "<span class=\"" << style << "\"><span class=\"ln\">" << lineNumber
          << "</span>" << htmlEscape(*lines);
      if (!points.empty()) {
        out << " <span class=\"points\">[";
        for (size_t index = 0; index != points.size(); ++index) {
          if (index)
            out << ", ";
          out << "point " << points[index]->id << " @col "
              << points[index]->column << ": " << counts[index].value;
          if (counts[index].overflow)
            out << " (saturated)";
          if (auto exclusion = lineExclusions.find(points[index]->id);
              exclusion != lineExclusions.end()) {
            out << " <span class=\"excluded-point\">[excluded";
            if (!exclusion->second.empty())
              out << ": " << htmlEscape(exclusion->second);
            out << "]</span>";
          }
        }
        out << "]</span>";
      }
      out << "</span>\n";
      ++lineNumber;
    }
    out << "</pre></section>";
  }
  out << html;
}

const Summary &summaryFor(const ReportData &report, MetricKind metric) {
  if (metric == MetricKind::Line)
    return report.line;
  if (metric == MetricKind::Toggle)
    return report.toggle;
  return report.functional;
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  if (!parseOptions(argc, argv, options)) {
    usage(llvm::errs());
    return 1;
  }
  Database database;
  if (!load(options, database))
    return 1;
  if (options.command == "inspect") {
    llvm::outs()
        << "format: obcov\ncodec-version: " << CodecVersion
        << "\nschema-fingerprint: "
        << digestText(computeSchemaFingerprint(database))
        << "\nproducer: " << database.producer
        << "\nruns: " << database.runs.size()
        << "\nline-points: " << database.linePoints.size()
        << "\ntoggle-objects: " << database.toggleObjects.size()
        << "\nfunctional-types: " << database.functionalTypes.size()
        << "\nfunctional-formals: " << database.functionalFormals.size()
        << "\nfunctional-item-templates: " << database.functionalItems.size()
        << "\nfunctional-bin-templates: " << database.functionalBins.size()
        << "\nfunctional-configurations: "
        << database.functionalConfigurations.size()
        << "\nresolved-functional-items: "
        << database.resolvedFunctionalItems.size()
        << "\nresolved-functional-bins: "
        << database.resolvedFunctionalBins.size()
        << "\nresolved-cross-plans: " << database.resolvedCrossPlans.size()
        << "\nresolved-cross-automatic-nodes: "
        << database.resolvedCrossAutomaticNodes.size()
        << "\nresolved-cross-automatic-edges: "
        << database.resolvedCrossAutomaticEdges.size()
        << "\nsparse-cross-tuples: " << database.sparseCrossTuples.size()
        << "\nresolved-instances: " << database.resolvedInstances.size()
        << "\nresolved-instance-options: "
        << database.resolvedInstanceOptions.size()
        << "\nexclusions: " << database.exclusions.size() << '\n';
    return 0;
  }
  if (options.command == "merge") {
    Diagnostic diagnostic;
    if (options.discard &&
        discardTestDetail(database, &diagnostic) != Status::Ok) {
      printDiagnostic(options.output, diagnostic);
      return 1;
    }
    if (writeFileAtomically(options.output, database, &diagnostic) !=
        Status::Ok) {
      printDiagnostic(options.output, diagnostic);
      return 1;
    }
    return 0;
  }
  ReportData report = analyze(database, options.test);
  if (!report.error.empty()) {
    llvm::errs() << "obelisk-cov: " << report.error << '\n';
    return 1;
  }
  for (const auto &threshold : options.thresholds) {
    const Summary &summary = summaryFor(report, threshold.first);
    if (!summary.available) {
      llvm::errs() << "obelisk-cov: threshold requested for unavailable metric "
                   << metricName(threshold.first) << '\n';
      return 1;
    }
  }
  std::error_code error;
  std::optional<llvm::raw_fd_ostream> file;
  llvm::raw_ostream *out = &llvm::outs();
  if (!options.output.empty()) {
    file.emplace(options.output, error, llvm::sys::fs::OF_None);
    if (error) {
      llvm::errs() << "obelisk-cov: cannot open output: " << options.output
                   << ": " << error.message() << '\n';
      return 1;
    }
    out = &*file;
  }
  if (options.format == "text")
    textReport(*out, database, report, options.thresholds);
  else if (options.format == "json")
    jsonReport(*out, database, report, options.thresholds);
  else if (options.format == "lcov")
    lcovReport(*out, database, report);
  else
    htmlReport(*out, database, report, options.sourceRoot, options.thresholds);
  out->flush();
  if (file ? file->has_error() : llvm::outs().has_error()) {
    llvm::errs() << "obelisk-cov: output write failed\n";
    return 1;
  }
  for (const auto &threshold : options.thresholds)
    if (summaryFor(report, threshold.first).percentage() < threshold.second)
      return 2;
  return 0;
}
