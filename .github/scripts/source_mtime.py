import json
import os
import subprocess
import sys
import time


def tracked(root):
    result = {}
    modules = [""]
    while modules:
        base = modules.pop()
        output = subprocess.run(
            ["git", "-C", os.path.join(root, base), "ls-files", "-s", "-z"],
            check=True,
            capture_output=True).stdout.decode("utf-8", "surrogateescape")
        for entry in output.split("\0"):
            if not entry:
                continue
            info, path = entry.split("\t", 1)
            mode, sha, _ = info.split(" ")
            full = base + "/" + path if base else path
            if mode == "160000":
                if os.path.exists(os.path.join(root, full, ".git")):
                    modules.append(full)
            else:
                result[full] = sha
    return result


def main():
    action, root = sys.argv[1:]
    manifest = os.path.join(root, "out", "source_mtime.json")
    files = tracked(root)
    if action == "save":
        data = {}
        for path, sha in files.items():
            full = os.path.join(root, path)
            if os.path.isfile(full):
                data[path] = [sha, os.stat(full).st_mtime_ns]
        os.makedirs(os.path.dirname(manifest), exist_ok=True)
        with open(manifest, "w", encoding="utf-8", errors="surrogateescape") as f:
            json.dump(data, f)
        print("saved", len(data))
        return
    if not os.path.isfile(manifest):
        print("no manifest")
        return
    with open(manifest, encoding="utf-8", errors="surrogateescape") as f:
        data = json.load(f)
    now = time.time_ns()
    same = 0
    for path, sha in files.items():
        full = os.path.join(root, path)
        if not os.path.isfile(full):
            continue
        known = data.get(path)
        if known and known[0] == sha:
            stamp = known[1]
            same += 1
        else:
            stamp = now
        os.utime(full, ns=(stamp, stamp))
    print("unchanged", same, "of", len(files))


main()
