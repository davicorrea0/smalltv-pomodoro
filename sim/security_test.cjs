// Compile the actual upload handlers with an in-memory filesystem/server double.
const fs=require('fs'),path=require('path');
const source=require('./fonte.cjs');
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
{ // portal de Wi-Fi (1.24.13): a estação para sem apagar a rede salva, a varredura não bloqueia e a rede nova volta com o padrão
 const portal=fn('startPortal'),save=portal.slice(portal.indexOf('"/save"'),portal.indexOf('onNotFound'));
 const stop=portal.indexOf('WiFi.disconnect(false, false);');
 if(stop<0||stop>portal.indexOf('WiFi.softAP('))throw Error('startPortal must stop the station (disconnect(false, false)) before softAP');
 if(/WiFi[.]disconnect[(](true|false)[)]/.test(portal)||portal.includes('eraseConfig'))throw Error('disconnect(wifioff) erases the saved network in core 3.1.2; use disconnect(false, false)');
 if(!portal.includes('WiFi.setAutoReconnect(false);'))throw Error('startPortal must stop the station reconnect');
 if(/scanNetworks[(][)]/.test(portal)||!portal.includes('scanNetworks(true)'))throw Error('portal scan must be async (a sync scan blocks the server ~3 s)');
 const ar=save.indexOf('WiFi.setAutoReconnect(true);'),bg=save.indexOf('WiFi.begin(');
 if(ar<0||bg<0||ar>bg||save.indexOf('WiFi.persistent(true);')>bg)throw Error('/save must restore auto-reconnect and persistence before WiFi.begin');
 if(!save.includes('if (!formOk()) { deny(); return; }')||!save.includes('safeRestart();'))throw Error('/save must check the form token and restart');
 if(!portal.includes('server.collectHeaders("Accept-Language")')||portal.includes('<datalist'))throw Error('portal: Accept-Language header and visible list required (no datalist)');
}
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
for(const [name,hs] of [['setupWeb',['Origin','X-Firmware-Sig','Authorization','X-SmallTV']],['startRecovery',['Authorization','X-SmallTV','Accept-Language']]]){
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
{ // chamadas HTTP de saída: nome por DNS assíncrono, prazos curtos e espera crescente; o laço nunca espera um nome
 const hs=source.indexOf('int httpGet(WiFiClient'),hg=source.slice(hs,source.indexOf('\n}',hs));
 if(!hg.includes('dnsResolve(host.c_str(), ip)')||!hg.includes('c.connect(IPAddress(ip), port)')||/c[.]connect[(](host|url)/.test(hg))throw Error('httpGet must connect by cached IP, never by name (blocking DNS)');
 if(!hg.includes('c.setTimeout(HTTP_CONNECT_MS)')||!/HTTP_CONNECT_MS = 1[0-9]{3};/.test(source))throw Error('connect timeout must be short (<= 1.9 s)');
 if(!source.includes('dns_gethostbyname(host, &a, dnsFound, nullptr)'))throw Error('lwIP async dns_gethostbyname with callback expected');
 const wl=fn('weatherLoop'),hl=fn('hookLoop');
 if(!wl.includes('wxBack.fail(millis())')||!wl.includes('wxBack.ok(millis(), 15UL * 60000UL)')||!wl.includes('if (d == 0) return;'))throw Error('weatherLoop: backoff and non-blocking dns wait required');
 if(!hl.includes('hookBack.fail(millis())')||!hl.includes('hookBack.ok(millis(), 0)')||!hl.includes('code == HTTP_WAIT'))throw Error('hookLoop: backoff and dns wait required');
 const tp=fn('tickPomodoro');if(tp.includes('hookFire')||tp.includes('httpGet')||tp.includes('hookLoop'))throw Error('tickPomodoro must not call HTTP');
 if(!source.includes('stallAt\\"')||!source.includes('diagStallAt = diagWhere'))throw Error('diag must report the longest loop stall and where');
}
const out=process.argv[2];if(!out)throw Error('output.cpp required');
const header=path.relative(path.dirname(path.resolve(out)),path.resolve(__dirname,'../smalltv-pomodoro/security.h')).replaceAll('\\','/');
fs.writeFileSync(out,`#include "${header}"
#include <string>
#include <cassert>
#include <cstdio>
#include <cctype>
#include <limits>
#include <vector>
#include <map>
#include <algorithm>
#define F(s) (s)
#define PSTR(s) (s)
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
bool validGif(const String&){return true;}enum JpgResult:uint8_t{JPG_OK,JPG_BAD,JPG_NOMEM};static JpgResult jpgRes=JPG_OK;JpgResult validJpg(const String&){return jpgRes;}void rebuildAlbum(){rebuilds++;}
const char*tr(const char*a,const char*,const char*){return a;}
File uploadFile,packUpload;String uploadPath,uploadError,packUploadError;
UploadGate photoGate,packGate;bool packFaceReady=false;
${fn('handleUpload')}
${fn('handlePackUpload')}
void reset(){jpgRes=JPG_OK;allowed=password=true;opens=writes=deletes=rebuilds=0;written=0;uploadFile={};packUpload={};uploadPath="";uploadError="";packUploadError="";packFaceReady=false;photoGate={};packGate={};event={};}
void send(void(*handler)(),int status){event.status=status;handler();}
int main(){
 for(bool panel:{false,true}){reset();allowed=panel;password=false;
  send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);
  assert(!opens&&!writes&&!deletes);reset();allowed=panel;password=false;
  send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);
  assert(!opens&&!writes&&!deletes&&!packFaceReady);
 }
 reset();send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);assert(opens==1&&writes==1&&written==1);
 // foto sem memória para conferir: fica salva e o envio avisa; arquivo recusado pelo decodificador: apagado
 reset();jpgRes=JPG_NOMEM;send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);assert(deletes==0&&uploadError.length()&&uploadError!="JPG fora do padrao"&&rebuilds==1);
 reset();jpgRes=JPG_BAD;send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);assert(deletes==1&&uploadError=="JPG fora do padrao");
 reset();jpgRes=JPG_OK;send(handleUpload,UPLOAD_FILE_START);send(handleUpload,UPLOAD_FILE_WRITE);send(handleUpload,UPLOAD_FILE_END);assert(deletes==0&&!uploadError.length());
 reset();event.filename="a..b.jpg";send(handleUpload,UPLOAD_FILE_START);assert(uploadPath=="/image/a.b.jpg");
 reset();send(handleUpload,UPLOAD_FILE_START);event.currentSize=3*1024*1024+1;send(handleUpload,UPLOAD_FILE_WRITE);assert(!writes&&deletes==1&&!photoGate.active);
 reset();send(handlePackUpload,UPLOAD_FILE_START);event.currentSize=24577;send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);assert(!writes&&!packFaceReady&&!packGate.active);
 reset();send(handlePackUpload,UPLOAD_FILE_START);event.currentSize=24576;send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);assert(writes==1&&packFaceReady);  // pacote de até 24 KB (formato 2, 1.26.0)
 reset();send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_WRITE);send(handlePackUpload,UPLOAD_FILE_END);assert(packFaceReady&&written==1);
 reset();send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_ABORTED);assert(!packFaceReady);
 reset();event.name="unknown";send(handlePackUpload,UPLOAD_FILE_START);send(handlePackUpload,UPLOAD_FILE_WRITE);assert(!opens&&!writes);
 { // Accept-Language: idioma das páginas do Wi-Fi e da recuperação
  auto L=[](const char*h){return (int)langFromAccept(h);};
  assert(L("pt-BR,pt;q=0.9,en-US;q=0.8,en;q=0.7")==0);assert(L("en-US,en;q=0.9")==1);assert(L("es-419,es;q=0.9,en;q=0.5")==2);
  assert(L("fr-FR,fr;q=0.9")==1);assert(L("de;q=0.9, es;q=0.8, en;q=0.1")==2);assert(L("en;q=0.5, pt;q=0.9")==0);
  assert(L("pt;q=0, en")==1);assert(L("pt;q=0")==1);assert(L("PT-br")==0);assert(L("ptx")==1);assert(L("p")==1);assert(L("*")==1);
  assert(L("en , pt")==1);assert(L("pt , en")==0);assert(L("pt;q=abc, es;q=0.2")==2);assert(L("es;q=0.123,pt;q=0.122")==2);
  assert(L("")==255&&L(nullptr)==255);assert(L(";;;")==1);assert(L(",,,")==1);assert(L("pt;q=1.0000000")==0);assert(L("pt;")==0);assert(L("pt;q")==0);
  std::string big(5000,',');big+="es";assert(L(big.c_str())==2);
 }
 { // lista de redes do portal: sem nomes vazios nem repetidos, o sinal mais forte primeiro
  struct N{const char*s;int r;};
  auto pick=[](const std::vector<N>&v,int cap,std::vector<int>&idx){uint8_t o[32];
   int k=scanPick((int)v.size(),[&](int i,char*d){strcpy(d,v[i].s);},[&](int i){return v[i].r;},o,cap);idx.assign(o,o+k);return k;};
  std::vector<int> o;
  assert(pick({},12,o)==0);
  assert(pick({{"",-40},{"",-50}},12,o)==0);
  assert(pick({{"casa",-70},{"vizinho",-50},{"",-30},{"casa",-60},{"bar",-80}},12,o)==3&&o==std::vector<int>({1,3,4}));
  assert(pick({{"a",-50},{"a",-70}},12,o)==1&&o[0]==0);
  assert(pick({{"a",-70},{"a",-50}},12,o)==1&&o[0]==1);
  assert(pick({{"a",-50},{"a",-50}},12,o)==1&&o[0]==0);
  assert(pick({{"a",-60},{"b",-50},{"c",-70}},2,o)==2&&o==std::vector<int>({1,0}));
  assert(pick({{"a",-60},{"b",-50},{"c",-40}},2,o)==2&&o==std::vector<int>({2,1}));
  assert(pick({{"a",-60},{"b",-70},{"c",-80}},2,o)==2&&o==std::vector<int>({0,1}));
  srand(7);  // contra a conta por força bruta: sinais distintos (sem empate), nomes repetidos e vazios, qualquer limite
  const char*pool[]={"","casa","casa-5G","bar","loja","x","Y","wifi"};
  for(int t=0;t<20000;t++){
   int n=rand()%30,cap=1+rand()%14;std::vector<N> v;int perm[64];for(int i=0;i<64;i++)perm[i]=i;
   for(int i=63;i>0;i--)std::swap(perm[i],perm[rand()%(i+1)]);
   for(int i=0;i<n;i++)v.push_back({pool[rand()%8],-20-perm[i]});
   std::map<std::string,int> best;for(auto&x:v)if(x.s[0]&&(!best.count(x.s)||best[x.s]<x.r))best[x.s]=x.r;
   std::vector<std::pair<int,std::string>> want;for(auto&b:best)want.push_back({b.second,b.first});
   std::sort(want.rbegin(),want.rend());if((int)want.size()>cap)want.resize(cap);
   int k=pick(v,cap,o);assert(k==(int)want.size());
   for(int i=0;i<k;i++)assert(v[o[i]].r==want[i].first&&want[i].second==v[o[i]].s);
  }
 }
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
  assert(ask(hdr+nm+q({0,1,0,1}),&qu)==MDNS_A&&!qu);
  assert(ask(hdr+nm+q({0,255,0x80,1}),&qu)==(MDNS_A|MDNS_AAAA)&&qu);
  assert(ask(hdr+nm+q({0,28,0x80,1}),&qu)==MDNS_AAAA&&qu);  // AAAA (o Windows pergunta junto com o A)
  assert(!ask(hdr+nm+q({0,16,0,1})));                       // TXT: não respondemos
  {std::string two=q({0,0,0,0,0,2,0,0,0,0,0,0})+nm+q({0,1,0,1})+q({0xC0,12,0,28,0x80,1});  // A e AAAA no mesmo pacote
   assert(ask(two,&qu)==(MDNS_A|MDNS_AAAA)&&qu);
   assert(ask(two.substr(0,two.size()-2),&qu)==MDNS_A&&!qu);}  // a 2ª pergunta cortada: vale a 1ª
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
  uint8_t o[96],ip[4]={192,168,1,9};
  size_t m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0x1234,false);
  assert(m==12+20+10+4&&o[0]==0&&o[1]==0&&o[2]==0x84&&o[7]==1&&o[5]==0&&!memcmp(o+m-4,ip,4)&&o[12+20+2]==0x80);
  m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0x1234,true);
  assert(m==12+20+4+2+10+4&&o[0]==0x12&&o[1]==0x34&&o[5]==1&&o[36]==0xC0&&o[37]==12&&!memcmp(o+m-4,ip,4));
  {size_t j=12;char nb[64];assert(dnsReadName(o,m,j,nb,sizeof nb)&&!strcmp(nb,"smalltv-1a2b.local")&&j==32);}
  assert(!mdnsAnswer(o,40,"smalltv-1a2b",ip,0,false));
  assert(!mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0,false,0));
  // percorre os registros de uma resposta: tipo, tamanho e dados de cada um, terminando no fim exato do pacote
  auto rrs=[&](const uint8_t*p,size_t n){std::vector<std::pair<int,std::string>> v;size_t j=12;char nb[64];
   unsigned qd=p[4]<<8|p[5],rr=(p[6]<<8|p[7])+(p[10]<<8|p[11]);
   for(unsigned k=0;k<qd;k++){assert(dnsReadName(p,n,j,nb,sizeof nb)&&j+4<=n);j+=4;}
   for(unsigned k=0;k<rr;k++){assert(dnsReadName(p,n,j,nb,sizeof nb)&&!strcmp(nb,"smalltv-1a2b.local")&&j+10<=n);
    int t=p[j]<<8|p[j+1];size_t l=p[j+8]<<8|p[j+9];assert(j+10+l<=n);v.push_back({t,std::string((const char*)p+j+10,l)});j+=10+l;}
   assert(j==n);return v;};
  const std::string v4((const char*)ip,4),mapped=std::string(10,0)+std::string(2,(char)0xFF)+v4;
  m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0,false,MDNS_AAAA);   // só o AAAA: ele na resposta e o A como adicional
  assert(m==12+20+10+16+2+10+4&&o[7]==1&&o[11]==1);
  {auto v=rrs(o,m);assert(v.size()==2&&v[0].first==28&&v[0].second==mapped&&v[1].first==1&&v[1].second==v4);}
  m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0,false,MDNS_A|MDNS_AAAA);  // os dois na resposta
  assert(o[7]==2&&o[11]==0);{auto v=rrs(o,m);assert(v.size()==2&&v[0].first==1&&v[1].first==28&&v[1].second==mapped);}
  m=mdnsAnswer(o,sizeof o,"smalltv-1a2b",ip,0x4321,true,MDNS_AAAA);  // resolvedor comum: repete a pergunta do AAAA
  assert(m==12+20+4+2+10+16+2+10+4&&o[0]==0x43&&o[5]==1&&o[32]==0&&o[33]==28);
  {auto v=rrs(o,m);assert(v.size()==2&&v[0].first==28&&v[1].first==1);}
  assert(!mdnsAnswer(o,m-1,"smalltv-1a2b",ip,0x4321,true,MDNS_AAAA));  // não cabe: nada
  assert(mdnsHasAnswer(o,m,"smalltv-1a2b"));
  {uint32_t r=12345;for(int t=0;t<20000;t++){  // pacotes aleatórios (com nomes de verdade às vezes): sem ler fora
    uint8_t b[256];size_t n=12+(r=r*1103515245+12345)%240;for(size_t k=0;k<n;k++)b[k]=(uint8_t)((r=r*1103515245+12345)>>16);
    b[2]&=t&1?0xFF:0x07;b[4]=0;b[5]=b[5]%4;if(t%3==0&&n>40)memcpy(b+12,nm.data(),nm.size());
    bool x;mdnsAsksFor(b,n,"smalltv-1a2b",&x);mdnsHasAnswer(b,n,"smalltv-1a2b");}}
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
 // cabeçalho Date do HTTP: só para mostrar a hora
 assert(httpDateToEpoch(" Wed, 28 Oct 2026 12:00:00 GMT\\r")==1793188800LL);assert(httpDateToEpoch("Thu, 01 Jan 1970 00:00:00 GMT")==-1);
 assert(httpDateToEpoch("Sat, 01 Jan 2000 00:00:00 GMT")==946684800LL&&httpDateToEpoch("Sun, 06 Nov 1994 08:49:37 GMT")==-1);assert(httpDateToEpoch("28 Oct 2026 12:00:00 GMT")==1793188800LL);
 assert(httpDateToEpoch("Wed, 28 Foo 2026 12:00:00 GMT")==-1&&httpDateToEpoch("Wed, 28 Oct 2026 25:00:00 GMT")==-1&&httpDateToEpoch("")==-1&&httpDateToEpoch(nullptr)==-1);
 assert(httpDateToEpoch("Wed, 28 Oct 2026 12:00 GMT")==-1&&httpDateToEpoch("Wed, 32 Oct 2026 12:00:00 GMT")==-1);
 {const int64_t T=1792715400LL;  // hora na memória RTC entre reinícios: soma o tempo que passou; lixo ou velho demais, não
  const uint32_t q12=(uint32_t)(5.75*4096);                              // ~5,75 µs por tick
  uint32_t tk=1000000;RtcClock c=rtcClockMake((uint32_t)T,tk);
  assert(rtcClockRestore(c,tk,q12)==T);
  assert(rtcClockRestore(c,tk+(uint32_t)(3100000.0/5.75),q12)==T+3);       // ~3,1 s depois
  assert(rtcClockRestore(c,tk-5,q12)==-1);                                 // contador voltou (ligou de novo): inválido
  assert(rtcClockRestore(c,tk+(uint32_t)(601e6/5.75),q12)==-1);            // parado há mais de 10 min
  RtcClock bad=c;bad.sec++;assert(rtcClockRestore(bad,tk,q12)==-1);       // corrompido
  RtcClock zero={0,0,0,0};assert(rtcClockRestore(zero,0,q12)==-1&&rtcClockRestore(c,tk,0)==-1);
  RtcClock old=rtcClockMake(1000,tk);assert(rtcClockRestore(old,tk,q12)==-1);
 }
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
 { // espera crescente das chamadas HTTP: 2, 4, 8 e 15 min; o sucesso zera
  HttpBackoff b;assert(b.due(0));
  uint32_t t=1000,want[]={2,4,8,15,15,15};
  for(int i=0;i<6;i++){b.fail(t);assert(b.next==t+want[i]*60000UL&&b.fails==i+1);assert(!b.due(t)&&!b.due(b.next-1)&&b.due(b.next));t=b.next;}
  b.ok(t,900000);assert(b.fails==0&&!b.due(t+899999)&&b.due(t+900000));
  b.fail(t);assert(b.next==t+120000UL);                                // depois do sucesso recomeça em 2 min
  HttpBackoff w;w.fails=255;w.fail(5);assert(w.fails==255&&w.next==5+900000UL);
  HttpBackoff z;z.fail(4294900000u);assert(!z.due(4294900000u+119999)&&z.due(4294900000u+120000));  // millis() dando a volta
 }
 { // cache de nomes: não bloqueia, vale 1 h, falha vira espera
  DnsCache c;uint32_t ip=0;int calls=0,mode=1;uint32_t got=0;
  auto start=[&](const char*h,uint32_t&out){calls++;if(mode==0){out=0x0100007f;return 0;}return mode;};
  assert(c.get("api.open-meteo.com",1000,ip,start)==0&&calls==1);       // em andamento: o laço segue
  assert(c.get("api.open-meteo.com",1100,ip,start)==0&&calls==1);       // sem disparar outra busca
  c.done("api.open-meteo.com",0x01020304,1500);
  assert(c.get("api.open-meteo.com",1600,ip,start)==1&&ip==0x01020304&&calls==1);
  assert(c.get("api.open-meteo.com",1500+DnsCache::TTL-1,ip,start)==1&&calls==1);   // vale 1 h
  assert(c.get("api.open-meteo.com",1500+DnsCache::TTL,ip,start)==0&&calls==2);     // venceu: busca de novo
  c.done("api.open-meteo.com",0,1500+DnsCache::TTL+50);                              // não achou
  assert(c.get("api.open-meteo.com",1500+DnsCache::TTL+60,ip,start)==-1&&calls==2);  // falha uma vez...
  assert(c.get("api.open-meteo.com",1500+DnsCache::TTL+70,ip,start)==0&&calls==3);   // ...e a seguinte recomeça
  assert(c.get("api.open-meteo.com",1500+DnsCache::TTL+70+DnsCache::WAIT,ip,start)==-1);  // sem resposta do lwIP: desiste
  c.done("api.open-meteo.com",0x05050505,0);                                          // aviso tardio é ignorado
  assert(c.find("api.open-meteo.com")==nullptr);
  mode=0;calls=0;assert(c.get("ex.com",10,ip,start)==1&&ip==0x0100007f&&calls==1);    // resolvido na hora (IP literal ou cache do lwIP)
  mode=-1;assert(c.get("ruim.com",10,ip,start)==-1&&c.find("ruim.com")==nullptr);      // erro ao iniciar
  mode=1;c.get("a.com",20,ip,start);c.get("b.com",30,ip,start);c.get("c.com",40,ip,start);
  c.done("a.com",1,40);c.done("b.com",2,41);c.done("c.com",3,42);
  assert(c.get("d.com",50,ip,start)==0&&c.find("a.com")==nullptr&&c.find("b.com")&&c.find("c.com"));  // troca o mais antigo
  std::string big(60,'x');assert(c.get(big.c_str(),60,ip,start)==-1);                  // nome grande demais
 }
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
