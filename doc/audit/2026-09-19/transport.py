import ctypes as c, os, select, threading, time
lib=c.CDLL(os.path.abspath('lib/build/libcgra.so'))
lib.cgra_open.argtypes=[c.c_char_p,c.c_uint];lib.cgra_open.restype=c.c_void_p
for name in ['cgra_close','cgra_set_timeout','cgra_set_retries','cgra_get_stats','cgra_reset_stats','cgra_identify','cgra_configure','cgra_run','cgra_read_regs']:
 getattr(lib,name).argtypes=[c.c_void_p]+({'cgra_close':[],'cgra_set_timeout':[c.c_uint],'cgra_set_retries':[c.c_uint],'cgra_get_stats':[c.c_void_p],'cgra_reset_stats':[],'cgra_identify':[c.c_void_p],'cgra_configure':[c.c_void_p],'cgra_run':[c.c_uint8],'cgra_read_regs':[c.c_void_p]}[name])
lib.emu_new.restype=c.c_void_p;lib.emu_free.argtypes=[c.c_void_p]
lib.emu_feed.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t];lib.emu_drain.argtypes=[c.c_void_p,c.c_void_p,c.c_size_t];lib.emu_drain.restype=c.c_size_t
class Stats(c.Structure):_fields_=[(f,c.c_ulong) for f in ['transactions','retries','tx','rx']]

def slow_id(send):
 master,slave=os.openpty();dev=lib.cgra_open(os.ttyname(slave).encode(),115200);lib.cgra_set_timeout(dev,50)
 def peer():
  os.read(master,1)
  if send:
   for b in [0xCA,3,4,4,16]:time.sleep(.03);os.write(master,bytes([b]))
 t=threading.Thread(target=peer);t.start();start=time.monotonic();rc=lib.cgra_identify(dev,None);elapsed=time.monotonic()-start
 st=Stats();lib.cgra_get_stats(dev,c.byref(st));t.join();print('ID', 'slow' if send else 'silent', 'rc',rc,'elapsed_ms',round(elapsed*1000),'rx_counter',st.rx,'actual_rx',5 if send else 0)
 lib.cgra_close(dev);os.close(master);os.close(slave)
slow_id(True);slow_id(False)
master,slave=os.openpty();dev=lib.cgra_open(os.ttyname(slave).encode(),115200);lib.cgra_set_timeout(dev,50);lib.cgra_set_retries(dev,0)
emu=lib.emu_new();stop=False;dropped=False

def proxy():
 global dropped
 reply=c.create_string_buffer(256)
 while not stop:
  if not select.select([master],[],[],.02)[0]:continue
  data=os.read(master,256)
  # The host writes RUN's two bytes together on this local PTY.
  if data==b'\x04\x01' and not dropped:data=data[:1];dropped=True
  lib.emu_feed(emu,data,len(data));n=lib.emu_drain(emu,reply,256)
  if n:os.write(master,reply.raw[:n])
t=threading.Thread(target=proxy);t.start()
cfg=(c.c_uint32*16)();cfg[0]=1|(14<<16)|(4<<20)|(6<<23) # ACC const(1)
print('configure',lib.cgra_configure(dev,cfg))
rc=lib.cgra_run(dev,1);regs=(c.c_int16*16)();readrc=lib.cgra_read_regs(dev,regs)
print('dropped RUN argument:', 'run_rc',rc,'read_rc',readrc,'PE0',regs[0],'expected after promised reset',0)
stop=True;t.join();lib.cgra_close(dev);lib.emu_free(emu);os.close(master);os.close(slave)
