#!/usr/bin/env python3
"""Compatibility entrypoint for the current B5.2 interactive-host acceptance gate.

The historical B4.3 scenario driver is preserved under tests/legacy/ and
should only be run against the original B4.3 host archive. Starting in B5.2,
the current interactive host deliberately begins docked, so B4.3 assumptions
about an initially undocked miner no longer describe this executable.
"""
from B52InteractiveEndToEndTests import direct,relay
import argparse,pathlib
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--executable',required=True,type=pathlib.Path)
 p.add_argument('--relay',required=True,type=pathlib.Path)
 a=p.parse_args();direct(a.executable.resolve());relay(a.executable.resolve(),a.relay.resolve())
