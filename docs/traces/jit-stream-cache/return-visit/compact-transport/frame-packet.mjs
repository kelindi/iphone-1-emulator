import {createHash} from 'node:crypto';
export function encodeBytes(bytes) {
 let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(binary);
}
export function decodeFrame(packet,expectedBytes=320*480*4) {
 if(typeof packet?.base64!=='string'||packet.base64.length%4!==0||! /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(packet.base64))throw Error('Invalid frame encoding');
 const bytes=Buffer.from(packet.base64,'base64');
 if(bytes.length!==expectedBytes)throw Error('Frame byte length mismatch');
 if(typeof packet.sha256!=='string'||createHash('sha256').update(bytes).digest('hex')!==packet.sha256)throw Error('Frame transport identity mismatch');
 return bytes;
}
