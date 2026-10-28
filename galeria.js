// Galeria: os pacotes do catálogo catalog/index.json (mostradores novos, instaláveis) e os
// mostradores embutidos (faces.js). No catálogo, "preview" e "file" são relativos ao
// próprio index.json (ou URLs completas); "name_en"/"desc_en" são usados na página em inglês,
// "fw" é a versão mínima do firmware e "tags" os estilos do filtro. "anim" é um GIF da
// animação (gravado pelo simulador), mostrado no lugar da prévia parada quando há sprites animados.
(function () {
  var en = window.SITE_LANG === "en", root = window.SITE_ROOT || "";
  var still = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  var S = en ? {
    zoom: "Enlarge ", preview: "Preview of ", by: "by ", builtin: "Built-in", anim: "animated",
    inFw: " · comes with the firmware", dl: "Download .json", soonT: "Coming soon",
    fw: function (v) { return "Requires firmware v" + v + " or newer"; },
    none: "No faces in this style here.",
    soonP: "The first face packs are being prepared. Once published they show up here with a preview and a download button."
  } : {
    zoom: "Ampliar ", preview: "Prévia do mostrador ", by: "por ", builtin: "Embutido", anim: "animado",
    inFw: " · já vem no firmware", dl: "Baixar .json", soonT: "Em breve",
    fw: function (v) { return "Precisa do firmware v" + v + " ou mais novo"; },
    none: "Nenhum mostrador deste estilo aqui.",
    soonP: "Os primeiros pacotes de mostradores estão sendo preparados. Quando forem publicados, aparecem aqui com prévia e botão para baixar."
  };
  var CATALOG = root + "catalog/index.json";
  var lists = { packs: [], builtin: [] };
  var kind = "all", style = "all";

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
    img.src = f.anim || f.preview;
    prev.appendChild(img);
    prev.addEventListener("click", function () { zoom(f); });
    c.appendChild(prev);

    var body = el("div", "body");
    var title = el("div", "title");
    title.appendChild(el("h3", null, f.name));
    if (f.kind === "builtin") {
      title.appendChild(el("span", "num", "#" + f.num));
      title.appendChild(el("span", "tag", S.builtin));
    } else if (f.fw) {
      var t = el("span", "tag pkg", "v" + f.fw + "+");
      t.title = S.fw(f.fw);
      title.appendChild(t);
    }
    if (f.animated) title.appendChild(el("span", "tag anim", S.anim));
    body.appendChild(title);
    if (f.desc) body.appendChild(el("p", "desc", f.desc));

    var by = el("div", "by", S.by);
    by.appendChild(el("b", null, f.author || "?"));
    body.appendChild(by);

    if (f.kind === "pkg") {
      if (f.size) by.appendChild(document.createTextNode(" · " + (f.size / 1024).toFixed(1).replace(".", en ? "." : ",") + " KB"));
      if (f.fw) body.appendChild(el("p", "req", S.fw(f.fw)));
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

  function match(f) { return style === "all" || (f.tags || []).indexOf(style) >= 0; }

  function fill(id) {
    var grid = document.getElementById(id);
    var list = lists[id].filter(match);
    grid.textContent = "";
    list.forEach(function (f) { grid.appendChild(card(f)); });
    if (!list.length && lists[id].length) {
      var p = el("p", null, S.none);
      p.style.color = "var(--muted)";
      grid.appendChild(p);
    }
    var n = document.getElementById(id + "-count");
    if (n) n.textContent = list.length;
    return list.length;
  }

  // Filtros: tipo (Todos / Novos / Embutidos) esconde as seções com data-kind;
  // estilo (Analógico, Digital...) filtra os cartões das duas grades.
  function render() {
    var np = fill("packs"), nb = fill("builtin");
    var set = function (id, v) { var e = document.getElementById(id); if (e) e.textContent = v; };
    set("n-pkg", np);
    set("n-builtin", nb);
    set("n-all", np + nb);
    document.querySelectorAll("section[data-kind]").forEach(function (s) {
      s.hidden = kind !== "all" && s.dataset.kind !== kind;
    });
    var first = document.querySelector("section[data-kind]:not([hidden])");
    document.querySelectorAll("section[data-kind]").forEach(function (s) {
      s.classList.toggle("first", s === first);
    });
  }
  function bind(attr, apply) {
    var buttons = document.querySelectorAll(".tabs [" + attr + "]");
    buttons.forEach(function (b) {
      b.addEventListener("click", function () {
        buttons.forEach(function (o) { o.setAttribute("aria-pressed", String(o === b)); });
        apply(b.getAttribute(attr));
        render();
      });
    });
  }
  bind("data-filter", function (v) { kind = v; });
  bind("data-style", function (v) { style = v; });

  // Visualização ampliada
  var dlg = document.getElementById("zoom");
  function zoom(f) {
    var img = document.getElementById("zoom-img");
    img.src = f.anim || f.preview;
    img.alt = S.preview + f.name;
    document.getElementById("zoom-name").textContent = f.name;
    document.getElementById("zoom-desc").textContent = (f.desc || "") + (f.author ? " · " + S.by + f.author : "") +
      (f.fw ? " · " + S.fw(f.fw) : "");
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

  lists.builtin = (window.BUILTIN_FACES || []).map(function (f) { return Object.assign({ kind: "builtin" }, f); });
  render();
  fetch(CATALOG, { cache: "no-cache" })
    .then(function (r) { return r.ok ? r.json() : Promise.reject(r.status); })
    .then(function (data) {
      var base = new URL(CATALOG, location.href);
      var items = Array.isArray(data) ? data : (data.faces || []);
      lists.packs = items.filter(function (f) { return f && f.name && f.file; }).map(function (f) {
        return {
          kind: "pkg",
          id: f.id,
          name: (en && f.name_en) || f.name,
          desc: (en && f.desc_en) || f.desc,
          author: f.author,
          size: f.size,
          fw: f.fw,
          tags: f.tags,
          preview: f.preview ? new URL(f.preview, base).href : root + "img/tomate.svg",
          animated: !!f.anim,
          anim: f.anim && !still ? new URL(f.anim, base).href : null,
          file: new URL(f.file, base).href
        };
      });
      if (!lists.packs.length) return soon();
      render();
      if (location.hash.length > 1) {
        var t = document.getElementById(decodeURIComponent(location.hash.slice(1)));
        if (t) t.scrollIntoView();
      }
    })
    .catch(soon);
})();
