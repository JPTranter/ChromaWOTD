import subprocess, os, sys, shutil, glob

def run(cmd, cwd):
    print('$ ' + ' '.join(cmd))
    return subprocess.run(cmd, cwd=cwd).returncode

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    test_dir = os.path.join(root, 'firmware', 'test')
    build_dir = os.path.join(test_dir, 'build')
    docs_images = os.path.join(root, 'docs', 'images')
    os.makedirs(docs_images, exist_ok=True)

    # Configure on first run so the documented one-liner works from a clean checkout.
    if not os.path.exists(os.path.join(build_dir, 'CMakeCache.txt')):
        print('[0/3] Configuring CMake build directory...')
        if run(['cmake', '-S', test_dir, '-B', build_dir, '-G', 'Ninja'], root) != 0:
            print('CMake configure failed!')
            sys.exit(1)

    print('[1/3] Building host tests...')
    if run(['cmake', '--build', build_dir, '--config', 'Debug'], root) != 0:
        print('CMake build failed!')
        sys.exit(1)

    print('[2/3] Running tests and generating PNG dumps...')
    if run(['ctest', '--test-dir', build_dir, '--output-on-failure'], root) != 0:
        print('Tests failed!')
        sys.exit(1)

    print('[3/3] Copying screenshots to docs/images/...')
    output_dir = os.path.join(test_dir, 'output')
    # Keep the archive in lockstep with the build: drop renders that no longer exist.
    for stale in glob.glob(os.path.join(docs_images, '*.png')):
        if os.path.basename(stale) not in {os.path.basename(p) for p in glob.glob(os.path.join(output_dir, '*.png'))}:
            os.remove(stale)
            print(f'  Removed stale: {os.path.basename(stale)}')
    for p in sorted(glob.glob(os.path.join(output_dir, '*.png'))):
        shutil.copy2(p, docs_images)
        print(f'  Synced: {os.path.basename(p)}')

    print('Done! All screenshots synchronized with current layout.')

if __name__ == '__main__':
    main()
