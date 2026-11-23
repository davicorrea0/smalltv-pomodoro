// Sessões de foco: extrai e compila o motor REAL do Pomodoro com relógio e dependências simulados.
const fs = require('fs'), path = require('path');
const fw = path.join(__dirname, '../smalltv-pomodoro');
const pomo = fs.readFileSync(path.join(fw, 'modulos/pomodoro.cpp'), 'utf8');
const settings = fs.readFileSync(path.join(fw, 'modulos/settings.cpp'), 'utf8');
const portal = fs.readFileSync(path.join(fw, 'modulos/web_portal.cpp'), 'utf8');
const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const out = process.argv[2];
if (!out) throw Error('output.cpp required');

const a = pomo.indexOf('const char *PHASE_KEY[]'), b = pomo.indexOf('// ---------- avisos de fim de fase');
if (a < 0 || b < 0) throw Error('não achei o motor do Pomodoro');
const motor = pomo.slice(a, b);
for (const nome of ['phaseMinutes', 'startPhase', 'advance', 'tickPomodoro', 'command'])
  if (!motor.includes(nome + '(')) throw Error('não extraí ' + nome);

const fm = html.match(/function sessionMinutes\(x\)\{[^}]+\}/);
if (!fm) throw Error('não achei sessionMinutes no painel');
const sessionMinutes = Function('return (' + fm[0].replace(/^function /, 'function ') + ')')();
if (sessionMinutes({work:50,short:10,long:25,every:3,cycles:3}) !== 195 ||
    sessionMinutes({work:25,short:5,long:20,every:2,cycles:2}) !== 75) throw Error('fórmula dos templates incorreta');

// Compatibilidade da EEPROM e contrato da rota: as entradas seguem o argRange real (100 -> 99, -1/abc -> 0).
if (!/if \(cfg\.cycles > 99\) cfg\.cycles = 0;/.test(settings)) throw Error('loadSettings não limpa cycles inválido');
if (!/uint8_t cycles = argRange\("cycles", cfg\.cycles, 0, 99\);/.test(portal) ||
    !/if \(cycles != cfg\.cycles\) sessionFoci = 0;/.test(portal)) throw Error('rota de cycles não usa argRange ou não reinicia a contagem');
const ar = portal.match(/static uint8_t argRange[\s\S]*?\n\}/)?.[0] || '';
if (!ar.includes('constrain(server.arg(name).toInt(), lo, hi)')) throw Error('argRange mudou: reveja 100, -1 e abc');

const casos = [];
for (let n=1;n<=6;n++) for (let every=1;every<=8;every++) {
  const x={cycles:n,every,work:3+n,short:1+(every%3),long:4+(n%4)};
  casos.push({...x,total:sessionMinutes(x)});
}
const tabela = casos.map(x=>`{${x.work},${x.short},${x.long},${x.every},${x.cycles},${x.total}}`).join(',\n  ');

