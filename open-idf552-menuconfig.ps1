$ErrorActionPreference = "Stop"

$env:IDF_TOOLS_PATH = "D:\Espressif\tools-v5.5.2"
. "D:\Espressif\frameworks\esp-idf-v5.5.2\export.ps1"

Set-Location $PSScriptRoot
idf.py menuconfig
