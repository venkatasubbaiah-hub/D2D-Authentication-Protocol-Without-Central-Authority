@echo off
setlocal
cd /d "%~dp0"
title Secure D2D Dual COM Dashboard

echo ============================================
echo       SECURE D2D DUAL COM DASHBOARD
echo ============================================
echo.

where python >nul 2>nul
if errorlevel 1 (
    echo Python was not found.
    echo Install Python 3 from https://www.python.org/downloads/
    echo IMPORTANT: select "Add Python to PATH" during installation.
    pause
    exit /b 1
)

echo Installing dashboard packages...
python -m pip install streamlit pyserial streamlit-autorefresh
if errorlevel 1 (
    echo.
    echo Package installation failed. Check your internet connection.
    pause
    exit /b 1
)

echo.
echo Starting dashboard at http://localhost:8501
python -m streamlit run app.py

echo.
echo Dashboard process ended.
pause
endlocal
