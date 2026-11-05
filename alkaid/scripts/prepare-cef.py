"""显式准备锁定版本的 CEF，仅在项目 D 盘目录下载并校验，不在 CMake 配置时联网。"""
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def main():
    lock = json.loads((ROOT / 'cef.lock.json').read_text(encoding='utf-8'))
    destination = ROOT / 'third_party/cef'
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / lock['archive']
    extracted = destination / lock['archive'].removesuffix('.tar.bz2')
    marker = extracted / '.alkaid-verified.json'
    if marker.exists() and json.loads(marker.read_text(encoding='utf-8')).get('sha1') == lock['sha1']:
        print(f'CEF 已准备：{extracted}', flush=True)
        return
    if not archive.exists():
        url = lock['source'] + urllib.parse.quote(lock['archive'], safe='._-')
        partial = archive.with_suffix(archive.suffix + '.partial')
        print(f'下载 CEF，大小 {lock["size"] / 1024 / 1024:.1f} MiB', flush=True)
        with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as output:
            downloaded = 0
            next_report = 16 * 1024 * 1024
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
                downloaded += len(chunk)
                if downloaded >= next_report:
                    print(f'已下载 {downloaded / 1024 / 1024:.0f} MiB', flush=True)
                    next_report += 16 * 1024 * 1024
        if partial.stat().st_size != lock['size']:
            raise RuntimeError('下载文件大小与锁定信息不符')
        partial.replace(archive)
    with archive.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha1').hexdigest()
    if digest != lock['sha1']:
        raise RuntimeError('CEF 下载校验失败，请检查下载文件与版本锁定信息')
    print('校验通过，正在解压……', flush=True)
    with tarfile.open(archive, 'r:bz2') as package:
        for member in package.getmembers():
            target = (destination / member.name).resolve()
            if not target.is_relative_to(extracted.resolve()):
                raise RuntimeError('CEF 分发包包含预期目录以外的内容')
        package.extractall(destination, filter='data')
    marker.write_text(json.dumps(lock, indent=2), encoding='utf-8')
    print(f'CEF 已准备：{extracted}', flush=True)


if __name__ == '__main__':
    main()
