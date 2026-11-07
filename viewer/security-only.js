/* Single-Galaxy owner-scoped security-only dashboard.
 * No owner ID, target sensor, judgment or force authority is client-supplied.
 * The trusted relay resolves the authenticated account cookie to the actual
 * original game owner, then the native host independently checks the hull.
 */
(function(){
'use strict';
const el=id=>document.getElementById(id);
const text=(id,value)=>{el(id).textContent=value;};
let csrf=null,source=null,account=null,closed=false;
const allowed=new Set([
 'colonySecurityRefresh','colonySecurityCall','colonySecurityCallReply'
]);
async function json(url,body,extra={}){
 const response=await fetch(url,Object.assign({
  method:'POST',headers:{'Content-Type':'application/json'},
  credentials:'same-origin',body:JSON.stringify(body)
 },extra));
 const data=await response.json();
 if(!response.ok)throw Error(data.error||'Authenticated request refused');
 return data;
}
function reset(){
 closed=true;
 if(source){source.close();source=null;}
 csrf=null;account=null;
 window.GalaxySecurityDesk?.reset();
 text('securityOwnAccount','');
 el('securityOwnerPanel').hidden=true;
 el('securityLogin').hidden=false;
 el('securityPassword').value='';
}
window.GalaxyColonyCommand=async function(action,args={}){
 if(!csrf || !allowed.has(action))return false;
 try{
  // UI schema is redundant, not authoritative; Python and native C++
  // validate exact payload, owner and physical ship independently.
  const data=await json('/command',Object.assign(
   {},args,{token:csrf,action}));
  if(data.status!=='queued_not_committed')throw Error('Unexpected result');
  return true;
 }catch(error){
  text('securityDeskFeedback','Action rejected: '+error.message);
  return false;
 }
};
function openStream(){
 if(source)source.close();
 source=new EventSource('/events',{withCredentials:true});
 source.addEventListener('colony',event=>{
  try{
   const binary=atob(event.data);
   if(!/^COLONY\|1\|[0-9]+\|SECURITY_/.test(binary))return;
   window.GalaxySecurityDesk?.accept(binary);
  }catch(_error){
   text('securityDeskFeedback','Invalid private security frame rejected.');
  }
 });
 source.addEventListener('failed',()=>reset());
}
async function bootstrap(){
 try{
  const response=await fetch('/session',{credentials:'same-origin'});
  if(!response.ok)throw Error('Security session unavailable');
  const data=await response.json();
  if(typeof data.token!=='string' ||
     !Array.isArray(data.commands) ||
     !['colonySecurityRefresh','colonySecurityCall',
        'colonySecurityCallReply'].every(v=>data.commands.includes(v)))
      throw Error('Native security commands not authorized');
  csrf=data.token;
  closed=false;
  el('securityLogin').hidden=true;
  el('securityOwnerPanel').hidden=false;
  text('securityOwnAccount','Authenticated security desk'+
       (account?' · '+account:''));
  window.GalaxySecurityDesk?.reset();
  openStream(); // native SECURITY_REFRESH on verified owner, SQLite-backed
 }catch(error){
  reset();
  text('securityLoginFeedback',error.message);
 }
}
el('securityLoginButton').addEventListener('click',async()=>{
 const username=el('securityUsername').value;
 const password=el('securityPassword').value;
 el('securityPassword').value='';
 try{
  const session=await json('/api/login',{username,password});
  if(session.ok!==true)throw Error('Account refused');
  account=session.username;
  await bootstrap();
 }catch(error){text('securityLoginFeedback',error.message);}
});
el('securityLogout').addEventListener('click',async()=>{
 const key=csrf;
 reset();
 if(key)try{
  await json('/api/logout',{}, {'headers':{
   'Content-Type':'application/json','X-SG-CSRF':key
  }});
 }catch(_error){}
});
bootstrap();
})();
