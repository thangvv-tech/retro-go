# Multi-Target Wireless Gamepad for Retro-Go (ESP-NOW)

Standalone, ultra-low-latency wireless gamepad firmware for Retro-Go. Communicates directly with the console using **ESP-NOW 2.4GHz P2P** action frames — no Bluetooth stack overhead, no WiFi router needed, zero-configuration pairing.

Supports multiple hardware targets via standard ESP-IDF:
- **ESP32-C3 SuperMini** (Default / Compact form factor)
- **ESP32-S3** (Custom PCB / DevKit)
- **ESP32 Classic** (NodeMCU / WROOM)

---

## 1. Key Features & Optimizations

- **Sub-3ms Ultra-Low Latency**: Direct raw MAC-layer 802.11 action frames via ESP-NOW. Bypasses Bluetooth stack and pairing negotiation.
- **Single-Cycle Hardware Register Polling**:
  - Samples all GPIOs in a single CPU clock cycle (`REG_READ(GPIO_IN_REG)`).
  - ~6.25ns at 160MHz vs tens of microseconds of loop `gpio_get_level()` overhead.
- **Asymmetric Debounce Filter**:
  - **Press down**: 0ms instant trigger (`newly_pressed = current & ~debounced`). Zero perceptible button lag.
  - **Release**: 2-sample consecutive zero filter (`candidate_release & ~last_sample`). Eliminates contact bounce.
- **High-Speed 24Mbps PHY Rate**:
  - Configured with `esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_24M)`.
  - Packet airtime is ~40μs (7.5x faster than default 1Mbps CCK rate).
- **60Hz Display-Matched Heartbeat (16ms)**:
  - Transmits immediate packet on button state transition.
  - Periodic 16ms heartbeat matching 60fps refresh rate provides instant recovery if an RF burst is dropped.
- **Fixed WiFi Channel Lock**:
  - Locked to **Channel 1** (`CONFIG_GAMEPAD_WIFI_CHANNEL`). Zero channel scanning delay on boot.
- **Maximum RF Output (19.5 dBm)**:
  - Configured with `esp_wifi_set_max_tx_power(78)` for maximum penetration through hands and enclosures.
- **Multi-Player Role Persistence (NVS) & Boot LED Feedback**:
  - Hold **Button A** at power-on $\rightarrow$ Saved as **Player 1**.
  - Hold **Button B** at power-on $\rightarrow$ Saved as **Player 2**.
  - Onboard status LED blinks **1x for P1**, **2x for P2** at boot, then reconfigures as normal button input.
- **Automatic Light-Sleep Power Saving**:
  - Enters light sleep (~1mA current) after 60s idle (`CONFIG_GAMEPAD_SLEEP_TIMEOUT_MS`).
  - All button pins are wake-up sources. Any button press wakes chip in <3ms and transmits immediately.
- **Dedicated Hardware Buttons (No Combos)**:
  - All 13 buttons (including MENU) have dedicated pins. No `SELECT + START` combos required on the wireless pad.

---

## 2. Supported Targets & Pinout

Button pinouts are abstracted in `main/boards.h` and selected automatically when setting the ESP-IDF target. All buttons connect between the designated GPIO pin and **GND** (Active LOW with internal pull-up).

### Pin Mapping by Target

| Gamepad Button | Retro-Go Key | ESP32-C3 SuperMini | ESP32-S3 | ESP32 Classic |
| :--- | :--- | :---: | :---: | :---: |
| **D-Pad UP** | `RG_KEY_UP` | GPIO 0 | GPIO 1 | GPIO 13 |
| **D-Pad DOWN** | `RG_KEY_DOWN` | GPIO 1 | GPIO 2 | GPIO 12 |
| **D-Pad LEFT** | `RG_KEY_LEFT` | GPIO 2 | GPIO 3 | GPIO 14 |
| **D-Pad RIGHT** | `RG_KEY_RIGHT` | GPIO 3 | GPIO 4 | GPIO 27 |
| **Button A** | `RG_KEY_A` | GPIO 4 | GPIO 5 | GPIO 26 |
| **Button B** | `RG_KEY_B` | GPIO 5 | GPIO 6 | GPIO 25 |
| **Button X** | `RG_KEY_X` | GPIO 6 | GPIO 7 | GPIO 33 |
| **Button Y** | `RG_KEY_Y` | GPIO 7 | GPIO 8 | GPIO 32 |
| **Button L** | `RG_KEY_L` | GPIO 8 *(LED multiplexed)* | GPIO 9 | GPIO 18 |
| **Button R** | `RG_KEY_R` | GPIO 10 | GPIO 10 | GPIO 19 |
| **SELECT** | `RG_KEY_SELECT` | GPIO 20 | GPIO 11 | GPIO 22 |
| **START** | `RG_KEY_START` | GPIO 21 | GPIO 12 | GPIO 23 |
| **MENU** | `RG_KEY_MENU` | GPIO 9 | GPIO 13 | GPIO 21 |
| **Status LED** | — | GPIO 8 | GPIO 21 | GPIO 2 |

> **Note on ESP32-C3 Pin 8**:
> GPIO 8 is connected to the onboard blue LED (Active LOW). At boot, it flashes to indicate Player 1 (1x) or Player 2 (2x), then switches to input pullup mode for Button L.

---

## 3. Wiring Schematic (ESP32-C3 SuperMini Example)

