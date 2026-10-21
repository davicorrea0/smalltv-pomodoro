// Página inicial: o relógio do topo troca de mostrador, a vitrine mostra alguns pacotes
// do catálogo e a área de downloads lê a versão mais recente da API do GitHub
// (com o link fixo para a página de releases como reserva).
(function () {
  var en = window.SITE_LANG === "en", root = window.SITE_ROOT || "";
  var screen = document.getElementById("screen");
  var label = document.getElementById("screen-name");
  var faces = window.BUILTIN_FACES || [];
  if (screen && faces.length) {
    var imgs = faces.map(function (f, i) {
      var img = i === 1 ? screen.querySelector("img") : new Image(240, 240);
      if (i !== 1) {
        img.className = "px";
        img.src = f.preview;
        img.alt = f.name;
        screen.appendChild(img);
      }
      return img;
    });
    var cur = 1;
    if (!window.matchMedia("(prefers-reduced-motion: reduce)").matches) {
      setInterval(function () {
        imgs[cur].classList.remove("on");
        cur = (cur + 1) % imgs.length;
        imgs[cur].classList.add("on");
        label.textContent = faces[cur].name;
      }, 2600);
    }
  }

  var shelf = document.getElementById("shelf");
  if (shelf && window.fetch) {
    fetch(root + "catalog/index.json", { cache: "no-cache" })
      .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
      .then(function (data) {
        var items = (data.faces || []).slice(0, 8);
        if (!items.length) return;
        shelf.textContent = "";
        items.forEach(function (f) {
          var a = document.createElement("a");
          a.href = (en ? "gallery.html" : "galeria.html") + "#" + f.id;
          var img = new Image(240, 240);
          img.className = "px";
          img.loading = "lazy";
          img.src = root + "catalog/" + f.preview;
          img.alt = (en && f.name_en) || f.name;
          a.title = img.alt;
          a.appendChild(img);
          shelf.appendChild(a);
        });
        var n = document.getElementById("shelf-count");
        if (n) n.textContent = data.faces.length;
      })
      .catch(function () {});
  }

  var bin = document.getElementById("rel-bin");
  if (!bin || !window.fetch) return;
  fetch("https://api.github.com/repos/davicorrea0/smalltv-pomodoro/releases/latest")
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (rel) {
      var asset = (rel.assets || []).filter(function (a) { return /\.bin$/i.test(a.name); })[0];
      document.getElementById("rel-ver").textContent = rel.tag_name + (en ? " · latest version" : " · versão mais recente");
      document.getElementById("rel-name").textContent = rel.name || rel.tag_name;
      if (asset) {
        bin.href = asset.browser_download_url;
        var info = document.getElementById("rel-info");
        info.textContent = "";
        var code = document.createElement("code");
        code.textContent = asset.name;
        info.append(code, " · " + Math.round(asset.size / 1024) + " KB · " +
          new Date(rel.published_at).toLocaleDateString(en ? "en-US" : "pt-BR"));
      }
    })
    .catch(function () { /* fica o link para a página de releases */ });
})();
