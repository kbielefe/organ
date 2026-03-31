# Detailed Specification of Original Organ Key Reader

## System Overview
The `key_reader.ino` implements a MIDI output device for a pipe organ interface. It reads key states from a chain of shift registers (likely 74HC165 or similar, input type) via SPI, debounces inputs, and outputs standard MIDI messages over serial (115200 baud, raw binary bytes). An analog volume pedal is read separately.

- **Input Hardware:**
  - Shift registers: 14 bytes (112 bits/keys), daisy-chained. Latch on D10 (load pulse: low then high).
  - SPI Config: CLK=D13, MISO=D12, MOSI=D11 (unused, input mode), Mode 3, LSBFIRST (bits read LSB first), Clock div 2 (~8MHz).
  - Keys: Two manuals (61 keys each?) + 32-key pedalboard, wired conveniently to shift chain. Unused bits mapped to pitch=0.
  - Volume: Potentiometer on A5 (0-1023 ADC -> 0-127 MIDI).
  - Power: 5V bus-powered; assumes active-low or pullup logic (key press pulls bit low/high?).

- **Output Protocol:**
  - Raw MIDI bytes over Serial (no wrapper; bridged by Hairless to USB MIDI).
  - Messages: Channel Voice (Notes, CC); no SysEx, timing, etc.
  - Baud: 115200 (non-standard for MIDI, but works with bridge).
  - Channels: 1 (likely upper manual), 2 (lower), 3 (pedal), 4 (volume CC).
  - Velocity: Fixed (127 on, 0 off); no true velocity sensing.
  - Debounce: 20ms stability before reporting change.
  - Logic Note: Appears inverted – "keyState" true (bit set) triggers Note Off (0x80, vel 0), false triggers Note On (0x90, vel 127). This may be due to wiring (e.g., press clears bit). Preserve unless testing shows mismatch with GrandOrgue expectations.

- **Key Count & Mapping:**
  - Total: 112 bits (14*8), but many unused (pitch=0 skipped).
  - Pitches: MIDI note numbers (36=C2 to 84=C6). Array `pitches[keyCount]` defines mapping; 0 = unused (channel=0 skips output).
  - Channels: `channels[keyCount]` assigns to 1-3; 0 skips.
  - Order: Bits read LSBFIRST per byte; bytes sequential from chain. Physical wiring: Custom PCB, manuals/pedal interleaved for convenience (e.g., low notes first?).

### Detailed Key Mapping
Keys are indexed 0-111 (byte 0 bit 0 = index 0). Below: Index, Pitch (MIDI note), Channel, Usage (inferred: Manual/Pedal based on range/channels).

