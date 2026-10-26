#!/usr/bin/env python3
"""FoloTalker 仓库检查脚本的单元测试。

检查脚本本身要可靠,否则"门禁通过"就没有意义。
这个测试验证:
  - 链接检查能发现缺失目标,不会误报外部链接
  - 密钥检查能发现凭证
  - 构建产物检查能发现误提交的固件
  - 模块注册检查能发现漏注册的源文件
  - 字体只注册一次
"""

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load_check_repo():
    """按脚本路径加载 tools/check_repo.py(它按自身位置确定 ROOT)。"""
    spec = importlib.util.spec_from_file_location(
        "check_repo", ROOT / "tools" / "check_repo.py"
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class CheckRepoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.mod = load_check_repo()

    def test_repo_passes_its_own_checks(self) -> None:
        errors: list[str] = []
        self.mod.check_required_files(errors)
        self.mod.check_chinese_primary_docs(errors)
        self.mod.check_links(errors)
        self.mod.check_secrets(errors)
        self.mod.check_no_build_artifacts(errors)
        self.mod.check_app_modules(errors)
        self.mod.check_fonts_registered_once(errors)
        self.assertEqual(errors, [], "仓库自身没通过自己的检查:\n" + "\n".join(errors))

    def test_missing_link_target_is_detected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / "doc.md").write_text(
                "[good](other.md)\n[bad](nope.md)\n"
                "[ext](https://example.com/x)\n",
                encoding="utf-8",
            )
            (tmp_path / "other.md").write_text("ok\n", encoding="utf-8")

            # 临时把 ROOT 指向临时目录
            original_root = self.mod.ROOT
            self.mod.ROOT = tmp_path
            try:
                errors: list[str] = []
                self.mod.check_links(errors)
            finally:
                self.mod.ROOT = original_root

            self.assertEqual(len(errors), 1, errors)
            self.assertIn("nope.md", errors[0])

    def test_secret_patterns_detect_credentials(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            # 拼出测试用的假 token,避免这个测试文件本身被密钥检查命中
            fake_token = "ghp_" + ("a" * 36)
            (tmp_path / "secret.txt").write_text(
                f"token = {fake_token}\n", encoding="utf-8"
            )
            original_root = self.mod.ROOT
            self.mod.ROOT = tmp_path
            try:
                errors: list[str] = []
                self.mod.check_secrets(errors)
            finally:
                self.mod.ROOT = original_root
            self.assertTrue(errors, "没有检测到 GitHub token")

    def test_build_artifacts_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            (tmp_path / "firmware.bin").write_bytes(b"\x00")
            original_root = self.mod.ROOT
            self.mod.ROOT = tmp_path
            try:
                errors: list[str] = []
                self.mod.check_no_build_artifacts(errors)
            finally:
                self.mod.ROOT = original_root
            self.assertTrue(errors, "没有检测到误提交的固件产物")

    def test_app_modules_are_all_registered(self) -> None:
        # 仓库里每一个 main/ft_*.c 都必须出现在 main/CMakeLists.txt 里
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        for src in sorted((ROOT / "main").glob("ft_*.c")):
            self.assertIn(src.name, cmake, f"{src.name} 没有在 CMakeLists 里注册")

    def test_fonts_registered_exactly_once(self) -> None:
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")
        for name in ("ft_font_16.c", "ft_font_20.c", "ft_font_28.c"):
            self.assertEqual(cmake.count(name), 1, f"{name} 注册次数不对")

        # 字体 .c 不能被别的源文件 #include
        for path in (ROOT / "main").glob("*.c"):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn(
                "ft_font_16.c\"", text, f"{path.name} 直接 include 了字体源文件"
            )

    def test_english_docs_have_no_chinese_prose(self) -> None:
        errors: list[str] = []
        self.mod.check_english_docs_have_no_cjk(errors)
        self.assertEqual(errors, [], "\n".join(errors))


if __name__ == "__main__":
    unittest.main()
