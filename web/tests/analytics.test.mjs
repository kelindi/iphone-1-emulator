import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../analytics.js', import.meta.url), 'utf8');
function load(hostname) {
  const scripts = [];
  const window = {location:{hostname}};
  const document = {
    createElement:() => ({}),
    getElementsByTagName:() => [{parentNode:{insertBefore:script => scripts.push(script)}}],
  };
  const context = vm.createContext({window, document});
  vm.runInContext(source.replace('export function track', 'function track') + '\nglobalThis.track = track;', context);
  return {window, scripts, track:context.track};
}

test('production queues events until the asynchronous SDK is available', () => {
  const {window, scripts, track} = load('2007.mobile');
  assert.equal(scripts.length, 1);
  assert.equal(scripts[0].async, true);
  assert.equal(scripts[0].src, 'https://us-assets.i.posthog.com/static/array.js');
  const [token, config] = window.posthog._i[0];
  assert.match(token, /^phc_/);
  assert.equal(config.api_host, 'https://us.i.posthog.com');
  assert.equal(config.capture_pageview, true);
  assert.equal(config.capture_pageleave, true);
  assert.equal(config.disable_session_recording, true);
  assert.equal(config.autocapture, false);
  assert.equal(config.respect_dnt, true);
  assert(config.property_denylist.includes('$current_url'));
  track('emulator ready', {startup_ms:1250});
  const event = window.posthog.at(-1);
  assert.equal(event[0], 'capture');
  assert.equal(event[1], 'emulator ready');
  assert.equal(event[2].startup_ms, 1250);
});

test('localhost and branch previews neither load the SDK nor capture events', () => {
  for (const hostname of ['localhost', '127.0.0.1', 'slidetorun-git-feature-kelindi.vercel.app']) {
    const {window, scripts, track} = load(hostname);
    track('emulator started');
    assert.equal(scripts.length, 0);
    assert.equal(window.posthog, undefined);
  }
});

test('capture failures cannot escape into the emulator input or startup path', () => {
  const {window, track} = load('2007.mobile');
  window.posthog.capture = () => {throw new Error('Blocked analytics');};
  assert.doesNotThrow(() => track('phone control used', {control:'home'}));
});
