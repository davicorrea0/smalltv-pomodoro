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
 int indexOf(const char*p)const{size_t i=find(p);return i==npos?-1:(int)i;}
 void replace(const char*a,const char*b){for(size_t i;(i=find(a))!=npos;)std::string::replace(i,strlen(a),b);}
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
 reset();event.filename="a..b.jpg";send(handleUpload,UPLOAD_FILE_START);assert(uploadPath=="/image/a.b.jpg");
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
 const char*hosts[]={"192.168.1.2","192.168.1.2:80","0.0.0.0","255.255.255.255:65535"};
 for(auto h:hosts)assert(safeIpv4Host(h,strlen(h)));
 const char*badHosts[]={"smalltv.local","999.1.1.1","1.2.3","1.2.3.4:0","1.2.3.4:65536","1.2.3.4:80:1","1.2.3.4 ",""};
 for(auto h:badHosts)assert(!safeIpv4Host(h,strlen(h)));
 // hora do clima
 assert(isoToEpoch("1970-01-01T00:00")==0);assert(isoToEpoch("2026-10-23T00:30")==1792715400LL);
 assert(isoToEpoch("2026-13-01T00:00")==-1&&isoToEpoch("2026-10-23")==-1&&isoToEpoch(nullptr)==-1&&isoToEpoch("2026-10-23T0a:30")==-1);
 {const int64_t T=1792715400LL;  // TimeGuard: hora confirmada pelo clima; salto do SNTP desfeito até confirmar
  TimeGuard g;uint32_t ms=1000;
  assert(g.onSntp(T,ms,false)==T&&!g.ok(T,ms));                       // logo ao ligar: mostra, não grava
  assert(g.onWeather(T-300,T+5,ms+5000)==T+5&&g.ok(T+5,ms+5000));        // clima confirma
  ms+=3600000;int64_t now=T+5+3600-5;
  assert(g.onSntp(now+3,ms,false)==now+3&&g.ok(now+3,ms));             // correção pequena: aceita
  int64_t fake=now-40*86400;                                           // pacote forjado: 40 dias atrás
  assert(g.onSntp(fake,ms+10,false)==now+3&&g.undone==1&&g.ok(now+3,ms+10));
  assert(g.onSntp(T+100*86400,ms+20,false)==now+3&&g.undone==2);       // e para o futuro
  ms+=900000;now+=900;                                                 // clima de novo: a hora real segue confirmada
  assert(g.onWeather(now-600,now,ms)==now&&g.ok(now,ms));
  TimeGuard h;assert(h.onSntp(T,1000,false)==T);                       // ligou com hora falsa; o clima traz a real
  assert(h.onWeather(T+86400*3-300,T+20,21000)==T+20&&!h.ok(T+20,21000));
  assert(h.onSntp(T+86400*3,60000,false)==T+86400*3&&!h.ok(T+86400*3,60000));
  assert(h.onWeather(T+86400*3-300,T+86400*3+30,90000)==T+86400*3+30&&h.ok(T+86400*3+30,90000));
  TimeGuard k;assert(k.onSntp(T,1000,true)==T&&!k.ok(T,1000));         // sem clima: duas sincronizações
  assert(k.onSntp(T+60,61000,true)==T+60&&k.ok(T+60,61000));
  assert(k.onSntp(T-86400,121000,true)==T+120&&k.undone==1);           // salto: desfeito
  assert(k.onSntp(T-86400+60,181000,true)==T-86400+60);                // repetido 1 min depois: confirmado (sem clima, é o limite)
  TimeGuard w;w.confirm(T,4000000000u);assert(w.ok(T+400000,(uint32_t)(4000000000ull+400000000ull)));  // millis() voltando a zero
 }
 {AuthThrottle a;assert(!a.blocked(7,1000));a.fail(7,1000);assert(a.blocked(7,1500)&&!a.blocked(8,1500)&&!a.blocked(7,2001));
  for(uint32_t ip=10;ip<20;ip++)a.fail(ip,3000+ip);assert(a.blocked(19,3100)&&!a.blocked(7,3100));}
 {RateLimit r;int n=0;for(int i=0;i<50;i++)n+=r.take(5000+i,4);assert(n==4);assert(r.take(6001,4));}
 AuthThrottle throttle;uint32_t wait=0;
 assert(!throttle.blocked(1,100,&wait)&&wait==0);
 assert(throttle.fail(1,100)==1000&&throttle.blocked(1,100,&wait)&&wait==1000);
 assert(throttle.blocked(1,1099,&wait)&&wait==1&&!throttle.blocked(1,1100,&wait));
 assert(throttle.fail(1,1100)==2000);throttle.success(1);assert(!throttle.blocked(1,1100,&wait));
 for(int i=0;i<8;i++){wait=throttle.fail(2,2000+i);}assert(wait==32000);
 assert(throttle.blocked(2,3000,&wait));
 AuthThrottle wrap;assert(wrap.fail(3,0xfffffff0UL)==1000);assert(wrap.blocked(3,20,&wait));
 puts("security: actual upload handlers deny unauthenticated writes; bounds, aborts and URL injection checks passed");
 puts("security: hora protegida contra NTP falso, bloqueio de senha por IP e limite de SSDP ok");
}
`);
