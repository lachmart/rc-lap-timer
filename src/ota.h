// Firmware upload over Wi-Fi (ArduinoOTA, port 3232): pio run -e ota -t upload
#pragma once

void otaBegin();      // after wlanBegin()
void otaLoop();
