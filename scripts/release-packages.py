#!/usr/bin/env python3
# Copyright 2026, Andrea Bernardi
# SPDX-License-Identifier: MIT
#
# release-packages.py <list> <base packages> -- <package folders>...
#
# Chooses the packages of a release image: the ones named in <list> (one
# package name per line, # for comments) and everything they need, found in
# the package folders (the first folder wins, then the newest version).
# What the packages in <base packages> (the system of the image) provide is
# not added again. Prints the package files to add, one per line; stops when
# a requirement cannot be met.
#
# The package tool of the Haiku build is taken from $PACKAGE_TOOL.

import os
import re
import subprocess
import sys


def package_info(path, cache={}):
	if path not in cache:
		output = subprocess.run([os.environ["PACKAGE_TOOL"], "list", "-i",
			path], capture_output=True, text=True, check=True).stdout
		info = {"name": None, "version": "", "provides": [], "requires": []}
		for line in output.splitlines():
			line = line.strip()
			key, _, value = line.partition(": ")
			if key == "name":
				info["name"] = value
			elif key == "version":
				info["version"] = value
			elif key in ("provides", "requires"):
				# "lib:libssl = 3.0.0", "haiku>=r1~beta5"
				info[key].append(re.split(r"[\s<>=]", value, 1)[0])
		cache[path] = info
	return cache[path]


def version_key(version):
	return [int(part) if part.isdigit() else part
		for part in re.split(r"[._~-]", version)]


def main():
	args = sys.argv[1:]
	if "--" not in args or len(args) < 3:
		sys.exit(__doc__ if __doc__ else "usage: see the script")
	split = args.index("--")
	listFile, base, folders = args[0], args[1:split], args[split + 1:]

	wanted = []
	for line in open(listFile):
		line = line.split("#")[0].strip()
		if line:
			wanted.append(line)

	provided = set()
	for path in base:
		info = package_info(path)
		provided.add(info["name"])
		provided.update(info["provides"])

	# every package of the folders, by name: the first folder, then the
	# newest version
	candidates = {}
	for rank, folder in enumerate(folders):
		for leaf in sorted(os.listdir(folder)):
			path = os.path.join(folder, leaf)
			if not leaf.endswith(".hpkg") or not os.path.isfile(path):
				continue
			info = package_info(path)
			best = candidates.get(info["name"])
			if best is None or rank < best[0] or (rank == best[0]
					and version_key(info["version"])
						> version_key(best[1]["version"])):
				candidates[info["name"]] = (rank, info, path)

	providers = {}
	for name, (rank, info, path) in candidates.items():
		for item in [name] + info["provides"]:
			providers.setdefault(item, name)

	chosen = []
	todo = list(wanted)
	missing = []
	while todo:
		item = todo.pop(0)
		if item in provided:
			continue
		name = item if item in candidates else providers.get(item)
		if name is None:
			missing.append(item)
			continue
		if name in chosen:
			continue
		chosen.append(name)
		info = candidates[name][1]
		provided.add(name)
		provided.update(info["provides"])
		todo.extend(info["requires"])

	if missing:
		sys.exit("nothing provides: " + ", ".join(sorted(set(missing))))
	for name in chosen:
		print(candidates[name][2])


if __name__ == "__main__":
	main()
