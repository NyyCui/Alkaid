"""下载锁定的包管理依赖到项目 third_party/package；--check 只检查，不联网。"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import urllib.request
import zipfile

project = Path(__file__).resolve().parents[1]
root = project / 'third_party' / 'package'
lock = json.loads((project / 'package-dependencies.lock.json').read_text(encoding='utf-8'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
archives = {}
for entry in lock['files']:
    target = root / entry['path']
    if not target.resolve().is_relative_to(root.resolve()):
        raise RuntimeError('依赖目标路径超出指定目录')
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() == entry['sha256']:
        continue
    if args.check:
        raise RuntimeError('依赖缺失或哈希不匹配：' + entry['path'])
    url = entry['url']
    if url.endswith('.zip'):
        if url not in archives:
            archives[url] = urllib.request.urlopen(url, timeout=90).read()
        folder = url.rsplit('/', 1)[1].removesuffix('.zip')
        with zipfile.ZipFile(io.BytesIO(archives[url])) as archive:
            data = archive.read(folder + '/' + target.name)
    else:
        data = urllib.request.urlopen(url, timeout=90).read()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise RuntimeError('下载内容与锁定哈希不一致：' + entry['path'])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
export = root / 'miniz' / 'miniz_export.h'
expected_export = '#pragma once\n#define MINIZ_EXPORT\n'
if args.check:
    if not export.exists() or export.read_text(encoding='utf-8') != expected_export:
        raise RuntimeError('miniz 静态导出头不匹配')
else:
    export.write_text(expected_export, encoding='utf-8', newline='\n')
print('包管理依赖检查通过' if args.check else '包管理依赖准备完成')
