"""构建独立 desktop 项目及框架无关 SDK，不联网、不打包原型备份。"""
import argparse
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', required=True)
    parser.add_argument('--typescript', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    desktop = project.parent / 'desktop'
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    # 清理早期 rootDir 配置产生的两个旧产物，避免被开发侧打包器再次收入包中。
    for sdk_root in [output / 'sdk', output / 'samples' / 'inspector' / 'sdk']:
        for filename in ['index.js', 'method_catalog.js']:
            stale = sdk_root / 'src' / filename
            if stale.resolve().is_relative_to((project / 'build').resolve()):
                stale.unlink(missing_ok=True)
        stale_dir = sdk_root / 'src'
        if stale_dir.exists() and not any(stale_dir.iterdir()) and stale_dir.resolve().is_relative_to((project / 'build').resolve()):
            stale_dir.rmdir()
    for name in ['index.html', '1.css', 'runtime.css', 'src/main.js', 'src/bridge.js', 'src/packages.js', 'src/interactions.js', 'src/workspace.js', 'experience.css']:
        target = output / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(desktop / name, target)
    shutil.copytree(desktop / 'assets', output / 'assets', dirs_exist_ok=True)
    subprocess.run([args.node, args.typescript, '-p', str(project / 'packages/sdk/tsconfig.json'),
                    '--noEmit', 'false', '--rewriteRelativeImportExtensions', 'true',
                    '--outDir', str(output / 'sdk')], check=True)
    for bundled in ['inspector', 'files']:
        sample = output / 'samples' / bundled
        for name in ['index.html', '1.css', 'runtime.css', 'src/main.js']:
            target = sample / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(project / 'samples' / bundled / name, target)
        shutil.copy2(desktop / 'src/bridge.js', sample / 'src/bridge.js')
        shutil.copytree(output / 'sdk', sample / 'sdk', dirs_exist_ok=True)
    notes = output / 'samples' / 'notes'
    for name in ['index.html', 'notes.css', 'src/main.js']:
        target = notes / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(project / 'samples' / 'notes' / name, target)
    shutil.copy2(desktop / 'src/bridge.js', notes / 'src/bridge.js')
    shutil.copytree(output / 'sdk', notes / 'sdk', dirs_exist_ok=True)
    network = output / 'samples' / 'network'
    for name in ['index.html', 'network.css', 'src/main.js']:
        target = network / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(project / 'samples' / 'network' / name, target)
    shutil.copy2(desktop / 'src/bridge.js', network / 'src/bridge.js')
    shutil.copytree(output / 'sdk', network / 'sdk', dirs_exist_ok=True)
    serial = output / 'samples' / 'serial'
    for name in ['index.html', 'serial.css', 'src/main.js']:
        target = serial / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(project / 'samples' / 'serial' / name, target)
    shutil.copy2(desktop / 'src/bridge.js', serial / 'src/bridge.js')
    shutil.copytree(output / 'sdk', serial / 'sdk', dirs_exist_ok=True)
    print(f'桌面构建完成：{output}')


if __name__ == '__main__':
    main()
