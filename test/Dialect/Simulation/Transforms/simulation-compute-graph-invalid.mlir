// RUN: obelisk-opt %s --split-input-file --verify-diagnostics --pass-pipeline='builtin.module(simulation.design(obelisk-sim-verify-compute-graph))'

// The verifier re-derives the whole schedule from the executable CFG and
// compares. Every rejection therefore names what disagreed; none of them may
// fail the pass silently.

module {
  // expected-error @below {{has no typed compute_graph metadata}}
  simulation.design @missing {
    simulation.scope.decl 0
  }
}

// -----

module {
  // A fragment cost that no longer matches the block it describes. The
  // rejection names the element that disagreed, not just the whole graph.
  // expected-error @below {{compute graph does not match the executable CFG}}
  // expected-note @below {{node 0 is #schedule.fragment<id = 0, function = @process, block = 0, region = active, action = terminate, tier = native, cost = 99,}}
  simulation.design @stale_cost attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#schedule.fragment<id = 0, function = @process, block = 0,
        region = active, action = terminate, tier = native, cost = 99, lane = 0,
        twoState = true, effects = []>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.stale_cost.process.9000001"
    simulation.scope.decl 0
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [],
      fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  // A fragment region that contradicts its function's entry kind. Both the
  // node and the region plan that placed it are reported.
  // expected-error @below {{compute graph does not match the executable CFG}}
  // expected-note @below {{node 0 is #schedule.fragment<id = 0, function = @process, block = 0, region = postponed,}}
  // expected-note @below {{region 0 is #schedule.region<kind = active, groups = []>}}
  simulation.design @wrong_region attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#schedule.fragment<id = 0, function = @process, block = 0,
        region = postponed, action = terminate, tier = native, cost = 0,
        lane = 0, twoState = true, effects = []>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = []>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>]>]>
  } {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.wrong_region.process.9000001"
    simulation.scope.decl 0
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [],
      fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @wrong_observability attributes {
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
    // expected-error @below {{observability does not match compute-graph VPI mode}}
    simulation.storage.decl 0 in 0 : i1 design {observability = 2 : i32}
  }
}

// -----

module {
  simulation.design @stale_summary attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#schedule.fragment<id = 0, function = @process, block = 0,
        region = active, action = terminate, tier = native, cost = 0, lane = 0,
        twoState = true, effects = []>],
      edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.stale_summary.process.9000001"
    simulation.scope.decl 0
    // expected-error @below {{effect summary does not match the executable CFG}}
    // expected-error @below {{fragment ABI does not match its CFG blocks}}
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32,
      effect_summary = [#schedule.effect<effect = read, resource = unknown,
        target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
        dynamic = false, deferred = false, trigger = none>],
      fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>, code_unit_id = 9000001 : i64} {
      simulation.return
    }
  }
}

// -----

module {
  // Compiled sites are checked one operation at a time, so a stale or missing
  // site names the operation that carries it.
  simulation.design @stale_sites attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @process, block = 0,
          region = active, action = terminate, tier = native, cost = 4,
          lane = 0, twoState = true, effects = [
            #schedule.effect<effect = nba, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false, trigger = none>]>,
        #schedule.nba_commit<id = 1, slots = [0], accumulatorSites = [],
          frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
                    descriptor = 0, formal = 0, low = 0, width = 8,
                    dynamic = false, deferred = false, trigger = none>>],
      edges = [#schedule.edge<source = 0, target = 1, kind = nba_stage,
        resource = <effect = nba, resource = storage, target = descriptor,
                    descriptor = 0, formal = 0, low = 0, width = 8,
                    dynamic = false, deferred = false, trigger = none>>],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = [
          #schedule.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.stale_sites.process.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design {observability = 0 : i32}
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %dst: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32,
      effect_summary = [#schedule.effect<effect = nba, resource = storage,
        target = descriptor, descriptor = 0, formal = 0, low = 0, width = 8,
        dynamic = false, deferred = false, trigger = none>],
      fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0]>, code_unit_id = 9000001 : i64} {
      %zero = arith.constant 0 : i8
      // A single-shot site proven at compile time cannot claim the frontier.
      // expected-error @below {{has a stale NBA site}}
      simulation.nba.enqueue %zero to %dst {
        site = #schedule.nba_site<id = 0, commit = 1,
                                     storage = dynamic_frontier>
      } : (i8, !simulation.ref<i8>) -> ()
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @missing_continuation attributes {
    compute_graph = #schedule.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #schedule.fragment<id = 0, function = @process, block = 0,
          region = active, action = suspend_delay, tier = native, cost = 2,
          lane = 0, twoState = true, effects = []>,
        #schedule.fragment<id = 1, function = @process, block = 1,
          region = active, action = terminate, tier = native, cost = 0,
          lane = 0, twoState = true, effects = []>],
      edges = [#schedule.edge<source = 0, target = 1, kind = resume>],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic, feedback = []>,
          #schedule.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.missing_continuation.process.9000001"
    simulation.scope.decl 0
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [],
      fragment_abi = #schedule.fragment_abi<version = 1, fragments = [0, 1]>, code_unit_id = 9000001 : i64} {
      %delay = simulation.time.constant 5
      // expected-error @below {{is missing its continuation site}}
      // expected-error @below {{is missing its timing site}}
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
  }
}

// -----

module {
  // Cross-attribute invariants still need the independent structural verifier:
  // every scheduled ID must name an existing node.
  // expected-error @below {{event-region group references an invalid node}}
  simulation.design @bad_membership attributes {
    compute_graph = #schedule.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [],
      regions = [
        #schedule.region<kind = active, groups = [
          #schedule.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #schedule.region<kind = nba, groups = []>,
        #schedule.region<kind = observed, groups = []>,
        #schedule.region<kind = reactive, groups = []>,
        #schedule.region<kind = postponed, groups = []>]>
  } {
    simulation.scope.decl 0
  }
}

// -----

// Locally malformed metadata is rejected by the attribute verifiers before
// any pass sees it.

// expected-error @below {{control-only edge cannot carry a resource}}
#bad_edge = #schedule.edge<source = 0, target = 0, kind = process_order,
  resource = <effect = write, resource = unknown, target = unknown,
              descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false,
              deferred = false, trigger = none>>

// -----

// expected-error @below {{only convergence groups may carry feedback}}
#bad_group = #schedule.group<fragments = [0], schedule = acyclic,
  feedback = [#schedule.effect<effect = watch, resource = unknown,
    target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
    dynamic = false, deferred = false, trigger = change>]>

// -----

// expected-error @below {{NBA commit has an invalid or duplicate site}}
#bad_commit = #schedule.nba_commit<id = 0, slots = [3],
  accumulatorSites = [3], frontierSites = [],
  effect = <effect = write, resource = unknown, target = unknown,
            descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false,
            deferred = false, trigger = none>>

// -----

// expected-error @below {{compute graph event regions are out of order}}
#bad_regions = #schedule.graph<version = 1, vpi = off, workers = 1,
  nodes = [], edges = [],
  regions = [
    #schedule.region<kind = nba, groups = []>,
    #schedule.region<kind = active, groups = []>,
    #schedule.region<kind = observed, groups = []>,
    #schedule.region<kind = reactive, groups = []>,
    #schedule.region<kind = postponed, groups = []>]>
