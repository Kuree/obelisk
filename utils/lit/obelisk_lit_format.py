"""Lit test format for the Obelisk regression suite."""

import re

import lit.formats
import lit.Test
import lit.TestRunner


class ObeliskShTest(lit.formats.ShTest):
    """ShTest that keeps RUN scripts small and free of text-munging tools.

    Each RUN command is a serial step of one lit job, so a file may hold at
    most MAX_RUN_COMMANDS of them; independent cases belong in separate files
    or in `// -----` sections under --split-input-file. Inputs are written as
    checked-in files or split-file sections, never generated or edited with
    shell tools, and binary blobs are printed by obelisk-opt test passes.
    """

    MAX_RUN_COMMANDS = 8
    BANNED_TOOLS = re.compile(
        r"(?:^|[|;&(]|\s)(sed|awk|perl|grep|echo|printf|%python|python3?|"
        r"(?:/bin/)?(?:ba)?sh\s+-c)(?=\s|$)"
    )

    def execute(self, test, litConfig):
        commands = []
        continued = False
        for _, _, line in lit.TestRunner.parseIntegratedTestScriptCommands(
            test.getSourcePath(), ["RUN:"]
        ):
            line = line.strip()
            if continued:
                commands[-1] += " " + line
            else:
                commands.append(line)
            continued = commands[-1].endswith("\\")
            if continued:
                commands[-1] = commands[-1][:-1]
        problems = []
        if len(commands) > self.MAX_RUN_COMMANDS:
            problems.append(
                "test has {} RUN commands; the limit is {}".format(
                    len(commands), self.MAX_RUN_COMMANDS
                )
            )
        for command in commands:
            match = self.BANNED_TOOLS.search(command)
            if match:
                problems.append(
                    "RUN command uses '{}': {}".format(match.group(1), command)
                )
        if problems:
            return lit.Test.Result(lit.Test.FAIL, "\n".join(problems) + "\n")
        return super().execute(test, litConfig)
