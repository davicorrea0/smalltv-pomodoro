> 🇧🇷 **Resumo em português:** se o relógio não liga nem entra no modo de recuperação (tela apagada, sem Wi-Fi), dá para regravar o firmware pelo USB-TTL com o `esptool`, sem apagar fotos nem configurações: ligue GND, TXD0→RX, RXD0→TX e GPIO0→GND (só na hora de ligar), faça uma cópia com `read_flash 0x0 0x400000` e grave com `write_flash 0x0 smalltv-pomodoro-vX.Y.Z.bin`. `erase_flash` só em último caso (apaga tudo). Sem adaptador USB-TTL há alternativas (placa NodeMCU/ESP32, Raspberry Pi, Arduino Uno) na seção 6. Detalhes abaixo, em inglês.

# Recovering a bricked SmallTV Ultra over USB-TTL

Use this when the clock shows nothing, never joins Wi-Fi and the [3-power-cycle recovery mode](../README.md#if-something-goes-wrong) does not open: the bootloader or the program area is damaged, so only a serial flash can bring it back.

**What happened in the one known case (v1.20.0):** the ESP8266 bootloader (eboot) reads the **last 4 bytes of the staged `.bin.gz`** as the image size. The 1.20.0 test build appended a 260-byte signature after the gzip, so that number became 256; eboot copied 256 bytes over sector 0 (itself) and erased the bootloader. Since 1.19.9 the clock refuses such files before the swap, `scripts/publicar.sh` checks every `.bin.gz` the way eboot reads it (`scripts/conferir-gz.cjs`), and signatures travel in separate `.sig` files (1.20.1).

Reflashing with `write_flash 0x0 <firmware>.bin` rewrites only the program area at the start of the 4 MB flash. The LittleFS storage (photos, settings, history, face packs) lives from 1 MB on and **is kept**.

## 1. What you need

- A **3.3 V** USB-TTL adapter (CP2102, CH340, FT232...). Never 5 V on the ESP8266 pins.
- Three or four jumper wires; a soldering iron helps (or spring probes / pin headers pressed against the pads).
- `esptool` (`pip install esptool`; the Arduino ESP8266 core also ships it) and a firmware `.bin` from the [releases](https://github.com/davicorrea0/smalltv-pomodoro/releases). Use the plain `.bin`, not the `.bin.gz`.

## 2. Wiring

Open the clock (2 screws underneath, slide out the tray). The ESP-12F module has labelled pads:

| Clock pad | USB-TTL adapter |
|---|---|
| GND | GND |
| TXD0 | RX |
| RXD0 | TX |
| GPIO0 | GND, **only while powering on** (flash mode) |
| 3V3 | 3.3 V (see below) |
| RST | optional: briefly to GND to reset |

Safest: keep powering the clock from its own USB cable and **do not connect the adapter's 3V3** (many adapters cannot supply Wi-Fi plus the screen). Then connect only GND, TX, RX and GPIO0. Set the adapter's jumper/switch to 3.3 V.

## 3. Enter flash mode

Power the clock off, connect GPIO0 to GND, power it on, then release GPIO0 (or leave it until the flash is done). Test:

```bash
esptool --port COM5 chip_id
```

It should report `ESP8266EX`. Replace `COM5` with your port (`/dev/ttyUSB0` on Linux; Device Manager → Ports on Windows). On older esptool the command is `esptool.py`.

## 4. Optional: back up the whole flash

```bash
esptool --port COM5 --baud 460800 read_flash 0x0 0x400000 backup-flash.bin
```

This keeps your photos and settings and shows what was on the chip. If it errors, drop `--baud 460800`.

## 5. Flash the firmware (keeps photos and settings)

Enter flash mode again (section 3), then:

```bash
esptool --port COM5 --baud 460800 write_flash 0x0 smalltv-pomodoro-vX.Y.Z.bin
```

Power off, remove the GPIO0 wire and power on normally. The version shows on the screen.

**Last resort only**, if it still does not boot: `esptool --port COM5 erase_flash`, then the `write_flash` above. This erases **everything** (Wi-Fi, photos, history); the clock comes back like new and opens its setup network.

## 6. No USB-TTL adapter?

Any board with a 3.3 V UART can do the same job. The wiring to the clock stays as in section 2 (their TX → clock RXD0, their RX → clock TXD0, common GND).

- **NodeMCU / ESP32 dev board as a USB-serial bridge:** hold its **EN/RST pin to GND** so its own chip stays in reset and the on-board USB chip just passes the serial lines through. Connect the board's TX/RX pins to the clock (TX → RXD0, RX → TXD0). Flash from the PC as usual.
- **Raspberry Pi:** use the hardware UART (`/dev/serial0`, GPIO14 TX / GPIO15 RX), which is **3.3 V**. Enable the serial port without a login console in `raspi-config`, then run `esptool --port /dev/serial0 ...`. Drive GPIO0/RST by hand with a wire or two spare GPIOs.
- **Arduino Uno:** put **RESET to GND** so the ATmega only forwards the USB-serial lines. The Uno is 5 V: **its TX must go through a voltage divider** (for example 1 kΩ in series, 2 kΩ to GND, taking the middle point) before the clock's RXD0. The clock's TX (3.3 V) can go straight to the Uno's RX. Power the clock from its own USB cable, not from the Uno.

## 7. If it still fails

- `Failed to connect`: GPIO0 was not low at power-on, TX/RX are swapped, or the ground is not shared. Retry flash mode; lower the speed (drop `--baud`).
- Garbled output or random resets: the adapter is sagging under load; power the clock from its own USB cable.
- Check the file before flashing: `sha256sum smalltv-pomodoro-vX.Y.Z.bin` against `SHA256SUMS.txt` in the release.
