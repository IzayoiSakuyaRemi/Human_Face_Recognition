param(
    [switch]$e
)

# 1. 确保 xtensa 工具链的副本存在（xtensa-esp-elf-* -> xtensa-esp32s3-elf-*）
$toolchainBin = "D:\Espressif\.espressif\tools\xtensa-esp-elf\bin"
if (Test-Path "$toolchainBin\xtensa-esp-elf-gcc.exe") {
    Get-ChildItem "$toolchainBin\xtensa-esp-elf-*.exe" | ForEach-Object {
        $newName = $_.Name -replace 'xtensa-esp-elf-', 'xtensa-esp32s3-elf-'
        if (-not (Test-Path "$($_.Directory)\$newName")) {
            Copy-Item $_.FullName "$($_.Directory)\$newName"
        }
    }
}

# 2. 加载官方 IDF 环境
. "C:\Espressif\tools\Microsoft.v5.5.4.PowerShell_profile.ps1" 2>$null

# 3. 补充 xtensa-esp-elf 工具链路径
$env:PATH = "D:\Espressif\.espressif\tools\xtensa-esp-elf\bin;$env:PATH"

# 4. 补充 ESP_ROM_ELF_DIR
$env:ESP_ROM_ELF_DIR = "D:\Espressif\.espressif\tools\esp-rom-elfs\20241011\"

# 5. 如果传入 -e 则打印环境变量
if ($e) {
    Write-Host "IDF_PATH=$env:IDF_PATH"
    Write-Host "IDF_TOOLS_PATH=$env:IDF_TOOLS_PATH"
    Write-Host "IDF_PYTHON_ENV_PATH=$env:IDF_PYTHON_ENV_PATH"
    Write-Host "ESP_ROM_ELF_DIR=$env:ESP_ROM_ELF_DIR"
    return
}

Write-Host ""
Write-Host "ESP-IDF 环境已就绪！" -ForegroundColor Green
Write-Host "  编译: idf.py build" -ForegroundColor Cyan
Write-Host "  烧录: idf.py -p COM38 flash" -ForegroundColor Cyan
Write-Host "  监视: idf.py -p COM38 monitor" -ForegroundColor Cyan
Write-Host ""
