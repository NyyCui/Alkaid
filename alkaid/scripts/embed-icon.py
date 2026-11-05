"""将项目 ICO 写入复制后的宿主启动程序，保持多尺寸任务栏及资源管理器图标一致。"""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('icon', type=Path)
args = parser.parse_args()
data = args.icon.read_bytes()
reserved, kind, count = struct.unpack_from('<HHH', data)
if reserved != 0 or kind != 1 or not 1 <= count <= 32:
    raise ValueError('ICO 文件格式无效')
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
kernel.BeginUpdateResourceW.restype = wintypes.HANDLE
kernel.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                  wintypes.WORD, ctypes.c_void_p, wintypes.DWORD]
kernel.UpdateResourceW.restype = wintypes.BOOL
kernel.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
kernel.EndUpdateResourceW.restype = wintypes.BOOL
handle = kernel.BeginUpdateResourceW(str(args.executable.resolve()), False)
if not handle:
    raise ctypes.WinError(ctypes.get_last_error())
try:
    group = bytearray(struct.pack('<HHH', 0, 1, count))
    for index in range(count):
        width, height, colors, zero, planes, bits, size, offset = struct.unpack_from('<BBBBHHII', data, 6 + index * 16)
        image = data[offset:offset + size]
        if len(image) != size:
            raise ValueError('ICO 图像数据不完整')
        resource_id = 200 + index
        buffer = ctypes.create_string_buffer(image)
        if not kernel.UpdateResourceW(handle, 3, resource_id, 0, buffer, size):
            raise ctypes.WinError(ctypes.get_last_error())
        group.extend(struct.pack('<BBBBHHIH', width, height, colors, zero, planes, bits, size, resource_id))
    buffer = ctypes.create_string_buffer(bytes(group))
    if not kernel.UpdateResourceW(handle, 14, 1, 0, buffer, len(group)):
        raise ctypes.WinError(ctypes.get_last_error())
except BaseException:
    kernel.EndUpdateResourceW(handle, True)
    raise
if not kernel.EndUpdateResourceW(handle, False):
    raise ctypes.WinError(ctypes.get_last_error())
print('宿主多尺寸图标已嵌入')
