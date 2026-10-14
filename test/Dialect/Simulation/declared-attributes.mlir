// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

// CHECK: module attributes {simulation.gc_polled, simulation.pulse_on_detect = true, simulation.pulse_reject_percent = 80 : i32}
module attributes {simulation.gc_polled, simulation.pulse_on_detect = true,
                   simulation.pulse_reject_percent = 80 : i32} {
  // CHECK-LABEL: func.func private @summarized
  // CHECK-SAME: simulation.integer_range = #simulation.integer_range<umin = 0 : i8, umax = 3 : i8, smin = 0 : i8, smax = 3 : i8>
  // CHECK-SAME: simulation.result_exprs = [#simulation.integer_expression<kind = add, width = 8, operands = [#simulation.integer_expression<kind = argument, width = 8, index = 0>, #simulation.integer_expression<kind = constant, width = 8, value = 1 : i8>]>, unit]
  func.func private @summarized(
      i8 {simulation.integer_range = #simulation.integer_range<
          umin = 0 : i8, umax = 3 : i8, smin = 0 : i8, smax = 3 : i8>})
      attributes {simulation.result_exprs = [
        #simulation.integer_expression<kind = add, width = 8, operands = [
          #simulation.integer_expression<kind = argument, width = 8, index = 0>,
          #simulation.integer_expression<kind = constant, width = 8,
                                         value = 1 : i8>]>,
        unit]}

  // CHECK-LABEL: func.func private @minimized
  // CHECK-SAME: simulation.boolean_minimizations = [#simulation.boolean_minimization<antecedent, solver = "z3", queries = 9, alternatives = 3 -> 2, literals = 6 -> 4>, #simulation.boolean_minimization<consequent, solver = "heuristic", queries = 0, alternatives = 2 -> 2, literals = 2 -> 2>]
  func.func private @minimized() attributes {simulation.boolean_minimizations = [
    #simulation.boolean_minimization<antecedent, solver = "z3", queries = 9,
                                     alternatives = 3 -> 2, literals = 6 -> 4>,
    #simulation.boolean_minimization<consequent, solver = "heuristic",
                                     queries = 0, alternatives = 2 -> 2,
                                     literals = 2 -> 2>]}

  // CHECK-LABEL: func.func private @randomize_plan
  // CHECK-SAME: simulation.random_nested_hooks = [#simulation.random_nested_hooks<object = <field = @child, concrete = <@Leaf>, storage = <@Node>, outerModeIndex = 1, path = [#simulation.random_object_step<field = @next, storage = <@Node>, concrete = <@Leaf>, modeIndex = 0>]>, post = <callee = @post, owner = @Leaf, source = @s1.post_randomize, captures = [], reads = ["top.limit"]>>]
  // CHECK-SAME: simulation.random_post_hook = #simulation.randomize_hook<callee = @post, owner = @Leaf, source = @s1.post_randomize>
  // CHECK-SAME: simulation.random_recursive_alias_guards = [#simulation.random_alias_guard<object = <field = @child, concrete = <@Leaf>, storage = <@Node>, outerModeIndex = 1, path = [#simulation.random_object_step<field = @next, storage = <@Node>, concrete = <@Leaf>, modeIndex = 0>]>, aliasDepth = 0>]
  // CHECK-SAME: simulation.randomize_nested_dispatch_path = [#simulation.random_object_step<field = @child, storage = <@Node>>]
  // CHECK-SAME: simulation.randomize_nested_plans = [#simulation.random_nested_selection<field = @child, path = [@child], target = @Leaf>, #simulation.random_nested_selection<field = @child>]
  func.func private @randomize_plan() attributes {
    simulation.random_nested_hooks = [#simulation.random_nested_hooks<
      object = #simulation.random_nested_object<field = @child,
        concrete = !simulation.class_handle<@Leaf>,
        storage = !simulation.class_handle<@Node>, outerModeIndex = 1,
        path = [#simulation.random_object_step<field = @next,
          storage = !simulation.class_handle<@Node>,
          concrete = !simulation.class_handle<@Leaf>, modeIndex = 0>]>,
      post = #simulation.randomize_hook<callee = @post, owner = @Leaf,
        source = @s1.post_randomize, captures = [], reads = ["top.limit"]>>],
    simulation.random_post_hook = #simulation.randomize_hook<callee = @post,
      owner = @Leaf, source = @s1.post_randomize>,
    simulation.random_recursive_alias_guards = [#simulation.random_alias_guard<
      object = #simulation.random_nested_object<field = @child,
        concrete = !simulation.class_handle<@Leaf>,
        storage = !simulation.class_handle<@Node>, outerModeIndex = 1,
        path = [#simulation.random_object_step<field = @next,
          storage = !simulation.class_handle<@Node>,
          concrete = !simulation.class_handle<@Leaf>, modeIndex = 0>]>,
      aliasDepth = 0>],
    simulation.randomize_nested_dispatch_path = [
      #simulation.random_object_step<field = @child,
        storage = !simulation.class_handle<@Node>>],
    simulation.randomize_nested_plans = [
      #simulation.random_nested_selection<field = @child, path = [@child],
                                          target = @Leaf>,
      #simulation.random_nested_selection<field = @child>]}

  // CHECK-LABEL: func.func private @random_properties
  // CHECK-SAME: simulation.random_container_properties = [#simulation.random_container<field = @queue, type = !simulation.queue<i8, 0>, elementType = i8, elementWidth = 8, modeIndex = 2>]
  // CHECK-SAME: simulation.random_properties = [#simulation.random_property<field = @mode, type = i4, width = 4, modeIndex = 0, isSigned = false, isRandC = true, cycle = <key = @key, position = @position, domain = @domain>, domains = [#simulation.random_domain<offset = 0, width = 4, patterns = [#simulation.random_pattern<mask = -4 : i64, value = 4 : i64>]>]>, #simulation.random_property<path = "top.s", type = i8, width = 8, modeIndex = 1, isSigned = true, isRandC = false, modeStorage = 3, member = <path = [1], type = !simulation.unpacked_struct<[#simulation.field<name = "a", type = i1, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>]>, parentModeMask = 0, staticModeMask = 3>>]
  // CHECK-SAME: simulation.random_struct_members = [#simulation.random_struct_member<path = [0], semanticType = i4, type = i4, isRandC = true, modeIndex = 1, cycle = <key = "top.k", position = "top.p", domain = "top.d">, elementModeStorage = 7>]
  func.func private @random_properties() attributes {
    simulation.random_container_properties = [#simulation.random_container<
      field = @queue, type = !simulation.queue<i8, 0>, elementType = i8,
      elementWidth = 8, modeIndex = 2, inertClassHandles = false>],
    simulation.random_properties = [
      #simulation.random_property<field = @mode, type = i4, width = 4,
        modeIndex = 0, isSigned = false, isRandC = true,
        cycle = #simulation.random_cycle_state<key = @key,
                                               position = @position,
                                               domain = @domain>,
        domains = [#simulation.random_domain<offset = 0, width = 4,
          patterns = [#simulation.random_pattern<mask = -4 : i64,
                                                 value = 4 : i64>]>]>,
      #simulation.random_property<path = "top.s", type = i8, width = 8,
        modeIndex = 1, isSigned = true, isRandC = false, modeStorage = 3,
        member = #simulation.random_member_storage<path = [1],
          type = !simulation.unpacked_struct<[#simulation.field<name = "a", type = i1, ordinal = 0, packedOffset = 0>, #simulation.field<name = "b", type = i8, ordinal = 1, packedOffset = 0>]>,
          parentModeMask = 0, staticModeMask = 3>>],
    simulation.random_struct_members = [#simulation.random_struct_member<
      path = [0], semanticType = i4, type = i4, isRandC = true,
      modeIndex = 1,
      cycle = #simulation.random_cycle_state<key = "top.k",
                                             position = "top.p",
                                             domain = "top.d">,
      elementModeStorage = 7>]}

  // CHECK-LABEL: func.func private @call_contract
  // CHECK-SAME: simulation.callee_formals = [#simulation.callee_formal<direction = in, type = i32, isSigned = true, argumentRef = false, isStatic = false>, #simulation.callee_formal<direction = ref, type = i8, isSigned = false, argumentRef = true, isStatic = false, semanticType = i8>]
  // CHECK-SAME: simulation.virtual_interface_callees = [#simulation.virtual_interface_callee<scope = 4, callee = @unit_3, captures = ["top.a"], reads = ["top.b"], writes = []>]
  func.func private @call_contract() attributes {
    simulation.callee_formals = [
      #simulation.callee_formal<direction = in, type = i32, isSigned = true,
                                argumentRef = false, isStatic = false>,
      #simulation.callee_formal<direction = ref, type = i8, isSigned = false,
                                argumentRef = true, isStatic = false,
                                semanticType = i8>],
    simulation.virtual_interface_callees = [
      #simulation.virtual_interface_callee<scope = 4, callee = @unit_3,
        captures = ["top.a"], reads = ["top.b"], writes = []>]}

  // CHECK-LABEL: func.func private @timing_paths
  // CHECK-SAME: simulation.timing_path_monitor_rules = [#simulation.timing_path_monitor<inputs = ["top.clk"], snapshots = ["top.clk_snapshot"], selection = <inputLows = [0], inputWidths = [1], inputLsbs = [0], outputLow = 0, outputWidth = 4, outputRootWidth = 4>, edge = <identifier = 1, pending = "top.pending", epoch = "top.epoch">>]
  // CHECK-SAME: simulation.timing_path_rules = [#simulation.timing_path_rule<inputs = ["top.clk"], snapshots = ["top.clk_snapshot"], polarity = 1, delays = [2, 3], pulseReject = [1, 1], pulseError = [2, 2], pulseOnDetect = true, selection = <inputLows = [0], inputWidths = [1], inputLsbs = [0], outputLow = 0, outputWidth = 4, outputRootWidth = 4, full = true, driverNode = 7>, edge = <identifier = 1, pending = "top.pending", epoch = "top.epoch">, procedural = <site = 3, wakeKind = 1, monitor = true>, condition = <kind = if, group = 0, node = 9, evaluator = @cond, captures = ["top.en"]>>]
  func.func private @timing_paths() attributes {
    simulation.timing_path_monitor_rules = [#simulation.timing_path_monitor<
      inputs = ["top.clk"], snapshots = ["top.clk_snapshot"],
      selection = #simulation.timing_path_selection<inputLows = [0],
        inputWidths = [1], inputLsbs = [0], outputLow = 0, outputWidth = 4,
        outputRootWidth = 4>,
      edge = #simulation.timing_path_edge<identifier = 1,
        pending = "top.pending", epoch = "top.epoch">>],
    simulation.timing_path_rules = [#simulation.timing_path_rule<
      inputs = ["top.clk"], snapshots = ["top.clk_snapshot"], polarity = 1,
      delays = [2, 3], pulseReject = [1, 1], pulseError = [2, 2],
      pulseOnDetect = true,
      selection = #simulation.timing_path_selection<inputLows = [0],
        inputWidths = [1], inputLsbs = [0], outputLow = 0, outputWidth = 4,
        outputRootWidth = 4, full = true, driverNode = 7>,
      edge = #simulation.timing_path_edge<identifier = 1,
        pending = "top.pending", epoch = "top.epoch">,
      procedural = #simulation.timing_path_procedural<site = 3, wakeKind = 1,
                                                      monitor = true>,
      condition = #simulation.timing_path_condition<kind = if, group = 0,
        node = 9, evaluator = @cond, captures = ["top.en"]>>]}

  // CHECK-LABEL: func.func private @udp
  // CHECK-SAME: simulation.udp_metadata = #simulation.udp<name = "latch", ports = ["q", "d"], portDirections = [2, 0], sequential = true, edgeSensitive = false, initValue = "1'b0", rows = [#simulation.udp_row<inputs = "1", state = "?", output = "1", edge = false>]>
  func.func private @udp() attributes {
    simulation.udp_metadata = #simulation.udp<name = "latch", ports = ["q", "d"],
      portDirections = [2, 0], sequential = true, edgeSensitive = false,
      initValue = "1'b0",
      rows = [#simulation.udp_row<inputs = "1", state = "?", output = "1",
                                  edge = false>]>}
}
