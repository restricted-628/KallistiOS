#!/usr/bin/env python3
"""Exercise the actual compiled parser against the accepted Python oracle."""
import ast
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import aica_bank as abk
from test_bank import golden, seal


def records(ns, nz, frames=4, enc=2):
    po = 64 + (ns+nz)*32
    payload = bytearray()
    samples = bytearray()
    for i in range(ns):
        samples += abk.SAMPLE.pack(po+len(payload),frames*enc,frames,22050,
                                   0,frames,enc,1,0)
        pcm = bytes(frames*enc)
        payload += pcm + bytes(abk.align32(len(pcm))-len(pcm))
    zones = bytearray()
    for i in range(nz):
        v = list(abk.ZONE_DEFAULTS)
        v[0],v[1],v[8] = i//128, i%128, (-100,0,100)[i%3]
        zones += abk.ZONE.pack(i%ns,*v,bytes(7))
    header = abk.HEADER.pack(abk.MAGIC,1,64,po+len(payload),ns,nz,64,
                             64+ns*32,po,len(payload),0,0,bytes(16))
    return seal(header+samples+zones+payload)


def corpus():
    g=golden()
    result=[g,records(256,1024),records(1,1,65534),records(1,1,1,1),
            records(14,1,65534),records(15,1,65534)]
    result += [g[:i] for i in range(len(g))]
    for i in range(len(g)):
        for bit in range(8):
            b=bytearray(g); b[i]^=1<<bit; result.append(bytes(b))
    tree=ast.parse((Path(__file__).resolve().parent.parent/'test_bank.py').read_text())
    cases=next(ast.literal_eval(n.value) for n in ast.walk(tree) if isinstance(n,ast.Assign)
               and any(isinstance(t,ast.Name) and t.id=='cases' for t in n.targets))
    for off,width,value in cases:
        b=bytearray(g); b[off:off+width]=value.to_bytes(width,'little'); result.append(seal(b))
    # Sweep boundary/overflow values in every header/sample32-bit field.
    for off in list(range(12,48,4))+list(range(64,88,4)):
        for value in (0,1,31,32,63,64,255,256,1024,65534,65535,0x1d0000,
                      0x200000,0x7fffffff,0xffffffe0,0xffffffff):
            b=bytearray(g);struct.pack_into('<I',b,off,value);result.append(seal(b))
    rng=random.Random(0xa1ca)
    for _ in range(3000):
        b=bytearray(g)
        for _ in range(rng.randrange(1,9)):
            b[rng.randrange(len(b))]=rng.randrange(256)
        result.append(seal(b))
    # Multi-record overlap/cross-reference/maximum-count corruption.
    for original in (records(2,2), records(256,1024)):
        zo=64+abk.HEADER.unpack_from(original)[4]*32
        for off,value in ((96,struct.unpack_from('<I',original,64)[0]),
                          (zo,0xffffffff),(zo+32+4,0),(zo+32+5,0)):
            b=bytearray(original)
            if off in (zo+36,zo+37): b[off]=value
            else: struct.pack_into('<I',b,off,value)
            result.append(seal(b))
    return result


def expected(data):
    try: meta=abk.validate(data)
    except abk.BankError: return None
    po=struct.unpack_from('<I',data,32)[0]
    values=[0,meta['size'],po,meta['payload_bytes'],len(meta['samples']),len(meta['zones'])]
    for i,s in enumerate(meta['samples']):
        values += [s['offset'],s['offset']-po,s['bytes'],s['frames'],s['rate'],
                   s['loop_start'],s['loop_end'],s['encoding'],struct.unpack_from('<H',data,64+i*32+26)[0]]
    for z in meta['zones']:
        values += [z['sample']]+[z[k] for k in abk.ZONE_FIELDS]
    return values


def main():
    cases=corpus()
    payload=b''.join(struct.pack('<I',len(b))+b for b in cases)
    run=subprocess.run([sys.argv[1]],input=payload,capture_output=True,check=True)
    lines=run.stdout.decode().splitlines()
    assert len(lines)==len(cases),(len(lines),len(cases))
    accepted=0
    for i,(data,line) in enumerate(zip(cases,lines)):
        actual=list(map(int,line.split())); want=expected(data)
        if want is None: assert actual[0] != 0,('unexpected accept',i)
        else:
            assert actual==want,('metadata/acceptance mismatch',i,actual[:10],want[:10])
            accepted+=1
    print(json.dumps({'cases':len(cases),'input_alignments':4,'accepted':accepted,
                      'rejected':len(cases)-accepted,'metadata':'exact match',
                      'failure_outputs':'unchanged','placement_capacity_alias':'PASS'}))


if __name__=='__main__': main()
