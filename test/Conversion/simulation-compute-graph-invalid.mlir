// RUN: obelisk-opt %s --split-input-file --verify-diagnostics --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-verify-compute-graph))'

// The verifier re-derives the whole schedule from the executable CFG and
// compares. Every rejection therefore names what disagreed; none of them may
// fail the pass silently.

module {
  // expected-error @below {{has no typed compute_graph metadata}}
  obelisk_sim.design @missing {
    obelisk_sim.scope.decl 0
  }
}

// -----

module {
  // A fragment cost that no longer matches the block it describes. The
  // rejection names the element that disagreed, not just the whole graph.
  // expected-error @below {{compute graph does not match the executable CFG}}
  // expected-note @below {{node 0 is #obelisk_sim.fragment<id = 0, function = @process, block = 0, region = active, action = terminate, tier = native, cost = 99,}}
  obelisk_sim.design @stale_cost attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.fragment<id = 0, function = @process, block = 0,
        region = active, action = terminate, tier = native, cost = 99, lane = 0,
        twoState = true, effects = []>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [],
      fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [0]>} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // A fragment region that contradicts its function's entry kind. Both the
  // node and the region plan that placed it are reported.
  // expected-error @below {{compute graph does not match the executable CFG}}
  // expected-note @below {{node 0 is #obelisk_sim.fragment<id = 0, function = @process, block = 0, region = postponed,}}
  // expected-note @below {{region 0 is #obelisk_sim.region<kind = active, groups = []>}}
  obelisk_sim.design @wrong_region attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.fragment<id = 0, function = @process, block = 0,
        region = postponed, action = terminate, tier = native, cost = 0,
        lane = 0, twoState = true, effects = []>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>]>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [],
      fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [0]>} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @wrong_observability attributes {
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
    // expected-error @below {{observability does not match compute-graph VPI mode}}
    obelisk_sim.storage.decl 0 in 0 : i1 design {observability = 2 : i32}
  }
}

// -----

module {
  obelisk_sim.design @stale_summary attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.fragment<id = 0, function = @process, block = 0,
        region = active, action = terminate, tier = native, cost = 0, lane = 0,
        twoState = true, effects = []>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    // expected-error @below {{effect summary does not match the executable CFG}}
    // expected-error @below {{fragment ABI does not match its CFG blocks}}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32,
      effect_summary = [#obelisk_sim.effect<effect = read, resource = unknown,
        target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
        dynamic = false, deferred = false, trigger = none>],
      fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = []>} {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // Compiled sites are checked one operation at a time, so a stale or missing
  // site names the operation that carries it.
  obelisk_sim.design @stale_sites attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @process, block = 0,
          region = active, action = terminate, tier = native, cost = 4,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = nba, resource = storage,
              target = descriptor, descriptor = 0, formal = 0, low = 0,
              width = 8, dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.nba_commit<id = 1, slots = [0], accumulatorSites = [],
          frontierSites = [],
          effect = <effect = write, resource = storage, target = descriptor,
                    descriptor = 0, formal = 0, low = 0, width = 8,
                    dynamic = false, deferred = false, trigger = none>>],
      edges = [#obelisk_sim.edge<source = 0, target = 1, kind = nba_stage,
        resource = <effect = nba, resource = storage, target = descriptor,
                    descriptor = 0, formal = 0, low = 0, width = 8,
                    dynamic = false, deferred = false, trigger = none>>],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = [
          #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design {observability = 0 : i32}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %dst: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32,
      effect_summary = [#obelisk_sim.effect<effect = nba, resource = storage,
        target = descriptor, descriptor = 0, formal = 0, low = 0, width = 8,
        dynamic = false, deferred = false, trigger = none>],
      fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [0]>} {
      %zero = arith.constant 0 : i8
      // A single-shot site proven at compile time cannot claim the frontier.
      // expected-error @below {{has a stale NBA site}}
      obelisk_sim.nba.enqueue %zero to %dst {
        site = #obelisk_sim.nba_site<id = 0, commit = 1,
                                     storage = dynamic_frontier>
      } : (i8, !obelisk_sim.ref<i8>) -> ()
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @missing_continuation attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @process, block = 0,
          region = active, action = suspend_delay, tier = native, cost = 2,
          lane = 0, twoState = true, effects = []>,
        #obelisk_sim.fragment<id = 1, function = @process, block = 1,
          region = active, action = terminate, tier = native, cost = 0,
          lane = 0, twoState = true, effects = []>],
      edges = [#obelisk_sim.edge<source = 0, target = 1, kind = resume>],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>,
          #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, effect_summary = [],
      fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = [0, 1]>} {
      %delay = obelisk_sim.time.constant 5
      // expected-error @below {{is missing its continuation site}}
      // expected-error @below {{is missing its timing site}}
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }
  }
}

// -----

module {
  // Cross-attribute invariants still need the independent structural verifier:
  // every scheduled ID must name an existing node.
  // expected-error @below {{event-region group references an invalid node}}
  obelisk_sim.design @bad_membership attributes {
    compute_graph = #obelisk_sim.graph<version = 1, vpi = off, workers = 1,
      nodes = [], edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic,
            feedback = []>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }
}

// -----

// Locally malformed metadata is rejected by the attribute verifiers before
// any pass sees it.

// expected-error @below {{control-only edge cannot carry a resource}}
#bad_edge = #obelisk_sim.edge<source = 0, target = 0, kind = process_order,
  resource = <effect = write, resource = unknown, target = unknown,
              descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false,
              deferred = false, trigger = none>>

// -----

// expected-error @below {{only convergence groups may carry feedback}}
#bad_group = #obelisk_sim.group<fragments = [0], schedule = acyclic,
  feedback = [#obelisk_sim.effect<effect = watch, resource = unknown,
    target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
    dynamic = false, deferred = false, trigger = change>]>

// -----

// expected-error @below {{NBA commit has an invalid or duplicate site}}
#bad_commit = #obelisk_sim.nba_commit<id = 0, slots = [3],
  accumulatorSites = [3], frontierSites = [],
  effect = <effect = write, resource = unknown, target = unknown,
            descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false,
            deferred = false, trigger = none>>

// -----

// expected-error @below {{compute graph event regions are out of order}}
#bad_regions = #obelisk_sim.graph<version = 1, vpi = off, workers = 1,
  nodes = [], edges = [],
  regions = [
    #obelisk_sim.region<kind = nba, groups = []>,
    #obelisk_sim.region<kind = active, groups = []>,
    #obelisk_sim.region<kind = observed, groups = []>,
    #obelisk_sim.region<kind = reactive, groups = []>,
    #obelisk_sim.region<kind = postponed, groups = []>]>
