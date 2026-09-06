"""Call only after viewing this run's actual Home framebuffer."""
from pathlib import Path
import datetime,hashlib,json,sys
b=Path(__file__).resolve().parent/'native-run-1';p=Path(sys.argv[1]).resolve();assert p.parent==b and p.name.startswith('frame.') and p.is_file()
receipt={'frame':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'reviewUTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'review':'Actual original Home framebuffer visually reviewed; existing OS repair alert may be present.'}
with (b/'capture-request.json').open('x')as f:json.dump(receipt,f,indent=2);f.write('\n')
print(receipt)
