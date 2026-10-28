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

  // pacotes com sprites animados têm um GIF ("anim") além da prévia parada
  var still = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
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
          img.src = root + "catalog/" + (f.anim && !still ? f.anim : f.preview);
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

// "Veja de perto": abas que trocam a imagem mostrada
(function () {
  var viewer = document.getElementById("viewer");
  if (!viewer) return;
  var tabs = viewer.querySelectorAll("[role=tab]"), panels = viewer.querySelectorAll("[role=tabpanel]");
  tabs.forEach(function (t) {
    t.addEventListener("click", function () {
      tabs.forEach(function (o) {
        var on = o === t;
        o.setAttribute("aria-selected", String(on));
        o.setAttribute("aria-pressed", String(on));
      });
      panels.forEach(function (p) { p.hidden = p.dataset.shot !== t.dataset.shot; });
    });
  });
})();

// Horta de exemplo na página do jogo: 53 semanas x 7 dias, sempre o mesmo desenho
(function () {
  var g = document.getElementById("garden");
  if (!g) return;
  var seed = 7;
  function rnd() { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }
  for (var i = 0; i < 53 * 7; i++) {
    var c = document.createElement("i"), p = i / (53 * 7), r = rnd();
    if (i > 53 * 7 - 9) c.className = "on";                  // sequência atual
    else if (r < p * 0.55) c.className = "on";               // mais dias na meta perto de hoje
    else if (r < p * 0.85) c.className = "some";
    g.appendChild(c);
  }
})();

// Pomodoro animado ("Veja de perto"): cada estado é uma faixa de quadros do simulador
(function () {
  var box = document.getElementById("pomo-anim");
  if (!box) return;
  var cap = document.getElementById("pomo-cap"), buttons = box.parentNode.querySelectorAll("[data-state]");
  var still = window.matchMedia("(prefers-reduced-motion: reduce)").matches, timer = null;
  function play(b) {
    var n = +b.dataset.n, ms = +b.dataset.ms, i = 0;
    buttons.forEach(function (o) { o.setAttribute("aria-pressed", String(o === b)); });
    box.style.backgroundImage = "url(" + box.dataset.src + b.dataset.state + ".png)";
    box.style.backgroundSize = n * 100 + "% 100%";
    box.style.backgroundPosition = "0 0";
    cap.textContent = b.dataset.cap + " ";
    if (b.dataset.sub) {
      var s = document.createElement("span");
      s.className = "sub";
      s.textContent = "(" + b.dataset.sub + ")";
      cap.appendChild(s);
    }
    clearInterval(timer);
    if (still) return;
    timer = setInterval(function () {
      i = (i + 1) % n;
      box.style.backgroundPosition = (i * 100 / (n - 1)) + "% 0";
    }, ms);
  }
  buttons.forEach(function (b) { b.addEventListener("click", function () { play(b); }); });
  play(buttons[0]);
})();
