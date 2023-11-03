Houses with many people have a problem: when the doorbell rings, who's going to get it? If someone decides to get the door, how do they let someone else know that they're getting the door?

This project is a solution. It consists of a set of battery powered microcontrollers and led-integrated buttons. If a button is pressed, the LEDs on all the other buttons are lit up to indicate that someone has pressed the button. After, say, 20 seconds, the system resets and waits for a new button press.

This has a respectable battery life, because it largely sits in deep sleep. There's one coordinator that determines the winner (first message to get to the coordinator wins). This allows for meshing such that nodes can talk through each other to reach the coordinator.

# Low Power Calculation
- When on with ESP NOW running, the chip takes ~70mA
- We turn on the device for ~50ms every 2s
- During deep sleep, the device takes <1mA
- So, (50 / 2000) * 70 + (1950 / 2000) * 1 = 2.725mA average
- 3200mAh battery / 2.725 = 1174.31 hours = ~49 days

These calculations are somewhat rough. I need to get a good current meter and/or oscilliscope to get a better guess.

# Materials
- WeMos D1 Mini Pro V3.0 (Need one of these newer versions because they use less current during sleep)
- TP4056 Li-ion charger breakout board
- Some resistors a capacitor
- 1x button + LED
- 18650 battery and holder (We used an [EVE INR18650-33V](https://www.18650batterystore.com/products/eve-18650-33v))
- A micro-usb cable that will get cut ([like this](https://www.amazon.com/gp/product/B0BZ8XWL18/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1))

# Schematic
![Schematic](assets/schematic.png)

# Wiring Diagram
![Wiring Diagram](assets/wiring_diagram.png)

# State Machine
![State Machine](assets/state_machine.png)

# Programming steps

- Install platformio
- Connect one of the microcontrollers via USB
- Run `platformio run -t upload` (This automatically installs FastLED).
