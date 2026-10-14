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
}
