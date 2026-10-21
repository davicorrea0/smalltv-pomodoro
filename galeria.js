// Galeria: os mostradores embutidos (faces.js) mais os pacotes do catálogo
// catalog/index.json. Caminhos de "preview" e "file" no catálogo são relativos
// ao próprio index.json (ou URLs completas).
(function () {
  var CATALOG = "catalog/index.json";
  var grid = document.getElementById("faces");
  var status = document.getElementById("pkg-status");
  var builtin = (window.BUILTIN_FACES || []).map(function (f) {
    return Object.assign({ kind: "builtin" }, f);
  });
  var packages = [];
  var filter = "all";

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  function card(f) {
    var c = el("article", "face");
    c.dataset.kind = f.kind;

    var prev = el("button", "prev");
    prev.type = "button";
    prev.setAttribute("aria-label", "Ampliar " + f.name);
    var img = new Image(240, 240);
    img.className = "px";
    img.loading = "lazy";
    img.alt = "Prévia do mostrador " + f.name;
    img.src = f.preview;
    prev.appendChild(img);
    prev.addEventListener("click", function () { zoom(f); });
    c.appendChild(prev);

    var body = el("div", "body");
    var title = el("div", "title");
    title.appendChild(el("h3", null, f.name));
    if (f.kind === "builtin") title.appendChild(el("span", "num", "#" + f.num));
    title.appendChild(el("span", f.kind === "pkg" ? "tag pkg" : "tag", f.kind === "pkg" ? "Pacote" : "Embutido"));
    body.appendChild(title);
    if (f.desc) body.appendChild(el("p", "desc", f.desc));

    var by = el("div", "by", "por ");
    by.appendChild(el("b", null, f.author || "anônimo"));
    body.appendChild(by);

    if (f.kind === "pkg" && f.file) {
      var actions = el("div", "actions");
      var dl = el("a", "btn small primary", "Baixar");
      dl.href = f.file;
      dl.setAttribute("download", (f.id || "mostrador") + ".json");
      actions.appendChild(dl);
      body.appendChild(actions);
    } else if (f.kind === "builtin") {
      by.appendChild(document.createTextNode(" · já vem no firmware"));
    }
    c.appendChild(body);
    return c;
  }

  function render() {
    var list = builtin.concat(packages).filter(function (f) {
      return filter === "all" || f.kind === filter;
    });
    grid.textContent = "";
    list.forEach(function (f) { grid.appendChild(card(f)); });
    if (filter === "pkg" && !packages.length) {
      var p = el("p", null, "Ainda não há pacotes publicados. ");
      p.style.color = "var(--muted)";
      var a = el("a", null, "Saiba mais");
      a.href = "#pacotes";
      p.appendChild(a);
      grid.appendChild(p);
    }
    document.getElementById("n-all").textContent = builtin.length + packages.length;
    document.getElementById("n-builtin").textContent = builtin.length;
    document.getElementById("n-pkg").textContent = packages.length;
  }

  document.querySelectorAll(".tabs button").forEach(function (b) {
    b.addEventListener("click", function () {
      filter = b.dataset.filter;
      document.querySelectorAll(".tabs button").forEach(function (o) {
        o.setAttribute("aria-pressed", String(o === b));
      });
      render();
    });
  });

  // Visualização ampliada
  var dlg = document.getElementById("zoom");
  function zoom(f) {
    document.getElementById("zoom-img").src = f.preview;
    document.getElementById("zoom-img").alt = "Prévia do mostrador " + f.name;
    document.getElementById("zoom-name").textContent = f.name;
    document.getElementById("zoom-desc").textContent = (f.desc || "") + (f.author ? " · por " + f.author : "");
    if (dlg.showModal) dlg.showModal(); else dlg.setAttribute("open", "");
  }
  document.getElementById("zoom-close").addEventListener("click", function () { dlg.close(); });
  dlg.addEventListener("click", function (e) { if (e.target === dlg) dlg.close(); });

  function soon(extra) {
    status.textContent = "";
    var box = el("div", "soon");
    var img = new Image(64, 64);
    img.className = "px";
    img.src = "img/tomate.svg";
    img.alt = "";
    box.appendChild(img);
    var txt = el("div");
    txt.appendChild(el("h3", null, "Em breve"));
    txt.appendChild(el("p", null, "Os primeiros pacotes de mostradores estão sendo preparados. Quando forem publicados, aparecem aqui com prévia e botão para baixar."));
    if (extra) txt.appendChild(el("p", null, extra));
    box.appendChild(txt);
    status.appendChild(box);
  }

  render();
  fetch(CATALOG, { cache: "no-cache" })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (data) {
      var base = new URL(CATALOG, location.href);
      var items = Array.isArray(data) ? data : (data.faces || []);
      packages = items.filter(function (f) { return f && f.name && f.file; }).map(function (f) {
        return {
          kind: "pkg",
          id: f.id,
          name: f.name,
          author: f.author,
          desc: f.desc,
          preview: f.preview ? new URL(f.preview, base).href : "img/tomate.svg",
          file: new URL(f.file, base).href
        };
      });
      if (packages.length) status.textContent = ""; else soon();
      render();
    })
    .catch(function () { soon(); });
})();
