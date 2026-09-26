// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

class mailbox_payload;
  int value;
endclass

class fixed_array_holder;
  int values[1:4];
endclass

module native_container_mailbox_scan;
  int queue_values[$];
  int fixed_values[1:4];
  int ordered_values[3:1];
  int dynamic_values[];
  int associative_values[int];
  string text;
  mailbox messages;
  int number;
  string message;
  real fraction;
  mailbox_payload sent_object;
  mailbox_payload received_object;
  logic [7:0] logic_value;
  logic [7:0] logic_output;
  bit [7:0] bit_output;
  int unsigned unsigned_value;
  int status;
  int total;
  fixed_array_holder holder;

  initial begin
    // IEEE 1800-2017 7.6 and 7.10.1: queue and unpacked-array slices are
    // writable, with assignment-compatible element ordering.
    queue_values = '{0, 0, 0, 0};
    queue_values[1:2] = '{11, 12};
    assert (queue_values.size() == 4 && queue_values[1] == 11 &&
            queue_values[2] == 12);
    fixed_values = '{1, 2, 3, 4};
    fixed_values[2:3] = '{8, 9};
    assert (fixed_values[2] == 8 && fixed_values[3] == 9);
    queue_values = '{1, 2, 3, 4};
    fixed_values = '{1, 2, 3, 4};
    fixed_values[2:3] <= '{10, 11};
    queue_values[0] = 7;
    fixed_values[1] = 6;
    holder = new;
    holder.values = '{1, 2, 3, 4};
    holder.values[2:3] <= '{12, 13};
    holder.values[4] <= 14;
    holder.values[1] = 5;
    #1;
    assert (queue_values[0] == 7 && queue_values[1] == 2 &&
            queue_values[2] == 3);
    assert (fixed_values[1] == 6 && fixed_values[2] == 10 &&
            fixed_values[3] == 11);
    assert (holder.values[1] == 5 && holder.values[2] == 12 &&
            holder.values[3] == 13 && holder.values[4] == 14);

    // IEEE 1800-2017 7.12.2 includes fixed-size unpacked arrays. Declared
    // indices remain available to an iterator while values are reordered.
    ordered_values = '{3, 1, 2};
    ordered_values.sort();
    assert (ordered_values[3] == 1 && ordered_values[2] == 2 &&
            ordered_values[1] == 3);
    ordered_values.reverse();
    assert (ordered_values[3] == 3 && ordered_values[2] == 2 &&
            ordered_values[1] == 1);
    ordered_values = '{30, 20, 10};
    ordered_values.sort(item) with (item.index());
    assert (ordered_values[3] == 10 && ordered_values[2] == 20 &&
            ordered_values[1] == 30);
    ordered_values.shuffle();
    total = ordered_values.sum();
    assert (total == 60);

    // A string character is a variable assignment target, including an NBA;
    // it is not, however, a legal ref actual under 13.5.2.
    text = "abc";
    text[1] <= 8'h5a;
    #1;
    assert (text == "aZc");

    // IEEE 1800-2017 21.3.4 copies out only successful conversions. Captured
    // destinations retain container, aggregate, and character writeback.
    queue_values[0] = 0;
    queue_values[1] = 11;
    fixed_values[4] = 0;
    text = "abc";
    status = $sscanf("21 22 Q", "%d %d %c", queue_values[0],
                     fixed_values[4], text[2]);
    assert (status == 3 && queue_values[0] == 21 && fixed_values[4] == 22 &&
            text == "abQ");
    status = $sscanf("bad", "%d", queue_values[1]);
    assert (status == 0 && queue_values[1] == 11);
    dynamic_values = new[1];
    dynamic_values[0] = 0;
    associative_values[5] = 0;
    status = $sscanf("31 32 33", "%d %d %d", dynamic_values[0],
                     associative_values[5], holder.values[4]);
    assert (status == 3 && dynamic_values[0] == 31 &&
            associative_values[5] == 32 && holder.values[4] == 33);

    // IEEE 1800-2017 15.4.3-15.4.9: a nonparameterized mailbox records each
    // message's type. A mismatch is negative, copies nothing, and removes
    // nothing; heterogeneous matching gets preserve their exact payloads.
    messages = new;
    number = 42;
    message = "hello";
    fraction = 1.5;
    messages.put(number);
    messages.put(message);
    messages.put(fraction);
    message = "kept";
    status = messages.try_get(message);
    assert (status < 0 && message == "kept" && messages.num() == 3);
    number = 0;
    messages.get(number);
    message = "";
    messages.get(message);
    fraction = 0.0;
    messages.get(fraction);
    assert (number == 42 && message == "hello" && fraction == 1.5 &&
            messages.num() == 0);

    sent_object = new;
    sent_object.value = 55;
    logic_value = 8'hx5;
    messages.put(sent_object);
    messages.put(logic_value);
    messages.get(received_object);
    assert (received_object == sent_object && received_object.value == 55);
    bit_output = 8'haa;
    status = messages.try_peek(bit_output);
    assert (status < 0 && bit_output == 8'haa && messages.num() == 1);
    messages.get(logic_output);
    assert (logic_output === 8'hx5);
    status = messages.try_get(logic_output);
    assert (status == 0);

    // Signedness and state domain participate in mailbox type equivalence,
    // even when the normalized storage width is otherwise identical.
    unsigned_value = 7;
    messages.put(unsigned_value);
    number = 9;
    status = messages.try_get(number);
    assert (status < 0 && number == 9 && messages.num() == 1);
    unsigned_value = 0;
    messages.get(unsigned_value);
    assert (unsigned_value == 7);

    $display("container-mailbox-scan-pass");
    $finish;
  end
endmodule

// CHECK: container-mailbox-scan-pass
