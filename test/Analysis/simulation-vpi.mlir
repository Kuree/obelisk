// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --test-obelisk-simulation-vpi-analysis 2>&1 | FileCheck %s

// CHECK:      vpi @missing graph=false mode=off observability=invisible read=false write=false static-dependencies=false
// CHECK-NEXT: vpi @off graph=true mode=off observability=invisible read=false write=false static-dependencies=true
// CHECK-NEXT: vpi @read graph=true mode=read observability=safe_point read=true write=false static-dependencies=true
// CHECK-NEXT: vpi @full graph=true mode=full observability=externally_writable read=true write=true static-dependencies=false

module {
  simulation.design @missing {
    simulation.scope.decl 0
  }

  simulation.design @off attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1, nodes = [], edges = [],
      regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }

  simulation.design @read attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = read, workers = 1, nodes = [], edges = [],
      regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }

  simulation.design @full attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = full, workers = 1, nodes = [], edges = [],
      regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }
}
