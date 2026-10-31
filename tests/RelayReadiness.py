"""Reusable loopback test readiness; monotonic deadlines, not fixed retry counts.

Test-only helper: does not change server authorization or production behavior.
"""
import time


def wait_for_relay(process, probe, *, seconds=15.0, label='local relay'):
    if seconds <= 0:
        raise ValueError('readiness deadline must be positive')
    end = time.monotonic() + seconds
    last = 'no successful response'
    while time.monotonic() < end:
        rc = process.poll()
        if rc is not None:
            try:
                stdout, stderr = process.communicate(timeout=1)
                detail = (stderr or stdout or b'')[-900:].decode(errors='replace')
            except (OSError, TimeoutError):
                detail = '<could not read exit diagnostics>'
            raise AssertionError(f'{label} exited before listening (code {rc}): {detail}')
        try:
            result = probe()
            if result:
                return result
        except (OSError, TimeoutError, ConnectionError) as exc:
            last = f'{type(exc).__name__}: {exc}'
        time.sleep(0.05)
    raise AssertionError(f'{label} not ready after {seconds:g}s; process exit={process.poll()}, last={last}')
