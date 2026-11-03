let disposeCurrent;
export function closeTerminal(){disposeCurrent?.();disposeCurrent=null;}
export function openTerminal(container,csrf,onClose){
  closeTerminal();
  const terminal=new window.Terminal({cursorBlink:true,convertEol:false,fontSize:15,scrollback:1500,theme:{background:'#172320',foreground:'#e5ede6'}});
  // FitAddon measures its direct parent without subtracting parent padding.
  const mount=document.createElement('div');mount.className='terminal-mount';container.replaceChildren(mount);
  const fit=new window.FitAddon.FitAddon();terminal.loadAddon(fit);terminal.open(mount);
  let disposed=false,frame;
  const fitTerminal=()=>{if(disposed)return;const size=fit.proposeDimensions();if(size)terminal.resize(Math.max(20,Math.min(240,size.cols)),Math.max(5,Math.min(100,size.rows)));};
  fitTerminal();document.fonts?.ready.then(fitTerminal);
  // Terminal output cannot change document title, open links, or write clipboard.
  terminal.parser.registerOscHandler(52,()=>true);
  terminal.parser.registerOscHandler(8,()=>true);
  const url=new URL('api/terminal',location.href);url.protocol=location.protocol==='https:'?'wss:':'ws:';
  const ws=new WebSocket(url,['liner-notes-v1','csrf.'+csrf]);ws.binaryType='arraybuffer';
  const encoder=new TextEncoder();
  const resize=()=>{if(ws.readyState===WebSocket.OPEN)ws.send(JSON.stringify({type:'resize',columns:terminal.cols,rows:terminal.rows}));};
  ws.onopen=()=>{resize();terminal.focus();};
  ws.onmessage=e=>{if(e.data instanceof ArrayBuffer)terminal.write(new Uint8Array(e.data));};
  ws.onclose=()=>{terminal.writeln('\r\n[Disconnected]');onClose?.();};
  terminal.onData(data=>{if(ws.readyState===WebSocket.OPEN)ws.send(encoder.encode(data));});
  terminal.onResize(resize);
  const observer=new ResizeObserver(()=>{cancelAnimationFrame(frame);frame=requestAnimationFrame(fitTerminal);});observer.observe(mount);
  disposeCurrent=()=>{disposed=true;cancelAnimationFrame(frame);observer.disconnect();ws.close();terminal.dispose();};
  return ()=>{if(ws.readyState===WebSocket.OPEN)ws.send(new Uint8Array([29]));};
}
