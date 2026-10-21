# Site do SmallTV Pomodoro

Branch `gh-pages`: o site publicado em https://davicorrea0.github.io/smalltv-pomodoro/.
O firmware está na branch [`main`](https://github.com/davicorrea0/smalltv-pomodoro).

Site estático, sem etapa de build: edite os arquivos e faça push.

| Arquivo | Conteúdo |
|---|---|
| `index.html` | Explicação, instalação, downloads e créditos |
| `galeria.html` + `galeria.js` | Galeria de mostradores |
| `faces.js` | Os mostradores embutidos (mesma ordem de `faces_list.h` na `main`) |
| `catalog/index.json` | Pacotes de mostradores instaláveis (formato em `catalog/README.md`) |
| `img/` | Imagens copiadas de `docs/` da `main` |

Para testar localmente: `npx serve .` (ou qualquer servidor estático) e abra o endereço mostrado.
