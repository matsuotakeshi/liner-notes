"""Schema-5 named compatibility fields; also generate the C descriptors.

Only format/checksum metadata is retained. No arbitrary byte-offset/value store.
The journal identity is (stream,offset), never a mutable user or message ID.
"""
import argparse,struct
from pathlib import Path
# table, parent, event codes (empty for all), attribute size, named integer fields
SPECS=[
 ('article_format','article_records','',64,[('format_flag',0,4),('checksum',60,4)]),
 ('mail_format','mail_records','',48,[('draft_consumed',1,1),('attachment_count',2,1),('sequence',4,4),('checksum',44,4)]),
 ('member_registration','member_records','AJ',128,[('pending',1,1),('iterations',88,4),('checksum',124,4)]),
 ('member_password','member_records','P',128,[('iterations',88,4),('checksum',124,4)]),
 ('member_password_reset','member_records','Q',128,[('actor_id',8,4),('created_at',12,8),('iterations',88,4),('checksum',124,4)]),
 ('member_level','member_records','LV',128,[('level',8,1),('actor_id',16,4),('created_at',20,8),('checksum',124,4)]),
 ('member_group_change','member_records','G',128,[('actor_id',24,4),('created_at',28,8),('checksum',124,4)]),
 ('member_group_move','member_records','g',128,[('destination',8,4),('actor_id',12,4),('created_at',16,8),('checksum',124,4)]),
 ('member_preferences','member_records','F',128,[('language',8,1),('pager',9,1),('checksum',124,4)]),
 ('member_terminal','member_records','f',128,[('language',8,1),('pager',9,1),('editor',10,1),('auxiliary',11,1),('columns',12,4),('rows',16,4),('checksum',124,4)]),
 ('member_removal','member_records','X',128,[('reason',8,1),('actor_id',12,4),('created_at',16,8),('checksum',124,4)]),
 ('member_mail_notice','member_records','N',128,[('deny',8,1),('checksum',124,4)]),
 ('member_chat_policy','member_records','C',128,[('deny',8,1),('checksum',124,4)]),
 ('member_mute','member_records','M',128,[('enabled',8,1),('target_id',12,4),('checksum',124,4)]),
 ('member_read_state','member_records','R',128,[('article_id',8,4),('seen',12,1),('checksum',124,4)]),
 ('member_profile_marker','member_records','p',128,[('checksum',124,4)]),
 ('member_draft','member_records','Dd',36,[('active',1,1),('board_id',8,4),('parent_id',12,4),('revision',16,4),('checksum',32,4)]),
]

def spec(kind,event):
    parent={1:'article_records',2:'mail_records',3:'member_records'}[kind]
    for item in SPECS:
        if item[1]==parent and (not item[2] or chr(event) in item[2]):return item
    raise ValueError('Unsupported normalized event')

def attributes(db,stream,offset,kind,event):
    table,parent,events,size,fields=spec(kind,event)
    row=db.execute('SELECT '+','.join(f[0] for f in fields)+' FROM '+table+' WHERE stream=? AND offset=?',(stream,offset)).fetchone()
    if row is None:raise ValueError('Missing normalized fields')
    out=bytearray(size)
    if kind!=1:out[0]=event
    for (name,at,width),value in zip(fields,row):
        if type(value)!=int or not 0<=value<1<<(width*8) or (width==8 and value>0x7fffffffffffffff):raise ValueError('Invalid normalized field: '+name)
        out[at:at+width]=value.to_bytes(width,'little')
    if kind==3:
        groups=list(db.execute('SELECT group_id FROM member_event_groups WHERE stream=? AND offset=? ORDER BY group_id',(stream,offset)))
        maximum=32 if chr(event)=='L' else 100 if chr(event)=='G' else 0
        for (group,) in groups:
            if type(group)!=int or not 1<=group<=maximum:raise ValueError('Invalid member group relationship')
            at=12 if chr(event)=='L' else 8+4*((group-1)//32)
            value=int.from_bytes(out[at:at+4],'little')|(1<<((group-1)%32));out[at:at+4]=value.to_bytes(4,'little')
    return bytes(out)

def validate_relations(db):
    """Read-only relationship audit; no bodies, passwords or personal text returned."""
    counts={}
    for table,parent,events,size,fields in SPECS:
        condition='event_type IN ('+','.join(str(ord(c)) for c in events)+')' if events else '1'
        missing=db.execute(f'SELECT count(*) FROM {parent} p WHERE {condition} AND NOT EXISTS(SELECT 1 FROM {table} f WHERE f.stream=p.stream AND f.offset=p.offset)').fetchone()[0]
        wrong=db.execute(f'SELECT count(*) FROM {table} f LEFT JOIN {parent} p USING(stream,offset) WHERE p.stream IS NULL OR NOT ({condition})').fetchone()[0]
        if missing or wrong:raise ValueError('Missing/mismatched normalized relation: '+table)
        counts[table]=db.execute('SELECT count(*) FROM '+table).fetchone()[0]
    for parent in ('article_records','mail_records','member_records'):
        if db.execute(f"SELECT 1 FROM {parent} WHERE typeof(attributes)!='blob' OR length(attributes)!=0 LIMIT 1").fetchone():
            raise ValueError('Duplicated normalized attributes: '+parent)
    codes=','.join(str(ord(c)) for item in SPECS if item[1]=='member_records' for c in item[2])
    if db.execute('SELECT 1 FROM member_records WHERE event_type NOT IN ('+codes+') LIMIT 1').fetchone():raise ValueError('Unsupported member event')
    if db.execute("SELECT 1 FROM member_event_groups g JOIN member_records m USING(stream,offset) WHERE m.event_type NOT IN (76,71) OR (m.event_type=76 AND g.group_id>32) LIMIT 1").fetchone():raise ValueError('Invalid member group relationship')
    if db.execute("SELECT 1 FROM mail_records m JOIN mail_format f USING(stream,offset) WHERE (SELECT count(*) FROM mail_attachments a WHERE a.stream=m.stream AND a.offset=m.offset)!=CASE WHEN m.event_type IN (117,120) THEN f.attachment_count WHEN m.event_type IN (116,118) THEN 1 ELSE 0 END LIMIT 1").fetchone():raise ValueError('Invalid attachment relationship')
    if db.execute('SELECT 1 FROM blocks b JOIN record_streams r USING(stream) LIMIT 1').fetchone():raise ValueError('Duplicated structured stream blocks')
    return counts

def c_source():
    out=['/* Generated by scripts/normalized_fields.py --generate. Do not edit. */']
    for table,parent,events,size,fields in SPECS:
        out.append('static const NormalField '+table+'_fields[]={'+','.join('{"%s",%d,%d}'%f for f in fields)+'};')
    out.append('static const NormalSpec normal_specs[]={')
    for table,parent,events,size,fields in SPECS:
        out.append('    {"%s","%s","%s",%d,%d,%s_fields},'%(table,parent,events,size,len(fields),table))
    out.append('};')
    return '\n'.join(out)+'\n'

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--generate',action='store_true');p.add_argument('--check',action='store_true');a=p.parse_args()
    target=Path(__file__).resolve().parents[1]/'src/storage_normalized_fields.inc'
    if a.generate:target.write_text(c_source(),encoding='utf8',newline='\n')
    elif a.check:
        if target.read_text(encoding='utf8')!=c_source():raise SystemExit('Stale normalization descriptors')
    else:p.error('select --generate or --check')