fs.writeFileSync(out, `#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
using String = std::string;
enum Phase : uint8_t { IDLE, WORK, SHORT_BREAK, LONG_BREAK };
struct Settings { uint8_t work, shortBreak, longBreak, longEvery, cycles, autoStart; uint16_t todayCount; } cfg;
static uint32_t simMs = 0;
static uint32_t millis() { return simMs; }
static int saves = 0, focos = 0, wakes = 0;
static void dayRolloverTick() {}
static void saveSettings() { saves++; }
static void statsFocus() { focos++; }
static void wakeScreen(uint32_t) { wakes++; }
${motor}
static int falhas = 0;
#define CONFERE(c, ...) do { if (!(c)) { printf("sessoes: FALHA "); printf(__VA_ARGS__); puts(""); falhas++; } } while (0)
static void limpa(uint8_t w,uint8_t s,uint8_t l,uint8_t e,uint8_t n,bool aut=true) {
  cfg={w,s,l,e,n,(uint8_t)aut,0}; simMs=0; phase=IDLE; running=false; totalMs=remainingMs=lastTick=0;
  cycleDone=sessionFoci=0; sessionDone=false; alertUntil=0; phaseSeq=shownSeq=0; saves=focos=wakes=0;
}
static void minuto() { simMs += 60000; tickPomodoro(); }
static int ateFim(bool manual=false, int limite=2000) {
  int m=0; command("start");
  while (phase != IDLE && m < limite) { if (manual && !running) command("start"); minuto(); m++; }
  return m;
}
static void templates() {
  limpa(50,10,25,3,3); command("start"); Phase seq[7]={WORK,SHORT_BREAK,WORK,SHORT_BREAK,WORK,LONG_BREAK,IDLE}; int k=0,m=0; Phase prev=phase;
  while (phase!=IDLE && m<300) { minuto();m++;if(phase!=prev){prev=phase;CONFERE(++k<7 && phase==seq[k],"sequência Deep na troca %d",k);} }
  CONFERE(m==195 && k==6 && sessionDone && sessionFoci==0 && phase==IDLE,"Deep: %d min, fase %d, done %d, sess %u",m,phase,sessionDone,sessionFoci);
  limpa(25,5,20,2,2); m=ateFim(); CONFERE(m==75 && sessionDone && sessionFoci==0,"Gentle: %d min",m);
}
static void semFim() {
  limpa(25,5,15,4,0); command("start"); Phase esperada[10]={SHORT_BREAK,WORK,SHORT_BREAK,WORK,SHORT_BREAK,WORK,LONG_BREAK,WORK,SHORT_BREAK,WORK};
  for(int k=0;k<10;k++){Phase p=phase;while(phase==p)minuto();CONFERE(phase==esperada[k],"cycles 0: troca %d virou %d",k,phase);}
  CONFERE(!sessionDone && phase!=IDLE,"cycles 0 encerrou a sessão");
}
static void comandos() {
  limpa(2,1,3,2,2); command("start"); command("skip"); CONFERE(sessionFoci==0 && phase==SHORT_BREAK,"skip do foco contou");
  command("skip"); CONFERE(phase==WORK && sessionFoci==0,"skip da pausa comum não voltou ao foco");
  for(int i=0;i<2;i++)minuto(); CONFERE(sessionFoci==1 && phase==SHORT_BREAK,"foco concluído não contou"); command("reset");
  CONFERE(phase==IDLE && !running && !sessionDone && !sessionFoci && !cycleDone,"reset não zerou a sessão");
  limpa(1,1,2,2,1); command("start"); minuto(); CONFERE(phase==LONG_BREAK && sessionFoci==1,"última pausa não foi longa"); command("skip");
  CONFERE(phase==IDLE && sessionDone && sessionFoci==0,"skip da última pausa não encerrou");
  limpa(1,1,2,4,3); command("start"); minuto(); minuto(); minuto(); minuto(); // dois focos e duas pausas
  CONFERE(phase==WORK && sessionFoci==2,"preparo para reduzir cycles falhou"); cfg.cycles=1; command("skip"); CONFERE(phase==SHORT_BREAK,"skip do foco após reduzir cycles falhou"); command("skip");
  CONFERE(phase==IDLE && sessionDone,"comparação >= não encerrou na próxima pausa");
  limpa(2,1,3,2,2,false); command("start"); minuto(); minuto(); CONFERE(phase==SHORT_BREAK && !running && sessionFoci==1,"autoStart 0 não parou após o foco 1");
  command("start"); minuto(); CONFERE(phase==WORK && !running,"autoStart 0 não parou após a pausa curta");
  command("start"); minuto(); minuto(); CONFERE(phase==LONG_BREAK && !running && sessionFoci==2,"autoStart 0 não parou após o foco 2");
  command("start"); minuto(); minuto(); minuto(); CONFERE(phase==IDLE && sessionDone,"autoStart 0 não concluiu após 8 min");
}
struct Caso { uint8_t w,s,l,e,n; int total; };
static Caso casos[] = {
  ${tabela}
};
static void formula() { for (auto x:casos) { limpa(x.w,x.s,x.l,x.e,x.n); int m=ateFim(); CONFERE(m==x.total,"fórmula n=%u every=%u: painel %d, firmware %d",x.n,x.e,x.total,m); } }
int main(){templates();semFim();comandos();formula();if(falhas)return 1;puts("sessoes: ok (motor real, templates 195/75 min, sem fim, skips, reset, >=, autoStart 0 e fórmula do painel)");return 0;}
`);
