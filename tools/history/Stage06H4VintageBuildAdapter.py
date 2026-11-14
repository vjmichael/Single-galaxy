#!/usr/bin/env python3
"""S6-H4B pinned vintage native compiler/test adapter.

Arguments: original SHA-pinned Git checkout, separate shadow build source tree,
and original-to-shadow immutable SHA256 manifest location. The original Git
checkout remains unchanged. This is an isolated test-only source adapter,
never a replacement historical source or a saved-world version rewrite.
"""
import hashlib,json,pathlib,re,subprocess,sys
original=pathlib.Path(sys.argv[1]).resolve(strict=True)
target=pathlib.Path(sys.argv[2]).resolve()
manifest=pathlib.Path(sys.argv[3])
source_sha=subprocess.check_output(['git','-C',str(original),'rev-parse','HEAD'],text=True).strip()
eras={
    '23bad9be38936d61b8c29b5be5f3900bec87fa6d':'pre-v109-original-v92',
    '44619e9b186f99fa836ae9494a88137f3f2bf3f5':'v111-planet-housing',
    'd47e5cdb0532b615ffc99ffce23358a92ab05724':'v118-family-custody',
    'eb448b6735a6e90b20480b3ac5065e06f30ae1ad':'v128-planetary-resource',
    '8ccb4a37b76e6e1510ee3fa1751fc23a9cdbc685':'v135-medical-atmosphere'
}
if source_sha not in eras:raise RuntimeError('unrecognized exact historical source SHA')
era=eras[source_sha]
def pristine():
    return not subprocess.check_output(['git','-C',str(original),'status',
                                        '--porcelain','--untracked-files=no'],text=True).strip()
if not pristine():raise RuntimeError('original historical source was modified')
target.mkdir(parents=True,exist_ok=False)
changes=[]
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def replace_one(text,from_text,to_text,label,edits):
    count=text.count(from_text)
    if count!=1:raise RuntimeError(f'{era} {label}: expected exactly one historical pattern; found {count}')
    edits.append(label)
    return text.replace(from_text,to_text,1)
def split_auto(text,edits):
    output=[]; fixed=0
    for line in text.splitlines(keepends=True):
        found=re.match(r'^([ \t]*)const auto (.+);([ \t]*(?://[^\n]*)?)(\n?)$',line)
        if not found:
            output.append(line);continue
        indent,fields,suffix,newline=found.groups()
        pieces=[];start=0;depth=0;quote=None;escaped=False
        for i,ch in enumerate(fields):
            if quote:
                if escaped:escaped=False
                elif ch=='\\':escaped=True
                elif ch==quote:quote=None
            elif ch in ('"',"'"):quote=ch
            elif ch in '([{':depth+=1
            elif ch in ')]}':depth-=1
            elif ch==',' and depth==0:
                pieces.append(fields[start:i].strip());start=i+1
        pieces.append(fields[start:].strip())
        if len(pieces)<=1 or not all(re.match(r'^[a-zA-Z_]\w*\s*=',p) for p in pieces):
            output.append(line);continue
        output.extend(indent+'const auto '+p+';'+(suffix if j==len(pieces)-1 else '')+newline
                      for j,p in enumerate(pieces))
        fixed+=1
    merged=''.join(output)
    multiline=re.compile(
        r'(?m)^([ \t]*)const auto ((?:[A-Za-z_]\w*=(?:u64|i64)\(f\[\d+\]\),\s*)+[A-Za-z_]\w*=(?:u64|i64)\(f\[\d+\]\));'
    )
    def split_snapshot_fields(found):
        indent,fields=found.group(1),found.group(2)
        declarations=re.split(r',\s*(?=[A-Za-z_]\w*=)',fields)
        return '\n'.join(indent+'const auto '+x+';' for x in declarations)
    merged,split_lines=multiline.subn(split_snapshot_fields,merged)
    if fixed:edits.append(f'split {fixed} same-line auto-deduced declarations')
    if split_lines:edits.append(f'split {split_lines} original multiline typed snapshot declarations')
    return merged
