@echo off
rem Plays WWE SmackDown vs. Raw 2009 with the native renderer (this folder's build).
rem Extra arguments are passed to the game, e.g. --fullscreen=true
powershell -ExecutionPolicy Bypass -File "%~dp0tools\windows\run.ps1" %*
