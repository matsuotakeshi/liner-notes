"""Compaction must reject corrupt superseded records, not hide corruption."""
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import state_compact as state
from notes_compact import put, sealed


def rejected(call):
    try: call()
    except ValueError: return
    raise AssertionError('corrupt source accepted')


def run():
    retention = state.retention_module()
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp); members = root/'members'
        row=bytearray(124);row[0]=ord('A');put(row,4,1)
        members.write_bytes(b'RTMEMB01'+sealed(row))
        policy=root/'policy'
        def event(kind,text): return dict(kind=kind,actor=1,target=2,time=0,text=text)
        for bad in ('invalid', 'f'*32+' '+'z'*64):
            policy.write_bytes(retention.encode(b'LNBDP001',[event('P',bad),event('P','')]))
            rejected(lambda:state.policy_copy(policy,members))
        policy.write_bytes(retention.encode(b'LNBDP001',[event('I','3'),event('J','a\x01b'),event('E',''),event('I','0'),event('E','')]))
        rejected(lambda:state.policy_copy(policy,members))
        profile=root/'profile'
        def field(text):
            return [dict(kind=k,actor=1,target=1,time=0,text=t) for k,t in [('I','8 10'),('J',text),('E','')]]
        profile.write_bytes(retention.encode(b'LNPROF01',field('2001-02-29')+field('2000-02-29')))
        rejected(lambda:state.profile_copy(profile,members))
        extras=root/'extras'
        records=[]
        for number in (1,2):
            row=bytearray(508);row[0]=ord('H');put(row,4,number);put(row,8,1);put(row,12,1);put(row,16,1);row[20]=ord('a')
            if number==1:row[99]=1
            records.append(sealed(row))
        extras.write_bytes(b'RTEXTRA1'+b''.join(records))
        rejected(lambda:state.extras_copy(extras,members))
    print('PASS state compaction: superseded invalid password, guide control, birthday and extras padding are rejected')


if __name__=='__main__':run()
