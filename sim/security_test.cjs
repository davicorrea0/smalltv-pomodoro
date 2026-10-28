// Compile the actual upload handlers with an in-memory filesystem/server double.
const fs=require('fs'),path=require('path');
const source=fs.readFileSync(path.join(__dirname,'../smalltv-pomodoro/main.cpp'),'utf8');
function fn(name){const a=source.indexOf(`void ${name}() {`),b=source.indexOf('\n}',a);if(a<0||b<0)throw Error(name);return source.slice(a,b+2);}
for(const name of ['handleUpload','handlePackUpload','handlePanelUpload'])
 if(!fn(name).includes('uploadAuthed()'))throw Error(`${name} bypasses upload throttle`);
{ // /api/crash (Limpar da última queda) é ação: POST com guard(), e a gravação só acontece em reinício anormal
 const i=source.indexOf('server.on("/api/crash", HTTP_POST'),blk=source.slice(i,source.indexOf('});',i));
 if(i<0||!blk.includes('if (!guard()) return;'))throw Error('/api/crash must be a guarded POST');
 const cl=fn('crashLoad');
 if(!cl.includes('REASON_EXCEPTION_RST')||!cl.includes('return;')||cl.indexOf('return;')>cl.indexOf('EEPROM.put'))throw Error('crashLoad must only write after an abnormal reset');
}
const recovery=fn('startRecovery');
if(!recovery.includes('WiFi.mode(WIFI_AP);')||recovery.includes('WiFi.begin()'))throw Error('recovery must be AP-only');
// eboot uses the staged file's last 4 bytes as the image size: trailing data (the 1.20.0 signature) erased the bootloader
const gzv=source.slice(source.indexOf('bool otaVerifyGz() {'),source.indexOf('\n}',source.indexOf('bool otaVerifyGz() {')));
if(!gzv.includes('cmd.args[2] - 4')||!gzv.includes('!= ota.got'))throw Error('otaVerifyGz must check the eboot size trailer');
const update=fn('setupUpdate');
if(!update.includes('!recoveryMode && !authed()'))throw Error('update page bypasses throttle');
// Signed OTA (1.20.1): the signature travels apart from the file and is checked before Update.end(true)
// schedules the swap, on both the .bin and the .bin.gz paths. Never the core's appended-signature scheme.
if(/installSignature|UpdaterHashClass/.test(source))throw Error('do not use the core appended signature (it bricked 1.20.0)');
const end=update.slice(update.indexOf('UPLOAD_FILE_END'),update.indexOf('UPLOAD_FILE_ABORTED'));
const ends=[...end.matchAll(/Update\.end\(true\)/g)].map(m=>m.index);
if(ends.length!==2)throw Error('expected two Update.end(true) in the upload end');
const gzEnd=end.slice(end.indexOf('if (ota.gz) {'),ends[0]);
if(!gzEnd.includes('if (!otaSigOk()) Update.end();'))throw Error('.bin.gz swap scheduled without the signature check');
const binEnd=end.slice(end.indexOf('arquivo corrompido (CRC)'),ends[1]);
if(!binEnd.includes('else otaSigOk();')||!binEnd.includes('if (ota.err.length()) Update.end();'))throw Error('.bin swap scheduled without the signature check');
const sigOk=source.slice(source.indexOf('static bool otaSigOk() {'),source.indexOf('\n}',source.indexOf('static bool otaSigOk() {')));
if(!sigOk.includes('if (recoveryMode) return true;')||!sigOk.includes('stack_thunk_add_ref();')||!sigOk.includes('getMaxFreeBlockSize()'))
 throw Error('otaSigOk: recovery escape, thunk stack and heap check are required');
