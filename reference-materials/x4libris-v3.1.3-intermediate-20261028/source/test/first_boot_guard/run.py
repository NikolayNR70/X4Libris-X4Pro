#!/usr/bin/env python3
# Build the production guard and OTA-switch code against persistent fake flash.
# Linux: python3 test/first_boot_guard/run.py (g++, OpenSSL development headers).
# Every boot is a fresh process; NVS and flash survive on disk.
from pathlib import Path
import tempfile,subprocess,os,sys
if len(sys.argv)>1: os.environ["FIRST_BOOT_TEST_IMAGE"]=str(Path(sys.argv[1]).resolve())
src=Path(__file__).resolve().parents[2]/'src'
with tempfile.TemporaryDirectory(prefix='first_boot_test_') as tmp:
 t=Path(tmp)
 def put(n,s):
  p=t/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
 put('Arduino.h','#pragma once\n#include <cstdint>\nvoid esp_restart();\n')
 put('Logging.h','#pragma once\n#define LOG_ERR(...) ((void)0)\n#define LOG_INF(...) ((void)0)\n')
 put('esp_partition.h',r'''#pragma once
#include <cstdint>
#include <cstddef>
using esp_err_t=int;constexpr int ESP_OK=0;
using esp_partition_subtype_t=int;
constexpr int ESP_PARTITION_TYPE_APP=0,ESP_PARTITION_TYPE_DATA=1,ESP_PARTITION_SUBTYPE_ANY=255,ESP_PARTITION_SUBTYPE_DATA_OTA=0,ESP_PARTITION_SUBTYPE_APP_OTA_0=16;
struct esp_partition_t {int type,subtype;uint32_t address,size;const char* label;};
using esp_partition_iterator_t=int;
esp_partition_iterator_t esp_partition_find(int,int,const char*);
const esp_partition_t* esp_partition_get(int);
int esp_partition_next(int);
void esp_partition_iterator_release(int);
const esp_partition_t* esp_partition_find_first(int,int,const char*);
int esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
int esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
int esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
''')
 put('esp_ota_ops.h',r'''#pragma once
#include "esp_partition.h"
using esp_ota_img_states_t=int;
constexpr int ESP_OTA_IMG_NEW=0,ESP_OTA_IMG_PENDING_VERIFY=1,ESP_OTA_IMG_VALID=2;
const esp_partition_t* esp_ota_get_running_partition();
const esp_partition_t* esp_ota_get_next_update_partition(const void*);
int esp_ota_get_state_partition(const esp_partition_t*,int*);
int esp_ota_mark_app_valid_cancel_rollback();
''')
 put('esp_timer.h',r'''#pragma once
using esp_timer_handle_t=void*;
struct esp_timer_create_args_t {void(*callback)(void*);const char* name;};
int esp_timer_create(esp_timer_create_args_t*,esp_timer_handle_t*);
int esp_timer_start_once(esp_timer_handle_t,unsigned long long);
int esp_timer_stop(esp_timer_handle_t);
''')
 put('nvs.h',r'''#pragma once
#include <cstddef>
using nvs_handle_t=int;constexpr int NVS_READWRITE=1;
int nvs_open(const char*,int,int*);
int nvs_get_blob(int,const char*,void*,size_t*);
int nvs_set_blob(int,const char*,const void*,size_t);
int nvs_commit(int);
int nvs_get_u32(int,const char*,uint32_t*);
int nvs_set_u32(int,const char*,uint32_t);
''')
 put('spi_flash_mmap.h','#pragma once\nconstexpr unsigned SPI_FLASH_SEC_SIZE=4096;\n')
 put('esp_rom_crc.h',r'''#pragma once
#include <cstdint>
#include <cstddef>
inline uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t* p,size_t n){crc=~crc;while(n--){crc^=*p++;for(int i=0;i<8;i++)crc=(crc>>1)^((crc&1)?0xedb88320:0);}return ~crc;}
''')
 put('mbedtls/sha256.h',r'''#pragma once
#include <openssl/sha.h>
using mbedtls_sha256_context=SHA256_CTX;
inline void mbedtls_sha256_init(mbedtls_sha256_context*){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* s,int is224){return !is224 && SHA256_Init(s)==1?0:1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* s,const unsigned char* p,size_t n){return SHA256_Update(s,p,n)==1?0:1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* s,unsigned char* d){return SHA256_Final(d,s)==1?0:1;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
''')
 put('main.cpp',r'''#include "FirstBootGuard.h"
#include "network/OtaBootSwitch.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include <openssl/sha.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdlib>
namespace fs=std::filesystem;
fs::path root;std::string mode;bool writeFail=false,nvsFail=false;int running=2;void(*timerCb)(void*)=nullptr;
struct Restart{};void esp_restart(){throw Restart{};}
esp_partition_t partitions[]={{1,0,0xe000,8192,"otadata"},{0,16,0x10000,0x640000,"ota_0"},{0,17,0x650000,0x640000,"ota_1"}};
fs::path file(const esp_partition_t* p){return root/p->label;}
int esp_partition_read(const esp_partition_t* p,size_t at,void* out,size_t n){if(at+n>p->size)return 1;std::ifstream f(file(p),std::ios::binary);f.seekg(at);f.read((char*)out,n);return f?0:1;}
int esp_partition_write(const esp_partition_t* p,size_t at,const void* in,size_t n){if(writeFail || at+n>p->size)return 1;std::fstream f(file(p),std::ios::binary|std::ios::in|std::ios::out);f.seekp(at);f.write((char*)in,n);return f?0:1;}
int esp_partition_erase_range(const esp_partition_t* p,size_t at,size_t n){std::vector<char> v(n,(char)255);return esp_partition_write(p,at,v.data(),n);}
int esp_partition_find(int,int,const char*){return 1;}
const esp_partition_t* esp_partition_get(int i){return &partitions[i];}
int esp_partition_next(int i){return i==1?2:0;}
void esp_partition_iterator_release(int){}
const esp_partition_t* esp_partition_find_first(int type,int subtype,const char*){for(auto& p:partitions)if(p.type==type&&p.subtype==subtype)return &p;return nullptr;}
const esp_partition_t* esp_ota_get_running_partition(){return &partitions[running];}
const esp_partition_t* esp_ota_get_next_update_partition(const void*){return &partitions[running==1?2:1];}
int esp_ota_get_state_partition(const esp_partition_t*,int* out){*out=1;return 0;}
int esp_ota_mark_app_valid_cancel_rollback(){return 0;}
int esp_timer_create(esp_timer_create_args_t* a,esp_timer_handle_t* t){timerCb=a->callback;*t=(void*)1;return 0;}
int esp_timer_start_once(esp_timer_handle_t,unsigned long long n){assert(n==180000000);return 0;}
int esp_timer_stop(esp_timer_handle_t){return 0;}
int nvs_open(const char*,int,int* h){*h=1;return nvsFail?1:0;}
int nvs_get_blob(int,const char*,void* out,size_t* n){std::ifstream f(root/"nvs",std::ios::binary);f.read((char*)out,*n);return f?0:1;}
int nvs_set_blob(int,const char*,const void* in,size_t n){if(nvsFail)return 1;std::ofstream f(root/"nvs",std::ios::binary);f.write((const char*)in,n);return f?0:1;}
int nvs_commit(int){return nvsFail?1:0;}
int nvs_get_u32(int,const char*,uint32_t* v){std::ifstream f(root/"seq",std::ios::binary);f.read((char*)v,4);return f?0:1;}
int nvs_set_u32(int,const char*,uint32_t v){if(nvsFail)return 1;std::ofstream f(root/"seq",std::ios::binary);f.write((char*)&v,4);return f?0:1;}
void image(int i){std::vector<unsigned char> v(partitions[i].size,255); if(i==2 && std::getenv("FIRST_BOOT_TEST_IMAGE")){std::ifstream in(std::getenv("FIRST_BOOT_TEST_IMAGE"),std::ios::binary);in.read((char*)v.data(),v.size());assert(in.gcount()>65536);std::ofstream out(file(&partitions[i]),std::ios::binary);out.write((char*)v.data(),v.size());return;}v[0]=0xe9;v[1]=1;v[12]=9;v[13]=0;v[23]=1;uint32_t n=65536;memcpy(v.data()+28,&n,4);memset(v.data()+32,i,n);size_t end=(32+n+16)&~15;SHA256(v.data(),end,v.data()+end);std::ofstream f(file(&partitions[i]),std::ios::binary);f.write((char*)v.data(),v.size());}
void init(){fs::remove_all(root);fs::create_directories(root);image(1);image(2);std::vector<char> d(8192,(char)255);std::ofstream f(root/"otadata",std::ios::binary);f.write(d.data(),d.size());f.close();ota_boot::SelectEntry e{};e.ota_seq=1;e.ota_state=2;e.crc=ota_boot::computeSeqCrc(1);esp_partition_write(&partitions[0],0,&e,32);e.ota_seq=2;e.ota_state=1;e.crc=ota_boot::computeSeqCrc(2);esp_partition_write(&partitions[0],4096,&e,32);}
int main(int argc,char** argv){assert(argc==3);root=argv[1];mode=argv[2];
 if(mode=="init"){init();return 0;}
 if(mode=="installNew") {
  assert(ota_boot::switchTo(&partitions[1]));
  ota_boot::SelectEntry e{};esp_partition_read(&partitions[0],0,&e,32);
  assert(e.ota_seq==3 && e.ota_state==ota_boot::kOtaImgNew);
  std::cout<<"PASS ordinary installation retains NEW trial state\n";return 0;
 }
 if(mode=="corrupt"){unsigned char bad=0;esp_partition_write(&partitions[1],100,&bad,1);}
 if(mode=="nvsfail")nvsFail=true;
 if(mode=="switchfail")writeFail=true;
 if(mode=="reinstall" || mode=="reinstallReused") {
  ota_boot::SelectEntry e{};e.ota_seq=mode=="reinstallReused"?1:3;e.ota_state=2;e.crc=ota_boot::computeSeqCrc(e.ota_seq);esp_partition_write(&partitions[0],0,&e,32);
  e.ota_seq=mode=="reinstallReused"?2:4;e.ota_state=1;e.crc=ota_boot::computeSeqCrc(e.ota_seq);esp_partition_write(&partitions[0],4096,&e,32);
 }
 if(mode=="upgrade" || mode=="repeatUpgrade")running=1;
 if(mode=="upgrade"){
  ota_boot::SelectEntry e{};e.ota_seq=3;e.ota_state=1;e.crc=ota_boot::computeSeqCrc(3);esp_partition_write(&partitions[0],0,&e,32);
  e.ota_seq=2;e.ota_state=2;e.crc=ota_boot::computeSeqCrc(2);esp_partition_write(&partitions[0],4096,&e,32);
 }
 if(mode=="noFallback"){
  ota_boot::SelectEntry e{};e.ota_seq=1;e.ota_state=4;e.crc=ota_boot::computeSeqCrc(1);esp_partition_write(&partitions[0],0,&e,32);
 }
 if(mode=="corruptCurrent"){unsigned char bad=0;esp_partition_read(&partitions[2],100,&bad,1);bad^=0x80;esp_partition_write(&partitions[2],100,&bad,1);}
 bool restarted=false;
 try{
  FirstBootGuard::begin();
  if(mode=="trial" || mode=="timeout" || mode=="reinstall" || mode=="reinstallReused") {assert(FirstBootGuard::active()&&FirstBootGuard::hasFallback());assert(!FirstBootGuard::prepareUpdate());if(mode=="timeout")timerCb(nullptr);}
  if(mode=="accept"){assert(FirstBootGuard::accept());assert(!FirstBootGuard::active());assert(FirstBootGuard::prepareUpdate());}
  if(mode=="auto") {
    for(unsigned t=0;t<60000;t+=100) {FirstBootGuard::observeHealthyLoop(t,false);assert(FirstBootGuard::active());}
    for(unsigned t=60000;t<90000;t+=100) {FirstBootGuard::observeHealthyLoop(t,true);assert(FirstBootGuard::active());}
    FirstBootGuard::observeHealthyLoop(90000,true);assert(!FirstBootGuard::active());assert(FirstBootGuard::prepareUpdate());
  }
  if(mode=="autoBlocked") {
    for(unsigned t=0;t<29000;t+=100) FirstBootGuard::observeHealthyLoop(t,true);
    FirstBootGuard::observeHealthyLoop(32000,true);assert(FirstBootGuard::active());
    timerCb(nullptr);
  }
  if(mode=="accepted")assert(!FirstBootGuard::active());
  if(mode=="corrupt" || mode=="noFallback"){assert(FirstBootGuard::active());assert(!FirstBootGuard::hasFallback());assert(!FirstBootGuard::accept());}
  if(mode=="reject")FirstBootGuard::rollback();
  if(mode=="switchfail"){FirstBootGuard::rollback();assert(FirstBootGuard::active());}
 }catch(Restart&){restarted=true;}
 if(mode=="autoBlocked" || mode=="repeat" || mode=="repeatReinstall" || mode=="timeout" || mode=="reject" || mode=="nvsfail" || mode=="corruptCurrent") {
  assert(restarted);ota_boot::SelectEntry e{};esp_partition_read(&partitions[0],0,&e,32);assert(e.ota_seq==(mode=="repeatReinstall"?5u:3u)&&e.ota_state==2&&e.crc==ota_boot::computeSeqCrc(e.ota_seq));
 }
 if(mode=="repeatUpgrade") {
  assert(restarted);ota_boot::SelectEntry e{};esp_partition_read(&partitions[0],4096,&e,32);assert(e.ota_seq==4 && e.ota_state==2);
 }
 if(mode=="timeout")assert(restarted);
 if(mode=="trial" || mode=="accept" || mode=="accepted" || mode=="corrupt" || mode=="switchfail")assert(!restarted);
 std::cout<<"PASS "<<mode<<"\n";
}
''')
 exe=t/'guard-test'
 subprocess.run(['g++','-std=c++20','-O1','-Wall','-Wextra','-Wno-deprecated-declarations','-DFREEINK_DEVICE_X4PRO=1','-I'+str(t),'-I'+str(src),str(t/'main.cpp'),str(src/'FirstBootGuard.cpp'),str(src/'network/OtaBootSwitch.cpp'),'-lcrypto','-o',str(exe)],check=True)
 state=t/'state'
 def run(mode):
  before={p.name:p.read_bytes() for p in state.glob('ota_*')}
  subprocess.run([str(exe),str(state),mode],check=True)
  if mode not in ('init','corrupt','corruptCurrent'):
   assert before=={p.name:p.read_bytes() for p in state.glob('ota_*')}, 'App slot bytes changed during boot/rollback' 
 for steps in [('installNew',),('auto','accepted'),('autoBlocked',),('trial','repeat'),('trial','repeat','reinstall','repeatReinstall'),('accept','accepted'),('timeout',),('timeout','reinstall','repeatReinstall'),('timeout','reinstallReused','repeat'),('reject',),('corrupt',),('nvsfail',),('switchfail',),('noFallback',),('corruptCurrent',),('accept','upgrade','repeatUpgrade','accepted')]:
  run('init')
  for step in steps:run(step)
 print('PASS: production guard + real OTA switch; persistent reboots, acceptance, timeout, rejection, hash corruption, NVS failure, flash-switch failure')
