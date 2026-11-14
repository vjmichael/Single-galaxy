#!/usr/bin/env bash
# Restore the user-owned private LLVM 23.1.5 Linux x64 three-part archive.
# Never commit the downloaded LLVM archive or extracted 12 GiB tree to Git.
set -euo pipefail
PARTS="$1"
DEST="$2"
mkdir -p "$DEST"
ZIP="$PARTS/LLVM-23.1.5-Linux-X64.tar.zip"
cat "$PARTS"/LLVM-23.1.5-Linux-X64.tar.zip.00{1,2,3} > "$ZIP"
echo "1d9d0f74a70ab8fd6630770022706dd2dbc25e08ecca0ce751d95a9dabf24ea4  $ZIP" | sha256sum -c -
python3 - "$ZIP" "$DEST" <<'PY'
import subprocess
import sys
import zipfile

archive, destination = sys.argv[1:]
with zipfile.ZipFile(archive) as z:
    if z.testzip() is not None:
        raise RuntimeError("LLVM 23 ZIP CRC check failed")
    with z.open("LLVM-23.1.5-Linux-X64.tar.zst") as src:
        # The upstream Zstd frame uses a ~1 GiB window. tar --zstd without
        # --long=30 fails with Window size larger than maximum.
        cmd = ['bash', '-o', 'pipefail', '-c',
               'zstd -d --long=30 -c | tar -xf - -C "' + destination + '"']
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        try:
            while True:
                data = src.read(4 * 1024 * 1024)
                if not data:
                    break
                proc.stdin.write(data)
        except BrokenPipeError:
            pass
        proc.stdin.close()
        if proc.wait() != 0:
            raise RuntimeError("LLVM 23 Zstd/tar unpack failed")
print("PASS LLVM 23 archive rebuilt and extracted")
PY
"$DEST/LLVM-23.1.5-Linux-X64/bin/clang-cl" --version | head -1
