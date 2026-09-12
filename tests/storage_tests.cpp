#include "veu/storage.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace veu::storage;
int main() {
 int failures=0;
 auto check=[&](bool v,const char* name){if(!v){std::cerr<<"FAIL "<<name<<"\n";++failures;}};
 Plan p; p.active_tool=2;p.fields={{"address","2001:db8::1"},{"label","Line\n\"quoted\" \\ path \xf0\x9f\x98\x80"}};
 auto bytes=encode(p);auto q=decode(bytes);
 check(q.active_tool==2&&q.fields==p.fields,"UTF8 JSON roundtrip");
 check(decode("{\"format\":\"Velocity NetTools plan\",\"version\":1,\"activeTool\":0,\"fields\":{\"address\":\"192.0.2.1/24\"}}").fields.at("address")=="192.0.2.1/24","independent JSON");
 for(auto s:{"{}","{\"format\":\"Velocity NetTools plan\",\"version\":2,\"activeTool\":0,\"fields\":{}}","{\"format\":\"Velocity NetTools plan\",\"version\":1,\"activeTool\":0,\"fields\":{\"x\":\"\\uD800\"}}","{\"format\":\"Velocity NetTools plan\",\"version\":1,\"activeTool\":0,\"fields\":{}} trailing","{\"format\":\"Velocity NetTools plan\",\"version\":1,\"version\":1,\"activeTool\":0,\"fields\":{}}"}) {
  bool rejected=false;try{decode(s);}catch(const std::exception&){rejected=true;}check(rejected,"reject invalid plan");
 }
 check(decode("{\"format\":\"Velocity NetTools plan\",\"version\":1,\"activeTool\":0,\"fields\":{\"x\":\"\\ud83d\\ude00\"}}").fields.at("x")=="\xf0\x9f\x98\x80","surrogate decoding");
 check(csv_cell("=SUM(1,2)")=="\"'=SUM(1,2)\"","formula neutralisation");
 check(csv_cell("a\"b")=="\"a\"\"b\"","CSV quoting");
 check(csv_cell("\t+cmd")=="\"'\t+cmd\"","whitespace formula neutralisation");
 bool nul_value_rejected=false;
 try { Plan invalid; invalid.fields["x"]=std::string("a\0b",3); (void)encode(invalid); }
 catch(const std::exception&) { nul_value_rejected=true; }
 check(nul_value_rejected,"encoding rejects NUL field value");
 bool nul_key_rejected=false;
 try { Plan invalid; invalid.fields.emplace(std::string("a\0b",3),"x"); (void)encode(invalid); }
 catch(const std::exception&) { nul_key_rejected=true; }
 check(nul_key_rejected,"encoding rejects NUL field key");

 bool oversized_rejected=false;
 try {
  Plan oversized;
  oversized.fields["x"]=std::string(16u*1024*1024-106,'a');
  (void)encode(oversized);
 } catch(const std::exception&) { oversized_rejected=true; }
 check(oversized_rejected,"encoded plan cannot exceed read limit after final suffix");

 namespace fs=std::filesystem;
 const fs::path test_dir=fs::temp_directory_path()/(L"VelocityNetTools-storage-tests-"+std::to_wstring(GetCurrentProcessId()));
 std::error_code cleanup_error;
 fs::remove_all(test_dir,cleanup_error);
 fs::create_directories(test_dir);
 const fs::path destination=test_dir/L"plan.json";
 safe_save(destination,"old plan bytes");
 safe_save(destination,"new plan bytes");
 check(read(destination)=="new plan bytes","replacement publishes new bytes");
 bool recovery_found=false;
 for(const auto& entry:fs::directory_iterator(test_dir)) {
  const auto name=entry.path().filename().wstring();
  if(name.starts_with(L"plan.json.recovery-")&&read(entry.path())=="old plan bytes") recovery_found=true;
 }
 check(recovery_found,"successful replacement retains prior recovery bytes");

 const fs::path locked=test_dir/L"locked.json";
 safe_save(locked,"protected original");
 HANDLE lock=CreateFileW(locked.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 bool replacement_rejected=false;
 try { safe_save(locked,"replacement"); } catch(const std::exception&) { replacement_rejected=true; }
 if(lock!=INVALID_HANDLE_VALUE) CloseHandle(lock);
 check(replacement_rejected,"sharing conflict rejects replacement");
 check(read(locked)=="protected original","failed replacement preserves destination bytes");
 bool temporary_found=false;
 for(const auto& entry:fs::directory_iterator(test_dir)) {
  if(entry.path().filename().wstring().starts_with(L"locked.json.tmp-")) temporary_found=true;
 }
 check(temporary_found,"ambiguous replacement failure retains temporary recovery bytes");
 fs::remove_all(test_dir,cleanup_error); std::cout<<"Storage tests "<<(failures?"FAILED":"passed")<<"\n";return failures?1:0;
}
