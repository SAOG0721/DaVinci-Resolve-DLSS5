# Installation / 安装与回退

Version 0.4.0 is an experimental validation build. The public package name does not remove its validation gates or establish full 0.4 acceptance. Do not copy the source ZIP into the OFX directory.

1. Download/extract `ResolveDLSS5-0.4.0-win64.zip` and compare its SHA-256 with the adjacent `.sha256.txt` file using `Get-FileHash`.
2. Obtain your authorized local `nvngx_dlssnr.dll` 310.8.0.0. Its SHA-256 must be `984BEE0F775C277D5829B8FD6775D53A7B0F75396C852B3AAF06A18375F81014`. The ZIP and installer contain no runtime download.
3. In the extracted `0.4.0` directory, preview:

```powershell
.\Install-Development.ps1 -Validation -RuntimeDll 'C:\path\to\nvngx_dlssnr.dll'
```

4. 关闭 Resolve，在管理员 PowerShell 中使用相同命令增加 `-Apply`。目标固定为 `C:\Program Files\Common Files\OFX\Plugins\ResolveDlss5.ofx.bundle`。安装前保存工程；脚本不操作 Resolve 数据库、缓存或应用目录。
5. 保留输出的 `receipt.json` 路径，重启 Resolve，在测试工程检查效果。正常不带 `-Validation` 的安装仍被阻止。

The installer checks the plugin hash and reports both the runtime identity and permitted target. It preserves the prior bundle under `%LOCALAPPDATA%\ResolveDlss5\Backups`. Failed replacements attempt to restore it. Inspect the preview before applying.

## Rollback / 回退

Close Resolve and use the receipt from this installation:

```powershell
.\Restore-Development.ps1 -Receipt 'C:\path\to\receipt.json'
# To restore after reviewing, run in administrator PowerShell with -Apply.
```

Restore checks the current plugin/runtime hashes, permitted backup path and target before replacing the bundle. It preserves the replaced version. A receipt for another installed binary cannot be used interchangeably. Do not edit receipts or bypass hash guards to force a rollback.

The original float/net-correction and HDR changes may alter 0.3.x appearances. Compare on a copy before using existing projects. See [manual checks](../tests/MANUAL-ACCEPTANCE.md) in the source tree, or `MANUAL-ACCEPTANCE.md` in the binary ZIP.
