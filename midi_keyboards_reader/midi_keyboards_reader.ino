
#include "USB.h"
#include "USBMIDI.h"

// define mode
#define DEBUG false

// ---------------------
//  Keyboard parameters
// ---------------------
#define N_MANUALS 2           // number of manuals
const uint8_t columns_per_manual[N_MANUALS] = {8, 8};       // number of columns for each manual
const uint8_t midi_channel_per_manual[N_MANUALS] = {1, 2};  // midi channel for each manual
const uint8_t base_note[N_MANUALS] = {24,24};               // lowest note for each manual
#define N_ROWS 8              // number of rows of manuals
#define ENABLE LOW            // active scan logic value - LOW for common cathode / HIGH for common anode
#define SETTLE_TIME 5         // [us] row settling time
#define DEBOUNCE 2            // number of ENABLE readings required to set a note on / DISABLE readings required to set a note off
#define NOTE_VEL 127 // organs do not have dynamics. Set this value to the desired fixed velocity [0,127]


constexpr uint16_t computeTotalColumns() {
  uint16_t sum = 0;
  for (uint8_t i = 0; i < N_MANUALS; i++)
    sum += columns_per_manual[i];
  return sum;
}
constexpr uint16_t n_columns = computeTotalColumns();

#define DISABLE !ENABLE

// ---------------------------------------
//  74HC595 shift register pin definition
// ---------------------------------------
#define LATCH_PIN 12 // pin connected to ST_CP of 74HC595（Pin12） 
#define CLOCK_PIN 13 // pin connected to SH_CP of 74HC595（Pin11） 
#define DATA_PIN  11 // pin connected to DS of 74HC595（Pin14） 

// -----------------------------------
//  MCU PIN inputs reading the N_ROWS
// -----------------------------------
int key_pin[N_ROWS] = {4,5,6,7,15,16,17,18};

// --------------
//  State arrays
// --------------
uint8_t keyboard_array[N_ROWS*n_columns] = {0};  // integer array for performing debounce of keys
bool notes_array[N_ROWS*n_columns] = {0};        // boolean array to store if notes are on or off

// -----------------
//  MIDI OUT events
// -----------------
// create midi object
USBMIDI MIDI;

// define a struct for midi events
typedef struct {
  uint8_t note;
  bool noteOn;
} MidiEvent;

// Create a task to run on secondary core
void midiTask(void *parameter);

// create a Queue of midi commands to be sent out
QueueHandle_t midiQueue;
#define QUEUE_LENGTH 256


// -------------------------------- //
void setup() { 
  
  if (DEBUG) {
    // attach serial for debugging purposes
    Serial.begin(115200);
  } else {
    // initialise ESP32 as midi object
    MIDI.begin();
    USB.begin();
  }

  // create a Queue buffer to store midi events to be sent by secondary core (core 1)
  midiQueue = xQueueCreate(QUEUE_LENGTH, sizeof(MidiEvent));

  // create task to run on secondary core (core 1) to read from queue and send MIDI commands to USB
  xTaskCreatePinnedToCore(midiTask, "MidiTask", 4096, NULL, 1, NULL, 1);
  

  // set I/O pins 
  pinMode(LATCH_PIN, OUTPUT); 
  pinMode(CLOCK_PIN, OUTPUT); 
  pinMode(DATA_PIN, OUTPUT); 
  
  for (int pin=0; pin < N_ROWS; pin++)
    if (ENABLE == LOW)
      pinMode(key_pin[pin], INPUT_PULLUP);
    else if (ENABLE == HIGH)
      pinMode(key_pin[pin], INPUT_PULLDOWN);

  // --- SCANNING INITIALISATION ---
  // set all scan lines to DISABLE
  digitalWrite(DATA_PIN, DISABLE);
  digitalWrite(LATCH_PIN, LOW);               // disable all output
  for (int sl = 0; sl < n_columns; sl++) {
    digitalWrite(CLOCK_PIN, LOW);             
    digitalWrite(CLOCK_PIN, HIGH);            
  }
  digitalWrite(LATCH_PIN, HIGH);              // update output by rising level to latch pin

  if (DEBUG)
    Serial.print("Start scanning \n");
} 


