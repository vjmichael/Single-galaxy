# FP3D-02A-WINTEST: portable Windows game export

Double-click `BUILD_WINDOWS.cmd` (no Godot editor installation or Python needed). The builder fetches the **official Godot 4.7.2 stable** Windows editor and standard export templates from `godotengine/godot-builds` on GitHub, requires each archive to match the release's SHA-256 digest, extracts the editor locally, and installs only the Windows x64 export template. Downloading the template archive can use over 1 GB of bandwidth and temporary disk space. A pre-existing template is accepted but is labelled not independently authenticated.

After importing the project with real Godot and passing `godot_smoke.gd`, the builder exports an embedded-PCK standalone Windows PE32+ file to `build/SingleGalaxy_Graybox.exe`, checks the x86-64 PE signature, bundles the upstream MIT notice, and packages `SingleGalaxy_Graybox_Windows_x64.zip` at the project root.

The builder also writes `SingleGalaxy_Windows_Build_Evidence.zip` containing actual Godot stdout/stderr and `build-result.json` regardless of whether a build succeeds. Upload that evidence ZIP into the `Single Galaxy` Library for review. A successful editor smoke test does **not** establish interactive native Windows GPU/collision acceptance.

The release build is a **local graybox prototype, not a connected game kernel**: it must not be confused with the R4 browser simulation, authoritative ship custody, combat or saved NPCs. Windows 10/11 and working OpenGL 3.3 compatibility GPU/driver are prerequisites.

We must verify the download on a machine with network access, including final SHA256, Godot `--version` and exit codes. Do not claim this script has been run on Windows until its log artifact exists.
