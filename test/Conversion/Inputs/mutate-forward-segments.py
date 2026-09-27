"""Derive boundary and wide-ready-set MLIR cases from the readable chain."""
import re
import sys
from pathlib import Path

mode, path = sys.argv[1:]
text = Path(path).read_text()
if mode == 'external-split':
    # Extend the existing VPI-clock fixture into a budget-split copy chain.
    # The native and required-bytecode images use this same semantic input.
    text = text[text.index('module attributes {'):]
    text = text.replace('module attributes {',
                        'module attributes {schedule.native.max_inline_ops = 700 : i64,', 1)
    text = text.replace('design hierarchy "external_clock.count"', 'design')
    start = text.index('    obelisk_sim.func @relay2(')
    end = text.index('\n    }\n', start) + len('\n    }\n')
    template = text[start:end]
    declarations, spawns, functions = [], [], []
    previous = '%visible'
    for relay in range(3, 13):
        hierarchy = ' hierarchy "external_clock.count"' if relay == 12 else ''
        declarations.append(
            f'    obelisk_sim.code_unit.decl {relay + 2} in 0 always_comb hierarchy "external_clock.relay{relay}"\n'
            f'    obelisk_sim.storage.decl {relay + 1} in 0 : !obelisk_sim.logic<32> design{hierarchy}\n')
        output = f'%relay_value_{relay}'
        spawns.append(
            f'      {output} = obelisk_sim.context.storage %ctx[{relay + 1}] : !obelisk_sim.ref<!obelisk_sim.logic<32>>\n'
            f'      %relay{relay} = obelisk_sim.spawn @relay{relay}(%ctx, {previous}, {output}) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<32>>, !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.process\n')
        previous = output
        body = template.replace('@relay2(', f'@relay{relay}(')
        body = re.sub(r'(obelisk_sim.descriptor_id = )([23])',
                      lambda match: match[1] + str(relay + int(match[2]) - 2), body)
        body = body.replace('code_unit_id = 4 : i64', f'code_unit_id = {relay + 2} : i64')
        body = body.replace('continuation<id = 3>', f'continuation<id = {relay + 1}>')
        functions.append(body)
    text = text.replace('    obelisk_sim.func @root(',
                        ''.join(declarations) + '    obelisk_sim.func @root(', 1)
    text = text.replace('      %process = obelisk_sim.spawn @counter(',
                        ''.join(spawns) + '      %process = obelisk_sim.spawn @counter(', 1)
    end = text.rindex('\n  }\n}')
    text = text[:end] + '\n' + ''.join(functions) + text[end:]
    print(text)
    sys.exit(0)
text = '\n'.join(line.strip() for line in text[text.index('!ref ='):].splitlines())
if mode in ('nba', 'feedback', 'skip'):
    start = text.index('obelisk_sim.func @step3(')
    end = text.index('obelisk_sim.func @step4(', start)
    body = text[start:end]
    store = 'obelisk_sim.ref.store %next to %output : i8, !vref'
    if mode == 'nba':
        body = body.replace(store, 'obelisk_sim.nba.enqueue %next to %output : (i8, !vref) -> ()')
    elif mode == 'feedback':
        # An actual unchanged store terminates, but the static write-back
        # requires convergence handling; values do not prove acyclicity.
        body = body.replace(store, store + '\nobelisk_sim.ref.store %v to %input : i8, !vref')
    else:
        body = body.replace(store, '''%two = arith.constant 2 : i8
%skip = arith.cmpi eq, %v, %two : i8
cf.cond_br %skip, ^wait, ^publish
^publish:
''' + store)
        # Count downstream activations so speculative execution cannot pass
        # merely by storing the same result. Only the first edge reaches step5.
        text = text.replace('obelisk_sim.storage.decl 5 in 0 : i8 design',
                            'obelisk_sim.storage.decl 5 in 0 : i8 design\nobelisk_sim.storage.decl 6 in 0 : i8 design')
        text = text.replace('%s5 = obelisk_sim.context.storage %ctx[5] : !vref',
                            '%s5 = obelisk_sim.context.storage %ctx[5] : !vref\n%count = obelisk_sim.context.storage %ctx[6] : !vref')
        text = text.replace('@step5(%ctx, %s4, %s5) : !obelisk_sim.context, !vref, !vref',
                            '@step5(%ctx, %s4, %s5, %count) : !obelisk_sim.context, !vref, !vref, !vref')
        text = text.replace('obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 3',
                            'obelisk_sim.descriptor_id = 5 : i64}, %count: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 6 : i64}) attributes {entry_kind = 3')
        step5 = text.index('obelisk_sim.func @step5(')
        pos = text.index(store, step5)
        text = text[:pos] + '''%oldcount = obelisk_sim.ref.load %count : !vref -> i8
%newcount = arith.addi %oldcount, %one : i8
obelisk_sim.ref.store %newcount to %count : i8, !vref
''' + text[pos:]
        # Observe the activation count instead of the terminal chain value.
        text = text.replace('%output = obelisk_sim.context.storage %ctx[5] : !vref',
                            '%output = obelisk_sim.context.storage %ctx[6] : !vref')
        start = text.index('obelisk_sim.func @step3(')
        end = text.index('obelisk_sim.func @step4(', start)
    text = text[:start] + body + text[end:]