def patched(path):
    text=(original/path).read_text();edits=[]
    if path=='CMakeLists.txt' and era in ('v111-planet-housing','v118-family-custody'):
        for wrong in ('// WO3-A: proposal-only colony construction planning and original-world actual',
                      '// facility, census, utility, immutable SQLite copies and material conservation.'):
            text=replace_one(text,wrong,'#'+wrong[2:],'CMake vintage comment grammar',edits)
    if path=='src/Simulation.cpp':
        if era in ('v111-planet-housing','v118-family-custody'):
            text=replace_one(text,'if(!ship || !c || !l || frame==ship_frames_.end() ||',
                             'if(!ship || c==colony_ships_.end() || l==life_support_.end() || frame==ship_frames_.end() ||',
                             'typed iterator end comparison',edits)
            text=replace_one(text,'plan.destination_frame.anchor_body,',
                             'plan.destination_frame.anchor_body_id,',
                             'actual LocalSpaceFrame member',edits)
            text=replace_one(text,'namespace galaxy {\nnamespace {\nconstexpr int kPlacementBound',
                             'namespace galaxy {\nnamespace {\nEntityId shuttle_transit_custody_id(const PlanetarySiteKey& key);\nconstexpr int kPlacementBound',
                             'native shuttle helper forward declaration',edits)
            if era=='v111-planet-housing':
                text=replace_one(text,
                    'if(dependent.guardian_id==npc_id ||\n           dependent.co_guardian_id==npc_id)return false;',
                    'if(dependent.guardian_id==npc_id)return false;',
                    'v111 only has guardian_id; no co-guardian exists in this snapshot schema',edits)
            text=split_auto(text,edits)
        if era in ('v128-planetary-resource','v135-medical-atmosphere'):
            marker='            const bool genuine_station_visitor=[&](){\n'
            end_marker='            }();\n'
            if text.count(marker)!=1:raise RuntimeError('unrecognized visitor predicate')
            start=text.index(marker);end=text.find(end_marker,start)
            if end<0:raise RuntimeError('unterminated original visitor predicate')
            end+=len(end_marker)
            pred=text[start:end]
            text=text[:start]+text[end:]
            anchor='            if(!result.colony_ships_.count(n.home_ship) ||\n'
            text=replace_one(text,anchor,pred+'\n'+anchor,
                             'move original visitor predicate outside Boolean expression',edits)
        if era=='v135-medical-atmosphere':
            text=replace_one(text,
                '        for(const auto& body:bodies)\n            out<<"celestial|"',
                '        for(const auto& body:bodies) {\n            out<<"celestial|"',
                'bracket mass/atmosphere serializer per real celestial body',edits)
            text=replace_one(text,
                "            out<<'\\n';\n    if(snapshot_version_>=127) {",
                "            out<<'\\n';\n        }\n    if(snapshot_version_>=127) {",
                'close per-body serialization loop',edits)
    if path=='src/DockWorkforce.cpp' and era in ('v128-planetary-resource','v135-medical-atmosphere'):
        text=split_auto(text,edits)
    if path=='src/CyberIncidentJournal.cpp' and era in ('v111-planet-housing','v118-family-custody'):
        text=split_auto(text,edits)
    if path=='tests/SHIPSTATUS02_AuthoritativePersistenceTests.cpp' and era=='pre-v109-original-v92':
        # First repair original pre-v109 genesis ordering, then
        # register an authentic native frame on that same ship.
        text=replace_one(text,
            ('            genesis.provision_starter_exploration_scanner(ship);\n'
             '            genesis.provision_starter_probe_kit(ship);\n'
             '            genesis.enable_individual_workforce();'),
            ('            genesis.enable_individual_workforce();\n'
             '            genesis.provision_starter_exploration_scanner(ship);\n'
             '            genesis.provision_starter_probe_kit(ship);'),
            'move original workforce genesis before versioned scanner/probe kit',edits)
        text=replace_one(text,
            '            genesis.provision_starter_probe_kit(ship);',
            ('            genesis.provision_starter_probe_kit(ship);\n'
             '            genesis.ensure_ship_frame_registry();'),
            'register original ship frame before v100 status codec',edits)
    if path=='tests/WO3PlanetaryUtilityTests.cpp' and era in ('v111-planet-housing','v118-family-custody'):
        text=replace_one(text,'    seed.enable_individual_workforce();','',
            'remove late individual-workforce initialization',edits)
        text=replace_one(text,
            '    seed.provision_starter_exploration_scanner(ship);',
            ('    seed.enable_individual_workforce();\n'
             '    seed.provision_starter_exploration_scanner(ship);'),
            'original crew identity before versioned scanner',edits)
        if era=='v118-family-custody':
            # The native trade-training API requires a real Habitat worker
            # assigned at the powered site, not an available ship crewmember.
            # Use accepted original AssignHabitatResidents commands; retain
            # every original training, certification and crew-return check.
            text=replace_one(text,
                '    act(w,ship,291,Action::AssignHabitatResidents,orbital,-2);',
                ('    act(w,ship,291,Action::AssignHabitatResidents,orbital,-2);\n'
                 '    act(w,ship,299,Action::AssignHabitatResidents,orbital,4);'),
                'native habitat placement for independently trained workers',edits)
            text=replace_one(text,
                ('n.location==ship && n.role==NpcRole::Ship &&\n'
                 '           w.world().npc_available_for_duty(id)) {'),
                ('n.location==orbital && n.role==NpcRole::Habitat &&\n'
                 '           w.world().npc_available_for_duty(id)) {'),
                'select actual site-resident partner for original Shape training',edits)
            text=replace_one(text,
                ('n.home_ship==ship && n.location==ship &&\n'
                 '           n.role==NpcRole::Ship && w.world().npc_available_for_duty(id)) {'),
                ('n.home_ship==ship && n.location==orbital &&\n'
                 '           n.role==NpcRole::Habitat && w.world().npc_available_for_duty(id)) {'),
                'select actual site-resident visitor for original Shape training',edits)
            text=replace_one(text,
                ('    CHECK(w.world().workshop_trade_certified(\n'
                 '        trained_visitor,WorkshopTrade::Shape));'),
                ('    CHECK(w.world().workshop_trade_certified(\n'
                 '        trained_visitor,WorkshopTrade::Shape));\n'
                 '    act(w,ship,324,Action::AssignHabitatResidents,orbital,-4);'),
                'return certified original workers to ship before actual crew selection',edits)
    if path=='tests/Stage06FinitePlanetaryGenesisTests.cpp' and era in ('v128-planetary-resource','v135-medical-atmosphere'):
        wrong=('        CHECK(!seed.scan_planetary_region_source(\n'
               '            7,ship,body,first.first,second.second==first.second?\n'
               '            second.second+1:second.second));')
        correct=('        // One canonical cell that is neither of the two real funded cells.\n'
                 '        std::uint16_t absent=0;\n'
                 '        while(absent==first.first*72+first.second ||\n'
                 '              absent==second.first*72+second.second)++absent;\n'
                 '        CHECK(absent<36*72);\n'
                 '        CHECK(!seed.scan_planetary_region_source(\n'
                 '            7,ship,body,absent/72,absent%72));')
        text=replace_one(text,wrong,correct,
            'negative planetary source probe uses truly unallocated canonical cell',edits)
        text=replace_one(text,
            '        seed.ensure_physical_celestial_systems();',
            ('        seed.enable_native_galactic_physics({{1,galactic::Point{{0,0},{0,0},{0,0}}}});\n'
             '        seed.ensure_physical_celestial_systems();'),
            'source-derived galactic v120 pose before v127 terrain genesis',edits)
        text=replace_one(text,
            '        legacy.ensure_physical_celestial_systems();',
            ('        legacy.enable_native_galactic_physics({{1,galactic::Point{{0,0},{0,0},{0,0}}}});\n'
             '        legacy.ensure_physical_celestial_systems();'),
            'historical original galactic source before journaled v127 upgrade',edits)
        text=replace_one(text,
            '        world.steps(2);',
            ('        Command native{};\n'
             '        native.id=99127001;native.sequence=native.id;\n'
             '        native.execute_tick=world.world().tick()+1;\n'
             '        native.issuer=7;native.actor=ship;native.action=Action::Thrust;\n'
             '        native.thrust_x=1;native.thrust_y=0;\n'
             '        CHECK(world.submit(native));\n'
             '        world.step();\n'
             '        CHECK(world.world().events().back().command_id==native.id);\n'
             '        CHECK(world.world().events().back().result==Outcome::Applied);\n'
             '        world.steps(2);'),
            'original authorized physical thrust and executed receipt before capture',edits)
        # Supplementary ORIGINAL vintage engine scenario, distinct
        # from the unmodified original negative checks. The snapshot
        # version changes only through real source migration APIs.
        version='135' if era=='v135-medical-atmosphere' else '128'
        promotion=('certified.enable_physical_planetary_environment();\n'
                   '            CHECK(certified.s6h4_attest_original_atmosphere());'
                   if version=='135' else
                   'certified.enable_physical_planetary_environment();')
        proof=(
            '        {\n'
            '            Simulation certified=seed;\n'
            '            '+promotion+'\n'
            '            CHECK(certified.canonical_state().rfind("galaxy-kernel-v'+version+'|",0)==0);\n'
            '            const auto extra_db=db+"-native-v'+version+'.sqlite";\n'
            '            cleanup(extra_db);\n'
            '            {\n'
            '                PersistentWorld original(extra_db,std::move(certified));\n'
            '                Command action{};action.id=99128002;action.sequence=action.id;\n'
            '                action.execute_tick=original.world().tick()+1;\n'
            '                action.issuer=7;action.actor=ship;action.action=Action::Thrust;\n'
            '                action.thrust_x=1;\n'
            '                CHECK(original.submit(action));original.step();\n'
            '                CHECK(original.world().events().back().command_id==action.id);\n'
            '                CHECK(original.world().events().back().result==Outcome::Applied);\n'
            '                CHECK(original.world().canonical_state().rfind("galaxy-kernel-v'+version+'|",0)==0);\n'
            '                CHECK(Simulation::from_snapshot(original.world().canonical_state()).canonical_state()==\n'
            '                      original.world().canonical_state());\n'
            '            }\n'
            '            {\n'
            '                PersistentWorld reopened(extra_db,Simulation(777,1));\n'
            '                CHECK(reopened.world().has_command_id(99128002));\n'
            '                CHECK(reopened.world().canonical_state().rfind("galaxy-kernel-v'+version+'|",0)==0);\n'
            '            }\n'
            '            cleanup(extra_db);\n'
            '        }\n'
            '        initial_world=seed.canonical_state();'
        )
        text=replace_one(text,'        initial_world=seed.canonical_state();',
            proof,
            'native source v'+version+' command, receipt, immutable resources and restart',edits)
    if path=='tests/Stage06PhysicalEnvironmentTests.cpp' and era=='v135-medical-atmosphere':
        text=replace_one(text,
            '    sim.steps(80);',
            '    const auto original_tick_zero=sim;\n    sim.steps(80);',
            'preserve real tick-zero genesis for SQLite world writer',edits)
        text=replace_one(text,
            '        PersistentWorld world(db,std::move(sim));',
            ('        PersistentWorld world(db,std::move(original_tick_zero));\n'
             '        world.steps(80);\n'
             '        CHECK(world.world().canonical_state()==canonical);'),
            'replay original 80 ticks through canonical SQLite writer',edits)
        text=replace_one(text,
            '    sim.ensure_planetary_genesis_manifest();',
            ('    sim.enable_native_galactic_physics({{1,galactic::Point{{0,0},{0,0},{0,0}}}});\n'
             '    sim.ensure_planetary_genesis_manifest();'),
            'seed original v120 physical pose before v128 process codec',edits)
        text=replace_one(text,
            '    historical.ensure_planetary_genesis_manifest();',
            ('    historical.enable_native_galactic_physics({{1,galactic::Point{{0,0},{0,0},{0,0}}}});\n'
             '    historical.ensure_planetary_genesis_manifest();'),
            'explicit native v120 identity for historical genesis fixture',edits)
    if path=='tests/Stage06EcologyIndustryTests.cpp' and era=='v135-medical-atmosphere':
        text=replace_one(text,
            '    seed.ensure_physical_celestial_systems();',
            ('    seed.enable_native_galactic_physics({{1,galactic::Point{{0,0},{0,0},{0,0}}}});\n'
             '    seed.ensure_physical_celestial_systems();'),
            'authoritative world pose before ecological snapshot upgrade',edits)
        text=replace_one(text,
            '    (void)stage6::provision_first_planet_seed(new_world);',
            ('    new_world.enable_native_galactic_physics({{1,galactic::Point{{0,0},{0,0},{0,0}}}});\n'
             '    (void)stage6::provision_first_planet_seed(new_world);'),
            'galactic pose for distinct regional ecology producer',edits)
    if path=='include/galaxy/Simulation.hpp' and era=='v135-medical-atmosphere':
        # Only this SHA-pinned historical test binary receives the
        # observer entry point. The real source's private validator
        # executes unchanged; no state/header/atmosphere is invented.
        text=replace_one(text,
            ('private:\n'
             '    // S6-F5-C: original deterministic generated mass is sealed into v134.'),
            ('public:\n'
             '    // S6-H4 shadow-only observer of this original sealed generator.\n'
             '    bool s6h4_attest_original_atmosphere() {\n'
             '        return establish_original_surface_atmosphere();\n'
             '    }\n'
             'private:\n'
             '    // S6-F5-C: original deterministic generated mass is sealed into v134.'),
            'test-only native access to original private v135 atmosphere audit',edits)
    if path=='include/galaxy/PlanetaryContinuousSurface.hpp' and era=='v135-medical-atmosphere':
        text=replace_one(text,
            '    const auto lat=lat_diff/scale,lon=lon_projected/scale,\n               limit=allowed/scale;',
            '    const auto lat=lat_diff/scale;\n    const auto lon=lon_projected/scale;\n    const auto limit=allowed/scale;',
            'separate distinct unsigned integer deduction types',edits)
    return text,edits
