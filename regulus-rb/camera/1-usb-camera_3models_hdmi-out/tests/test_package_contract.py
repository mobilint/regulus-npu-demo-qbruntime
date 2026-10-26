import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


PACKAGE_DIR = Path(__file__).resolve().parents[1]
RUN_SH = PACKAGE_DIR / "run.sh"
FIXED_MODELS = {
    "yolo11s.mxq",
    "yolo11s-pose.mxq",
    "yolov11s-face.mxq",
}
LEGACY_SP500_ENV = {
    "SP500_LIB_DIR",
    "SP500_FIRMWARE_LOADER",
    "SP500_PTCM",
    "SP500_DTCM",
    "SP500_EXTERNAL",
}
SYSTEM_SH = shutil.which("sh") or "/bin/sh"


def write_executable(path, body):
    path.write_text("#!/bin/sh\n" + body, encoding="utf-8")
    path.chmod(0o755)


class LauncherFixture:
    def __init__(self):
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary_directory.name)
        self.package = self.root / "demo"
        self.mock_bin = self.root / "mock-bin"
        self.events = self.root / "events"
        self.package.mkdir()
        self.mock_bin.mkdir()
        (self.package / "mxq").mkdir()
        (self.package / "img").mkdir()

        shutil.copy2(RUN_SH, self.package / "run.sh")
        write_executable(
            self.package / "demo_usb_hdmi",
            'echo "binary $*" >> "$TEST_EVENTS"\nexit 0\n',
        )
        for model in FIXED_MODELS:
            (self.package / "mxq" / model).touch()
        (self.package / "img" / "[Regulus] Smart Camera_Demo_260804.jpg").touch()

        dirname = shutil.which("dirname")
        if dirname is None:
            raise RuntimeError("dirname is required to test run.sh")
        (self.mock_bin / "dirname").symlink_to(dirname)

        write_executable(
            self.mock_bin / "id",
            'echo id >> "$TEST_EVENTS"\nprintf "0\\n"\n',
        )
        write_executable(
            self.mock_bin / "pgrep",
            'echo "pgrep $*" >> "$TEST_EVENTS"\nexit 1\n',
        )
        write_executable(
            self.mock_bin / "pkill",
            'echo "pkill $*" >> "$TEST_EVENTS"\nexit 99\n',
        )
        write_executable(
            self.mock_bin / "chrt",
            'echo "chrt $*" >> "$TEST_EVENTS"\nexit 99\n',
        )

        self.loader = self.mock_bin / "dsp_firmware_load"
        write_executable(
            self.loader,
            'echo "loader argc=$# args=$*" >> "$TEST_EVENTS"\n'
            'exit "${TEST_LOADER_STATUS:-0}"\n',
        )

    def cleanup(self):
        self._temporary_directory.cleanup()

    def environment(self):
        environment = os.environ.copy()
        environment.update(
            {
                "PATH": f"{self.mock_bin}{os.pathsep}{environment['PATH']}",
                "TEST_EVENTS": str(self.events),
                "RT": "0",
            }
        )
        for variable in LEGACY_SP500_ENV:
            environment.pop(variable, None)
        return environment

    def run(self, environment=None):
        return subprocess.run(
            [SYSTEM_SH, str(self.package / "run.sh")],
            cwd=self.package,
            env=environment or self.environment(),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )

    def read_events(self):
        if not self.events.exists():
            return []
        return self.events.read_text(encoding="utf-8").splitlines()


class LauncherValidationTests(unittest.TestCase):
    def setUp(self):
        self.fixture = LauncherFixture()

    def tearDown(self):
        self.fixture.cleanup()

    def assert_preflight_failure(self, result, missing_name):
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(missing_name, result.stderr)
        self.assertEqual(self.fixture.read_events(), [])

    def test_missing_fixed_model_fails_before_side_effects(self):
        missing_model = "yolo11s-pose.mxq"
        (self.fixture.package / "mxq" / missing_model).unlink()

        result = self.fixture.run()

        self.assert_preflight_failure(result, missing_model)

    def test_missing_demo_executable_fails_before_side_effects(self):
        (self.fixture.package / "demo_usb_hdmi").unlink()

        result = self.fixture.run()

        self.assert_preflight_failure(result, "demo_usb_hdmi")

    def test_mxq_override_cannot_replace_missing_fixed_model(self):
        missing_model = "yolov11s-face.mxq"
        (self.fixture.package / "mxq" / missing_model).unlink()
        (self.fixture.package / "mxq" / "replacement.mxq").touch()
        environment = self.fixture.environment()
        environment["MXQ_FACE"] = "replacement.mxq"

        result = self.fixture.run(environment)

        self.assert_preflight_failure(result, missing_model)

    def test_missing_loader_fails_before_side_effects(self):
        environment = self.fixture.environment()
        self.fixture.loader.unlink()
        environment["PATH"] = str(self.fixture.mock_bin)

        result = self.fixture.run(environment)

        self.assert_preflight_failure(result, "dsp_firmware_load")

    def test_missing_background_fails_before_side_effects(self):
        background = (
            self.fixture.package
            / "img"
            / "[Regulus] Smart Camera_Demo_260804.jpg"
        )
        background.unlink()

        result = self.fixture.run()

        self.assert_preflight_failure(result, background.name)

    def test_loader_failure_stops_before_demo_launch(self):
        environment = self.fixture.environment()
        environment["TEST_LOADER_STATUS"] = "23"

        result = self.fixture.run(environment)

        self.assertNotEqual(result.returncode, 0)
        events = self.fixture.read_events()
        self.assertEqual(events.count("loader argc=0 args="), 1)
        self.assertTrue(any(event == "id" for event in events))
        self.assertTrue(any(event.startswith("pgrep ") for event in events))
        self.assertFalse(any(event.startswith("pkill ") for event in events))
        self.assertFalse(any(event.startswith("binary ") for event in events))

    def test_legacy_sp500_environment_does_not_redirect_loader(self):
        environment = self.fixture.environment()
        environment["TEST_LOADER_STATUS"] = "23"
        for variable in LEGACY_SP500_ENV:
            environment[variable] = str(self.fixture.root / f"bad-{variable.lower()}")

        result = self.fixture.run(environment)

        self.assertNotEqual(result.returncode, 0)
        events = self.fixture.read_events()
        self.assertEqual(events.count("loader argc=0 args="), 1)
        self.assertFalse(any(event.startswith("binary ") for event in events))


