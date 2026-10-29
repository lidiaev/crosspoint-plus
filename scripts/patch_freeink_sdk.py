"""
PlatformIO pre-build script: apply CrossPoint Plus patches to the FreeInk SDK.

The SDK is a submodule pinned to upstream; CrossPoint Plus carries its display
changes (UC8279 fast typing: short custom LUT, windowed transfers) as patches in
`scripts/freeink_sdk_patches/`, applied in lexical order to the submodule
working tree so CI release builds get them too.

Idempotency is decided by git:
  * `git apply --check --reverse` succeeds  -> already applied, skip
  * `git apply --check`            succeeds  -> apply
  * neither succeeds                          -> abort the build
"""

Import("env")  # noqa: F821 (SCons-injected global)
import os
import subprocess
import sys

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
PATCH_DIR = os.path.join(PROJECT_DIR, "scripts", "freeink_sdk_patches")
SDK_DIR = os.path.join(PROJECT_DIR, "freeink-sdk")


def _git_apply(args, patch):
    return subprocess.run(["git", "apply", *args, patch], cwd=SDK_DIR, capture_output=True, text=True)


def main():
    if not os.path.isdir(PATCH_DIR) or not os.path.exists(os.path.join(SDK_DIR, ".git")):
        return
    for name in sorted(os.listdir(PATCH_DIR)):
        if not name.endswith(".patch"):
            continue
        patch = os.path.join(PATCH_DIR, name)
        if _git_apply(["--check", "--reverse"], patch).returncode == 0:
            continue
        check = _git_apply(["--check"], patch)
        if check.returncode != 0:
            sys.stderr.write(f"[freeink-sdk] patch {name} does not apply:\n{check.stderr}\n")
            env.Exit(1)  # noqa: F821
        _git_apply([], patch)
        print(f"[freeink-sdk] applied {name}")


main()
