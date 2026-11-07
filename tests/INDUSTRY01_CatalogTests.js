"use strict";
const test=require("node:test");
const assert=require("node:assert/strict");
const path=require("node:path");
const fs=require("node:fs");
const {validateCatalog}=require("../scripts/validate_industry01.js");
const original=JSON.parse(fs.readFileSync(
  path.join(__dirname,"..","data","industry","industry01_candidate_catalog.json"),"utf8"
));
const copy=()=>JSON.parse(JSON.stringify(original));
function rejects(changer,keyword) {
  const c=copy();
  changer(c);
  const result=validateCatalog(c);
  assert.equal(result.ok,false,"invalid catalogue was accepted");
  assert.ok(result.errors.some(e=>e.includes(keyword)),result.errors.join("; "));
}
test("INDUSTRY-01 candidate validates; no world-facing material or consumption shortcuts",()=>{
  const result=validateCatalog(original);
  assert.deepEqual(result.errors,[]);
  assert.equal(result.ok,true);
  assert.deepEqual(result.stats,{items:65,recipes:45,services:7,legacy_mappings:6});
  assert.equal(original.status,"research_candidate_not_world_authoritative");
  assert.ok(original.recipes.every(x=>x.quantities==="illustrative_game_balance"));
  assert.ok(original.legacy_bridge.every(x=>x.authoritative_conversion!=="automatic"));
});
test("shared copper and electronics inputs serve energy, communications, media and healthcare",()=>{
  const uses=original.recipes.filter(r=>r.inputs.some(i=>i.item==="part/circuit_board"));
  const outputs=new Set(uses.flatMap(r=>r.outputs.map(o=>o.item)));
  for(const id of ["item/computer","item/network_node","item/radio","item/camera","item/video_camera","item/printer"])
    assert.ok(outputs.has(id),"shared PCB missing consumer "+id);
  const tags=original.items.find(x=>x.id==="item/houseplant").need_tags;
  assert.ok(tags.includes("nature"));
  assert.ok(original.services.some(s=>s.id==="service/legal_consultation"&&!s.durable_requirements.includes("item/printed_document")));
});
test("duplicates, unknown IDs, malformed material dimensions and misleading yields fail closed",()=>{
  rejects(c=>c.items.push({...c.items[0]}),"duplicate item");
  rejects(c=>c.recipes[0].inputs[0].item="bulk/imaginary_metal","unknown input");
  rejects(c=>c.items[0].mass_g_per_unit=-1,"positive safe-integer");
  rejects(c=>c.recipes[0].outputs[0].qty=2,"mass mismatch");
  rejects(c=>c.recipes[0].inputs[0].qty=0,"invalid input quantity");
  rejects(c=>c.recipes[0].work_ticks="4","invalid capacity");
  rejects(c=>c.recipes[0].source_ids=["invented_claim"],"unknown or missing source");
});
test("dependency cycle detected, even where a conversion conserves mass",()=>{
  rejects(c=>{
    const p=(id,i,o)=>({
      id,site_capability:"workshop",work_ticks:1,energy_kj:1,
      inputs:[{item:i,qty:1}],outputs:[{item:o,qty:1}],
      source_ids:["epa"],quantities:"illustrative_game_balance"
    });
    // Both count as one 1000 g product; an unbounded loop is not an approved
    // new manufacturing value chain merely because mass is conserved.
    c.recipes.push(p("circular_pack_a","item/paper_pack","item/detergent_pack"));
    c.recipes.push(p("circular_pack_b","item/detergent_pack","item/paper_pack"));
  },"production dependency cycle");
});
test("legacy identities cannot silently map kg stock to invented gram units",()=>{
  rejects(c=>c.legacy_bridge[0].authoritative_conversion="automatic","unsafe automatic conversion");
  rejects(c=>c.legacy_bridge[0].proposed_item="bulk/unknown","unknown bridge target");
  rejects(c=>c.legacy_bridge.push({...c.legacy_bridge[0]}),"duplicate legacy mapping");
});
test("services require genuine staff, site capabilities and equipment",()=>{
  rejects(c=>c.services[0].staff=[],"invalid service staffing");
  rejects(c=>c.services[0].durable_requirements.push("item/fake_washing_machine"),"unknown durable_requirements");
  rejects(c=>c.services[0].source_ids=["mystery"],"unknown service source");
});
