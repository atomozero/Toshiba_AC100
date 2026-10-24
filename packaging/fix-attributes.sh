#!/bin/bash
# fix-attributes.sh <pkg.hpkg>...
# Packages staged on Linux have no Haiku attributes: run mimeset on every
# file (BEOS:TYPE, and BEOS:APP_SIG & co. from the resources of apps), then
# rebuild the package in place. The original goes to <dir>/orig/ and is
# always the input, so the script can be run again.
#
# The host tools emulate attributes in $G/attributes, keyed by inode only:
# a file that gets the inode of a file deleted without rm_attrs inherits its
# attributes. So the extracted files keep only the attributes the package
# gives them, and the work directory is removed with rm_attrs.
set -e
G=${G:-$HOME/haiku-arm/haiku-renku/generated.arm}
T=$G/objects/linux/x86_64/release/tools
DB=$G/objects/common/data/mime_db/mime_db
ATTRS=$G/attributes
export LD_LIBRARY_PATH=$G/objects/linux/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}

for pkg in "$@"; do
	pkg=$(readlink -f "$pkg")
	orig=$(dirname "$pkg")/orig/$(basename "$pkg")
	mkdir -p "$(dirname "$orig")"
	[ -f "$orig" ] || cp "$pkg" "$orig"

	tmp=$(mktemp -d)
	(cd "$tmp" && $T/package/package extract "$orig" >/dev/null)

	# drop the attributes the original package does not have
	$T/package/package list -a "$orig" | python3 -c '
import os, re, sys
attrs_dir, root = sys.argv[1], sys.argv[2]
wanted, stack = {}, []
for line in sys.stdin:
	line = line.rstrip("\n")
	m = re.match(r"^( *)<(\S+)\s", line)
	if m:
		if stack:
			wanted.setdefault("/".join(stack), set()).add(m.group(2))
		continue
	m = re.match(r"^( *)(\S.*?)\s{2,}\d+\s+\d{4}-", line)
	if not m:
		continue
	depth = len(m.group(1)) // 2
	stack = stack[:depth] + [m.group(2)]
def escape(name):
	return "_" + name.replace("_", "__").replace("/", "_s")
for dirpath, dirs, files in os.walk(root):
	for name in dirs + files:
		path = os.path.join(dirpath, name)
		rel = os.path.relpath(path, root)
		d = os.path.join(attrs_dir, str(os.lstat(path).st_ino))
		if not os.path.isdir(d):
			continue
		keep = {escape(a) for a in wanted.get(rel, ())}
		for entry in os.listdir(d):
			name = entry[1:] if entry.startswith("t_") else entry
			if name.startswith("_") and name not in keep:
				os.remove(os.path.join(d, entry))
' "$ATTRS" "$tmp"

	find "$tmp" -type f ! -name .PackageInfo -print0 \
		| xargs -0 -r -n 50 $T/mimeset --mimedb "$DB" -f
	rm -f "$pkg.new"
	$T/package/package create -C "$tmp" -i "$tmp/.PackageInfo" "$pkg.new" \
		>/dev/null
	mv "$pkg.new" "$pkg"
	$T/rm_attrs -rf "$tmp"
	echo "fixed: $(basename "$pkg") ($($T/package/package list -a "$pkg" \
		| grep -c '<BEOS:APP_SIG') apps)"
done