elif mode == 'budget':
    text = text.replace('schedule.native_scheduler = 3 : i32',
                        'schedule.native_scheduler = 3 : i32, schedule.native.max_inline_ops = 1 : i64')
elif mode == 'reconvergent':
    text = text.replace('@step3(%ctx, %s2, %s3)', '@step3(%ctx, %s1, %s3)')
    start = text.index('obelisk_sim.func @step3(')
    end = text.index('obelisk_sim.func @step4(', start)
    body = text[start:end].replace('descriptor_id = 2 : i64', 'descriptor_id = 1 : i64')
    text = text[:start] + body + text[end:]
    text = text.replace('@step4(%ctx, %s3, %s4) : !obelisk_sim.context, !vref, !vref',
                        '@step4(%ctx, %s3, %s4, %s2) : !obelisk_sim.context, !vref, !vref, !vref')
    start = text.index('obelisk_sim.func @step4(')
    end = text.index('obelisk_sim.func @step5(', start)
    body = text[start:end].replace('descriptor_id = 4 : i64}) attributes',
        'descriptor_id = 4 : i64}, %left: !vref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes')
    body = body.replace('suspend.change %input to ^body', 'suspend.any %input, %left edges [0, 0] to ^body')
    body = body.replace('continuation<id = 5>} : !vref', 'continuation<id = 5>} : !vref, !vref')
    body = body.replace('%one = arith.constant 1 : i8', '%one = obelisk_sim.ref.load %left : !vref -> i8')
    text = text[:start] + body + text[end:]
elif mode == 'pulse':
    # Publish a transition and restore the old value before the downstream
    # actor executes. Its already-pending activation must survive restoration.
    start = text.index('obelisk_sim.func @step1(')
    end = text.index('obelisk_sim.func @step2(', start)
    body = text[start:end].replace('obelisk_sim.ref.store %next to %output : i8, !vref', '''%before = obelisk_sim.ref.load %output : !vref -> i8
%pulse = arith.constant 255 : i8
obelisk_sim.ref.store %pulse to %output : i8, !vref
obelisk_sim.ref.store %before to %output : i8, !vref''')
    text = text[:start] + body + text[end:]
