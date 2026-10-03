#!/usr/bin/env python3
"""
Extract labeled code snippets from C++ source files for LaTeX/Beamer inclusion.

Usage:
    python3 scripts/extract_snippets.py --src src --out docs/snippets/

Snippet format in source:
    // SNIPPET: phase1-consteval-fib
    consteval int fib(int n) { ... }
    // END SNIPPET

Generates:
    docs/snippets/phase1-consteval-fib.tex
"""

import argparse
import re
import sys
from pathlib import Path
from typing import Dict, List, Tuple

SNIPPET_START = re.compile(r"//\s*SNIPPET:\s*(\S+)")
SNIPPET_END = re.compile(r"//\s*END SNIPPET")


def extract_snippets(file_path: Path) -> Dict[str, str]:
    """Extract all snippets from a single file."""
    snippets = {}
    current_name = None
    current_lines = []

    with open(file_path, "r", encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            start_match = SNIPPET_START.search(line)
            if start_match:
                if current_name is not None:
                    print(
                        f"Warning: {file_path}:{line_num}: Nested SNIPPET start, closing previous"
                    )
                current_name = start_match.group(1)
                current_lines = []
                continue

            end_match = SNIPPET_END.search(line)
            if end_match:
                if current_name is not None:
                    snippets[current_name] = "".join(current_lines).rstrip()
                    current_name = None
                    current_lines = []
                else:
                    print(f"Warning: {file_path}:{line_num}: END SNIPPET without start")
                continue

            if current_name is not None:
                current_lines.append(line)

    if current_name is not None:
        print(f"Warning: {file_path}: Unclosed SNIPPET '{current_name}'")

    return snippets


def escape_latex(text: str) -> str:
    """Escape special LaTeX characters in code."""
    # Don't escape inside minted/verbatim - we'll use minted
    return text


def write_snippet_tex(
    snippet_name: str, code: str, out_dir: Path, language: str = "cpp"
) -> Path:
    """Write a single snippet as a .tex file using minted."""
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{snippet_name}.tex"

    # Use minted for syntax highlighting
    content = f"""\\begin{{minted}}[linenos,breaklines,frame=lines,framesep=2mm]{{{language}}}
{code}
\\end{{minted}}
"""
    out_file.write_text(content, encoding="utf-8")
    return out_file


def write_index_tex(snippets: Dict[str, Path], out_dir: Path) -> Path:
    """Write an index file listing all snippets for easy inclusion."""
    index_file = out_dir / "_snippets_index.tex"
    lines = [
        "% Auto-generated snippet index",
        "% Usage: \\input{docs/snippets/phase1-consteval-fib}",
        "",
    ]
    for name, path in sorted(snippets.items()):
        rel_path = (
            path.relative_to(out_dir.parent) if out_dir.parent in path.parents else path
        )
        lines.append(
            f"\\newcommand{{\\{name}}}{{\\input{{{rel_path.with_suffix('')}}}}}"
        )

    index_file.write_text("\n".join(lines), encoding="utf-8")
    return index_file


def main():
    parser = argparse.ArgumentParser(description="Extract C++ snippets for LaTeX")
    parser.add_argument("--src", nargs="+", required=True, help="Source directories")
    parser.add_argument(
        "--out", required=True, help="Output directory for .tex snippets"
    )
    parser.add_argument(
        "--ext",
        nargs="*",
        default=[".cpp", ".cppm", ".hpp", ".h"],
        help="File extensions to process",
    )
    args = parser.parse_args()

    out_dir = Path(args.out).resolve()

    all_snippets: Dict[str, str] = {}
    files_processed = 0

    for src in args.src:
        src_dir = Path(src).resolve()
        if not src_dir.exists():
            print(f"Error: Source directory {src_dir} does not exist")
            sys.exit(1)

        for ext in args.ext:
            for file_path in src_dir.rglob(f"*{ext}"):
                if "build" in file_path.parts or "_deps" in file_path.parts:
                    continue
                snippets = extract_snippets(file_path)
                if snippets:
                    print(
                        f"  {file_path.relative_to(src_dir)}: {len(snippets)} snippet(s)"
                    )
                    for name, code in snippets.items():
                        if name in all_snippets:
                            print(
                                f"  Warning: Duplicate snippet name '{name}' (in {file_path})"
                            )
                        all_snippets[name] = code
                    files_processed += 1

    print(
        f"\nProcessed {files_processed} files, found {len(all_snippets)} total snippets"
    )

    # Write each snippet
    written_paths = {}
    for name, code in all_snippets.items():
        path = write_snippet_tex(name, code, out_dir)
        written_paths[name] = path
        print(f"  Generated: {path.relative_to(out_dir.parent)}")

    # Write index
    index_path = write_index_tex(written_paths, out_dir)
    print(f"\nIndex written: {index_path.relative_to(out_dir.parent)}")
    print("\nAdd to your LaTeX preamble:")
    print(f"  \\usepackage{{minted}}")
    print(f"  \\input{{{index_path.relative_to(out_dir.parent)}}}")
    print("\nThen use in slides:")
    print(
        f"  \\phase1-consteval-fib   % expands to \\input{{docs/snippets/phase1-consteval-fib}}"
    )


if __name__ == "__main__":
    main()
