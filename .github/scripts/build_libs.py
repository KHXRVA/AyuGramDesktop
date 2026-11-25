import os
import re
import shutil
import subprocess
import sys
import time

GARBAGE = (".obj", ".o", ".pdb", ".ilk", ".idb", ".pch")
STAGE = re.compile(rb"^\[\d+/\d+\]\((\w+)/([^)#]+)", re.MULTILINE)


def free(path):
    return "%.1f GB" % (shutil.disk_usage(path).free / 2 ** 30)


def trim(libs, current):
    for name in os.listdir(libs):
        if name == current:
            continue
        for root, dirs, files in os.walk(os.path.join(libs, name)):
            dirs[:] = [x for x in dirs if not x.startswith("objects-")]
            for file in files:
                if file.lower().endswith(GARBAGE):
                    try:
                        os.remove(os.path.join(root, file))
                    except OSError:
                        pass


def main():
    libs, log = sys.argv[1:3]
    with open(log, "wb") as output:
        process = subprocess.Popen(
            sys.argv[3:],
            stdout=output,
            stderr=subprocess.STDOUT,
            shell=True)
    known = None
    report = []
    started = time.time()
    while True:
        code = process.poll()
        with open(log, "rb") as f:
            found = STAGE.findall(f.read())
        if found and found[-1] != known:
            known = found[-1]
            location, name = (x.decode() for x in known)
            before = free(libs)
            trim(libs, name if location == "Libraries" else None)
            report.append("%s: %d min, %s -> %s" % (
                name,
                (time.time() - started) / 60,
                before,
                free(libs)))
        if code is not None:
            break
        time.sleep(10)
    report.append("end: %d min, %s" % (
        (time.time() - started) / 60,
        free(libs)))
    print("::notice::" + "%0A".join(report), flush=True)
    return code


sys.exit(main())
