/* Single Galaxy ECON-02B2d4b: pure browser/Node blueprint editor contract.
 * Presentation never authorizes or executes a world construction command.
 * The canonical .sglayout is consumed by authoritative C++ FactoryLayout.
 * Walls, doors, and floors are design-only metadata until world code supports them.
 */
(function (root, make) {
  const exports = make();
  if (typeof module !== 'undefined' && module.exports) module.exports = exports;
  else root.GalaxyFactory = exports;
})(typeof globalThis === 'object' ? globalThis : this, function () {
  'use strict';
  const WORKSHOP = 4294967295;
  const ITEMS = ['good/aluminum', 'good/aluminum_frames', 'good/aluminum_panels', 'good/aluminum_scrap'];
  const OUTS = ['good/aluminum_frames', 'good/aluminum_panels', 'good/aluminum_scrap'];
  const MAX_COORD = 1000000; // host-side safety limit, NOT a factory-size cap
  const supported = new Set(ITEMS);
  const integer = v => Number.isSafeInteger(v);
  function check(ok, description) { if (!ok) throw new Error(description); }
  function coord(v) { check(integer(v) && Math.abs(v) <= MAX_COORD, 'Position must be an integer in the construction grid'); return v; }
  function point(x, y) { return {x: coord(x), y: coord(y)}; }
  const key = p => `${p.x},${p.y}`;
  const boxHas = (d, p) => p.x >= d.x && p.x < d.x + d.w && p.y >= d.y && p.y < d.y + d.h;
  function nextTo(d, p) {
    return !boxHas(d, p) && ((p.x === d.x - 1 || p.x === d.x + d.w) && p.y >= d.y && p.y < d.y + d.h ||
      (p.y === d.y - 1 || p.y === d.y + d.h) && p.x >= d.x && p.x < d.x + d.w);
  }
  const adjacent = (a, b) => Math.abs(a.x-b.x)+Math.abs(a.y-b.y) === 1;
  function edgeAt(x, y) {
    // The nearest grid edge, not the nearest cell centre. Aligned segments
    // share integer endpoints automatically, including corners and doorways.
    check(Number.isFinite(x) && Number.isFinite(y), 'Invalid pointer position');
    const dx = Math.abs(x-Math.round(x));
    const dy = Math.abs(y-Math.round(y));
    if (dy <= dx) return {axis:'h', x:coord(Math.floor(x)), y:coord(Math.round(y))};
    return {axis:'v', x:coord(Math.round(x)), y:coord(Math.floor(y))};
  }
  const edgeKey = e => `${e.axis}:${e.x}:${e.y}`;
  function endpoints(e) {return e.axis === 'h' ? [{x:e.x,y:e.y},{x:e.x+1,y:e.y}] : [{x:e.x,y:e.y},{x:e.x,y:e.y+1}];}
  function edgesBetween(a,b) {
    a=point(a.x,a.y);b=point(b.x,b.y);
    check(a.x===b.x || a.y===b.y,'Wall drag must be horizontal or vertical');
    const result=[];
    if(a.x===b.x){for(let y=Math.min(a.y,b.y);y<Math.max(a.y,b.y);y++)result.push({axis:'v',x:a.x,y});}
    else {for(let x=Math.min(a.x,b.x);x<Math.max(a.x,b.x);x++)result.push({axis:'h',x,y:a.y});}
    check(result.length<=4096,'Wall drag is too long; place multiple sections');
    return result;
  }
  function wallCrosses(a,b,walls) {
    // Cells a and b: crossed grid edge is the boundary between them.
    if (!adjacent(a,b)) return true;
    let edge;
    if(a.x!==b.x)edge={axis:'v',x:Math.max(a.x,b.x),y:a.y};
    else edge={axis:'h',x:a.x,y:Math.max(a.y,b.y)};
    return walls.some(w=>w.type==='wall' && edgeKey(w)===edgeKey(edge));
  }
  function overlap(a,b) {return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;}
  function itemAllowed(device, item, asSource) {
    if(device.kind==='storage')return true;
    return asSource ? OUTS.includes(item) : item==='good/aluminum';
  }
  const clone = v => JSON.parse(JSON.stringify(v));
  class Editor {
    constructor(snapshot) {
      this.devices=[];this.routes=[];this.walls=[];this.floors=[];
      this.history=[];this.future=[];
      if (snapshot) this.load(snapshot);
    }
    snapshot() { return {format:'single-galaxy-factory-design-v1', devices:clone(this.devices),routes:clone(this.routes), walls:clone(this.walls),floors:clone(this.floors)}; }
    _replace(data) { this.devices=clone(data.devices);this.routes=clone(data.routes);this.walls=clone(data.walls);this.floors=clone(data.floors); }
    _remember() {this.history.push(this.snapshot());if(this.history.length>80)this.history.shift();this.future=[];}
    undo() {if(!this.history.length)return false;this.future.push(this.snapshot());this._replace(this.history.pop());return true;}
    redo() {if(!this.future.length)return false;this.history.push(this.snapshot());this._replace(this.future.pop());return true;}
    clear() {this._remember();this._replace({devices:[],routes:[],walls:[],floors:[]});}
    nextStorageId() {let id=2;const ids=new Set(this.devices.map(d=>d.id));while(ids.has(id))id++;return id;}
    nextRouteId() {let id=101;const ids=new Set(this.routes.map(d=>d.id));while(ids.has(id))id++;return id;}
    addDevice(kind,x,y,requestedId=null) {
      point(x,y);check(['base','storage','workshop'].includes(kind),'Unknown construction device');
      const id=kind==='base'?1:kind==='workshop'?WORKSHOP:(requestedId===null?this.nextStorageId():requestedId);
      check(integer(id)&&id>=1&&id<=WORKSHOP&&((kind==='storage'&&id>1&&id<WORKSHOP)||(kind==='base'&&id===1)||(kind==='workshop'&&id===WORKSHOP)),'Invalid construction equipment ID');
      check(!this.devices.some(d=>d.id===id), 'Only one base storage and one workshop per plan');
      const w=kind==='workshop'?2:1;
      const candidate={id,kind:kind==='base'?'storage':kind,x,y,w,h:w};
      this._canPlace(candidate);this._remember();this.devices.push(candidate);this.devices.sort((a,b)=>a.id-b.id);return id;
    }
    _canPlace(d, ignored) {
      check(!this.devices.some(v=>v.id!==ignored && overlap(v,d)),'Equipment footprint collides with another device');
      check(!this.routes.some(r=>r.path.some(p=>boxHas(d,p))),'Equipment footprint collides with conveyor');
      // Bound checked for both corners, including machine width.
      point(d.x+d.w,d.y+d.h);
    }
    moveDevice(id,x,y) {
      point(x,y);const d=this.devices.find(v=>v.id===id);check(d,'Unknown device');
      check(!this.routes.some(r=>r.from===id||r.to===id),'Remove connected conveyors before moving equipment');
      const next={...d,x,y};this._canPlace(next,id);this._remember();Object.assign(d,next);
    }
    deviceAt(p) {return this.devices.find(d=>boxHas(d,p))||null;}
    routeAt(p) {return this.routes.find(r=>r.path.some(q=>q.x===p.x&&q.y===p.y))||null;}
    eraseCell(x,y) {
      const p=point(x,y),dev=this.deviceAt(p),route=this.routeAt(p),floor=this.floors.find(q=>q.x===x&&q.y===y);
      if(dev){this._remember();this.devices=this.devices.filter(d=>d.id!==dev.id);this.routes=this.routes.filter(r=>r.from!==dev.id&&r.to!==dev.id);return 'Equipment removed';}
      if(route){this._remember();this.routes=this.routes.filter(r=>r.id!==route.id);return 'Conveyor removed';}
      if(floor){this._remember();this.floors=this.floors.filter(q=>q!==floor);return 'Floor removed';}
      return 'Nothing at selected cell';
    }
    setFloor(x,y,present=true) {
      point(x,y);const old=this.floors.find(p=>p.x===x&&p.y===y);
      if(Boolean(old)===present)return false;
      this._remember();if(present)this.floors.push({x,y});else this.floors=this.floors.filter(p=>p!==old);return true;
    }
    setEdge(edge,type) {
      check(['h','v'].includes(edge.axis),'Invalid wall axis');point(edge.x,edge.y);check(type==='wall'||type==='door'||type==='erase','Invalid wall tool');
      const id=edgeKey(edge),old=this.walls.find(w=>edgeKey(w)===id);
      if((old?.type||'erase')===type)return false;
      if(type==='wall')this._checkWallRouteCollision([edge]);
      this._remember();this.walls=this.walls.filter(w=>edgeKey(w)!==id);
      if(type!=='erase')this.walls.push({...edge,type});return true;
    }
    setEdgeRun(start,end,type) {
      check(type==='wall'||type==='door'||type==='erase','Invalid wall mode');
      const edges=edgesBetween(start,end),ids=new Set(edges.map(edgeKey));
      if(!edges.length)return false;
      const changes=edges.some(e=>{const old=this.walls.find(w=>edgeKey(w)===edgeKey(e));return type==='erase'?Boolean(old):old?.type!==type;});
      if(!changes)return false;
      if(type==='wall')this._checkWallRouteCollision(edges);
      this._remember();this.walls=this.walls.filter(w=>!ids.has(edgeKey(w)));
      if(type!=='erase')this.walls.push(...edges.map(e=>({...e,type})));return true;
    }
    _checkWallRouteCollision(edges) {
      const ids=new Set(edges.map(edgeKey));
      for(const r of this.routes){
        for(let i=1;i<r.path.length;i++){
          const a=r.path[i-1],b=r.path[i];
          const e=a.x!==b.x?{axis:'v',x:Math.max(a.x,b.x),y:a.y}:{axis:'h',x:a.x,y:Math.max(a.y,b.y)};
          check(!ids.has(edgeKey(e)), 'Wall would cut through an existing conveyor; place a doorway instead');
        }
      }
    }
    _validateRoute(from,to,item,rate,path,id=null) {
      check(integer(from)&&integer(to)&&from!==to,'Distinct equipment endpoints required');
      if(id!==null)check(integer(id)&&id>=1&&id<=Number.MAX_SAFE_INTEGER&&!this.routes.some(r=>r.id===id),'Duplicate or invalid route ID');
      const a=this.devices.find(d=>d.id===from),b=this.devices.find(d=>d.id===to);
      check(a&&b,'Route endpoints must be installed in the blueprint');
      check(supported.has(item)&&itemAllowed(a,item,true)&&itemAllowed(b,item,false),'Item incompatible with equipment port');
      check(integer(rate)&&rate>=1&&rate<=1000,'Conveyor rate must be 1–1000 units/tick');
      check(Array.isArray(path)&&path.length>=1&&path.length<=4096,'Conveyor requires 1–4096 adjacent cells');
      check(nextTo(a,path[0])&&nextTo(b,path[path.length-1]),'Conveyor must begin and end next to equipment');
      const seen=new Set();
      for(let i=0;i<path.length;i++){
        const p=point(path[i].x,path[i].y),k=key(p);
        check(!seen.has(k),'Conveyor cannot cross itself');seen.add(k);
        check(!this.devices.some(d=>boxHas(d,p)),'Conveyor runs through installed equipment');
        check(!this.routes.some(r=>r.id!==id&&r.path.some(v=>v.x===p.x&&v.y===p.y)),'Conveyor tile already occupied');
        if(i)check(adjacent(path[i-1],p),'Conveyor route must be orthogonally connected');
        if(i)check(!wallCrosses(path[i-1],p,this.walls),'Conveyor crosses a planned wall without a doorway');
      }
      // C++ FactoryLayout prevents directed cycles through its route graph.
      const graph=new Map();for(const r of this.routes){if(r.id===id)continue;const links=graph.get(r.from)||[];links.push(r.to);graph.set(r.from,links);}
      const todo=[to],visited=new Set();while(todo.length){const node=todo.pop();if(node===from)throw new Error('Directed conveyor cycle not permitted');if(visited.has(node))continue;visited.add(node);todo.push(...(graph.get(node)||[]));}
      return {id:id||this.nextRouteId(),from,to,item,rate,path:clone(path)};
    }
    addRoute(from,to,item,rate,path,id=null) {const c=this._validateRoute(from,to,item,rate,path,id);this._remember();this.routes.push(c);return c.id;}
    // UI helper: shortest bounded route through open cells, no occupancy or wall crossing.
    autoRoute(from,to,item,rate=7) {
      const a=this.devices.find(d=>d.id===from),b=this.devices.find(d=>d.id===to);
      check(a&&b&&from!==to,'Select two different devices');
      const starts=[],goals=new Set(),occupied=new Set();
      const neighbors=d=>{const cells=[];for(let x=d.x;x<d.x+d.w;x++){cells.push({x,y:d.y-1},{x,y:d.y+d.h});}for(let y=d.y;y<d.y+d.h;y++){cells.push({x:d.x-1,y},{x:d.x+d.w,y});}return cells;};
      const passable=p=>!this.devices.some(d=>boxHas(d,p))&&!this.routes.some(r=>r.path.some(q=>key(q)===key(p)));
      for(const s of neighbors(a))if(passable(s))starts.push(s);
      for(const s of neighbors(b))if(passable(s))goals.add(key(s));
      check(starts.length&&goals.size,'No free conveyor port beside equipment');
      const q=[...starts],prev=new Map();for(const s of starts)prev.set(key(s),null);
      let ending=null,first=0;
      while(first<q.length&&q.length<12000){const p=q[first++];if(goals.has(key(p))){ending=p;break;}
        for(const n of [{x:p.x+1,y:p.y},{x:p.x-1,y:p.y},{x:p.x,y:p.y+1},{x:p.x,y:p.y-1}]){
          if(Math.abs(n.x-a.x)+Math.abs(n.y-a.y)>100 || !passable(n)||prev.has(key(n))||wallCrosses(p,n,this.walls))continue;
          prev.set(key(n),p);q.push(n);
        }
      }
      check(ending,'No valid path within 100 cells of source; reposition or add an intermediate container');
      const path=[];for(let p=ending;p;p=prev.get(key(p)))path.push(p);path.reverse();
      return this.addRoute(from,to,item,rate,path);
    }
    canonical() {
      let out='single-galaxy-factory-layout-v1\n';
      for(const d of [...this.devices].sort((a,b)=>a.id-b.id)){
        const workshop=d.kind==='workshop';
        out+=`D|${d.id}|${workshop?1:0}|${workshop?1:0}|${d.x}|${d.y}|${d.w}|${d.h}|${workshop?1000:4000}|${workshop?'good/aluminum':''}|${workshop?OUTS.join(','):''}|\n`;
      }
      for(const r of [...this.routes].sort((a,b)=>a.id-b.id)){
        out+=`C|${r.id}|${r.from}|${r.to}|${r.item}|${r.rate}|${r.path.map(p=>`${p.x}:${p.y}`).join(';')}\n`;
      }
      return out;
    }
    audit() {
      const hasBase=this.devices.some(d=>d.id===1);
      const hasWorkshop=this.devices.some(d=>d.id===WORKSHOP);
      const errors=[];
      if(!hasBase)errors.push('Place base storage before exporting a build-ready layout');
      if(this.devices.length>256||this.routes.length>256)errors.push('Split into multiple plans (max 256 devices and 256 routes per construction plan)');
      const jobs=(hasBase?1:0)+this.devices.filter(d=>d.id!==1&&d.kind==='storage').length+(hasWorkshop?1:0)+this.devices.length+this.routes.reduce((s,r)=>s+r.path.length+2,0);
      if(jobs>10000)errors.push('Split this plan into batches of at most 10,000 construction steps');
      const materials=(hasBase?1:0)+this.devices.filter(d=>d.id!==1&&d.kind==='storage').length+(hasWorkshop?1:0)+this.routes.reduce((s,r)=>s+r.path.length+1,0);
      return {valid:errors.length===0,errors,jobs,modules:materials,devices:this.devices.length,conveyors:this.routes.length,segments:this.routes.reduce((s,r)=>s+r.path.length,0),walls:this.walls.length,floors:this.floors.length};
    }
    loadCanonical(text) {
      check(typeof text==='string'&&text.length<=3000000,'Blueprint must be a bounded text file');
      const lines=text.split('\n');
      check(lines.shift()==='single-galaxy-factory-layout-v1'&&lines.pop()==='','Unsupported or malformed native blueprint');
      const trial=new Editor(),pending=[];
      const natural=(s)=>{check(/^(0|[1-9]\d*)$/.test(s),'Noncanonical unsigned integer');const v=Number(s);check(integer(v),'Integer outside safe range');return v;};
      const signed=(s)=>{check(/^(0|-?[1-9]\d*)$/.test(s),'Noncanonical signed integer');const v=Number(s);check(integer(v),'Integer outside safe range');return v;};
      for(const line of lines){
        const f=line.split('|');
        if(f[0]==='D'){
          check(f.length===12,'Invalid native device record');
          const id=natural(f[1]),kind=natural(f[2]),tier=natural(f[3]);
          const x=signed(f[4]),y=signed(f[5]),w=natural(f[6]),h=natural(f[7]),cap=natural(f[8]);
          if(kind===0){check(tier===0&&w===1&&h===1&&cap===4000&&f[9]===''&&f[10]===''&&f[11]==='', 'Unsupported native storage geometry');
            trial.addDevice(id===1?'base':'storage',x,y,id);
          }else if(kind===1){check(id===WORKSHOP&&tier===1&&w===2&&h===2&&cap===1000&&f[9]==='good/aluminum'&&f[10]===OUTS.join(',')&&f[11]==='','Unsupported native machine geometry');
            trial.addDevice('workshop',x,y,id);
          }else throw new Error('Native device is not supported by this editor');
        } else if(f[0]==='C'){
          check(f.length===7,'Invalid native conveyor record');
          const path=f[6].split(';').map(value=>{const pair=value.split(':');check(pair.length===2,'Invalid conveyor coordinates');return {x:signed(pair[0]),y:signed(pair[1])};});
          pending.push({id:natural(f[1]),from:natural(f[2]),to:natural(f[3]),item:f[4],rate:natural(f[5]),path});
        } else throw new Error('Unknown native blueprint record');
      }
      for(const r of pending)trial.addRoute(r.from,r.to,r.item,r.rate,r.path,r.id);
      check(trial.canonical()===text,'Native blueprint is not canonical');
      this._remember();this._replace(trial.snapshot());return this;
    }
    exportDesign() {return JSON.stringify({...this.snapshot(),canonical:this.canonical()},null,2)+'\n';}
    load(json) {
      const data=typeof json==='string'?JSON.parse(json):clone(json);
      check(data && data.format==='single-galaxy-factory-design-v1','Unsupported factory design file');
      check(Array.isArray(data.devices)&&Array.isArray(data.routes)&&Array.isArray(data.walls)&&Array.isArray(data.floors),'Malformed design arrays');
      check(data.devices.length<=256&&data.routes.length<=256&&data.walls.length<=100000&&data.floors.length<=100000,'Design exceeds safe file limits');
      const trial=new Editor();
      for(const d of data.devices){
        check(d&&d.w===(d.kind==='workshop'?2:1)&&d.h===d.w,'Invalid device geometry');
        const id=trial.addDevice(d.id===1?'base':d.kind,d.x,d.y,d.id);
        check(id===d.id,'Invalid or out-of-order equipment ID');
      }
      for(const w of data.walls){
        check(w&&['wall','door'].includes(w.type),'Invalid wall record');
        trial.setEdge({axis:w.axis,x:w.x,y:w.y},w.type);
      }
      for(const p of data.floors){check(p&&integer(p.x)&&integer(p.y),'Invalid floor record');trial.setFloor(p.x,p.y);}
      for(const r of data.routes){check(r&&integer(r.id),'Invalid conveyor ID');trial.addRoute(r.from,r.to,r.item,r.rate,r.path,r.id);}
      if(data.canonical!==undefined)check(data.canonical===trial.canonical(),'Blueprint canonical record does not match design');
      this._remember();this._replace(trial.snapshot());return this;
    }
  }
  return Object.freeze({Editor,WORKSHOP,ITEMS,edgeAt,edgeKey,endpoints,edgesBetween,point});
});
