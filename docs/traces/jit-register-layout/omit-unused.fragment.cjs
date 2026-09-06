
// Isolated region-wide omission experiment. The original emitter above is
// unchanged; this removes only its unconditional register transfer boilerplate.
const compileWithAllRegisters = module.exports.compileRegion;
module.exports.compileRegion = function(e) {
  const result = compileWithAllRegisters(e);
  if (!result) return result;
  let instructions = result.wat;
  const transfers = Array.from({length:16}, (_, n) => ({
    load: `(local.set $r${n} (i32.load offset=${R+n*4} (local.get $c)))`,
    spill: `(i32.store offset=${R+n*4} (local.get $c)(local.get $r${n}))`
  }));
  for (const {load, spill} of transfers) {
    if (instructions.split(load).length !== 2 || instructions.split(spill).length !== 2)
      throw Error('Register transfer boilerplate changed');
    instructions = instructions.replace(load, '').replace(spill, '');
  }
  const reads = new Set([...instructions.matchAll(/\(local.get \$r(\d+)\)/g)].map(m => +m[1]));
  const writes = new Set([...instructions.matchAll(/\(local.set \$r(\d+) /g)].map(m => +m[1]));
  let wat = result.wat;
  for (let n = 0; n < 16; n++) {
    if (!writes.has(n)) {
      wat = wat.replace(transfers[n].spill, '');
      if (!reads.has(n)) wat = wat.replace(transfers[n].load, '');
    }
  }
  // Keep initial loads for every written register even if its body never reads
  // it: zero budgets, conditional writes and early exits must preserve its value.
  return {...result, wat};
};
