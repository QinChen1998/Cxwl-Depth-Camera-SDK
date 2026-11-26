"""Generate or check the three deployment profiles from the app-local SDK/bin."""
import argparse
import json
from pathlib import Path


def generate(root, check=False):
    bins = root / 'bin'
    components = {'core': [], 'capture': [], 'viewer': []}
    for path in sorted(bins.rglob('*')):
        if not path.is_file():
            continue
        name = path.relative_to(bins).as_posix()
        if '/' not in name and name.startswith('CxwlDepth') and name.endswith('.exe'):
            continue
        if (name.startswith(('avcodec-', 'avformat-',
                             'avutil-', 'swresample-', 'swscale-'))):
            group = 'capture'
        elif (name.startswith(('licenses/', 'tls/', 'networkinformation/', 'msvcp', 'vcruntime', 'concrt'))
              or name in ('CxwlDepthSdk.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'Qt6Concurrent.dll')):
            group = 'core'
        else:
            group = 'viewer'
        components[group].append(name)

    required = {'core': ('CxwlDepthSdk.dll', 'tls/qschannelbackend.dll'), 'capture': ('avcodec-61.dll', 'avformat-61.dll'),
                'viewer': ('Qt6Widgets.dll',)}
    for group, names in required.items():
        components[group].sort()
        for name in names:
            if name not in components[group]:
                raise ValueError(f'Missing {name}; use the complete SDK package')
    profiles = {'MINIMAL': components['core'], 'STANDARD': components['core'] + components['capture'],
                'FULL': components['core'] + components['capture'] + components['viewer']}
    result = {
        'schema': 1,
        'generated_by': 'Script/generate_runtime_components.py',
        'components': components,
        'profiles': {name: {'files': sorted(files), 'bytes': sum((bins / file).stat().st_size for file in files)}
                     for name, files in profiles.items()},
    }
    cmake = ['# Generated with runtime-components.json by generate_runtime_components.py; do not edit.']
    for name, profile in result['profiles'].items():
        cmake.extend([f'set(CXWL_RUNTIME_{name}_FILES', *(f'  "{file}"' for file in profile['files']), ')'])
    outputs = {'runtime-components.json': json.dumps(result, indent=2) + '\n',
               'cmake/RuntimeComponents.cmake': '\n'.join(cmake) + '\n'}
    for name, contents in outputs.items():
        path = root / name
        expected = contents.encode('utf-8')
        if check:
            if not path.is_file() or path.read_bytes() != expected:
                raise ValueError(f'Generated contents differ from {name}')
        else:
            path.write_bytes(expected)
    print(json.dumps({name: info['bytes'] for name, info in result['profiles'].items()}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path(__file__).resolve().parent.parent / 'SDK')
    parser.add_argument('--check', action='store_true', help='Compare both manifests without writing any files')
    args = parser.parse_args()
    generate(args.sdk.resolve(), args.check)
