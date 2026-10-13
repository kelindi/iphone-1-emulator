// Media-only correction/audit. No emulator invocation or new timing samples.
import {readFile,writeFile,copyFile,rename} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import assert from 'node:assert/strict';
import ffmpeg from 'ffmpeg-static';
const out=resolve('.context/adaptive-compiler/first-gate-recording');
const video=resolve(out+'/compiler_first_gate.mp4');
const sha=b=>createHash('sha256').update(b).digest('hex');
const report=JSON.parse(await readFile(out+'/report.json'));
const audit={passed:false,scope:'Media-only reencode of the exact existing PNG/timestamp sequence. No guest rerun. Initial decode preserved all frames/PTS; FFmpeg reported a shorter stream duration with B-frame reordering, while direct movie-header duration was correct. Reencoding without B frames removes the duration ambiguity; no lost frames or player truncation were demonstrated.',sourceReportSha256:sha(await readFile(out+'/report.json')),scriptSha256:sha(await readFile(new URL(import.meta.url))),initialVideoSha256:sha(await readFile(video))};
function run(args,log){const r=spawnSync(ffmpeg,['-hide_banner','-nostdin',...args],{encoding:'utf8',timeout:60000,killSignal:'SIGKILL',maxBuffer:8<<20});return writeFile(out+'/'+log,(r.stdout||'')+(r.stderr||'')).then(()=>{assert.ifError(r.error);assert.equal(r.status,0,log);});}
function atoms(b){const a=new Map();for(let p=0;p+8<=b.length;){const n=b.readUInt32BE(p);assert(n>=8&&p+n<=b.length);a.set(b.toString('ascii',p+4,p+8),b.subarray(p+8,p+n));p+=n;}return a;}
function duration(b){const h=atoms(atoms(b).get('moov')).get('mvhd');assert.equal(h[0],0);return h.readUInt32BE(16)/h.readUInt32BE(12);}
function frames(text){const [,n,d]=text.match(/#tb 0: (\d+)\/(\d+)/);return text.split('\n').filter(l=>l&&!l.startsWith('#')).map(l=>({pts:Number(l.split(',')[2])*Number(n)/Number(d),hash:l.split(',').at(-1).trim()}));}
try{
 assert(report.passed);assert.equal(audit.initialVideoSha256,report.media.files[0].sha256);
 await copyFile(video,out+'/initial-bframe-encode.mp4',1);audit.initialMovieDurationSeconds=duration(await readFile(video));
 const font='/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf',split=report.arms[1].videoStartMs/1000;
 const text=(name,y,size,extra='')=>`drawtext=fontfile=${font}:textfile=${out}/${name}.txt:fontcolor=white:fontsize=${size}:x=(w-text_w)/2:y=${y}${extra}`;
 const filter=['pad=640:640:160:82:color=0x151c28',text('title',15,21),text('label-A',48,20,`:enable='lt(t,${split})'`),text('label-B',48,20,`:enable='gte(t,${split})'`),text('note1',580,18),text('note2',609,17)].join(',');
 const staged=out+'/no-bframe-encode.mp4';
 await run(['-threads','1','-f','concat','-safe','0','-i',out+'/frames.ffconcat','-vf',filter,'-filter_threads','1','-c:v','libx264','-preset','fast','-crf','20','-bf','0','-threads','1','-pix_fmt','yuv420p','-fps_mode','passthrough','-video_track_timescale','1000000','-movflags','+faststart',staged],'no-bframe-encode.log');
 await run(['-xerror','-copyts','-threads','1','-i',staged,'-fps_mode','passthrough','-enc_time_base','-1','-f','framemd5',out+'/no-bframe-demux-time.framemd5'],'no-bframe-decode.log');
 const raw=frames(await readFile(out+'/source.framemd5','utf8')),encoded=frames(await readFile(out+'/no-bframe-demux-time.framemd5','utf8'));
 assert.equal(raw.length,encoded.length);for(let i=0;i<raw.length;i++)assert(Math.abs(raw[i].pts-encoded[i].pts)<1e-8,'Exact source/encoded PTS');
 const expected=[...report.media.timeline.map(f=>f.timeMs/1000),report.media.expectedDurationMs/1000];
 assert.equal(expected.length,raw.length);audit.maximumAcquisitionQuantizationMs=Math.max(...raw.map((f,i)=>Math.abs(f.pts-expected[i])*1000));assert(audit.maximumAcquisitionQuantizationMs<=1);
 const bytes=await readFile(staged);audit.finalMovieDurationSeconds=duration(bytes);audit.finalPresentationTimestampSeconds=encoded.at(-1).pts;
 assert(audit.finalMovieDurationSeconds>=audit.finalPresentationTimestampSeconds);assert(audit.finalMovieDurationSeconds-audit.finalPresentationTimestampSeconds<=0.003);
 audit.frames=raw.length;audit.allPresentationTimestampsExact=true;audit.fullDecodePassed=true;
 audit.finalVideo={path:video,bytes:bytes.length,sha256:sha(bytes)};
 audit.thumbnail={path:out+'/compiler_first_gate.jpg',sha256:sha(await readFile(out+'/compiler_first_gate.jpg')),content:'Actual100M Settings loading shell; not a completed app launch.'};
 await rename(staged,video);audit.passed=true;
}catch(e){audit.error=e.stack;process.exitCode=1;}
await writeFile(out+'/media-review.json',JSON.stringify(audit,null,2)+'\n');console.log(JSON.stringify(audit));
