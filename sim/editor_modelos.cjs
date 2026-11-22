// Pacotes do editor de mostrador (web/editor.js) para o simulador conferir a tabela de tamanhos: cada modelo com
// cada conteúdo e fonte, com e sem rótulo, todos os blocos iguais e com o texto mais largo que o editor deixa
// escrever. Uma linha por pacote: as vagas "x,y,w,h;..." e o JSON do pacote, separados por tabulação.
// sim.cpp (testEditorModels) desenha cada um com o motor do firmware e recusa pixel fora das vagas.
// Uso: node editor_modelos.cjs out/editor-modelos.txt
const fs = require('fs'), path = require('path'), assert = require('assert');
const w = {};
new Function('window', fs.readFileSync(path.join(__dirname, '../web/editor.js'), 'utf8'))(w);
const E = w.faceEditor, lines = [];
// conteúdos e fontes do editor (o teste falha se o editor ganhar um e a lista não)
const combos = [];
for (const [k, K] of Object.entries(E.KINDS)) for (const f of K[3]) combos.push([k, f]);
assert.strictEqual(combos.length, 5, 'conteúdo ou fonte nova no editor: confira a lista do teste');
E.MODELS.forEach((M, m) => {
  for (const [k, f] of combos) for (const lbl of [false, true]) {
    // "W" é a letra mais larga da fonte pixel (todas avançam 6, mas W ocupa as 5 colunas)
    const b = M[1].map(s => ({c: k, f, k: '#f2eee6', t: 'W'.repeat(E.TXTMAX[s[4]]), l: lbl ? 'W'.repeat(E.LBLMAX[s[4]]) : ''}));
    const p = E.pack({m, n: 'teste', id: `ed-${m}-${k}-${f}-${lbl ? 1 : 0}`, b});
    const texts = p.layers.map(l => l.text);
    assert.strictEqual(texts.length, M[1].length * (lbl ? 2 : 1));
    for (const x of texts) assert(Buffer.byteLength(x) <= 47, 'texto passa de 47 bytes: ' + x);
    for (const l of p.layers) {
      // bebas só desenha 0-9 e ':' (a temperatura negativa perderia o "-"); hora com 40 px ou mais (docs/PACOTES.md)
      if (l.font == 'bebas') assert(/^(\{HH\}|\{MM\}|:)+$/.test(l.text), 'bebas com texto que ela não desenha: ' + l.text);
      if (l.text == '{HH}:{MM}') assert(l.font == 'bebas' ? l.size >= 40 : l.size >= 5, 'hora pequena demais: ' + JSON.stringify(l));
    }
    lines.push(M[1].map(s => s.slice(0, 4).join(',')).join(';') + '\t' + JSON.stringify(p));
  }
});
// texto com acento e °: bytes UTF-8, não letras
{
  const p = E.pack({m: 2, n: 'Café ☕ {HH}', id: 'x', b: [{c: 'texto', t: 'ÁÉÍÓÚÇÃÕÂÊ°{x}'}, {c: 'texto', t: 'ação'}, {c: 'hora', f: 'pixel', l: 'São Paulo'}]});
  assert.strictEqual(p.name, 'Café  HH');
  assert.strictEqual(p.layers[0].text, 'ÁÉÍÓÚÇÃÕÂÊ°x');   // sem { } (seriam variável) e cortado em 12 letras
  assert(p.layers.every(l => Buffer.byteLength(l.text) <= 47));
  // a hora pedida em pixel numa vaga pequena sai em bebas (pixel 3 teria 24 px, abaixo do mínimo do guia)
  assert.deepStrictEqual(p.layers.map(l => l.font), ['pixel', 'pixel', 'pixel', 'bebas']);
  assert.strictEqual(E.pack({m: 0, b: [{c: 'texto', t: '  '}]}).layers.length, 0);   // vazio: o editor não instala
}
fs.writeFileSync(process.argv[2], lines.join('\n') + '\n');
console.log(`editor: ${lines.length} pacotes de modelo para o simulador`);
