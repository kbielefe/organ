# USB MIDI Organ Key Reader Conversion Plan

## Overview
This plan converts the existing Arduino-based organ key reader (reading two manuals, pedalboard via shift registers over SPI, and volume pedal via analog) from serial MIDI output (requiring Hairless MIDI Serial bridge) to native USB MIDI. The new setup will enumerate as a standard USB MIDI device on Linux (PipeWire/JACK), compatible with GrandOrgue and similar virtual organ software. No host-side shims or drivers needed.

**Goals:**
- Plug-and-play USB MIDI detection.
- Preserve exact key/note mappings, channels, and behavior.
- Low maintenance: Use stable libraries, minimal custom code.
- Bus-powered, no changes to custom PCB wiring (SPI on pins 10,11,12,13; analog A5).
- Target: Linux host with PipeWire; test with GrandOrgue.

**Assumptions/Constraints:**
- Hardware replacement required (Uno R2 lacks USB MIDI MCU).
- No input-side changes: Keep shift register chain, wiring, debounce logic.
- Output: Pure MIDI (Note On/Off, CC for volume); no serial fallback.
- Dev env: Linux (Arduino IDE 2.x+).
- Budget: <$30 for board.
- Total effort: 4-6 hours post-hardware arrival.

**Recommended Hardware:**
Based on research (MIDIUSB library compatibility, pinout similarity to Uno, availability):
- **Primary: Arduino Leonardo (original or clone)** (~$15-25 USD).
  - MCU: ATmega32u4 (built-in USB HID/MIDI support).
  - Pins: Matches Uno for SPI (13=CLK, 12=MISO, 11=MOSI, 10=SS/Load).
  - Analog: A5 available.
  - Power: Bus-powered USB.
  - Why: Simplest drop-in; MIDIUSB library works out-of-box. Widely available (Adafruit, SparkFun, Amazon). Clones like Pro Micro are cheaper (~$5) but verify pinout (e.g., SparkFun Pro Micro).
- **Alternative: Arduino Micro** (official, ~$20) – Same as Leonardo but smaller form factor.
- **Avoid:** SAMD boards (e.g., Zero) unless needed for speed; more complex MIDIUSB setup.
- **Order from:** Adafruit/SparkFun for reliability, or Amazon for speed. Include male headers if PCB needs soldering.
- **Arrival time:** 2-7 days; test with breadboard first if possible.

If hardware differs (e.g., custom enclosure), verify pin compatibility: Load pin on D10, SPI defaults, A5 for volume.

## Incremental Implementation Steps
Follow these steps sequentially. Each includes verification. Use Arduino IDE for uploads; test with `aconnect -l` (Linux) to see MIDI ports, and `aseqdump` or GrandOrgue for message validation.

### Phase 1: Preparation (1 hour, pre-hardware)
1. **Install Tools/Libraries**
   - Download Arduino IDE 2.x (if not installed): `sudo apt install arduino` or from arduino.cc.
   - Add Boards: File > Preferences > Additional Boards Manager URLs: `https://downloads.arduino.cc/packages/package_index.json`.
   - Install "Arduino AVR Boards" and "Leonardo" support via Boards Manager.
   - Install MIDIUSB Library: Tools > Manage Libraries > Search "MIDIUSB" (by Arduino LLC, v1.0.3+). This handles USB MIDI protocol.
   - Backup original: Copy `key_reader.ino` to `key_reader_original.ino`.

