@echo off
chcp 65001 >nul
setlocal

:: 切换到当前批处理所在的目录，防止路径空格问题
cd /d "%~dp0"

set "SRC=mobp.c"
set "OUT=mobp.exe"
set "RC_FILE=mobp.rc"
set "RES_FILE=mobp.res"

set "GCC=D:\software\msys64\ucrt64\bin\gcc.exe"
set "WINDRES=D:\software\msys64\ucrt64\bin\windres.exe"

:: 专属配置：带 -mwindows 隐藏黑框，带资源文件
set "FLAGS=-mwindows -fdiagnostics-color=always -g"
set "LIBS=-luser32 -lshell32"

echo 正在编译 mobp 资源文件...
if exist "%RC_FILE%" (
    "%WINDRES%" "%RC_FILE%" -O coff -o "%RES_FILE%"
    if errorlevel 1 (
        echo 资源编译失败！
        pause
        exit /b 1
    )
) else (
    echo 未找到 mobp.rc，跳过资源编译。
)

echo 正在编译 C 代码...
"%GCC%" %FLAGS% "%SRC%" "%RES_FILE%" -o "%OUT%" %LIBS%

if errorlevel 1 (
    echo 编译失败！
    pause
    exit /b 1
)

echo 编译成功: %OUT%
:: 如果双击运行这个 bat，可以让窗口停一下，方便看输出
if "%~1"=="" pause
exit /b 0