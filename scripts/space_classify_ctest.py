#!/usr/bin/env python3
"""Classify CTest --output-junit results; missing evidence fails closed.

Usage: python3 scripts/space_classify_ctest.py build/.../ctest-results.xml
Exit 0: all pass; 1: failures or skipped; 2: unexpected failure count;
3: absent, invalid or empty evidence.
"""
import argparse
import collections
import json
import pathlib
import re
import sys
import xml.etree.ElementTree as ET


def group(name):
    tokens = re.split(r"[_\s./-]+", name.lower())
    for key in ("space", "flight", "wo5", "wo4", "wo3", "play", "npc",
                "ship", "colony", "industry", "econ", "work"):
        if key in tokens or any(t.startswith(key) for t in tokens):
            return key.upper()
    return "OTHER"


def classify(path):
    root = ET.parse(path).getroot()
    cases = root.findall(".//testcase")
    if not cases:
        raise ValueError("JUnit has zero testcase elements")
    seen, failures, counts = set(), [], collections.Counter()
    for case in cases:
        name = case.get("name", "")
        owner = case.get("classname", "")
        identity = (owner, name)
        if not name or identity in seen:
            raise ValueError("missing or duplicate testcase identity")
        seen.add(identity)
        problem = case.find("failure")
        if problem is None:
            problem = case.find("error")
        if problem is not None:
            counts["FAIL"] += 1
            failures.append({
                "test": name, "class": owner, "group": group(name),
                "message": (problem.get("message") or problem.text or "").strip()[:2000]
            })
        elif case.find("skipped") is not None:
            counts["SKIP"] += 1
        else:
            counts["PASS"] += 1
    clusters = collections.Counter(f["group"] for f in failures)
    return {
        "source": str(path), "total": len(cases),
        "passed": counts["PASS"], "failed": counts["FAIL"],
        "skipped": counts["SKIP"], "failure_groups": dict(sorted(clusters.items())),
        "failures": failures
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("junit", type=pathlib.Path)
    parser.add_argument("--json-out", type=pathlib.Path)
    parser.add_argument("--expect-failures", type=int)
    args = parser.parse_args()
    try:
        result = classify(args.junit)
    except (OSError, ET.ParseError, ValueError) as error:
        print("UNVERIFIED CTEST EVIDENCE: " + str(error), file=sys.stderr)
        return 3
    document = json.dumps(result, indent=2, sort_keys=True)
    print(document)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(document + "\n", encoding="utf-8")
    if args.expect_failures is not None and args.expect_failures != result["failed"]:
        print("FAILURE COUNT MISMATCH: expected={} actual={}".format(
            args.expect_failures, result["failed"]), file=sys.stderr)
        return 2
    return 1 if result["failed"] or result["skipped"] else 0


if __name__ == "__main__":
    sys.exit(main())
