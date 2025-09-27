# WiFi Disconnection Recovery Sequence

## Setup Steps

1. `idf.py -p COMXX erase-flash`

   Clear all your wifi configs

2. `idf.py build&flash monitor`

   Remember to monitor

## Recovery Sequence

1st startup -> BLE send first ssid&pwd, device will restart 
                                     |
                                    \|/
after restart and connected, turn off 1st wifi -> device will spend 5-10 secs sending attempt and restart
                                     |
                                    \|/
restart and 15-sec wait for next scan, you will use BLE to send 2nd ssid&pwd -> restart
                                     |
                                    \|/
finally the 2nd wifi is connected
