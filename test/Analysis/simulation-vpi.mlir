// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --test-obelisk-simulation-vpi-analysis 2>&1 | FileCheck %s

// CHECK:      vpi @missing graph=false mode=off observability=invisible read=false write=false static-dependencies=false
// CHECK-NEXT: vpi @off graph=true mode=off observability=invisible read=false write=false static-dependencies=true
// CHECK-NEXT: vpi @read graph=true mode=read observability=safe_point read=true write=false static-dependencies=true
// CHECK-NEXT: vpi @full graph=true mode=full observability=externally_writable read=true write=true static-dependencies=false

module {
  obelisk_sim.design @missing {
    obelisk_sim.scope.decl 0
  }

  obelisk_sim.design @off attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1, nodes = [], edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }

  obelisk_sim.design @read attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = read, workers = 1, nodes = [], edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }

  obelisk_sim.design @full attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = full, workers = 1, nodes = [], edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }
}
