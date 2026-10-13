import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync, readFileSync} from 'node:fs';

const web = new URL('../', import.meta.url);
const index = readFileSync(new URL('index.html', web), 'utf8');
const sitemap = readFileSync(new URL('sitemap.xml', web), 'utf8');

test('the emulator is the only public page', () => {
  assert.equal(existsSync(new URL('about.html', web)), false);
  assert.doesNotMatch(index, /about\.html|About this iPhone/i);
  assert.doesNotMatch(sitemap, /about\.html/i);
  assert.deepEqual([...sitemap.matchAll(/<loc>(.*?)<\/loc>/g)].map(match => match[1]), [
    'https://2007.mobile/',
  ]);
});

test('the emulator page retains descriptive search metadata', () => {
  assert.match(index, /<title>iPhone Emulator — Original iPhone OS 1\.0 \| 2007\.mobile<\/title>/);
  assert.match(index, /<link rel="canonical" href="https:\/\/2007\.mobile\/">/);
  assert.match(index, /<script type="application\/ld\+json">/);
});
