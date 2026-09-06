from pathlib import Path
import importlib.util,json,sys
root=Path(__file__).resolve().parent;spec=importlib.util.spec_from_file_location('analyzer',root/'analyze-cache.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
p=Path(sys.argv[1]);report,_=m.analyze_events(json.loads(p.read_text())['traceEvents'],'startup',sys.argv[2])
print(json.dumps(dict(accepted=bool(report['seedAccepted']) and not report['unmatchedRelevantScopes'],analysis=report)))
