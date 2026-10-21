# Catálogo de mostradores

`index.json` lista os pacotes de mostradores instaláveis (formato 1 do motor `facepack` do
firmware). A galeria do site (`galeria.html`, `en/gallery.html`) e o painel do relógio leem
este arquivo.

```json
{
  "format": 1,
  "faces": [
    {
      "id": "bento",
      "name": "Bento",
      "author": "davicorrea0",
      "desc": "Cartões em grade, como widgets: hora, clima e focos",
      "name_en": "Bento",
      "desc_en": "Widget-style grid of cards: time, weather and focus",
      "file": "bento.json",
      "preview": "bento.png",
      "size": 2317,
      "fw": "1.6.5",
      "tags": ["digital"]
    }
  ]
}
```

- `file`: o pacote `.json` do mostrador (o mesmo formato de `faces/*.json` na `main`).
- `preview`: PNG 240×240 renderizado pelo simulador (`sim/build.sh` na `main`); o site amplia sem borrar.
- `name_en` / `desc_en`: opcionais, usados na página em inglês.
- `fw`: versão mínima do firmware (ex.: `"1.6.6"`); a galeria mostra "v1.6.6+" no cartão.
- `tags`: estilos para o filtro da galeria: `analogico`, `digital`, `pixel`, `esportivo`, `retro` (um ou mais).
- Caminhos relativos a este `index.json` (ou URLs completas). Entradas sem `name` ou `file` são ignoradas.
