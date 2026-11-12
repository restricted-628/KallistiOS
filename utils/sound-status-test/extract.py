from pathlib import Path
import re,hashlib,json,sys
p=Path(sys.argv[1]) if len(sys.argv)>1 else Path('../../kernel/arch/dreamcast/sound/snd_iface.c')
s=p.read_text();parts=[];records=[]
for name in (['channel_config_unpack','channel_status_thread_ready','channel_status_read_once','snd_channel_get_status_ex'] if 'channel_status_read_once' in s else ['channel_config_unpack','snd_channel_get_status_ex']):
 m=re.search(r'^(?:static )?(?:void|int|bool) '+name+r'\([^;]+?\) \{',s,re.M);assert m,name
 end=m.end();depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 body=s[m.start():end];parts.append(body);records.append({'function':name,'sha256':hashlib.sha256(body.encode()).hexdigest()})
Path('exact.inc').write_text('\n\n'.join(parts)+'\n');Path('extraction.json').write_text(json.dumps(records,indent=2)+'\n')