elif mode in ('large', 'large-ssa'):
    count = 70
    first = text.index('obelisk_sim.func @step1(')
    last = text.index('obelisk_sim.func @check(')
    actors = text[first:last]
    step1 = actors[:actors.index('obelisk_sim.func @step2(')]
    step2 = actors[actors.index('obelisk_sim.func @step2('):actors.index('obelisk_sim.func @step3(')]
    actors = [step1]
    for index in range(2, count + 1):
        body = step2.replace('@step2(', f'@step{index}(')
        body = re.sub(r'descriptor_id = ([12]) : i64',
                      lambda m: f'descriptor_id = {index - 2 + int(m[1])} : i64', body)
        body = body.replace('code_unit_id = 4 : i64', f'code_unit_id = {index + 2} : i64')
        body = body.replace('continuation<id = 3>', f'continuation<id = {index + 1}>')
        actors.append(body)
    text = text[:first] + ''.join(actors) + text[last:]
    text = text.replace('entry_kind = 1 : i32, code_unit_id = 8 : i64',
                        f'entry_kind = 1 : i32, code_unit_id = {count + 3} : i64')
    text = text.replace('code_unit.decl 8 in 0 initial', f'code_unit.decl {count + 3} in 0 initial')
    kind = 'continuous' if 'entry_kind = 7 : i32' in step2 else 'always'
    text = text.replace('obelisk_sim.scope.decl 0', 'obelisk_sim.scope.decl 0\n' + '\n'.join(
        f'obelisk_sim.storage.decl {i} in 0 : i8 design\n'
        f'obelisk_sim.code_unit.decl {i+2} in 0 {kind} hierarchy "step{i}"'
        for i in range(6, count+1)))
    start = text.index('%p5 = obelisk_sim.spawn')
    end = text.index('%c = obelisk_sim.spawn', start)
    text = text[:start] + '\n'.join(
        f'%s{i} = obelisk_sim.context.storage %ctx[{i}] : !vref'
        for i in range(6, count+1)) + '\n' + '\n'.join(
        f'%p{i} = obelisk_sim.spawn @step{i}(%ctx, ' +
        ('%clk' if i == 1 else f'%s{i-1}') + f', %s{i}) : !obelisk_sim.context, ' +
        ('!ref' if i == 1 else '!vref') + ', !vref -> !obelisk_sim.process'
        for i in range(count, 0, -1)) + '\n' + text[end:]
    text = text.replace('%output = obelisk_sim.context.storage %ctx[5] : !vref',
                        f'%output = obelisk_sim.context.storage %ctx[{count}] : !vref')
    if mode == 'large-ssa':
        text = text.replace('schedule.native_scheduler = 3 : i32',
                            'schedule.native_scheduler = 3 : i32, schedule.native.max_inline_ops = 50000 : i64')
elif mode == 'four-state':
    text = text.replace('!vref = !obelisk_sim.ref<i8>', '!word = !obelisk_sim.logic<8>\n!vref = !obelisk_sim.ref<!word>')
    text = re.sub(r'(storage.decl [1-5] in 0 :) i8', r'\1 !word', text)
    text = text.replace('-> i8', '-> !word').replace(': i8, !vref', ': !word, !vref')
    text = text.replace('%one = arith.constant 1 : i8', '%one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !word')
    text = text.replace('%next = arith.addi %v, %one : i8', '%next = obelisk_sim.logic.binary add %v, %one : !word')
    text = text.replace('%wide = arith.extui %v : i1 to i8', '%high = obelisk_sim.logic.constant 2 : i8, 0 : i8 : !word\n%low = obelisk_sim.logic.constant 255 : i8, 255 : i8 : !word')
    text = text.replace('%next = arith.addi %wide, %one : i8', '%next = arith.select %v, %high, %low : !word')
    text = text.replace(': !obelisk_sim.bytes, i8', ': !obelisk_sim.bytes, !word')
    # The third edge recovers from X. All tiers must preserve both transitions.
    marker = '%zero = arith.constant 0 : i32\nobelisk_sim.finish'
    third = '''%thirdDelay = obelisk_sim.time.constant 2
obelisk_sim.suspend.delay %thirdDelay to ^third
^third:
%c = obelisk_sim.ref.load %output : !vref -> !word
%fmt3 = obelisk_sim.bytes.constant "chain %0d"
%channel3 = arith.constant 1 : i32
obelisk_sim.display %ctx to %channel3(%fmt3, %c) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !word
'''
    text = text.replace(marker, third + marker)
else:
    raise ValueError(mode)
print(text)
