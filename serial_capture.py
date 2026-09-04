import serial
import time
import sys

PORT = 'COM6'
BAUD = 115200
DURATION = 20

try:
    s = serial.Serial(PORT, BAUD, timeout=0.5)
except Exception as e:
    print("GAGAL buka %s: %s" % (PORT, e))
    sys.exit(1)

# Reset board via auto-reset circuit (pulse EN/RTS, IO0/DTR tetap high)
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False

print("Menangkap output serial %d detik (board di-reset)..." % DURATION)
out = []
end = time.time() + DURATION
while time.time() < end:
    data = s.read(2048)
    if data:
        txt = data.decode(errors='ignore')
        out.append(txt)
        print(txt, end='', flush=True)
s.close()

full = ''.join(out)
with open('serial_log.txt', 'w', encoding='utf-8') as f:
    f.write(full)

print("\n--- Selesai. Log disimpan ke serial_log.txt ---")
print("\n=== BARIS PENTING (Nav/SD/Session/Error) ===")
for line in full.splitlines():
    low = line.lower()
    if ('nav' in low or 'sd' in low or 'session' in low or
            'error' in low or 'fail' in low or 'guru' in low or
            'abort' in low or 'boot' in low):
        print(" >>", line)