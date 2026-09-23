#!/usr/bin/env python3
"""Scripts/sprint/ownership.py <manifest>#<ID> <worktree> [--base <rev>]

The [OWN] check of the definition of done (03 §4.6; SPRINTS §0.1 rule 5, §0.2): every path the task changed must match
one of its manifest's OWNS globs.

  <manifest>#<ID>  a sprint file and a task, e.g. docs/sprints/s0.md#B0. The manifest is the fenced text block under
                   the heading "## <ID>" whose first line is "TASK <ID> ..." (docs/sprints/s0.md, "Manifest format").
                   A field starts at column 0 with an upper-case keyword; indented lines continue it. OWNS is a
                   whitespace-separated list of globs relative to the task's repository root: `*` matches within one
                   path segment, `**` any number of segments, `?` one character, `{a,b}` alternatives (nested allowed).
                   Parenthesised text inside OWNS is a comment.
  <worktree>       the task's worktree (FCompressor, or FunkGui when the lead checks a FunkGui card from here).
  --base <rev>     the commit the task started from. Default: `git merge-base HEAD main` when a `main` branch exists,
                   so a handoff commit on the task's own branch is still checked.

Changed paths = the working tree's status (untracked files included, both sides of a rename) plus the files that differ
between <base> and HEAD. Paths under .claude/ are ignored. Exit 0 clean, 1 a path outside OWNS, 2 usage or input error.
"""
import argparse
import os
import re
import subprocess
import sys

FIELDS = {"TASK", "GOAL", "OWNS", "FROZEN", "FUNKGUI", "PRESETS", "DONE", "READS", "INPUTS", "DELIVERABLES", "NOTES"}


def fail(msg):
    print("ownership: " + msg, file=sys.stderr)
    sys.exit(2)


def parse_manifest(path, task):
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError as e:
        fail("cannot read %s: %s" % (path, e))
    heading = re.compile(r"^##\s+" + re.escape(task) + r"\s*$")
    start = next((i for i, l in enumerate(lines) if heading.match(l)), None)
    if start is None:
        fail("%s has no heading '## %s'" % (path, task))
    i = start + 1
    while i < len(lines) and not lines[i].startswith("```"):
        if lines[i].startswith("## "):
            fail("%s: no fenced manifest block under '## %s'" % (path, task))
        i += 1
    if i >= len(lines):
        fail("%s: no fenced manifest block under '## %s'" % (path, task))
    end = next((j for j in range(i + 1, len(lines)) if lines[j].startswith("```")), None)
    if end is None:
        fail("%s: the manifest block of %s is not closed" % (path, task))
    block = lines[i + 1:end]
    if not block or not re.match(r"^TASK\s+" + re.escape(task) + r"(\s|$)", block[0]):
        fail("%s: the manifest block of %s must start with 'TASK %s'" % (path, task, task))
    fields, current = {}, None
    for n, line in enumerate(block, start=i + 2):
        m = re.match(r"^([A-Z]+)(?:\s+(.*))?$", line)
        if m and m.group(1) in FIELDS:
            current = m.group(1)
            fields[current] = [m.group(2) or ""]
        elif line[:1].isspace() and current:
            fields[current].append(line.strip())
        elif line.strip():
            fail("%s:%d: not a field or a continuation line: %r" % (path, n, line))
    if "OWNS" not in fields:
        fail("%s: the manifest of %s has no OWNS field" % (path, task))
    owns = re.sub(r"\([^)]*\)", " ", " ".join(fields["OWNS"]))
    globs = owns.split()
    if not globs:
        fail("%s: the OWNS field of %s is empty" % (path, task))
    return globs


def expand_braces(pattern):
    m = re.search(r"\{([^{}]*)\}", pattern)            # innermost first, so nesting works
    if not m:
        return [pattern]
    out = []
    for alt in m.group(1).split(","):
        out.extend(expand_braces(pattern[:m.start()] + alt + pattern[m.end():]))
    return out


def glob_regex(glob):
    segs = glob.strip("/").split("/")
    out = ""
    for k, seg in enumerate(segs):
        last = k == len(segs) - 1
        if seg == "**":
            out += ".*" if last else "(?:[^/]+/)*"
            continue
        for c in re.sub(r"\*+", "*", seg):
            out += "[^/]*" if c == "*" else "[^/]" if c == "?" else re.escape(c)
        if not last:
            out += "/"
    return re.compile("^" + out + "$")


def git(wt, *args):
    r = subprocess.run(["git", "--no-optional-locks", "-C", wt] + list(args), capture_output=True)
    if r.returncode != 0:
        fail("git %s failed in %s: %s" % (" ".join(args), wt, r.stderr.decode(errors="replace").strip()))
    return r.stdout.decode("utf-8", errors="surrogateescape")


def changed_paths(wt, base):
    paths = set()
    entries = git(wt, "status", "--porcelain=v1", "-z", "--untracked-files=all").split("\0")
    k = 0
    while k < len(entries):
        e = entries[k]
        k += 1
        if len(e) < 4:
            continue
        paths.add(e[3:])
        if e[0] in "RC":                                  # "XY to\0from\0": the source path follows
            if k < len(entries) and entries[k]:
                paths.add(entries[k])
            k += 1
    if base:
        for p in git(wt, "diff", "--name-only", "--no-renames", "-z", base, "HEAD").split("\0"):
            if p:
                paths.add(p)
    return sorted(p for p in paths if not (p == ".claude" or p.startswith(".claude/")))


def main():
    ap = argparse.ArgumentParser(description="Check a worktree's changed paths against a task's OWNS globs.")
    ap.add_argument("manifest", help="<sprint file>#<task id>, e.g. docs/sprints/s0.md#B0")
    ap.add_argument("worktree")
    ap.add_argument("--base", help="the commit the task started from (default: merge-base of HEAD and main)")
    a = ap.parse_args()
    if "#" not in a.manifest:
        fail("the manifest argument must be <file>#<task id>")
    path, task = a.manifest.rsplit("#", 1)
    globs = parse_manifest(path, task)
    patterns = [(g, glob_regex(x)) for g in globs for x in expand_braces(g)]

    wt = os.path.abspath(a.worktree)
    if not os.path.isdir(wt):
        fail("%s is not a directory" % wt)
    base = a.base
    if base is None:
        r = subprocess.run(["git", "--no-optional-locks", "-C", wt, "rev-parse", "-q", "--verify", "refs/heads/main"],
                           capture_output=True)
        if r.returncode == 0:
            base = git(wt, "merge-base", "HEAD", "main").strip()
    base_note = base[:12] if base else "none (working tree only)"
    paths = changed_paths(wt, base)

    print("ownership: %s#%s against %s (base %s): %d OWNS glob(s), %d changed path(s)"
          % (path, task, wt, base_note, len(globs), len(paths)))
    bad = 0
    for p in paths:
        owner = next((g for g, rx in patterns if rx.match(p)), None)
        if owner:
            print("  ok         %-70s %s" % (p, owner))
        else:
            bad += 1
            print("  NOT OWNED  %s" % p)
    if bad:
        print("ownership: %d path(s) outside OWNS" % bad)
        return 1
    print("ownership: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
