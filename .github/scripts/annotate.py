import re
import sys

MAX_ANNOTATIONS = 9
MAX_LENGTH = 3800


def emit(chunks):
    for chunk in chunks[:MAX_ANNOTATIONS]:
        text = "\n".join(chunk)
        text = text.replace("%", "%25").replace("\r", "").replace("\n", "%0A")
        print("::error::" + text)


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
    print("\n".join(lines[-400:]))
    if mode == "errors":
        seen = set()
        found = []
        for line in lines:
            if not re.search(r"\b(fatal )?error\b", line):
                continue
            line = re.sub(r"\s*\[[^\]]*\.vcxproj[^\]]*\]$", "", line).strip()
            line = re.sub(r"^.*?tdesktop[\\/]Telegram[\\/](SourceFiles[\\/])?", "", line)
            if line not in seen:
                seen.add(line)
                found.append(line)
        if found:
            print("::notice::" + str(len(found)) + " unique errors")
            emit(split(found))
            return
    emit(list(reversed(split(lines[-260:])))[:MAX_ANNOTATIONS])


main()
