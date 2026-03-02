# ESP32 Dual-Core USB MIDI Matrix Controller

A high-performance USB MIDI keyboard matrix scanner implemented on the ESP32-S3, designed for low-latency organ and multi-manual keyboard applications.

Specifically, this project implements a real-time scanning architecture capable of sustaining ~5 kHz full two-manual matrix (128 keys) scan rates while maintaining deterministic timing through dual-core task separation.

---

## 1. Overview

This repository contains firmware for a USB MIDI controller targeting large key matrices.
The system is particularly suited for:

- Conversion of legacy pipe or electronic organs into modern USB MIDI consoles
- Retrofit of existing organ manuals for integration with software platforms
- Custom-built multi-manual MIDI controllers
- High-density matrix-based input systems

The system is designed with the following engineering objectives:

- Deterministic scan timing
- Minimal latency
- Scalable matrix architecture
- Clean task separation
- USB class-compliant MIDI output
- Hardware simplicity

The implementation leverages the ESP32-S3 dual-core architecture and native USB support.

---

## 2. System Architecture

### 2.1 Hardware Architecture

- Microcontroller: ESP32-S3 (native USB support required)
- Row scanning: cascaded 74HC595 shift register
- Column inputs: Direct GPIO with pull-ups
- Key topology: Matrix configuration (configurable dimensions)

The row selection logic is implemented via serial shift propagation over the cascaded shift registers, allowing a single active row to propagate through the register chain.


### 2.2 Software Architecture

The firmware is structured across both ESP32 cores:

#### Core 0 – Real-Time Scanner
- Controls 74HC595 active row selection
- Reads column inputs
- Applies debounce logic
- Detects state transitions (edge detection)
- Generates MIDI events
- Pushes events to a FreeRTOS queue

#### Core 1 – USB MIDI Engine
- Blocks on queue reception
- Transmits USB MIDI NoteOn / NoteOff events
- Operates independently from scan timing

This separation guarantees that USB transmission latency does not influence matrix scanning determinism.

---

## 3. Performance Characteristics

- Full two-manuals matrix (128 keys) scan frequency: ~5 kHz
- Row settling time: 5 µs (configurable)
- Event-driven MIDI transmission

The architecture maintains stable performance even under high note density.

---

## 4. MIDI Implementation

- USB MIDI Class compliant
- Fixed velocity (organ-oriented design)
- Configurable base note
- Configurable MIDI channel
- Edge-triggered NoteOn / NoteOff generation
- No repeated NoteOn flooding

Velocity is intentionally fixed due to the organ application domain.

---

## 5. Configurable Parameters

The system is fully parameterized at compile time:

```cpp
#define N_MANUALS 2         // number of manuals
const uint8_t columns_per_manual[N_MANUALS] = {8, 8};       // number of columns for each manual
const uint8_t midi_channel_per_manual[N_MANUALS] = {1, 2};  // midi channel for each manual
const uint8_t base_note[N_MANUALS] = {24,24}                // lowest note for each manual
#define N_ROWS 8            // number of rows
#define ENABLE LOW          // active scan logic value - LOW for common cathode / HIGH for common anode
#define SETTLE_TIME 5       // [us] row settling time
#define DEBOUNCE 2          // number of ENABLE readings required to set a note on / DISABLE readings required to set a note off
#define NOTE_VEL 127        // [0,127] fixed note velocity (organs do not have dynamics)

