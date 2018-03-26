@echo off
copy /y ziplibtemplate.bat ziplibtemplatetemp.bat
bin\setversion.exe /getversion "#define ECSUTIL_VERSION" ECSUtil\Version.h /setversionfile ~VERSION~ ziplibtemplatetemp.bat
call ziplibtemplatetemp.bat
del ziplibtemplatetemp.bat
