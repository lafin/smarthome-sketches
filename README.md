platformio init -b esp12e --ide=vscode

platformio run --target upload --upload-port /dev/cu.wchusbserial1420
platformio run --target upload --upload-port 192.168.0.26