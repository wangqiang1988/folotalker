#!/usr/bin/env python3
"""FoloTalker 仓库的静态检查(不依赖第三方库)。

这个仓库和上游 ai-passport 的文档约定不同:
  - 本仓库是**独立应用仓库**,文档以简体中文为主,不做强制的中英配对要求
  - 但仍然要求:链接目标存在、没有提交密钥、没有把固件产物提交进仓库

上游 ai-passport 的 tools/check_repo.py 强制要求每个 .md 都有 .zh_CN.md 配对,
那是上游基线仓库的约定,不适用于这里,所以本仓库用自己的检查脚本。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parent.parent

MARKDOWN_LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
HTML_LINK_RE = re.compile(r"\bhref\s*=\s*['\"]([^'\"]+)['\"]", re.IGNORECASE)
MARKDOWN_REFERENCE_RE = re.compile(r"(?m)^ {0,3}\[[^\]]+\]:\s*(<[^>\n]+>|\S+)")

# 不能提交进仓库的东西
SECRET_PATTERNS = {
    "GitHub token": re.compile(r"(?:ghp_|github_pat_)[A-Za-z0-9_]{20,}"),
    "AWS access key": re.compile(r"AKIA[0-9A-Z]{16}"),
    "private key": re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "HomeTalker family code": re.compile(r"^\s*FAMILY_CODE\s*=\s*(?!change-me|your-|<|\$)\S+", re.M),
}

# 不该进仓库的构建产物
IGNORED_DIRS = {".git", "build", "managed_components", "node_modules", ".venv"}
BINARY_SUFFIXES = {".bin", ".elf", ".map", ".o", ".a", ".pyc"}


def iter_text_files():
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in IGNORED_DIRS for part in path.relative_to(ROOT).parts):
            continue
        if path.suffix in BINARY_SUFFIXES:
            continue
        yield path


def check_links(errors: list[str]) -> None:
    """Markdown 里的相对链接目标必须存在。"""
    for path in iter_text_files():
        if path.suffix != ".md":
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        targets = [m.group(1) for m in MARKDOWN_LINK_RE.finditer(text)]
        targets += [m.group(1) for m in HTML_LINK_RE.finditer(text)]
        targets += [m.group(1) for m in MARKDOWN_REFERENCE_RE.finditer(text)]
        for raw in targets:
            target = raw.strip().split("#")[0].split("?")[0]
            if not target:
                continue
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1]
            parts = urlsplit(target)
            if parts.scheme or parts.netloc:
                continue  # 外部链接
            if target.startswith("/"):
                resolved = ROOT / target.lstrip("/")
            else:
                resolved = path.parent / unquote(parts.path)
            if not resolved.exists():
                errors.append(
                    f"{path.relative_to(ROOT)}: missing link target {target}"
                )


def check_secrets(errors: list[str]) -> None:
    """不能有凭证、私钥、真实口令。"""
    for path in iter_text_files():
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError):
            continue
        for name, pattern in SECRET_PATTERNS.items():
            if pattern.search(text):
                errors.append(f"{path.relative_to(ROOT)}: possible {name} committed")


def check_no_build_artifacts(errors: list[str]) -> None:
    """固件产物不能提交。

    注意不能复用 iter_text_files() —— 它会跳过 .bin/.elf/.map,
    而这些恰恰是要检查的对象。这里直接遍历仓库里的文件。
    """
    for path in ROOT.rglob("*"):
        if not path.is_file():
            continue
        if any(part in IGNORED_DIRS for part in path.relative_to(ROOT).parts):
            continue
        if path.suffix in {".bin", ".elf", ".map"}:
            errors.append(
                f"{path.relative_to(ROOT)}: build artifact must not be committed"
            )


def check_required_files(errors: list[str]) -> None:
    """仓库必须有的文件。"""
    required = [
        "README.md",
        "README.en.md",
        "使用手册.md",
        "HANDOFF.md",
        "HANDOFF.en.md",
        "docs/README.md",
        "docs/architecture.md",
        "LICENSE",
        "CMakeLists.txt",
        "partitions.csv",
        "sdkconfig.defaults",
        "main/CMakeLists.txt",
        "main/main.c",
        "main/idf_component.yml",
        "tools/validate.sh",
        "tools/gen_fonts.py",
        "assets/fonts/ft_font_16.c",
        "assets/fonts/ft_font_20.c",
        "assets/fonts/ft_font_28.c",
        "assets/fonts/charset.txt",
    ]
    for rel in required:
        if not (ROOT / rel).exists():
            errors.append(f"missing required file: {rel}")


def check_chinese_primary_docs(errors: list[str]) -> None:
    """主文档是中文,英文版是摘要 —— 两者都要在,且互相链接。

    这个仓库不强制每个 .md 都有 .zh_CN.md 配对(那是上游基线的约定),
    但要求:中文主文档与英文摘要成对存在,并且互相引用。
    """
    pairs = [
        ("README.md", "README.en.md"),
        ("HANDOFF.md", "HANDOFF.en.md"),
    ]
    for zh, en in pairs:
        zh_path = ROOT / zh
        en_path = ROOT / en
        if not zh_path.exists() or not en_path.exists():
            errors.append(f"missing doc pair: {zh} / {en}")
            continue
        if en not in zh_path.read_text(encoding="utf-8"):
            errors.append(f"{zh}: must link to {en}")
        if zh not in en_path.read_text(encoding="utf-8"):
            errors.append(f"{en}: must link to {zh}")


def check_english_docs_have_no_cjk(errors: list[str]) -> None:
    """英文版文档里不该有大段中文。"""
    for rel in ("README.en.md", "HANDOFF.en.md", "docs/architecture.md"):
        path = ROOT / rel
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8")
        # 去掉行内代码块再统计,代码里的中文标识符是允许的
        stripped = re.sub(r"`[^`]*`", "", text)
        hits = re.findall(r"[\u3400-\u4dbf\u4e00-\u9fff]", stripped)
        if len(hits) > 20:
            errors.append(
                f"{rel}: {len(hits)} CJK characters outside code spans "
                "(this is an English document)"
            )


def check_app_modules(errors: list[str]) -> None:
    """应用层模块必须都在 CMakeLists 里注册。"""
    cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
    for src in sorted((ROOT / "main").glob("ft_*.c")):
        if src.name not in cmake:
            errors.append(f"main/{src.name}: not registered in main/CMakeLists.txt")


def check_fonts_registered_once(errors: list[str]) -> None:
    """字体只能在 CMakeLists 里注册一次,不能在别处 #include .c。"""
    cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
    for path in (ROOT / "main").glob("*.c"):
        text = path.read_text(encoding="utf-8")
        if re.search(r'#include\s+"[^"]*ft_font_\d+\.c"', text):
            errors.append(
                f"main/{path.name}: font .c must be registered in CMakeLists only"
            )
    for name in ("ft_font_16.c", "ft_font_20.c", "ft_font_28.c"):
        if cmake.count(name) != 1:
            errors.append(f"assets/fonts/{name}: must appear exactly once in main/CMakeLists.txt")


def main() -> int:
    errors: list[str] = []
    check_required_files(errors)
    check_chinese_primary_docs(errors)
    check_english_docs_have_no_cjk(errors)
    check_links(errors)
    check_secrets(errors)
    check_no_build_artifacts(errors)
    check_app_modules(errors)
    check_fonts_registered_once(errors)

    if errors:
        for error in errors:
            print(f"ERROR: {error}")
        print(f"\nRepository checks: FAIL ({len(errors)} problems)")
        return 1

    count = sum(1 for _ in iter_text_files())
    print(f"Repository checks: PASS ({count} text files scanned)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