// -----------------------------------------
//  LOOP on primary core scanning keyboards
// -----------------------------------------
void loop() { 
  // create variable to store midi output events
  MidiEvent midi_event;

  // --- SCANNING LOOP ---
  for (int sc = 0; sc < n_columns; sc++) {

    if (sc == 0) {
      // Initialise the first scan by sending a single ENABLE
      digitalWrite(DATA_PIN, ENABLE);
    } else if (sc == 1) {
      // Shift out a DISABLE for the rest n_columns-1
      digitalWrite(DATA_PIN, DISABLE);
    }

    // update 74hc595 output
    digitalWrite(LATCH_PIN, LOW);  // disable output
    digitalWrite(CLOCK_PIN, LOW);  // cycle the clock: falling-edge
    digitalWrite(CLOCK_PIN, HIGH); // cycle the clock: rising-edge
    digitalWrite(LATCH_PIN, HIGH); // update output by rising level to latch pin

    delayMicroseconds(SETTLE_TIME); // wait some time to let stable voltages on the row

    // --- READ 8 KEYS within ENABLED ROW ---
    for (int key = 0; key < N_ROWS; key++) {
      int idx_k = sc*N_ROWS + key; // current key index within the keyboards

      if (digitalRead(key_pin[key]) == ENABLE) { // key pressed

        // increment value in the keyboard matrix, saturated at DEBOUNCE 
        if (keyboard_array[idx_k] < DEBOUNCE)
          keyboard_array[idx_k]++;

        // send NOTE ON upon first reaching DEBOUNCE
        if (keyboard_array[idx_k] == DEBOUNCE && !notes_array[idx_k]) {
          notes_array[idx_k] = true;

          midi_event.note = idx_k;
          midi_event.noteOn = true;
          xQueueSend(midiQueue, &midi_event, 0);
        } 
      }
      else { // key not pressed
        
        // decrase value in the keyboard matrix, minimised to 0
        if (keyboard_array[idx_k] > 0)
          keyboard_array[idx_k]--;


        // send NOTE OFF upon first reaching 0
        if (keyboard_array[idx_k] == 0 && notes_array[idx_k]) {
          notes_array[idx_k] = false;

          midi_event.note = idx_k;
          midi_event.noteOn = false;
          xQueueSend(midiQueue, &midi_event, 0);
        } 
      }
    }
  }
} 


// --------------------------------------------------------
//  LOOP on SECONDARY CORE writing out MIDI events via USB
// --------------------------------------------------------
void midiTask(void *parameter) {

  MidiEvent process_event;
  uint8_t midi_channel = 1;

  while (1) {

    if (xQueueReceive(midiQueue, &process_event, portMAX_DELAY)) {

      // extract channel and actual note index
      uint16_t local_index;
      uint8_t manual = getManualFromIndex(process_event.note, local_index);
      uint8_t midi_channel = midi_channel_per_manual[manual];
      uint8_t note = base_note[manual] + local_index;

      if (process_event.noteOn) {
        if (!DEBUG) 
          MIDI.noteOn(note, NOTE_VEL, midi_channel);
        else
          Serial.printf("%d ON\n", process_event.note);

      } else {
        if (!DEBUG)
          MIDI.noteOff(note, 0, midi_channel);
        else
          Serial.printf("%d OFF\n", process_event.note);
          
      }
    }
  }
}


// function returning which manual does the note belong and the note index within the manual
uint8_t getManualFromIndex(uint16_t global_index, uint16_t &local_index) {

  uint16_t acc = 0;

  for (uint8_t m = 0; m < N_MANUALS; m++) {

    uint16_t manual_size = columns_per_manual[m] * N_ROWS;

    if (global_index < acc + manual_size) {
      local_index = global_index - acc;
      return m;
    }

    acc += manual_size;
  }

  // fallback
  local_index = 0;
  return 0;
}