**Pedalboard (Ch3, low notes 36-46?):**
- 0: 36 (C2), Ch3 – Pedal C
- 1: 38 (D2), Ch3
- 2: 40 (E2), Ch3
- 3: 42 (F2), Ch3
- 4: 43 (F#2), Ch3
- 5: 41 (D#2? Wait, 41=Fb? Typo? Standard: 41=F#? No, MIDI 41=F#2), Ch3
- 6: 39 (D#2? 39=D#2), Ch3
- 7: 37 (D2? 37=D2? Inconsistent; array has 36,38,40,42,43,41,39,37 – perhaps zigzag wiring.
- 8: 45 (A2), Ch3
- 9: 47 (B2), Ch3
- 10: 44 (G#2), Ch3
- 11-13: 0, Ch0 (unused)
- 14: 48 (C3), Ch3
- 15: 46 (A#2), Ch3

**Upper Manual? (Ch1, 41-78):**
- 16: 41 (F#2), Ch1
- 17: 43 (G#2), Ch1
- ... (continues to high C6=84, with gaps/0s for octaves/blanks)
- Full array covers ~5 octaves per manual, with pedals low.

**Lower Manual (Ch2, similar to Ch1 but offset/shifted pitches).**
- Starts at index 48: 41 (F#2), Ch2 – Mirrors upper but different wiring order.
- Note: Pitches repeat patterns (e.g., C major scale descending/ascending per octave), with 0s for non-piped stops or wiring skips.

**Unused:** Any pitch=0 or channel=0 skipped (no MIDI out). ~20-30 active keys per manual + 25-32 pedals (standard organ).

### Behavior Flow
1. **Setup:**
   - Init Serial 115200.
   - Pin D10 OUTPUT (latch).
   - SPI: Begin, LSBFIRST, DIV2, MODE3.
   - Arrays: reported/detected=true (initially "on"?), detectTime=0.

2. **Loop (per iteration, ~few ms):**
   - Read volume: analogRead(A5) -> scale 0-1023 to 0-127.
     - If changed: Send CC (0xB0 | (4-1)=0xB3? Wait, code: 0xB0, ch=4 -> data1=0xB0 | 3 =0xB3, CC=0x07 (vol), value.
     - Note: Code uses 0xB0 fixed command, but standard CC is 0xB0 | ch-1.
   - Latch: Pulse D10 low-high (loads shift registers).
   - now = millis().
   - For each of 14 bytes:
     - Transfer 0x00 via SPI -> reads byte from chain.
     - For each of 8 bits (LSB first):
       - keyState = bit & 1 (true if bit set).
       - stableTime = now - detectTime[index].
       - If keyState != reported[index] AND stable >=20ms:
         - reported = keyState.
         - If keyState (bit set): Send Note Off (0x80 | ch-1, note, 0) – implies bit set = released?
         - Else: Send Note On (0x90 | ch-1, note, 127) – bit clear = pressed?
       - If keyState != detected: Update detected=keyState, detectTime=now (reset debounce timer).
   - End loop.

3. **outputMidi(command, ch, data2, data3):**
   - Skip if ch==0.
   - data1 = command | (ch-1)  (e.g., 0x90 | 0 =0x90 for ch1).
   - Serial.write(data1, data2, data3).  // 3-byte message.

### Inferred Wiring/Logic
- Shift chain: 18x 74LS165 shift registers (parallel load, serial shift out), across 3 boards (6 per board: 2 for upper manual, 2 for lower, 2 for pedals with some unused bits). Daisy-chained per board, outputs buffered via 74LS125 quad buffers between boards to isolate MISO signal. Code reads only 14 bytes (112 bits), skipping the last 4 bits/registers (unused or expansion).
- Latch: PL (parallel load) pins tied to D10 (active-low: pulse LOW to load, HIGH to shift; code pulses LOW then HIGH for rising edge shift).
- CLK: CP (clock) to D13 (rising edge shifts Q7 out).
- Key inputs: Parallel to 165 DS0-DS7 pins, with pull-up resistors (visible at top of schematic) to 5V. Keys are switches shorting inputs to GND on press (active-low logic).
  - Idle (no press): Inputs high (1), bit shifted=1, keyState=true -> treated as released (Note Off if changed).
  - Press: Input low (0), bit=0, keyState=false -> after debounce, send Note On (0x90, vel 127).
  - Release: Back to high (1), keyState=true -> send Note Off (0x80, vel 0).
  - This matches standard MIDI (press=On, release=Off), with debounce preventing chatter.
- No matrix; direct parallel per bit (up to 48 bits/board).
- Volume: Linear pot on A5 (0-5V to ADC 0-1023, scaled 0-127); likely center-grounded or simple voltage divider.
- unplugged var: Declared but unused (perhaps remnant from earlier version).
- Power/comments: 5V Vcc/Gnd rails; connector pinout matches shift register header (1=Vcc, 2=Clk, 3=Load, 4=Out/MISO, 5=Gnd). TTL logic (LS series), low power (~10-20mA total).

### MIDI Message Examples
- Key press (assume bit clears): 0x91 29 7F  (Ch2 note 41 vel127, e.g., lower manual F#2)
- Key release: 0x81 29 00  (Note Off vel0)
- Volume to 64: 0xB3 07 64  (Ch4 CC7=64)

### Validation Notes
- Total active keys: Count non-zero pitches/channels (~150? But 112 bits; duplicates for octaves).
- For port: Map exactly; test inversion with physical keys.
- GrandOrgue: Expects standard On press/Off release; if inverted, fix by swapping commands or inverting keyState = !(bit & 1).