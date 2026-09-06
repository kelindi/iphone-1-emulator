"""Isolated emitter experiment; leaves production compiler/runtime untouched."""
from pathlib import Path
import hashlib
import json

repo = Path(__file__).resolve().parents[2]
here = Path(__file__).resolve().parent
original = (repo / 'tools/jit/compiler.cjs').read_text()

def replace_once(text, before, after):
    assert text.count(before) == 1, before
    return text.replace(before, after)

source = replace_once(original, 'function compileRegion(e){',
                      'function compileRegionOriginal(e, selection){')
source = replace_once(source, ' if(!blocks.length)return null;',
    ' if(!blocks.length)return null;\n'
    ' const allBlocks=blocks.map(b=>b.ops.map(c=>c.pc));\n'
    ' if(selection)blocks=blocks.slice(selection.start,selection.end);')
source = replace_once(source, 'return {masks,count:code.size,heads:blocks.length,wat:',
    'return {masks,count:code.size,heads:blocks.length,blockEntries:allBlocks,wat:')
source = replace_once(source, 'module.exports =', (here / 'dispatcher.txt').read_text() + '\nmodule.exports =')
(here / 'compiler.cjs').write_text(source)
(here / 'source-pins.json').write_text(json.dumps({
    'originalCompilerSha256': hashlib.sha256(original.encode()).hexdigest(),
    'candidateCompilerSha256': hashlib.sha256(source.encode()).hexdigest(),
    'scope': 'isolated seed emitter only; exact original basic-block construction and instruction bodies',
}, indent=2) + '\n')
