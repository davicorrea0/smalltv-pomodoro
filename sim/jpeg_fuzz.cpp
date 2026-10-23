#ifndef JPEG_SOURCE
#define JPEG_SOURCE "../smalltv-pomodoro/src/JPEGDEC/JPEGDEC.cpp"
#endif
#include JPEG_SOURCE
#include <vector>
#include <string>
#include <random>
#include <dirent.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
static uint32_t rowsHash = 2166136261U;
static int drawn;
static int draw(JPEGDRAW *p) {
  // Firmware uses RGB565 and clips/scales for a 240x240 screen.
  if (p->iWidth < 0 || p->iHeight < 0 || p->iWidth * p->iHeight > MAX_BUFFERED_PIXELS) abort();
  for (int i = 0; i < p->iWidth*p->iHeight; i++) rowsHash = (rowsHash ^ p->pPixels[i]) * 16777619U;
  drawn++;
  return p->y < 240;
}
static int decode(std::vector<uint8_t> &v) {
  JPEGDEC jpg;
  int ok = 0;
  if (jpg.openRAM(v.data(), v.size(), draw)) {
    int scale=0, factor=1;
    while (factor < 8 && (jpg.getWidth()/factor > 240 || jpg.getHeight()/factor > 240)) factor*=2;
    scale = factor==2 ? JPEG_SCALE_HALF : factor==4 ? JPEG_SCALE_QUARTER : factor==8 ? JPEG_SCALE_EIGHTH : 0;
    jpg.setPixelType(RGB565_LITTLE_ENDIAN);
    ok=jpg.decode(0,0,scale);
  }
  jpg.close();
  return ok;
}
static std::vector<uint8_t> load(const std::string &path) {
  FILE *f=fopen(path.c_str(),"rb"); std::vector<uint8_t> v;
  if(!f) return v;
  for(int c;(c=fgetc(f))!=EOF;) v.push_back(c);
  fclose(f);return v;
}
int main(int argc,char **argv) {
  if(argc==3 && !strcmp(argv[1],"run")) {
    auto v=load(argv[2]);int ok=decode(v);
    printf("%s ok=%d draws=%d hash=%08x\n",argv[2],ok,drawn,rowsHash);return 0;
  }
  // valid <arquivos>: cada JPEG válido precisa chegar a desenhar algo. Um endurecimento que recusa
  // tudo deixa o fuzzing "sem falhas" (nada passa do cabeçalho): este modo pega isso.
  if(argc>=3 && !strcmp(argv[1],"valid")) {
    int bad=0;
    for(int i=2;i<argc;i++){auto v=load(argv[i]);drawn=0;rowsHash=2166136261U;int ok=decode(v);
      printf("%s ok=%d draws=%d hash=%08x\n",argv[i],ok,drawn,rowsHash);if(!drawn)bad++;}
    if(bad)printf("ERRO: %d JPEG(s) válido(s) não decodificaram\n",bad);
    return bad?1:0;
  }
  if(argc<5) return 2;
  bool fast=!strcmp(argv[1],"fast");
  std::vector<std::vector<uint8_t>> seeds;
  DIR *dir=opendir(argv[2]); if(!dir)return 2;
  while(auto e=readdir(dir))if(strstr(e->d_name,".jpg")) seeds.push_back(load(std::string(argv[2])+"/"+e->d_name));
  closedir(dir);if(seeds.empty())return 2;
  std::mt19937 rng(12345);int bad=0;
  for(int c=0;c<atoi(argv[3]);c++) {
    auto v=seeds[rng()%seeds.size()];if(v.size()>20000)v.resize(13+rng()%20000);
    for(int m=0,n=1+rng()%8;m<n&&!v.empty();m++) {
      size_t i=rng()%v.size();
      switch(rng()%4) {
        case 0:v[i]^=1<<(rng()%8);break;
        case 1:v[i]=rng();break;
        case 2:if(i+1<v.size()){v[i]=rng();v[i+1]=rng();}break;
        case 3:v.resize(i+1);break;
      }
    }
    if(fast){alarm(5);decode(v);alarm(0);}
    else {
      pid_t pid=fork();if(pid<0)return 2;
      if(!pid){alarm(5);decode(v);_exit(0);}
      int st;waitpid(pid,&st,0);
      if(!WIFEXITED(st)||WEXITSTATUS(st)) {
        char path[512];snprintf(path,sizeof path,"%s/failure-%03d.jpg",argv[4],bad++);
        FILE*f=fopen(path,"wb");if(!f)return 2;fwrite(v.data(),1,v.size(),f);fclose(f);if(bad>=20)break;
      }
    }
    if(c%5000==0)fprintf(stderr,"%d cases, %d failures\n",c,bad);
  }
  printf("failures=%d\n",bad);return bad?1:0;
}
