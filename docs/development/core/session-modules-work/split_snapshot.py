from pathlib import Path
import re,subprocess,collections
root=Path('.')
s=subprocess.check_output(['git','show','HEAD:src/session.c']).decode('utf-8')
inc=subprocess.check_output(['git','show','HEAD:src/classic_commands.inc']).decode('utf-8')
def mask(t):
 return re.sub(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),t)
def renamed(t,mapping):
 hits=list(re.finditer(r'\b(?:'+ '|'.join(mapping)+r')\b',mask(t)))
 for hit in reversed(hits):t=t[:hit.start()]+mapping[hit[0]]+t[hit.end():]
 return t
def close(t,a):
 m=mask(t);depth=0
 for i in range(a,len(t)):
  if m[i]=='{':depth+=1
  elif m[i]=='}':
   depth-=1
   if depth==0:return i+1
 raise ValueError('brace')
def functions(t):
 out={}
 for mt in re.finditer(r'^(?:static )?[A-Za-z_][\w *]*?\b(\w+)\([^;\n]*\) \{',mask(t),re.M):
  end=close(t,mt.end()-1);out[mt[1]]=t[mt.start():end]
 return out
f=functions(s);f.update(functions(inc));original=set(f)
modules=collections.defaultdict(list)
def take(module,*names):
 for n in names:modules[module].append((n,f.pop(n)))
def add(module,name,body,args='Session *s,char *line',chain=False):
 body=re.sub(r'\breturn\s*;', 'return 1;',body)
 if chain:body+='\n    else return 0;\n    return 1;'
 else:body+='\n    return 0;'
 modules[module].append((name,'int '+name+'('+args+') {\n'+body+'\n}'))
def segment(t,a,b):return t[t.index(a):t.index(b)]
# Extract stateful input phases before ordinary named commands, preserving ordering.
c=f.pop('command_core');body=c[c.index('{')+1:c.rfind('}')]
a=body.index('    if(members_auto_ids()');add('session_auth','application_input',body[:a])
a=body.index('    if(s->editing && s->memo_input)');b=body.index("    if(s->password_mode=='f'")
pre=body[a:b].replace('    char out[128];\n','').replace('    char editor_aux[INPUT_LINE_MAX+2];\n','').replace('sizeof(editor_aux)','(INPUT_LINE_MAX+2)').replace('editor_aux','scratch')
pre='    char *line=*input;\n'+pre+'    *input=line;\n'
add('session_editor','editor_preinput',pre,'Session *s,char **input,char *scratch')
a=b;b=body.index('    if(s->editing && (!strcmp(line,"/#")')
add('session_auth','password_input',body[a:b])
a=b;b=body.index('    if(!strncmp(line,"BOARDOPTIONS"')
add('session_editor','editor_input',body[a:b])
# Named commands remain in exact legacy precedence. Helpers return handled, never authorization.
markers=[
('session_board','board_options','    if(!strncmp(line,"BOARDOPTIONS"'),
('session_auth','password_reset_command','    if(!strncmp(line,"RESETPASS "'),
('session_editor','editor_mode_command','    if(!strcmp(line,"EDITOR LINE")'),
(None,'service_command','    if(service_command(s,line))return;'),
(None,'mail_command','    if(mail_command(s,line))return;'),
('session_chat','conversation_command','    if(!strcmp(line,"TALK")'),
(None,'extended_command','    if(extended_command(s,line)) return;'),
('session_editor','draft_command','    if(!strcmp(line,"DRAFT")'),
('session_members','member_rights_command','    if(!strncmp(line,"REJECT "'),
('session_settings','preference_command','    if(!strcmp(line,"PREFS")'),
('session_reading','search_command','    if(!strcmp(line,"SEARCH")'),
('session_auth','password_change_command','    if(!strcmp(line,"PASSWD")'),
('session_transfer','transfer_command','    if(!strcmp(line,"BATCH")'),
('session_vault','vault_command','    if(!strcmp(line,"FILES")'),
('session_reading','audit_request','    if(!strcmp(line,"AUDIT")'),
('session_members','member_directory_command','    if(!strcmp(line,"MEMBERS")'),
('session_menu','door_command','    if(!strcmp(line,"GAME")'),
('session_settings','menu_settings_command','    if(!strncmp(line,"MENUMODE "'),
('session_board','board_structure_command','    if(!strncmp(line,"MOVEBRANCH "'),
('session_transfer','mail_batch_command','    if(!strcmp(line,"ZMAILUPLOADBATCH")'),
('session_auth','account_command','    if(!strncmp(line,"LOGIN "')]
order=[]
for i,(module,name,mark) in enumerate(markers):
 order.append(name)
 if not module:continue
 a=body.index(mark);b=body.index(markers[i+1][2]) if i+1<len(markers) else len(body)
 if name=='account_command':
  b=body.index(' else if(!strcmp(line,"NEXTBOARD"))',a)
  add(module,name,'    char out[128];\n'+body[a:b],chain=True)
 else:add(module,name,body[a:b])
