'use strict';
// Bounded diagnostic capture at the verified object-buffer read boundary.
// No PCM marker filtering, no deduplication by sample values, no data writes.
const mod=Process.mainModule;
if(mod.name.toLowerCase()!=='audiodg.exe')throw Error('audiodg only');
const addr=mod.base.add(CAPTURE_PROFILE.getter);
const hex=a=>Array.from(a).map(x=>x.toString(16).padStart(2,'0')).join('');
const original=hex(new Uint8Array(addr.readByteArray(32)));
if(!original.startsWith('48895c240848896c241048897424185741'))throw Error('Unexpected prologue');
// Observed Wwise stream has 140 slots: retain a bounded 160-slot ceiling.
const drainMs=typeof STREAM_DRAIN_MS==='number'?STREAM_DRAIN_MS:5;
const durationMs=typeof STREAM_DURATION_MS==='number'?STREAM_DURATION_MS:15000;
const maxObjects=160,maxFrames=4096,capacity=16,maxBytes=Math.max(512*1024*1024,durationMs*32000);
const kernel=Process.getModuleByName('kernel32.dll');
const qpc=new NativeFunction(kernel.getExportByName('QueryPerformanceCounter'),'bool',['pointer'],{scheduling:'exclusive',traps:'none'});
const qpf=new NativeFunction(kernel.getExportByName('QueryPerformanceFrequency'),'bool',['pointer'],{scheduling:'exclusive',traps:'none'});
const clock=Memory.alloc(8);qpf(clock);const frequency=clock.readS64().toNumber();
function timestamp(){qpc(clock);return clock.readS64().toNumber()*1e9/frequency;}
const slots=Array.from({length:capacity},()=>({data:Memory.alloc(maxObjects*(32+maxFrames*4))}));
let head=0,tail=0,depth=0,highWater=0,copies=0,hits=0,dropped=0,totalBytes=0,budgetRejected=0;
let stopped=false,listener,timer,stats,scheduler,schedulerStats;const errors=[];const blocks={};const layouts={};
function drain(){
 for(let n=0;depth&&n<4;n++){
  const s=slots[tail];s.record.sendQpcNs=timestamp();send({kind:'snapshot',record:s.record},s.data.readByteArray(s.bytes));
  tail=(tail+1)%capacity;depth--;
 }
}
function stop(){
 if(stopped)return stats;stopped=true;if(listener)listener.detach();Interceptor.flush();if(scheduler)schedulerStats=scheduler.stop();else clearInterval(timer);
 try{while(depth)drain();}catch(e){errors.push(String(e));}
 stats={schedulerStats,hits,copies,queueDropped:dropped,queueHighWater:highWater,queueRemaining:depth,totalBytes,budgetRejected,errors,blocks,layouts,
  hookBytesRestored:hex(new Uint8Array(addr.readByteArray(32)))===original};return stats;
}
listener=Interceptor.attach(addr,{
 onEnter(args){this.block=args[0].sub(0x590);},
 onLeave(){
  if(stopped)return;hits++;
  try{
   const b=this.block,vt=b.readPointer();
   if(!CAPTURE_PROFILE.vtables.some(rva=>vt.equals(mod.base.add(rva))))return;
   const count=b.add(0x5d4).readU32(),index=b.add(0x5d0).readU32();
   const window=b.add(0x5d8).readU8();
   const layoutKey=count+'/'+index+'/'+window;
   if(Object.keys(layouts).length<24||layouts[layoutKey])layouts[layoutKey]=(layouts[layoutKey]||0)+1;
   if(window!==1||count<1||count>maxObjects||index>1)return;
   const desc=b.add(0x510).readPointer(),stride=desc.add(20).readU32();
   const descKey='desc/'+desc.add(12).readU32()+'/'+stride;
   if(Object.keys(layouts).length<24||layouts[descKey])layouts[descKey]=(layouts[descKey]||0)+1;
   if(desc.add(12).readU32()!==32||stride<4||stride>maxFrames*4||stride%4)return;
   const props=desc.add(0x28+index*24).readPointer(),pcm=desc.add(0x38+index*24).readPointer();
   let bytes=count*32;const frames=[];
   for(let i=0;i<count;i++){
    const f=props.add(i*32+4).readU32();if(f>maxFrames||f*4>stride)return;
    frames.push(f);bytes+=f*4;
   }
   const key=b.toString(),sequence=blocks[key]||0;blocks[key]=sequence+1;
   if(totalBytes+bytes>maxBytes){budgetRejected++;return;}
   if(depth===capacity){dropped++;return;}
   const captureQpcNs=timestamp();
   const s=slots[head];Memory.copy(s.data,props,count*32);let offset=count*32;
   for(let i=0;i<count;i++){if(frames[i])Memory.copy(s.data.add(offset),pcm.add(i*stride),frames[i]*4);offset+=frames[i]*4;}
   if(b.add(0x5d8).readU8()!==1||b.add(0x5d0).readU32()!==index)return;
   s.bytes=bytes;s.record={block:key,sequence,ordinal:copies,bufferIndex:index,count,frames,stride,threadId:this.threadId,captureQpcNs,
    copyDoneQpcNs:timestamp(),pcmPointer:pcm.toString(),propertiesPointer:props.toString(),capturedUnixMs:Date.now(),scope:'unmarked-synchronous-owned-copy'};
   copies++;totalBytes+=bytes;head=(head+1)%capacity;depth++;highWater=Math.max(highWater,depth);
  }catch(e){if(errors.length<8)errors.push(String(e));}
 }
});
Interceptor.flush();
const tick=()=>{try{drain();}catch(e){if(errors.length<8)errors.push(String(e));}};
if(typeof STREAM_WAITABLE !== 'undefined' && STREAM_WAITABLE)scheduler=makeWaitableDrain(drainMs,tick);
else timer=setInterval(tick,drainMs);
rpc.exports={stop,health:()=>({queueDropped:dropped,budgetRejected,errors,copies})};setTimeout(()=>send({kind:'stopped',stats:stop()}),durationMs);
send({kind:'ready',pid:Process.id,address:addr.toString(),mode:'unmarked-bounded',capacity,maxObjects,maxFrames,maxBytes,drainMs});
