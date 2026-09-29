#!/usr/bin/env python3
# Copyright (c) 2026 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
'''
Check that no executable embeds the path of a C++ standard library header.

Such a path means a source location was captured inside a standard library
header instead of at the call site. gsl::not_null is the way this happens here:
its constructor defaults a nostd::source_location argument, so constructing one
through a forwarding wrapper - std::stack::emplace(), std::optional::emplace(),
std::make_unique() - records the wrapper's header rather than our own code.

That costs us twice. gsl::details::terminate() prints the location, so a failed
precondition names a standard library internal instead of the code that broke
it. And the path is the toolchain's, so on darwin it comes from the macOS SDK,
whose location differs between builders and makes the release binaries
irreproducible.

Only the sections that hold string literals are examined. DWARF legitimately
names standard library headers for inlined template code, and identifying
those sections by name is not portable - PE stores a long section name as an
offset into the string table, so they appear as '/81' rather than '.debug_*'.
Naming the sections we want instead cannot pick up debug data by accident.

Exit status will be 0 if successful, and the program will be silent.
Otherwise the exit status will be 1 and it will log which executables embed
which paths, and the section each was found in - which is what tells you
whether a hit is a string literal or debug data this script failed to skip.

Example usage:

    find ../path/to/binaries -type f -executable | xargs python3 contrib/guix/stdlib-path-check.py
'''
import re
import sys

import lief

# Matches the include directory of both libc++ (include/c++/v1) and libstdc++
# (include/c++/<version>), wherever the toolchain or sysroot places it.
STDLIB_INCLUDE = re.compile(rb'[\x20-\x7e]*include/c\+\+/[\x20-\x7e]*')

# Where a string literal ends up: .rodata and its variants on ELF, .rdata on
# PE, __cstring and __const on Mach-O.
LITERAL_SECTIONS = ('.rodata', '.rdata', '__cstring', '__const')

def section_label(section) -> str:
    # Mach-O sections are only meaningful together with their segment.
    segment = getattr(section, 'segment_name', '')
    return f'{segment},{section.name}' if segment else section.name

def holds_string_literals(section) -> bool:
    return section.name.startswith(LITERAL_SECTIONS)

def embedded_stdlib_paths(filename) -> list:
    binary = lief.parse(filename)
    if binary is None:
        raise IOError(f'{filename}: unable to parse')
    found: dict = {}
    for section in binary.sections:
        if not holds_string_literals(section):
            continue
        label = section_label(section)
        content = bytes(section.content)
        for match in STDLIB_INCLUDE.finditer(content):
            path = match.group().decode(errors='replace')
            # Identical literals are usually merged, so the count is a lower
            # bound on the number of sites. Offsets let a hit be attributed to
            # referencing code without rebuilding anything.
            found.setdefault((label, path), []).append(
                section.virtual_address + match.start())
    return sorted((label, path, offsets)
                  for (label, path), offsets in found.items())

def main() -> None:
    retval = 0
    for filename in sys.argv[1:]:
        try:
            paths = embedded_stdlib_paths(filename)
        except IOError as e:
            print(f'{e}')
            retval = 1
            continue
        for label, path, offsets in paths:
            where = ' '.join(f'0x{o:x}' for o in offsets[:4])
            if len(offsets) > 4:
                where += ' ...'
            print(f'{filename}: {label} embeds standard library header path '
                  f'{path} (x{len(offsets)} at {where})')
            retval = 1
    sys.exit(retval)

if __name__ == '__main__':
    main()
