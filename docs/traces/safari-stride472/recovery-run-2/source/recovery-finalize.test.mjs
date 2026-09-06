import assert from 'node:assert/strict';
import {finalAcceptance,validateManualText,requireOpenInput} from './recovery-finalize.mjs';
let checks=0;const good=()=>({finished:true,checks:[{name:'typed-glyphs',intendedText:'glyph',actualText:'glyph',passed:true},...['home-first','safari-open','safari-reopened','address-keyboard','home-second','exact-home-repeat'].map(name=>({name,passed:true}))],serial:[],errors:[],requestFailures:[],finalExport:{sha256:'a'.repeat(64),bytes:260767808},browserClosed:true,serverClosed:true,assetsExact:true,projectionUnchanged:true});
assert(finalAcceptance(good()));checks++;
for(const text of ['Graphics hang with pending 2D blit','Graphics Restart during 3D Blit']) {const r=good();assert(finalAcceptance(r));r.serial.push({text:text.slice(0,8)},{text:text.slice(8)});assert.equal(finalAcceptance(r),false);checks++;}
for(const actualText of ['glvph','',null,undefined]) {assert.throws(()=>validateManualText({observe:'typed-glyphs',intendedText:'glyph',actualText}));const r=good();r.checks[0].actualText=actualText;assert.equal(finalAcceptance(r),false);checks++;}
for(const intendedText of ['', '  ',null])assert.throws(()=>validateManualText({observe:'typed-glyphs',intendedText,actualText:intendedText}));checks+=3;
assert.throws(()=>requireOpenInput(true),/INCOMPLETE/);requireOpenInput(false);checks++;
const eof=good();eof.finished=false;assert.equal(finalAcceptance(eof),false);checks++;
for(const field of ['finalExport','browserClosed','serverClosed','assetsExact','projectionUnchanged']){const r=good();delete r[field];assert.equal(finalAcceptance(r),false);checks++;}
for(const kind of ['runtime','request']){const r=good();if(kind==='runtime')r.errors.push('WASM runtime error');else r.requestFailures.push({url:'emu.wasm'});assert.equal(finalAcceptance(r),false);checks++;}
console.log(JSON.stringify({passed:true,checks,scope:'Actual finalization helper: late split serial, missing/mismatched text and closed-input controls'}));
