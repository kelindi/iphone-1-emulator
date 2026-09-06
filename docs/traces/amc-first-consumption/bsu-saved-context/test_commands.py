from commands import audit_command
from pathlib import Path
import sys
# Pin the exact previously broken shape: script is one Path, directory another.
assert audit_command(Path('run')/'ordering-audit.py',Path('run')/'negative-record')==[sys.executable,'run/ordering-audit.py','run/negative-record']
assert audit_command(Path('a b')/'third-audit.py',Path('a b')/'negative')==[sys.executable,'a b/third-audit.py','a b/negative']
print('structured auditor argv:2 checks pass; no subprocess or guest executed')
