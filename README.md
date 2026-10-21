# Site do SmallTV Pomodoro

Branch `gh-pages`: o site publicado em https://davicorrea0.github.io/smalltv-pomodoro/
(em inglês: https://davicorrea0.github.io/smalltv-pomodoro/en/).
O firmware está na branch [`main`](https://github.com/davicorrea0/smalltv-pomodoro).

Site estático, sem etapa de build: edite os arquivos e faça push.

| Arquivo | Conteúdo |
|---|---|
| `index.html`, `en/index.html` | Explicação, instalação, downloads e créditos (PT e EN) |
| `galeria.html`, `en/gallery.html` + `galeria.js` | Galeria: mostradores novos (catálogo) e embutidos |
| `faces.js` | Os mostradores embutidos (mesma ordem de `faces_list.h` na `main`), com nomes em PT e EN |
| `catalog/` | Pacotes de mostradores instaláveis e `index.json` (formato em `catalog/README.md`) |
| `img/` | Imagens copiadas de `docs/` da `main`; `img/icons/` são ícones em pixel art (SVG) feitos dos sprites de `assets/` |

As páginas em `en/` declaram `<html lang="en" data-root="../">`; os scripts usam isso para
achar imagens e o catálogo e para escolher o idioma.

Para testar localmente: `npx serve .` (ou qualquer servidor estático) e abra o endereço mostrado.
