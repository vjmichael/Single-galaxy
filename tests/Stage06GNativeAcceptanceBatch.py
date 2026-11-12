#!/usr/bin/env python3
"""S6-G exact-head NATIVE acceptance batch with machine-readable receipts.
Run manually on the actual checked-out native Windows/Linux source and build,
AFTER implementation. This file does NOT create CI, start an automation,
claim installed-Godot/SGDX hardware security, or pass merely by existing.
Usage: python tests/Stage06GNativeAcceptanceBatch.py --source . --build build
       [--config Release] [--output acceptance/stage06g-native.json]
"""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

CORE=(
    "galaxy_stage06_seasonal_environment",
    "galaxy_stage06_finite_planet_genesis",
    "galaxy_stage06_physical_environment",
    "galaxy_stage06_authorized_source",
    "galaxy_stage06_ecology_industry",
    "galaxy_stage06g_consolidated_migration",
)
SURFACE=(
    "galaxy_stage6_original_planet_surface",
    "galaxy_stage6_authorized_landing",
    "galaxy_stage6_player_planetary_custody",
    "galaxy_stage6_native_surface_movement",
    "galaxy_stage6f5b_continuous_surface_math",
    "galaxy_stage6f5c_atmosphere_source",
    "galaxy_stage6f5c_body_gravity",
    "galaxy_stage6f5c_vertical_physics",
    "galaxy_stage6f5e_planet_host_recovery",
    "galaxy_stage6f5f_original_shuttle_roundtrip",
    "galaxy_stage6f6_original_role_reconciliation",
    "galaxy_stage06g_original_world_audit_matrix",
    "galaxy_stage6f5c_original_planet_host_refusal",
)
INDUSTRY=(
    "galaxy_wo2_industry_workforce",
    "galaxy_wo3_colony_operations",
    "galaxy_wo3_planetary_housing",
    "galaxy_wo3_historical_sqlite_import",
    "galaxy_wo3_planetary_utility",
    "galaxy_wo4_resource_demand",
    "galaxy_wo4_material_allocation",
    "galaxy_wo5_physical_logistics",
    "galaxy_dock07_local_industry",
    "galaxy_dock08_colony_facility_workforce",
    "galaxy_dock09_original_colony_industry_vertical",
)
GODOT=(
    "galaxy_stage6f5e_godot_tile_window",
    "galaxy_stage6f5f_godot_shuttle_source",
    "galaxy_stage6f6_godot_recovery_source",
    "galaxy_stage6f6_godot_unsigned_source_id",
    "galaxy_stage6f6_godot_selected_world_hint",
)
SOURCE_SURVEY=(
    "galaxy_play13d_planetary_watershed",
    "galaxy_play13d_planetary_geography",
    "galaxy_play13d_live_planet_survey",
)
GROUPS={
    "environment_and_migration": CORE+SOURCE_SURVEY,
    "surface_and_native_recovery": SURFACE,
    "industry_and_logistics": INDUSTRY,
    "godot_source_only": GODOT,
}

def run(argv, timeout, cwd=None):
    started=time.monotonic()
    try:
        output=subprocess.run(argv,cwd=cwd,capture_output=True,text=True,
                              errors="replace",timeout=timeout,check=False)
        return {"command":argv,"exit_code":output.returncode,
                "elapsed_seconds":round(time.monotonic()-started,3),
                "stdout_tail":output.stdout[-24000:],
                "stderr_tail":output.stderr[-24000:]}
    except subprocess.TimeoutExpired as e:
        return {"command":argv,"exit_code":None,"timeout":True,
                "elapsed_seconds":round(time.monotonic()-started,3),
                "stdout_tail":str(e.stdout or "")[-24000:],
                "stderr_tail":str(e.stderr or "")[-24000:]}

