"""Selected-tab-only glass and opaque native-theme pixels/input at every DPI.

Every positive run uses unmodified production QML. Flat-gradient negative
controls must fail the same strict rendered-reflection assertion, not merely a
property check. Never hide a renderer failure with a retry or a relaxed limit.
"""
import os
from pathlib import Path
import subprocess
import sys


def main():
    # An omitted stop reads recycled native memory on older Qt 5/6. Exercise
    # the exact production factory independently before the full DPI matrix;
    # this never retries a failed frame or replaces any pixel assertion.
    subprocess.run([sys.executable, "-X", "faulthandler", "-u",
                    str(Path(__file__).with_name("settings_panel_gradient_test.py"))],
                   check=True, timeout=30)
    test = Path(__file__).with_name("settings_panel_glass_test.py")
    artifacts = os.environ.get("BSI_GLASS_ARTIFACTS")
    for negative in (None, "flat", "frame"):
        for scale in (("1", "2") if negative else (None, "1", "1.25", "1.5", "2")):
            name = (negative + "-negative-" if negative else "production-") + (scale or "host-default")
            env = dict(os.environ)
            env.pop("BSI_GLASS_NEGATIVE_FLAT", None)
            env.pop("BSI_SETTINGS_NEGATIVE_FRAME", None)
            if scale is not None:
                env["QT_SCALE_FACTOR"] = scale
            if negative == "flat":
                env["BSI_GLASS_NEGATIVE_FLAT"] = "1"
            elif negative == "frame":
                env["BSI_SETTINGS_NEGATIVE_FRAME"] = "1"
            if artifacts:
                env["BSI_GLASS_ARTIFACTS"] = str(Path(artifacts) / name)
            result = subprocess.run([sys.executable, "-X", "faulthandler", "-u", str(test)],
                                    env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                    timeout=90)
            if negative:
                assertion = ("the selected-tab reflection is not present in the rendered pixels" if negative == "flat"
                             else "the whole Settings frame must use the exact flat native theme")
                valid = result.returncode == 1 and ("AssertionError: ('" + assertion + "'") in result.stdout
            else:
                valid = result.returncode == 0 and "PASS Qt " in result.stdout
            if not valid:
                print(result.stdout, end="", flush=True)
                raise AssertionError(f"{name}: unexpected glass-test result {result.returncode}")
            if negative:
                print(f"PASS: {name} rejected by strict rendered-pixel assertion: {assertion}", flush=True)
            else:
                print(f"PASS: {name}\n{result.stdout}", end="", flush=True)
    print("PASS: five production DPI runs plus four strict negative controls; selected-tab reflection, exact opaque native-theme frame/page/footer pixels and unchanged native input/contrast thresholds")


if __name__ == "__main__":
    main()
