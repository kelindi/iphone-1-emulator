import test from 'node:test';
import assert from 'node:assert/strict';
import {gzipSync} from 'node:zlib';
import {checkpointBytes} from '../storage.js';

const payload = Buffer.from('A real saved note: meet at 10.\n'.repeat(100));
const gzipRecord = () => ({encoding:'gzip', size:payload.length, data:new Blob([gzipSync(payload)])});

test('existing checkpoints without encoding remain readable as raw bytes', async () => {
  const record = {data:new Blob([payload])};
  assert.deepEqual(Buffer.from(await checkpointBytes(record)), payload);
  assert.deepEqual(Buffer.from(await checkpointBytes({...record, encoding:'raw'})), payload);
});

test('gzip checkpoints expand to their original bytes', async () => {
  assert.deepEqual(Buffer.from(await checkpointBytes(gzipRecord())), payload);
});

test('corrupt gzip and inconsistent expanded size are rejected', async () => {
  const compressed = gzipSync(payload);
  compressed[compressed.length - 8] ^= 0xff; // CRC mismatch, not merely an invalid header.
  await assert.rejects(checkpointBytes({...gzipRecord(), data:new Blob([compressed])}));
  await assert.rejects(checkpointBytes({...gzipRecord(), size:1}), /recorded size/);
  await assert.rejects(checkpointBytes({...gzipRecord(), size:payload.length + 1}), /does not match/);
});

test('encoding and allocation limits are checked before expansion', async () => {
  await assert.rejects(checkpointBytes({...gzipRecord(), encoding:'unknown'}), /Unsupported checkpoint encoding/);
  for (const size of [0, -1, 1.5, 513 * 1048576])
    await assert.rejects(checkpointBytes({...gzipRecord(), size}), /invalid expanded size/);
  await assert.rejects(checkpointBytes({data:{size:513 * 1048576, arrayBuffer() { throw new Error('must not read'); }}}), /512 MB limit/);
  await assert.rejects(checkpointBytes(null), /no readable data/);
});
