@echo off
echo test\%1 1>&2
powershell Start-Sleep -m 500 2>nul
type test\%1
