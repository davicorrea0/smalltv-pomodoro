# Catálogo de mostradores

`index.json` lista os pacotes de mostradores instaláveis. A galeria do site
(`galeria.html`) e o painel do relógio leem este arquivo.

```json
{
  "version": 1,
  "faces": [
    {
      "id": "praia",
      "name": "Praia",
      "author": "Fulano",
      "desc": "Sol, mar e a hora em DSEG7",
      "preview": "praia.png",
      "file": "praia.json"
    }
  ]
}
```

- `preview`: PNG 240×240 (a tela em tamanho real; o site amplia sem borrar).
- `file`: o pacote `.json` do mostrador.
- Os dois caminhos são relativos a este `index.json` (ou URLs completas).
- Entradas sem `name` ou `file` são ignoradas.