a=body.index(' else if(!strcmp(line,"NEXTBOARD"))');b=body.index(' else if(!strcmp(line,"PING"))',a)
add('session_reading','article_command','    if'+body[a+8:b],chain=True)
# Utilities are the final catch-all, keeping the existing unknown-command response.
add('session_commands','utility_command','    char out[128];\n    if'+body[b+8:])
order+=['article_command','utility_command']
dispatch='''void command_core(Session *s,char *line) {
    if(application_input(s,line))return;
    if(members_auto_ids() && !strcmp(line,"REGISTER"))line="REGISTER auto";
    char scratch[INPUT_LINE_MAX+2];
    if(editor_preinput(s,&line,scratch) || password_input(s,line) || editor_input(s,line))return;
    static int (*const handlers[])(Session *,char *)={
'''+''.join('        '+n+',\n' for n in order)+'''    };
    for(size_t i=0;i<sizeof(handlers)/sizeof(*handlers);i++)if(handlers[i](s,line))return;
}'''
# Existing read-only command entry points use const text; wrapper signatures remain compatible.
for n in ['service_command','mail_command','extended_command']:
 f[n]=f[n].replace('const char *line','char *line',1)
modules['session_commands'].append(('command_core',dispatch))
# Ordered completion effects: all are behind session_result's identity gate.
r=f.pop('session_result');rb=r[r.index('{')+1:r.rfind('}')]
ranges=[
('session_reading','traversal_result',"    if(completion->service_action=='n')"),
('session_transfer','mail_batch_result','    if(!strncmp(completion->text,"MAILBATCH"'),
('session_vault','vault_children_result','    if(!strncmp(completion->text,"FILECHILDREN "'),
('session_chat','status_result','    if(s->status_pending)'),
('session_reading','profile_walk_result','    if(s->profile_walk.pending)'),
('session_vault','batch_copy_result','    if(s->batch_copying)'),
('session_reading','report_result','    if(s->report.pending)'),
(None,None,'    if(!strncmp(completion->text,"FILEBOARD UNLOCKED "'),
('session_vault','vault_edit_result','    if(!strncmp(completion->text,"BATCHMATCH"'),
('session_services','message_result','    if(s->gather.pending)'),
('session_mail','mail_result','    if(s->mail_pending)'),
('session_chat','chat_result','    if(s->chat_pending)'),
('session_transfer','transfer_result','    if(s->zmodem_start)'),
('session_connector','binary_result','    if(completion->binary_enter)'),
('session_auth','authentication_result','    if(completion->preferences)'),
('session_board','board_unlock_result',"    if(completion->service_action=='1')"),
('session_editor','buffer_result',"    if(completion->service_action=='8')"),
('session_auth','password_prompt_result',"    if(completion->service_action=='3')"),
('session_editor','board_editor_result',"    if(completion->service_action=='4'"),
('session_auth','account_flags_result',"    if(completion->service_action=='p')"),
('session_reading','article_result','    if(completion->board)')]
result=rb[:rb.index(ranges[0][2])]
for i,(module,name,mark) in enumerate(ranges):
 a=rb.index(mark);b=rb.index(ranges[i+1][2]) if i+1<len(ranges) else len(rb)
 code=rb[a:b]
 if module:
  # Every completion helper gets the same time value; explicitly tolerate unused time.
  add(module,name,'    (void)now;\n'+code,'Session *s,const BoardResult *completion,uint64_t now')
  result+='    if('+name+'(s,completion,now))return;\n'
 else:result+=code
