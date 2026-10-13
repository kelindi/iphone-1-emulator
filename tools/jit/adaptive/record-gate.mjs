// Separate visualization of actual fixed-work CPU replay. Never benchmark data.
import {readFile,writeFile,mkdir,stat} from 'node:fs/promises';
import {resolve,dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
import {deflateSync,gzipSync} from 'node:zlib';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import ffmpeg from 'ffmpeg-static';
import wabtFactory from 'wabt';
import {createWorkloadCompiler} from './workload-compiler.mjs';
import {workloads,nextBoundary} from './workloads.mjs';

const out=resolve(process.env.ARTIFACT_DIR||'.context/adaptive-compiler/first-gate-recording');
const root=resolve(process.env.RUNTIME_ROOT||'.context/adaptive-compiler/workload-refresh-runtime');
const baseline=resolve('.context/adaptive-compiler/baseline-final');
const gatePath=resolve(process.env.GATE_REPORT||'.context/adaptive-compiler/workload-abba-first/report.json');
const video=resolve(process.env.VIDEO_PATH||out+'/compiler_first_gate.mp4');
const thumbnail=resolve(process.env.THUMBNAIL_PATH||out+'/compiler_first_gate.jpg');
await mkdir(out); // Never overwrite a recording attempt.
const hash=(b,algorithm='sha256')=>createHash(algorithm).update(b).digest('hex');
const report={passed:false,scope:'Independent visualization of frozen Settings first100M scheduler credits, A then B. Actual emu_frame pixels at observed wall-clock pace. NOT ABBA measurement, browser latency or Mac performance.',method:'Same frozen inputs and1M scheduler-credit chunks. Frame capture, PNG encoding and recording I/O overhead remain in displayed elapsed pace. Each arm starts after restore/engine setup; setup and the gap between arms are outside the recording. After100M, the actual stopped framebuffer is held for one measured second. Millisecond video timestamp quantization; no interpolation or speed change.',identities:{},arms:[],process:{pid:process.pid,startedUtc:new Date().toISOString()},media:{}};
const save=()=>writeFile(out+'/report.json',JSON.stringify(report,null,2)+'\n');
const pins=new Map();
report.configuration={runtime:root,gateReport:gatePath,video,thumbnail};
async function pin(path,expected){path=resolve(path);const b=await readFile(path),h=hash(b);if(expected)assert.equal(h,expected,'Identity '+path);pins.set(path,h);report.identities[path]=h;return b;}
async function verify(){for(const [p,h] of pins)assert.equal(hash(await readFile(p)),h,'Changed input '+p);}
// Minimal lossless RGBA PNG writer; source PNG decode hashes are checked below.
const crcTable=Array.from({length:256},(_,i)=>{for(let n=0;n<8;n++)i=i&1?0xedb88320^(i>>>1):i>>>1;return i>>>0;});
function chunk(type,data){const t=Buffer.from(type),v=Buffer.concat([t,data]);let crc=0xffffffff;for(const b of v)crc=crcTable[(crc^b)&255]^(crc>>>8);const a=Buffer.alloc(4),z=Buffer.alloc(4);a.writeUInt32BE(data.length);z.writeUInt32BE((crc^0xffffffff)>>>0);return Buffer.concat([a,v,z]);}
function png(rgba){const header=Buffer.alloc(13);header.writeUInt32BE(320);header.writeUInt32BE(480,4);header[8]=8;header[9]=6;const rows=Buffer.alloc(480*(1+320*4));for(let y=0;y<480;y++)rgba.copy(rows,y*1281+1,y*1280,(y+1)*1280);return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',deflateSync(rows,{level:1})),chunk('IEND',Buffer.alloc(0))]);}
function frameTimes(text){const [,n,d]=text.match(/#tb 0: (\d+)\/(\d+)/);return text.split('\n').filter(l=>l&&!l.startsWith('#')).map(l=>Number(l.split(',')[2])*Number(n)/Number(d));}
function atoms(b){const found=new Map();for(let p=0;p+8<=b.length;){const n=b.readUInt32BE(p);assert(n>=8&&p+n<=b.length);found.set(b.toString('ascii',p+4,p+8),b.subarray(p+8,p+n));p+=n;}return found;}
function movieDuration(b){const h=atoms(atoms(b).get('moov')).get('mvhd');assert.equal(h[0],0);return h.readUInt32BE(16)/h.readUInt32BE(12);}
function runFF(args,log){const r=spawnSync(ffmpeg,['-hide_banner','-nostdin',...args],{encoding:'utf8',maxBuffer:8<<20,timeout:60000,killSignal:'SIGKILL'});return writeFile(out+'/'+log,(r.stdout||'')+(r.stderr||'')).then(()=>{assert.ifError(r.error);assert.equal(r.status,0,'FFmpeg '+log);});}
let active;
try{
 const gate=JSON.parse(await pin(gatePath));
 assert.equal(gate.correctnessPassed,true);assert.equal(gate.performancePassed,false);
 const frozen=async path=>pin(path,gate.identities[resolve(path)]||assert.fail('Missing gate identity '+path));
 const reference=JSON.parse(await frozen('docs/traces/adaptive-workloads/report.json'));
 const manifest=JSON.parse(await frozen(baseline+'/freeze-manifest.json'));
 await frozen(baseline+'/site/dist/emu.js');await frozen(baseline+'/site/dist/emu.wasm');
 const seedBytes=await frozen(baseline+'/site/jit/seed.wasm'),regions=JSON.parse(await frozen(baseline+'/site/jit/regions.json'));
 const build=JSON.parse(await frozen(root+'/build-report.json'));assert(build.passed);
 await frozen(root+'/workload.cjs');await frozen(root+'/workload.wasm');
 for(const f of ['workload-compiler.mjs','workloads.mjs','unit.mjs','decode.mjs','emit.mjs'])await frozen('tools/jit/adaptive/'+f);
 // The first gate predates the cache. Any gate that included it must bind the
 // exact implementation used by the dynamically imported compiler owner.
 const cachePath='tools/jit/adaptive/module-cache.mjs';
 if(gate.identities[resolve(cachePath)])await frozen(cachePath);
 await frozen('package-lock.json');await pin('tools/jit/adaptive/record-gate.mjs');await pin(ffmpeg);
 const fixture=await frozen('.context/adaptive-compiler/workload-home.snap');
 const kernel=await frozen('web/firmware/kernel_1A543a.macho'),tree=await frozen('web/firmware/devicetree_m68ap.bin');
 assert.equal(hash(fixture),reference.fixture.sha256);
 assert.equal(hash(kernel),manifest.firmware.assetSha256[manifest.firmware.kernel]);
 const w=workloads.find(w=>w.name==='settings');assert.deepEqual(w,reference.workloads.find(w=>w.name==='settings'));
 report.inputEvents=w.events.filter(e=>e.at<100000000);
 const expected=reference.runs.find(r=>r.name==='settings'&&r.repeat===0).checkpoints.find(c=>c.credits===100000000);
 const {default:createA}=await import(pathToFileURL(baseline+'/site/dist/emu.js'));
 const {default:createB}=await import(pathToFileURL(root+'/workload.cjs'));
 const wabt=await wabtFactory(),seed=new WebAssembly.Module(seedBytes);
 for(const version of ['A','B']){
  await verify();const m=version==='A'?await createA({locateFile:p=>baseline+'/site/dist/'+p}):await createB();
  const pointers=[],ids=[];let compiler;
  active={m,pointers,ids,close:()=>{if(compiler){compiler.close();m.workloadCompile=undefined;compiler=undefined;}else{m._emu_jit_reset();for(const id of ids.splice(0))m.removeFunction(id);}for(const p of pointers.splice(0))m._free(p);}};
  const alloc=b=>{const p=m._malloc(b.length);assert(p);pointers.push(p);m.HEAPU8.set(b,p);return p;};
  const k=alloc(kernel),d=alloc(tree),s=alloc(fixture),mask=alloc(Buffer.alloc(64)),frame=alloc(Buffer.alloc(320*480*4)),dims=alloc(Buffer.alloc(8));
  assert.equal(m._emu_init(k,kernel.length,d,tree.length,0,0,256),0);assert.equal(m._emu_load_snapshot(s,fixture.length),0);
  if(version==='B'){assert.equal(m._workload_prepare(),1);compiler=createWorkloadCompiler(m,wabt);m.workloadCompile=compiler.compile;m._workload_observe();m._adaptive_enable(1);}
  else{m._emu_jit_reset();const instance=new WebAssembly.Instance(seed,{env:{memory:m.jitMemory,stale:m._emu_jit_stale,span:m._emu_jit_span}});for(const r of regions){const id=m.addFunction(instance.exports['b'+r.index],'iii');ids.push(id);m.HEAPU32.set(r.masks,mask>>>2);assert.equal(m._emu_jit_install(r.pc,+r.thumb,id,mask),1);}m._emu_jit_enable(1);}
  const row={version,label:version==='A'?'A - Frozen seeded CPU replay':(process.env.CANDIDATE_LABEL||'B - Adaptive CPU replay'),startedUtc:new Date().toISOString(),frames:[],credits:0,passed:false};report.arms.push(row);await mkdir(out+'/'+version);
  const start=performance.now();
  async function capture(credits,phase){const beganMs=performance.now()-start;assert.equal(m._emu_frame(frame,dims,dims+4),1);assert.deepEqual(Array.from(m.HEAPU32.subarray(dims>>>2,(dims>>>2)+2)),[320,480]);const pixels=Buffer.from(m.HEAPU8.subarray(frame,frame+320*480*4)),readyMs=performance.now()-start;const path=`${version}/${String(row.frames.length).padStart(4,'0')}.png`;const bytes=png(pixels);await writeFile(out+'/'+path,bytes);row.frames.push({path,credits,phase,beganMs,readyMs,writtenMs:performance.now()-start,rgbaSha256:hash(pixels),rgbaMd5:hash(pixels,'md5'),pngSha256:hash(bytes)});if(credits===100000000&&phase==='running')await writeFile(out+'/'+version+'/final.rgba.gz',gzipSync(pixels));}
  await capture(0,'running');
  for(let done=0;done<100000000;){
   for(const e of w.events.filter(e=>e.at===done)){assert.equal(e.type,'touch');m._emu_touch(e.down,e.x,e.y);}
   const next=Math.min(100000000,nextBoundary(w,done));assert.equal(m._emu_run(next-done),next-done);assert.equal(m._emu_status(),0);
   if(compiler){assert.equal(m._adaptive_failed(),0);compiler.drain();}
   done=next;row.credits=done;await capture(done,'running');
   if(done%10000000===0){await save();console.log(JSON.stringify({version,credits:done,frames:row.frames.length}));}
  }
  row.replayEndedMs=performance.now()-start;
  await new Promise(r=>setTimeout(r,1000));await capture(100000000,'stopped-end-hold');row.endedMs=performance.now()-start;
  const p=m._emu_save_snapshot(),bytes=m._emu_snapshot_size();assert(p&&bytes);
  row.checkpoint={credits:100000000,bytes,sha256:hash(m.HEAPU8.subarray(p,p+bytes))};m._emu_free_snapshot();assert.deepEqual(row.checkpoint,expected);
  row.generated=compiler?m._adaptive_stat(0):m._emu_jit_stat(0);assert(row.generated>0);
  if(compiler)row.compiler={...compiler.stats};
  active.close();active=null;row.cleanup=true;row.passed=true;await verify();await save();
 }
 // All frames survive. Their original acquisition intervals determine VFR PTS.
 let offset=0;const timeline=[];
 for(const arm of report.arms){arm.videoStartMs=offset;const base=arm.frames[0].readyMs;for(const f of arm.frames)timeline.push({...f,timeMs:offset+f.readyMs-base});arm.videoDurationMs=arm.endedMs-base;offset+=arm.videoDurationMs;}
 report.media.expectedDurationMs=offset;report.media.timeline=timeline;
 let concat='ffconcat version 1.0\n';for(let i=0;i<timeline.length;i++){const f=timeline[i],until=i+1<timeline.length?timeline[i+1].timeMs:offset;assert(until>f.timeMs);concat+=`file '${resolve(out,f.path)}'\noption framerate 1000\nduration ${((until-f.timeMs)/1000).toFixed(6)}\n`;}
 concat+=`file '${resolve(out,timeline.at(-1).path)}'\noption framerate 1000\n`;await writeFile(out+'/frames.ffconcat',concat);
 await runFF(['-threads','1','-f','concat','-safe','0','-i',out+'/frames.ffconcat','-fps_mode','passthrough','-pix_fmt','rgba','-f','framemd5',out+'/source.framemd5'],'source-decode.log');
 const decoded=(await readFile(out+'/source.framemd5','utf8')).split('\n').filter(l=>l&&!l.startsWith('#')).map(l=>l.split(',').map(s=>s.trim()));
 assert.equal(decoded.length,timeline.length+1);const expectedFrames=[...timeline,timeline.at(-1)];
 for(let i=0;i<decoded.length;i++)assert.equal(decoded[i].at(-1),expectedFrames[i].rgbaMd5,'Lossless source pixel identity');
 await writeFile(out+'/title.txt','Recorded CPU replay | Settings first 100M credits');
 await writeFile(out+'/label-A.txt',report.arms[0].label);await writeFile(out+'/label-B.txt',report.arms[1].label);
 await writeFile(out+'/note1.txt','Actual framebuffer; capture overhead included');
 await writeFile(out+'/note2.txt','Not browser or Mac latency. Gate measured separately.');
 const font='/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf';await pin(font);
 const text=(name,y,size,extra='')=>`drawtext=fontfile=${font}:textfile=${out}/${name}.txt:fontcolor=white:fontsize=${size}:x=(w-text_w)/2:y=${y}${extra}`;
 const split=report.arms[1].videoStartMs/1000;
 const filter=['pad=640:640:160:82:color=0x151c28',text('title',15,21),text('label-A',48,20,`:enable='lt(t,${split})'`),text('label-B',48,20,`:enable='gte(t,${split})'`),text('note1',580,18),text('note2',609,17)].join(',');
 await mkdir(dirname(video),{recursive:true});await mkdir(dirname(thumbnail),{recursive:true});
 for(const p of [video,thumbnail])await stat(p).then(()=>assert.fail('Refuse overwriting '+p),e=>{if(e.code!=='ENOENT')throw e;});
 await runFF(['-threads','1','-f','concat','-safe','0','-i',out+'/frames.ffconcat','-vf',filter,'-filter_threads','1','-c:v','libx264','-preset','fast','-crf','20','-bf','0','-threads','1','-pix_fmt','yuv420p','-fps_mode','passthrough','-video_track_timescale','1000000','-movflags','+faststart',video],'encode.log');
 await runFF(['-xerror','-copyts','-threads','1','-i',video,'-fps_mode','passthrough','-enc_time_base','-1','-f','framemd5',out+'/video.framemd5'],'video-decode.log');
 const videoFrames=(await readFile(out+'/video.framemd5','utf8')).split('\n').filter(l=>l&&!l.startsWith('#'));
 assert.equal(videoFrames.length,decoded.length,'All recorded frames survive encode');
 const sourceTimes=frameTimes(await readFile(out+'/source.framemd5','utf8'));
 const videoTimes=frameTimes(await readFile(out+'/video.framemd5','utf8'));
 for(let i=0;i<sourceTimes.length;i++)assert(Math.abs(sourceTimes[i]-videoTimes[i])<1e-8,'Exact source/encoded PTS');
 const acquired=[...timeline.map(f=>f.timeMs/1000),offset/1000];
 report.media.maximumAcquisitionQuantizationMs=Math.max(...sourceTimes.map((t,i)=>Math.abs(t-acquired[i])*1000));assert(report.media.maximumAcquisitionQuantizationMs<=1);
 report.media.allPresentationTimestampsExact=true;
 report.media.movieDurationSeconds=movieDuration(await readFile(video));
 report.media.finalPresentationTimestampSeconds=videoTimes.at(-1);
 assert(report.media.movieDurationSeconds>=videoTimes.at(-1));
 assert(report.media.movieDurationSeconds-videoTimes.at(-1)<=0.003);
 await runFF(['-threads','1','-i',out+'/'+report.arms[1].frames.at(-1).path,'-frames:v','1','-q:v','2','-threads','1',thumbnail],'thumbnail.log');
 await runFF(['-xerror','-threads','1','-i',thumbnail,'-f','null','-'],'thumbnail-decode.log');
 report.media.sourcePixelIdentity=true;report.media.fullDecodePassed=true;report.media.frames=decoded.length;
 report.media.encoding='Sequential variable-frame-rate H264 with B frames disabled; no frame interpolation, all source frames retained, millisecond acquisition timestamp quantization. YUV420 lossy encoding is not pixel-identical; raw PNGs and decoded source hashes retained.';
 report.media.files=[];for(const p of [video,thumbnail]){const b=await readFile(p);report.media.files.push({path:p,bytes:b.length,sha256:hash(b)});}
 await verify();report.passed=true;
}catch(e){report.error=e.stack;process.exitCode=1;}
finally{if(active)try{active.close();report.cleanup=true;}catch(e){report.cleanupError=e.stack;process.exitCode=1;}report.process.endedUtc=new Date().toISOString();await save();console.log(JSON.stringify({passed:report.passed,error:report.error,report:out+'/report.json'}));}
