// RUN: obelisk-opt %s --split-input-file --verify-diagnostics --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-verify-compute-graph))'

module {
  // expected-error @below {{has no typed compute_graph metadata}}
  obelisk_sim.design @missing {
    obelisk_sim.scope.decl 0
  }
}

// -----

module {
  // A commit inventory must contain exactly the NBA operations in the design.
  // expected-error @below {{has an invalid continuation, timing, NBA, or event site}}
  obelisk_sim.design @extra_nba_site attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.nba_commit<
        id = 0, slots = [0], accumulatorSites = [], staticJournalSites = [],
        frontierSites = [],
        effect = <effect = write, resource = unknown, target = unknown,
                  descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false,
                  deferred = false, trigger = none>>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
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
  // expected-error @below {{compute graph has a duplicate NBA commit target}}
  obelisk_sim.design @duplicate_commit attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.nba_commit<id = 0, slots = [0], accumulatorSites = [],
          staticJournalSites = [], frontierSites = [],
          effect = <effect = write, resource = unknown, target = unknown,
                    descriptor = 0, formal = 0, low = 0, width = 0,
                    dynamic = false, deferred = false, trigger = none>>,
        #obelisk_sim.nba_commit<id = 1, slots = [1], accumulatorSites = [],
          staticJournalSites = [], frontierSites = [],
          effect = <effect = write, resource = unknown, target = unknown,
                    descriptor = 0, formal = 0, low = 0, width = 0,
                    dynamic = false, deferred = false, trigger = none>>],
      edges = [],
      regions = [
        #obelisk_sim.region<kind = active, groups = []>,
        #obelisk_sim.region<kind = nba, groups = [
          #obelisk_sim.group<fragments = [0], schedule = acyclic, feedback = []>,
          #obelisk_sim.group<fragments = [1], schedule = acyclic, feedback = []>]>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
  }
}

// -----

module {
  // A multi-node convergence group must be one strongly connected component.
  // expected-error @below {{multi-node schedule group is not an SCC}}
  obelisk_sim.design @not_an_scc attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [
        #obelisk_sim.fragment<id = 0, function = @first, block = 0,
          region = active, action = terminate, tier = native, cost = 0,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = write, resource = unknown,
              target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
              dynamic = false, deferred = false, trigger = none>]>,
        #obelisk_sim.fragment<id = 1, function = @second, block = 0,
          region = active, action = terminate, tier = native, cost = 0,
          lane = 0, twoState = true, effects = [
            #obelisk_sim.effect<effect = write, resource = unknown,
              target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
              dynamic = false, deferred = false, trigger = none>]>],
      edges = [#obelisk_sim.edge<source = 0, target = 1, kind = conflict,
        resource = <effect = write, resource = unknown, target = unknown,
                    descriptor = 0, formal = 0, low = 0, width = 0, dynamic = false,
                    deferred = false, trigger = none>>],
      regions = [
        #obelisk_sim.region<kind = active, groups = [
          #obelisk_sim.group<fragments = [0, 1], schedule = convergence, feedback = [
            #obelisk_sim.effect<effect = write, resource = unknown,
              target = unknown, descriptor = 0, formal = 0, low = 0, width = 0,
              dynamic = false, deferred = false, trigger = none>]>]>,
        #obelisk_sim.region<kind = nba, groups = []>,
        #obelisk_sim.region<kind = observed, groups = []>,
        #obelisk_sim.region<kind = reactive, groups = []>,
        #obelisk_sim.region<kind = postponed, groups = []>]>
  } {
    obelisk_sim.scope.decl 0
    obelisk_sim.func @first(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
      entry_kind = 1 : i32,
      effect_summary = [#obelisk_sim.effect<
        effect = write, resource = unknown, target = unknown,
        descriptor = 0, formal = 0,
        low = 0, width = 0, dynamic = false, deferred = false, trigger = none>],
      fragment_abi = #obelisk_sim.fragment_abi<
        version = 1, fragments = [0]>
    } {
      obelisk_sim.return
    }
    obelisk_sim.func @second(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
      entry_kind = 1 : i32,
      effect_summary = [#obelisk_sim.effect<
        effect = write, resource = unknown, target = unknown,
        descriptor = 0, formal = 0,
        low = 0, width = 0, dynamic = false, deferred = false, trigger = none>],
      fragment_abi = #obelisk_sim.fragment_abi<
        version = 1, fragments = [1]>
    } {
      obelisk_sim.return
    }
  }
}

// -----

module {
  // The ABI action is derived from the fragment terminator, not trusted.
  obelisk_sim.design @wrong_action attributes {
    compute_graph = #obelisk_sim.graph<
      version = 1, vpi = off, workers = 1,
      nodes = [#obelisk_sim.fragment<id = 0, function = @process, block = 0,
        region = active, action = continue, tier = native, cost = 0, lane = 0,
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
    // expected-error @below {{fragment ABI does not match its CFG blocks}}
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {
      entry_kind = 1 : i32,
      effect_summary = [],
      fragment_abi = #obelisk_sim.fragment_abi<
        version = 1, fragments = [0]>
    } {
      obelisk_sim.return
    }
  }
}