def receipt(destination,data):
    destination.parent.mkdir(parents=True,exist_ok=True)
    temp=destination.with_name(destination.name+".tmp")
    with temp.open("w",encoding="utf-8") as f:
        json.dump(data,f,indent=2,sort_keys=True)
        f.write("\n")
        f.flush()
        os.fsync(f.fileno())
    os.replace(temp,destination)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source",required=True,type=Path)
    ap.add_argument("--build",required=True,type=Path)
    ap.add_argument("--config",default="Release")
    ap.add_argument("--output",type=Path,default=Path("stage06g-native-acceptance.json"))
    args=ap.parse_args()
    src=args.source.resolve()
    build=args.build.resolve()
    dest=args.output.resolve()
    report={"schema":"SG-S6G-EXACT-HEAD-NATIVE-01",
            "status":"blocked","source":str(src),"build":str(build),
            "config":args.config,"limitations":[
                "No installed Windows human or physical Radeon acceptance",
                "No independent Windows SCM/Job kernel or Linux cgroup proof",
                "No full multi-cell server/long-duration production acceptance",
                "A CTest PASS is native evidence only, not release authorization",
            ],"phases":{}}
    if not (src/"CMakeLists.txt").is_file() or not (build/"CMakeCache.txt").is_file():
        report["blocker"]="Missing source or preconfigured native CMake build"
        receipt(dest,report)
        return 2
    sha=run(["git","-C",str(src),"rev-parse","HEAD"],10)
    changes=run(["git","-C",str(src),"status","--porcelain"],10)
    if sha["exit_code"]!=0 or changes["exit_code"]!=0 or changes["stdout_tail"].strip():
        report["blocker"]="Exact-head run requires a known clean source checkout"
        report["git"]={"head":sha,"working_tree":changes}
        receipt(dest,report)
        return 2
    report["exact_source_head"]=sha["stdout_tail"].strip()
    if not re.fullmatch(r"[0-9a-f]{40}",report["exact_source_head"]):
        report["blocker"]="Git did not supply an exact 40-character source SHA"
        receipt(dest,report)
        return 2
    # Prevent a perfectly passing test from a DIFFERENT CMake source tree.
    cache=(build/"CMakeCache.txt").read_text(encoding="utf-8",errors="replace")
    home=re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$",cache,re.MULTILINE)
    if not home or Path(home.group(1)).resolve()!=src:
        report["blocker"]="Build cache does not reference exact source checkout"
        receipt(dest,report)
        return 2
    receipt(dest,report)
    compilation=run(["cmake","--build",str(build),"--config",args.config,
                     "--parallel","4"],2400)
    report["phases"]["compile"]=compilation
    receipt(dest,report)
    if compilation["exit_code"]!=0:
        report["status"]="failed_native_compile"
        receipt(dest,report)
        return 1
    listing_argv=["ctest","--test-dir",str(build),"-C",args.config,
                  "--show-only=json-v1"]
    listed=subprocess.run(listing_argv,capture_output=True,text=True,
                          errors="replace",timeout=60,check=False)
    report["phases"]["discovery"]={
        "command":listing_argv,"exit_code":listed.returncode,
        "stderr_tail":listed.stderr[-12000:]}
    if listed.returncode!=0:
        report["status"]="blocked_native_test_discovery"
        receipt(dest,report)
        return 2
    try:
        # CTest can return hundreds of records (>24KiB). Parse the full
        # captured payload, never the intentionally bounded report excerpt.
        registered={t["name"] for t in json.loads(listed.stdout)["tests"]}
    except (ValueError,TypeError,KeyError) as e:
        report["status"]="blocked_native_test_discovery"
        report["blocker"]="Invalid CTest JSON: "+str(e)
        receipt(dest,report)
        return 2
    required=set(CORE+SOURCE_SURVEY+SURFACE+INDUSTRY)
    missing=sorted(required-registered)
    report["missing_native_tests"]=missing
    if missing:
        report["status"]="blocked_missing_native_tests"
        receipt(dest,report)
        return 2
    failed=[]
    for group, names in GROUPS.items():
        available=tuple(x for x in names if x in registered)
        if group=="godot_source_only" and not available:
            report["phases"][group]={"status":"not_installed","details":"Godot optional; installed gate still open"}
            receipt(dest,report)
            continue
        if group=="godot_source_only" and len(available)!=len(names):
            report["phases"][group]={"status":"incomplete","missing":sorted(set(names)-registered)}
            failed.append(group)
            receipt(dest,report)
            continue
        regex="^("+ "|".join(re.escape(x) for x in available)+")$"
        result=run(["ctest","--test-dir",str(build),"-C",args.config,
                    "--output-on-failure","-R",regex],2400)
        report["phases"][group]=result
        if result["exit_code"]!=0:
            failed.append(group)
        receipt(dest,report)
    report["status"]="failed_native_matrix" if failed else "native_matrix_passed_only"
    report["failed_groups"]=failed
    receipt(dest,report)
    return 1 if failed else 0

if __name__=="__main__":
    sys.exit(main())
