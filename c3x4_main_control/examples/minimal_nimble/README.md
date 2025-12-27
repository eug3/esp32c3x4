Minimal NimBLE repro for ESP32-C3 (controller + HCI + nimble only)

Purpose:
- Isolate Bluetooth controller / esp-nimble HCI / NimBLE host initialization order on ESP32-C3.

How to run:
1. Open a terminal and source your ESP-IDF environment (adjust path if needed):

```bash
source ~/esp/esp-idf/export.sh
```

2. Change to this example directory:

```bash
cd /Users/beijihu/Github/esp32c3x4/c3x4_main_control/examples/minimal_nimble
```

3. Build and flash (specify your serial port):

```bash
idf.py -p /dev/cu.usbmodem2101 flash monitor
```

What to look for in the monitor output:
- `esp_nimble_hci_init returned: 0` then `nimble_port_init returned: 0` indicates success.
- Any `ESP_FAIL` from `esp_nimble_hci_init` or `nimble_port_init` indicates an ordering/resource issue.

If it fails here but succeeds when Wi-Fi is not started in your main project, that confirms a coexistence/ordering problem.

Notes:
- This example intentionally omits Wi-Fi and all peripherals to reduce interference.
- If you want, run this example first to verify BLE init works on bare device, then reintroduce Wi-Fi in your main app.
