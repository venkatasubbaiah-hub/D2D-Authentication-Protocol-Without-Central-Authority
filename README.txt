SECURE D2D DUAL-COM DASHBOARD - CORRECTED PACKAGE

FILES
- app.py
- requirements.txt
- RUN_DASHBOARD.bat
- README.txt

INSTALL AND RUN
1. Extract the ZIP into a normal Windows folder.
2. Install Python 3 if it is not already installed:
   https://www.python.org/downloads/
   During installation, enable "Add Python to PATH".
3. Close Arduino Serial Monitor, PuTTY, and other programs using COM7 or COM9.
4. Double-click RUN_DASHBOARD.bat.
5. The batch file installs the required Python packages.
6. Open http://localhost:8501 in Chrome if it does not open automatically.
7. Select ESP32 COM7 and STM32 COM9, baud rate 115200.
8. Click Connect BOTH boards.

HARDWARE / SERIAL NOTES
- COM7 is the ESP32 USB Serial connection.
- COM9 is normally the NUCLEO-F446RE ST-LINK Virtual COM Port.
- On the NUCLEO-F446RE, ST-LINK VCP is normally connected to USART2 PA2/PA3.
  STM32 firmware must transmit debug messages through that UART for COM9 to show them.
- The board-to-board link is STM32 USART1 PB6/PB7 to ESP32 UART2 GPIO32/GPIO33.
- Connect common GND between STM32 and ESP32.
- The dashboard is a serial monitor/transmitter. It does not calculate HMAC.
  Matching firmware on both boards is required for mutual authentication.

TROUBLESHOOTING
- Access denied / port busy: close all other serial monitor applications.
- COM port missing: check Windows Device Manager and select the detected port.
- No STM32 console output: verify STM32 USART2 PA2/PA3 debug transmission.
- No ESP32 console output: verify ESP32 prints relevant messages to USB Serial.
