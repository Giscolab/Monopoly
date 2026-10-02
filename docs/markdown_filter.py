"""Resolve labelled Markdown pages for Doxygen without changing GitHub links."""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
LINK = re.compile(r"\]\(([^()\s#]+\.md)\)", re.IGNORECASE)
PAGE_ID = re.compile(r"^# .+?\s+\{#([A-Za-z_][\w-]*)\}\s*$", re.MULTILINE)


def render(source: Path) -> str:
    text = source.read_text(encoding="utf-8-sig")

    def resolve(match: re.Match[str]) -> str:
        target = match.group(1)
        if ":" in target or target.startswith("/"):
            return match.group(0)
        for candidate in (source.parent / target, ROOT / "modern" / target):
            candidate = candidate.resolve()
            if not candidate.is_relative_to(ROOT) or not candidate.is_file():
                continue
            label = PAGE_ID.search(candidate.read_text(encoding="utf-8-sig"))
            if label:
                return f"](@ref {label.group(1)})"
            break
        return match.group(0)

    return LINK.sub(resolve, text)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdout.write(render(Path(sys.argv[1]).resolve()))
