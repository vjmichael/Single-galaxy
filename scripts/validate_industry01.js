#!/usr/bin/env node
"use strict";
// INDUSTRY-01. Pure, research-candidate catalogue audit. No world mutation.
// The game still uses the canonical Simulation/PersistentWorld inventories.
const fs = require("node:fs");
const path = require("node:path");

function validateCatalog(catalog) {
  const errors = [];
  const err = (message) => errors.push(message);
  const positive = (value) => Number.isSafeInteger(value) && value > 0;
  const nonnegative = (value) => Number.isSafeInteger(value) && value >= 0;
  const identifier = (value) => typeof value === "string" &&
    /^[a-z][a-z0-9_]*(?:\/[a-z0-9_]+)*$/.test(value);

  if (!catalog || typeof catalog !== "object") return {ok:false,errors:["catalog must be object"]};
  if (catalog.schema_version !== 1) err("schema_version must be 1");
  if (catalog.status !== "research_candidate_not_world_authoritative")
    err("catalog must explicitly be research-only, never authorized world inventory");
  const sources = catalog.sources && typeof catalog.sources === "object" ? catalog.sources : {};
  for (const [key,source] of Object.entries(sources)) {
    if (!identifier(key) || !source || typeof source.title !== "string" ||
        !/^https:\/\//.test(source.url || "")) err("invalid source: "+key);
  }
  if (!Object.keys(sources).length) err("at least one credible source is required");

  const items = new Map();
  for (const [index,item] of (Array.isArray(catalog.items) ? catalog.items : []).entries()) {
    if (!item || !identifier(item.id) || !item.id.includes("/")) {
      err("invalid item id at index "+index); continue;
    }
    if (items.has(item.id)) err("duplicate item "+item.id);
    items.set(item.id,item);
    if (!["g","count"].includes(item.unit)) err("invalid item unit "+item.id);
    if (!positive(item.mass_g_per_unit) || !positive(item.cargo_mm3_per_unit))
      err("item requires positive safe-integer mass and volume: "+item.id);
    if (item.unit === "g" && item.mass_g_per_unit !== 1)
      err("bulk unit must represent precisely 1 gram: "+item.id);
    if (!["feedstock","intermediate","durable","consumable","waste"].includes(item.lifecycle))
      err("invalid item lifecycle "+item.id);
    if (!Array.isArray(item.need_tags) ||
        item.need_tags.some(v=>!identifier(v))) err("invalid need tags "+item.id);
    if (!identifier(item.group)) err("invalid group "+item.id);
  }
  if (!items.size) err("empty item catalogue");

  const recipes = Array.isArray(catalog.recipes) ? catalog.recipes : [];
  const recipeIds = new Set(), graph = new Map();
  const checkLines = (lines,recipeId,kind) => {
    let mass = 0n;
    if (!Array.isArray(lines) || !lines.length) {
      err(recipeId+" "+kind+" must be nonempty"); return mass;
    }
    const seen = new Set();
    for (const line of lines) {
      const id = line && line.item;
      if (!items.has(id)) {err(recipeId+" unknown "+kind+" item "+id);continue;}
      if (seen.has(id)) err(recipeId+" duplicates "+kind+" item "+id);
      seen.add(id);
      if (!positive(line.qty)) {err(recipeId+" invalid "+kind+" quantity");continue;}
      const unitMass = items.get(id).mass_g_per_unit;
      if (positive(unitMass)) mass += BigInt(unitMass)*BigInt(line.qty);
    }
    return mass;
  };
  for (const [index,r] of recipes.entries()) {
    if (!r || !identifier(r.id)) {err("invalid recipe at index "+index); continue;}
    if (recipeIds.has(r.id)) err("duplicate recipe "+r.id);
    recipeIds.add(r.id);
    if (!identifier(r.site_capability) || !positive(r.work_ticks) || !nonnegative(r.energy_kj))
      err("recipe has invalid capacity, time or energy "+r.id);
    if (r.quantities !== "illustrative_game_balance")
      err("recipe quantities must not falsely claim real-world yields "+r.id);
    if (!Array.isArray(r.source_ids) || !r.source_ids.length ||
        r.source_ids.some(id=>!Object.hasOwn(sources,id)))
      err("unknown or missing source for "+r.id);
    const from=checkLines(r.inputs,r.id,"input");
    const to=checkLines(r.outputs,r.id,"output");
    if (from !== to) err("material mass mismatch for "+r.id+": "+from+"g vs "+to+"g");
    if (Array.isArray(r.inputs) && Array.isArray(r.outputs))
      for(const input of r.inputs) for(const output of r.outputs) {
        if (!items.has(input.item) || !items.has(output.item)) continue;
        const next=graph.get(input.item)||new Set();
        next.add(output.item); graph.set(input.item,next);
      }
  }
  if (!recipes.length) err("empty recipe catalogue");

  // Every straight-line production dependency must be acyclic. Recovery,
  // reuse and recycling will require a separate explicitly bounded contract.
  const color=new Map(),stack=[];
  const visit=(id)=>{
    const state=color.get(id)||0;
    if(state===1){err("production dependency cycle: "+[...stack,id].join(" -> "));return;}
    if(state===2)return;
    color.set(id,1);stack.push(id);
    for(const other of graph.get(id)||[])visit(other);
    stack.pop();color.set(id,2);
  };
  for(const id of graph.keys()) if(!color.has(id))visit(id);

  const services=Array.isArray(catalog.services)?catalog.services:[];
  const serviceIds=new Set();
  for(const service of services) {
    if(!service || !identifier(service.id) || !service.id.startsWith("service/")){
      err("invalid service id");continue;
    }
    if(serviceIds.has(service.id))err("duplicate service "+service.id);
    serviceIds.add(service.id);
    if(!Array.isArray(service.staff)||!service.staff.length ||
       service.staff.some(x=>!identifier(x)))err("invalid service staffing "+service.id);
    if(!Array.isArray(service.site_capabilities)||!service.site_capabilities.length ||
       service.site_capabilities.some(x=>!identifier(x)))err("invalid service site "+service.id);
    for(const name of ["input_consumables","durable_requirements"]){
      if(!Array.isArray(service[name])){err("invalid "+name+" for "+service.id);continue;}
      for(const id of service[name])if(!items.has(id))err("unknown "+name+" "+id+" for "+service.id);
    }
    if(!identifier(service.result))err("invalid service result "+service.id);
    if(!Array.isArray(service.source_ids)||!service.source_ids.length ||
       service.source_ids.some(id=>!Object.hasOwn(sources,id)))
      err("unknown service source "+service.id);
  }
  if(!services.length)err("empty service catalogue");

  // Lossy recycling is separately typed so an intentional material recovery
  // cannot bypass the DAG cycle guard or silently create free resources.
  const recovery=Array.isArray(catalog.recovery_recipes)?catalog.recovery_recipes:[];
  const recoveryIds=new Set();
  for(const x of recovery) {
    if(!x || !identifier(x.id)) {err("invalid recovery id");continue;}
    if(recipeIds.has(x.id)||recoveryIds.has(x.id))err("duplicate recovery recipe "+x.id);
    recoveryIds.add(x.id);
    if(x.kind!=="lossy_recovery_not_straight_line" ||
       !identifier(x.site_capability) ||
       !positive(x.work_ticks) || !nonnegative(x.energy_kj) ||
       x.quantities!=="illustrative_game_balance")
      err("invalid bounded recovery contract "+x.id);
    if(!Array.isArray(x.source_ids)||!x.source_ids.length ||
       x.source_ids.some(id=>!Object.hasOwn(sources,id)))
      err("invalid recovery provenance "+x.id);
    const inputMass=checkLines(x.inputs,x.id,"recovery input");
    const outputMass=checkLines(x.outputs,x.id,"recovery output");
    if(inputMass!==outputMass)err("unconserved recovery mass "+x.id);
    if(!items.has(x.primary_recovered_item) || !Array.isArray(x.outputs) ||
       !x.outputs.some(y=>y.item===x.primary_recovered_item))
      err("missing primary recovered output "+x.id);
    let recovered=0n,secondary=0n;
    for(const line of Array.isArray(x.outputs)?x.outputs:[]) {
      const item=items.get(line.item);
      if(!item || !positive(line.qty))continue;
      const mass=BigInt(item.mass_g_per_unit)*BigInt(line.qty);
      if(line.item===x.primary_recovered_item) recovered+=mass;
      else if(item.lifecycle==="waste")secondary+=mass;
    }
    if(!recovered || recovered>=inputMass || !secondary)
      err("recovery must lose useful output to physical waste "+x.id);
  }
  if(!recoveryIds.size)err("missing separately bounded recovery recipes");

  const calibration=Array.isArray(catalog.research_calibration)?catalog.research_calibration:[];
  const calibrated=new Set();
  for(const x of calibration) {
    if(!x || !recipeIds.has(x.recipe) ||
       typeof x.source_url!=="string" || !x.source_url.startsWith("https://") ||
       typeof x.evidence!=="string" || x.evidence.length<30 ||
       !["illustrative_not_verified_chemistry","process_dependencies_only","game_balance_only"].includes(x.numeric_status) ||
       !identifier(x.environmental_scope))
      err("invalid process research calibration");
    else if(calibrated.has(x.recipe))err("duplicate calibration "+x.recipe);
    else calibrated.add(x.recipe);
  }
  if(!calibrated.size)err("missing measured process research references");

  const legacy=Array.isArray(catalog.legacy_bridge)?catalog.legacy_bridge:[];
  const legacyIds=new Set();
  for(const row of legacy){
    if(!row||!identifier(row.legacy_code)){
      err("invalid legacy item mapping");continue;
    }
    if(legacyIds.has(row.legacy_code))err("duplicate legacy mapping "+row.legacy_code);
    legacyIds.add(row.legacy_code);
    if(row.proposed_item!==null&&!items.has(row.proposed_item))
      err("unknown bridge target "+row.proposed_item);
    if(!["unresolved_do_not_automatically_convert","distinct_sealed_cargo_not_ordinary_recipe_stock"].includes(row.authoritative_conversion))
      err("unsafe automatic conversion of legacy inventory "+row.legacy_code);
  }
  if(!legacy.length)err("legacy inventory compatibility not accounted for");

  return {ok:errors.length===0,errors,stats:{items:items.size,recipes:recipeIds.size,recovery_recipes:recoveryIds.size,services:serviceIds.size,legacy_mappings:legacyIds.size}};
}


function validateLegacyUnits(catalog, audit) {
  const errors=[];
  if(!audit || audit.schema_version!==1 ||
     audit.status!=="original_world_id_audit_read_only" ||
     !Array.isArray(audit.rows))return {ok:false,errors:["legacy audit missing or wrong version"]};
  const seen=new Set(),codes=new Set((catalog.legacy_bridge||[]).map(x=>x.legacy_code));
  for(const row of audit.rows) {
    if(!row || typeof row.legacy_code!=="string" || !row.legacy_code ||
       seen.has(row.legacy_code)) {errors.push("duplicate or invalid legacy identity");continue;}
    seen.add(row.legacy_code);
    if(!codes.has(row.legacy_code))errors.push("original legacy identity missing bridge "+row.legacy_code);
    if(!Number.isSafeInteger(row.storage_litres_per_unit) || row.storage_litres_per_unit<=0 ||
       !Number.isSafeInteger(row.mass_g_per_unit) || row.mass_g_per_unit<=0)
      errors.push("invalid original unit "+row.legacy_code);
    if(row.migration!=="read_only_identity_no_automatic_world_conversion")
      errors.push("unsafe original world migration "+row.legacy_code);
    if(typeof row.original_source_field!=="string" || !row.original_source_field.length)
      errors.push("missing original field "+row.legacy_code);
  }
  for(const code of codes)if(!seen.has(code))errors.push("bridge not in native unit manifest "+code);
  if(seen.size!==28)errors.push("unexpected legacy identity count "+seen.size);
  const unique=(x)=>audit.rows.find(r=>r.legacy_code===x);
  for(const [id,kg,litres] of [
    ["ore",1000000,1000],["refined",1000000,1000],
    ["construction_module",2000000,2000],["good/aluminum",1000,1],
    ["good/aluminum_panels",1000,1],["good/freight_nutrients",1000,1]
  ]) {
    const value=unique(id);
    if(!value || value.mass_g_per_unit!==kg ||
       value.storage_litres_per_unit!==litres)
      errors.push("known original source unit mismatch "+id);
  }
  return {ok:errors.length===0,errors,stats:{native_items:seen.size}};
}


function validateUseContracts(catalog,use) {
  const errors=[],err=x=>errors.push(x);
  if(!use || use.schema_version!==1 ||
    use.status!=="stage1_demand_contracts_no_live_npc_mutation")
    return {ok:false,errors:["use contracts not staged or wrong schema"]};
  if(!use.principles || !Object.values(use.principles).every(x=>x===true))
    err("use contracts must preserve explicit non-magical resource principles");
  const items=new Map(catalog.items.map(x=>[x.id,x]));
  const services=new Set(catalog.services.map(x=>x.id));
  const valid=x=>typeof x==="string" && /^[a-z_]+$/.test(x);
  const needs=new Set();
  for(const x of Array.isArray(use.needs)?use.needs:[]) {
    if(!x || !valid(x.id) || needs.has(x.id) || !valid(x.priority) || !valid(x.access))
      err("invalid need category");
    else needs.add(x.id);
  }
  if(needs.size<6)err("insufficient real needs categories");
  const used=new Set();
  for(const [list,kind] of [["durables","durable"],["consumables","consumable"]]) {
    for(const x of Array.isArray(use[list])?use[list]:[]){
      const item=items.get(x && x.item);
      if(!item || item.lifecycle!==kind || used.has(x.item))
        err("unregistered or duplicate "+kind+" use profile");
      else used.add(x.item);
      if(!Array.isArray(x.need_ids)||!x.need_ids.length ||
         x.need_ids.some(y=>!needs.has(y)))
        err("unknown needs for "+(x&&x.item));
      if(kind==="durable"){
        if(x.condition_max!==100 || !valid(x.shared_access) ||
           !valid(x.maintenance) || x.maintenance==="free_renewal")
          err("durable capacity or maintenance is not physical");
      }else{
        if(!valid(x.shelf_policy)||!valid(x.on_spoil_mass_fraction))
          err("no physical spoilage policy");
        if(x.on_spoil_to!==null &&
          (!items.has(x.on_spoil_to)||items.get(x.on_spoil_to).lifecycle!=="waste"))
          err("spoilage must preserve physical waste");
      }
    }
  }
  if(!used.size)err("no genuine use profiles");
  const named=new Set();
  for(const s of Array.isArray(use.services)?use.services:[]) {
    if(!s || !services.has(s.service) || named.has(s.service))
      err("unknown or duplicate service use contract");
    else named.add(s.service);
    if(!valid(s.use_mode)||!valid(s.physical_effect)||!valid(s.capacity) ||
       !Array.isArray(s.demand_needs)||!s.demand_needs.length ||
       s.demand_needs.some(x=>!needs.has(x)))
      err("service missing physical capacity or demand");
    if(s.environmental_byproduct!==null &&
       (!items.has(s.environmental_byproduct) ||
       items.get(s.environmental_byproduct).lifecycle!=="waste"))
      err("service byproduct must be waste material");
  }
  if(named.size!==services.size)err("not all source services covered");
  for(const x of Array.isArray(use.substitutions)?use.substitutions:[]){
    if(!x||!valid(x.id)||!Array.isArray(x.satisfies)||!x.satisfies.length ||
       x.satisfies.some(y=>!needs.has(y)) ||
       !Array.isArray(x.items)||!x.items.length ||
       x.items.some(y=>!items.has(y)) ||
       !valid(x.policy))
      err("invalid substitution group");
  }
  for(const x of Array.isArray(use.variants)?use.variants:[])
    if(!x || !items.has(x.item_family) || x.new_catalog_item_required!==false ||
       !Array.isArray(x.tags) || !x.tags.length || x.tags.some(y=>!valid(y)))
      err("cosmetic variants must not multiply item codes");
  if(!use.event_contract ||
     use.event_contract.migration!=="later_society_system_requires_real_destination_transport_and_consented_household_custody" ||
     use.event_contract.service!=="real_staff_duration_equipment_and_consumables_with_original_world_commit")
     err("demand does not grant free service or automatic migration");
  return {ok:errors.length===0,errors,stats:{needs:needs.size,use_profiles:used.size,services:named.size}};
}

module.exports={validateCatalog,validateLegacyUnits,validateUseContracts};
if (require.main===module) {
  const input=process.argv[2] || path.join(__dirname,"..","data","industry","industry01_candidate_catalog.json");
  const catalog=JSON.parse(fs.readFileSync(input,"utf8"));
  const result=validateCatalog(catalog);
  const legacy=JSON.parse(fs.readFileSync(path.join(__dirname,"..","data","industry","industry01_legacy_units.json"),"utf8"));
  const original=validateLegacyUnits(catalog,legacy);
  const use=JSON.parse(fs.readFileSync(path.join(__dirname,"..","data","industry","industry01_use_contracts.json"),"utf8"));
  const useAudit=validateUseContracts(catalog,use);
  result.errors.push(...original.errors,...useAudit.errors);
  result.ok=result.ok&&original.ok&&useAudit.ok;
  if(!result.ok){for(const e of result.errors)console.error("INDUSTRY-01: "+e);process.exitCode=1;}
  else console.log("INDUSTRY-01 candidate validated:",JSON.stringify(result.stats));
}
