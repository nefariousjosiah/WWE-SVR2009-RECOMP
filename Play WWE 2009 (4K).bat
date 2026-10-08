@echo off
rem Plays WWE SmackDown vs. Raw 2009 rendered internally at 4K (3840x2160), downscaled to your
rem screen: the sharpest image, for stronger GPUs. "Play WWE 2009.bat" picks the resolution from
rem your display (1440p on 1080p/1440p screens). Extra arguments are passed to the game.
powershell -ExecutionPolicy Bypass -File "%~dp0tools\windows\run.ps1" --svr_render_scale=3 %*
