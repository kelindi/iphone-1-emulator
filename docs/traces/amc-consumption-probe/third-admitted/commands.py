"""Structured postprocessing argv; never string interpolation or guest execution."""
import sys
from pathlib import Path

def audit_command(script,directory):
 return [sys.executable,str(Path(script)),str(Path(directory))]
