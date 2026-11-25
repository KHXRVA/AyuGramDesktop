import os
import re
import sys

MAX_ANNOTATIONS = 9
MAX_LENGTH = 3800


def emit(chunks, level="error"):
    for chunk in chunks[:MAX_ANNOTATIONS]:
        text = "\n".join(chunk)
        text = text.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
        print("::" + level + "::" + text)


def split(lines):
    chunks = []
    size = 0
    for line in lines:
        line = line[:600]
        if not chunks or size + len(line) > MAX_LENGTH:
            chunks.append([])
            size = 0
        chunks[-1].append(line)
        size += len(line) + 1
    return chunks


def main():
    mode, path = sys.argv[1:]
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = [line.rstrip() for line in f if line.strip()]
    lines = [line for line in lines if not line.startswith("Updating files:")]
    for name in ("API_ID", "API_HASH"):
        value = os.environ.get(name, "").strip()
        if value:
            lines = [line.replace(value, "***") for line in lines]
    print("\n".join(lines[-400:]))
    if mode == "errors":
        seen = set()
        found = []
        count = {}
        for line in lines:
            if not re.search(r"\berror\b[^:]{0,12}:", line):
                continue
            line = re.sub(r"\s*\[[^\]]*\.vcxproj[^\]]*\]$", "", line).strip()
            line = re.sub(r"^.*?tdesktop[\\/]Telegram[\\/](SourceFiles[\\/])?", "", line)
            if line not in seen:
                seen.add(line)
                source = re.split(r"[(:]", line)[0]
                count[source] = count.get(source, 0) + 1
                if count[source] <= 12:
                    found.append(line)
        if found:
            steps = [x for x in (re.match(r"\[\d+/\d+\]", line) for line in lines) if x]
            print("::notice::%d unique errors, %d shown, progress %s" % (
                len(seen),
                len(found),
                steps[-1].group() if steps else "unknown"))
            chunks = split(found)
            emit(chunks)
            emit(chunks[MAX_ANNOTATIONS:], "warning")
            emit(chunks[MAX_ANNOTATIONS * 2:][:7], "notice")
            return
    emit(list(reversed(split(lines[-260:])))[:MAX_ANNOTATIONS])


main()
