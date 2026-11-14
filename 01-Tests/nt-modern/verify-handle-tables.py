"""Compare owned class58 observations against a native modern-NT reference.
Counts from different processes need not match; errors, probing, truncation,
canaries, own known handles, ordering and LastError preservation must match.
This script does not execute targets or change profiles/system settings.
"""
import argparse,json
from pathlib import Path

def rows(path):
    result=[]
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        parts=line.split('|')
        if parts[0] not in ['QUERY','BOUNDARY','READONLY']:continue
        row={'kind':parts[0],'label':parts[1]}
        for part in parts[2:]:
            key,value=part.split('=',1);row[key]=int(value,16) if value.startswith('0x') else int(value)
        result.append(row)
    return result

def verify(reference,candidate):
    expected=rows(reference);actual=rows(candidate);failures=[];assertions=0
    def check(ok,message):
        nonlocal assertions
        assertions+=1
        if not ok:failures.append(message)
    check(len(expected)==len(actual),'observation count')
    for index,(ref,test) in enumerate(zip(expected,actual)):
        label=f'{index}:{ref["kind"]}:{ref["label"]}:{ref.get("length")}:{ref.get("special")}'
        for field in ['kind','label','length','special','status','exception']:
            check(ref.get(field)==test.get(field),label+':'+field)
        success=test['status']==0
        if not success or test.get('length',0)<4096 or test.get('special')==3:
            check(ref.get('returned')==test.get('returned'),label+':returned')
        if test['kind']=='QUERY':
            check(test['outside_capacity']==0,label+':canary')
            check(test['last_error']==0x6b145239,label+':LastError')
            check(test['exception']==0,label+':no user exception')
            if not success:check(test['changed']==0,label+':failure data unchanged')
            if success and test['special']!=3:
                check(test['returned']<=test['length'] and test['returned']%4==0,label+':ULONG capacity')
                if test['length']<4096:check(test['returned']==test['length']//4*4,label+':partial result')
            if success:
                check(test['unique']==1 and test['ascending']==1,label+':unique ascending')
            if success and test['length']==4096 and test['label'] in ['self-pseudo','child-full','child-query-dup']:
                check(test['known']==31,label+':all five independently owned event/job handles')
    return {'status':'PASS' if not failures else 'FAIL','comparisons':len(actual),'assertions':assertions,'failures':failures,'reference':str(reference),'candidate':str(candidate),'limits':['Owned quiescent tables and bounded validation matrix; not a proof of concurrent snapshot linearizability.','Class52 enforcement, privileged/system targets, low-integrity tokens and arbitrary enormous handle populations are not covered.']}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('reference',type=Path);p.add_argument('candidate',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    receipt=verify(a.reference,a.candidate)
    if a.out:
        with a.out.open('x',encoding='utf-8') as stream:json.dump(receipt,stream,indent=2)
    print(json.dumps(receipt,indent=2));raise SystemExit(0 if receipt['status']=='PASS' else 1)
