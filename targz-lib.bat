@echo off
copy /y targz-libtemplate.bat targz-libtemplatetemp.bat
bin\setversion.exe /getversion "#define ECSUTIL_VERSION" ECSUtil\Version.h /setversionfile ~VERSION~ targz-libtemplatetemp.bat
call targz-libtemplatetemp.bat
del targz-libtemplatetemp.bat
