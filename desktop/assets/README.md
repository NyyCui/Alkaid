# 瑶光台图标

2026-11-05 重绘品牌图标：松绿圆角底板、暖白镂空星光与金色辅星。使用几何轮廓高倍采样后输出 PNG 和多尺寸 ICO。

- `alkaid.png`：高分辨率 RGBA 原图。
- `alkaid-256.png`：桌面界面与 CEF 应用图标。
- `alkaid.ico`：16、24、32、48、64、128、256 像素 Windows 图标。

`alkaid/scripts/embed-icon.py` 在构建时把 ICO 写入复制后的宿主启动程序；不会修改 third_party 内的原始 CEF bootstrap。
