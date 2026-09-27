// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

// Each classification has a distinct attribute type, including classifications
// whose runtime encodings overlap. Flags and reverse traversal round-trip.
module attributes {
  test.element = #simulation.element_kind<logic>,
  test.flags = #simulation.element_flags<four_state|signed>,
  test.container = #simulation.container_kind<queue>,
  test.key = #simulation.assoc_key_kind<wildcard>,
  test.distribution = #simulation.random_distribution<chi_square>,
  test.queue = #simulation.stochastic_queue_action<remove>,
  test.assertion = #simulation.assertion_control_action<nonvacuous_on>,
  test.event = #simulation.block_event_kind<end>,
  test.radix = #simulation.radix<hex>,
  test.traversal = #simulation.assoc_traversal_direction<backward>,
  test.visibility = #simulation.member_visibility<protected>
} {}

// CHECK-DAG: #simulation.element_kind<logic>
// CHECK-DAG: #simulation.element_flags<four_state|signed>
// CHECK-DAG: #simulation.container_kind<queue>
// CHECK-DAG: #simulation.assoc_key_kind<wildcard>
// CHECK-DAG: #simulation.random_distribution<chi_square>
// CHECK-DAG: #simulation.stochastic_queue_action<remove>
// CHECK-DAG: #simulation.assertion_control_action<nonvacuous_on>
// CHECK-DAG: #simulation.block_event_kind<end>
// CHECK-DAG: #simulation.radix<hex>
// CHECK-DAG: #simulation.assoc_traversal_direction<backward>
// CHECK-DAG: #simulation.member_visibility<protected>
