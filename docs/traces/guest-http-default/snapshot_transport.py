"""Bounded private FIFO→lossless gzip; publication is a separate caller gate."""
import gzip,hashlib,json,os,select,struct,time,zlib
from pathlib import Path
MAX_RAW=512*1024*1024
MAGIC=b'S5LBox SNAPSHOT'.ljust(16,b'\0')
def receive_fifo(fifo,partial,writer,deadline,fail_after=None):
 fd=os.open(fifo,os.O_RDONLY|os.O_NONBLOCK);raw_size=0;header=bytearray();raw_sha=hashlib.sha256();packed=zlib.compressobj(1,zlib.DEFLATED,31);wanted=None;tail=b''
 try:
  with Path(partial).open('xb')as output:
   while True:
    if time.monotonic()>deadline():raise TimeoutError('FIFO/serialization deadline')
    select.select([fd],[],[],.05)
    try:chunk=os.read(fd,1024*1024)
    except BlockingIOError:continue
    if not chunk:
     if raw_size:break
     if writer.poll() is not None:raise ValueError('writer ended before any snapshot bytes')
     time.sleep(.02);continue
    raw_size+=len(chunk)
    if raw_size>MAX_RAW:raise ValueError('expanded snapshot exceeds bound')
    if fail_after is not None and raw_size>fail_after:raise OSError('injected output failure')
    if len(header)<40:header.extend(chunk[:40-len(header)])
    if len(header)==40 and wanted is None:
     magic,version,hlen,payload,flags=struct.unpack('<16sIIQQ',header)
     if magic!=MAGIC or version!=39 or hlen!=40 or flags!=0 or payload>MAX_RAW-48:raise ValueError('snapshot header rejected')
     wanted=48+payload
    if wanted is not None and raw_size>wanted:raise ValueError('extra bytes after snapshot')
    raw_sha.update(chunk);tail=(tail+chunk)[-8:];output.write(packed.compress(chunk))
   if wanted is None or raw_size!=wanted:raise ValueError('truncated snapshot')
   output.write(packed.flush());output.flush();os.fsync(output.fileno())
 finally:os.close(fd)
 expanded=0;verified=hashlib.sha256()
 with gzip.open(partial,'rb')as stream:
  for chunk in iter(lambda:stream.read(1024*1024),b''):expanded+=len(chunk);verified.update(chunk)
 if expanded!=raw_size or verified.digest()!=raw_sha.digest():raise ValueError('gzip roundtrip mismatch')
 return {'rawBytes':raw_size,'rawSHA256':raw_sha.hexdigest(),'gzipBytes':Path(partial).stat().st_size,'gzipSHA256':hashlib.sha256(Path(partial).read_bytes()).hexdigest(),'headerHex':bytes(header).hex(),'snapshotTrailerHex':tail.hex(),'gzipRoundtripExact':True}
def publish(partial,target,receipt,writer_ok,guest_gate):
 if not writer_ok or not guest_gate or not receipt.get('gzipRoundtripExact'):raise ValueError('unqualified publication')
 os.link(partial,target);Path(partial).unlink()
