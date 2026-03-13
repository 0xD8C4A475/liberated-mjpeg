@echo off
setlocal

set SCRIPT_DIR=%~dp0
set PROJECT_DIR=%SCRIPT_DIR%..
set TESTDATA=%PROJECT_DIR%\testdata

:: Try to find FFmpeg
if defined FFMPEG goto :found
where ffmpeg >nul 2>&1 && set FFMPEG=ffmpeg && goto :found
if exist "C:\Program Files\ShareX\ffmpeg.exe" set FFMPEG="C:\Program Files\ShareX\ffmpeg.exe" && goto :found
if exist "C:\ffmpeg\bin\ffmpeg.exe" set FFMPEG="C:\ffmpeg\bin\ffmpeg.exe" && goto :found
echo ERROR: FFmpeg not found. Set FFMPEG environment variable or add ffmpeg to PATH.
exit /b 1

:found
echo Using FFmpeg: %FFMPEG%

if not exist "%TESTDATA%" mkdir "%TESTDATA%"

echo === Generating test content ===

echo [1/4] 1080p MJPEG AVI (10s, 30fps, quality 2)...
%FFMPEG% -y -f lavfi -i testsrc=duration=10:size=1920x1080:rate=30 -c:v mjpeg -q:v 2 "%TESTDATA%\test_1080p_q2.avi"

echo [2/4] 720p MJPEG AVI (10s, 30fps, quality 2)...
%FFMPEG% -y -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 -c:v mjpeg -q:v 2 "%TESTDATA%\test_720p_q2.avi"

echo [3/4] 480p MJPEG AVI (5s, 30fps, quality 5)...
%FFMPEG% -y -f lavfi -i testsrc=duration=5:size=640x480:rate=30 -c:v mjpeg -q:v 5 "%TESTDATA%\test_480p_q5.avi"

echo [4/4] Reference 1080p JPEG frame...
%FFMPEG% -y -f lavfi -i testsrc=size=1920x1080 -frames:v 1 -q:v 2 "%TESTDATA%\reference_frame.jpg"

echo.
echo === Done! Generated files: ===
dir "%TESTDATA%"
