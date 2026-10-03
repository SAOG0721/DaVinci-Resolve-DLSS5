# Resolve DLSS5 0.4.0 安装 / Installation

解压后包含 `ResolveDlss5.ofx.bundle`、`Install.ps1`、`Restore.ps1` 和本说明。

## 安装

1. 准备获授权的 `nvngx_dlssnr.dll` 310.8.0.0，SHA256：

   `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`
2. 关闭 DaVinci Resolve，在解压后的 `0.4.0` 目录打开管理员 PowerShell。
3. 指定运行库路径进行预览：

```powershell
.\Install.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll'
```

4. 检查预览后增加 `-Apply` 安装：

```powershell
.\Install.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll' -Apply
```

已有运行库位于原插件的 `Contents\Win64\runtime` 时，可省略 `-RuntimeDll`。

安装目录为 `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`。脚本备份原插件并输出 `receipt.json` 路径，请保留收据。重启 Resolve，从效果库添加 **DLSS Neural Video Experimental**。

## 回退

关闭 Resolve，在管理员 PowerShell 中使用本次安装收据预览，检查后增加 `-Apply`：

```powershell
.\Restore.ps1 -Receipt 'C:\path\to\receipt.json'
.\Restore.ps1 -Receipt 'C:\path\to\receipt.json' -Apply
```

## English

Extract the ZIP, prepare your authorized `nvngx_dlssnr.dll` 310.8.0.0 with the SHA-256 above, and close Resolve. Open administrator PowerShell in the extracted `0.4.0` directory.

Preview with `Install.ps1 -Validation -RuntimeDll <path>`, then add `-Apply` to install. If the runtime is already in the previous plugin's `Contents\Win64\runtime` folder, omit `-RuntimeDll`.

The installer backs up the previous bundle and prints a `receipt.json` path. Keep the receipt and restart Resolve. To restore, close Resolve and preview with `Restore.ps1 -Receipt <receipt>`, then add `-Apply` in administrator PowerShell.

功能与参数用法 / Features and controls: [GitHub README](https://github.com/SAOG0721/DaVinci-Resolve-DLSS5).
