// Docs (docs.html e en/docs.html): menu de seções no celular, busca no menu,
// marca no menu a parte que está na tela e o botão de copiar dos blocos de comando.
// Atalho da busca: só "/" (Ctrl+K fica com o navegador).
// Sem JS a página continua inteira: o menu é HTML comum e os links são âncoras.
(function () {
  var html = document.documentElement, en = html.lang === "en";
  var T = en
    ? { copy: "Copy", copied: "Copied", select: "Selected: press Ctrl+C", none: "Nothing found for", sections: "Sections" }
    : { copy: "Copiar", copied: "Copiado", select: "Selecionado: aperte Ctrl+C", none: "Nada encontrado para", sections: "Seções" };

  // ---------- menu ----------
  var side = document.querySelector(".docs-side"), nav = document.getElementById("docs-nav");
  var secoes = document.getElementById("secoes"), secoesTxt = secoes && secoes.querySelector("span");
  if (!side || !nav) return;
  function abre(on) {
    side.classList.toggle("open", on);
    if (secoes) secoes.setAttribute("aria-expanded", String(on));
  }
  if (secoes) {
    secoes.hidden = false;
    secoes.addEventListener("click", function () { abre(!side.classList.contains("open")); });
  }
  nav.addEventListener("click", function (e) {
    if (e.target.closest("a")) { abre(false); limpa(); }
  });
  document.addEventListener("keydown", function (e) {
    if (e.key === "Escape" && side.classList.contains("open")) { abre(false); secoes.focus(); }
  });

  // cada link do menu e o trecho da página que ele representa
  var links = [].slice.call(nav.querySelectorAll('a[href^="#"]'));
  var alvos = links.map(function (a) { return document.getElementById(a.getAttribute("href").slice(1)); });

  // ---------- parte na tela ----------
  var pendente = false;
  function marca() {
    pendente = false;
    var lim = window.innerWidth >= 960 ? 110 : 160, ativo = -1;
    for (var i = 0; i < alvos.length; i++) {
      if (alvos[i] && alvos[i].getBoundingClientRect().top <= lim) ativo = i;
    }
    if (window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 2) ativo = alvos.length - 1;
    links.forEach(function (a, i) {
      if (i === ativo) a.setAttribute("aria-current", "location");
      else a.removeAttribute("aria-current");
    });
    nav.querySelectorAll(".grp").forEach(function (g) {
      g.classList.toggle("on", !!g.querySelector("[aria-current]"));
    });
    if (secoesTxt) secoesTxt.textContent = ativo >= 0 ? links[ativo].textContent : T.sections;
    // no computador o menu rola sozinho para a parte atual continuar à vista
    if (ativo >= 0 && window.innerWidth >= 960) {
      var a = links[ativo], s = side.getBoundingClientRect(), r = a.getBoundingClientRect();
      if (r.top < s.top + 40 || r.bottom > s.bottom - 40) side.scrollTop += r.top - s.top - s.height / 3;
    }
  }
  window.addEventListener("scroll", function () {
    if (!pendente) { pendente = true; requestAnimationFrame(marca); }
  }, { passive: true });
  window.addEventListener("resize", marca);
  marca();

  // ---------- busca no menu ----------
  // procura no título e no texto de cada parte; ignora acentos e maiúsculas
  var busca = document.getElementById("busca"), vazio = document.getElementById("docs-empty");
  function norm(s) { return s.normalize("NFD").replace(/[\u0300-\u036f]/g, "").toLowerCase(); }
  function texto(a, el) {
    if (!el) return "";
    if (el.tagName === "H3") {            // subtítulo: dele até o próximo subtítulo
      var t = el.textContent, n = el.nextElementSibling;
      while (n && !(n.tagName === "H3" && n.classList.contains("sub"))) { t += " " + n.textContent; n = n.nextElementSibling; }
      return t;
    }
    if (a.parentNode.classList.contains("grp") && el.querySelector(".topic, h3.sub")) {
      // parte com itens: só o cabeçalho dela (o resto já está nos itens)
      return [].slice.call(el.querySelectorAll(":scope > .eyebrow, :scope > h2, :scope > .section-lead"))
        .map(function (x) { return x.textContent; }).join(" ");
    }
    return el.textContent;
  }
  var indice = links.map(function (a, i) { return norm(a.textContent + " " + texto(a, alvos[i])); });
  function filtra() {
    var q = norm(busca.value).split(/\s+/).filter(Boolean), algum = false;
    links.forEach(function (a, i) {
      var ok = !q.length || q.every(function (w) { return indice[i].indexOf(w) >= 0; });
      a.classList.toggle("hit", ok && q.length > 0);
      if (a.parentNode.tagName === "LI") a.parentNode.hidden = !ok;
      if (ok) algum = true;
    });
    nav.querySelectorAll(".grp").forEach(function (g) {
      var label = g.querySelector(":scope > a");
      g.hidden = q.length > 0 && !label.classList.contains("hit") && !g.querySelector("li:not([hidden])");
    });
    vazio.hidden = algum || !q.length;
    if (!algum && q.length) vazio.textContent = T.none + " “" + busca.value.trim() + "”.";
    side.classList.toggle("busca", q.length > 0);
  }
  function limpa() { if (busca && busca.value) { busca.value = ""; filtra(); } }
  if (busca) {
    busca.addEventListener("input", filtra);
    busca.addEventListener("keydown", function (e) {
      if (e.key === "Escape") { limpa(); busca.blur(); }
      if (e.key === "Enter") {
        var a = links.filter(function (x) { return x.classList.contains("hit"); })[0];
        if (a) { e.preventDefault(); location.hash = a.getAttribute("href"); limpa(); busca.blur(); }
      }
    });
    document.addEventListener("keydown", function (e) {
      var campo = /^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName);
      if (e.key === "/" && !campo) {
        e.preventDefault();
        busca.focus();
      }
    });
  }

  // ---------- copiar comandos ----------
  document.querySelectorAll(".code").forEach(function (box) {
    var code = box.querySelector("code"), bar = box.querySelector(".bar");
    if (!code || !bar) return;
    var b = document.createElement("button");
    b.type = "button";
    b.className = "copy";
    b.textContent = T.copy;
    bar.appendChild(b);
    var volta;
    function diz(s) {
      b.textContent = s;
      clearTimeout(volta);
      volta = setTimeout(function () { b.textContent = T.copy; }, 1800);
    }
    function seleciona() {
      var r = document.createRange();
      r.selectNodeContents(code);
      var s = getSelection();
      s.removeAllRanges();
      s.addRange(r);
      diz(T.select);
    }
    b.addEventListener("click", function () {
      if (!navigator.clipboard) return seleciona();
      navigator.clipboard.writeText(code.textContent.replace(/\n$/, "")).then(function () { diz(T.copied); }, seleciona);
    });
  });
})();
