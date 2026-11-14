"use strict";
const test=require("node:test");
const assert=require("node:assert/strict");
const fs=require("node:fs");
const path=require("node:path");
const {validateWO2}=require("../scripts/validate_wo2_professions.js");
const {generate}=require("../scripts/generate_wo2_professions.js");
const folder=path.join(__dirname,"..","data","industry");
const industry=JSON.parse(fs.readFileSync(path.join(folder,"industry01_candidate_catalog.json"),"utf8"));
const profession=JSON.parse(fs.readFileSync(path.join(folder,"wo2_professions.json"),"utf8"));
const copy=()=>JSON.parse(JSON.stringify(profession));
const bad=(change,keyword)=>{
 const x=copy();
 change(x);
 const result=validateWO2(industry,x);
 assert.equal(result.ok,false,"invalid workforce definition was accepted");
 assert.ok(result.errors.some(e=>e.includes(keyword)),result.errors.join("; "));
};
test("every production and recycling capability and service staffing role maps to a finite trade",()=>{
 const v=validateWO2(industry,profession);
 assert.equal(v.ok,true,v.errors.join("; "));
 assert.deepEqual(v.stats,{trades:29,service_roles:9,native_bindings:2,recipe_nodes:74});
 for(const r of [...industry.recipes,...industry.recovery_recipes]){
  const p=profession.site_professions.find(x=>x.site_capability===r.site_capability);
  assert.ok(p,"missing physical production capability "+r.id);
 }
 for(const s of industry.services)for(const staff of s.staff)
  assert.ok(profession.service_roles.find(x=>x.staff_role===staff),"missing service role "+staff);
});
test("only the two original physically built workshop recipes may have active authority",()=>{
 const native=profession.site_professions.filter(p=>p.native_binding!==null);
 assert.deepEqual(native.map(x=>x.native_binding).sort(),["native_cut_aluminum_panel","native_shape_aluminum_frame"]);
 bad(w=>w.site_professions.find(x=>x.site_capability==="precision_electronics").native_binding="assemble_computer",
     "invented native work authority");
 bad(w=>w.site_professions.find(x=>x.site_capability==="cut").training_units=2,
     "real workshop credential altered");
 bad(w=>w.site_professions.find(x=>x.site_capability==="shape").minimum_workers=2,
     "real workshop credential altered");
});
test("no missing trade, redundant work taxonomy or phantom specialist certificates",()=>{
 bad(w=>w.site_professions.pop(),"missing trade for");
 bad(w=>w.site_professions.push({...w.site_professions[0]}),"unknown or duplicate production capability");
 bad(w=>w.site_professions.find(x=>x.site_capability==="metal_forming").training_policy="free_skill_on_hire",
     "invalid staffed trade");
 bad(w=>w.service_roles.pop(),"missing service worker");
 bad(w=>w.service_roles.push({...w.service_roles[0]}),"unknown or duplicate staff role");
});
test("no automatic wage, work authorization, shadow inventory or spawned NPCs",()=>{
 for(const [id,value] of Object.entries(profession.management_constraints))
   assert.equal(value,true,id);
 assert.equal(profession.owner_policy,"world_owner_approves_every_new_delegated_policy_or_order");
 bad(w=>w.owner_policy="auto_staff_without_owner","missing owner consent");
 bad(w=>w.management_constraints.no_shadow_inventory=false,"missing original-world staff authority invariant");
 bad(w=>w.management_constraints.no_spawned_workers=false,"missing original-world staff authority invariant");
});
test("deterministic generated C++ profiles exactly match checked in registry",()=>{
 const generated=generate(industry,profession);
 const committed=fs.readFileSync(path.join(__dirname,"..","src","WO2ProfessionCatalog.generated.cpp"),"utf8");
 assert.equal(generated,committed);
 assert.ok(generated.includes("validate_against_industry01"));
});
test("management stages progress in order but never grant implicit staffing authority",()=>{
 assert.deepEqual(profession.stages.map(x=>x.id),
  ["volunteer","division_of_labor","appointed_manager","formal_workforce"]);
 bad(w=>w.stages.pop(),"wrong phased-management progression");
 bad(w=>w.stages[1].policy="","invalid management stage");
});
