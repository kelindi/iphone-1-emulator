// Derived compiled artifacts only. Every use still needs fresh runtime admission.
export function createModuleCache({entries=2048,bytes=32*1024*1024}={}){
 if(!Number.isSafeInteger(entries)||entries<1||!Number.isSafeInteger(bytes)||bytes<1)throw Error('Invalid cache limits');
 const items=new Map(),stats={hits:0,misses:0,evictions:0,bytes:0,entries:0};
 function get(wat){
  if(typeof wat!=='string')throw Error('Cache key must be exact WAT');
  const entry=items.get(wat);
  if(!entry){stats.misses++;return undefined;}
  items.delete(wat);items.set(wat,entry);stats.hits++;return entry;
 }
 function put(wat,module,binaryBytes){
  if(typeof wat!=='string'||!(module instanceof WebAssembly.Module)||!Number.isSafeInteger(binaryBytes)||binaryBytes<1)throw Error('Invalid compiled artifact');
  // UTF-16 upper-bound string payload plus original binary size. Native engine
  // code/metadata is implementation-defined; this is the explicit accounting cap.
  const retained=wat.length*2+binaryBytes;
  const old=items.get(wat);if(old){items.delete(wat);stats.bytes-=old.retained;}
  if(retained>bytes){stats.entries=items.size;return false;}
  while(items.size>=entries||stats.bytes+retained>bytes){const key=items.keys().next().value;stats.bytes-=items.get(key).retained;items.delete(key);stats.evictions++;}
  items.set(wat,{module,binaryBytes,retained});stats.bytes+=retained;stats.entries=items.size;return true;
 }
 function clear(){items.clear();stats.bytes=0;stats.entries=0;}
 return {get,put,clear,stats};
}