modules['session_results'].append(('session_result','void session_result(Session *s,const BoardResult *completion,uint64_t now) {\n'+result+'}'))
take('session_output','binary_frame','queue_text','page_clear','page_next','emit','show_menu','prompt','show_help')
take('session_parse','number_arg','audit_number','audit_arguments','audit_command','safe_text','find_arguments')
take('session_settings','session_preferences','preferences_request','session_disconnect_preferences','session_ui_signature')
take('session_reading','check_page','profile_continue','session_auto_read','session_scan_pause','session_board_refresh','session_access_refresh','session_scan_stop')
take('session_extended','extended_command')
take('session_editor','editor_document','undo_clear','edit_apply','editor_error','editor_view_write','edit_draft','editor_count','classic_editor')
take('session_chat','chat_prompt','chat_event','chat_error','talk_number','talk_start','session_status','chat_input','session_chat_kick')
take('session_mail','mail_event','mail_list_request','mail_submit','mail_command')
take('session_services','service_command','gather_input')
take('session_auth','application_prompt')
take('session_menu','command','session_door_finish')
take('session_classic','classic_run','classic_ask','classic_read','profile_fields','classic_input','classic_commands')
take('session_connector','classic_input_state','session_uppercase','normalize_command','connector_transfer','connector_link','connector_line','session_binary_byte')
take('session_transfer','session_transfer_receipt','session_zmodem_finish')
# Only lifecycle / submit / incoming-line gate / timers remain in public coordinator.
for name in list(f):take('session',name)
# Include public and private contracts. Cross-module linkage is discovered, not globally exposed.
locations={name:module for module,items in modules.items() for name,_ in items}
public=set(re.findall(r'^(?:void|int|uint64_t) (session_\w+)\(',Path('src/session.h').read_text(),re.M))
cross=set()
for module,items in modules.items():
 for name,code in items:
  for called in re.findall(r'\b(\w+)\s*\(',mask(code)):
   if called in locations and locations[called]!=module:cross.add(called)
  # Function pointers as data also cross modules.
  for called in locations:
   if locations[called]!=module and re.search(r'\b'+called+r'\b',mask(code)):cross.add(called)
rename={n:'session_impl_'+n for n in cross if n not in public}
# Some local helpers have to precede callers within their TU; forward declarations are private/static.
prototypes=[];counts={}
includes=s[:s.index('void session_abort')].replace('#include "session.h"','#include "session_internal.h"')
enums=s[s.index('enum { CI_NONE'):s.index('/* Do not carry per-account')]
for module,items in modules.items():
 texts=[];local=[]
 for name,code in items:
  code=re.sub(r'^static ', '',code)
  proto=code[:code.index('{')].strip()+';'
  if name not in public and name not in cross:code='static '+code;local.append('static '+proto)
  elif name not in public:prototypes.append(proto)
  texts.append(code)
 t=includes+'\n'+('\n'.join(local)+'\n\n' if local else '')+'\n\n'.join(texts)+'\n'
 t=renamed(t,rename)
 Path('src/'+module+'.c').write_text(t,encoding='utf-8',newline='\n');counts[module]=len(t.splitlines())
h='''#ifndef SESSION_INTERNAL_H
#define SESSION_INTERNAL_H
/* Private adapter collaboration only. Runtime/worker/transport use session.h.
 * All handlers run synchronously on one Session event loop. A handled result
 * stops routing; it is NOT an authorization decision. Worker ACLs remain final.
 * Completion handlers must only be called after session_result validates identity.
 * Borrowed input/result pointers may not be retained. submit copies requests.
 */
#include "session.h"
#include "editor_core.h"
#include "chat.h"
'''+enums+'\n'+'\n'.join(prototypes)+'\n#endif\n'
h=renamed(h,rename)
Path('src/session_internal.h').write_text(h,encoding='utf-8',newline='\n')
# Classic bindings now compile as a normal translation unit, never textual inclusion.
Path('src/classic_commands.inc').unlink(missing_ok=True)
p=Path('Makefile');mk=subprocess.check_output(['git','show','HEAD:Makefile']).decode('utf-8');names=sorted(modules)
mk=mk.replace('src/session.c','$(SESSION_SOURCES)').replace('src/classic_commands.inc','src/session_internal.h')
mk='SESSION_SOURCES= '+' '.join('src/'+n+'.c' for n in names)+'\n\n'+mk
p.write_text(mk,encoding='utf-8',newline='\n')
print(counts);print('private cross-module calls',len(prototypes),'original functions',len(original))
