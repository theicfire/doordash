# Light sleep, ESPNOW, and RTOS SDK Demo

The Arduino code in the main doordash repository uses the [NonOS SDK](https://github.com/espressif/ESP8266_NONOS_SDK). This is the deprecated version of their framework, but more importantly, it supports "forced light sleep" properly. It seems the NonOS SDK also does, but emperically forced light sleep won't wake up from a timer (only a pin change). https://blog.creations.de/?p=149 agrees that it doesn't work.

**Edit**: Actually it may be possible with some very careful steps to get the Arduino code working. See https://github.com/esp8266/Arduino/issues/7055#issuecomment-811918663, which is linked from [this great guide](https://kevinstadler.github.io/notes/esp8266-deep-sleep-light-sleep-arduino/). But I tried that demo code and my device would not wake up!

This example uses the [RTOS SDK](https://github.com/espressif/ESP8266_RTOS_SDK) to light sleep repeatedly, with support for waking up on a timer or a pin change. This results in lower power usage because waking up is faster than deep sleep. See the end of https://www.espressif.com/sites/default/files/9b-esp8266-low_power_solutions_en_0.pdf.

Light sleep also simplifies the circuit:
- We don't need to connect D0 and RST, because that's only needed for deep sleep
- We no longer need a capacitor or pull up resistor tied to the button, because we can find the wakeup reason purely in software (as shown in this demo).

This ESPNOW code is based on the [RTOS SDK example](https://github.com/espressif/ESP8266_RTOS_SDK/tree/master/examples/wifi/espnow).

This is half-finished, and just a demo about what is possible. I'd need to port over the Arduino code to make this useful.

# Setup and Programming
The RTOS SDK instructions are pretty straightforward. Here are the instructions copied over and simplified:
```
cd ~/esp
git clone https://github.com/espressif/ESP8266_RTOS_SDK.git
export IDF_PATH=~/esp/ESP8266_RTOS_SDK
export PATH="$PATH:/Users/YOUR_USERNAME/esp/bin" # They missed this in the instructions, huh.
```

Then the following are useful:
- `ESPPORT=/dev/cu.usbserial-21210 make -j4 flash`
- `ESPPORT=/dev/cu.usbserial-21210 make -j4 monitor`
- `ESPPORT=/dev/cu.usbserial-21210 make -j4 flash monitor # flash and monitor together`
- `make menuconfig` could be useful, though I haven't used it.

Apparently `make app-flash` is faster than `make flash`. They both seem somewhat slow to me.

# Power Graphs
## 20ms wake up
If the device only waits for 20ms each time it wakes up, it can get below 2mA average:
![image](https://github.com/theicfire/doordash/assets/442311/0e20264c-1a8e-4e3d-8374-d2f26be584b9)
![image](https://github.com/theicfire/doordash/assets/442311/47febca2-0d3a-45f0-ab11-897de686c06b)
Note how it takes over a second for the current to drop down to 1mA, **I have no idea why**:
![image](https://github.com/theicfire/doordash/assets/442311/4ee19b10-6c6e-4bec-9147-e7109e55ced0)

## 50ms wake up
A 50ms wakeup is 3mA average (in contrast to 4mA for deep sleep in the main directory):
![image](https://github.com/theicfire/doordash/assets/442311/3d3a7131-1ced-48ee-9fcb-329a0a8ac901)
