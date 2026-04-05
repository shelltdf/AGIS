@echo off
REM Optional: generate sqlite3.c from full sqlite-src-* tree (not needed if you use sqlite-amalgamation-* zip).
REM Run from "x64 Native Tools Command Prompt for VS".

cd /d "%~dp0sqlite-src-3510300"
if not exist Makefile.msc (
  echo ERROR: sqlite-src-3510300 not found or missing Makefile.msc
  exit /b 1
)
nmake /f Makefile.msc sqlite3.c sqlite3.h shell.c
if errorlevel 1 exit /b 1
echo OK: amalgamation generated in %cd%
