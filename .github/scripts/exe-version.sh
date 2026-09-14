#!/usr/bin/env bash
# exe-version.sh PATH-TO-EXE
#
# Prints the FileVersion string from a Windows executable's version resource
# (e.g. "1.1.21"), or exits 1 with a message on stderr if there is none.
#
# No PE parser: the version resource stores its keys and values as UTF-16LE
# strings, so the FileVersion value is the UTF-16LE string that follows the
# UTF-16LE key "FileVersion" and its NUL padding. UPX leaves the version
# resource uncompressed (Explorer must still be able to show it), which is why
# this works on the packed build/win/nutshell.exe from a hosted Linux runner.
#
# Shared by check-version.sh (the merge gate) and release.yml (which publishes
# the committed exe only if it was built from the tagged version).

set -u

exe="${1:-}"
if [ -z "$exe" ]; then
    echo "exe-version: usage: exe-version.sh PATH-TO-EXE" >&2
    exit 1
fi
[ -f "$exe" ] || { echo "exe-version: $exe not found" >&2; exit 1; }

version=$(python3 - "$exe" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read()
key = "FileVersion".encode('utf-16-le')
i = b.find(key)
if i < 0:
    sys.exit(0)
j = i + len(key)
while b[j:j+2] == b'\0\0':
    j += 2
print(b[j:j+64].decode('utf-16-le', 'ignore').split('\0')[0].strip())
PY
)

if [ -z "$version" ]; then
    echo "exe-version: no FileVersion string in $exe's version resource" >&2
    exit 1
fi
echo "$version"
