// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

module native_container_reference_persistence;
  int queue_values[$];
  int bounded_values[$:2];
  int associative_values[int];
  int dynamic_values[];

  task automatic shift_and_delete(ref int item);
    // IEEE 1800-2017 7.10.3: insertion moves a reference with its existing
    // element; deletion makes only the removed element's reference outdated.
    queue_values.push_front(5);
    assert (item == 20);
    item = 21;
    assert (queue_values[2] == 21);
    queue_values.delete(2);
    assert (item == 21);
    item = 99;
    assert (queue_values.size() == 3 && queue_values[0] == 5 &&
            queue_values[1] == 10 && queue_values[2] == 30);
  endtask

  task automatic ordinary_element_writes(ref int first, ref int last);
    queue_values[0] = 11;
    queue_values[2] = 31;
    assert (first == 11 && last == 31);
  endtask

  task automatic order_references(ref int first, ref int middle);
    // Ordering methods move element identity as well as value. Neither method
    // is one of the operations that 13.5.2 says makes a reference outdated.
    queue_values.reverse();
    assert (first == 3 && middle == 1);
    first = 30;
    assert (queue_values[2] == 30);
    queue_values.sort();
    assert (first == 30 && middle == 1);
    middle = 10;
    assert (queue_values[0] == 10 && queue_values[1] == 2 &&
            queue_values[2] == 30);
  endtask

  task automatic bounded_references(ref int kept, ref int dropped);
    bounded_values.push_front(5);
    assert (kept == 10 && dropped == 30);
    kept = 11;
    dropped = 99;
    assert (bounded_values.size() == 3 && bounded_values[0] == 5 &&
            bounded_values[1] == 11 && bounded_values[2] == 20);
  endtask

  task automatic associative_references(ref int removed, ref int kept);
    associative_values[2] = 44;
    assert (kept == 44);
    associative_values.delete(1);
    assert (removed == 41 && kept == 44);
    removed = 90;
    kept = 43;
    assert (!associative_values.exists(1) && associative_values[2] == 43);
  endtask

  task automatic replace_dynamic(ref int item);
    dynamic_values = new[1](dynamic_values);
    assert (item == 32);
    item = 77;
    assert (dynamic_values.size() == 1 && dynamic_values[0] == 30);
  endtask

  task automatic replace_queue(ref int item);
    queue_values = '{7};
    assert (item == 20);
    item = 88;
    assert (queue_values.size() == 1 && queue_values[0] == 7);
  endtask

  task automatic clear_associative(ref int first, ref int second);
    associative_values.delete();
    assert (first == 51 && second == 52);
    first = 61;
    second = 62;
    assert (associative_values.num() == 0);
  endtask

  task automatic order_dynamic(ref int first, ref int middle);
    dynamic_values.reverse();
    assert (first == 3 && middle == 1);
    first = 30;
    dynamic_values.sort();
    middle = 10;
    assert (dynamic_values[0] == 10 && dynamic_values[1] == 2 &&
            dynamic_values[2] == 30);
  endtask

  initial begin
    queue_values = '{10, 20, 30};
    ordinary_element_writes(queue_values[0], queue_values[2]);
    assert (queue_values[0] == 11 && queue_values[2] == 31);
    queue_values = '{10, 20, 30};
    shift_and_delete(queue_values[1]);

    queue_values = '{3, 1, 2};
    order_references(queue_values[0], queue_values[1]);

    bounded_values = '{10, 20, 30};
    bounded_references(bounded_values[0], bounded_values[2]);

    associative_values[1] = 41;
    associative_values[2] = 42;
    associative_references(associative_values[1], associative_values[2]);

    dynamic_values = new[3];
    dynamic_values[0] = 30;
    dynamic_values[1] = 31;
    dynamic_values[2] = 32;
    replace_dynamic(dynamic_values[2]);

    queue_values = '{10, 20, 30};
    replace_queue(queue_values[1]);

    associative_values[1] = 51;
    associative_values[2] = 52;
    clear_associative(associative_values[1], associative_values[2]);

    dynamic_values = new[3];
    dynamic_values[0] = 3;
    dynamic_values[1] = 1;
    dynamic_values[2] = 2;
    order_dynamic(dynamic_values[0], dynamic_values[1]);

    $display("reference-persistence-pass");
    $finish;
  end
endmodule

// CHECK: reference-persistence-pass
