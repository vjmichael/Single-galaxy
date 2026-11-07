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

  const legacy=Array.isArray(catalog.legacy_bridge)?catalog.legacy_bridge:[];
  const legacyIds=new Set();
  for(const row of legacy){
    if(!row||!identifier(row.legacy_code)||!row.legacy_code.includes("/")){
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

  return {ok:errors.length===0,errors,stats:{items:items.size,recipes:recipeIds.size,services:serviceIds.size,legacy_mappings:legacyIds.size}};
}

module.exports={validateCatalog};
if (require.main===module) {
  const input=process.argv[2] || path.join(__dirname,"..","data","industry","industry01_candidate_catalog.json");
  const result=validateCatalog(JSON.parse(fs.readFileSync(input,"utf8")));
  if(!result.ok){for(const e of result.errors)console.error("INDUSTRY-01: "+e);process.exitCode=1;}
  else console.log("INDUSTRY-01 candidate validated:",JSON.stringify(result.stats));
}
