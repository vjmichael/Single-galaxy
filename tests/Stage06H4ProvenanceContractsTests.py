#!/usr/bin/env python3
"""INT-03 negative fixture checks; not evidence of vintage game execution."""
import hashlib
import sqlite3
import unittest
import xml.etree.ElementTree as ET
from Stage06H4ProvenanceChecks import (
    verified_ctest_selection, verify_corpus_summary, verify_source_record,
)


class HistoricalEvidenceContracts(unittest.TestCase):
    def test_original_ctest_selection_not_swappable(self):
        case = ET.Element('testcase', name='galaxy_wo3_planetary_utility')
        good = ['^galaxy_wo3_planetary_utility$']
        self.assertEqual(verified_ctest_selection(good, [case]), [case.get('name')])
        for declared, cases in [
            (['^galaxy_wo3_planetary_housing$'], [case]),
            (['.*'], [case]),
            (good, [case, case]),
            ([], [case]),
            (good, []),
        ]:
            with self.subTest(declared=declared, cases=len(cases)), self.assertRaises(RuntimeError):
                verified_ctest_selection(declared, cases)
        for child in ('failure', 'error', 'skipped'):
            failed = ET.Element('testcase', name=case.get('name'))
            ET.SubElement(failed, child)
            with self.subTest(child=child), self.assertRaises(RuntimeError):
                verified_ctest_selection(good, [failed])

    def test_source_metadata_and_version_coverage_not_invented(self):
        first = b'galaxy-kernel-v127|old\nnpc|human|1\n'
        last = b'galaxy-kernel-v128|new\nnpc|human|1\nplanet|1\n'
        with sqlite3.connect(':memory:') as db:
            db.executescript('''CREATE TABLE snapshots(generation INTEGER,state BLOB,checksum TEXT);
                CREATE TABLE operation_log(generation INTEGER,kind INTEGER);
                CREATE TABLE receipts(id INTEGER);
                INSERT INTO operation_log VALUES(1,0);
                INSERT INTO operation_log VALUES(2,1);
                INSERT INTO receipts VALUES(1);''')
            db.execute('INSERT INTO snapshots VALUES(1,?,?)', (first, 'hash-1'))
            db.execute('INSERT INTO snapshots VALUES(2,?,?)', (last, 'hash-2'))
            record = dict(native_version=128, original_generation=2,
                original_snapshot_rows=2, saved_prior_versions=[127,128],
                committed_command_rows=1, original_receipt_rows=1,
                last_snapshot_sha256=hashlib.sha256(last).hexdigest(),
                line_families={'npc':1,'planet':1},original_npc_line_count=1,
                original_medical_line_count=0,original_galactic_line_count=0,
                original_planetary_line_count=1)
            verify_source_record(record, db)
            manifest = dict(verified_historical_versions=[128], saved_sqlite_artifacts=1,
                            complete_v86_v135_coverage=False)
            verify_corpus_summary(manifest, [record])
            for k, bad in [
                ('native_version', 129), ('original_generation', 3),
                ('saved_prior_versions', [128]), ('original_receipt_rows', 7),
                ('line_families', {'npc':1}),
                ('last_snapshot_sha256', '0'*64),
            ]:
                with self.subTest(field=k), self.assertRaises(RuntimeError):
                    verify_source_record({**record,k:bad}, db)
            with self.assertRaises(RuntimeError):
                verify_corpus_summary({**manifest,'complete_v86_v135_coverage':True}, [record])
            db.execute('DELETE FROM receipts')
            with self.assertRaises(RuntimeError):
                verify_source_record(record, db)


if __name__ == '__main__':
    unittest.main()
