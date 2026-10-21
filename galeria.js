// Galeria: os pacotes do catálogo catalog/index.json (mostradores novos, instaláveis) e os
// mostradores embutidos (faces.js). No catálogo, "preview" e "file" são relativos ao
// próprio index.json (ou URLs completas); "name_en"/"desc_en" são usados na página em inglês.
(function () {
  var en = window.SITE_LANG === "en", root = window.SITE_ROOT || "";
  var S = en ? {
    zoom: "Enlarge ", preview: "Preview of ", by: "by ", builtin: "Built-in", pkg: "New",
    inFw: " · comes with the firmware", dl: "Download", soonT: "Coming soon",
    soonP: "The first face packs are being prepared. Once published they show up here with a preview and a download button."
  } : {
    zoom: "Ampliar ", preview: "Prévia do mostrador ", by: "por ", builtin: "Embutido", pkg: "Novo",
    inFw: " · já vem no firmware", dl: "Baixar", soonT: "Em breve",
    soonP: "Os primeiros pacotes de mostradores estão sendo preparados. Quando forem publicados, aparecem aqui com prévia e botão para baixar."
  };
  var CATALOG = root + "catalog/index.json";

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  function card(f) {
    var c = el("article", "face");
    if (f.kind === "pkg" && f.id) c.id = f.id;

    var prev = el("button", "prev");
    prev.type = "button";
    prev.setAttribute("aria-label", S.zoom + f.name);
    var img = new Image(240, 240);
    img.className = "px";
    img.loading = "lazy";
    img.alt = S.preview + f.name;
    img.src = f.preview;
    prev.appendChild(img);
    prev.addEventListener("click", function () { zoom(f); });
    c.appendChild(prev);

    var body = el("div", "body");
    var title = el("div", "title");
    title.appendChild(el("h3", null, f.name));
    if (f.kind === "builtin") title.appendChild(el("span", "num", "#" + f.num));
    title.appendChild(el("span", f.kind === "pkg" ? "tag pkg" : "tag", f.kind === "pkg" ? S.pkg : S.builtin));
    body.appendChild(title);
    if (f.desc) body.appendChild(el("p", "desc", f.desc));

    var by = el("div", "by", S.by);
    by.appendChild(el("b", null, f.author || "?"));
    body.appendChild(by);

    if (f.kind === "pkg") {
      if (f.size) by.appendChild(document.createTextNode(" · " + (f.size / 1024).toFixed(1).replace(".", en ? "." : ",") + " KB"));
      var actions = el("div", "actions");
      var dl = el("a", "btn small primary");
      var ico = new Image(16, 16);
      ico.className = "px";
      ico.alt = "";
      ico.src = root + "img/icons/download.svg";
      dl.append(ico, S.dl);
      dl.href = f.file;
      dl.setAttribute("download", (f.id || "mostrador") + ".json");
      actions.appendChild(dl);
      body.appendChild(actions);
    } else {
      by.appendChild(document.createTextNode(S.inFw));
    }
    c.appendChild(body);
    return c;
  }

  function fill(id, list) {
    var grid = document.getElementById(id);
    grid.textContent = "";
    list.forEach(function (f) { grid.appendChild(card(f)); });
    var n = document.getElementById(id + "-count");
    if (n) n.textContent = list.length;
  }

  // Visualização ampliada
  var dlg = document.getElementById("zoom");
  function zoom(f) {
    var img = document.getElementById("zoom-img");
    img.src = f.preview;
    img.alt = S.preview + f.name;
    document.getElementById("zoom-name").textContent = f.name;
    document.getElementById("zoom-desc").textContent = (f.desc || "") + (f.author ? " · " + S.by + f.author : "");
    var dl = document.getElementById("zoom-dl");
    dl.hidden = f.kind !== "pkg";
    if (f.kind === "pkg") {
      dl.href = f.file;
      dl.setAttribute("download", (f.id || "mostrador") + ".json");
    }
    if (dlg.showModal) dlg.showModal(); else dlg.setAttribute("open", "");
  }
  document.getElementById("zoom-close").addEventListener("click", function () { dlg.close(); });
  dlg.addEventListener("click", function (e) { if (e.target === dlg) dlg.close(); });

  function soon() {
    var grid = document.getElementById("packs");
    grid.textContent = "";
    var box = el("div", "soon");
    var img = new Image(64, 64);
    img.className = "px";
    img.src = root + "img/tomate.svg";
    img.alt = "";
    box.appendChild(img);
    var txt = el("div");
    txt.appendChild(el("h3", null, S.soonT));
    txt.appendChild(el("p", null, S.soonP));
    box.appendChild(txt);
    grid.appendChild(box);
  }

  fill("builtin", (window.BUILTIN_FACES || []).map(function (f) { return Object.assign({ kind: "builtin" }, f); }));
  fetch(CATALOG, { cache: "no-cache" })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (data) {
      var base = new URL(CATALOG, location.href);
      var items = Array.isArray(data) ? data : (data.faces || []);
      var packs = items.filter(function (f) { return f && f.name && f.file; }).map(function (f) {
        return {
          kind: "pkg",
          id: f.id,
          name: (en && f.name_en) || f.name,
          desc: (en && f.desc_en) || f.desc,
          author: f.author,
          size: f.size,
          preview: f.preview ? new URL(f.preview, base).href : root + "img/tomate.svg",
          file: new URL(f.file, base).href
        };
      });
      if (!packs.length) return soon();
      fill("packs", packs);
      if (location.hash.length > 1) {
        var t = document.getElementById(decodeURIComponent(location.hash.slice(1)));
        if (t) t.scrollIntoView();
      }
    })
    .catch(soon);
})();
