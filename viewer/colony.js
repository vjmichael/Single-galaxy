/* B7.0b authenticated local colony telemetry viewer; no simulation authority here. */
(function(root,factory){'use strict';const api=factory();if(typeof module!=='undefined'&&module.exports)module.exports=api;root.GalaxyColonyContract=api;})(typeof window!=='undefined'?window:globalThis,function(){
  'use strict';
  const names=['Carbonaceous','Silicate','Metal-rich'];
  const goods=new Set(['structural_alloy','aluminum','ceramic','glass','water','sulfur','carbon_feed']);
  const minerals=new Set(['serpentine','saponite','magnetite','pyrrhotite','carbonates','olivine','pyroxene','nickel_iron','other','plagioclase','troilite']);
  function numeric(x,max=1e12){if(!/^(0|[1-9][0-9]*)$/.test(x))throw Error('Noncanonical number');const n=Number(x);if(!Number.isSafeInteger(n)||n>max)throw Error('Numeric overflow');return n;}
  function decode(raw){
    if(typeof raw!=='string'||raw.length>180||!/^COLONY\|1\|/.test(raw)||!/^[A-Za-z0-9_|-]+$/.test(raw))throw Error('Invalid colony telemetry');
    const v=raw.split('|'),tick=numeric(v[2],1200),t=v[3];let e={tick,type:t};
    if(t==='LOCAL_FRAME'){
      if(v.length!==6)throw Error('Malformed local frame');
      return {...e,system:numeric(v[4],4294967295),
        anchor:numeric(v[5],Number.MAX_SAFE_INTEGER)};
    }
    if(t==='FRAME_TRANSIT_STARTED'||t==='FRAME_TRANSIT_ARRIVED'){
      if(v.length!==6)throw Error('Malformed frame transition');
      return {...e,request:numeric(v[4],Number.MAX_SAFE_INTEGER),
        body:numeric(v[5],Number.MAX_SAFE_INTEGER)};
    }
    if(t==='CELESTIAL_HEADER'){
      if(v.length!==8)throw Error('Malformed celestial catalogue');
      const architecture=numeric(v[4],3),morphology=numeric(v[5],2),
        stars=numeric(v[6],3),bands=numeric(v[7],9);
      if(!stars||!bands)throw Error('Invalid celestial catalogue');
      return {...e,architecture,morphology,stars,bands};
    }
    if(t==='CELESTIAL_BAND'){
      if(v.length!==8)throw Error('Malformed orbital band');
      const ordinal=numeric(v[4],9),radius=numeric(v[5],100000),
        eccentricity=numeric(v[6],999),zone=numeric(v[7],4);
      if(!ordinal||!radius)throw Error('Invalid orbital band');
      return {...e,ordinal,radius,eccentricity,zone};
    }
    if(t==='CELESTIAL_HISTORY'){
      if(v.length!==6)throw Error('Malformed celestial survey history');
      const first=numeric(v[4],1201),last=numeric(v[5],1201);
      if(!first||last<first)throw Error('Invalid celestial survey history');
      return {...e,first,last};
    }
    if(t==='CELESTIAL_BODY'){
      if(v.length!==12)throw Error('Malformed persisted celestial body');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER),
        parent=numeric(v[5],Number.MAX_SAFE_INTEGER),kind=numeric(v[6],3),
        orbit=numeric(v[7],300000),radius=numeric(v[11],Number.MAX_SAFE_INTEGER);
      const coord=value=>{
        if(!/^(?:0|-?[1-9][0-9]*)$/.test(value))throw Error('Invalid cosmic coordinate');
        const n=Number(value);
        if(!Number.isSafeInteger(n)||Math.abs(n)>50000000000000000)
          throw Error('Cosmic coordinate out of range');
        return n;
      };
      const x=coord(v[8]),y=coord(v[9]),z=coord(v[10]);
      if(!id||!radius||(kind===0&&parent!==0)||
         (kind!==0&&parent===0))throw Error('Invalid celestial parent/body');
      return {...e,id,parent,kind,orbit,x,y,z,radius};
    }
    if(t==='PLANET_CREW_AVAILABLE'){
      if(v.length!==5)throw Error('Malformed available surface NPC');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER);
      if(!id)throw Error('Invalid surface NPC');
      return {...e,id};
    }
    if(t==='PLANET_CREW_DONE'){
      if(v.length!==4)throw Error('Malformed surface crew terminator');
      return e;
    }
    if(t==='PLANET_POLICY_COMMITTED'){
      if(v.length!==9)throw Error('Malformed planetary policy receipt');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER),
       latitude=numeric(v[5],35),longitude=numeric(v[6],71),
       policy=numeric(v[7],4),generation=numeric(v[8],Number.MAX_SAFE_INTEGER);
      if(!id || !generation)throw Error('Invalid planetary policy receipt');
      return {...e,id,latitude,longitude,policy,generation};
    }
    if(t==='PLANET_SHUTTLE_STATUS'){
      if(v.length!==17)throw Error('Malformed shuttle status');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER),latitude=numeric(v[5],35),
       longitude=numeric(v[6],71),present=numeric(v[7],1),phase=numeric(v[8],3),
       pilot=numeric(v[9],Number.MAX_SAFE_INTEGER),water=numeric(v[10],1000),
       food=numeric(v[11],1000),arrival=numeric(v[12],Number.MAX_SAFE_INTEGER),
       request=numeric(v[13],Number.MAX_SAFE_INTEGER),
       descent=numeric(v[14],Number.MAX_SAFE_INTEGER),
       ascent=numeric(v[15],Number.MAX_SAFE_INTEGER),
       generation=numeric(v[16],Number.MAX_SAFE_INTEGER);
      if(!id||!generation||water+food>1000||
         (!present&&(phase||pilot||water||food||arrival||request||descent||ascent))||
         (present&&phase>0&&!pilot)||
         ((phase===1||phase===3)!==(request>0))||
         ((phase===1||phase===3)!==(arrival>0)))
        throw Error('Invalid authoritative shuttle state');
      return {...e,id,latitude,longitude,present:!!present,phase,pilot,
        water,food,arrival,request,descent,ascent,generation};
    }
    if(t==='PLANET_SHUTTLE_COMMITTED'){
      if(v.length!==9)throw Error('Malformed shuttle commit');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER),latitude=numeric(v[5],35),
       longitude=numeric(v[6],71),operation=v[7],
       generation=numeric(v[8],Number.MAX_SAFE_INTEGER);
      if(!id||!generation||!new Set(['BUILD','PILOT','LOAD','UNLOAD','DESCEND','ASCEND']).has(operation))
        throw Error('Invalid shuttle operation receipt');
      return {...e,id,latitude,longitude,operation,generation};
    }
    if(t==='PLANET_SITE_STATUS'){
      if(v.length!==22)throw Error('Malformed planetary spaceport status');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER),
       latitude=numeric(v[5],35),longitude=numeric(v[6],71),
       phase=numeric(v[7],3),progress=numeric(v[8],240),
       required=numeric(v[9],240),workers=numeric(v[10],2),
       food=numeric(v[11],1000000),water=numeric(v[12],1000000),
       oxygen=numeric(v[13],1000000),npc1=numeric(v[14],Number.MAX_SAFE_INTEGER),
       npc2=numeric(v[15],Number.MAX_SAFE_INTEGER),
       policy=numeric(v[16],4),condition=numeric(v[17],100),
       cycles=numeric(v[18],Number.MAX_SAFE_INTEGER),
       stalled=numeric(v[19],Number.MAX_SAFE_INTEGER),
       extracted=numeric(v[20],Number.MAX_SAFE_INTEGER),
       repairs=numeric(v[21],Number.MAX_SAFE_INTEGER);
      if(!id || phase<1 || required!==240 ||
         (workers!==2 && !(workers===0 && phase===3 && npc1===0 && npc2===0)) ||
          (npc1===npc2 && npc1!==0) ||
          (phase===3 && progress!==240))
        throw Error('Invalid surface spaceport status');
      return {...e,id,latitude,longitude,phase,progress,required,workers,
              food,water,oxygen,npc1,npc2,policy,condition,cycles,stalled,extracted,repairs};
    }
    if(t==='PLANET_OPERATION_COMMITTED'){
      if(v.length!==9)throw Error('Malformed planetary operation receipt');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER);
      const latitude=numeric(v[5],35),longitude=numeric(v[6],71);
      const action=v[7],generation=numeric(v[8],Number.MAX_SAFE_INTEGER);
      const permitted=new Set(['PLANET_BUILD','PLANET_EQUIP','PLANET_SUPPLY',
         'PLANET_GROW','PLANET_EXTRACT','PLANET_RETRIEVE','PLANET_SUPPORT']);
      if(!id||!permitted.has(action)||!generation)
        throw Error('Invalid planetary operation receipt');
      return {...e,id,latitude,longitude,action,generation};
    }
    if(t==='PLANET_REGION'){
      if(v.length!==15)throw Error('Malformed native planet region');
      const signed=x=>{if(!/^(0|-?[1-9][0-9]*)$/.test(x))throw Error('Invalid signed region value');
        const n=Number(x);if(!Number.isSafeInteger(n))throw Error('Region integer overflow');return n;};
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER);
      const latitude=numeric(v[5],35),longitude=numeric(v[6],71);
      const elevation=signed(v[7]),ocean=numeric(v[8],1),
            terrain=numeric(v[9],10),watercourse=numeric(v[10],3),
            runoff=numeric(v[11],Number.MAX_SAFE_INTEGER),
            downLatitude=signed(v[12]),downLongitude=signed(v[13]),
            revision=numeric(v[14],100000);
      if(!id || !revision || (downLatitude===-1)!==(downLongitude===-1) ||
         downLatitude>35 || downLongitude>71 || downLatitude< -1 || downLongitude< -1)
        throw Error('Invalid planetary region geography');
      return {...e,id,latitude,longitude,elevation,ocean:!!ocean,
        terrain,watercourse,runoff,downLatitude,downLongitude,revision};
    }
    if(t==='OASIS_REGION'){
      if(v.length!==8)throw Error('Malformed finite oasis region waypoint');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER),
        latitude=numeric(v[5],35),longitude=numeric(v[6],71),
        revision=numeric(v[7],100000);
      if(!id||!revision)throw Error('Invalid finite oasis region waypoint');
      return {...e,id,latitude,longitude,revision};
    }
    if(t==='OASIS_PROSPECT'){
      if(v.length!==9)throw Error('Malformed oasis prospect');
      const id=numeric(v[4],Number.MAX_SAFE_INTEGER);
      const water=numeric(v[5],1),energy=numeric(v[6],1),
            nutrients=numeric(v[7],1),shelter=numeric(v[8],1);
      if(!id||!water||!energy||!nutrients||!shelter)
        throw Error('Invalid oasis prerequisite');
      return {...e,id,water,energy,nutrients,shelter};
    }
    if(t==='CELESTIAL_DONE'){
      if(v.length!==5)throw Error('Malformed orbital catalogue count');
      return {...e,count:numeric(v[4],9)};
    }
    if(t==='ENVIRONMENT_STATUS'){
      if(v.length!==8)throw Error('Malformed environment survey');
      const system=numeric(v[4],100000000),magnetized=numeric(v[5],1),
        dust=numeric(v[6],1),speed=numeric(v[7],200000);
      if(!system)throw Error('Invalid environment system');
      return {...e,system,magnetized:!!magnetized,dust:!!dust,speed};
    }
    if(t==='SIGNATURE_RESET'){
      if(v.length!==4)throw Error('Malformed signature reset');
      return e;
    }
    if(t==='SIGNATURE_DONE'){
      if(v.length!==5)throw Error('Malformed signature count');
      return {...e,count:numeric(v[4],128)};
    }
    if(t==='SIGNATURE_CONTACT'){
      if(v.length!==8)throw Error('Malformed coarse signature');
      const coord=x=>{if(!/^(?:0|-?[1-9][0-9]*)$/.test(x))throw Error('Invalid coarse coordinate');
        const n=Number(x);if(!Number.isSafeInteger(n)||Math.abs(n)>1000000000)throw Error('Signature coordinate overflow');return n;};
      const id=numeric(v[4],100000000),x=coord(v[5]),y=coord(v[6]);
      const uncertainty=numeric(v[7],500000000);
      if(!id||!uncertainty)throw Error('Invalid signature');
      return {...e,id,x,y,uncertainty};
    }
    if(t==='PROBE_RESET'){
      if(v.length!==4)throw Error('Malformed probe roster reset');
      return e;
    }
    if(t==='PROBE_REMOVED'){
      if(v.length!==5)throw Error('Malformed recovered probe');
      return {...e,id:numeric(v[4],1000000000000000)};
    }
    if(t==='PROBE_STOCK'){
      if(v.length!==6)throw Error('Malformed physical probe inventory');
      return {...e,carried:numeric(v[4],8),launcher:!!numeric(v[5],1)};
    }
    if(t==='SURVEY_PROBE'){
      if(v.length!==8 && v.length!==12 && v.length!==15)throw Error('Malformed deployed survey probe');
      const id=numeric(v[4],1000000000000000),system=numeric(v[5],100000000);
      const coord=value=>{if(!/^(?:0|-?[1-9][0-9]*)$/.test(value))throw Error('Bad probe coordinate');
        const n=Number(value);if(!Number.isSafeInteger(n)||Math.abs(n)>1000000000)throw Error('Probe coordinate out of range');return n;};
      const x=coord(v[6]),y=coord(v[7]);
      if(!id||!system)throw Error('Invalid deployed probe identity');
      if(v.length>=12){
        const phase=numeric(v[8],3),target=numeric(v[9],100000000);
        const sample=numeric(v[10],0),taxonomy=numeric(v[11],3);
        if((phase===0 && (target||sample||taxonomy)) ||
           (phase!==0 && !target) || sample!==0)throw Error('Invalid geological drone state');
        const focus=v.length===15?numeric(v[12],100000000):0;
        const fx=v.length===15?coord(v[13]):0,fy=v.length===15?coord(v[14]):0;
        if(focus && phase)throw Error('Invalid focused geological drone');
        if(!focus && (fx||fy))throw Error('Unowned focus waypoint');
        return {...e,id,system,x,y,phase,target,sample,taxonomy,focus,fx,fy};
      }
      return {...e,id,system,x,y,phase:0,target:0,sample:0,taxonomy:0,focus:0,fx:0,fy:0};
    }
    if(t==='FITTING'){
      if(v.length!==9)throw Error('Malformed ship fitting telemetry');
      const installed=numeric(v[4],1),hardpoints=numeric(v[5],2);
      const support=numeric(v[6],2),subsystems=numeric(v[7],2),rigging=numeric(v[8],1);
      if(hardpoints!==2||support!==2||subsystems!==2||rigging!==1)
        throw Error('Unknown ship hull fitting capacity');
      return {...e,installed:!!installed,hardpoints,support,subsystems,rigging};
    }
    if(t==='PROXIMITY'){
      if(v.length!==6)throw Error('Malformed proximity telemetry');
      const id=numeric(v[4],100000000),distance=numeric(v[5],5000);
      if(id===0 && distance!==0)throw Error('Invalid safety contact');
      return {...e,id,distance};
    }
    if(t==='DISCOVERY'){
      if(v.length!==10 && v.length!==11)throw Error('Malformed discovery record');
      const site=numeric(v[4],100000000),system=numeric(v[5],100000000);
      const coord=x=>{if(!/^(?:0|-?[1-9][0-9]*)$/.test(x))throw Error('Invalid signed discovery coordinate');const n=Number(x);if(!Number.isSafeInteger(n)||Math.abs(n)>1000000000)throw Error('Discovery coordinate overflow');return n;};
      const x=coord(v[6]),y=coord(v[7]);
      const firstTick=numeric(v[8],Number.MAX_SAFE_INTEGER),geological=numeric(v[9],1);
      if(!site||!system||!firstTick||firstTick>e.tick)throw Error('Invalid discovery metadata');
      if(v.length===11){
        const surveyTaxonomy=numeric(v[10],3);
        if(surveyTaxonomy && !geological)throw Error('Non-geological survey classification');
        return {...e,site,system,x,y,firstTick,geological:!!geological,surveyTaxonomy};
      }
      return {...e,site,system,x,y,firstTick,geological:!!geological};
    }
    if(t==='SALVAGE_WRECK'){
      if(v.length!==6)throw Error('Malformed salvage wreck');
      const id=numeric(v[4],100000000),cargo=numeric(v[5],1000000);
      if(!id)throw Error('Invalid salvage ID');
      return {...e,id,cargo};
    }
    if(t==='COMBAT_SHIP'){
      if(v.length!==6)throw Error('Malformed combat ship');
      return {...e,hull:numeric(v[4],100),charges:numeric(v[5],12)};
    }
    if(t==='COMBAT_TARGET'){
      if(v.length!==6)throw Error('Malformed combat target');
      return {...e,id:numeric(v[4],100000000),hull:numeric(v[5],100)};
    }
    if(t==='HEADING'){
      if(v.length!==5)throw Error('Bad ship heading');
      return {...e,heading:numeric(v[4],359)};
    }
    if(t==='SAFETY'){
      if(v.length!==10||!v.slice(4).every(x=>/^\d+$/.test(x)))throw Error('Malformed safety report');
      const [site,risk,food,water,oxygen,pop]=v.slice(4).map(Number);
      if(!site||risk>3||risk<0)throw Error('Invalid safety report');
      return {...e,site:numeric(v[4],1e8),risk:numeric(v[5],3),
        food:numeric(v[6],1e10),water:numeric(v[7],1e10),
        oxygen:numeric(v[8],1e10),pop:numeric(v[9],1e8)};
    }
    if(t==='SAFETY_CHECK'||t==='SAFETY_ORDER'){
      if((t==='SAFETY_CHECK'&&v.length!==6)||(t==='SAFETY_ORDER'&&v.length!==7))throw Error('Malformed safety order');
      if(t==='SAFETY_CHECK')return {...e,site:numeric(v[4],1e8),risk:numeric(v[5],3)};
      if(!['REFUSED','PREFLIGHT_ONLY'].includes(v[5]))throw Error('Invalid safety decision');
      return {...e,site:numeric(v[4],1e8),decision:v[5],risk:numeric(v[6],3)};
    }
    if(t==='NEW_COLONY'||t==='RESUMED'){if(v.length!==4)throw Error('Malformed colony mode');}
    else if(t==='STATUS'){
      if(v.length!==10)throw Error('Bad status');
      const n=v.slice(4).map(x=>numeric(x,1e12));
      Object.assign(e,{crew:n[0],raw:n[1],refined:n[2],modules:n[3],outposts:n[4],total:n[5]});
      if(e.crew<1||e.crew>1000||e.modules*2+e.refined+e.raw>1e9)throw Error('Invalid inventory');
    }else if(t==='STOCK_CLEAR'){
      if(v.length!==4)throw Error('Bad stock reset');
    }else if(t==='STOCK'){
      if(v[4]==='TAILINGS'){
        if(v.length!==6)throw Error('Bad tailings');
        e.category='TAILINGS';e.kg=numeric(v[5],1e12);
      }else{
        if(v.length!==7)throw Error('Bad stock inventory');
        e.category=v[4];e.name=v[5];e.kg=numeric(v[6],1e12);
        if(!e.kg || !(e.category==='MINERAL'&&minerals.has(e.name) || e.category==='GOOD'&&goods.has(e.name)))throw Error('Bad material stock');
      }
    }else if(t==='LIFE'){
      if(v.length!==17)throw Error('Bad life-support status');
      const n=v.slice(4,15).map(x=>numeric(x,1e10));
      const allowed=new Set(['SURVIVAL','STABLE','LIVABLE','SELF_SUFFICIENT','PROSPEROUS']);
      if(!allowed.has(v[15]))throw Error('Unknown viability stage');
      Object.assign(e,{food:n[0],water:n[1],oxygen:n[2],nutrients:n[3],co2:n[4],
        crops:n[5],compost:n[6],wastewater:n[7],cycles:n[8],shortages:n[9],reserves:n[10],
        stage:v[15],readyTick:numeric(v[16],1e10)});
    }else if(t==='OUTPOST'){
      if(v.length!==6)throw Error('Bad outpost');
      e.id=numeric(v[4],1e8);e.farm=numeric(v[5],1);
      if(!e.id)throw Error('Zero outpost id');
    }else if(t==='FARM'){
      if(v.length!==17)throw Error('Bad farm status');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{id:n[0],workers:n[1],food:n[2],water:n[3],oxygen:n[4],
        nutrients:n[5],co2:n[6],crops:n[7],harvested:n[8],recent:n[9],
        cycles:n[10],shortages:n[11],production:n[12]});
      if(!e.id||e.workers>8||e.shortages>e.cycles)throw Error('Bad farm inventory');
    }else if(t==='HABITAT'){
      if(v.length!==14)throw Error('Bad habitat status');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{id:n[0],residents:n[1],capacity:n[2],occupied:n[3],
        food:n[4],water:n[5],oxygen:n[6],cycles:n[7],comfort:n[8],shortages:n[9]});
      if(!e.id||e.capacity!==8||e.residents>e.occupied||e.occupied>e.capacity||
         e.comfort>e.cycles||e.shortages>e.cycles)throw Error('Bad habitat inventory');
    }else if(t==='WATERWORKS'){
      if(v.length!==10)throw Error('Bad waterworks record');
      const n=v.slice(4).map(x=>numeric(x,1e9));
      Object.assign(e,{id:n[0],raw:n[1],brine:n[2],potable:n[3],cycles:n[4],continuity:n[5]});
      if(!e.id || e.potable!==e.brine*9 || e.raw>100000000 || e.continuity>e.cycles)
        throw Error('Nonconservative water purification');
    }else if(t==='WATER_SUPPLY'){
      if(v.length!==11)throw Error('Bad water supply assessment');
      const n=v.slice(4).map(x=>numeric(x,1e9));
      Object.assign(e,{sites:n[0],equipped:n[1],measured:n[2],output:n[3],demand:n[4],covered:n[5],renewable:n[6]});
      if(e.equipped>e.sites||e.measured>e.equipped||e.covered>1||e.renewable!==0||
         (e.covered && (!e.sites||e.measured!==e.sites||e.output<e.demand)))
        throw Error('False mineral water renewal claim');
    }else if(t==='POLICY_AUDIT'){
      if(v.length!==12)throw Error('Bad policy audit length');
      const n=v.slice(4).map(x=>numeric(x,1e8));
      Object.assign(e,{site:n[0],domain:n[1],declared:n[2],inspectors:n[3],required:n[4],operational:n[5],staffed:n[6],supported:n[7]});
      if(!e.site||e.domain>3||e.declared>3||e.inspectors>3||e.required!==e.declared||
         e.operational>1||e.staffed>1||e.supported>1||
         e.staffed!==Number(e.inspectors>=e.required)||
         e.supported!==Number(e.declared>0&&!!e.staffed&&!!e.operational))
         throw Error('Contradictory policy evidence');
    }else if(t==='RECLAIMER'){
      if(v.length!==11)throw Error('Bad reclaimer telemetry');
      const n=v.slice(4).map(x=>numeric(x,1e8));
      Object.assign(e,{id:n[0],reclaimed:n[1],generated:n[2],sludge:n[3],carbon:n[4],cycles:n[5],continuity:n[6]});
      if(!e.id || e.continuity>e.cycles || e.reclaimed!==9*e.sludge ||
         e.generated*3!==7*e.carbon)throw Error('Nonconservative reclamation');
    }else if(t==='LIFE_RECOVERY'){
      if(v.length!==15)throw Error('Bad life recovery assessment');
      const n=v.slice(4).map(x=>numeric(x,1e9));
      Object.assign(e,{sites:n[0],equipped:n[1],measured:n[2],
        waterOutput:n[3],oxygenOutput:n[4],waterNeed:n[5],oxygenNeed:n[6],
        sludge:n[7],carbon:n[8],covered:n[9],renewable:n[10]});
      if(e.equipped>e.sites||e.measured>e.equipped||e.covered>1||
         e.renewable!==0 || (e.covered && (!e.sites || e.measured!==e.sites ||
         e.waterOutput<e.waterNeed || e.oxygenOutput<e.oxygenNeed)))
          throw Error('False life-support renewal claim');
    }else if(t==='CLINIC'){
      if(v.length!==14)throw Error('Bad clinic record');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{id:n[0],medics:n[1],water:n[2],substrate:n[3],active:n[4],
        cycles:n[5],care:n[6],shortages:n[7],treated:n[8],recorded:n[9]});
      if(!e.id || e.medics>4 || e.active>8 || e.care>e.cycles ||
         e.shortages>e.cycles || e.recorded>e.cycles ||
         e.treated>e.recorded || e.active!==e.recorded-e.treated)
          throw Error('Contradictory clinic service');
    }else if(t==='CLINIC_ASSESS'){
      if(v.length!==9)throw Error('Bad clinic assessment');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{clinicSites:n[0],sites:n[1],active:n[2],treated:n[3],access:n[4]});
      if(e.clinicSites>e.sites||e.access>1||
         e.access!==((e.sites>0&&e.clinicSites===e.sites&&e.active===0)?1:0))
         throw Error('Contradictory clinical access');
    }else if(t==='COMMUNITY'){
      if(v.length!==12)throw Error('Bad community service');
      const n=v.slice(4).map(x=>numeric(x,1e12));
      Object.assign(e,{id:n[0],attendants:n[1],food:n[2],water:n[3],cycles:n[4],
        continuity:n[5],shortages:n[6],visits:n[7]});
      if(!e.id||e.attendants>4||e.continuity>e.cycles||e.shortages>e.cycles||e.visits>e.cycles*8)
        throw Error('Contradictory community service');
    }else if(t==='UTILITY'){
      if(v.length!==12)throw Error('Bad utility status');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{id:n[0],condition:n[1],stored:n[2],powered:n[3],
        cycles:n[4],reliable:n[5],outages:n[6],repairs:n[7]});
      if(!e.id||e.condition>100||e.stored>120||e.powered>1||e.reliable>e.cycles||e.outages>e.cycles)throw Error('Invalid utility');
    }else if(t==='MAINTENANCE'){
      if(v.length!==10)throw Error('Bad maintenance record');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{id:n[0],spares:n[1],technicians:n[2],enabled:n[3],
        delegatedRepairs:n[4],blocked:n[5]});
      if(!e.id||e.spares>1000||e.technicians>8||e.enabled>1||e.blocked>tick)throw Error('Invalid maintenance');
    }else if(t==='MAINTENANCE_ASSESS'){
      if(v.length!==8)throw Error('Bad maintenance assessment');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{spares:n[0],technicians:n[1],blockedSites:n[2],ready:n[3]});
      if(e.ready>1)throw Error('Invalid maintenance readiness');
    }else if(t==='RESILIENCE'){
      if(v.length!==16)throw Error('Bad resilience assessment');
      const n=v.slice(4).map(x=>numeric(x,1e12));
      Object.assign(e,{onboardCycles:n[0],siteCycles:n[1],sites:n[2],stocked:n[3],
        powered:n[4],maintained:n[5],spares:n[6],freeResidents:n[7],foodBuffer:n[8],
        operationsCandidate:n[9],growth:n[10],blockers:n[11]});
      if(e.sites>10000||e.stocked>e.sites||e.powered>e.sites||e.maintained>e.sites||
         e.foodBuffer>1||e.operationsCandidate>1||e.growth!==0||
         e.blockers>4095||!(e.blockers&256)||!(e.blockers&512)||!(e.blockers&1024)||
         e.operationsCandidate!==((e.blockers&255)===0?1:0))throw Error('Contradictory resilience');
    }else if(t==='COMMUNITY_ASSESS'){
      if(v.length!==7)throw Error('Bad community assessment');
      const n=v.slice(4).map(x=>numeric(x,1e9));
      Object.assign(e,{communitySites:n[0],sites:n[1],leisure:n[2]});
      if(e.communitySites>e.sites||e.leisure>1||
         e.leisure!==((e.sites>0&&e.communitySites===e.sites)?1:0))
        throw Error('Contradictory community readiness');
    }else if(t==='UTILITY_ASSESS'){
      if(v.length!==7)throw Error('Bad utility assessment');
      const n=v.slice(4).map(x=>numeric(x,1e9));
      Object.assign(e,{poweredSites:n[0],unpoweredSites:n[1],sustained:n[2]});
      if(e.sustained>1)throw Error('Bad service flag');
    }else if(t==='HAB_ASSESS'){
      if(v.length!==10)throw Error('Bad housing assessment');
      const n=v.slice(4,9).map(x=>numeric(x,1e10));
      if(!['SURVIVAL','STABLE','LIVABLE','SELF_SUFFICIENT','PROSPEROUS'].includes(v[9]))throw Error('Bad housing stage');
      if(v[10]!==undefined)throw Error('Bad housing suffix');
      Object.assign(e,{residents:n[0],housed:n[1],capacity:n[2],unhoused:n[3],provisioned:n[4],stage:v[9]});
      if(e.housed>e.capacity||e.provisioned>1)throw Error('Bad housing capacity');
    }else if(t==='ASSESS'){
      if(v.length!==9)throw Error('Bad viability assessment');
      const n=v.slice(4).map(x=>numeric(x,1e10));
      Object.assign(e,{totalCrew:n[0],farmStaff:n[1],recentFood:n[2],demandFood:n[3],foodSustained:n[4]});
      if(e.farmStaff>e.totalCrew||e.foodSustained>1)throw Error('Bad viability status');
    }else if(t==='ROCK'){
      if(v.length!==10)throw Error('Bad rock');
      const n=v.slice(4).map(x=>numeric(x,1e12));
      Object.assign(e,{id:n[0],classId:n[1],radius:n[2],density:n[3],original:n[4],remaining:n[5]});
      if(e.id<1||e.classId>2||e.radius<10||e.radius>350||e.density<900||e.density>8000||e.remaining>e.original)throw Error('Invalid geological metadata');
    }else if(t==='MINERAL'){
      if(v.length!==7)throw Error('Bad mineral');
      e.id=numeric(v[4]);e.name=v[5];e.share=numeric(v[6],1000);
      if(!minerals.has(e.name)||e.id<1||e.share<1)throw Error('Unknown mineral');
    }else if(t==='EXPEDITION'){
      if(v.length!==13)throw Error('Malformed native expedition assessment');
      e.body=numeric(v[4],Number.MAX_SAFE_INTEGER);
      e.ore=numeric(v[5],1e12);
      e.nickel=numeric(v[6],1e15);
      e.plagioclase=numeric(v[7],1e15);
      e.food=numeric(v[8],1e12);
      e.water=numeric(v[9],1e12);
      e.oxygen=numeric(v[10],1e12);
      e.hazard=numeric(v[11],3);
      e.support=numeric(v[12],1);
      if(!e.body)throw Error('Invalid expedition body');
    }else if(t==='OASIS_EXTRACTOR'){
      if(v.length!==9)throw Error('Malformed oasis extractor');
      e.ship=numeric(v[4],100000000);
      e.body=numeric(v[5],Number.MAX_SAFE_INTEGER);
      e.energy=numeric(v[6],120);
      e.workers=numeric(v[7],2);
      e.condition=numeric(v[8],100);
      if(!e.ship||!e.body||e.workers!==2)throw Error('Invalid oasis extractor');
    }else if(t==='OASIS_INSTALLED'){
      if(v.length!==5)throw Error('Malformed oasis installation');
      e.body=numeric(v[4],Number.MAX_SAFE_INTEGER);
      if(!e.body)throw Error('Invalid oasis installation body');
    }else if(t==='OASIS_SOURCE'){
      if(v.length!==7)throw Error('Malformed physical oasis reserve');
      e.body=numeric(v[4],Number.MAX_SAFE_INTEGER);
      e.waterKg=numeric(v[5],100000000);e.nutrientsKg=numeric(v[6],100000000);
      if(!e.body)throw Error('Invalid oasis body ID');
    }else if(t==='OASIS_COLLECTED'){
      if(v.length!==7)throw Error('Malformed oasis collection receipt');
      e.body=numeric(v[4],Number.MAX_SAFE_INTEGER);
      e.material=v[5];e.kg=numeric(v[6],10000);
      if(!e.body || !['WATER','NUTRIENTS'].includes(e.material) || !e.kg)
        throw Error('Invalid oasis collection receipt');
    }else if(t==='LOCAL_ENVIRONMENT'){
      if(v.length!==7)throw Error('Malformed celestial environment');
      e.body=numeric(v[4],Number.MAX_SAFE_INTEGER);
      e.zone=numeric(v[5],5);e.hazard=numeric(v[6],3);
      if(!e.body||!e.zone)throw Error('Invalid celestial environment');
    }else if(t==='LOCAL_DEPOSIT'){
      if(v.length!==6)throw Error('Malformed local ore taxonomy');
      e.id=numeric(v[4],100000000);e.classId=numeric(v[5],2);
      if(!e.id)throw Error('Invalid local deposit ID');
    }else if(t==='SMALL_ROCK'){
      if(v.length!==6)throw Error('Bad small rock');e.id=numeric(v[4]);e.remaining=numeric(v[5]);
    }else if(t==='SCAN_DONE'){
      if(v.length!==5)throw Error('Bad scan count');e.count=numeric(v[4],10000);
    }else if(t==='RESULT'){
      if(v.length!==7||!/^\d+$/.test(v[4])||!/^[A-Z_]+$/.test(v[5]))throw Error('Bad action result');
      e.id=numeric(v[4],Number.MAX_SAFE_INTEGER);e.outcome=v[5];e.transferred=numeric(v[6]);
    }else if(t==='ERROR'){
      if(v.length!==5||!['INVALID_INPUT','INVALID_SAFETY_ORDER','UNAUTHORIZED_PRINCIPAL','FRAME_TRAVEL_DENIED','FRAME_TRANSIT_BLOCKED'].includes(v[4]))throw Error('Bad rejected input');e.error=v[4];
    }else throw Error('Unknown colony event');
    return e;
  }
  return {decode,names};
});
(function(){'use strict';if(typeof document==='undefined')return;
 const q=id=>document.getElementById(id), policyAudits=new Map(), geology=new Map(),small=new Map(),log=[],stocks=new Map(),outposts=new Map(),farms=new Map(),habitats=new Map(),utilities=new Map(),communities=new Map(),clinics=new Map(),reclaimers=new Map(),waterworks=new Map();
 const combatTargets=new Map();
 const salvageWrecks=new Map();
 const deployedProbes=new Map();
 const signatureContacts=new Map();
 const celestialBodies=new Map(), localOreTaxonomy=new Map(),oasises=new Map();
 let probeCarried=0,probeLauncher=false,probeChoice='';
 const discoveries=new Map();
 window.GalaxySalvageStock=salvageWrecks;
 window.GalaxyColonyGeology=geology;
 window.GalaxySelectedRockId=null;
 window.GalaxySelectAsteroid=id=>{
   if(!Number.isSafeInteger(id)||id<=0||(!small.has(id)&&!geology.has(id)))return false;
   const control=q('colonyRockTarget');
   if(![...control.options].some(option=>option.value===String(id)))return false;
   control.value=String(id);
   window.GalaxySelectedRockId=id;
   q('expeditionTarget').textContent='Scanned asteroid #'+id+' selected. Check the current distance before mining.';
   return true;
 };
 q('colonyRockTarget').addEventListener('change',()=>{
   const id=Number(q('colonyRockTarget').value);
   window.GalaxySelectedRockId=(Number.isSafeInteger(id)&&id>0&&
     (small.has(id)||geology.has(id)))?id:null;
 });
 window.GalaxyColonyOutpostIds=new Set();
 const guideStorage=(()=>{try{return window.localStorage;}catch{return null;}})();
 let skippedOpening=window.GalaxyFlightControls?.guideSkipped(guideStorage)||false;
 q('openingGuide').hidden=skippedOpening;
 q('skipOpening').textContent=skippedOpening?'Show tutorial':'Skip tutorial';
 q('skipOpening').addEventListener('click',()=>{
   skippedOpening=!skippedOpening;
   window.GalaxyFlightControls?.setGuideSkipped(guideStorage,skippedOpening);
   q('openingGuide').hidden=skippedOpening;
   q('showOpening').hidden=!skippedOpening;
 });
 q('showOpening').addEventListener('click',()=>{
   skippedOpening=false;
   window.GalaxyFlightControls?.setGuideSkipped(guideStorage,false);
   q('openingGuide').hidden=false;q('showOpening').hidden=true;
 });
 q('showOpening').hidden=!skippedOpening;
 let openingStatus=null,openingScanned=false;
 let expeditionAssessment=null;
 function refreshOpening(){
   const guide=window.GalaxyOpeningGuide?.progress(openingStatus,openingScanned);
   if(!guide)return;
   q('openingStage').textContent=guide.done?'Mission complete':guide.stage<0?'Waiting for world':`Step ${guide.stage+1} of 5 · ${guide.title}`;
   q('openingDetail').textContent=guide.detail;
   const button=q('openingAction');
   button.disabled=!guide.action;
   button.textContent=guide.action?guide.title:guide.done?'First outpost complete':'Awaiting world';
   button.dataset.action=guide.action||'';
 }
 q('openingAction').addEventListener('click',()=>{
   const guide=window.GalaxyOpeningGuide?.progress(openingStatus,openingScanned);
   if(guide?.action&&q('openingAction').dataset.action===guide.action)
     window.GalaxyColonyCommand?.(guide.action,
       guide.action==='colonyMine'||guide.action==='colonyRefine'?{amount:8}:
       guide.action==='colonyFabricate'?{amount:4}:{});
 });
 window.GalaxyColony={
   reset(){signatureContacts.clear();celestialBodies.clear();oasises.clear();localOreTaxonomy.clear();expeditionAssessment=null;
     q('celestialBodyList').textContent='No physical celestial bodies surveyed yet.';
     q('signatureTarget').replaceChildren(new Option('Scan to detect signals',''));
     q('signatureFocus').disabled=true;q('signatureCancel').disabled=true;
     q('signatureFeedback').textContent='Search for coarse anomaly signatures.';
     q('droneTarget').replaceChildren(new Option('Scan nearby rocks first',''));q('droneLaunch').disabled=true;deployedProbes.clear();probeCarried=0;probeLauncher=false;probeChoice='';q('probeTarget').replaceChildren(new Option('No probe deployed',''));q('probeLaunch').disabled=true;q('probeSurvey').disabled=true;q('probeRetrieve').disabled=true;q('probeInventory').textContent='Awaiting native probe stock.';q('probeFeedback').textContent='Native equipment required.';q('fittingCapacity').textContent='Awaiting actual ship fittings.';q('proximityStatus').textContent='Proximity safety sensor awaiting contacts.';q('fitScanner').disabled=true;q('unfitScanner').disabled=true;discoveries.clear();q('explorationResults').replaceChildren();q('explorationSummary').textContent='No mapped discoveries yet. Execute a native local scan.';salvageWrecks.clear();q('salvageTarget').replaceChildren(new Option('No recoverable wreck reported',''));q('salvageRecover').disabled=true;q('salvageFeedback').textContent='Waiting for native wreck inventory.';combatTargets.clear();q('combatHull').textContent='—';q('combatCharges').textContent='—';q('combatFire').disabled=true;q('combatFeedback').textContent='Awaiting native combat telemetry.';window.GalaxySelectedRockId=null;window.GalaxyShipHeading=null;window.GalaxyColonyOutpostIds.clear();openingStatus=null;openingScanned=false;refreshOpening();window.GalaxyColonyHomeShipId=null;q('colonySafetyAssess').disabled=true;q('colonySafetyPreflight').disabled=true;policyAudits.clear();geology.clear();small.clear();stocks.clear();outposts.clear();farms.clear();habitats.clear();utilities.clear();communities.clear();clinics.clear();reclaimers.clear();waterworks.clear();log.length=0;q('colonyEvents').textContent='Awaiting colony telemetry…';q('expeditionOutcome').textContent='Awaiting an actual native result';},
   accept(raw){const e=window.GalaxyColonyContract.decode(raw);
     if(e.type==='CELESTIAL_HEADER'){
       celestialBodies.clear();oasises.clear();
       q('celestialBodyList').textContent='Receiving authoritative physical bodies…';
     }
     if(e.type==='CELESTIAL_BODY'){
       celestialBodies.set(e.id,e);
       const labels=['Star','Planet','Moon','Debris region'];
       const rows=[...celestialBodies.values()];
       q('celestialBodyList').replaceChildren(...rows.map(body=>{
         const item=document.createElement('div');
         item.className='entry';
         item.textContent=labels[body.kind]+' #'+body.id+
           ' · parent '+(body.parent||'none')+
           ' · '+(body.orbit/1000).toFixed(2)+' AU orbital band'+
           ' · radius '+Math.round(body.radius/1000000)+' km'+
           ' · world '+(body.x/1000000).toFixed(0)+', '+
           (body.y/1000000).toFixed(0)+', '+
           (body.z/1000000).toFixed(0)+' km (x/y/z)';
         const action=document.createElement('button');
         action.type='button';
         action.textContent='Travel to local region';
         action.disabled=body.id===window.GalaxyActiveCelestialBody;
         action.addEventListener('click',()=>{
           if(window.GalaxyColonyCommand)
             window.GalaxyColonyCommand('colonyFrameTravel',{body:body.id});
           q('signatureFeedback').textContent='Celestial travel requested. Awaiting the authoritative host.';
         });
         item.appendChild(action);
         if(body.kind===1 || body.kind===2) {
           // Coordinates are logical coarse surface cells, not a claim of
           // exact landing position. Native Simulation validates them again.
           const latitude=document.createElement('input');
           latitude.type='number';latitude.min='0';latitude.max='35';
           latitude.step='1';latitude.value='17';
           latitude.setAttribute('aria-label','Region latitude cell 0–35');
           const longitude=document.createElement('input');
           longitude.type='number';longitude.min='0';longitude.max='71';
           longitude.step='1';longitude.value='36';
           longitude.setAttribute('aria-label','Region longitude cell 0–71');
           const survey=document.createElement('button');
           survey.type='button';
           survey.textContent='Survey region';
           survey.addEventListener('click',()=>{
             const lat=Number(latitude.value),lon=Number(longitude.value);
             if(!Number.isSafeInteger(lat)||lat<0||lat>=36||
                !Number.isSafeInteger(lon)||lon<0||lon>=72) {
               q('signatureFeedback').textContent='Select valid region cells: latitude 0–35 and longitude 0–71.';
               return;
             }
             window.GalaxyColonyCommand?.('colonyPlanetSurvey',
               {body:body.id,latitude:lat,longitude:lon});
             q('signatureFeedback').textContent=
               'Planetary region requested from authoritative native geography.';
           });
           const actions=document.createElement('details');
           const caption=document.createElement('summary');
           caption.textContent='Regional construction / farming / extraction';
           const choice=document.createElement('select');
           choice.setAttribute('aria-label','Planetary region operation');
           const operations=[
             ['Delegate NPCs — build surface spaceport','colonyPlanetBuild',{}],
             ['Install farm (two modules)','colonyPlanetEquip',{equipment:'FARM'}],
             ['Install extractor (two modules)','colonyPlanetEquip',{equipment:'EXTRACT'}],
             ['Supply 100 kg water','colonyPlanetSupply',{resource:'WATER',kg:100}],
             ['Supply 50 kg nutrients','colonyPlanetSupply',{resource:'NUTRIENTS',kg:50}],
             ['Resupply surface NPCs with 50 kg food','colonyPlanetSupport',{resource:'FOOD',kg:50}],
             ['Resupply surface NPCs with 50 kg oxygen','colonyPlanetSupport',{resource:'OXYGEN',kg:50}],
             ['Grow two farm batches','colonyPlanetGrow',{batches:2}],
             ['Extract 1000 kg water','colonyPlanetExtract',{resource:'WATER',kg:1000}],
             ['Extract 1000 kg nutrients','colonyPlanetExtract',{resource:'NUTRIENTS',kg:1000}],
             ['Retrieve 12 kg food','colonyPlanetRetrieve',{resource:'FOOD',kg:12}],
             ['Retrieve 1000 kg water','colonyPlanetRetrieve',{resource:'WATER',kg:1000}]
           ];
           operations.forEach(([label],index)=>choice.add(new Option(label,String(index))));
           const firstNpc=document.createElement('input');
           const secondNpc=document.createElement('input');
           for(const [input,label] of [[firstNpc,'First NPC ID (optional)'],
                                       [secondNpc,'Second NPC ID (optional)']]){
             input.type='number';input.min='1';input.step='1';
             input.placeholder=label;
             input.setAttribute('aria-label',label);
           }
           const crewButton=document.createElement('button');
           crewButton.type='button';crewButton.textContent='List available NPCs';
           crewButton.addEventListener('click',()=>window.GalaxyColonyCommand?.('colonyPlanetCrew',{}));
           const statusButton=document.createElement('button');
           statusButton.type='button';statusButton.textContent='Check spaceport progress';
           statusButton.addEventListener('click',()=>{
             const lat=Number(latitude.value),lon=Number(longitude.value);
             if(Number.isInteger(lat)&&lat>=0&&lat<36&&Number.isInteger(lon)&&lon>=0&&lon<72)
               window.GalaxyColonyCommand?.('colonyPlanetStatus',
                 {body:body.id,latitude:lat,longitude:lon});
           });
           const policySelect=document.createElement('select');
           policySelect.setAttribute('aria-label','Autonomous NPC planetary work policy');
           [['Pause autonomous work',0],['Run farm',1],
             ['Extract water',2],['Extract nutrients',3],
             ['Maintain equipment',4]].forEach(([label,value])=>
               policySelect.add(new Option(label,String(value))));
           const policyButton=document.createElement('button');
           policyButton.type='button';policyButton.textContent='Assign NPC work policy';
           policyButton.addEventListener('click',()=>{
             const lat=Number(latitude.value),lon=Number(longitude.value);
             if(!Number.isInteger(lat)||lat<0||lat>=36||
                !Number.isInteger(lon)||lon<0||lon>=72)return;
             window.GalaxyColonyCommand?.('colonyPlanetPolicy',
               {body:body.id,latitude:lat,longitude:lon,
                policy:Number(policySelect.value)});
           });
           const perform=document.createElement('button');
           perform.type='button';perform.textContent='Send orbital order';
           perform.addEventListener('click',()=>{
             const lat=Number(latitude.value),lon=Number(longitude.value);
             if(!Number.isSafeInteger(lat)||lat<0||lat>=36||
                !Number.isSafeInteger(lon)||lon<0||lon>=72){
               q('signatureFeedback').textContent='Invalid planetary region coordinates.';
               return;
             }
             const selected=operations[Number(choice.value)];
             if(!selected)return;
             const payload={body:body.id,latitude:lat,longitude:lon,...selected[2]};
             if(selected[1]==='colonyPlanetBuild' &&
                (firstNpc.value||secondNpc.value)){
               const first=Number(firstNpc.value),second=Number(secondNpc.value);
               if(!Number.isSafeInteger(first)||first<=0||
                  !Number.isSafeInteger(second)||second<=0||first===second){
                 q('signatureFeedback').textContent='Select two distinct valid NPC IDs.';
                 return;
               }
               payload.firstNpc=first;payload.secondNpc=second;
             }
             window.GalaxyColonyCommand?.(selected[1],payload);
             q('signatureFeedback').textContent=
               'Operation requested. A native survey of this exact region is required.';
           });
           actions.append(caption,choice,firstNpc,secondNpc,crewButton,statusButton,policySelect,policyButton,perform);
           const shuttlePanel=document.createElement('details');
           const shuttleTitle=document.createElement('summary');
           shuttleTitle.textContent='Orbital shuttle / surface logistics';
           const shuttleChoice=document.createElement('select');
           shuttleChoice.setAttribute('aria-label','Shuttle command');
           const shuttleOperations=[
             ['Check shuttle status','STATUS'],['Fabricate shuttle (2 modules)','BUILD'],
             ['Assign named pilot','PILOT'],['Load supplies','LOAD'],
             ['Unload at current berth','UNLOAD'],['Descend to surface','DESCEND'],
             ['Return to orbit','ASCEND']
           ];
           shuttleOperations.forEach(([name,code])=>shuttleChoice.add(new Option(name,code)));
           const shuttleNpc=document.createElement('input');
           shuttleNpc.type='number';shuttleNpc.min='1';shuttleNpc.step='1';
           shuttleNpc.placeholder='Pilot NPC ID';shuttleNpc.setAttribute('aria-label','Shuttle pilot NPC ID');
           const shuttleResource=document.createElement('select');
           shuttleResource.setAttribute('aria-label','Shuttle cargo material');
           shuttleResource.add(new Option('Water','WATER'),new Option('Food','FOOD'));
           const shuttleKg=document.createElement('input');
           shuttleKg.type='number';shuttleKg.min='1';shuttleKg.max='1000';
           shuttleKg.step='1';shuttleKg.value='50';shuttleKg.setAttribute('aria-label','Shuttle cargo kilograms');
           const shuttleState=document.createElement('output');
           shuttleState.textContent='Request status to see committed shuttle progress.';
           const shuttleSend=document.createElement('button');
           shuttleSend.type='button';shuttleSend.textContent='Send shuttle order';
           shuttleSend.addEventListener('click',()=>{
             const lat=Number(latitude.value),lon=Number(longitude.value);
             if(!Number.isSafeInteger(lat)||lat<0||lat>=36||
                !Number.isSafeInteger(lon)||lon<0||lon>=72)return;
             const operation=shuttleChoice.value;
             const payload={body:body.id,latitude:lat,longitude:lon,operation};
             if(operation==='PILOT'){
               const npc=Number(shuttleNpc.value);
               if(!Number.isSafeInteger(npc)||npc<=0)return;
               payload.npc=npc;
             }else if(operation==='LOAD'||operation==='UNLOAD'){
               const kg=Number(shuttleKg.value);
               if(!Number.isInteger(kg)||kg<=0||kg>1000)return;
               payload.resource=shuttleResource.value;payload.kg=kg;
             }else if(operation==='DESCEND'||operation==='ASCEND'){
               // Stable within one click; new requests use a fresh millisecond ID.
               const request=Date.now()*1000+Math.floor(Math.random()*1000);
               if(!Number.isSafeInteger(request))return;
               payload.request=request;
             }
             window.GalaxyColonyCommand?.('colonyPlanetShuttle',payload);
             shuttleState.textContent='Shuttle order submitted; awaiting native receipt.';
           });
           shuttlePanel.append(shuttleTitle,shuttleChoice,shuttleNpc,
               shuttleResource,shuttleKg,shuttleSend,shuttleState);
           shuttlePanel.dataset.shuttleBody=String(body.id);
           shuttlePanel.dataset.shuttleRegion='';
           const updateRegion=()=>{shuttlePanel.dataset.shuttleRegion=latitude.value+','+longitude.value;};
           latitude.addEventListener('change',updateRegion);
           longitude.addEventListener('change',updateRegion);updateRegion();
           actions.append(shuttlePanel);

           item.append(latitude,longitude,survey,actions);
         }
         return item;
       }));
     }
     if(e.type==='PLANET_CREW_AVAILABLE'){
       if(!window.GalaxyAvailableSurfaceNpcIds)window.GalaxyAvailableSurfaceNpcIds=[];
       window.GalaxyAvailableSurfaceNpcIds.push(e.id);
     }
     if(e.type==='PLANET_CREW_DONE'){
       const roster=window.GalaxyAvailableSurfaceNpcIds||[];
       q('signatureFeedback').textContent=roster.length?
         'Ready NPCs (assign two by ID): '+roster.join(', '):
         'No individually available NPC records. Legacy finite crew can be auto-delegated.';
       window.GalaxyAvailableSurfaceNpcIds=[];
     }
     if(e.type==='PLANET_SHUTTLE_STATUS'||e.type==='PLANET_SHUTTLE_COMMITTED'){
       const cards=q('celestialBodyList').querySelectorAll('[data-shuttle-body]');
       cards.forEach(panel=>{
         if(panel.dataset.shuttleBody!==String(e.id)||
            panel.dataset.shuttleRegion!==e.latitude+','+e.longitude)return;
         const out=panel.querySelector('output');
         if(!out)return;
         if(e.type==='PLANET_SHUTTLE_COMMITTED'){
           out.textContent='Committed '+e.operation+' · generation '+e.generation;
         }else{
           const phases=['Docked in orbit','Descending','Landed at surface','Ascending'];
           out.textContent=e.present?
             phases[e.phase]+' · pilot #'+e.pilot+' · water '+e.water+
             ' kg · food '+e.food+' kg · due tick '+e.arrival+
             ' · active request '+e.request+' · generation '+e.generation:
             'No shuttle built at this planetary region.';
         }
       });
       q('signatureFeedback').textContent=e.type==='PLANET_SHUTTLE_COMMITTED'?
         'Shuttle '+e.operation+' committed for body #'+e.id+'.':
         'Orbital shuttle status received from the native world.';
     }
     if(e.type==='PLANET_SITE_STATUS'){
       const names={1:'NPC descent / equipment unloading',
                    2:'Spaceport under construction',3:'Spaceport operational'};
       q('signatureFeedback').textContent=
         'Orbital site '+e.id+' ('+e.latitude+','+e.longitude+'): '+
         names[e.phase]+' · '+e.progress+'/'+e.required+
         ' progress ticks · '+e.workers+' delegated NPCs '+
         (e.npc1?'#'+e.npc1+' and #'+e.npc2:'(legacy finite crew)')+
         ' · surface food '+e.food+' kg, water '+e.water+
         ' kg, oxygen '+e.oxygen+' kg. Autonomous policy '+e.policy+
         ' · condition '+e.condition+'% · work cycles '+e.cycles+
         ' · paused/stalled cycles '+e.stalled+' · extracted '+
         e.extracted+' kg · repairs '+e.repairs+'.';
     }
     if(e.type==='PLANET_POLICY_COMMITTED'){
       const names=['Paused','Farming','Water extraction','Nutrient extraction',
                    'Maintenance'];
       q('signatureFeedback').textContent=
         'NPC planetary policy committed: '+names[e.policy]+
         ' at body #'+e.id+' region ('+e.latitude+','+e.longitude+
         '). Autonomous work now follows simulation ticks; generation '+
         e.generation+'.';
     }
     if(e.type==='PLANET_OPERATION_COMMITTED'){
       q('signatureFeedback').textContent=
          (e.action==='PLANET_BUILD'?
           'NPC surface delegation accepted; spaceport construction is underway. ':
           'Planetary operation committed: '+e.action+'. ')+
          'Body #'+e.id+' region ('+e.latitude+','+e.longitude+
          '), world generation '+e.generation+
          '. Use Check spaceport progress for the current state.';
     }
     if(e.type==='PLANET_REGION'){
       const terrains=['Deep ocean','Shallow sea','Coast','Wetland','Grassland',
         'Forest','Dryland','Desert','Mountain','Ice','Bare rock'];
       const channels=['No channel','Dry arroyo','Seasonal stream','Perennial river'];
       q('signatureFeedback').textContent=
         'Planet #'+e.id+' region ('+e.latitude+','+e.longitude+') · '+
         terrains[e.terrain]+' · elevation '+e.elevation+
         ' · '+channels[e.watercourse]+' · watershed runoff score '+e.runoff+
         ' · geographic version '+e.revision+
         ' (survey potential, not extractable water).';
     }
     if(e.type==='OASIS_REGION'){
       if(celestialBodies.has(e.id)){
         const rows=[...celestialBodies.keys()];
         const card=q('celestialBodyList').children[rows.indexOf(e.id)];
         if(card && !card.dataset.oasisRegion){
           card.dataset.oasisRegion='1';
           const note=document.createElement('div');
           note.textContent='Finite oasis source waypoint: region ('+
             e.latitude+','+e.longitude+') · version '+e.revision+
             '. Survey and physically arrive before construction or extraction.';
           card.appendChild(note);
           const inputs=card.querySelectorAll('input[type="number"]');
           if(inputs.length>=2){
             inputs[0].value=String(e.latitude);
             inputs[1].value=String(e.longitude);
           }
         }
       }
     }
     if(e.type==='OASIS_PROSPECT'){
       if(celestialBodies.has(e.id)){
         oasises.set(e.id,e);
         const rows=[...celestialBodies.keys()];
         const index=rows.indexOf(e.id);
         const card=q('celestialBodyList').children[index];
         if(card && !card.dataset.oasis){
           card.dataset.oasis='1';
           const note=document.createElement('div');
           note.textContent='Rare oasis prospect: local water, accessible energy, nutrients and possible radiation shelter. No free supplies, buildings, oxygen or guaranteed safety; development required.';
           card.appendChild(note);
         }
       }
     }
     if(e.type==='LOCAL_FRAME'){
       if(window.GalaxyActiveCelestialBody!==e.anchor){
         window.GalaxyActiveCelestialBody=e.anchor;
         q('signatureFeedback').textContent='Current local frame: '+
           (e.anchor?'celestial body #'+e.anchor:'open-space legacy region')+'.';
       }
     }
     if(e.type==='FRAME_TRANSIT_STARTED')
       q('signatureFeedback').textContent='Transit #'+e.request+
         ' started for body #'+e.body+'. Local operations are suspended.';
     if(e.type==='FRAME_TRANSIT_ARRIVED'){
       window.GalaxyActiveCelestialBody=e.body;
       signatureContacts.clear();geology.clear();small.clear();localOreTaxonomy.clear();
       q('signatureTarget').replaceChildren(new Option('Rescan new local region',''));
       q('signatureFocus').disabled=true;
       q('droneTarget').replaceChildren(new Option('Rescan local targets',''));
       q('droneLaunch').disabled=true;
       q('colonyRockTarget').replaceChildren(new Option('Rescan local targets',''));
       window.GalaxySelectedRockId=null;
       q('signatureFeedback').textContent='Arrived at body #'+e.body+
         '. Local sensor contacts were cleared; scan this region.';
     }
     if(e.type==='CELESTIAL_HISTORY')
       q('signatureFeedback').textContent='Previously surveyed on tick '+e.first+
         ' (most recent survey tick '+e.last+'). ';
     if(e.type==='CELESTIAL_HEADER'){
       const names=['Single star','Wide binary','Circumbinary','Hierarchical triple'];
       const history=q('signatureFeedback').textContent.startsWith('Previously surveyed')?
         q('signatureFeedback').textContent:'';
       q('signatureFeedback').textContent=history+
         'System catalogue: '+names[e.architecture]+', '+e.stars+
         ' star(s), '+e.bands+' generated orbital bands. Catalogue only.';
     }
     if(e.type==='CELESTIAL_BAND')
       q('signatureFeedback').textContent+=
         ' Band '+e.ordinal+': '+(e.radius/1000).toFixed(2)+' AU.';
     if(e.type==='ENVIRONMENT_STATUS')
       q('signatureFeedback').textContent='System '+e.system+
         ' survey — ship speed '+e.speed+' mm/tick; '+
         (e.magnetized?'magnetized-region feature':'no magnetized-region feature')+
         ', '+(e.dust?'dust-arc feature':'no dust-arc feature')+
         '. Indicators only; not physical hazards yet.';
     if(e.type==='SIGNATURE_RESET'){
       signatureContacts.clear();
       q('signatureTarget').replaceChildren(new Option('No signatures detected',''));
       q('signatureFocus').disabled=true;
     }
     if(e.type==='SIGNATURE_CONTACT'){
       signatureContacts.set(e.id,e);
       const select=q('signatureTarget'),chosen=select.value;
       select.replaceChildren(new Option('Select signature',''),
         ...[...signatureContacts].map(([id,p])=>new Option(
           '#'+id+' · coarse '+p.x+', '+p.y+' mm (±'+p.uncertainty+' mm)',String(id))));
       if([...select.options].some(option=>option.value===chosen))select.value=chosen;
       q('signatureFocus').disabled=!select.value||!q('probeTarget').value;
     }
     if(e.type==='SIGNATURE_DONE'){
       q('signatureFeedback').textContent=e.count+
         ' nearby signatures detected. Select one, choose a deployed probe and lock on.';
     }
     if(e.type==='PROBE_RESET'){
        probeChoice=q('probeTarget').value||probeChoice;
        deployedProbes.clear();
        q('probeTarget').replaceChildren(new Option('Choose deployed probe',''));
      }
      if(e.type==='PROBE_REMOVED'){
        deployedProbes.delete(e.id);
        const select=q('probeTarget'),current=select.value;
        select.replaceChildren(new Option('Choose deployed probe',''),...[...deployedProbes].map(([id,p])=>
          new Option('#'+id+' · '+p.x+', '+p.y+' mm'+
            (p.phase?' · drone '+['','outbound','returning','ready'][p.phase]+
            ' · geology '+(p.taxonomy?window.GalaxyColonyContract.names[p.taxonomy-1]:'pending'):' · stationary probe'),String(id))));
        if([...select.options].some(o=>o.value===current))select.value=current;
        q('probeSurvey').disabled=!probeLauncher||!select.value;
        q('probeRetrieve').disabled=!probeLauncher||!select.value;
      }
      if(e.type==='PROBE_STOCK'){
        probeCarried=e.carried;probeLauncher=e.launcher;
        q('probeInventory').textContent=probeCarried+' aboard · '+(probeLauncher?'launcher fitted':'no launcher fitted')+
          ' · '+deployedProbes.size+' currently deployed';
        q('probeLaunch').disabled=!probeLauncher||probeCarried===0;
        q('droneLaunch').disabled=!probeLauncher||!probeCarried||!q('droneTarget').value;
        q('probeSurvey').disabled=!probeLauncher||!q('probeTarget').value;
        q('probeRetrieve').disabled=!probeLauncher||!q('probeTarget').value;
      }
      if(e.type==='SURVEY_PROBE'){
        deployedProbes.set(e.id,e);
        const selected=q('probeTarget'),current=selected.value;
        selected.replaceChildren(new Option('Choose deployed probe',''),...[...deployedProbes].map(([id,p])=>
          new Option('#'+id+' · '+p.x+', '+p.y+' mm'+
            (p.phase?' · drone '+['','outbound','returning','ready to recover'][p.phase]+
            ' · report '+(p.taxonomy?window.GalaxyColonyContract.names[p.taxonomy-1]:'pending'):' · stationary probe'),String(id))));
        if([...selected.options].some(o=>o.value===current))selected.value=current;
        else if([...selected.options].some(o=>o.value===probeChoice))selected.value=probeChoice;
        q('probeSurvey').disabled=!probeLauncher||!selected.value;
        const p=deployedProbes.get(Number(selected.value));
        q('probeRetrieve').disabled=!probeLauncher||!p||
          (p.phase!==0 && p.phase!==3);
        q('probeSurvey').disabled=!probeLauncher||!p||p.phase!==0||!!p.focus;
        q('signatureFocus').disabled=!probeLauncher||!p||!!p.focus||!q('signatureTarget').value;
        q('signatureCancel').disabled=!probeLauncher||!p||!p.focus;
        if(p&&p.focus)
          q('signatureFeedback').textContent='Probe #'+p.id+
            ' refining signature #'+p.focus+' · native waypoint progress: '+
            p.x+', '+p.y+' mm';
        q('probeFeedback').textContent=e.phase?
          'Geological drone #'+e.id+' · '+
          ['','outbound','returning','ready to recover'][e.phase]+
          ' · geological survey '+(e.taxonomy?window.GalaxyColonyContract.names[e.taxonomy-1]:'pending'):
          'Probe #'+e.id+' remains at '+e.x+', '+e.y+' mm';
      }
      if(e.type==='FITTING'){
       q('fittingCapacity').textContent=
         'Hardpoints '+(Number(e.installed)+Number(probeLauncher))+'/'+e.hardpoints+
         ' · Support 0/'+e.support+' · Subsystems 0/'+e.subsystems+
         ' · Rigging 0/'+e.rigging+
         ' · '+(e.installed?'Survey scanner installed':'No exploration scanner installed');
       q('fitScanner').disabled=e.installed;
       q('unfitScanner').disabled=!e.installed;
       q('explorationScan').disabled=!e.installed;
       q('flightScan').disabled=!e.installed;
     }
     if(e.type==='PROXIMITY'){
       q('proximityStatus').textContent=e.id?
         'Proximity contact: ship #'+e.id+' at '+e.distance+' mm. Maneuver manually to avoid collision risk.':
         'No other ships within 5,000 mm (safety awareness only).';
     }
     if(e.type==='DISCOVERY'){
       if(!discoveries.has(e.site) || e.surveyTaxonomy)
          discoveries.set(e.site,e);
       const rows=window.GalaxyExplorationJournal?.entries(discoveries,e.system);
       if(rows){
         const summary=window.GalaxyExplorationJournal.stats(rows);
         q('explorationSummary').textContent=summary.total+' historically mapped objects · '+
           summary.geological+' geological deposits · '+summary.systems+' system(s) represented.';
         q('explorationResults').replaceChildren(...rows.slice(-40).reverse().map(record=>{
           const node=document.createElement('div');node.className='entry';
           node.textContent=window.GalaxyExplorationJournal.describe(record);return node;
         }));
       }
     }
     if(e.type==='SALVAGE_WRECK'){
       salvageWrecks.set(e.id,e.cargo);
       const select=q('salvageTarget'),current=select.value;
       select.replaceChildren(new Option('Choose recoverable wreck',''),...[...salvageWrecks].map(([id,cargo])=>
         new Option('#'+id+' · '+cargo+' t remaining',String(id))));
       if([...select.options].some(option=>option.value===current))select.value=current;
       q('salvageRecover').disabled=!select.value||!salvageWrecks.get(Number(select.value));
       q('salvageFeedback').textContent='Wreck #'+e.id+' contains '+e.cargo+' t of conserved bulk material.';
     }
     if(e.type==='COMBAT_SHIP'){
       q('combatHull').textContent=e.hull+' / 100';
       q('combatCharges').textContent=String(e.charges);
       q('combatFire').disabled=e.charges===0||q('combatTarget').value==='';
     }
     if(e.type==='COMBAT_TARGET'){
       combatTargets.set(e.id,e.hull);
       const select=q('combatTarget'),current=select.value;
       select.replaceChildren(new Option('Select training drone',''),...[...combatTargets].map(([id,hull])=>
         new Option('#'+id+' · hull '+hull,String(id))));
       if([...select.options].some(o=>o.value===current))select.value=current;
       q('combatFire').disabled=!select.value||q('combatCharges').textContent==='0';
       q('combatFeedback').textContent='Training drone #'+e.id+' has '+e.hull+' hull remaining.';
     }
     if(e.type==='HEADING'){
       window.GalaxyShipHeading=e.heading;
       q('flightHeading').textContent=e.heading+'°';
     }
     if(e.type==='STATUS'){
       openingStatus={crew:e.crew,raw:e.raw,refined:e.refined,modules:e.modules,outposts:e.outposts};
       refreshOpening();
       for(const [name,val] of [['colonyCrew',e.crew],['colonyRaw',e.raw],['colonyRefined',e.refined],['colonyModules',e.modules],['colonyOutposts',e.outposts],['colonyTotal',e.total]])q(name).textContent=val.toLocaleString();
     }else if(e.type==='SAFETY'){
       window.GalaxyColonyHomeShipId=e.site;
       const states=['STABLE','FORECAST SHORTAGE','CRITICAL BUFFER','EMERGENCY'];
       q('colonySafetyRisk').textContent=states[e.risk];
       q('colonySafetyRunway').textContent=`${e.pop} colonists · food ${e.food}, water ${e.water}, oxygen ${e.oxygen} consumption periods remaining · physical supplies only, not promised imports.`+
         (expeditionAssessment?` Frontier survey: ${expeditionAssessment.ore} t reachable regional ore, ${expeditionAssessment.nickel.toLocaleString()} kg nickel-iron and ${expeditionAssessment.plagioclase.toLocaleString()} kg plagioclase in existing deposits. Supply runway: ${Math.min(expeditionAssessment.food,expeditionAssessment.water,expeditionAssessment.oxygen)} consumption periods. Local supply site: ${expeditionAssessment.support?'yes':'none'}.`:'');
       q('colonySafetyAssess').disabled=false;
       q('colonySafetyPreflight').disabled=false;
     }else if(e.type==='LIFE'){
       for(const [name,val] of [['lifeFood',e.food],['lifeWater',e.water],['lifeOxygen',e.oxygen],
         ['lifeNutrients',e.nutrients],['lifeCrops',e.crops],['lifeCompost',e.compost],
         ['lifeWastewater',e.wastewater],['lifeCycles',e.cycles],['lifeShortages',e.shortages],
         ['lifeReserves',e.reserves]])q(name).textContent=val.toLocaleString();
       q('lifeStage').textContent=e.stage.replaceAll('_',' ');
       q('lifeStage').title=e.stage==='STABLE'?'Supplies have met crew needs for multiple cycles. This is not self-sufficiency.':'Colony viability based on current reserves and recent shortages.';
     }else if(e.type==='OUTPOST'){outposts.set(e.id,e.farm);window.GalaxyColonyOutpostIds.add(e.id);}
     else if(e.type==='FARM')farms.set(e.id,e);
     else if(e.type==='HABITAT')habitats.set(e.id,e);
     else if(e.type==='COMMUNITY')communities.set(e.id,e);
     else if(e.type==='RECLAIMER')reclaimers.set(e.id,e);
     else if(e.type==='WATERWORKS')waterworks.set(e.id,e);
     else if(e.type==='POLICY_AUDIT')policyAudits.set(`${e.site}:${e.domain}`,e);
     else if(e.type==='WATER_SUPPLY')q('waterworksReadiness').textContent=
       `${e.equipped}/${e.sites} equipped sites · ${e.measured} with three service periods · last 3 periods: ${e.output}/${e.demand} kg purified local water / demand · supply ${e.covered?'covered':'not covered'} · finite source (not independently renewable)`;
     else if(e.type==='LIFE_RECOVERY')q('reclaimerReadiness').textContent=
       `${e.equipped}/${e.sites} occupied sites equipped · ${e.measured} with three powered periods · last 3 needs periods: ${e.waterOutput}/${e.waterNeed} kg reclaimed water / demand, ${e.oxygenOutput}/${e.oxygenNeed} kg oxygen / demand · ${e.sludge} kg sludge, ${e.carbon} kg carbon residue · local throughput ${e.covered?'covering recent demand':'not covering recent demand'} · independent water/oxygen renewal NOT proven`;
     else if(e.type==='CLINIC')clinics.set(e.id,e);
     else if(e.type==='CLINIC_ASSESS')q('clinicReadiness').textContent=
       `${e.clinicSites}/${e.sites} occupied sites with three supported care cycles · ${e.active} open aggregate cases · ${e.treated} treated cases · basic care access ${e.access?'supported':'not supported'} · NOT a full healthcare or autonomous-growth certificate`;
     else if(e.type==='UTILITY')utilities.set(e.id,{...utilities.get(e.id),...e});
     else if(e.type==='MAINTENANCE')utilities.set(e.id,{...utilities.get(e.id),...e});
     else if(e.type==='MAINTENANCE_ASSESS')q('maintenanceViability').textContent=`${e.spares} stored spare modules · ${e.technicians} assigned technicians · ${e.blockedSites} sites not ready · ${e.ready?'delegated maintenance ready':'maintenance buffers or provisions incomplete'} · NOT autonomous growth certification`;
     else if(e.type==='RESILIENCE'){
       const reasons=['No occupied outpost','Ship reserves under six needs cycles',
         'A local site lacks six needs cycles of provisions','Recent local or ship shortages',
         'Missing or unreliable solar power','Maintenance or two spare modules missing',
         'More than half of residents assigned maintenance','25% food buffer unproven',
         'Water replacement rate not verified','Healthcare and rest services not modeled',
         'NPC growth governance not implemented','Restorative community service missing or interrupted'];
       const pending=reasons.filter((_,i)=>(e.blockers&(1<<i))!==0);
       q('colonyResilience').textContent=`B7.0j resilience · ship ${e.onboardCycles} cycles · weakest occupied site ${e.siteCycles} cycles · ${e.stocked}/${e.sites} stocked sites · ${e.powered} powered · ${e.maintained} maintainable · ${e.spares} local spares · ${e.freeResidents} non-maintenance residents · food buffer ${e.foodBuffer?'proven':'unproven'} · community service details below · ${e.operationsCandidate?'Operational delegation candidate (not activated)':'Operational delegation unsafe'} · Autonomous growth NOT certified.\nBottlenecks: ${pending.join('; ')}.`;
     }
     else if(e.type==='COMMUNITY_ASSESS'){
       q('communityReadiness').textContent=`${e.communitySites}/${e.sites} occupied sites with sustained services · restorative leisure ${e.leisure?'evidenced':'not proven'} · healthcare and growth governance NOT certified`;
     }
     else if(e.type==='UTILITY_ASSESS')q('utilityViability').textContent=`${e.poweredSites} powered / ${e.unpoweredSites} unpowered sites · ${e.sustained?'3 consecutive reliable service periods':'service resilience not yet demonstrated'}`;
     else if(e.type==='HAB_ASSESS'){
       q('habViability').textContent=`${e.housed}/${e.capacity} housed · ${e.residents} residents · ${e.unhoused} unhoused farm staff · ${e.provisioned?'provisioning demonstrated':'housing not yet sustainably provisioned'} · ${e.stage.replaceAll('_',' ')}`;
     }
     else if(e.type==='ASSESS'){
       q('farmViability').textContent=`${e.recentFood} / ${e.demandFood} kg over last 3 cycles · ${e.foodSustained?'food production covers demand':'not yet demonstrated'}`;
       q('farmViability').title='Does not certify self-sufficiency or quality of life. Only recent food production versus crew demand.';
     }else if(e.type==='STOCK_CLEAR')stocks.clear();
     else if(e.type==='STOCK')stocks.set(e.category==='TAILINGS'?'Tailings':e.category+' · '+e.name.replaceAll('_',' '),e.kg);
     else if(e.type==='ROCK')geology.set(e.id,{...e,minerals:new Map(geology.get(e.id)?.minerals||[])});
     else if(e.type==='MINERAL'){
       const rock=geology.get(e.id);if(!rock)throw Error('Mineral without scanned rock');rock.minerals.set(e.name,e.share);
     }else if(e.type==='SMALL_ROCK')small.set(e.id,e.remaining);
     else if(e.type==='LOCAL_DEPOSIT')localOreTaxonomy.set(e.id,e.classId);
     else if(e.type==='OASIS_EXTRACTOR'){
       q('signatureFeedback').textContent='Deployed oasis extractor at #'+e.body+
         ': '+e.energy+'/120 stored solar power units, '+e.workers+
         ' assigned onboard crew, condition '+e.condition+'/100.';
     }
     else if(e.type==='OASIS_INSTALLED'){
       q('signatureFeedback').textContent='Installed a physical two-module oasis extractor at #'+e.body+
         '. Requires actual crew, food, water, oxygen and solar charge before resource collection.';
     }
     else if(e.type==='OASIS_SOURCE'){
       q('signatureFeedback').textContent='Physical oasis reserves at body #'+e.body+
         ': '+e.waterKg.toLocaleString()+' kg water and '+
         e.nutrientsKg.toLocaleString()+' kg nutrients. These are finite deposits, not free food or oxygen.';
     }
     else if(e.type==='OASIS_COLLECTED'){
       q('signatureFeedback').textContent='Collected '+e.kg.toLocaleString()+
         ' kg '+e.material.toLowerCase()+' from real oasis #'+e.body+
         ' into the ship life-support inventory.';
     }
     else if(e.type==='EXPEDITION')expeditionAssessment=e;
     else if(e.type==='LOCAL_ENVIRONMENT'){
       const names=['','Stellar radiation zone','Debris region','Rocky planetary region','Outer icy planetary region','Lunar region'];
       q('signatureFeedback').textContent=names[e.zone]+
         ' · environmental severity '+e.hazard+'/3 (survey indicator only; not simulated damage).';
     }
     else if(e.type==='SCAN_DONE'){openingScanned=e.count>0;refreshOpening();}
     else if(e.type==='SAFETY_CHECK'||e.type==='SAFETY_ORDER'){
       const msg=e.type==='SAFETY_CHECK'?`Site ${e.site}: safety assessment requested; current runway shown above.`:
        `Site ${e.site}: ${e.decision.replaceAll('_',' ')} (risk ${e.risk}). ${e.decision==='REFUSED'?'Unsafe order refused and recorded.':'No production state changed.'}`;
       log.push(msg);q('colonyEvents').textContent=log.slice(-5).reverse().join('\n');
     }
     else if(e.type==='RESULT'||e.type==='ERROR'||e.type==='SCAN_DONE'||e.type==='NEW_COLONY'||e.type==='RESUMED'){
       if(e.type==='RESULT'&&combatTargets.size){
         q('combatFeedback').textContent='Latest native command #'+e.id+': '+e.outcome+
           ' · effect '+e.transferred+' (see training drone hull for confirmed damage).';
       }
       if(e.type==='RESULT'){
         window.GalaxyAssistNativeOutcome?.(e.outcome);
         const detail=window.GalaxyExpeditionFeedback?.diagnostic(e.outcome,e.transferred);
         if(detail)q('expeditionOutcome').textContent='Command #'+e.id+': '+detail;
       }else if(e.type==='ERROR'){
         q('expeditionOutcome').textContent='Host refused command input: '+e.error;
       }
       const msg=e.type==='RESULT'?`Command ${e.id}: ${e.outcome} (${e.transferred} t)`:
         e.type==='ERROR'?`Rejected ${e.error}`:e.type==='SCAN_DONE'?`Scan returned ${e.count} asteroids`:
         e.type==='RESUMED'?'Existing colony resumed':'New colony founded';
       log.push(msg);q('colonyEvents').textContent=log.slice(-5).reverse().join('\n');
     }
     const rows=[...geology.values()].sort((a,b)=>a.id-b.id).map(g=>{
       const items=[...g.minerals].sort((a,b)=>b[1]-a[1]).slice(0,4).map(([n,v])=>`${n.replaceAll('_','-')} ${v/10}%`).join(' · ');
       return `#${g.id} ${window.GalaxyColonyContract.names[g.classId]} · radius ${g.radius} m · density ${g.density} kg/m³ · ${g.remaining.toLocaleString()} tonnes remaining\n${items}`;
     });
     for(const [id,tax] of localOreTaxonomy){
       if(small.has(id))rows.push('#'+id+' · local '+
         window.GalaxyColonyContract.names[tax]+
         ' mineral profile · '+small.get(id)+' tonnes remaining');
     }
     q('colonyGeology').replaceChildren(...rows.map(x=>{const div=document.createElement('div');div.className='entry';div.textContent=x;div.style.whiteSpace='pre-wrap';return div;}));
     q('colonyStocks').textContent=[...stocks].map(([n,kg])=>`${n}: ${kg.toLocaleString()} kg`).join('\n')||'No separated mineral cargo yet.';
     const farmsOut=[...outposts].sort((a,b)=>a[0]-b[0]);
     const farmSelect=q('colonyFarmOutpost'), curFarm=farmSelect.value;
     farmSelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id,built])=>
       new Option(`#${id} ${built?'· greenhouse':'· no farm yet'}`,String(id))));
     if([...farmSelect.options].some(o=>o.value===curFarm))farmSelect.value=curFarm;
     const habSelect=q('colonyHabOutpost'),curHab=habSelect.value;
     habSelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>
       new Option(`#${id} ${habitats.has(id)?'· habitat built':'· no housing'}`,String(id))));
     if([...habSelect.options].some(o=>o.value===curHab))habSelect.value=curHab;
     const reclaimSelect=q('colonyReclaimerOutpost'),curReclaim=reclaimSelect.value;
     reclaimSelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>
       new Option(`#${id} ${reclaimers.has(id)?'· recycler built':'· no recycler'}`,String(id))));
     if([...reclaimSelect.options].some(o=>o.value===curReclaim))reclaimSelect.value=curReclaim;
     q('reclaimerTelemetry').textContent=[...reclaimers.values()].map(r=>
       `Outpost #${r.id}: reclaimed water ${r.reclaimed} kg · regenerated O₂ ${r.generated} kg · sludge ${r.sludge} kg · carbon residue ${r.carbon} kg · powered periods ${r.continuity}`).join('\n')||'Build a local reclamation unit at an occupied outpost.';
     const waterSelect=q('colonyWaterworksOutpost'),curWater=waterSelect.value;
     waterSelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>
       new Option(`#${id} ${waterworks.has(id)?'· waterworks built':'· no waterworks'}`,String(id))));
     if([...waterSelect.options].some(o=>o.value===curWater))waterSelect.value=curWater;
     q('waterworksTelemetry').textContent=[...waterworks.values()].map(p=>
       `Outpost #${p.id}: unpurified feed ${p.raw} kg · potable output ${p.potable} kg · retained brine ${p.brine} kg · continuous supported periods ${p.continuity}`).join('\n')||'Build waterworks next to an existing habitat and solar utility.';
     const clinicSelect=q('colonyClinicOutpost'),curClinic=clinicSelect.value;
     clinicSelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>
       new Option(`#${id} ${clinics.has(id)?'· clinic built':'· no clinic'}`,String(id))));
     if([...clinicSelect.options].some(o=>o.value===curClinic))clinicSelect.value=curClinic;
     q('clinicTelemetry').textContent=[...clinics.values()].map(c=>
       `Outpost #${c.id}: ${c.medics} medics · clean water ${c.water} kg · first-aid substrate ${c.substrate} kg · open cases ${c.active} · treated ${c.treated} · care continuity ${c.care} periods · interruptions ${c.shortages}`).join('\n')||'Build a first-aid clinic next to housing.';
     const communitySelect=q('colonyCommunityOutpost'),curCommunity=communitySelect.value;
     communitySelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>
       new Option(`#${id} ${communities.has(id)?'· hall built':'· no hall'}`,String(id))));
     if([...communitySelect.options].some(o=>o.value===curCommunity))communitySelect.value=curCommunity;
     q('communityTelemetry').textContent=[...communities.values()].map(c=>
       `Outpost #${c.id}: ${c.attendants} attendants · food ${c.food} kg · water ${c.water} kg · service continuity ${c.continuity} periods · fulfilled social visits ${c.visits} · missed service ${c.shortages}`).join('\n')||'Build a community hall next to housing.';
     const policySelect=q('colonyPolicyOutpost'),curPolicy=policySelect.value;
     policySelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>new Option(`#${id}`,String(id))));
     if([...policySelect.options].some(o=>o.value===curPolicy))policySelect.value=curPolicy;
     q('colonyPolicyTelemetry').textContent=[...policyAudits.values()].map(p=>{
       const names=['Maintenance','Healthcare','Community','Waterworks'];
       const levels=['None','Lax','Medium','Strict'];
       return `Outpost #${p.site} ${names[p.domain]}: ${levels[p.declared]} declared · ${p.inspectors}/${p.required} oversight staff · ${p.operational?'service verified':'no sustained service evidence'} · ${p.supported?'ASSURANCE SUPPORTED':'NOT ASSURED'}`;
     }).join('\n')||'No standing assurance policies configured.';
     const utSelect=q('colonyUtilityOutpost'),curUt=utSelect.value;
     utSelect.replaceChildren(new Option('Select an outpost',''),...farmsOut.map(([id])=>
       new Option(`#${id} ${utilities.has(id)?'· utility built':'· no utility'}`,String(id))));
     if([...utSelect.options].some(o=>o.value===curUt))utSelect.value=curUt;
     q('utilityTelemetry').textContent=[...utilities.values()].map(u=>
       `Outpost #${u.id}: ${u.powered?'powered':'OUTAGE'} · equipment ${u.condition}% · reserve ${u.stored}/120 · reliable cycles ${u.reliable} · outages ${u.outages} · repairs ${u.repairs} · local spares ${u.spares??0} modules · assigned techs ${u.technicians??0} · policy ${u.enabled?'ON':'OFF'} · automated repairs ${u.delegatedRepairs??0} · blocked cycles ${u.blocked??0}`).join('\n')||'Build a solar utility at your outpost.';
     q('habTelemetry').textContent=[...habitats.values()].map(h=>
       `Outpost #${h.id}: ${h.occupied}/${h.capacity} occupied (${h.residents} resident-only) · food ${h.food} kg · water ${h.water} kg · O₂ ${h.oxygen} kg · comfort cycles ${h.comfort} · shortages ${h.shortages}`).join('\n')||'Build a habitat at a player-owned outpost.';
     q('farmTelemetry').textContent=[...farms.values()].map(f=>
       `Outpost #${f.id}: ${f.workers} crew · food ${f.food} kg · water ${f.water} kg · O₂ ${f.oxygen} kg · harvest total ${f.harvested} kg · shortages ${f.shortages}`).join('\n')||'Build a greenhouse at a player-owned outpost.';
     const selected=q('colonyRockTarget'), current=selected.value;
     const targets=[
       ...[...geology.values()].map(rock=>({id:rock.id,name:window.GalaxyColonyContract.names[rock.classId]})),
       ...[...small].map(([id,remaining])=>({id,name:'Small asteroid · '+remaining+' t'}))
     ].sort((x,y)=>x.id-y.id);
     const droneSelect=q('droneTarget'),oldDrone=droneSelect.value;
     const geologicalTargets=[...geology.values()].filter(rock=>rock.remaining>0);
     droneSelect.replaceChildren(new Option('Choose geological asteroid',''),
       ...geologicalTargets.map(rock=>new Option('#'+rock.id+' · '+window.GalaxyColonyContract.names[rock.classId],String(rock.id))));
     if([...droneSelect.options].some(option=>option.value===oldDrone))droneSelect.value=oldDrone;
     q('droneLaunch').disabled=!probeLauncher||!probeCarried||!droneSelect.value;
     selected.replaceChildren(new Option('Select a scanned rock',''),...targets.map(rock=>
       new Option(`#${rock.id} · ${rock.name}`,String(rock.id))));
     if(window.GalaxySelectedRockId!==null&&!targets.some(rock=>rock.id===window.GalaxySelectedRockId))
       window.GalaxySelectedRockId=null;
     if([...selected.options].some(o=>o.value===current))selected.value=current;
   }
 };
 for(const [id,action,input] of [['colonyPolicySet','colonyPolicySet','colonyPolicyLevel'],['colonyPolicyStaff','colonyPolicyStaff','colonyPolicyStaffCount']]){
   q(id).addEventListener('click',()=>{
     const outpost=Number(q('colonyPolicyOutpost').value),domain=Number(q('colonyPolicyDomain').value),amount=Number(q(input).value);
     if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(domain)&&domain>=0&&domain<=3&&
        Number.isSafeInteger(amount)&&amount>=0&&amount<=3)
       window.GalaxyColonyCommand?.(action,{outpost,domain,amount});
   });
 }
 q('colonyWaterworksBuild').addEventListener('click',()=>{
   const outpost=Number(q('colonyWaterworksOutpost').value);
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyWaterworksBuild',{outpost});
 });
 q('colonyWaterworksFeed').addEventListener('click',()=>{
   const outpost=Number(q('colonyWaterworksOutpost').value),amount=Number(q('colonyWaterworksAmount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(amount)&&amount!==0&&
      amount%1000===0&&Math.abs(amount)<=10000)
     window.GalaxyColonyCommand?.('colonyWaterworksFeed',{outpost,amount});
 });
 q('colonyReclaimerBuild').addEventListener('click',()=>{
   const outpost=Number(q('colonyReclaimerOutpost').value);
   if(Number.isSafeInteger(outpost)&&outpost>0)
     window.GalaxyColonyCommand?.('colonyReclaimerBuild',{outpost});
 });
 const chosenClinic=()=>Number(q('colonyClinicOutpost').value);
 q('colonyClinicBuild').addEventListener('click',()=>{const outpost=chosenClinic();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyClinicBuild',{outpost});});
 q('colonyClinicStaff').addEventListener('click',()=>{
   const outpost=chosenClinic(),amount=Number(q('colonyClinicStaffCount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(amount)&&amount>=0&&amount<=4)
     window.GalaxyColonyCommand?.('colonyClinicStaff',{outpost,amount});});
 q('colonyClinicSupply').addEventListener('click',()=>{
   const outpost=chosenClinic(),resource=q('colonyClinicResource').value,
      amount=Number(q('colonyClinicAmount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&['water','substrate'].includes(resource)&&
      Number.isSafeInteger(amount)&&amount!==0&&amount>=-10000&&amount<=10000)
     window.GalaxyColonyCommand?.('colonyClinicStock',{outpost,resource,amount});});
 const chosenCommunity=()=>Number(q('colonyCommunityOutpost').value);
 q('colonyCommunityBuild').addEventListener('click',()=>{const outpost=chosenCommunity();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyCommunityBuild',{outpost});});
 q('colonyCommunityStaff').addEventListener('click',()=>{
   const outpost=chosenCommunity(),amount=Number(q('colonyCommunityStaffCount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(amount)&&amount>=0&&amount<=4)
     window.GalaxyColonyCommand?.('colonyCommunityStaff',{outpost,amount});});
 q('colonyCommunitySupply').addEventListener('click',()=>{
   const outpost=chosenCommunity(),resource=q('colonyCommunityResource').value,
      amount=Number(q('colonyCommunityAmount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&['food','water'].includes(resource)&&
      Number.isSafeInteger(amount)&&amount!==0&&amount>=-10000&&amount<=10000)
     window.GalaxyColonyCommand?.('colonyCommunityStock',{outpost,resource,amount});});
 const chosenUtility=()=>Number(q('colonyUtilityOutpost').value);
 q('colonyUtilityBuild').addEventListener('click',()=>{const outpost=chosenUtility();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyUtilityBuild',{outpost});});
 q('colonyUtilityRepair').addEventListener('click',()=>{const outpost=chosenUtility();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyUtilityRepair',{outpost});});
 for(const [control,action,input] of [
    ['colonyUtilitySpares','colonyUtilitySpares','colonyUtilitySpareAmount'],
    ['colonyUtilityStaff','colonyUtilityStaff','colonyUtilityTechnicianCount']
 ])q(control).addEventListener('click',()=>{
    const outpost=chosenUtility(),amount=Number(q(input).value);
    const valid=action==='colonyUtilitySpares'?amount!==0&&amount>=-32&&amount<=32:amount>=0&&amount<=8;
    if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(amount)&&valid)
      window.GalaxyColonyCommand?.(action,{outpost,amount});
 });
 for(const [id,amount] of [['colonyUtilityAutoOn',1],['colonyUtilityAutoOff',0]])
   q(id).addEventListener('click',()=>{const outpost=chosenUtility();
     if(Number.isSafeInteger(outpost)&&outpost>0)
       window.GalaxyColonyCommand?.('colonyUtilityAuto',{outpost,amount});});
 const chosenHab=()=>Number(q('colonyHabOutpost').value);
 q('colonyHabBuild').addEventListener('click',()=>{const outpost=chosenHab();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyHabBuild',{outpost});});
 q('colonyHabAssign').addEventListener('click',()=>{const outpost=chosenHab();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyHabResidents',{outpost,amount:1});});
 q('colonyHabRecall').addEventListener('click',()=>{const outpost=chosenHab();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyHabResidents',{outpost,amount:-1});});
 q('colonyHabSupply').addEventListener('click',()=>{const outpost=chosenHab(),resource=q('colonyHabResource').value,
   amount=Number(q('colonyHabAmount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(amount)&&amount!==0&&Math.abs(amount)<=10000)
     window.GalaxyColonyCommand?.('colonyHabStock',{outpost,resource,amount});});
 const chosenFarm=()=>Number(q('colonyFarmOutpost').value);
 q('colonyFarmBuild').addEventListener('click',()=>{const outpost=chosenFarm();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyFarmBuild',{outpost});});
 q('colonyFarmAssign').addEventListener('click',()=>{const outpost=chosenFarm();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyFarmStaff',{outpost,amount:1});});
 q('colonyFarmRecall').addEventListener('click',()=>{const outpost=chosenFarm();
   if(Number.isSafeInteger(outpost)&&outpost>0)window.GalaxyColonyCommand?.('colonyFarmStaff',{outpost,amount:-1});});
 q('colonyFarmSupply').addEventListener('click',()=>{const outpost=chosenFarm(),resource=q('colonyFarmResource').value,
    amount=Number(q('colonyFarmAmount').value);
   if(Number.isSafeInteger(outpost)&&outpost>0&&Number.isSafeInteger(amount)&&amount!==0&&Math.abs(amount)<=10000)
     window.GalaxyColonyCommand?.('colonyFarmStock',{outpost,resource,amount});});
 q('colonyMineTarget').addEventListener('click',()=>{
   const target=Number(q('colonyRockTarget').value);
   if(Number.isSafeInteger(target)&&target>0)window.GalaxyColonyCommand?.('colonyMineTarget',{target,amount:8});
 });
 q('colonyProcess').addEventListener('click',()=>{
   const kg=Number(q('colonyProcessKg').value);
   if(Number.isSafeInteger(kg)&&kg>=1&&kg<=100000)
     window.GalaxyColonyCommand?.('colonyProcess',{mineral:q('colonyPhase').value,kg});
 });
 q('colonyFabricateMaterial').addEventListener('click',()=>window.GalaxyColonyCommand?.('colonyFabricateMaterial',{material:q('colonyMaterial').value,amount:1}));
 for(const [id,action,amount] of [
    ['lifePlant','colonyPlant',20],['lifeHarvest','colonyHarvest',20],
    ['lifeRecycleWater','colonyRecycleWater',20],['lifeRecycleCompost','colonyRecycleCompost',20],
    ['lifeOxygenAction','colonyOxygen',5],['lifeLoadWater','colonyLifeWater',1]
 ])q(id).addEventListener('click',()=>window.GalaxyColonyCommand?.(action,{amount}));
 for(const [id,action,extra] of [
   ['colonyScan','colonyScan',{}],['colonyMine','colonyMine',{amount:8}],
   ['colonyRefine','colonyRefine',{amount:8}],['colonyFabricate','colonyFabricate',{amount:4}],
   ['colonyBuild','colonyBuild',{}],['colonyLeft','thrust',{x:-1,y:0}],
   ['colonyRight','thrust',{x:1,y:0}],['colonyUp','thrust',{x:0,y:-1}],
   ['colonyDown','thrust',{x:0,y:1}],['colonyDrift','thrust',{x:0,y:0}]
 ])q(id).addEventListener('click',()=>window.GalaxyColonyCommand?.(action,extra));
})();
