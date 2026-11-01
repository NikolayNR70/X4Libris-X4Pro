#!/usr/bin/env python3
"""Verify unattended first-boot health and that no confirmation activity is routed."""
from pathlib import Path
import os, shutil, subprocess, tempfile
repo=Path(__file__).resolve().parents[2]
main=(repo/'src/main.cpp').read_text()
assert 'make_unique<FirstBootActivity>' not in main
assert 'FirstBootGuard::observeHealthyLoop' in main
with tempfile.TemporaryDirectory(prefix='auto_trial_health_') as tmp:
 p=Path(tmp)
 (p/'health.cpp').write_text('''#include "BootTrialHealth.h"
#include <cassert>
int main(){
 BootTrialHealth h;
 h.reset(0);
 for(unsigned t=0;t<5000;t+=100) h.observe(t,false);
 for(unsigned t=5000;t<30000;t+=100) h.observe(t,true);
 h.observe(30000,false);
 assert(!h.healthyAtDeadline(30000)); // Busy during the first deadline.
 h.observe(31000,true);
 assert(h.healthyAtDeadline(31000));  // Recovered UI can be accepted on retry.

 BootTrialHealth stall;
 stall.reset(0);
 for(unsigned t=0;t<29000;t+=100) stall.observe(t,true);
 assert(!stall.healthyAtDeadline(40000)); // A truly stuck loop is not healthy.
 stall.observe(41000,true);
 assert(stall.healthyAtDeadline(41000));  // One long book render is not fatal.

 BootTrialHealth wrap;
 wrap.reset(0xfffffff0u);
 wrap.observe(0xfffffff0u,true);
 wrap.observe(30000u - 16u,true);
 assert(wrap.healthyAtDeadline(30000u - 16u));
}
''')
 compiler = shutil.which('g++') or r'C:\msys64\ucrt64\bin\g++.exe'
 executable = p / 'health.exe'
 env = dict(os.environ, PATH=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH',''))
 subprocess.run([compiler,'-std=c++20','-I',str(repo/'src'),str(p/'health.cpp'),'-o',str(executable)],check=True,env=env)
 subprocess.run([str(executable)],check=True,env=env)
print('PASS: trial waits for responsive UI and recovers after an interaction; a stuck loop remains unhealthy')
