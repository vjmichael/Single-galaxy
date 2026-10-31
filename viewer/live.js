/* B4 local SSE adapter: one serialized SGVIEW frame per event. No gameplay writes. */
(function(){'use strict';
let source=null;
function start(){
  if(source){source.close();source=null;}
  const bridge=window.GalaxyLiveBridge;
  bridge.reset();
  const name=document.getElementById('scenario').value;
  source=new EventSource('/events?scenario='+encodeURIComponent(name));
  source.addEventListener('frame',e=>{
    try{
      const raw=atob(e.data); // SGVIEW is canonical ASCII numeric + LF
      bridge.accept(raw);
    }catch(ex){source.close();source=null;bridge.fail('Rejected live frame: '+ex.message);}
  });
  source.addEventListener('complete',()=>{bridge.complete();if(source){source.close();source=null;}});
  source.addEventListener('failed',e=>{bridge.fail('Live host failed: '+e.data);if(source){source.close();source=null;}});
  source.addEventListener('error',()=>{if(source){source.close();source=null;bridge.fail('Local live connection interrupted. Reload the page to start a new run.');}});
}
window.addEventListener('beforeunload',()=>{if(source)source.close();});
document.getElementById('scenario').addEventListener('change',start);
start();
})();
