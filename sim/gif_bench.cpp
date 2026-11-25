// Benchmark host do caminho GIF usado pelo album do firmware.
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

extern "C" void *gif_bench_malloc(size_t n);
extern "C" void *gif_bench_calloc(size_t n, size_t size);
extern "C" void gif_bench_free(void *p);

// Instrumenta apenas as alocacoes feitas pela copia local do AnimatedGIF.
#define GIF_MALLOC(sz) gif_bench_malloc(sz)
#define GIF_CALLOC(n, sz) gif_bench_calloc((n), (sz))
#define GIF_FREE(p) gif_bench_free(p)
#ifndef __LINUX__
#define __LINUX__
#endif
#include "../smalltv-pomodoro/src/AnimatedGIF/AnimatedGIF.cpp"

namespace fs = std::filesystem;

struct alignas(std::max_align_t) AllocHeader { size_t size; };
static size_t heapAtual, heapPico, mallocs, callocs, frees;

extern "C" void *gif_bench_malloc(size_t n) {
  if (n > SIZE_MAX - sizeof(AllocHeader)) return nullptr;
  auto *h = static_cast<AllocHeader *>(std::malloc(sizeof(AllocHeader) + n));
  if (!h) return nullptr;
  h->size = n;
  heapAtual += n;
  heapPico = std::max(heapPico, heapAtual);
  mallocs++;
  return h + 1;
}

extern "C" void *gif_bench_calloc(size_t n, size_t size) {
  if (size && n > SIZE_MAX / size) return nullptr;
  size_t bytes = n * size;
  void *p = gif_bench_malloc(bytes);
  if (p) std::memset(p, 0, bytes);
  mallocs--;
  callocs++;
  return p;
}

extern "C" void gif_bench_free(void *p) {
  if (!p) return;
  auto *h = static_cast<AllocHeader *>(p) - 1;
  heapAtual -= h->size;
  frees++;
  std::free(h);
}

static void iniciaMedicaoHeap() {
  mallocs = callocs = frees = 0;
  heapPico = heapAtual;
}

struct MemoryFile { const uint8_t *data; int size, pos; };
static MemoryFile entrada;

static void *abreEntrada(const char *, int32_t *size) {
  entrada.pos = 0;
  *size = entrada.size;
  return &entrada;
}
static void fechaEntrada(void *) {}
static int32_t leEntrada(GIFFILE *f, uint8_t *buf, int32_t len) {
  auto *in = static_cast<MemoryFile *>(f->fHandle);
  if (len < 0 || in->pos < 0 || in->pos > in->size) return 0;
  len = std::min(len, in->size - in->pos);
  if (len) std::memcpy(buf, in->data + in->pos, static_cast<size_t>(len));
  in->pos += len;
  f->iPos = in->pos;
  return len;
}
static int32_t buscaEntrada(GIFFILE *f, int32_t pos) {
  auto *in = static_cast<MemoryFile *>(f->fHandle);
  if (pos < 0) pos = 0;
  if (pos >= in->size) pos = in->size - 1;
  return f->iPos = in->pos = pos;
}

static uint32_t crcAtual;
static void crcByte(uint8_t v) {
  crcAtual ^= v;
  for (int i = 0; i < 8; i++) crcAtual = (crcAtual >> 1) ^ (0xedb88320U & (0U - (crcAtual & 1U)));
}
static void crc16(uint16_t v) { crcByte(static_cast<uint8_t>(v)); crcByte(static_cast<uint8_t>(v >> 8)); }
static void crc32(uint32_t v) { crc16(static_cast<uint16_t>(v)); crc16(static_cast<uint16_t>(v >> 16)); }

static void desenhaCRC(GIFDRAW *p) {
  // Inclui geometria, flags, indice RAW e RGB565 BE correspondente.
  crc32(static_cast<uint32_t>(p->iX));
  crc32(static_cast<uint32_t>(p->iY + p->y));
  crc32(static_cast<uint32_t>(p->iWidth));
  crcByte(p->ucHasTransparency);
  crcByte(p->ucTransparent);
  crcByte(p->ucDisposalMethod);
  for (int i = 0; i < p->iWidth; i++) {
    uint8_t px = p->pPixels[i];
    crcByte(px);
    crc16(p->pPalette[px]);
  }
}

