import subprocess, time, sys, os
exe = r'D:\C++VS pro\QtNetworkChat\build-qqnt-ui-ninja\Debug\QtNetworkChat.exe'
proc = subprocess.Popen([exe], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8', errors='ignore')
print(f'PID:{proc.pid}')
time.sleep(4)
ret = proc.poll()
if ret is None:
    print('RUNNING')
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
    sys.exit(0)
else:
    out, err = proc.communicate(timeout=2)
    print(f'EXIT:{ret}')
    print('STDOUT:', out[:500])
    print('STDERR:', err[:500])
    sys.exit(ret)
