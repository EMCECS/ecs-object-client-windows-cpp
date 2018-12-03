@echo off
if exist EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar del EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar
if exist EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar.gz del EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar.gz
7z.exe a -ttar EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar -r -x!*.user -x!*.aps -x!*.opendb -x!*.db -x!Debug -x!"Debug Lib" -x!"Debug Lib Static" -x!lint -x!objDebug -x!"objDebug Lib" -x!"objDebug Lib Static" -x!objRelease -x!"objRelease Lib" -x!"objRelease Lib Static" -x!Release -x!"Release Lib" -x!"Release Lib Static" -x!ziplib.bat -x!.git -x!.vs -x!.gitignore -x!*.zip -x!*.gz ..\ecs-object-client-windows-cpp
7z.exe a -tgzip EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar.gz  EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar
del EMCECS-ecs-object-client-windows-cpp-~VERSION~.tar