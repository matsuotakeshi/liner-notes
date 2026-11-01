#!/usr/bin/env python3
"""Validate and export a Liner notes activity/chat/user/download/catalog journal as JSON lines."""
import argparse,json,struct
from pathlib import Path

def export(path):
 with Path(path).open('rb') as f:
  magic=f.read(8)
  if magic not in (b'LNACTV01',b'LNCHAT01',b'LNUSER01',b'LNDOWN01',b'LNCAT001',b'LNSIG001'):raise ValueError('Unknown journal')
  sequence=0
  while row:=f.read(4096):
   if len(row)!=4096:raise ValueError('Truncated journal')
   h=2166136261
   for b in row[:-4]:h=((h^b)*16777619)&0xffffffff
   seq,actor,target=struct.unpack_from('<III',row,4);stamp=struct.unpack_from('<Q',row,16)[0];size=struct.unpack_from('<I',row,24)[0]
   if h!=struct.unpack_from('<I',row,4092)[0] or seq!=sequence+1 or size>3000 or any(row[1:4]) or any(row[28+size:4092]):raise ValueError('Invalid journal record')
   text=row[28:28+size].decode('utf-8');sequence=seq
   yield dict(sequence=seq,kind=chr(row[0]),actor=actor,target=target,time=stamp,text=text)
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('journal');a=p.parse_args()
 for item in export(a.journal):print(json.dumps(item,ensure_ascii=False))
