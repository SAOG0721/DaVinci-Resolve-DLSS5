# Resolve DLSS5 Experimental v0.3.1

这是一个 Windows x64 实验性补丁版本，修正 DaVinci Resolve/OpenFX 与
D3D12 纹理之间的垂直坐标方向。

## 本次修正

- 在上传与读回边界显式转换 OpenFX 的左下角原点和 D3D12 的左上角原点。
- 修正 DLSS Indicator 本应位于左下角、却显示在左上角的问题。
- 主体画面的最终方向保持不变。
- `DLSS.Indicator.Invert.Y.Axis` 保持为 `0`；坐标转换由图像边界代码统一处理。

## 下载与安装

1. 下载并完整解压 `Resolve-DLSS5-Experimental-x64.zip`。
2. 退出 DaVinci Resolve。
3. 将整个 `ResolveDlss5.ofx.bundle` 复制到
   `C:\Program Files\Common Files\OFX\Plugins\`。
4. 启动 Resolve，并从 `DLSS Experimental` 分类添加
   `DLSS Neural Video Experimental`。

不要将 DLL 移入 Resolve 安装目录，也不要替换系统或游戏中的 NGX DLL。

## 社区运行库

Release ZIP 包含独立的社区修改版 `nvngx_dlssnr.dll`：

- 文件版本：`310.8.0.0`
- SHA-256：`984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`

该 DLL 不适用本项目的 MIT 许可证，且不进入 Git 源码树或 GitHub 自动生成的
源码归档。请仅在确认拥有相应使用权限并理解实验风险时使用。

本 Release 对应 `v0.3.1-experimental` 标签。
