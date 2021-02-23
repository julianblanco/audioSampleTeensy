#include <Arduino.h>
#include "FastMCP3008.h"
#define CIRCULAR_BUFFER_INT_SAFE
#include <CircularBuffer.h>
//#include <sd.h>
#include <SdFat.h>

// Use this to enable serial monitoring
#define DEBUG 1

// Macros for debugger which can be enabled with the
// DEBUG macro definition.
#ifdef DEBUG
#define debug_log(...) Serial.println(__VA_ARGS__)
#define debug_write(...) Serial.print(__VA_ARGS__)
#define debug_init() Serial.begin(9600)
#define debug_delay(n) delay(n)
#else
#define debug_log(...)
#define debug_write(...)
#define debug_init()
#define debug_delay(n)
#endif

// Number of channels to capture
#define CHANNEL_COUNT 1

// Filename format string
#define FILENAME_FORMAT "rawdata%d.data"
// Maximum file name length. This allows for up to 999999
// data files with the default filename format above
#define FILENAME_LENGTH 16

// Which LED to flash for debugging
#define DEBUG_LED LED_BUILTIN

// Find the ceiling of a constant integer division at compile time
#define CEILING(x, y) (((x) + (y)-1) / (y))

// Panic situations mean we shouldn't continue
#define PANIC()                                            \
  do                                                       \
  {                                                        \
    debug_log("panic: exceptional condition; halting..."); \
    exit(1);                                               \
  } while (1);

// Number of bytes to buffer for the SD card write
// This should match your SD card hardware buffer size.
#define SD_BUFFER_SIZE 512

#define BUILTIN_SDCARD 254;

// Use built-in SD card pin if unspecified
#ifndef SDCARD_SS_PIN
#define SD_CS_PIN SS
#else
#define SD_CS_PIN SDCARD_SS_PIN
#endif

// FAT version to use on the SD card
#define SD_FAT_TYPE 3

// If frequency is known, use this
// #define SAMPLE_FREQ 20000

// Otherwise, define the time between samples
#ifndef SAMPLE_FREQ
#define TIME_BETWEEN_SAMPLES 200
#define SAMPLE_FREQ (1000000.0 / TIME_BETWEEN_SAMPLES)
#endif

#ifndef TIME_BETWEEN_SAMPLES
#define TIME_BETWEEN_SAMPLES ((int)(1000000.0 / SAMPLE_FREQ))
#endif

// Length of the recording to create in minutes
#define RECORDING_LENGTH 60
// Or, define the sample count
// #define SAMPLE_COUNT 10000

// Or, define the number of samples
#ifndef RECORDING_LENGTH
#define SAMPLE_COUNT 10000
#else
#define SAMPLE_COUNT ((int)((SAMPLE_FREQ)*60 * (RECORDING_LENGTH)))
#endif

#define SAMPLE_SIZE (CHANNEL_COUNT * sizeof(int))

// The number of chunks we will write based on sample count
#define EXPECTED_BLOCK_COUNT (CEILING(int(SAMPLE_COUNT) * SAMPLE_SIZE, 512))

// This is the number of blocks we will buffer in memory at any
// given time. The value of this depends on how fast your SD
// card is and how much memory you have available. I made up
// this ratio. I don't know how many blocks you're expecting
// to read for your sample nor how much memory you have. This
// value may be too high or too low.
//
// If you notice you are nearly out of memory when compiling/
// flashing, you can decrease this number. If you notice you
// have a lot of free space, you can increase in order to
// decrease the likelihood of a circular buffer overrun.
#define BLOCK_BUFFER_COUNT 800// (CEILING(int(EXPECTED_BLOCK_COUNT), 64))

// The number of samples in a block
#define BLOCK_SAMPLE_COUNT (SD_BUFFER_SIZE / SAMPLE_SIZE)

// SD Card Chip Select (a different value is used the first time
// that begin is called, and I don't know why).
#define SD_CHIP_SELECT BUILTIN_SDCARD

// A collection of samples which can be written to disk in
// one block.
typedef struct _block
{
  int samples[BLOCK_SAMPLE_COUNT][CHANNEL_COUNT];
} block_t;

// A circular buffer for holding ready blocks. We use a
// circular buffer of pointers so that there is minimal
// copying of data. The global ISR can modify the next
// block, and then notify the main loop that it is ready
// by add the block pointer to the circular buffer. The
// main loop can directly write the block to the SD from
// the block pointer. This should remove a few copies/
// writes from the program.
CircularBuffer<block_t *, BLOCK_BUFFER_COUNT> g_block_queue;
block_t g_blocks[BLOCK_BUFFER_COUNT];
// This indicates the block that the ISR is currently modifying
unsigned int g_current_block;
// This indicates which sample within the block the ISR will
// modify next.
unsigned int g_current_sample;
// The number of blocks which we have written
unsigned int g_write_count;
// Indicates a overrun condition on the circular buffer
bool g_overrun_flag = false;
// The interval timer to fire ISRs for sampling at the requested
// frequency.
int filesizemax = 5000;
char filename[FILENAME_LENGTH];
int data_file_idx = -1;
int format_result;
IntervalTimer g_sample_timer;
// ADC controllers
FastMCP3008 g_adc_controller;
// SD Card controller
SdFat g_sd_controller;
// Open SD card file
SdFile g_data_file;

// Callback for channel sampling
void sample_all_channels();

