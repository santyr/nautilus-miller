#!/usr/bin/python3
"""Create explicit disposable Miller acceptance/performance fixtures; never clean user data."""
import argparse
import json
import pathlib


def create_fixture(root: pathlib.Path, entries: int = 32, depth: int = 3) -> dict:
    if not 0 <= entries <= 100_000 or not 0 <= depth <= 30:
        raise ValueError('entries must be 0..100000 and depth 0..30')
    # Do not resolve away a symlink before validating ownership of the root.
    if root.is_symlink():
        raise ValueError('root must be a new or empty directory, not a symlink')
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        raise ValueError('root must be a new or empty directory')
    root = root.absolute()
    for name in ('empty', 'many', 'deep', 'names', 'left', 'right'):
        (root / name).mkdir()
    for index in range(entries):
        (root / 'many' / f'row-{index:06d}.txt').write_bytes(b'Miller fixture content\n')
    deep = root / 'deep'
    for index in range(depth):
        deep /= f'level-{index:02d}'
        deep.mkdir()
    for name in ('猫 100% .txt', '$(must-not-run).txt', '-leading-dash.txt', '.hidden.txt', 'a' * 240 + '.txt'):
        (root / 'names' / name).write_bytes(b'Miller fixture content\n')
    (root / 'link').symlink_to('deep', target_is_directory=True)
    (root / 'deep' / 'cycle').symlink_to('.', target_is_directory=True)
    manifest = {'root': str(root), 'entries': entries, 'depth': depth, 'deep': str(deep)}
    (root / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=pathlib.Path, required=True)
    parser.add_argument('--entries', type=int, default=32)
    parser.add_argument('--depth', type=int, default=3)
    args = parser.parse_args()
    try:
        print(json.dumps(create_fixture(args.root, args.entries, args.depth)))
    except (ValueError, OSError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