```
        ESP32-C3 SuperMini
       ┌──────────────────┐
       │              GND ├────────────────────────────┬───────────────┬────── ... (Common GND)
       │                  │                            │               │
       │              IO0 ├───[ Tactile D-Pad UP ]─────┘               │
       │              IO1 ├───[ Tactile D-Pad DOWN ]───────────────────┤
       │              IO2 ├───[ Tactile D-Pad LEFT ]                   │
       │              IO3 ├───[ Tactile D-Pad RIGHT ]                  │
       │              IO4 ├───[ Tactile Button A ]                     │
       │              IO5 ├───[ Tactile Button B ]                     │
       │              IO6 ├───[ Tactile Button X ]                     │
       │              IO7 ├───[ Tactile Button Y ]                     │
       │              IO8 ├───[ Tactile Button L ]                     │
       │             IO10 ├───[ Tactile Button R ]                     │
       │             IO20 ├───[ Tactile SELECT ]                       │
       │             IO21 ├───[ Tactile START ]                        │
       │              IO9 ├───[ Tactile MENU ]─────────────────────────┘
       └──────────────────┘
```

---

## 4. Power & Battery

1. **1S LiPo / Li-ion Battery (3.7V)**:
   - Positive (+) to **5V** / **VBUS** pin.
   - Negative (-) to **GND**.
   - Capacity: 300mAh – 800mAh provides 5–10 hours of active gameplay.
2. **Charging**:
   - Add a TP4056 or TP4054 Micro/Type-C charging board to charge the battery safely.
3. **Power Draw**:
   - Active transmitting: ~60–80mA.
   - Idle light-sleep: ~1mA (>300 hours standby).

---

## 5. Multi-Player Setup (P1 & P2)

Two independent wireless gamepads can connect simultaneously to a single Retro-Go console:

- **Configure Player 1**: Hold **Button A** while powering on. Status LED flashes **1 time**.
- **Configure Player 2**: Hold **Button B** while powering on. Status LED flashes **2 times**.
- Configuration is saved in non-volatile flash (NVS). Powering on normally keeps the saved role.

Console input routing:
- **NES**: Player 1 $\rightarrow$ Port 0, Player 2 $\rightarrow$ Port 1.
- **SNES**: Player 1 $\rightarrow$ Port 0, Player 2 $\rightarrow$ Port 1.
- **Genesis / Mega Drive**: Player 1 $\rightarrow$ Pad 0, Player 2 $\rightarrow$ Pad 1.
- **SMS / PC Engine**: Both player ports supported.
- **Launcher & Single Player Games**: Either controller controls the UI seamlessly.

---

## 6. Packet Structure & Multi-Console Anti-Conflict

Transmitted as an ESP-NOW broadcast frame to MAC `FF:FF:FF:FF:FF:FF`:

```c
typedef struct __attribute__((packed)) {
    uint16_t magic;      // Magic identifier 0x4752 ('R', 'G' - Retro-Go) (2 bytes)
    uint16_t system_id;  // System/Console ID: 0 = accept all, 1-65535 = dedicated room/console (2 bytes)
    uint16_t buttons;    // Bitmask matching Retro-Go rg_key_t (2 bytes)
    uint8_t  seq;        // Monotonically increasing sequence number (1 byte)
    uint8_t  player_id;  // 0 = Player 1, 1 = Player 2 (1 byte)
} gamepad_packet_t;
```

### Anti-Conflict Protection (3+ Gamepads or Multiple Consoles in Range)
1. **Magic Header Filter (`0x4752`)**: Retro-Go discards any packets without the Retro-Go magic identifier, eliminating foreign WiFi/IoT interference.
2. **Multi-Console Isolation (`system_id` + RF Channel)**:
   - To isolate 2 consoles in the same room, set Console 1 & its gamepads to `system_id = 1` (Channel 1), and Console 2 & its gamepads to `system_id = 2` (Channel 6).
   - Tần số vô tuyến và logic lọc độc lập 100%, không bị nhận chéo lệnh.
3. **Dynamic MAC Binding (First-Come, First-Served)**:
   - When Player 1 powers on, Retro-Go binds the Player 1 slot to that controller's unique 6-byte MAC address.
   - If a 3rd controller transmitting as Player 1 appears, Retro-Go rejects its packets while the active controller is communicating.
   - When the active controller is turned off or idle for >1.5s, the slot automatically releases and binds to the next active controller.
```

---

## 7. Build & Flash Instructions

Standard ESP-IDF build system (ESP-IDF v4.4 or v5.x).

### Step 1: Set Target Chip

```bash
# For ESP32-C3 SuperMini (default)
idf.py set-target esp32c3

# Or for ESP32-S3
idf.py set-target esp32s3

# Or for ESP32 Classic
idf.py set-target esp32
```

### Step 2: Compile Firmware

```bash
idf.py build
```

The build generates:
- Standard binaries in `build/`
- All-in-one merged image: `build/gamepad-${IDF_TARGET}.img` and `build/gamepad.img`

### Step 3: Flash (Single-File at Offset 0x0)

```bash
# Using idf.py
idf.py -p /dev/ttyUSB0 flash monitor

# Or using esptool.py directly with the merged binary:
esptool.py --chip esp32c3 -p /dev/ttyUSB0 -b 460800 write_flash 0x0 build/gamepad.img
```

### Docker Build (Alternative)

```bash
# Build for ESP32-C3 using official ESP-IDF Docker
docker run --rm -v $(pwd)/../../:/project -w /project/tools/gamepad espressif/idf:release-v4.4 idf.py set-target esp32c3
docker run --rm -v $(pwd)/../../:/project -w /project/tools/gamepad espressif/idf:release-v4.4 idf.py build
```
