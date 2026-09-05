import createEmu from "./dist/emu.js";

const module = await createEmu({
  locateFile: path => new URL(`./dist/${path}`, import.meta.url).pathname
});
const result = module.ccall(
  "emu_init", "number",
  ["number", "number", "number", "number", "number", "number", "number"],
  [0, 0, 0, 0, 0, 0, 256]
);
const error = module.ccall("emu_error", "string", [], []);
if (result >= 0 || !error.includes("kernel and devicetree are required"))
  throw new Error(`unexpected emu_init result ${result}: ${error}`);
console.log(`WASM smoke passed: emu_init=${result}; error=${error}`);
