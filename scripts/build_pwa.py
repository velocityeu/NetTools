"""Build the shared arithmetic engine for the static PWA using Emscripten 4.0.15."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'website-preview/dist/app'
def main():
    OUT.mkdir(parents=True, exist_ok=True)
    compiler = os.environ.get('EMXX') or shutil.which('em++') or str(ROOT / 'out/emsdk/upstream/emscripten/em++.py')
    command = ([sys.executable, compiler] if compiler.endswith('.py') else [compiler])
    command += ['-std=c++20', '-Oz', '-fexceptions', '--bind', '-Iinclude', 'src/core/subnet.cpp', 'src/core/classification.cpp', 'src/web/bindings.cpp', '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sENVIRONMENT=web,worker,node', '-sALLOW_MEMORY_GROWTH=1', '-sMAXIMUM_MEMORY=268435456', '-sSTACK_SIZE=1048576', '-sFILESYSTEM=0', '-sMIN_SAFARI_VERSION=170000', '-o', str(OUT / 'engine.mjs')]
    subprocess.run(command, cwd=ROOT, check=True)
    print('Built PWA engine:', (OUT / 'engine.wasm').stat().st_size, 'bytes')
if __name__ == '__main__': main()
