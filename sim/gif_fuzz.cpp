// Teste de robustez do leitor de GIF (AnimatedGIF) com AddressSanitizer, no WSL:
//   bash sim/test-gif.sh [corpus] [casos]  compila e roda no caminho de 32 bits do firmware
//   ./fuzz run <arquivo.gif>               decodifica um arquivo (para reproduzir)
//   ./fuzz fuzz <seeds> <casos> <saida>    corrompe as sementes e testa cada caso num processo filho
#ifndef GIF_UPSTREAM
#include "../smalltv-pomodoro/src/AnimatedGIF/AnimatedGIF.cpp"
#else
#include "AnimatedGIF.cpp"
#endif
#include "../smalltv-pomodoro/gifcheck.h"
#include <dirent.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <random>

// mesma lógica do gifDraw do firmware (main.cpp), desenhando num quadro 240x240
static uint16_t fb[240 * 240];
static int drawnLines;
static uint32_t drawHash = 2166136261U;
static int gifOffX, gifOffY;
static void gifDraw(GIFDRAW *p) {
  drawnLines++;
  static uint16_t buf[240];
  int y = p->iY + p->y + gifOffY;
  if (y < 0 || y >= 240) return;
  int x0 = p->iX + gifOffX;
  int w = p->iWidth;
  if (x0 + w > 240) w = 240 - x0;
  if (w <= 0) return;
  uint8_t *s = p->pPixels;
  uint16_t *pal = p->pPalette;
  if (p->ucDisposalMethod == 2) {
    for (int i = 0; i < w; i++)
      if (s[i] == p->ucTransparent) s[i] = p->ucBackground;
    p->ucHasTransparency = 0;
  }
  if (p->ucHasTransparency) {
    uint8_t tr = p->ucTransparent;
    int x = 0;
    while (x < w) {
      while (x < w && s[x] == tr) x++;
      int start = x, n = 0;
      while (x < w && s[x] != tr) buf[n++] = pal[s[x++]];
      for (int i = 0; i < n; i++) fb[y * 240 + x0 + start + i] = buf[i];
    }
  } else {
    for (int i = 0; i < w; i++) buf[i] = pal[s[i]];
    for (int i = 0; i < w; i++) fb[y * 240 + x0 + i] = buf[i];
  }
  for (int i = 0; i < 240; i++) drawHash = (drawHash ^ fb[y * 240 + i]) * 16777619U;
}

struct MemoryFile { const uint8_t *data; int size, pos; };
static MemoryFile input;
static void *openInput(const char *, int32_t *size) { *size = input.size; input.pos = 0; return &input; }
static void closeInput(void *) {}
static int32_t readInput(GIFFILE *f, uint8_t *buf, int32_t len) {
  auto *in = (MemoryFile *)f->fHandle;
  if (len < 0 || in->pos < 0 || in->pos > in->size) return 0;
  if (len > in->size - in->pos) len = in->size - in->pos;
  if (len) memcpy(buf, in->data + in->pos, len);
  in->pos += len; f->iPos = in->pos;
  return len;
}
static int32_t seekInput(GIFFILE *f, int32_t pos) {
  auto *in = (MemoryFile *)f->fHandle;
  if (pos < 0 || pos > in->size) return in->pos;
  return f->iPos = in->pos = pos;
}
static int checkInput(void *ctx, uint8_t *buf, int n) {
  auto *in = (MemoryFile *)ctx;
  if (n > in->size - in->pos) n = in->size - in->pos;
  if (n) memcpy(buf, in->data + in->pos, n);
  in->pos += n;
  return n;
}

static int decode(const uint8_t *d, int n) {
  static AnimatedGIF gif;  // o firmware também usa um objeto só (new uma vez por arquivo)
  gif.begin(GIF_PALETTE_RGB565_LE);
  bool opened;
  if (getenv("GIF_CALLBACKS")) {
    input = {d, n, 0};
    opened = gif.open("input.gif", openInput, closeInput, readInput, seekInput, gifDraw);
  } else opened = gif.open((uint8_t *)d, n, gifDraw);
  if (!opened) { gif.close(); return 0; }
  gifOffX = (240 - gif.getCanvasWidth()) / 2;
  if (gifOffX < 0) gifOffX = 0;
  gifOffY = (240 - gif.getCanvasHeight()) / 2;
  if (gifOffY < 0) gifOffY = 0;
  int frames = 0, delay = 0, r;
  while (frames < 300) {
    int before = drawnLines;
    r = gif.playFrame(false, &delay);
    if (getenv("GIF_TRACE")) fprintf(stderr, "frame=%d size=%dx%d lines=%d status=%d error=%d\n", frames,
      gif.getFrameWidth(), gif.getFrameHeight(), drawnLines-before, r, gif.getLastError());
    if (r <= 0) break;
    frames++;
  }
  gif.close();
  return frames;
}