class StaticPackageContractTests(unittest.TestCase):
    def production_text(self):
        paths = [PACKAGE_DIR / "Makefile"]
        paths.extend(PACKAGE_DIR.glob("*.sh"))
        paths.extend(PACKAGE_DIR.glob("*.cc"))
        paths.extend((PACKAGE_DIR / "src").rglob("*.cc"))
        paths.extend((PACKAGE_DIR / "src").rglob("*.h"))
        return "\n".join(
            path.read_text(encoding="utf-8") for path in paths if path.is_file()
        )

    def test_mxq_directory_has_exactly_three_fixed_models(self):
        actual_models = {
            path.name for path in (PACKAGE_DIR / "mxq").glob("*.mxq") if path.is_file()
        }
        self.assertEqual(actual_models, FIXED_MODELS)

        referenced_models = set(re.findall(r"[A-Za-z0-9_.-]+\.mxq", self.production_text()))
        self.assertEqual(referenced_models, FIXED_MODELS)

    def test_launcher_uses_zero_argument_system_loader(self):
        launcher = RUN_SH.read_text(encoding="utf-8")
        self.assertRegex(launcher, r"(?m)^command -v dsp_firmware_load\b")
        self.assertRegex(launcher, r"(?m)^dsp_firmware_load\s*$")
        for variable in LEGACY_SP500_ENV:
            self.assertNotIn(variable, launcher)

    def test_timeout_recovery_uses_zero_argument_system_loader(self):
        demo_text = (PACKAGE_DIR / "demo.cc").read_text(encoding="utf-8")
        call = re.search(r"execlp\((.*?)\);", demo_text, re.DOTALL)
        self.assertIsNotNone(call, "timeout recovery must execute dsp_firmware_load")
        arguments = call.group(1)
        self.assertEqual(arguments.count(","), 2)
        self.assertEqual(arguments.count('"dsp_firmware_load"'), 2)
        self.assertIn("nullptr", arguments)
        for variable in LEGACY_SP500_ENV:
            self.assertNotIn(variable, demo_text)

    def test_production_files_have_no_third_party_paths_or_model_overrides(self):
        text = self.production_text()
        self.assertNotIn(
            "third_party", text, "production files must not use bundled dependencies"
        )
        for variable in ("MXQ_FACE", "MXQ_POSE", "MXQ_OD"):
            self.assertNotIn(variable, text, f"unsupported model override: {variable}")

    def test_version_diagnostics_remain_without_version_equality_gates(self):
        demo_text = (PACKAGE_DIR / "demo.cc").read_text(encoding="utf-8")
        for diagnostic in (
            "getQbRuntimeVersion",
            "getQbRuntimeGitVersion",
            "sp500_version_string",
        ):
            self.assertIn(diagnostic, demo_text)

        version_value = re.compile(
            r"(?:getQbRuntimeVersion\(\)|getQbRuntimeGitVersion\(\)|"
            r"sp500_version\(\)|sp500_version_string\(\)|SP500_VERSION_INT)"
        )
        comparison = re.compile(version_value.pattern + r"\s*(?:==|!=)")
        self.assertIsNone(comparison.search(demo_text))
        for fixed_value in ("v1.5.2", "71f4cb3a"):
            self.assertNotIn(fixed_value, demo_text)

    def test_shell_scripts_have_valid_syntax(self):
        scripts = sorted(PACKAGE_DIR.glob("*.sh"))
        self.assertTrue(scripts)
        for script in scripts:
            with self.subTest(script=script.name):
                result = subprocess.run(
                    ["sh", "-n", str(script)],
                    text=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