if(!update.includes('br_sha256_update(&otaHash, up.buf, up.currentSize);')||!update.includes('otaSigBegin();'))throw Error('upload must hash every byte');
// collectHeaders replaces the list: the normal server (setupWeb) and recovery (startRecovery) each set theirs once
for(const [name,hs] of [['setupWeb',['Origin','X-Firmware-Sig','Authorization','X-SmallTV']],['startRecovery',['Authorization','X-SmallTV']]]){
 const ch=[...fn(name).matchAll(/server\.collectHeaders\(([^)]*)\)/g)];
 if(ch.length!==1||!hs.every(h=>ch[0][1].includes('"'+h+'"')))throw Error(name+': collectHeaders must be called once with '+hs);
}
{ // signer and verifier agree on the published key; a changed byte or another key fails
 const crypto=require('crypto'),{sign,verify,rawKey}=require('../scripts/assinar.cjs');
 const kh=fs.readFileSync(path.join(__dirname,'../smalltv-pomodoro/ota_key.h'),'utf8');
 const bytes=name=>Buffer.from(kh.match(new RegExp(name+'\\[\\d+\\] PROGMEM = \\{([^}]*)\\}'))[1].match(/0x[0-9a-f]{2}/g).map(Number));
 const raw=rawKey();
 if(!bytes('OTA_KEY_N').equals(raw.n)||!bytes('OTA_KEY_E').equals(raw.e)||raw.n.length!==256)
  throw Error('ota_key.h differs from ota-public.pem (node scripts/assinar.cjs header)');
 const k=crypto.generateKeyPairSync('rsa',{modulusLength:2048}),data=crypto.randomBytes(5000),sig=sign(data,k.privateKey);
 if(!verify(data,sig,k.publicKey)||sig.length!==256||sig.toString('base64').length!==344)throw Error('sign/verify');
 const bad=Buffer.from(data);bad[100]^=1;
 if(verify(bad,sig,k.publicKey)||verify(data,sig))throw Error('tampered file or foreign key accepted');
}
{ // scripts/conferir-gz.cjs: a .bin.gz is checked the way the eboot consumes it
 const crypto=require('crypto'),zlib=require('zlib'),{conferir}=require('../scripts/conferir-gz.cjs');
 const rnd=crypto.randomBytes(20000),bin=Buffer.concat([Buffer.from([0xe9,3,2,0]),rnd,Buffer.alloc(100),rnd]); // repeats 20 KB apart: needs a window > 8 KB
 const gz=zlib.gzipSync(bin,{level:9,windowBits:13,memLevel:9}),bad=(b,g)=>conferir(b,g).length>0;
 if(bad(bin,gz))throw Error('conferir-gz rejects a good .bin.gz: '+conferir(bin,gz));
 if(!bad(bin,Buffer.concat([gz,crypto.randomBytes(260)])))throw Error('conferir-gz accepts the 1.20.0 signed .gz (260 bytes after the gzip)');
 if(!bad(bin,Buffer.concat([gz,Buffer.alloc(4)])))throw Error('conferir-gz accepts trailing bytes');
 if(!bad(bin,zlib.gzipSync(bin,{level:9,windowBits:15})))throw Error('conferir-gz accepts a 32 KB window');
 const other=Buffer.from(bin);other[500]^=1;
 if(!bad(other,gz))throw Error('conferir-gz accepts a .gz that differs from the .bin');
 const nomagic=Buffer.from(bin);nomagic[0]=0;
 if(!bad(nomagic,zlib.gzipSync(nomagic,{level:9,windowBits:13})))throw Error('conferir-gz accepts a .bin without magic 0xE9');
}
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
bool panelOk(){return allowed;}bool passOk(){return password;}bool uploadAuthed(){return passOk();}
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
 const char*own[]={"smalltv-1a2b.local","SmallTV-1A2B.LOCAL","smalltv-1a2b.local:80","smalltv-1a2b.local:65535"};
 for(auto h:own)assert(safeOwnHost(h,strlen(h),"smalltv-1a2b"));
 const char*notOwn[]={"smalltv-1a2c.local","smalltv-1a2b.local.evil.com","evil.smalltv-1a2b.local","smalltv-1a2b.loca","smalltv-1a2b.local:","smalltv-1a2b.local:0","smalltv-1a2b.local:655350","smalltv-1a2b.local:8a","smalltv-1a2b","smalltv-1a2b.localx",""};
 for(auto h:notOwn)assert(!safeOwnHost(h,strlen(h),"smalltv-1a2b"));
 assert(!safeOwnHost("x.local",7,""));
 {// mDNS mínimo: pergunta A/ANY pelo nosso nome; resposta, compressão maliciosa e rótulo com ponto
  auto q=[](std::initializer_list<uint8_t> b){return std::string(b.begin(),b.end());};
  const std::string hdr=q({0x12,0x34,0,0,0,1,0,0,0,0,0,0});
  const std::string nm=q({12})+"SmallTV-1a2b"+q({5})+"local"+q({0});
  auto ask=[&](const std::string&s,bool*qu=nullptr){return mdnsAsksFor((const uint8_t*)s.data(),s.size(),"smalltv-1a2b",qu);};
  bool qu=false;
  assert(ask(hdr+nm+q({0,1,0,1}),&qu)&&!qu);
  assert(ask(hdr+nm+q({0,255,0x80,1}),&qu)&&qu);
  assert(!ask(hdr+nm+q({0,28,0,1})));                       // AAAA: não respondemos
  assert(!ask(hdr+nm+q({0,1,0,3})));                        // classe diferente de IN
  assert(!ask(hdr+nm+q({0,1})));                            // pergunta cortada
  std::string resp=hdr+nm+q({0,1,0,1});resp[2]=(char)0x84;assert(!ask(resp));  // é resposta, não pergunta
  assert(!ask(hdr+q({18})+"smalltv-1a2b.local"+q({0,0,1,0,1})));  // um rótulo só, com pontos
  assert(!ask(hdr+q({12})+"smalltv-1a2c"+q({5})+"local"+q({0,0,1,0,1})));
  std::string two=q({0,0,0,0,0,2,0,0,0,0,0,0})+q({5})+"other"+q({5})+"local"+q({0,0,1,0,1})+q({12})+"smalltv-1a2b"+q({0xC0,18,0,1,0,1});
  assert(ask(two));                                         // 2ª pergunta com ponteiro para ".local"
  assert(!ask(hdr+q({0xC0,12,0,1,0,1})));                   // ponteiro para si mesmo: laço limitado
  assert(!ask(hdr+q({0xC0,200,0,1,0,1})));                  // ponteiro para fora do pacote
  assert(!ask(hdr.substr(0,11)));
  uint8_t o[80],ip[4]={192,168,1,9};
  size_t m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0x1234,false);
  assert(m==12+20+10+4&&o[0]==0&&o[1]==0&&o[2]==0x84&&o[7]==1&&o[5]==0&&!memcmp(o+m-4,ip,4)&&o[12+20+2]==0x80);
  m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0x1234,true);
  assert(m==12+20+4+2+10+4&&o[0]==0x12&&o[1]==0x34&&o[5]==1&&o[36]==0xC0&&o[37]==12&&!memcmp(o+m-4,ip,4));
  {size_t j=12;char nb[64];assert(dnsReadName(o,m,j,nb,sizeof nb)&&!strcmp(nb,"smalltv-1a2b.local")&&j==32);}
  assert(!mdnsAnswer(o,40,"smalltv-1a2b",ip,0,false));
  // sondagem: a pergunta que o relógio manda e a resposta de outro relógio que já usa o nome
  m=mdnsProbeQuery(o,sizeof o,"smalltv");
  assert(m==12+15+4&&o[5]==1&&o[2]==0&&o[m-4]==0&&o[m-3]==255&&o[m-2]==0x80);
  assert(mdnsAsksFor(o,m,"smalltv",&qu)&&qu);                              // outro relógio responderia
  m=mdnsAnswer(o,sizeof o,"SmallTV",ip,0,false);
  assert(mdnsHasAnswer(o,m,"smalltv")&&!mdnsHasAnswer(o,m,"smalltv-2"));
  m=mdnsAnswer(o,sizeof o,"smalltv",ip,7,true);assert(mdnsHasAnswer(o,m,"smalltv"));  // com pergunta + ponteiro
  assert(!mdnsHasAnswer(o,20,"smalltv"));                                   // pacote cortado no meio do nome
  {uint8_t pq[64];size_t pm=mdnsProbeQuery(pq,sizeof pq,"smalltv");assert(!mdnsHasAnswer(pq,pm,"smalltv"));}  // pergunta não é resposta
  {std::string big=q({0,0,0x84,0,0,0,0,1,0,0,0,0})+q({5})+"other"+q({5})+"local"+q({0,0,1,0,1,0,0,0,120,0xFF,0xFF});
   assert(!mdnsHasAnswer((const uint8_t*)big.data(),big.size(),"smalltv"));}  // tamanho de dados maior que o pacote

 }
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
 AuthThrottle holes;holes.fail(10,100);holes.fail(20,100);holes.success(10);
 assert(holes.blocked(20,100,&wait)&&wait==1000);assert(!holes.blocked(30,100,&wait)&&wait==0);
 for(int i=0;i<8;i++){wait=throttle.fail(2,2000+i);}assert(wait==32000);
 assert(throttle.blocked(2,3000,&wait));
 AuthThrottle wrap;assert(wrap.fail(3,0xfffffff0UL)==1000);assert(wrap.blocked(3,20,&wait));
 puts("security: actual upload handlers deny unauthenticated writes; bounds, aborts and URL injection checks passed");
 puts("security: hora protegida contra NTP falso, bloqueio de senha por IP e limite de SSDP ok");
}
`);
