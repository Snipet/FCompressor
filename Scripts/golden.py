#!/usr/bin/env python3
"""Scripts/golden.py: FCompressor's front end to FunkGui's tools/golden.py (03 §1.1, §3.2.5).

    Scripts/golden.py report <build>
    Scripts/golden.py diff   <build> [...]
    Scripts/golden.py adopt  <build> [--x86 <build-x86>] --only '<globs>' --reason '<why>'     (lead only)

FunkGui owns the golden format v2 and its tooling; this wrapper runs the golden.py of the FunkGui this build was
configured against (the FunkGui row of <build>/fcmp-deps.txt, so a pin bump brings the matching tool) with
FCompressor's defaults: --golden-root <this repository>/tests/golden for every subcommand, and for `adopt` only
--allow-env FCMP_ALLOW_BLESS (adopt refuses unless FCMP_ALLOW_BLESS=1, and refuses in a linked worktree). Before
FunkGui v0.1.0 only adopt's parser defines --allow-env, so passing it to report or diff made them exit 2 with
"unrecognized arguments" (FZ0 errata, R-B0 #2 / R-G1 #1). Every other argument passes through unchanged.
Agents never run `adopt`; Scripts/verify.sh classifies results itself (SPRINTS §7 D8).
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def funkgui_dir(build):
    deps = os.path.join(build, "fcmp-deps.txt")
    try:
        with open(deps, encoding="utf-8") as f:
            for line in f:
                cols = line.rstrip("\n").split("\t")
                if cols and cols[0] == "FunkGui" and len(cols) >= 4:
                    return cols[3]
    except OSError:
        pass
    sys.exit("golden.py: %s has no FunkGui row (configure the build directory first)" % deps)


def main(argv):
    if len(argv) < 2 or argv[0] in ("-h", "--help"):
        print(__doc__.strip())
        return 0 if argv and argv[0] in ("-h", "--help") else 2
    build = os.path.abspath(argv[1])
    tool = os.path.join(funkgui_dir(build), "tools", "golden.py")
    if not os.path.isfile(tool):
        sys.exit("golden.py: %s does not exist (FunkGui v0.1.0 and later ship tools/golden.py)" % tool)
    args = [sys.executable, tool, argv[0], build]
    if argv[0] == "adopt":
        args += ["--allow-env", "FCMP_ALLOW_BLESS"]
    args += ["--golden-root", os.path.join(ROOT, "tests", "golden")] + argv[2:]
    os.execv(sys.executable, args)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
