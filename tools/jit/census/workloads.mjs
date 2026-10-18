// Scripted workloads from the shipped Home snapshot. Events are
// [instruction, 'touch', down, x, y] | [instruction, 'button', id, down] | [instruction, 'shot'].
const M = 1e6;
const tap = (at, x, y) => [[at, 'touch', 1, x, y], [at + 5 * M, 'touch', 0, x, y]];
const drag = (at, x, y0, y1) => [[at, 'touch', 1, x, y0],
  ...Array.from({length: 10}, (_, i) => [at + (i + 1) * M, 'touch', 1, x, Math.round(y0 + (y1 - y0) * (i + 1) / 10)]),
  [at + 11 * M, 'touch', 0, x, y1]];
const home = at => [[at, 'button', 1, 1], [at + 2 * M, 'button', 1, 0]];
const shots = (...at) => at.map(a => [a * M, 'shot']);
// Open an app, scroll, tap the middle of the screen, go Home.
const app = (x, y, extra = []) => ({instructions: 360 * M, events: [...tap(0, x, y), ...drag(150 * M, 160, 380, 140),
  ...tap(220 * M, 160, 240), ...extra, ...home(300 * M), ...shots(100, 200, 290, 355)]});
const keys = (at, n) => Array.from({length: n}, (_, i) => tap(at + i * 12 * M, 20 + (i * 67) % 290, 300 + (i % 3) * 54)).flat();
export const workloads = {
  probe: {instructions: 2 * M, events: shots(1)},
  // bench.mjs and replay.mjs workload: Settings, then Home at 300M.
  settings: {instructions: 600 * M, events: [[0, 'touch', 1, 275, 250], [5 * M, 'touch', 0, 275, 250], ...home(300 * M), ...shots(100, 299, 599)]},
  settingsScroll: app(275, 250),
  text: app(45, 70), calendar: app(122, 70), photos: app(198, 70),
  youtube: app(45, 160), stocks: app(122, 160), maps: app(198, 160), weather: app(275, 160),
  clock: app(45, 250, tap(260 * M, 198, 455)),
  calculator: {instructions: 360 * M, events: [...tap(0, 122, 250), ...keys(120 * M, 12), ...home(300 * M), ...shots(100, 290, 355)]},
  notes: {instructions: 360 * M, events: [...tap(0, 198, 250), ...tap(120 * M, 300, 22), ...keys(170 * M, 9), ...home(300 * M), ...shots(100, 160, 290, 355)]},
  phone: app(45, 440), mail: app(122, 440), safari: app(198, 440), ipod: app(275, 440),
  // Lock, wake, slide to unlock.
  lock: {instructions: 400 * M, events: [[20 * M, 'button', 0, 1], [22 * M, 'button', 0, 0], [150 * M, 'button', 1, 1], [152 * M, 'button', 1, 0],
    [250 * M, 'touch', 1, 60, 430], ...Array.from({length: 12}, (_, i) => [250 * M + (i + 1) * 2 * M, 'touch', 1, 60 + (i + 1) * 20, 430]),
    [280 * M, 'touch', 0, 305, 430], ...shots(100, 200, 245, 399)]},
};
