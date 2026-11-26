#!/usr/bin/env python3
# project: wup-stream-server-linux
# commit: Add standalone GamePad flight recorder collector
import argparse, datetime, json, pathlib, socket, struct, time
p=argparse.ArgumentParser();p.add_argument('--pad-ip');p.add_argument('--output',type=pathlib.Path,default=pathlib.Path('wup-diagnostics.jsonl'));a=p.parse_args()
stages={1:'firmware sleep',2:'recvfrom',3:'packet dispatch',4:'tick',10:'decoder stop begin',11:'decoder stop end',12:'decoder start begin',13:'decoder start end',20:'IDR assembly begin',21:'ring copy/cache clean',22:'ring copy complete',30:'frame submit',31:'frame submit complete',40:'session end begin',41:'session end complete',42:'session start',50:'decoder watchdog',51:'audio queue',52:'audio queue complete'}
s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('',28804));s.settimeout(.25);last={};warned=set()
def stamp():return datetime.datetime.now(datetime.timezone.utc).isoformat()
a.output.parent.mkdir(parents=True,exist_ok=True)
with a.output.open('a',buffering=1) as out:
 def save(record):out.write(json.dumps({'utc':stamp(),**record})+'\n')
 print(f'Listening UDP 28804 -> {a.output}',flush=True)
 try:
  while True:
   try:
    data,addr=s.recvfrom(2048);ip=addr[0]
    if a.pad_ip and ip!=a.pad_ip:continue
    if len(data)!=1056:continue
    v=struct.unpack('<264I',data)
    if v[0]!=0x31444657 or v[6]!=64:continue
    serial,progress,stage,unchanged,seq=v[1:6]
    events=[dict(seq=q,stage=st,name=stages.get(st,'unknown'),a=x,b=y) for q,st,x,y in zip(v[8::4],v[9::4],v[10::4],v[11::4]) if q]
    events.sort(key=lambda e:(seq-e['seq']) & 0xffffffff,reverse=True)
    save(dict(ip=ip,serial=serial,progress=progress,stage=stage,name=stages.get(stage,'unknown'),unchanged=unchanged,events=events))
    last[ip]=(time.monotonic(),stage,seq)
    if unchanged>=4:
     if (ip,'stall') not in warned:print(f'{ip}: MAIN LOOP STALLED at {stages.get(stage,stage)}',flush=True);warned.add((ip,'stall'))
    else:warned.discard((ip,'stall'))
    warned.discard((ip,'silence'))
   except socket.timeout:pass
   for ip,(t,stage,seq) in last.items():
    if time.monotonic()-t>2 and (ip,'silence') not in warned:
     warned.add((ip,'silence'));save(dict(ip=ip,error='heartbeat missing',stage=stage,seq=seq));print(f'{ip}: HEARTBEAT LOST (or stream stopped), last stage {stages.get(stage,stage)}',flush=True)
 except KeyboardInterrupt:print('\nCollector stopped.')
