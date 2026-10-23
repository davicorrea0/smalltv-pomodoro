// Compile the actual upload handlers with an in-memory filesystem/server double.
const fs=require('fs'),path=require('path');
const source=fs.readFileSync(path.join(__dirname,'../smalltv-pomodoro/main.cpp'),'utf8');
function fn(name){const a=source.indexOf(`void ${name}() {`),b=source.indexOf('\n}',a);if(a<0||b<0)throw Error(name);return source.slice(a,b+2);}
const out=process.argv[2];if(!out)throw Error('output.cpp required');
const header=path.relative(path.dirname(path.resolve(out)),path.resolve(__dirname,'../smalltv-pomodoro/security.h')).replaceAll('\\','/');
fs.writeFileSync(out,`#include "${header}"
#include <string>
#include <cassert>
#include <cstdio>
#include <cctype>
#include <limits>
struct String:std::string {
 using std::string::string;using std::string::operator=;
 String(const std::string&s):std::string(s){}
 bool endsWith(const char*p)const{size_t n=strlen(p);return size()>=n&&compare(size()-n,n,p)==0;}
 void toLowerCase(){for(char &c:*this)c=tolower((unsigned char)c);}
 String substring(size_t a,size_t b=std::string::npos)const{return substr(a,b==std::string::npos?b:b-a);}
};
enum{UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload{int status;String name="face",filename="photo.jpg";size_t currentSize=1;uint8_t buf[1]={0};}event;
struct{HTTPUpload&upload(){return event;}}server;
static bool allowed=true,password=true,fsOk=true;
static int opens,writes,deletes,rebuilds;static size_t written;
bool panelOk(){return allowed;}bool passOk(){return password;}
struct FSInfo{size_t totalBytes=4000000,usedBytes=0;};
struct File{bool live=false;operator bool()const{return live;}void close(){live=false;}
 size_t write(const uint8_t*,size_t n){assert(live);writes++;written+=n;return n;}};
struct{bool exists(const String&){return true;}void mkdir(const char*){}File open(const String&,const char*){opens++;return {true};}
 void info(FSInfo&){}void remove(const String&){deletes++;}}LittleFS;
bool isJpg(String s){s.toLowerCase();return s.endsWith(".jpg")||s.endsWith(".jpeg");}
bool isGif(String s){s.toLowerCase();return s.endsWith(".gif");}
bool validGif(const String&){return true;}bool validJpg(const String&){return true;}void rebuildAlbum(){rebuilds++;}
const char*tr(const char*a,const char*,const char*){return a;}
File uploadFile,packUpload;String uploadPath,uploadError,packUploadError;
UploadGate photoGate,packGate;bool packFaceReady=false;
${fn('handleUpload')}
${fn('handlePackUpload')}
void reset(){allowed=password=true;opens=writes=deletes=rebuilds=0;written=0;uploadFile={};packUpload={};uploadPath="";uploadError="";packUploadError="";packFaceReady=false;photoGate={};packGate={};event={};}
void send(void(*handler)(),int status){event.status=status;handler();}
int main(){
 for(bool panel:{false,true}){reset();allowed=panel;password=false;
  send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);
  assert(!opens&&!writes&&!deletes);reset();allowed=panel;password=false;
  send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);
  assert(!opens&&!writes&&!deletes&&!packFaceReady);
 }
 reset();send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);assert(opens==1&&writes==1&&written==1);
 reset();send(handleUpload,UPLOAD_FILE_START);event.currentSize=3*1024*1024+1;send(handleUpload,UPLOAD_FILE_WRITE);assert(!writes&&deletes==1&&!photoGate.active);
 reset();send(handlePackUpload,UPLOAD_FILE_START);event.currentSize=16385;send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);assert(!writes&&!packFaceReady&&!packGate.active);
 reset();send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);assert(packFaceReady&&written==1);
 reset();send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_ABORTED);assert(!packFaceReady);
 reset();event.name="unknown";send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_WRITE);assert(!opens&&!writes);
 UploadGate gate;gate.begin(true,10);assert(gate.take(10));assert(!gate.take(1));assert(!gate.take(0));
 gate.begin(true,10);assert(!gate.take(std::numeric_limits<size_t>::max()));
 const char*good[]={"http://192.168.1.2/path?a=b","http://example.com:8080/hook","http://a/"};
 for(auto u:good)assert(safeHttpUrl(u,strlen(u)));
 const char*bad[]={"http://a/\\r\\nInjected:1","http://a:65536/","http://a:0/","http://a:/","http://u@host/","https://a/","http://a/fragment#x","http://a/ space","http://a/\\t"};
 for(auto u:bad)assert(!safeHttpUrl(u,strlen(u)));
 const char nul[]="http://a/\\0hidden";assert(!safeHttpUrl(nul,sizeof(nul)-1));
 puts("security: actual upload handlers deny unauthenticated writes; bounds, aborts and URL injection checks passed");
}
`);
