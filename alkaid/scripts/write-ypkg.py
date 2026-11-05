"""开发侧 ZIP 写入器；输入只来自打包工具，运行时使用独立 C++ 校验器。"""
import base64
import json
import sys
import zipfile
from pathlib import Path

entries = json.load(sys.stdin)
output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for entry in entries:
        info = zipfile.ZipInfo(entry['name'], (2026, 1, 1, 0, 0, 0))
        info.create_system = 3
        info.external_attr = entry.get('attributes', 0o100644 << 16)
        info.compress_type = entry.get('compression', zipfile.ZIP_DEFLATED)
        archive.writestr(info, base64.b64decode(entry['data']))
