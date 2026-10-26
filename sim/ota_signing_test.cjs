const fs=require('fs'),path=require('path');
const src=fs.readFileSync(path.join(__dirname,'../smalltv-pomodoro/main.cpp'),'utf8').replace(/\r\n/g,'\n');
const a=src.indexOf('void otaConsume('),b=src.indexOf('\n}',a)+2;
if(a<0||b<2)throw Error('otaConsume not found');
const fn=src.slice(a,b);
const out=process.argv[2];if(!out)throw Error('output path required');
fs.writeFileSync(out,`#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cassert>
#include <string>
#include <vector>
#define F(x) x
using String=std::string;
struct { uint32_t getFreeSketchSpace(){return 1048576;} } ESP;
struct OtaCheck { uint32_t got,size,want,crc;uint8_t field[8];String err; bool gz=false; } ota;
${fn}
static void reset(){ota={0,0,0,0xffffffff,{0},String()};}
int main(int argc,char**argv){
 assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);
 std::vector<uint8_t> bytes(n+260,0xab);assert(fread(bytes.data(),1,n,f)==n);fclose(f);
 for(size_t chunk: {1,7,255,1024,2048,8192}) {
  reset();for(size_t i=0;i<bytes.size();i+=chunk){size_t k=bytes.size()-i<chunk?bytes.size()-i:chunk;otaConsume(bytes.data()+i,k,true);}
  assert(ota.err.empty());assert(ota.got==n);assert(ota.got==ota.size);assert(ota.crc==ota.want);
 }
 reset();otaConsume(bytes.data(),n,false);assert(ota.err.empty()&&ota.got==n&&ota.crc==ota.want);
 reset();otaConsume(bytes.data(),n+1,false);assert(!ota.err.empty()); // recovery rejects suffix on raw image
 reset();otaConsume(bytes.data(),n-1,true);assert(ota.got!=ota.size); // truncation
 bytes[4200]^=1;reset();otaConsume(bytes.data(),n,true);assert(ota.crc!=ota.want);
 bytes[0]=0;reset();otaConsume(bytes.data(),n,true);assert(!ota.err.empty());
 puts("OK: actual otaConsume, split headers, signed trailer, recovery, truncated/corrupt/wrong-chip image");
}
`);

