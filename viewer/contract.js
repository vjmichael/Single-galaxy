/* B4 visual replay: strict read-only B3 SGVIEW v1 parser and state validator.
   Runs in a browser without packages and under Node for regression tests.
   It intentionally does not grant client-side authority over the simulation. */
(function (root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.GalaxyContract = api;
})(typeof window !== 'undefined' ? window : globalThis, function () {
  'use strict';
  const MAX_BYTES = 4 * 1024 * 1024;
  const MAX_CHANGES = 60000;
  const UINT64_MASK = (1n << 64n) - 1n;
  const outcomes = ['APPLIED', 'INVALID_ACTOR', 'UNAUTHORIZED', 'INVALID_TARGET',
    'OUT_OF_RANGE', 'INVALID_ARGUMENT', 'NO_RESOURCES', 'CARGO_FULL', 'NOT_MINING',
    'NOT_IN_SPACE', 'NOT_DOCKED', 'IN_TRANSITION', 'SHIP_MOVING', 'HANGAR_FULL',
    'INSUFFICIENT_FUNDS','WRONG_SYSTEM','GATE_OFFLINE','NOT_COLONY_SHIP','INSUFFICIENT_CREW'];
  const kinds = ['ASTEROID_DESPAWNED', 'AUTO_MINING_TRANSFER', 'MINING_STOPPED',
    'UNDOCKED', 'DOCKED','JUMP_COMPLETED'];

  function integer(value, signed = false) {
    const pattern = signed ? /^(?:0|-?[1-9][0-9]*)$/ : /^(?:0|[1-9][0-9]*)$/;
    if (!pattern.test(value)) throw new Error('Non-canonical numeric field: ' + value.slice(0, 24));
    const n = Number(value);
    if (!Number.isSafeInteger(n)) throw new Error('Number exceeds safe browser integer range');
    return n;
  }
  function uint64(value) {
    if (!/^(?:0|[1-9][0-9]*)$/.test(value)) throw new Error('Bad unsigned 64-bit integer');
    const n = BigInt(value);
    if (n > UINT64_MASK) throw new Error('Unsigned 64-bit overflow');
    return n;
  }
  function lineFields(line, count) {
    const f = line.split('|');
    if (f.length !== count) throw new Error('Wrong field count for ' + f[0]);
    return f;
  }
  function decodeFrames(text) {
    if (typeof text !== 'string' || !text.length || text.includes('\r') || text.includes('\0') ||
        !text.endsWith('\n')) throw new Error('Expected canonical SGVIEW text with LF endings');
    const lines = text.split('\n');
    lines.pop();
    const frames = [];
    let i = 0;
    while (i < lines.length) {
      const h = lineFields(lines[i++], 9);
      if (h[0] !== 'SGVIEW' || h[1] !== '1') throw new Error('Not an SGVIEW v1 recording');
      const frame = {
        kind: integer(h[2]), stream: uint64(h[3]).toString(), sequence: integer(h[4]),
        tick: integer(h[5]), system: integer(h[6]), hash: uint64(h[7]).toString(),
        changes: []
      };
      const n = integer(h[8]);
      if (![1, 2].includes(frame.kind) || n > MAX_CHANGES || frame.system === 0 || frame.stream === '0')
        throw new Error('Invalid frame metadata');
      let size = lines[i - 1].length + 1;
      for (let j = 0; j < n; ++j) {
        if (i >= lines.length) throw new Error('Truncated recording');
        const line = lines[i++]; size += line.length + 1;
        if (size > MAX_BYTES) throw new Error('Oversize frame');
        const token = line.split('|')[0];
        let c;
        if (token === 'E') {
          const f = lineFields(line, 12);
          const v = f.slice(1).map((x, k) => integer(x, k >= 4 && k <= 7));
          c = {type: 'E', id: v[0], system: v[1], entityType: v[2], owner: v[3],
            x: v[4], y: v[5], vx: v[6], vy: v[7], ore: v[8], cargo: v[9], capacity: v[10]};
        } else if (token === 'D' || token === 'X') {
          const f = lineFields(line, 2);
          c = {type: token, id: integer(f[1])};
        } else if (token === 'J') {
          const f = lineFields(line, 5);
          c = {type: 'J', id: integer(f[1]), target: integer(f[2]),
            amount: integer(f[3]), command: integer(f[4])};
        } else if (token === 'Q') {
          const f=lineFields(line,5);
          c={type:'Q',id:integer(f[1]),state:integer(f[2]),station:integer(f[3]),remaining:integer(f[4])};
        } else if (token === 'Z') {
          const f=lineFields(line,2); c={type:'Z',id:integer(f[1])};
        } else if (token === 'K') {
          const f=lineFields(line,5);
          c={type:'K',id:integer(f[1]),state:integer(f[2]),gate:integer(f[3]),remaining:integer(f[4])};
        } else if (token === 'L') {
          const f=lineFields(line,2);c={type:'L',id:integer(f[1])};
        } else if (token === 'H') {
          const f=lineFields(line,5);
          c={type:'H',station:integer(f[1]),owner:integer(f[2]),ore:integer(f[3]),capacity:integer(f[4])};
        } else if (token === 'V') {
          const f=lineFields(line,3); c={type:'V',station:integer(f[1]),owner:integer(f[2])};
        } else if (token === 'W') {
          const f=lineFields(line,3);
          c={type:'W',owner:integer(f[1]),credits:integer(f[2])};
        } else if (token === 'Y') {
          const f=lineFields(line,2);c={type:'Y',owner:integer(f[1])};
        } else if (token === 'M') {
          const f=lineFields(line,5);
          c={type:'M',station:integer(f[1]),ore:integer(f[2]),credits:integer(f[3]),price:integer(f[4])};
        } else if (token === 'N') {
          const f=lineFields(line,2);c={type:'N',station:integer(f[1])};
        } else if (token === 'C') {
          const f = lineFields(line, 5);
          c = {type: 'C', tick: integer(f[1]), command: integer(f[2]),
            result: integer(f[3]), transferred: integer(f[4], true)};
        } else if (token === 'A') {
          const f = lineFields(line, 8);
          c = {type: 'A', tick: integer(f[1]), kind: integer(f[2]), command: integer(f[3]),
            actor: integer(f[4]), target: integer(f[5]), result: integer(f[6]),
            transferred: integer(f[7], true)};
        } else throw new Error('Unknown record type');
        frame.changes.push(c);
      }
      if (lines[i++] !== 'END') throw new Error('Missing END marker');
      size += 4;
      if (size > MAX_BYTES) throw new Error('Oversize frame');
      frames.push(frame);
    }
    if (!frames.length) throw new Error('Empty recording');
    return frames;
  }
  function hashBytes(value) {
    const bytes = new TextEncoder().encode(value);
    let h = 14695981039346656037n;
    for (const byte of bytes) h = ((h ^ BigInt(byte)) * 1099511628211n) & UINT64_MASK;
    return h.toString();
  }
  function hashProjection(s) {
    let canon = s.system + '|' + s.tick + '\n';
    for (const e of [...s.entities.values()].sort((a, b) => a.id - b.id)) {
      canon += ['E',e.id,e.system,e.entityType,e.owner,e.x,e.y,e.vx,e.vy,e.ore,e.cargo,e.capacity].join('|')+'\n';
    }
    for (const j of [...s.mining.entries()].sort((a,b)=>a[0]-b[0])) {
      canon += ['J',j[0],j[1].target,j[1].amount,j[1].command].join('|')+'\n';
    }
    for (const [id,d] of [...s.docks.entries()].sort((a,b)=>a[0]-b[0])) {
      canon+=['Q',id,d.state,d.station,d.remaining].join('|')+'\n';
    }
    for (const [id,j] of [...(s.jumps||new Map()).entries()].sort((a,b)=>a[0]-b[0])) {
      canon+=['K',id,j.state,j.gate,j.remaining].join('|')+'\n';
    }
    for (const h of [...s.hangars.values()].sort((a,b)=>a.station-b.station||a.owner-b.owner)) {
      canon+=['H',h.station,h.owner,h.ore,h.capacity].join('|')+'\n';
    }
    for(const [owner,credits] of [...(s.wallets||new Map()).entries()].sort((a,b)=>a[0]-b[0])) {
      canon+=['W',owner,credits].join('|')+'\n';
    }
    for(const m of [...(s.markets||new Map()).values()].sort((a,b)=>a.station-b.station)) {
      canon+=['M',m.station,m.ore,m.credits,m.price].join('|')+'\n';
    }
    return hashBytes(canon);
  }
  function validateState(s) {
    if (!s.system || s.entities.size > 100000 || s.mining.size > 100000) throw new Error('Invalid world state');
    for (const [id,e] of s.entities) {
      if (id <= 0 || e.id !== id || e.system !== s.system || e.entityType < 0 || e.entityType > 4 ||
          e.ore < 0 || e.cargo < 0 || e.capacity < 0) throw new Error('Invalid entity');
      // The native owner-scoped projection masks an observed foreign hull's owner to 0
      // and always removes its cargo. Zero owner is NOT an authority to control it.
      if (e.entityType === 0 && (!e.capacity || e.cargo > e.capacity || e.ore !== 0 ||
          (e.owner === 0 && e.cargo !== 0))) throw new Error('Invalid ship');
      if (e.entityType === 1 && (e.owner || e.cargo || e.capacity)) throw new Error('Invalid asteroid');
      // Entity type 4 is an actual recoverable wreck, not a vanished ship.
      if (e.entityType === 4 && (!e.owner || !e.capacity || e.ore !== 0 ||
          e.cargo > e.capacity)) throw new Error('Invalid wreck');
      if ((e.entityType === 2 || e.entityType === 3) && ((e.entityType === 3 && e.owner) || e.ore || e.cargo || e.capacity)) throw new Error('Invalid station');
    }
    if (s.docks.size > s.entities.size || s.hangars.size > 100000) throw new Error('Invalid station state size');
    for (const [shipId, d] of s.docks) {
      if (s.entities.get(shipId)?.entityType!==0 || s.entities.get(d.station)?.entityType!==2 ||
          ![1,2,3].includes(d.state) || (d.state===1 && d.remaining!==0) ||
          (d.state!==1 && d.remaining<=0)) throw new Error('Invalid docking telemetry');
    }
    for (const [id,j] of (s.jumps||new Map())) {
      if(s.entities.get(id)?.entityType!==0 || ![1,2].includes(j.state) ||
          j.remaining<=0 || (j.state===1 && (j.gate<=0 || s.entities.get(j.gate)?.entityType!==3)) ||
          (j.state===2 && j.gate!==0)) throw Error('Invalid jump state');
    }
    for (const h of s.hangars.values()) {
      if (s.entities.get(h.station)?.entityType!==2 || h.owner<=0 || h.capacity<=0 ||
          h.ore<0 || h.ore>h.capacity) throw new Error('Invalid hangar telemetry');
    }
    if (s.wallets?.size > 100000 || s.markets?.size > 100000) throw new Error('Excessive accounts');
    for (const [owner,credits] of (s.wallets||new Map())) {
      if(owner<=0 || credits<0) throw new Error('Invalid wallet');
    }
    for (const m of (s.markets||new Map()).values()) {
      if(s.entities.get(m.station)?.entityType!==2 || m.ore<0 || m.credits<0 || m.price<=0)
        throw new Error('Invalid market');
    }
    for (const [shipId, j] of s.mining) {
      if (s.entities.get(shipId)?.entityType !== 0 || s.entities.get(j.target)?.entityType !== 1 ||
          j.amount <= 0 || j.command <= 0) throw new Error('Invalid mining job');
    }
  }
  function applyNext(previous, f) {
    let state = previous;
      if (f.kind === 1) {
        if (state && f.stream === state.stream && f.sequence < state.sequence)
          throw new Error('Stale snapshot');
        state = {stream: f.stream, system: f.system, sequence: f.sequence, tick: f.tick,
          entities: new Map(), mining: new Map(), docks:new Map(), jumps:new Map(), hangars:new Map(),wallets:new Map(),markets:new Map(),
          notifications: [], removals: []};
      } else {
        if (!state || f.stream !== state.stream || f.system !== state.system ||
            f.sequence !== state.sequence + 1 || f.tick !== state.tick + 1)
          throw new Error('Missing or out-of-order delta');
        state = {...state, sequence: f.sequence, tick: f.tick,
          entities: new Map(state.entities), mining: new Map(state.mining),
          docks:new Map(state.docks),jumps:new Map(state.jumps||[]),hangars:new Map(state.hangars),
          wallets:new Map(state.wallets),markets:new Map(state.markets),notifications: [], removals: []};
      }
      for (const c of f.changes) {
        if (c.type === 'E') state.entities.set(c.id, c);
        else if (c.type === 'D') {
          if (!state.entities.delete(c.id)) throw new Error('Removal of unknown entity');
          state.removals.push(c.id);
        } else if (c.type === 'Q') state.docks.set(c.id,c);
        else if (c.type === 'Z') {
          if (!state.docks.delete(c.id)) throw new Error('Removal of unknown dock record');
        } else if (c.type === 'K') state.jumps.set(c.id,c);
        else if (c.type === 'L') {
          if(!state.jumps.delete(c.id))throw Error('Removal of unknown jump');
        } else if (c.type === 'H') state.hangars.set(c.station+':'+c.owner,c);
        else if (c.type === 'V') {
          if(!state.hangars.delete(c.station+':'+c.owner)) throw new Error('Removal of unknown hangar');
        } else if (c.type === 'W') state.wallets.set(c.owner,c.credits);
        else if (c.type === 'Y') {
          if(!state.wallets.delete(c.owner)) throw new Error('Removal of unknown wallet');
        } else if (c.type === 'M') state.markets.set(c.station,c);
        else if (c.type === 'N') {
          if(!state.markets.delete(c.station)) throw new Error('Removal of unknown market');
        } else if (c.type === 'J') state.mining.set(c.id, c);
        else if (c.type === 'X') {
          if (!state.mining.delete(c.id)) throw new Error('Removal of unknown mining job');
        } else {
          if (f.kind !== 2 || c.tick !== f.tick) throw new Error('Invalid event in snapshot');
          if (c.type === 'C' && (c.result < 0 || c.result >= outcomes.length)) throw new Error('Unknown result');
          if (c.type === 'A' && (c.kind < 0 || c.kind >= kinds.length ||
              c.result < 0 || c.result >= outcomes.length)) throw new Error('Unknown activity');
          state.notifications.push(c);
        }
      }
      validateState(state);
      if (hashProjection(state) !== f.hash) throw new Error('Frame projection hash mismatch');
    return state;
  }
  function applyFrames(frames) {
    let state=null;
    const timeline=[];
    for(const f of frames){state=applyNext(state,f);timeline.push(state);}
    return timeline;
  }
  return {decodeFrames, applyFrames, applyNext, hashProjection, outcomes, kinds};
});
