#!/usr/bin/env python3
"""Extend the owned nt-modern fixture with short-length/pointer gate cases.
The source stays separate so the original matrix remains reviewable.
"""
import argparse
import hashlib
from pathlib import Path


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    original=Path(__file__).resolve().parents[1]/'nt-modern/HandleTables.c'
    data=original.read_bytes()
    expected='0c797a29df11dc35fccda1e44c3cd7dc6069b1c72b82a26216fcebb2b601497e'
    if hashlib.sha256(data).hexdigest()!=expected:
        raise SystemExit('Base fixture changed; review the extension and update its pin first')
    needle='    Query(label,process,0,1); Query(label,process,3,1);'
    addition='''
    /* Reviewer-only gate proof: native Win7 may reject these before dispatch. */
    for(i=0;i<4;++i) {
        Query(label,process,i,2);
        Query(label,process,i,4);
        Query(label,process,i,5);
        Query(label,process,i,6);
    }'''
    text=data.decode('utf-8-sig')
    if text.count(needle)!=1: raise SystemExit('Base matrix anchor is not unique')
    with args.out.open('x',encoding='utf-8') as stream:
        stream.write(text.replace(needle,needle+addition))
    print(hashlib.sha256(args.out.read_bytes()).hexdigest(),args.out)


if __name__=='__main__': main()
