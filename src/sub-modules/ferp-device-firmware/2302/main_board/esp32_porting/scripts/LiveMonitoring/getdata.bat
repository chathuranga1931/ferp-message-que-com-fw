@Echo Off
set mypath=%cd%
set chromePath=C:\Program Files\Google\Chrome\Application
@echo %mypath%
"%chromePath%\chrome.exe" --user-data-dir="C://Chrome dev session" --disable-web-security --allow-file-access-from-files "file:///%mypath%/getdata.html"