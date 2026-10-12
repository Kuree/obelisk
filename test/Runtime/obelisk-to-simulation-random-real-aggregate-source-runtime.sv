// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt --lower-obelisk-to-sim=opt-level=0 | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t %s
// RUN: %t --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.4 and 18.5.8: real leaves of a qualified structure member
// and of an aliased finite object graph share numeric-coordinate variables in
// one compiled plan. A shortreal leaf is an i279 rank and a real leaf an i2100
// rank, so the Owner plan solves i2658 = 2 * 279 + 2100 bits and the Deep plan
// i279; both solves carry a SAMP bundle.
// PLAN-DAG: simulation.random.real_to_rank {{.*}}: f32 -> i279
// PLAN-DAG: simulation.random.real_to_rank {{.*}}: f64 -> i2100
// PLAN-DAG: simulation.random.real_from_rank {{.*}}: i279 -> f32
// PLAN-DAG: simulation.random.real_from_rank {{.*}}: i2100 -> f64
// PLAN-DAG: simulation.random.solve_wide {{.*}}SAMP{{.*}}-> (i2658, i1, i64)
// PLAN-DAG: simulation.random.solve_wide {{.*}}SAMP{{.*}}-> (i279, i1, i64)

// The run checks that, with a fixed seed, parent and child-owned global
// constraints both bind the shared leaf, the ordinary shortreal member keeps
// its value on every commit, and a frozen child field (rand_mode) is kept
// together with its alias. An infeasible frozen value, a null handle behind a
// true state guard and a failed solve preserve every property and the RNG
// state, a false guard sifts the null child away (18.5.8), disabling the
// block drops its constraints, and the inline constraint binds the explicitly
// named class-handle formal of randomize_named.
// RUNTIME: real aggregates valid 1

typedef struct { rand real value; shortreal keep; } Qualified;

class Leaf;
  rand shortreal value;
  constraint own { value >= 0.0; value <= 1.0; }
endclass

class Owner;
  rand Leaf child;
  rand Leaf alias_child;
  rand Qualified members;
  bit check_child = 1;
  constraint joint {
    if (check_child) {
      child.value > 0.25;
      child.value < 0.5;
      alias_child.value > 0.3;
    }
    members.value > -2.0;
    members.value < -1.0;
  }
endclass

class Middle;
  rand Leaf leaf;
  constraint own { leaf.value < 0.5; }
endclass

class Deep;
  rand Middle node;
  constraint own { node.leaf.value > 0.375; }
endclass

function int randomize_named(Owner target);
  return target.randomize() with { target.child.value > 0.4375; };
endfunction

module top;
  Owner o;
  Deep d;
  bit valid = 1;
  int status;
  shortreal previous_child;
  real previous_member;
  string previous_state;
  initial begin
    o = new;
    o.child = new;
    o.alias_child = o.child;
    o.members.keep = 7.25;
    repeat (16) begin
      status = o.randomize();
      valid &= status && o.child.value > 0.3 && o.child.value < 0.5 &&
               o.alias_child.value == o.child.value && o.members.value > -2.0 &&
               o.members.value < -1.0 && o.members.keep == 7.25;
    end
    o.child.value.rand_mode(0);
    previous_child = o.child.value;
    status = o.randomize();
    valid &= status && o.child.value == previous_child &&
             o.alias_child.value == previous_child && o.members.keep == 7.25;
    o.child.value = 0.2;
    previous_child = o.child.value;
    previous_member = o.members.value;
    previous_state = o.get_randstate();
    status = o.randomize();
    valid &= !status && o.child.value == previous_child &&
             o.alias_child.value == previous_child &&
             o.members.value == previous_member && o.get_randstate() == previous_state;
    o.check_child = 0;
    o.child = null;
    o.alias_child = null;
    status = o.randomize();
    valid &= status && o.members.value > -2.0 && o.members.value < -1.0 &&
             o.members.keep == 7.25;
    o.check_child = 1;
    previous_member = o.members.value;
    previous_state = o.get_randstate();
    status = o.randomize();
    valid &= !status && o.members.value == previous_member &&
             o.get_randstate() == previous_state;
    o.joint.constraint_mode(0);
    status = o.randomize();
    valid &= status && o.members.keep == 7.25;
    o.joint.constraint_mode(1);
    o.child = new;
    o.alias_child = new;
    repeat (8) begin
      status = o.randomize();
      valid &= status && o.child.value > 0.25 && o.child.value < 0.5 &&
               o.alias_child.value > 0.3 && o.alias_child.value <= 1.0 &&
               o.members.value > -2.0 && o.members.value < -1.0 && o.members.keep == 7.25;
    end
    status = randomize_named(o);
    valid &= status && o.child.value > 0.4375 && o.child.value < 0.5 &&
             o.alias_child.value > 0.3 && o.alias_child.value <= 1.0 &&
             o.members.value > -2.0 && o.members.value < -1.0 && o.members.keep == 7.25;
    d = new;
    d.node = new;
    d.node.leaf = new;
    repeat (8) begin
      status = d.randomize();
      valid &= status && d.node.leaf.value > 0.375 && d.node.leaf.value < 0.5;
    end
    $display("real aggregates valid %0d", valid);
  end
endmodule
