import {encodeCheckpoint} from './storage.js';

let used = false;
self.onmessage = async ({data}) => {
  if (used) return;
  used = true;
  try {
    if (data?.type !== 'encode' || !(data.buffer instanceof ArrayBuffer))
      throw new Error('Save compression worker received invalid input');
    const rawSize = data.buffer.byteLength;
    const encoded = await encodeCheckpoint(data.buffer);
    self.postMessage({type:'encoded', rawSize, encoded});
  } catch (error) {
    self.postMessage({type:'error', message:error?.message || 'Save compression failed'});
  }
};
self.postMessage({type:'ready', version:1});
