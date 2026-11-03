"""Monta os pacotes spec 2 desta pasta (imagens-spec2.stheme e dados-spec2.stheme) com as imagens
de sti/, que vieram prontas do empacotador do smalltv-mod (ver README.md). O contêiner é o mesmo
que o tools/theme_pack.py escreve: "STH1", u16 entradas, u16 0 e, por entrada, u16 tamanho do
caminho, u32 tamanho dos dados, caminho e dados; o theme.json primeiro, compacto e em ASCII.
Uso: python3 sim/stheme-seeds/montar.py
"""
import json
from pathlib import Path
import struct

HERE = Path(__file__).resolve().parent
FORMS = ['alpha-sti1', 'alpha-sti2-direct', 'alpha-sti2-indexed', 'opaque-sti1', 'opaque-sti2-direct', 'opaque-sti2-indexed']


def package(manifest, images):
    entries = [('theme.json', json.dumps(manifest, separators=(',', ':'), ensure_ascii=True).encode('ascii'))]
    entries += [(name + '.sti', (HERE/'sti'/(form + '.sti')).read_bytes()) for name, form in images]
    data = bytearray(b'STH1' + struct.pack('<HH', len(entries), 0))
    for path, payload in entries:
        data += struct.pack('<HI', len(path), len(payload)) + path.encode('ascii') + payload
    return bytes(data)


def theme(id, name):
    return {'id': id, 'name': name, 'author': 'SmallTV Pomodoro', 'version': '1.0.0'}


def images():
    """As seis formas de imagem do smalltv-mod, uma camada cada, e a hora por cima."""
    layers = [{'id': form, 'type': 'image', 'x': 20 + 100 * (i // 3), 'y': 20 + 30 * (i % 3),
               'source': 'images/%s.png' % form} for i, form in enumerate(FORMS)]
    layers.append({'id': 'hora', 'type': 'text', 'x': 120, 'y': 200, 'anchor': 'center',
                   'value': '{HH}:{MM}', 'size': 32, 'color': '#f2f2f2'})
    manifest = {'spec': 2, 'theme': theme('imagens-spec2', 'Imagens STI1 e STI2'),
                'display': {'width': 240, 'height': 240, 'background': '#000000'}, 'layers': layers}
    return package(manifest, [('images/%s.png' % form, form) for form in FORMS])


def data():
    """Fontes de dados (http e https com insecureTls), texto com acento escapado e bind em time.*."""
    manifest = {
        'spec': 2,
        'theme': theme('dados-spec2', 'Previsão e foco'),
        'display': {'width': 240, 'height': 240, 'background': '#16161a'},
        'data': [
            {'id': 'clima', 'url': 'http://192.168.1.10/clima.json', 'interval': 300,
             'fields': [{'id': 'temp', 'path': 'main.temp'}, {'id': 'cidade', 'path': 'name'}]},
            {'id': 'foco', 'url': 'https://exemplo.local/foco.json', 'interval': 60, 'insecureTls': True,
             'fields': [{'id': 'hoje', 'path': 'focus.today'}]},
        ],
        'layers': [
            {'id': 'clima', 'type': 'text', 'x': 120, 'y': 60, 'anchor': 'center',
             'value': '{clima.cidade} {clima.temp}C', 'size': 16, 'color': '#f2f2f2'},
            {'id': 'segundos', 'type': 'shape', 'shape': 'rectangle', 'x': 20, 'y': 200, 'width': 1, 'height': 6,
             'fill': '#ff5a47', 'bind': {'width': {'source': 'time.second', 'input': [0, 59], 'output': [0, 200]}}},
            {'id': 'focos', 'type': 'shape', 'shape': 'rectangle', 'x': 20, 'y': 220, 'width': 1, 'height': 6,
             'fill': '#3ecf7a', 'bind': {'width': {'source': 'device.focusToday', 'input': [0, 8], 'output': [0, 200]}}},
        ],
    }
    return package(manifest, [('images/alpha.png', 'alpha-sti2-indexed')])


if __name__ == '__main__':
    for name, build in (('imagens-spec2.stheme', images), ('dados-spec2.stheme', data)):
        (HERE/name).write_bytes(build())
        print(name, len(build()), 'bytes')