void setup()
{

  // Initialzie LED output
  pinMode(DEBUG_LED, OUTPUT);

  // Write some data to the debugging interface
  debug_init();
  debug_log("datalogger initializing with:");
  debug_write("  Sample Freq: ");
  debug_log(SAMPLE_FREQ);
  debug_write("  Num Records: ");
  debug_log(SAMPLE_COUNT);
  debug_write("Num Writes: ");
  debug_log(EXPECTED_BLOCK_COUNT);
  debug_delay(5000);

  // Wait for SD to be present
  debug_log("initializing sd-card");
  g_sd_controller.begin((SdioConfig(FIFO_SDIO)));
  while (!g_sd_controller.begin((SdioConfig(FIFO_SDIO))))
  {
    debug_log("error: sd initialization failed; waiting for SD card...");
    delay(1000);
    // NOTE: The old code used a different argument the second time. Is there a reason?
    // cardPresent = g_sd_controller.begin(chipSelect);
  }

  do
  {
    String baseFolderName = "recording";
    int basenumber = 0;
    String foldername = baseFolderName + basenumber;
    // Produce the next filename string
    while (1)
    {
      foldername = baseFolderName + basenumber;

      if (g_sd_controller.exists(foldername))
      {
        Serial.print("error: folder exists ");
        Serial.println(foldername);
        basenumber += 1;
      }
      else
      {
        if (!g_sd_controller.mkdir(foldername))
        {
          debug_log("Create Folder failed");
        }
        Serial.print("Folder Created ");
        Serial.println(foldername);
        break;
      }

      data_file_idx += 1;
    }
          if (!g_sd_controller.chdir(foldername))
      {
        debug_log("Could not cd to folder");
        PANIC();
      }
    format_result = snprintf(filename, FILENAME_LENGTH, FILENAME_FORMAT, data_file_idx);

    // Check if we have run out of data files
    if (format_result >= FILENAME_LENGTH)
    {
      debug_log("error: no more data files available");
      PANIC();
    }
  } while (g_sd_controller.exists(filename));

  // Open the data file
  g_data_file.open(filename, FILE_WRITE);
  if (!g_data_file)
  {
    debug_log(F("open failed"));
    PANIC();
  }

  // Let the debugger know we are good to go
  debug_write(F("opened: "));
  debug_log(filename);

  // Initialize the ADC
  // g_adc_controller.init();

  // Initialize the current block
  g_current_block = 0;
  g_current_sample = 0;
  g_write_count = 0;

  // Start our ISR
  debug_log("starting sampling timer");
  g_sample_timer.begin(sample_all_channels, TIME_BETWEEN_SAMPLES);

  // Turn on LED to indicate we are sampling
  digitalWrite(DEBUG_LED, HIGH);
}

// functions called by IntervalTimer should be short, run as quickly as
// possible, and should avoid calling other functions if possible.
void sample_all_channels()
{
  // Grab the address of our sample as an array of integers
  int *const pSample = g_blocks[g_current_block].samples[g_current_sample];

  // Read all channels
  for (int i = 0; i < CHANNEL_COUNT; ++i)
  {
    // pSample[i] = g_adc_controller.read(g_adc_controller.getChannelMask(i));
    //pSample[i] = i;
    pSample[0] = analogRead(21 );
  }

  // Increment the current sample
  g_current_sample += 1;

  // Push this block
  if (g_current_sample >= BLOCK_SAMPLE_COUNT)
  {
    // `push` returns false for an overrun and true for normal exit
    g_overrun_flag = !g_block_queue.push(&g_blocks[g_current_block]);
    g_current_block += 1;
    g_current_sample = 0;

    // Wrap around the available blocks
    if (g_current_block >= BLOCK_BUFFER_COUNT)
    {
      g_current_block = 0;
    }
  }
}

// Wait for a ready block, and flush it to the SD card
void loop()
{
  block_t *pBlock;

  if (g_overrun_flag)
  {
    debug_log("error: detected circular buffer overrun; data may be corrupted!");
  }

  // Do we have any ready blocks?
  if (g_block_queue.size() > 0)
  {
    if (g_write_count % filesizemax == 0)
    {
      g_data_file.close();
      debug_log(F("close file"));
      data_file_idx += 1;
      format_result = snprintf(filename, FILENAME_LENGTH, FILENAME_FORMAT, data_file_idx);
      debug_log(F("open"));
      g_data_file.open(filename, FILE_WRITE);
      if (!g_data_file)
      {
        debug_log(F("open failed"));
        PANIC();
      }
    }
    // Grab the next block pointer
    pBlock = g_block_queue.shift();
    // Write the block data
    g_data_file.write(pBlock, sizeof(block_t));
    // Increment our block write tracker
    g_write_count += 1;
  }

  // We've written the number of requested chunks.
  if (g_write_count >= EXPECTED_BLOCK_COUNT)
  {
    // Stop the ISR and close the data file
    g_sample_timer.end();
    g_data_file.close();

    // Dump some info to the debugger
    debug_write("sampling complete; ");
    debug_write(g_write_count);
    debug_log(" chunks written to sd card");
    debug_log("halting...");

    // Flash LED to indicate completion. Could replace with exit(0)
    // if you didn't need visual output
    while (1)
    {
      delay(1000);
      digitalWrite(DEBUG_LED, HIGH);
      delay(1000);
      digitalWrite(DEBUG_LED, LOW);
    }
  }
}
