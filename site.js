// Página inicial: o relógio do topo troca de mostrador e a área de downloads
// mostra a versão mais recente lida da API do GitHub (com o link fixo como reserva).
(function () {
  var screen = document.getElementById("screen");
  var label = document.getElementById("screen-name");
  var faces = window.BUILTIN_FACES || [];
  if (screen && faces.length) {
    var imgs = faces.map(function (f, i) {
      var img = i === 1 ? screen.querySelector("img") : new Image(240, 240);
      if (i !== 1) {
        img.className = "px";
        img.src = f.preview;
        img.alt = "Mostrador " + f.name;
        screen.appendChild(img);
      }
      return img;
    });
    var cur = 1;
    var still = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    if (!still) {
      setInterval(function () {
        imgs[cur].classList.remove("on");
        cur = (cur + 1) % imgs.length;
        imgs[cur].classList.add("on");
        label.textContent = faces[cur].name;
      }, 2600);
    }
  }

  var bin = document.getElementById("rel-bin");
  if (!bin || !window.fetch) return;
  fetch("https://api.github.com/repos/davicorrea0/smalltv-pomodoro/releases/latest")
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (rel) {
      var asset = (rel.assets || []).filter(function (a) { return /\.bin$/i.test(a.name); })[0];
      document.getElementById("rel-ver").textContent = rel.tag_name + " · versão mais recente";
      document.getElementById("rel-name").textContent = rel.name || rel.tag_name;
      if (asset) {
        bin.href = asset.browser_download_url;
        var info = document.getElementById("rel-info");
        info.textContent = "";
        var code = document.createElement("code");
        code.textContent = asset.name;
        info.append(code, " · " + Math.round(asset.size / 1024) + " KB · " +
          new Date(rel.published_at).toLocaleDateString("pt-BR"));
      }
    })
    .catch(function () { /* fica o link para a página de releases */ });
})();
