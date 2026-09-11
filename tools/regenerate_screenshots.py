import subprocess, os, sys, shutil, glob

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    test_dir = os.path.join(root, 'firmware', 'test')
    build_dir = os.path.join(test_dir, 'build')
    docs_images = os.path.join(root, 'docs', 'images')
    os.makedirs(docs_images, exist_ok=True)

    print('[1/3] Building host tests...')
    res = subprocess.run(['cmake', '--build', build_dir, '--config', 'Debug'], cwd=root)
    if res.returncode != 0:
        print('CMake build failed!')
        sys.exit(1)

    print('[2/3] Running tests and generating PNG dumps...')
    res = subprocess.run(['ctest', '--test-dir', build_dir, '--output-on-failure'], cwd=root)
    if res.returncode != 0:
        print('Tests failed!')
        sys.exit(1)

    print('[3/3] Copying screenshots to docs/images/...')
    output_dir = os.path.join(test_dir, 'output')
    pngs = glob.glob(os.path.join(output_dir, '*.png'))
    for p in pngs:
        shutil.copy2(p, docs_images)
        print(f'  Synced: {os.path.basename(p)}')

    print('Done! All screenshots synchronized with current layout.')

if __name__ == '__main__':
    main()
