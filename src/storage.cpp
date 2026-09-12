#include "veu/storage.hpp"
#include <windows.h>
#include <objbase.h>
#include <stdexcept>
#include <limits>
#include <charconv>
namespace veu::storage {
constexpr size_t limit=16u*1024*1024;
void fail(const char* m){throw std::runtime_error(m);}
void valid_utf8(std::string_view s) {
 if(s.size()>limit)fail("Plan exceeds the 16 MiB limit.");
 if(s.find('\0')!=std::string_view::npos)fail("NUL is not supported in plan fields.");
 if(!s.empty()&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0))fail("Invalid UTF-8.");
}
std::string quote(std::string_view s){
 valid_utf8(s);std::string r="\""; const char hex[]="0123456789abcdef";
 for(unsigned char c:s){switch(c){case '"':r+="\\\"";break;case '\\':r+="\\\\";break;case '\n':r+="\\n";break;case '\r':r+="\\r";break;case '\t':r+="\\t";break;default:if(c<32){r+="\\u00";r+=hex[c>>4];r+=hex[c&15];}else r+=static_cast<char>(c);}}return r+"\"";
}
struct Parser{
 std::string_view s;size_t i=0;
 void ws(){while(i<s.size()&&(s[i]==' '||s[i]=='\r'||s[i]=='\n'||s[i]=='\t'))++i;}
 bool take(char c){ws();if(i<s.size()&&s[i]==c){++i;return true;}return false;}
 void need(char c){if(!take(c))fail("Malformed plan JSON.");}
 unsigned hex4(){unsigned v=0;for(int j=0;j<4;j++){if(i==s.size())fail("Incomplete JSON escape.");char c=s[i++];v*=16;if(c>='0'&&c<='9')v+=c-'0';else if(c>='a'&&c<='f')v+=c-'a'+10;else if(c>='A'&&c<='F')v+=c-'A'+10;else fail("Invalid JSON escape.");}return v;}
 void utf(std::string& r,unsigned v){if(v<128)r+=char(v);else if(v<2048){r+=char(192|(v>>6));r+=char(128|(v&63));}else if(v<65536){r+=char(224|(v>>12));r+=char(128|((v>>6)&63));r+=char(128|(v&63));}else{r+=char(240|(v>>18));r+=char(128|((v>>12)&63));r+=char(128|((v>>6)&63));r+=char(128|(v&63));}}
 std::string str(){need('"');std::string r;while(i<s.size()){unsigned char c=s[i++];if(c=='"')return r;if(c<32)fail("Control character in JSON string.");if(c!='\\'){r+=char(c);continue;}if(i==s.size())fail("Incomplete JSON escape.");switch(s[i++]){case '"':r+='"';break;case '\\':r+='\\';break;case '/':r+='/';break;case 'b':r+='\b';break;case 'f':r+='\f';break;case 'n':r+='\n';break;case 'r':r+='\r';break;case 't':r+='\t';break;case 'u':{unsigned v=hex4();if(v>=0xd800&&v<=0xdbff){if(i+2>s.size()||s.substr(i,2)!="\\u")fail("Unpaired Unicode surrogate.");i+=2;unsigned lo=hex4();if(lo<0xdc00||lo>0xdfff)fail("Unpaired Unicode surrogate.");v=0x10000+((v-0xd800)<<10)+(lo-0xdc00);}else if(v>=0xdc00&&v<=0xdfff)fail("Unpaired Unicode surrogate.");if(v==0)fail("NUL is not supported in plan fields.");utf(r,v);break;}default:fail("Invalid JSON escape.");}}fail("Unclosed JSON string.");return{};}
 unsigned num(){ws();size_t start=i;while(i<s.size()&&s[i]>='0'&&s[i]<='9')++i;if(i==start||(i-start>1&&s[start]=='0'))fail("Invalid plan number.");unsigned v=0;auto [p,e]=std::from_chars(s.data()+start,s.data()+i,v);if(e!=std::errc())fail("Plan number out of range.");return v;}
};
std::string encode(const Plan& p){if(p.active_tool>63||p.fields.size()>10000)fail("Plan limit exceeded.");std::string r="{\n  \"format\": \"Velocity NetTools plan\",\n  \"version\": 1,\n  \"activeTool\": "+std::to_string(p.active_tool)+",\n  \"fields\": {";bool first=true;for(auto& [k,v]:p.fields){r+=first?"\n    ":",\n    ";first=false;r+=quote(k)+": "+quote(v);if(r.size()>limit)fail("Plan exceeds the 16 MiB limit.");}r+="\n  }\n}\n";if(r.size()>limit)fail("Plan exceeds the 16 MiB limit.");return r;}
Plan decode(std::string_view input){valid_utf8(input);if(input.substr(0,3)=="\xef\xbb\xbf")input.remove_prefix(3);Parser p{input};Plan result;bool format=false,version=false,active=false,fields=false;p.need('{');std::map<std::string,bool> seen;
 if(!p.take('}')){do{auto k=p.str();if(!seen.emplace(k,true).second)fail("Duplicate plan key.");p.need(':');if(k=="format"){if(p.str()!="Velocity NetTools plan")fail("Not a Velocity NetTools plan.");format=true;}else if(k=="version"){if(p.num()!=1)fail("Unsupported plan version.");version=true;}else if(k=="activeTool"){result.active_tool=p.num();if(result.active_tool>63)fail("Invalid active tool.");active=true;}else if(k=="fields"){fields=true;p.need('{');if(!p.take('}')){do{auto key=p.str();p.need(':');auto value=p.str();if(!result.fields.emplace(key,value).second)fail("Duplicate field.");if(result.fields.size()>10000)fail("Too many plan fields.");}while(p.take(','));p.need('}');}}else fail("Unknown plan member.");}while(p.take(','));p.need('}');}
 p.ws();
 if(p.i!=input.size()||!format||!version||!active||!fields)fail("Incomplete or malformed plan.");return result;
}
std::string csv_cell(std::string_view s){std::string r="\"";size_t i=0;while(i<s.size()&&(s[i]==' '||s[i]=='\t'||s[i]=='\r'||s[i]=='\n'))++i;if(i<s.size()&&(s[i]=='='||s[i]=='+'||s[i]=='-'||s[i]=='@'))r+="'";for(char c:s){if(c=='"')r+='"';r+=c;}return r+"\"";}
std::string read(const std::filesystem::path& p){HANDLE h=CreateFileW(p.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)fail("Cannot open plan file.");LARGE_INTEGER n{};if(!GetFileSizeEx(h,&n)||n.QuadPart<0||n.QuadPart>limit){CloseHandle(h);fail("File is too large.");}std::string s(static_cast<size_t>(n.QuadPart),'\0');DWORD count=0;bool ok=ReadFile(h,s.data(),static_cast<DWORD>(s.size()),&count,nullptr)!=0;CloseHandle(h);if(!ok||count!=s.size())fail("Could not read the complete file.");return s;}
std::string path_utf8(const std::filesystem::path& p){auto s=p.u8string();return std::string(s.begin(),s.end());}
void safe_save(const std::filesystem::path& destination,std::string_view bytes){
 if(bytes.size()>256u*1024*1024)fail("Export exceeds 256 MiB.");
 GUID id{};if(FAILED(CoCreateGuid(&id)))fail("Could not create a temporary name.");wchar_t g[40]{};StringFromGUID2(id,g,40);
 auto temp=destination;temp+=std::wstring(L".tmp-")+g;auto backup=destination;backup+=std::wstring(L".recovery-")+g;
 HANDLE h=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)fail("Cannot create a sibling temporary file. Choose a writable location.");
 DWORD wrote=0;bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size();
 if(ok)ok=FlushFileBuffers(h)!=0;CloseHandle(h);if(!ok){DeleteFileW(temp.c_str());fail("Writing failed; the destination was not replaced.");}
 bool existed=GetFileAttributesW(destination.c_str())!=INVALID_FILE_ATTRIBUTES;
 BOOL done=existed?ReplaceFileW(destination.c_str(),temp.c_str(),backup.c_str(),0,nullptr,nullptr):MoveFileExW(temp.c_str(),destination.c_str(),MOVEFILE_WRITE_THROUGH);
 if(!done)throw std::runtime_error("Save could not be completed. Preserve recovery files and inspect these paths:\n"+path_utf8(destination)+"\n"+path_utf8(temp)+"\n"+path_utf8(backup));
 // Retain the old destination as a recovery copy; never risk destroying it on a failed replacement.
}
}
