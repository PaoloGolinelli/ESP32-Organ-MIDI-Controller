
#include "USB.h"
#include "USBMIDI.h"

// define mode
#define DEBUG false

// ---------------------
//  Keyboard parameters
// ---------------------
#define N_SCAN_LINES_KEY1 8 // number of scan lines of keyboard 1
#define N_SCAN_LINES_KEY2 8 // numebr of scan lines 
#define N_KEYS_ROW 8        // todo
#define ENABLE LOW          // logic value todo
#define DEBOUNCE 2          // number of ENABLE readings required to set a note on / DISABLE readings required to set a note off
#define BASE_NOTE 24        // lowest note on the keyboard (36 = C2)


const uint8_t n_scan_lines = N_SCAN_LINES_KEY1 + N_SCAN_LINES_KEY2; // 
#define DISABLE !ENABLE

// ---------------------------------------
//  74HC595 shift register pin definition
// ---------------------------------------
#define LATCH_PIN 12 // Pin connected to ST_CP of 74HC595（Pin12） 
#define CLOCK_PIN 13 // Pin connected to SH_CP of 74HC595（Pin11） 
#define DATA_PIN  11 // Pin connected to DS of 74HC595（Pin14） 

// ---------------------------------------
//  MCU PIN inputs reading the N_KEYS_ROW
// ---------------------------------------
int key_pin[N_KEYS_ROW] = {4,5,6,7,15,16,17,18};

// --------------
//  State arrays
// --------------
uint8_t keyboard_array[N_KEYS_ROW*n_scan_lines] = {0};
bool notes_array[N_KEYS_ROW*n_scan_lines] = {0}; // boolean array to store if notes are on or off

// ----------------
//  MIDI paramters
// ----------------
#define NOTE_VEL 127 // organs do not have dynamics. Set this value to the desired fixed velocity [0,127]
#define MIDI_CHANNEL_KEY1 1 // midi channel for manual 1
#define MIDI_CHANNEL_KEY2 2 // midi channel for manual 2

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


  // set pins to output 
  pinMode(LATCH_PIN, OUTPUT); 
  pinMode(CLOCK_PIN, OUTPUT); 
  pinMode(DATA_PIN, OUTPUT); 

  for (int pin=0; pin < N_KEYS_ROW; pin++)
    pinMode(key_pin[pin], INPUT_PULLUP);


  // --- SCANNING INITIALISATION ---
  // set all scan lines to DISABLE
  digitalWrite(DATA_PIN, DISABLE);
  digitalWrite(LATCH_PIN, LOW);               // disable output
  for (int sl = 0; sl < n_scan_lines; sl++) {
    digitalWrite(CLOCK_PIN, LOW);             // 
    digitalWrite(CLOCK_PIN, HIGH);            // 
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
  for (int sl = 0; sl < n_scan_lines; sl++) {

    if (sl == 0) {
      // Initialise the first scan by sending a single ENABLE
      digitalWrite(DATA_PIN, ENABLE);
    } else if (sl == 1) {
      // Shift out a DISABLE for the rest N_SCANLINES-1
      digitalWrite(DATA_PIN, DISABLE);
    }

    // update 74hc595 output
    digitalWrite(LATCH_PIN, LOW);  // disable output
    digitalWrite(CLOCK_PIN, LOW);  // cycle the clock: falling-edge
    digitalWrite(CLOCK_PIN, HIGH); // cycle the clock: rising-edge
    digitalWrite(LATCH_PIN, HIGH); // update output by rising level to latch pin

    delayMicroseconds(5); // settle time

    // --- READ 8 KEYS within ENABLED ROW ---
    for (int key = 0; key < N_KEYS_ROW; key++) {
      int idx_k = sl*N_KEYS_ROW + key; // current key index within the keyboards

      if (digitalRead(key_pin[key]) == ENABLE) {

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
      else {
        
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

      // extract channel
      if (process_event.note < 64) 
        midi_channel = MIDI_CHANNEL_KEY1;
      else
        midi_channel = MIDI_CHANNEL_KEY2;

      // compute actual note
      int note = BASE_NOTE + (process_event.note % 64);

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