static std::vector<uint8_t> carrega(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

template <class T> static T mediana(std::vector<T> v) {
  if (v.empty()) return T{};
  std::sort(v.begin(), v.end());
  size_t m = v.size() / 2;
  return v.size() & 1 ? v[m] : static_cast<T>((v[m - 1] + v[m]) / 2);
}

struct Amostra {
  double us;
  size_t mallocs, callocs, frees, pico;
  uint32_t crc;
};

struct Esperado { std::map<std::string, uint32_t> crc; };

static Esperado leEsperado(const std::string &nome) {
  Esperado e;
  if (nome.empty()) return e;
  std::ifstream f(nome);
  std::string linha, chave, valor;
  while (std::getline(f, linha)) {
    std::istringstream in(linha);
    if (std::getline(in, chave, '\t') && std::getline(in, valor))
      e.crc[chave] = static_cast<uint32_t>(std::stoul(valor, nullptr, 16));
  }
  return e;
}

static std::string caminhoCurto(const fs::path &p) {
  std::error_code ec;
  fs::path rel = fs::relative(p, fs::current_path(), ec);
  return (ec ? p : rel).generic_string();
}

int main(int argc, char **argv) {
  int repeticoes = 15;
  std::string crcWrite, crcCheck;
  std::vector<fs::path> raizes;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--runs" && i + 1 < argc) repeticoes = std::max(1, std::atoi(argv[++i]));
    else if (a == "--crc-write" && i + 1 < argc) crcWrite = argv[++i];
    else if (a == "--crc-check" && i + 1 < argc) crcCheck = argv[++i];
    else raizes.emplace_back(a);
  }
  if (raizes.empty()) raizes = {"sim/gif-seeds", "assets", "faces", "docs"};

  std::vector<fs::path> arquivos;
  for (const auto &raiz : raizes) {
    if (!fs::exists(raiz)) continue;
    if (fs::is_regular_file(raiz) && raiz.extension() == ".gif") arquivos.push_back(raiz);
    else if (fs::is_directory(raiz)) {
      for (const auto &e : fs::recursive_directory_iterator(raiz))
        if (e.is_regular_file() && e.path().extension() == ".gif") arquivos.push_back(e.path());
    }
  }
  std::sort(arquivos.begin(), arquivos.end());
  arquivos.erase(std::unique(arquivos.begin(), arquivos.end()), arquivos.end());
  Esperado esperado = leEsperado(crcCheck);
  std::ofstream saidaCRC;
  if (!crcWrite.empty()) saidaCRC.open(crcWrite);

  std::printf("CONFIG\truns=%d\tpalette=RGB565_BE\tdraw=RAW\tMAX_WIDTH=%d\t-O2\n", repeticoes, MAX_WIDTH);
  std::printf("ARQUIVO\tstatus\tquadros\tmediana_us_arquivo\tmediana_us_quadro\tpico_heap\talloc_por_quadro\tfree_por_quadro\tcrc\n");
  int falhasCRC = 0, validos = 0;
  std::vector<double> todosQuadros;
  for (const auto &arquivo : arquivos) {
    std::string nome = caminhoCurto(arquivo);
    auto dados = carrega(arquivo);
    if (dados.empty()) {
      std::printf("ARQUIVO\t%s\tvazio\t0\t0\t0\t0\t0\t0\t-\n", nome.c_str());
      continue;
    }
    std::vector<std::vector<Amostra>> porQuadro;
    std::vector<double> porArquivo;
    std::string erro;
    for (int run = -1; run < repeticoes; run++) {
      AnimatedGIF gif;
      gif.begin(GIF_PALETTE_RGB565_BE);
      entrada = {dados.data(), static_cast<int>(dados.size()), 0};
      if (!gif.open("entrada.gif", abreEntrada, fechaEntrada, leEntrada, buscaEntrada, desenhaCRC)) {
        erro = "rejeitado_open_" + std::to_string(gif.getLastError());
        gif.close();
        break;
      }
      std::vector<Amostra> rodada;
      auto inicioArquivo = std::chrono::steady_clock::now();
      for (int frame = 0; frame < 10000; frame++) {
        crcAtual = 0xffffffffU;
        iniciaMedicaoHeap();
        auto inicio = std::chrono::steady_clock::now();
        int delay = 0;
        int r = gif.playFrame(false, &delay);
        auto fim = std::chrono::steady_clock::now();
        if (r < 0 || gif.getLastError() != GIF_SUCCESS) {
          erro = "decode_" + std::to_string(gif.getLastError());
          break;
        }
        rodada.push_back({std::chrono::duration<double, std::micro>(fim - inicio).count(),
                          mallocs, callocs, frees, heapPico, crcAtual ^ 0xffffffffU});
        if (r == 0) break;
      }
      auto fimArquivo = std::chrono::steady_clock::now();
      gif.close();
      if (!erro.empty()) break;
      if (rodada.empty() || rodada.size() >= 10000) { erro = "sem_fim"; break; }
      if (run >= 0) {
        porArquivo.push_back(std::chrono::duration<double, std::micro>(fimArquivo - inicioArquivo).count());
        if (porQuadro.empty()) porQuadro.resize(rodada.size());
        if (porQuadro.size() != rodada.size()) { erro = "quadros_instaveis"; break; }
        for (size_t i = 0; i < rodada.size(); i++) porQuadro[i].push_back(rodada[i]);
      }
    }
    if (!erro.empty()) {
      std::printf("ARQUIVO\t%s\t%s\t0\t0\t0\t0\t0\t0\t-\n", nome.c_str(), erro.c_str());
      continue;
    }
    validos++;
    std::vector<double> usQuadros;
    std::vector<size_t> picos, ma, fr;
    bool crcOk = true;
    for (size_t i = 0; i < porQuadro.size(); i++) {
      std::vector<double> us;
      std::vector<size_t> mp, cp, fp, pp;
      uint32_t crc = porQuadro[i][0].crc;
      for (const auto &a : porQuadro[i]) {
        us.push_back(a.us); mp.push_back(a.mallocs); cp.push_back(a.callocs); fp.push_back(a.frees); pp.push_back(a.pico);
        if (a.crc != crc) crcOk = false;
      }
      std::string chave = nome + "#" + std::to_string(i);
      auto it = esperado.crc.find(chave);
      if (it != esperado.crc.end() && it->second != crc) crcOk = false;
      if (saidaCRC) saidaCRC << chave << '\t' << std::hex << std::setw(8) << std::setfill('0') << crc << std::dec << '\n';
      double mu = mediana(us);
      usQuadros.push_back(mu); todosQuadros.push_back(mu);
      ma.push_back(mediana(mp) + mediana(cp)); fr.push_back(mediana(fp)); picos.push_back(mediana(pp));
      std::printf("QUADRO\t%s\t%zu\t%.3f\t%zu\t%zu\t%zu\t%08x\t%s\n", nome.c_str(), i, mu,
                  mediana(mp) + mediana(cp), mediana(fp), mediana(pp), crc, crcOk ? "sim" : "nao");
    }
    if (!crcOk) falhasCRC++;
    std::printf("ARQUIVO\t%s\tok\t%zu\t%.3f\t%.3f\t%zu\t%zu\t%zu\t%s\n", nome.c_str(), porQuadro.size(),
                mediana(porArquivo), mediana(usQuadros), mediana(picos), mediana(ma), mediana(fr), crcOk ? "sim" : "nao");
  }
  std::vector<double> ciclosAlocacao;
  GIFIMAGE work{};
  int ciclos = std::max(1000, repeticoes * 100);
  for (int i = 0; i < ciclos; i++) {
    auto inicio = std::chrono::steady_clock::now();
    if (!gifWorkBegin(&work)) return 2;
    gifWorkEnd(&work);
    auto fim = std::chrono::steady_clock::now();
    ciclosAlocacao.push_back(std::chrono::duration<double, std::micro>(fim - inicio).count());
  }
  std::printf("ALOCACAO\tciclos=%d\tmediana_us=%0.3f\tallocs=4\tfrees=4\tbytes=%d\n", ciclos,
              mediana(ciclosAlocacao), GIF_TABLE_BYTES + GIF_PIXELS_BYTES + FILE_BUF_SIZE + 8 + GIF_SMALL_WORK_BYTES);
  std::printf("RESUMO\tarquivos=%zu\tvalidos=%d\tmediana_us_quadro=%.3f\tcrc_falhas=%d\theap_final=%zu\n",
              arquivos.size(), validos, mediana(todosQuadros), falhasCRC, heapAtual);
  return falhasCRC || !validos || heapAtual ? 1 : 0;
}
