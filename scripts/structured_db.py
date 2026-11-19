"""Streaming logical export of schema-2 domain rows (one body at a time)."""
import struct
TABLES={1:'article_records',2:'mail_records',3:'member_records'}
TABLES.update(enumerate(('profile_events','board_access_events','download_events','userlog_events','chat_events','activity_events','board_policy_events','catalog_events','sigop_events','board_move_events','userlog_lock_events','moderation_events','content_events'),4))

def event_encode(kind,row):
    stream,offset,event,sequence,actor,target,created,text,checksum,flags,context=row
    text=text.encode('utf8') if isinstance(text,str) else text
    size=32 if kind==15 else 512 if kind==16 else 4096
    maximum=0 if kind==15 else 320 if kind==16 else 3000
    if not 0<event<256 or flags not in ((0,1) if kind>=15 else (0,)) or len(text)>maximum or b'\0' in text:raise ValueError('Invalid event row')
    r=bytearray(size);r[0]=event;r[1]=flags
    def put(at,n):struct.pack_into('<I',r,at,n)
    put(4,sequence);put(8 if kind>=15 else 12,target);put(16 if kind==15 else 12 if kind==16 else 8,actor)
    struct.pack_into('<Q',r,20 if kind==15 else 340 if kind==16 else 16,created)
    if kind>=15:put(12 if kind==15 else 348,context)
    if kind!=15:
        put(16 if kind==16 else 24,len(text));at=20 if kind==16 else 28;r[at:at+len(text)]=text
    value=2166136261
    for c in r[:-4]:value=((value^c)*16777619)&0xffffffff
    if checksum!=value:raise ValueError('Event checksum mismatch')
    put(size-4,checksum)
    return bytes(r)

def encode(kind,row):
    stream,offset,n,ident,parent,actor,to,created,title,body,login,salt,password,attr,event,sender=row
    if not 128<=n<=262400:raise ValueError('Invalid row length')
    title,body,login=(x.encode('utf8') if isinstance(x,str) else x for x in (title,body,login))
    if len(title)>80 or len(body)>262144 or len(login)>32:raise ValueError('Invalid row fields')
    r=bytearray(n)
    def put(at,num):struct.pack_into('<I',r,at,num&0xffffffff)
    if kind==1:
        if len(attr)!=64 or n<144+len(body):raise ValueError('Invalid article attributes')
        base=n-44;r[:16]=attr[:16];r[base-4:]=attr[16:]
        put(0,ident|(struct.unpack_from('<I',r)[0]&0x80000000));put(4,parent);put(8,len(title));put(12,len(body))
        r[16:16+len(title)]=title;r[96:96+len(body)]=body
        put(base-4,created);put(base,created>>32);r[base+4:base+4+len(login)]=login;put(base+36,to)
    elif kind==2:
        if not 48<=len(attr)<=112 or n<128+len(body):raise ValueError('Invalid mail attributes')
        r[:44]=attr[:44];r[n-(len(attr)-44):]=attr[44:]
        for at,num in ((8,ident),(12,actor),(16,sender),(20,to),(24,parent),(28,len(title)),(32,len(body)),(36,created),(40,created>>32)):put(at,num)
        r[44:44+len(title)]=title;r[124:124+len(body)]=body
    else:
        if chr(event) in 'Dd':
            if len(attr)!=36 or n<116+len(body):raise ValueError('Invalid draft attributes')
            r[:32]=attr[:32];r[-4:]=attr[32:];put(20,len(title));put(24,len(body));r[32:32+len(title)]=title;r[112:112+len(body)]=body
        else:
            if len(attr)!=128:raise ValueError('Invalid member attributes')
            r[:128]=attr
        put(4,ident)
        if chr(event) in 'AJ':r[8:8+len(login)]=login
        if chr(event) in 'AJPQ':
            if len(salt)!=16 or len(password)!=32:raise ValueError('Invalid credentials')
            r[40:56]=salt;r[56:88]=password
    return bytes(r)
def export(db,stream,kind,magic,length,out):
    offset=8;out.write(magic)
    if kind>=4:
        for row in db.execute('SELECT stream,offset,event_type,sequence,actor_id,target_id,created_at,CAST(text AS BLOB),checksum,flags,context_id FROM '+TABLES[kind]+' WHERE stream=? ORDER BY offset',(stream,)):
            if row[1]!=offset:raise ValueError('Missing event row')
            data=event_encode(kind,row);out.write(data);offset+=len(data)
        if offset!=length:raise ValueError('Truncated event stream')
        return
    version=db.execute('SELECT version FROM storage_meta WHERE id=1').fetchone()[0]
    for row in db.execute('SELECT stream,offset,length,record_id,parent_id,actor_id,recipient_id,created_at,CAST(title AS BLOB),CAST(body AS BLOB),CAST(login AS BLOB),salt,password_hash,attributes,event_type,sender_id FROM '+TABLES[kind]+' WHERE stream=? ORDER BY offset',(stream,)):
        if row[1]!=offset:raise ValueError('Missing structured record')
        if kind==2 and version>=4:
            attr=row[13]
            if len(attr)!=48:raise ValueError('Invalid normalized mail header')
            count=attr[2] if chr(attr[0]) in 'ux' else int(chr(attr[0]) in 'tv')
            if count>16:raise ValueError('Invalid attachment count')
            attachments=list(db.execute('SELECT ordinal,file_id FROM mail_attachments WHERE stream=? AND offset=? ORDER BY ordinal',(stream,row[1])))
            if len(attachments)!=count or any(i!=ordinal or not 1<=file<=2147483647 for i,(ordinal,file) in enumerate(attachments)):
                raise ValueError('Invalid attachment relationship')
            attr=attr[:44]+b''.join(struct.pack('<I',file) for _,file in attachments)+attr[44:]
            row=(*row[:13],attr,*row[14:])
        encoded=encode(kind,row);out.write(encoded);offset+=len(encoded)
    if offset!=length:raise ValueError('Truncated structured stream')