for entry in original.iterdir():
    if entry.name not in ('src','include','tests','CMakeLists.txt'):
        (target/entry.name).symlink_to(entry.resolve(),target_is_directory=entry.is_dir())
(target/'src').mkdir();(target/'include').mkdir();(target/'include'/'galaxy').mkdir()
(target/'tests').mkdir()
candidates={'CMakeLists.txt','src/Simulation.cpp','src/DockWorkforce.cpp',
            'src/CyberIncidentJournal.cpp',
            'include/galaxy/PlanetaryContinuousSurface.hpp',
            'include/galaxy/Simulation.hpp',
            'tests/Stage06FinitePlanetaryGenesisTests.cpp',
            'tests/Stage06EcologyIndustryTests.cpp',
            'tests/Stage06PhysicalEnvironmentTests.cpp',
            'tests/SHIPSTATUS02_AuthoritativePersistenceTests.cpp',
            'tests/WO3PlanetaryUtilityTests.cpp'}
for parent in ('src','include','tests'):
    for entry in (original/parent).iterdir():
        if parent=='include' and entry.name=='galaxy':continue
        if (parent+'/'+entry.name) in candidates:continue
        (target/parent/entry.name).symlink_to(entry.resolve(),target_is_directory=entry.is_dir())
for entry in (original/'include'/'galaxy').iterdir():
    if ('include/galaxy/'+entry.name) in candidates:continue
    (target/'include'/'galaxy'/entry.name).symlink_to(entry.resolve(),target_is_directory=entry.is_dir())
for rel in sorted(candidates):
    before=original/rel
    if not before.is_file():continue
    after=target/rel
    result,edits=patched(rel)
    if edits:
        after.write_text(result)
        changes.append(dict(path=rel,original_sha256=digest(before),
                            shadow_sha256=digest(after),repair_labels=edits))
    else:after.symlink_to(before.resolve())
if not pristine():raise RuntimeError('compiler adapter modified original Git checkout')
report=dict(schema='SG-S6H4-VINTAGE-SHADOW-COMPILER-01',historical_commit=source_sha,
            era=era,original_git_unchanged=True,
            adapter_source_files=changes,compiled_using_adapter=bool(changes),
            original_untouched_binary=False if changes else True,
            no_synthetic_snapshot_versions=True,
            no_data_schema_rewrite=True,version_acceptance_requires_original_ctests=True)
manifest.write_text(json.dumps(report,sort_keys=True,indent=2)+'\n')
print(json.dumps(report,sort_keys=True))
