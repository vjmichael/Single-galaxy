'use strict';
// Local-host credentials. Accounts are provisioned by the game host operator
// (never through public signup or browser-provided player IDs).
const crypto = require('node:crypto');
const fs = require('node:fs');
const USER=/^[a-zA-Z][a-zA-Z0-9_.-]{2,31}$/;
const SETTINGS={N:16384,r:8,p:1,maxmem:32*1024*1024};
function hash(password,salt) {
 return new Promise((resolve,reject)=>crypto.scrypt(password,Buffer.from(salt,'hex'),64,SETTINGS,(err,derived)=>err?reject(err):resolve(derived.toString('hex'))));
}
async function account(username,playerId,password) {
 if(!USER.test(username)||!Number.isSafeInteger(playerId)||playerId<1||playerId>Number.MAX_SAFE_INTEGER||
  typeof password!=='string'||password.length<12||Buffer.byteLength(password)>512)throw Error('Invalid provisioned account');
 const salt=crypto.randomBytes(32).toString('hex');
 return {username,playerId,salt,hash:await hash(password,salt)};
}
function validate(accounts) {
 if(!accounts||accounts.version!==1||!Array.isArray(accounts.accounts)||accounts.accounts.length<1||accounts.accounts.length>64)throw Error('Invalid accounts manifest');
 const users=new Set(),ids=new Set();
 for(const entry of accounts.accounts){
  if(!entry||!USER.test(entry.username)||!Number.isSafeInteger(entry.playerId)||entry.playerId<1||
   !/^[0-9a-f]{64}$/.test(entry.salt||'')||!/^[0-9a-f]{128}$/.test(entry.hash||'')||
   users.has(entry.username)||ids.has(entry.playerId))throw Error('Invalid or duplicate provisioned account');
  users.add(entry.username);ids.add(entry.playerId);
 }
 return accounts.accounts;
}
function loadAccounts(file) {
 const stat=fs.lstatSync(file);
 if(!stat.isFile()||stat.size>32768)throw Error('Invalid account file');
 if(process.platform!=='win32' && (stat.mode & 0o077))throw Error('Accounts file permissions must be 0600');
 return validate(JSON.parse(fs.readFileSync(file,'utf8')));
}
async function verify(accounts,username,password) {
 if(typeof username!=='string'||!USER.test(username)||typeof password!=='string'||Buffer.byteLength(password)>512)return null;
 const row=accounts.find(x=>x.username===username);
 // Comparable scrypt work for known and unknown usernames prevents a trivial
 // username enumeration timing oracle; the password hash comparison is constant time.
 const salt=row?row.salt:'00'.repeat(32),expected=row?row.hash:'00'.repeat(64);
 const calculated=Buffer.from(await hash(password,salt),'hex');
 return crypto.timingSafeEqual(calculated,Buffer.from(expected,'hex'))&&row?row:null;
}
module.exports={account,validate,loadAccounts,verify};
