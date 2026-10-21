// Mostradores embutidos no firmware, na ordem de smalltv-pomodoro/faces_list.h
// (o número é o salvo nas configurações). Prévias: docs/faces/ da branch main.
// As páginas em inglês ficam em en/ e declaram <html data-root="../">.
window.SITE_ROOT = document.documentElement.getAttribute("data-root") || "";
window.SITE_LANG = document.documentElement.lang === "en" ? "en" : "pt";
window.BUILTIN_FACES = [
  { id: "matrix",  name: "Matriz",    desc: "Pixel art com cartões de clima e focos do dia",
    name_en: "Matrix",   desc_en: "Pixel art with weather and daily focus cards" },
  { id: "tomato",  name: "Tomate",    desc: "Cena em pixel art que muda com a hora e o clima",
    name_en: "Tomato",   desc_en: "Pixel art scene that changes with the time and weather" },
  { id: "analog",  name: "Analógico", desc: "Ponteiros bastão e marcas grossas",
    name_en: "Analog",   desc_en: "Baton hands and bold markers" },
  { id: "weather", name: "Clima",     desc: "Temperatura grande e mini cartões",
    name_en: "Weather",  desc_en: "Big temperature and mini cards" },
  { id: "neon",    name: "Neon",      desc: "Tubos rosa e ciano com brilho",
    name_en: "Neon",     desc_en: "Glowing pink and cyan tubes" },
  { id: "flip",    name: "Flip",      desc: "Plaquinhas que viram, com a fonte Bebas Neue",
    name_en: "Flip",     desc_en: "Flipping cards in Bebas Neue" },
  { id: "lcd",     name: "LCD retrô", desc: "Relógio digital dos anos 80 (fonte DSEG7)",
    name_en: "Retro LCD", desc_en: "80s digital watch (DSEG7 font)" },
  { id: "words",   name: "Palavras",  desc: "A hora por extenso, em português",
    name_en: "Words",    desc_en: "The time spelled out, in Portuguese" }
].map(function (f, i) {
  if (window.SITE_LANG === "en") { f.name = f.name_en; f.desc = f.desc_en; }
  f.num = i;
  f.author = "davicorrea0";
  f.preview = window.SITE_ROOT + "img/faces/face-" + i + ".png";
  return f;
});
