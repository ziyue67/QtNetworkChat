import os, subprocess, sys

vcvars = r'"D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64'
proc = subprocess.run(vcvars + ' && set', shell=True, capture_output=True, text=True)
for line in proc.stdout.splitlines():
    if '=' in line:
        k, v = line.split('=', 1)
        if k == 'PATH':
            os.environ['PATH'] = v + ';' + os.environ.get('PATH', '')
        else:
            os.environ[k] = v

build_dir = r'D:\C++VS pro\QtNetworkChat\build-qqnt-ui-ninja'
os.chdir(build_dir)
log_path = r'D:\C++VS pro\QtNetworkChat\build-clean-final3.log'
with open(log_path, 'w', encoding='utf-8') as log:
    proc = subprocess.run([
        'cmake', '--build', '.', '--clean-first', '--config', 'Debug', '--target', 'QtNetworkChat', '-j4'
    ], stdout=log, stderr=subprocess.STDOUT, text=True)
    print(f'EXIT:{proc.returncode}')
    sys.exit(proc.returncode)