static std::vector<uint8_t> load(const std::string &p) {
  FILE *f = fopen(p.c_str(), "rb");
  std::vector<uint8_t> v;
  if (!f) return v;
  int c;
  while ((c = fgetc(f)) != EOF) v.push_back((uint8_t)c);
  fclose(f);
  return v;
}

int main(int argc, char **argv) {
  if (argc >= 3 && !strcmp(argv[1], "check")) {
    auto v = load(argv[2]);
    MemoryFile in{v.data(), (int)v.size(), 0};
    const char *error = gifCheck(checkInput, &in, 480, 480);
    printf("%s: %s\n", argv[2], error ? error : "accepted");
    return error ? 1 : 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "run")) {
    auto v = load(argv[2]);
    printf("%s: %d quadros\n", argv[2], decode(v.data(), (int)v.size()));
    uint32_t hash = 2166136261U;
    for (auto pixel : fb) { hash = (hash ^ pixel) * 16777619U; }
    printf("lines=%d framebuffer=%08x all_rows=%08x\n", drawnLines, hash, drawHash);
    return 0;
  }
  bool fast = argc >= 2 && !strcmp(argv[1], "fuzz-fast");
  if (argc < 5 || (!fast && strcmp(argv[1], "fuzz"))) return 2;
  std::vector<std::vector<uint8_t>> seeds;
  if (DIR *dir = opendir(argv[2])) {
    while (dirent *e = readdir(dir))
      if (strstr(e->d_name, ".gif")) seeds.push_back(load(std::string(argv[2]) + "/" + e->d_name));
    closedir(dir);
  }
  long cases = atol(argv[3]);
  if (seeds.empty()) { fprintf(stderr, "No GIF seeds found\n"); return 2; }
  std::mt19937 rng(12345);
  int bad = 0;
  const uint8_t INTERESTING[] = {0, 1, 2, 7, 8, 11, 12, 13, 0x7F, 0x80, 0xFE, 0xFF, 0x2C, 0x21, 0xF9, 0x3B};
  for (long c = 0; c < cases; c++) {
    std::vector<uint8_t> v = seeds[rng() % seeds.size()];
    if (v.size() > 20000) v.resize(13 + rng() % 20000);  // arquivos grandes: só o começo (mais casos por segundo)
    int muts = 1 + rng() % 8;
    for (int m = 0; m < muts && !v.empty(); m++) {
      size_t i = rng() % v.size();
      switch (rng() % 7) {
        case 0: v[i] ^= 1 << (rng() % 8); break;
        case 1: v[i] = INTERESTING[rng() % sizeof INTERESTING]; break;
        case 2: v[i] = rng(); break;
        case 3: if (i + 1 < v.size()) { v[i] = rng(); v[i + 1] = rng(); } break;  // tamanhos de 16 bits
        case 4: v.resize(i + 1); break;                                              // arquivo cortado
        case 5: { size_t l = 1 + rng() % 32; if (i + l <= v.size()) { auto copy = std::vector<uint8_t>(v.begin() + i, v.begin() + i + l); v.insert(v.begin() + i, copy.begin(), copy.end()); } } break;
        case 6: { size_t l = 1 + rng() % 32; if (i + l <= v.size()) v.erase(v.begin() + i, v.begin() + i + l); } break;
      }
    }
    if (fast) {
      alarm(5);
      decode(v.data(), (int)v.size());
      alarm(0);
      if (c % 5000 == 0) fprintf(stderr, "%ld casos, 0 falhas\n", c);
      continue;
    }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 2; }
    if (!pid) {
      alarm(5);  // laço infinito também é falha (o relógio travaria)
      decode(v.data(), (int)v.size());
      _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    if (!(WIFEXITED(st) && WEXITSTATUS(st) == 0)) {
      char name[512];
      snprintf(name, sizeof name, "%s/falha_%05d_%s.gif", argv[4], bad++,
               WIFSIGNALED(st) ? (WTERMSIG(st) == SIGALRM ? "trava" : "sinal") : "asan");
      FILE *f = fopen(name, "wb");
      fwrite(v.data(), 1, v.size(), f);
      fclose(f);
      if (bad >= 50) break;
    }
    if (c % 5000 == 0) fprintf(stderr, "%ld casos, %d falhas\n", c, bad);
  }
  printf("fim: %d falhas\n", bad);
  return bad ? 1 : 0;
}
