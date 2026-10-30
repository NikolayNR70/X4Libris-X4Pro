#!/usr/bin/env python3
"""Verify unattended first-boot health and that no confirmation activity is routed."""
from pathlib import Path
import subprocess,tempfile
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
 for(unsigned t=0;t<5000;t+=100) assert(!h.observe(t,false));
 for(unsigned t=5000;t<35000;t+=100) assert(!h.observe(t,true));
 assert(h.observe(35000,true));
 BootTrialHealth stall;
 for(unsigned t=0;t<29000;t+=100) assert(!stall.observe(t,true));
 assert(!stall.observe(40000,true));
 for(unsigned t=40100;t<70000;t+=100) assert(!stall.observe(t,true));
 assert(stall.observe(70000,true));
}
''')
 subprocess.run(['g++','-std=c++20','-I',str(repo/'src'),str(p/'health.cpp'),'-o',str(p/'health')],check=True)
 subprocess.run([str(p/'health')],check=True)
print('PASS: unattended acceptance requires 30 seconds of responsive rendered UI; a stalled loop restarts probation; no confirmation activity is routed')
