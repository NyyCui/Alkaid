import hashlib
import os
import stat
import sys
import tarfile
import urllib.parse
import urllib.request

def download_cef(version: str, platform: str, download_dir: str) -> None:
  """下载并解压 CEF 分发包。

  Args:
      platform: CEF 平台标识符（如 windows64、macosx64、linux64）
      version: CEF 版本号（如 "151.3.14+g5d67476+chromium-151.0.7922.72"）
      download_dir: 分发包下载和解压的目标目录
  """
  print(f"准备下载 CEF")
  print(f"平台：{platform}")
  print(f"版本：{version}")
  print(f"下载目录：{download_dir}")

  cef_distribution = f"cef_binary_{version}_{platform}"
  cef_root = os.path.join(download_dir, cef_distribution)

  if os.path.isdir(cef_root):
    print("CEF 已存在，跳过下载和解压")
    return

  os.makedirs(download_dir, exist_ok=True)

  cef_download_filename = f"{cef_distribution}.tar.bz2"
  cef_download_path = os.path.join(download_dir, cef_download_filename)

  if not os.path.exists(cef_download_path):
    # 构造下载 URL，对文件名中的特殊字符（如 "+"）进行 URL 编码
    encoded_filename = urllib.parse.quote(cef_download_filename, safe = "._-")
    cef_download_url = f"https://cef-builds.spotifycdn.com/{encoded_filename}"
    cef_download_url_escaped = cef_download_url

    # 第一步：下载 .sha1 校验文件
    sha1_url = f"{cef_download_url_escaped}.sha1"
    sha1_path = f"{cef_download_path}.sha1"
    print(f"从 {cef_download_url_escaped} 下载 {sha1_path}")
    urllib.request.urlretrieve(sha1_url, sha1_path)

    with open(sha1_path, "r") as f:
      expected_sha1 = f.read().strip()

    # 第二步：下载分发包，并使用 SHA1 校验确保文件完整性
    print(f"从 {cef_download_url_escaped} 下载 {cef_download_path}")
    _download_with_sha1_check(cef_download_url_escaped, cef_download_path, expected_sha1)
  else:
    print("分发包已存在，跳过下载")

  # 解压分发包到下载目录
  print(f"解压：{cef_download_path}")
  with tarfile.open(cef_download_path, "r:bz2") as tar:
    tar.extractall(path=download_dir, filter="data")

  # 设置只读权限
  _set_readonly(cef_root)

  print(f"CEF_ROOT={cef_root}")

def _download_with_sha1_check(url: str, dest: str, expected_sha1: str, chunk_size: int = 8192) -> None:
  """下载文件并校验 SHA1 完整性。"""
  sha1 = hashlib.sha1()
  tmp_path = f"{dest}.partial"

  with urllib.request.urlopen(url) as response, open(tmp_path, "wb") as out_file:
    total_size = response.getheader("Content-Length")
    downloaded = 0

    while True:
      chunk = response.read(chunk_size)
      if not chunk:
        break
      out_file.write(chunk)
      sha1.update(chunk)
      downloaded += len(chunk)

      if total_size:
        percent = downloaded * 100 / int(total_size)
        print(f"\r下载进度：{percent:.1f}%", end = "", flush = True)

    if total_size:
      print()  # 换行

  actual_sha1 = sha1.hexdigest()
  if actual_sha1 != expected_sha1:
    os.remove(tmp_path)
    raise RuntimeError(f"SHA1 校验失败！期望：{expected_sha1}，实际：{actual_sha1}")

  os.rename(tmp_path, dest)

def _set_readonly(path: str) -> None:
  """递归设置目录为只读权限。"""
  for root, dirs, files in os.walk(path):
    for d in dirs:
      os.chmod(os.path.join(root, d), stat.S_IRUSR | stat.S_IXUSR)
    for f in files:
      os.chmod(os.path.join(root, f), stat.S_IRUSR)

if __name__ == "__main__":
  if len(sys.argv) != 4:
    print(f"用法：python {sys.argv[ 0 ]} <platform> <version> <download_dir>")
    print(f"示例：python {sys.argv[ 0 ]} windows64 151.3.14+g5d67476+chromium-151.0.7922.72 cef")
    sys.exit(1)

  download_cef(sys.argv[ 1 ], sys.argv[ 2 ], sys.argv[ 3 ])
