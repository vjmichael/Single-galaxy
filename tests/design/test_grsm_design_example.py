"""Documentation-level GRSM example checks, NOT runtime simulation tests."""
import json
import pathlib
import unittest

EXAMPLE = (pathlib.Path(__file__).resolve().parents[2] /
           "docs/design/examples/grsm_incident_and_claims.json")

class DesignContractExamples(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.payload = json.loads(EXAMPLE.read_text(encoding="utf-8"))

    def test_factual_history_is_single_source_of_truth(self):
        d = self.payload
        ids = {x["event_id"] for x in d["canonical_events"]}
        self.assertEqual(len(ids), len(d["canonical_events"]))
        for claim in d["claims"]:
            self.assertIn(claim["related_event_id"], ids)
        ev = d["canonical_events"][0]
        self.assertEqual(ev["occupants_before"], ev["occupants_survived"] + ev["occupants_deceased"])
        self.assertEqual(ev["occupants_deceased"], 0)
        self.assertEqual(ev["cargo_containers_seized"], 2)

    def test_distorted_story_does_not_rewrite_event(self):
        ev = self.payload["canonical_events"][0]
        claims = self.payload["claims"]
        self.assertTrue(any(c["distorted"] for c in claims))
        self.assertEqual(ev["cargo_containers_seized"], 2)
        self.assertEqual(ev["occupants_deceased"], 0)
        self.assertTrue(any(p["kind"] == "freighters_captured" and p["value"] == 12
                            for c in claims for p in c["propositions"]))

    def test_different_faction_interpretations_and_conditional_recognition(self):
        c = {x["speaker_faction_id"]: x for x in self.payload["claims"]}
        self.assertGreater(c["merchant-guild"]["sentiments"]["perceived_danger"],
                           c["frontier-relief-circle"]["sentiments"]["perceived_danger"])
        self.assertGreater(c["frontier-relief-circle"]["sentiments"]["admiration"],
                           c["merchant-guild"]["sentiments"]["admiration"])
        self.assertTrue(any(not x["recognizes_player"] for x in self.payload["identification_cases"]))

if __name__ == "__main__":
    unittest.main()
