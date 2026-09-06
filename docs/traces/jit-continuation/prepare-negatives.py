from pathlib import Path
p=Path('.context/jit-continuation');src=(p/'jit-runtime.c').read_text();oracle=(p/'oracle.c').read_text()
changes={'negative-budget':('r->fn(c, limit - total)','r->fn(c, limit)'), 'negative-refill':('if (!total || !arm_fetch_cache_try_refill(c, c->r[15], priv)) break;', 'if (!total) break; /* Deliberately WRONG: use previous region mapping. */')}
for name,(old,new) in changes.items():
 assert src.count(old)==1
 (p/(name+'.c')).write_text(src.replace(old,new));(p/(name+'-oracle.c')).write_text(oracle.replace('#include "jit-runtime.c"','#include "'+name+'.c"'))
