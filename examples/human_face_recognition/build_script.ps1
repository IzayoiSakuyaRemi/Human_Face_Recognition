$env:IDF_TARGET = 'esp32p4'
$env:SDKCONFIG_DEFAULTS = 'sdkconfig.bsp.esp32_p4_function_ev_board'
$env:IDF_EXTRA_ACTIONS_PATH = 'D:\works\esp-who-master\tools'

Set-Location D:\works\esp-who-master\examples\human_face_recognition

# Remove MSYS env vars that idf.py uses to detect MinGW
Remove-Item Env:\MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:\TERM -ErrorAction SilentlyContinue

# Activate ESP-IDF Python venv
$venv = 'C:\Espressif\tools\python\v5.5.4\venv\Scripts\Activate.ps1'
. $venv

# Set required env vars
$env:IDF_TARGET = 'esp32p4'
$env:SDKCONFIG_DEFAULTS = 'sdkconfig.bsp.esp32_p4_function_ev_board'
$env:IDF_EXTRA_ACTIONS_PATH = 'D:\works\esp-who-master\tools'
$env:IDF_PATH = 'D:\Espressif\espidf\.espressif\v5.5.4\esp-idf'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v5.5.4\venv'

# Clean first
if (Test-Path .\build) {
    Remove-Item -Recurse -Force .\build -ErrorAction SilentlyContinue
    Write-Host "Build directory cleaned"
}

# Build
python $env:IDF_PATH\tools\idf.py build
exit $LASTEXITCODE
