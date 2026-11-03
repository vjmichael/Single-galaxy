#!/usr/bin/env node
'use strict';
// Operator-only CLI. Reads passwords from a controlling terminal; no password
// on command line, environment, browser route, or logfile.
const fs=require('node:fs');
const readline=require('node:readline');
const {account,validate}=require('./shipping_auth');
async function main(){
 const arg=process.argv.slice(2),p=x=>{const i=arg.indexOf(x);return i<0?'':arg[i+1]||'';};
 const file=p('--file'),username=p('--username'),id=Number(p('--player-id'));
 if(!file||!username||!p('--player-id')||!process.stdin.isTTY){
  console.error('Usage: node scripts/shipping_accounts.js --file PRIVATE_ACCOUNTS.json --username USER --player-id ID (interactive terminal required)');
  process.exitCode=2;return;
 }
 const terminal=readline.createInterface({input:process.stdin,output:process.stderr,terminal:true});
 // Disables readline echo of the password as it is typed.
 const original=terminal._writeToOutput.bind(terminal);
 terminal._writeToOutput=function(s){if(s==='\r\n'||s==='\n')original(s);};
 process.stderr.write('Password (12+ characters): ');
 const password=await new Promise(resolve=>terminal.question('',resolve));terminal.close();
 const record=await account(username,id,password);
 let accounts={version:1,accounts:[]};
 if(fs.existsSync(file)){const stat=fs.lstatSync(file);if(!stat.isFile())throw Error('Account destination must be regular file');if(process.platform!=='win32'&&(stat.mode&0o077))throw Error('Insecure existing account permissions');accounts=JSON.parse(fs.readFileSync(file,'utf8'));validate(accounts);}
 if(accounts.accounts.some(x=>x.username===username||x.playerId===id))throw Error('Username or player ID already assigned');
 accounts.accounts.push(record);
 fs.writeFileSync(file,JSON.stringify(accounts,null,2)+'\n',{encoding:'utf8',flag:fs.existsSync(file)?'w':'wx',mode:0o600});
 if(process.platform!=='win32')fs.chmodSync(file,0o600);
 console.error('Provisioned player ID '+id+'; credentials file is host-private.');
}
main().catch(err=>{console.error('Account provisioning failed:',err.message);process.exitCode=1;});
