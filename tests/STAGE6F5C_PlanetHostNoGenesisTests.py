#!/usr/bin/env python3
"""S6-F5-C: no original player landing means no desktop planet writer genesis.

This is a native host fixture, not proof of an installed Godot session or
landing. Positive 3D ground movement/restart acceptance remains outstanding.
"""
import pathlib
import subprocess
import sys
import tempfile


def invoke(exe, *args):
    return subprocess.run([str(exe), *map(str, args)], input="QUIT\n",
                          capture_output=True, text=True, timeout=20)


def main():
    exe = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="sg-f5c-") as d:
        root = pathlib.Path(d)
        missing = root / "missing-planet.sqlite"
        absent = invoke(exe, missing, "--planet")
        assert absent.returncode != 0
        assert not missing.exists(), "Planet mode must not mint a new original save"
        assert "existing original SQLite world" in absent.stderr

        # Existing standalone station genesis remains allowed only in the
        # original (non-planet) mode. No fake planetary arrival is inferred.
        legacy = root / "unlanded-original.sqlite"
        spawned = invoke(exe, legacy)
        assert spawned.returncode == 0, spawned.stderr
        assert legacy.exists()
        denied = invoke(exe, legacy, "--planet")
        assert denied.returncode != 0
        assert "not verified ashore" in denied.stderr
        assert "PFRAME" not in denied.stdout

        # The source-level planet host must also respect the original
        # colony .host.lock; Linux flock is tested here. Windows OS lock
        # admission remains an installed native acceptance requirement.
        if sys.platform != "win32":
            import fcntl
            with open(str(legacy) + ".host.lock", "a+b") as guard:
                fcntl.flock(guard, fcntl.LOCK_EX | fcntl.LOCK_NB)
                overlap = invoke(exe, legacy, "--planet")
                assert overlap.returncode != 0
                assert "writer already active" in overlap.stderr
    print("S6-F5-C missing/unlanded/dual-writer refusals observed")


if __name__ == "__main__":
    main()
