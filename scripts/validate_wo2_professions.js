#!/usr/bin/env node
"use strict";
const fs=require("node:fs");
const path=require("node:path");
function validateWO2(catalog,workforce){
 const errors=[],err=x=>errors.push(x);
 if(!catalog || catalog.schema_version!==1 ||
    catalog.status!=="research_candidates_with_two_exact_native_bindings" ||
    !workforce || workforce.schema_version!==1 ||
    workforce.status!=="wo2_professions_with_only_two_native_workshops")
    return {ok:false,errors:["incompatible industry/workforce schema or authority"]};
 const id=x=>typeof x==="string" && /^[a-z][a-z0-9_]*$/.test(x);
 const pos=x=>Number.isSafeInteger(x) && x>0;
 const nat=x=>Number.isSafeInteger(x) && x>=0;
 const proc=[...(catalog.recipes||[]),...(catalog.recovery_recipes||[])];
 const caps=new Set(proc.map(r=>r.site_capability));
 const staff=new Set((catalog.services||[]).flatMap(s=>s.staff));
 const trade=new Set(),roles=new Set();
 let native=0;
 for(const t of workforce.site_professions||[]){
    if(!t||!caps.has(t.site_capability)||trade.has(t.site_capability)){
        err("unknown or duplicate production capability");continue;
    }
    trade.add(t.site_capability);
    if(!id(t.profession)||!id(t.skill_family)||
       !["basic","skilled","technical","specialist","native_baseline"].includes(t.level)||
       !pos(t.minimum_workers)||!nat(t.training_units)||
       !["future_authorized_training_receipt_required","original_role_and_duty_only"].includes(t.training_policy)||
       t.power_required!==true ||
       t.worker_slot_policy!=="finite_physical_station_per_worker")
       err("invalid staffed trade "+t.site_capability);
    const nativeSite=t.site_capability==="cut"||t.site_capability==="shape";
    if(nativeSite){
       native++;
       const expected=t.site_capability==="cut"?"native_cut_aluminum_panel":"native_shape_aluminum_frame";
       if(t.native_binding!==expected||t.level!=="native_baseline"||
          t.training_units!==0||t.minimum_workers!==1||
          t.effect_authority!=="original_simulation"||
          t.training_policy!=="original_role_and_duty_only")
          err("real workshop credential altered "+t.site_capability);
    }else if(t.native_binding!==null ||
             t.effect_authority!=="not_executable_until_real_facility" ||
             t.training_policy!=="future_authorized_training_receipt_required" ||
             t.level==="native_baseline")
       err("invented native work authority "+t.site_capability);
 }
 for(const x of caps)if(!trade.has(x))err("missing trade for "+x);
 if(native!==2)err("exactly two original workshop executors expected");
 for(const p of workforce.service_roles||[]){
    if(!p||!staff.has(p.staff_role)||roles.has(p.staff_role)){
       err("unknown or duplicate staff role");continue;
    }
    roles.add(p.staff_role);
    if(!id(p.profession)||!id(p.skill_family)||
       !["basic","skilled","technical","specialist"].includes(p.level)||
       !nat(p.training_units)||
       p.training_policy!=="future_authorized_training_receipt_required"||
       p.service_execution!=="not_yet_world_authorized_or_worker_scheduled")
       err("service role has nonphysical credential "+p.staff_role);
 }
 for(const x of staff)if(!roles.has(x))err("missing service worker "+x);
 if(!Array.isArray(workforce.stages)||workforce.stages.length!==4 ||
    workforce.stages.map(x=>x.id).join(",")!=="volunteer,division_of_labor,appointed_manager,formal_workforce")
    err("wrong phased-management progression");
 for(const x of workforce.stages||[]){
    if(!id(x.id)||!id(x.policy)||typeof x.appointed_manager!=="boolean")
       err("invalid management stage");
 }
 const gates=workforce.management_constraints||{};
 for(const x of [
  "no_currency_or_wages_required","no_spawned_workers","no_shadow_inventory",
  "periodic_shift_assignment_is_not_self_authorizing",
  "one_named_npc_one_concurrent_physical_duty",
  "supervisors_do_not_create_skills",
  "training_requires_real_instructor_equipment_and_work_receipt",
  "existing_world_commands_only"
 ])if(gates[x]!==true)err("missing original-world staff authority invariant "+x);
 if(workforce.owner_policy!=="world_owner_approves_every_new_delegated_policy_or_order")
    err("missing owner consent");
 return {ok:errors.length===0,errors,stats:{trades:trade.size,service_roles:roles.size,native_bindings:native,recipe_nodes:proc.length}};
}
module.exports={validateWO2};
if(require.main===module){
 const root=path.join(__dirname,"..","data","industry");
 const industry=JSON.parse(fs.readFileSync(path.join(root,"industry01_candidate_catalog.json"),"utf8"));
 const workforce=JSON.parse(fs.readFileSync(path.join(root,"wo2_professions.json"),"utf8"));
 const result=validateWO2(industry,workforce);
 if(!result.ok){for(const e of result.errors)console.error("WO2: "+e);process.exitCode=1;}
 else console.log("WO2 professions validated:",JSON.stringify(result.stats));
}
