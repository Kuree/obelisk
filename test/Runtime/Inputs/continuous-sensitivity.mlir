
!inner = !simulation.unpacked_array<0 : 3 x !simulation.logic<8>>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  simulation.design @prefix {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 3 in 0 initial hierarchy "drive"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<!simulation.logic<32>>, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %drive = simulation.spawn @drive(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<!simulation.logic<32>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<!simulation.logic<32>> -> !simulation.logic<32>
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, !simulation.logic<32>) -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %element : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %output : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<!simulation.logic<32>>
    }
    simulation.func @drive(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      cf.br ^step0
    ^step0:
      %root0 = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %cell0 = simulation.ref.subelement %root0[[1, 0]] : !simulation.ref<!outer> -> !simulation.ref<!simulation.logic<8>>
      %value0 = simulation.logic.constant 18 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value0 to %cell0 : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %idx0 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index0 = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index0 to %idx0 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay0 = simulation.time.constant 1
      simulation.suspend.delay %delay0 to ^print0
    ^print0:
      %out0 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample0 = simulation.ref.load %out0 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt0 = simulation.bytes.constant "prefix step0=%h"
      %channel0 = arith.constant 1 : i32
      simulation.display %ctx to %channel0(%fmt0, %sample0) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step1
    ^step1:
      %root1 = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %cell1 = simulation.ref.subelement %root1[[1, 1]] : !simulation.ref<!outer> -> !simulation.ref<!simulation.logic<8>>
      %value1 = simulation.logic.constant 255 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value1 to %cell1 : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %idx1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index1 = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index1 to %idx1 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay1 = simulation.time.constant 1
      simulation.suspend.delay %delay1 to ^print1
    ^print1:
      %out1 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample1 = simulation.ref.load %out1 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt1 = simulation.bytes.constant "prefix step1=%h"
      %channel1 = arith.constant 1 : i32
      simulation.display %ctx to %channel1(%fmt1, %sample1) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step2
    ^step2:
      %idx2 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index2 = simulation.logic.constant 0 : i32, -1 : i32 : !simulation.logic<32>
      simulation.ref.store %index2 to %idx2 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay2 = simulation.time.constant 1
      simulation.suspend.delay %delay2 to ^print2
    ^print2:
      %out2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample2 = simulation.ref.load %out2 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt2 = simulation.bytes.constant "prefix step2=%h"
      %channel2 = arith.constant 1 : i32
      simulation.display %ctx to %channel2(%fmt2, %sample2) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step3
    ^step3:
      %idx3 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index3 = simulation.logic.constant 0 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index3 to %idx3 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay3 = simulation.time.constant 1
      simulation.suspend.delay %delay3 to ^print3
    ^print3:
      %out3 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample3 = simulation.ref.load %out3 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt3 = simulation.bytes.constant "prefix step3=%h"
      %channel3 = arith.constant 1 : i32
      simulation.display %ctx to %channel3(%fmt3, %sample3) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step4
    ^step4:
      %idx4 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index4 = simulation.logic.constant -1 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index4 to %idx4 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay4 = simulation.time.constant 1
      simulation.suspend.delay %delay4 to ^print4
    ^print4:
      %out4 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample4 = simulation.ref.load %out4 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt4 = simulation.bytes.constant "prefix step4=%h"
      %channel4 = arith.constant 1 : i32
      simulation.display %ctx to %channel4(%fmt4, %sample4) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step5
    ^step5:
      %root5 = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %cell5 = simulation.ref.subelement %root5[[1, 3]] : !simulation.ref<!outer> -> !simulation.ref<!simulation.logic<8>>
      %value5 = simulation.logic.constant 86 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value5 to %cell5 : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %idx5 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index5 = simulation.logic.constant 3 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index5 to %idx5 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay5 = simulation.time.constant 1
      simulation.suspend.delay %delay5 to ^print5
    ^print5:
      %out5 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample5 = simulation.ref.load %out5 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt5 = simulation.bytes.constant "prefix step5=%h"
      %channel5 = arith.constant 1 : i32
      simulation.display %ctx to %channel5(%fmt5, %sample5) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step6
    ^step6:
      %root6 = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %cell6 = simulation.ref.subelement %root6[[1, 3]] : !simulation.ref<!outer> -> !simulation.ref<!simulation.logic<8>>
      %value6 = simulation.logic.constant 120 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value6 to %cell6 : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %delay6 = simulation.time.constant 1
      simulation.suspend.delay %delay6 to ^print6
    ^print6:
      %out6 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample6 = simulation.ref.load %out6 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt6 = simulation.bytes.constant "prefix step6=%h"
      %channel6 = arith.constant 1 : i32
      simulation.display %ctx to %channel6(%fmt6, %sample6) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step7
    ^step7:
      %root7 = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %cell7 = simulation.ref.subelement %root7[[0, 3]] : !simulation.ref<!outer> -> !simulation.ref<!simulation.logic<8>>
      %value7 = simulation.logic.constant 153 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value7 to %cell7 : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %delay7 = simulation.time.constant 1
      simulation.suspend.delay %delay7 to ^print7
    ^print7:
      %out7 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample7 = simulation.ref.load %out7 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt7 = simulation.bytes.constant "prefix step7=%h"
      %channel7 = arith.constant 1 : i32
      simulation.display %ctx to %channel7(%fmt7, %sample7) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step8
    ^step8:
      %root8 = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %cell8 = simulation.ref.subelement %root8[[1, 2]] : !simulation.ref<!outer> -> !simulation.ref<!simulation.logic<8>>
      %value8 = simulation.logic.constant 52 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value8 to %cell8 : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %delay8 = simulation.time.constant 1
      simulation.suspend.delay %delay8 to ^print8
    ^print8:
      %out8 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample8 = simulation.ref.load %out8 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt8 = simulation.bytes.constant "prefix step8=%h"
      %channel8 = arith.constant 1 : i32
      simulation.display %ctx to %channel8(%fmt8, %sample8) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step9
    ^step9:
      %idx9 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index9 = simulation.logic.constant 2 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index9 to %idx9 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay9 = simulation.time.constant 1
      simulation.suspend.delay %delay9 to ^print9
    ^print9:
      %out9 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample9 = simulation.ref.load %out9 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt9 = simulation.bytes.constant "prefix step9=%h"
      %channel9 = arith.constant 1 : i32
      simulation.display %ctx to %channel9(%fmt9, %sample9) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step10
    ^step10:
      %idx10 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index10 = simulation.logic.constant 4 : i32, 0 : i32 : !simulation.logic<32>
      simulation.ref.store %index10 to %idx10 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay10 = simulation.time.constant 1
      simulation.suspend.delay %delay10 to ^print10
    ^print10:
      %out10 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample10 = simulation.ref.load %out10 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt10 = simulation.bytes.constant "prefix step10=%h"
      %channel10 = arith.constant 1 : i32
      simulation.display %ctx to %channel10(%fmt10, %sample10) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      cf.br ^step11
    ^step11:
      %idx11 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<32>>
      %index11 = simulation.logic.constant -1 : i32, -1 : i32 : !simulation.logic<32>
      simulation.ref.store %index11 to %idx11 : !simulation.logic<32>, !simulation.ref<!simulation.logic<32>>
      %delay11 = simulation.time.constant 1
      simulation.suspend.delay %delay11 to ^print11
    ^print11:
      %out11 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %sample11 = simulation.ref.load %out11 : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %fmt11 = simulation.bytes.constant "prefix step11=%h"
      %channel11 = arith.constant 1 : i32
      simulation.display %ctx to %channel11(%fmt11, %sample11) newline = true radix = <hex> flags = [0, 0] : !simulation.bytes, !simulation.logic<8>
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
