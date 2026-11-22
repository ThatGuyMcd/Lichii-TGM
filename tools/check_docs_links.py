#!/usr/bin/env python3

# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""
check_docs_links.py

Checks the markdown in the tree for links that cannot work:

- relative links whose target does not exist (dead file links)
- links written as a server-root path ("/src/..."), which resolve on a
  hosting root but not in a repository or a fork
- "#fragment" links, and links to another markdown file with a fragment,
  whose anchor does not match a heading

Anchors are compared using GitHub's rules: heading text lowercased, spaces
to hyphens, punctuation other than "-" and "_" removed, inline code and
emphasis markers dropped. ATX ("# Heading") and Setext ("Heading" over
"===" or "---") headings both count. Fenced code blocks and inline code
spans are skipped, so links shown as examples are not checked.

Usage:
    tools/check_docs_links.py [path...]

With no arguments it walks the repository containing the script. Exits 1
when anything is wrong, so it can gate CI.
"""

import os
import re
import sys

SKIP_DIRS = {".git", "build", "build-local", "build-linux", "build-windows", "externals", ".cache", ".reuse"}

INLINE_CODE = re.compile(r"`[^`]*`")
FENCE = re.compile(r"^\s*(```|~~~)")
ATX = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
SETEXT = re.compile(r"^(=+|-+)\s*$")
LINK = re.compile(r"!?\[[^\]]*\]\(\s*<?([^)\s>]*)>?(?:\s+\"[^\"]*\")?\s*\)")
REF_DEF = re.compile(r"^\s{0,3}\[[^\]]+\]:\s*<?([^>\s]+)>?")


def slug(text):
    """Heading text to the id GitHub generates for it."""
    text = INLINE_CODE.sub(lambda m: m.group(0).strip("`"), text)
    text = re.sub(r"[*_~]", "", text)
    text = re.sub(r"<[^>]+>", "", text)
    text = text.strip().lower()
    text = re.sub(r"[^\w\- ]", "", text, flags=re.UNICODE)
    return text.replace(" ", "-")


def anchors(path):
    """Every heading id in a markdown file, in document order."""
    ids = set()
    with open(path, encoding="utf-8", errors="replace") as handle:
        lines = handle.read().splitlines()

    fenced = False
    for index, line in enumerate(lines):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if fenced:
            continue

        match = ATX.match(line)
        if match:
            ids.add(slug(match.group(2)))
            continue

        # Setext: the heading text is the previous line, the rule is this one.
        if SETEXT.match(line) and index:
            previous = lines[index - 1].strip()
            if previous and not SETEXT.match(previous) and not previous.startswith("#"):
                ids.add(slug(previous))
    return ids


def markdown_files(paths):
    for path in paths:
        if os.path.isfile(path):
            yield path
            continue
        for directory, dirnames, filenames in os.walk(path):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in sorted(filenames):
                if name.lower().endswith(".md"):
                    yield os.path.join(directory, name)


def links(path):
    """Inline links and reference definitions, by line, code spans removed."""
    fenced = False
    with open(path, encoding="utf-8", errors="replace") as handle:
        for number, line in enumerate(handle.read().splitlines(), 1):
            if FENCE.match(line):
                fenced = not fenced
                continue
            if fenced:
                continue
            bare = INLINE_CODE.sub("", line)
            for match in LINK.finditer(bare):
                yield number, match.group(1)
            match = REF_DEF.match(bare)
            if match:
                yield number, match.group(1)


def check(root, paths):
    problems = []
    cache = {}

    def ids_for(path):
        if path not in cache:
            cache[path] = anchors(path)
        return cache[path]

    for path in markdown_files(paths):
        for number, target in links(path):
            target = target.strip()
            if not target:
                continue
            if re.match(r"^[a-z][a-z0-9+.-]*:", target, re.IGNORECASE):
                continue  # http:, https:, mailto: and friends
            if target.startswith("//"):
                continue

            where = "%s:%d" % (os.path.relpath(path, root), number)

            if target.startswith("/"):
                problems.append((where, target, "server-root path"))
                continue

            if "{{" in target or "<" in target:
                continue  # template placeholder, not a link

            file_part, _, fragment = target.partition("#")
            if file_part:
                resolved = os.path.normpath(
                    os.path.join(os.path.dirname(path), file_part)
                )
                if not os.path.exists(resolved):
                    problems.append((where, target, "no such file"))
                    continue
            else:
                resolved = path

            if fragment and resolved.lower().endswith(".md"):
                if fragment not in ids_for(resolved):
                    problems.append((where, target, "no such heading anchor"))

    return problems


def main():
    root = os.path.dirname(os.path.abspath(os.path.join(__file__, "..")))
    paths = sys.argv[1:] or [root]
    paths = [os.path.abspath(p) for p in paths]

    problems = check(root, paths)
    for where, target, reason in problems:
        print("%s: %s (%s)" % (where, target, reason))

    total = sum(1 for _ in markdown_files(paths))
    if problems:
        print("\n%d problem(s) in %d markdown file(s)" % (len(problems), total))
        return 1
    print("No broken links in %d markdown file(s)" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
