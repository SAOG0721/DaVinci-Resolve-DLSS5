# Resolve DLSS5 0.4.0 安装 / Installation

解压后包含 `ResolveDlss5.ofx.bundle`、`Install.ps1`、`Restore.ps1` 和本说明。社区修改版 `nvngx_dlssnr.dll` 310.8.0.0 位于插件的 `Contents\Win64\runtime` 目录。

## 安装

1. 关闭 DaVinci Resolve，在解压后的 `0.4.0` 目录打开管理员 PowerShell。
2. 使用随包运行库预览安装：

```powershell
.\Install.ps1 -Validation
```

3. 检查预览后增加 `-Apply` 安装：

```powershell
.\Install.ps1 -Validation -Apply
```

选择其他兼容的 DLSSNR 运行库时，增加 `-RuntimeDll 'C:\path\to\nvngx_dlssnr.dll'`。安装脚本使用所选文件并记录安装收据。

安装目录为 `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`。脚本备份原插件并输出 `receipt.json` 路径，请保留收据。重启 Resolve，从效果库添加 **DLSS Neural Video Experimental**。

## 回退

关闭 Resolve，在管理员 PowerShell 中使用本次安装收据预览，检查后增加 `-Apply`：

```powershell
.\Restore.ps1 -Receipt 'C:\path\to\receipt.json'
.\Restore.ps1 -Receipt 'C:\path\to\receipt.json' -Apply
```

## English

The plugin includes the community-modified `nvngx_dlssnr.dll` 310.8.0.0 in `Contents\Win64\runtime`. Close Resolve and open administrator PowerShell in the extracted `0.4.0` directory.

Preview with `Install.ps1 -Validation`, then add `-Apply` to install. To select another compatible DLSSNR runtime, add `-RuntimeDll <path>`.

The installer backs up the previous bundle and prints a `receipt.json` path. Keep the receipt and restart Resolve. To restore, close Resolve and preview with `Restore.ps1 -Receipt <receipt>`, then add `-Apply` in administrator PowerShell.

功能与参数用法 / Features and controls: [GitHub README](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5).