2. **Verify Current Behavior (Spec Validation)**
   - Flash original code to Uno R2.
   - Use Hairless (if functional) or `cat /dev/ttyUSB0 | hexdump -C` to capture serial output at 115200 baud.
   - Press keys/pedal/volume: Confirm Note Off (0x8X  note 00) on press?, Note On (0x9X note 7F) on release?, CC 0xB4 07 vol on volume change.
   - Note: Logic appears inverted (press sends Note Off vel0, release Note On vel127) – preserve as-is unless GrandOrgue expects standard (we'll confirm in testing).
   - Map keys: Use Spec.md for pitches/channels; physically label or diagram PCB wiring.

3. **Procure Hardware**
   - Order: Arduino Leonardo (or Pro Micro clone).
   - While waiting: Prototype MIDI output logic in simulation (e.g., Serial for debug) or on existing Uno (output to serial, bridge via software).

### Phase 2: Hardware Integration (0.5 hours, post-arrival)
1. **Wiring/Assembly**
   - Disconnect Uno R2 from PCB.
   - Connect Leonardo: 
     - Vcc/Gnd: To PCB power rails.
     - D10 -> Load pin (shift register latch).
     - D11 (MOSI), D12 (MISO), D13 (SCK) -> SPI chain (as per original comments).
     - A5 -> Volume pot (0-5V).
     - USB to host PC for power/programming.
   - If soldered PCB: Desolder Uno, solder Leonardo (or use headers for easy swap).
   - Power check: Measure current draw (<500mA USB limit); shift registers (likely 74HC595) are low-power.

2. **Basic Hardware Test**
   - Upload Blink sketch to Leonardo: Verify USB enumeration (lsusb shows "Arduino Leonardo").
   - Test SPI: Upload simple SPI reader sketch; monitor serial output for shift register data (dummy bytes if no keys pressed).

### Phase 3: Code Porting (1-2 hours)
1. **Scaffold New Code**
   - Create `key_reader_midi.ino` based on Spec.md.
   - Remove: `Serial.begin(115200);` and `Serial.write()` calls.
   - Add: `#include <MIDIUSB.h>`
   - In `setup()`: No serial init; keep SPI/pin setup.
   - Replace `outputMidi()`:
     ```cpp
     void outputMidi(byte command, byte channel, byte data2, byte data3) {
       if (!channel) return;
       byte data1 = command | (channel - 1);
       if (command == 0xB0) {  // CC
         midiEventPacket_t cc = {0x0B, data1, data2, data3};  // Cable 0, MIDI type 0xB
         MidiUSB.sendMIDI(cc);
         MidiUSB.flush();
       } else {  // Note On/Off
         midiEventPacket_t note = {0x09, data1, data2, data3};  // 0x09 for channel voice
         MidiUSB.sendMIDI(note);
         MidiUSB.flush();
       }
     }
     ```
   - In `loop()`: Add `MidiUSB.read();` at end (for bidirectional if needed, but not here).
   - Preserve: Debounce, arrays, latch logic, volume scaling (0-127).

2. **Handle Inverted Logic**
   - If testing shows inversion (press sends Note Off), either:
     - Option A (simple): Invert `keyState = !(keyByte & 0x01);` (due to pullups?).
     - Option B: Swap Note On/Off commands in outputMidi.
   - Decide post-testing; start with preserve-as-is.

3. **Compile & Upload**
   - Select Board: Arduino Leonardo.
   - Port: /dev/ttyACM0 (upload resets automatically).
   - Fix errors: Ensure LSBFIRST etc. compatible.

### Phase 4: Testing & Validation (1-2 hours)
1. **USB MIDI Detection**
   - Plug into Linux host: `lsusb` (look for "USB MIDI Device" or "Leonardo").
   - `aconnect -l`: Should list "key_reader MIDI 1" or similar as output port.
   - If not: Check MIDIUSB examples; reset board (double-tap reset button).

2. **Functional Tests**
   - Use `aseqdump -p <port>` to monitor MIDI output.
   - Press/release keys on manuals/pedal: Verify notes (e.g., C2=36 on ch3), channels (1=upper manual?, 2=lower?, 3=pedal?), On/Off (vel 127/0).
   - Volume pedal: Sweep A5; check CC7 ch4 values 0-127.
   - Debounce: Rapid presses; no chatter.
   - Hotplug: Unplug/replug; no crashes.
   - Edge: All keys, unused (pitch=0 skipped via channel=0).

3. **Integration with GrandOrgue**
   - Launch GrandOrgue; select USB MIDI input.
   - Load organ definition matching channels/notes (e.g., MIDI ch1=Great, ch2=Swell, ch3=Pedal, ch4=expression).
   - Play: Audio output via PipeWire; verify no latency/issues.
   - If inversion: Adjust in software or fix code.

4. **Debug Common Issues**
   - No MIDI: Flush not called; wrong packet type (use 0x09 for notes).
   - Garbage data: SPI mode wrong (MODE3, LSBFIRST).
   - Power: If draw high, add decoupling caps.
   - Use Serial (printf-style via MIDIUSB? Or temp add Serial for debug).

### Phase 5: Finalization (0.5 hours)
1. **Documentation**
   - Update README.md: Hardware, wiring diagram, MIDI spec (link Spec.md), GrandOrgue config tips.
   - Add MIDI channel mapping diagram (e.g., Mermaid in README).

2. **Version Control**
   - Git: Branch `usb-midi`, commit increments (e.g., "Add MIDIUSB integration", "Fix note logic").
   - Tag v2.0.

3. **Maintenance Notes**
   - Libraries: Pin versions (MIDIUSB 1.0.3).
   - Upgrades: If needed, migrate to RP2040 (Pico) for future-proofing, but not now.
   - Backup: Enclose Leonardo if exposed.

## Risks & Mitigations
- Hardware delay: Use Uno + software bridge temporarily.
- Pin mismatch: Leonardo D11=MOSI (not used here, since input-only SPI).
- Inverted keys: Test with multimeter on shift out; fix in code.
- PipeWire quirks: Fallback to JACK if issues; GrandOrgue supports both.
- If fails: Revert to Uno + updated Hairless (but avoid).

This plan is self-contained; execute phases in order. If stuck, reference Spec.md for exact behavior.