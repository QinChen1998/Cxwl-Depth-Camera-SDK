"""Check the actual release runtime's Qt module set and FFmpeg build license."""
import argparse
import ctypes
import json
import os
from pathlib import Path

ALLOWED_QT = {'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Network.dll',
              'Qt6Concurrent.dll', 'Qt6Svg.dll', 'Qt6SvgWidgets.dll', 'Qt6OpenGL.dll'}
def audit(root):
    qt = sorted({p.name for p in root.rglob('Qt6*.dll')})
    unexpected = sorted(set(qt) - ALLOWED_QT)
    if unexpected:
        raise ValueError(f'Unreviewed Qt modules in customer runtime: {unexpected}')
    if (root / 'qml').exists():
        raise ValueError('QML runtime must not be included in this Widgets-only release')
    for name in ('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll'):
        if name not in qt:
            raise ValueError(f'Missing runtime {name}')
    with os.add_dll_directory(str(root)):
        avcodec = ctypes.CDLL(str(root / 'avcodec-61.dll'))
        avcodec.avcodec_license.restype = ctypes.c_char_p
        avcodec.avcodec_configuration.restype = ctypes.c_char_p
        license_text = avcodec.avcodec_license().decode()
        configuration = avcodec.avcodec_configuration().decode()
    if not license_text.startswith('LGPL') or '--enable-gpl' in configuration or '--enable-nonfree' in configuration:
        raise ValueError(f'FFmpeg is not an approved LGPL build: {license_text} {configuration}')
    return {'qt_version': '6.11.1', 'qt_modules': qt, 'qt_license_option': 'LGPL-3.0-only',
            'ffmpeg_license': license_text, 'ffmpeg_configuration': configuration,
            'quick3d_present': False, 'gpl_only_qt_modules_present': False}
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.root.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('PASS: Qt LGPL module set and actual FFmpeg LGPL build')
