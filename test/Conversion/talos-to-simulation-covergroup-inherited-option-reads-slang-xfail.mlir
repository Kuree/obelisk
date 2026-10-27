// XFAIL: *
// IEEE 1800-2023 19.4.1 allows a derived cross to replace a base cross
// with the same name. Slang v11.0 diagnoses this as a redefinition.

// RUN: talos --std=1800-2023 --coverage=functional -emit-talos %S/../Coverage/functional-instance-option-reads.sv | talos-opt '--lower-talos-to-sim=opt-level=0' --canonicalize --cse | FileCheck %s --check-prefix=OWNERS

// Embedded inherited receivers use a single class-member load and cast.
// Four accesses call receiver() four times, including the two assignments.
// OWNERS: %[[OBJECT:[0-9]+]] = simulation.call @[[RECEIVER:unit_[0-9]+]]({{.*}}) : {{.*}} -> !simulation.class_handle<@[[CLASS:[^>]+]]>
// OWNERS-NOT: simulation.call @[[RECEIVER]]
// OWNERS: simulation.class.field_ref %[[OBJECT]][
// OWNERS: %[[HANDLE:[0-9]+]] = simulation.covergroup.cast {{.*}} to !simulation.covergroup_handle<@[[GROUP:[^>]+]]>
// OWNERS: simulation.covergroup.get_string_option {{.*}}, %[[HANDLE]] item 0 option name
// OWNERS: simulation.call @[[RECEIVER]]
// OWNERS-NOT: simulation.call @[[RECEIVER]]
// OWNERS: simulation.covergroup.get_string_option {{.*}} item [[INHERITED_POINT:-?[0-9]+]] option comment : !simulation.context, !simulation.covergroup_handle<@[[GROUP]]>
// OWNERS: simulation.call @[[RECEIVER]]
// OWNERS-NOT: simulation.call @[[RECEIVER]]
// OWNERS: simulation.covergroup.set_string_option {{.*}} item 0 option comment
// OWNERS: simulation.call @[[RECEIVER]]
// OWNERS-NOT: simulation.call @[[RECEIVER]]
// OWNERS: simulation.covergroup.set_string_option {{.*}} item [[INHERITED_POINT]] option comment
// OWNERS-NOT: simulation.call @[[RECEIVER]]
// OWNERS: simulation.finish
