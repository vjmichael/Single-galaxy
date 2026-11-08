#!/usr/bin/env python3
"""Guard source-level behaviors. Does NOT prove Windows PowerShell/Godot runtime."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
b=(r/'build_tools/Build-Windows.ps1').read_text()
s=(r/'world/native_session.gd').read_text()
m=(r/'world/main.gd').read_text()
checks={
  'official digest required': "'^sha256:([0-9a-fA-F]{64})$'" in b,
  'exact official size required': '[int64]$asset.size' in b,
  'download remains HTTPS': "'--proto' '=https'" in b and "'--proto-redir' '=https'" in b,
  'original release URL checked': 'https://github.com/godotengine/godot-builds/releases/download/' in b,
  'partial downloads persisted': '$output.part' in b,
  'resume supported': "'--continue-at' '-'" in b,
  'bounded retry supported': '$attempt -le 3' in b,
  'partial hash required': 'Get-FileHash -LiteralPath $tmp -Algorithm SHA256' in b,
  'installed editor version matched': "'^4\.7\.2\.stable'" in b,
  'installed editor provenance truthful': 'user-installed-Godot-version-verified-no-official-asset-digest' in b,
  'visible persistent diagnostics': 'sg_native_bridge_status.log' in s,
  'retained last frame': '_last_verified_tick' in s and 'last verified frame retained' in m,
  'bounded native observation': '"600"' in s,
  'no native client move commands': not any(token in s for token in ('THRUST|','BOARD|','MINE|','sqlite3_exec')),
  'F7 stop toggle': 'native_session.is_active()' in m,
  'native stderr exposed': '_read_stderr' in s,
  'native stdout draining': 'for i in range(16):' in s,
}
for label,pass_ in checks.items():
 if not pass_:raise AssertionError(label)
print(f"PASS {len(checks)}/{len(checks)} recovery-tool source checks; Godot/Windows runtime not executed")
