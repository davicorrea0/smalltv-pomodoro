// Mostradores embutidos no firmware, na ordem de smalltv-pomodoro/faces_list.h
// (o número é o salvo nas configurações). Prévias: docs/faces/ da branch main.
window.BUILTIN_FACES = [
  { id: "matrix",  name: "Matriz",    desc: "Pixel art com cartões de clima e focos do dia" },
  { id: "tomato",  name: "Tomate",    desc: "Cena em pixel art que muda com a hora e o clima" },
  { id: "analog",  name: "Analógico", desc: "Ponteiros bastão e marcas grossas" },
  { id: "weather", name: "Clima",     desc: "Temperatura grande e mini cartões" },
  { id: "neon",    name: "Neon",      desc: "Tubos rosa e ciano com brilho" },
  { id: "flip",    name: "Flip",      desc: "Plaquinhas que viram, com a fonte Bebas Neue" },
  { id: "lcd",     name: "LCD retrô", desc: "Relógio digital dos anos 80 (fonte DSEG7)" },
  { id: "words",   name: "Palavras",  desc: "A hora por extenso, em português" }
].map(function (f, i) {
  f.num = i;
  f.author = "Davi Mattos";
  f.preview = "img/faces/face-" + i + ".png";
  return f;
});
