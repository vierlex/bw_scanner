#include <string.h>
#include <bluefruit.h>

#include <SD.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>

#include <Adafruit_NeoPixel.h>

// Set to one to ouput to Serial
#define DEBUG (0)

// SCANNER THINGS --------------------------------------------

#define VERBOSE_OUTPUT (0)    // Set this to 1 for verbose adv packet output to the serial monitor
#define ARRAY_SIZE     (4)    // The number of RSSI values to store and compare
#define TIMEOUT_MS     (2500) // Number of milliseconds before a record is invalidated in the list

#if (ARRAY_SIZE <= 1)
  #error "ARRAY_SIZE must be at least 2"
#endif

// Note that the byte order is reversed ... 
const uint8_t BIRD_ONE_UUID[] =
{
    // 784b4c66-fec9-41cc-b82c-59bebbe6654d
    0x4D, 0x65, 0xE6, 0xBB, 0xBE, 0x59, 0x2C, 0xB8,
    0xCC, 0x41, 0xC9, 0xFE, 0x66, 0x4C, 0x4B, 0x78
};

BLEUuid bird_one_uuid = BLEUuid(BIRD_ONE_UUID);

/* This struct is used to track detected nodes */
typedef struct node_record_s
{
  uint8_t  addr[6];    // Six byte device address
  int8_t   rssi;       // RSSI value
  uint32_t timestamp;  // Timestamp for invalidation purposes
  int8_t   reserved;   // Padding for word alignment
} node_record_t;

node_record_t records[ARRAY_SIZE];

// AUDIO OUTPUT THINGS ---------------------------------------
#define VS1053_RESET   -1     // VS1053 reset pin (not used!)
#define VS1053_CS       6     // VS1053 chip select pin (output)
#define VS1053_DCS     10     // VS1053 Data/command select pin (output)
#define CARDCS          5     // Card chip select pin
// DREQ should be an Int pin *if possible* (not possible on 32u4)
#define VS1053_DREQ     9     // VS1053 Data request, ideally an Interrupt pin

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

#define PLAY_BUTTON PIN_A1
//#define PLAY_BUTTON PIN_BUTTON1

// LED THINGS -------------------------------------------------
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ400);


// BIRD LOGIC THINGS ------------------------------------------
int howClose = 0;
int currentUUID = 0;

// SETUP ======================================================
void setup() {
  if (DEBUG) Serial.begin(115200);
  
  // for nrf52840 with native usb
  if (DEBUG) while (!Serial) { delay(1); }
  delay(500);  

  if (DEBUG) Serial.println("Lets go!");  
  
  setup_scanner();
  setup_audio();
  setup_led();
}

// SCANNER SETUP -----------------------------------------------
void setup_scanner() {
  /* Clear the results list */
  memset(records, 0, sizeof(records));
  for (uint8_t i = 0; i<ARRAY_SIZE; i++)
  {
    // Set all RSSI values to lowest value for comparison purposes,
    // since 0 would be higher than any valid RSSI value
    records[i].rssi = -128;
  }

  /* Enable both peripheral and central modes */
  if ( !Bluefruit.begin(1, 1) )
  {
    if (DEBUG) Serial.println("Unable to init Bluefruit");
    while(1)
    {
      digitalToggle(LED_RED);
      delay(100);
    }
  }

  if (DEBUG) Serial.println("Bluefruit initialized (central mode)");
  Bluefruit.setTxPower(8);    // Check bluefruit.h for supported values

  /* Set the LED interval for blinky pattern on BLUE LED */
  Bluefruit.setConnLedInterval(250);

  /* Start Central Scanning
   * - Enable auto scan if disconnected
   * - Filter out packet with a min rssi
   * - Interval = 100 ms, window = 50 ms
   * - Use active scan (used to retrieve the optional scan response adv packet)
   * - Start(0) = will scan forever since no timeout is given
   */
  Bluefruit.Scanner.setRxCallback(scan_callback);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.filterRssi(-80);            // Only invoke callback for devices with RSSI >= -80 dBm
  Bluefruit.Scanner.filterUuid(bird_one_uuid);           // Only invoke callback if the target UUID was found
  Bluefruit.Scanner.setInterval(160, 80);       // in units of 0.625 ms
  Bluefruit.Scanner.useActiveScan(true);        // Request scan response data
  Bluefruit.Scanner.start(0);                   // 0 = Don't stop scanning after n seconds
  if (DEBUG) Serial.println("Scanning ...");
}

void setup_audio() {
  if (!musicPlayer.begin()) { // initialise the music player
    if (DEBUG) Serial.println("what");    
    if (DEBUG) Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1);
  }
  
  if (DEBUG) Serial.println(F("VS1053 found"));

  //musicPlayer.sineTest(0x44, 500);
  
  if (!SD.begin(CARDCS)) {
    if (DEBUG) Serial.println(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }

  // list files
  printDirectory(SD.open("/"), 0);
  
  // Set volume for left, right channels. lower numbers == louder volume!
  musicPlayer.setVolume(20,20);

  // Play in background via interrupt
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); 
}

void setup_led() {
  pixel.begin();
  pixel.setBrightness(255);
  pixel.setPixelColor(0, pixel.Color(140, 140, 140));  
  pixel.show();
  
  pinMode(PLAY_BUTTON, INPUT);
}


// MAIN LOOP ==============================================
void loop() {
  
  if (DEBUG) Serial.println("looping!");
  int buttonPress = LOW;

  while (1) {

    // TODO Bluefruit report

    switch (howClose) {
      case 0:
        pixel.clear();        
        break;
      case 1:
        pixel.setBrightness(255);
        pixel.setPixelColor(0, pixel.Color(140, 0, 0));
        pixel.show();
        break;
      case 2:
        pixel.setBrightness(255);
        pixel.setPixelColor(0, pixel.Color(255, 140, 0));
        pixel.show();
        break;
      case 3:
        pixel.setBrightness(255);
        pixel.setPixelColor(0, pixel.Color(0, 140, 0));
        pixel.show();
        break;
      default:
        pixel.setBrightness(255);        
        pixel.clear(); 
        break;
    }
        
    buttonPress = digitalRead(PLAY_BUTTON);
    if (buttonPress == HIGH && howClose == 3) {
      if (musicPlayer.playingMusic) {
        if (DEBUG) Serial.print("Stopping current Track.");
        musicPlayer.stopPlaying();
      }
      else {
        if (DEBUG) Serial.print("Playing Bird 1!");
        musicPlayer.startPlayingFile("/station1.mp3");
      }
      
      delay(500);      
    }
  }

}


// HELPER FUNCTIONS ========================================

// SCANNER HELPER ------------------------------------------

/* This callback handler is fired every time a valid advertising packet is detected */
void scan_callback(ble_gap_evt_adv_report_t* report)
{
  node_record_t record;
  
  /* Prepare the record to try to insert it into the existing record list */
  memcpy(record.addr, report->peer_addr.addr, 6); /* Copy the 6-byte device ADDR */
  record.rssi = report->rssi;                     /* Copy the RSSI value */
  record.timestamp = millis();                    /* Set the timestamp (approximate) */

  /* Attempt to insert the record into the list */
  if (insertRecord(&record) == 1)                 /* Returns 1 if the list was updated */
  { 
    if (record.rssi > -80) howClose = 1;
    if (record.rssi > -70) howClose = 2;
    if (record.rssi > -60) howClose = 3;

    printRecordList();                            /* The list was updated, print the new values */
    if (DEBUG) Serial.println("");
  }
  else {
    howClose = 0;
  }

  verbose_output();

  // For Softdevice v6: after received a report, scanner will be paused
  // We need to call Scanner resume() to continue scanning
  Bluefruit.Scanner.resume();
}


void verbose_output() {
  /* Fully parse and display the advertising packet to the Serial Monitor
 * if verbose/debug output is requested */
#if VERBOSE_OUTPUT
  uint8_t len = 0;
  uint8_t buffer[32];
  memset(buffer, 0, sizeof(buffer));

  /* Display the timestamp and device address */
  if (report->type.scan_response)
  {
    if (DEBUG) Serial.printf("[SR%10d] Packet received from ", millis());
  }
  else
  {
    if (DEBUG) Serial.printf("[ADV%9d] Packet received from ", millis());
  }
  // MAC is in little endian --> print reverse
  if (DEBUG) Serial.printBufferReverse(report->peer_addr.addr, 6, ':');
  if (DEBUG) Serial.println("");
  
  /* Raw buffer contents */
  if (DEBUG) Serial.printf("%14s %d bytes\n", "PAYLOAD", report->data.len);
  if (report->data.len)
  {
    if (DEBUG) Serial.printf("%15s", " ");
    if (DEBUG) Serial.printBuffer(report->data.p_data, report->data.len, '-');
    if (DEBUG) Serial.println();
  }

  /* RSSI value */
  if (DEBUG) Serial.printf("%14s %d dBm\n", "RSSI", report->rssi);

  /* Adv Type */
  if (DEBUG) Serial.printf("%14s ", "ADV TYPE");
  if ( report->type.connectable )
  {
    if (DEBUG) Serial.print("Connectable ");
  }else
  {
    if (DEBUG) Serial.print("Non-connectable ");
  }

  if ( report->type.directed )
  {
    if (DEBUG) Serial.println("directed");
  }else
  {
    if (DEBUG) Serial.println("undirected");
  }

  /* Shortened Local Name */
  if(Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME, buffer, sizeof(buffer)))
  {
    if (DEBUG) Serial.printf("%14s %s\n", "SHORT NAME", buffer);
    memset(buffer, 0, sizeof(buffer));
  }

  /* Complete Local Name */
  if(Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME, buffer, sizeof(buffer)))
  {
    if (DEBUG) Serial.printf("%14s %s\n", "COMPLETE NAME", buffer);
    memset(buffer, 0, sizeof(buffer));
  }

  /* TX Power Level */
  if (Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_TX_POWER_LEVEL, buffer, sizeof(buffer)))
  {
    if (DEBUG) Serial.printf("%14s %i\n", "TX PWR LEVEL", buffer[0]);
    memset(buffer, 0, sizeof(buffer));
  }

  /* Check for UUID16 Complete List */
  len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE, buffer, sizeof(buffer));
  if ( len )
  {
    printUuid16List(buffer, len);
  }

  /* Check for UUID16 More Available List */
  len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE, buffer, sizeof(buffer));
  if ( len )
  {
    printUuid16List(buffer, len);
  }

  /* Check for UUID128 Complete List */
  len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE, buffer, sizeof(buffer));
  if ( len )
  {
    printUuid128List(buffer, len);
  }

  /* Check for UUID128 More Available List */
  len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_MORE_AVAILABLE, buffer, sizeof(buffer));
  if ( len )
  {
    printUuid128List(buffer, len);
  }  

  /* Check for BLE UART UUID */
  if ( Bluefruit.Scanner.checkReportForUuid(report, BLEUART_UUID_SERVICE) )
  {
    if (DEBUG) Serial.printf("%14s %s\n", "BLE UART", "UUID Found!");
  }

  /* Check for DIS UUID */
  if ( Bluefruit.Scanner.checkReportForUuid(report, UUID16_SVC_DEVICE_INFORMATION) )
  {
    if (DEBUG) Serial.printf("%14s %s\n", "DIS", "UUID Found!");
  }

  /* Check for Manufacturer Specific Data */
  len = Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buffer, sizeof(buffer));
  if (len)
  {
    if (DEBUG) Serial.printf("%14s ", "MAN SPEC DATA");
    if (DEBUG) Serial.printBuffer(buffer, len, '-');
    if (DEBUG) Serial.println();
    memset(buffer, 0, sizeof(buffer));
  }

  if (DEBUG) Serial.println();
#endif
}


/* Prints a UUID16 list to the Serial Monitor */
void printUuid16List(uint8_t* buffer, uint8_t len)
{
  if (DEBUG) Serial.printf("%14s %s", "16-Bit UUID");
  for(int i=0; i<len; i+=2)
  {
    uint16_t uuid16;
    memcpy(&uuid16, buffer+i, 2);
    if (DEBUG) Serial.printf("%04X ", uuid16);
  }
  if (DEBUG) Serial.println();
}

/* Prints a UUID128 list to the Serial Monitor */
void printUuid128List(uint8_t* buffer, uint8_t len)
{
  (void) len;
  if (DEBUG) Serial.printf("%14s %s", "128-Bit UUID");

  // Print reversed order
  for(int i=0; i<16; i++)
  {
    const char* fm = (i==4 || i==6 || i==8 || i==10) ? "-%02X" : "%02X";
    if (DEBUG) Serial.printf(fm, buffer[15-i]);
  }

  if (DEBUG) Serial.println();  
}

/* Prints the current record list to the Serial Monitor */
void printRecordList(void)
{
  for (uint8_t i = 0; i<ARRAY_SIZE; i++)
  {
    if (DEBUG) Serial.printf("[%i] ", i);
    if (DEBUG) Serial.printBuffer(records[i].addr, 6, ':');
    if (DEBUG) Serial.printf(" %i (%u ms)\n", records[i].rssi, records[i].timestamp);
  }
}

/* This function performs a simple bubble sort on the records array */
/* It's slow, but relatively easy to understand */
/* Sorts based on RSSI values, where the strongest signal appears highest in the list */
void bubbleSort(void)
{
  int inner, outer;
  node_record_t temp;

  for(outer=0; outer<ARRAY_SIZE-1; outer++)
  {
    for(inner=outer+1; inner<ARRAY_SIZE; inner++)
    {
      if(records[outer].rssi < records[inner].rssi)
      {
        memcpy((void *)&temp, (void *)&records[outer], sizeof(node_record_t));           // temp=records[outer];
        memcpy((void *)&records[outer], (void *)&records[inner], sizeof(node_record_t)); // records[outer] = records[inner];
        memcpy((void *)&records[inner], (void *)&temp, sizeof(node_record_t));           // records[inner] = temp;
      }
    }
  }
}

/*  This function will check if any records in the list
 *  have expired and need to be invalidated, such as when
 *  a device goes out of range.
 *  
 *  Returns the number of invalidated records, or 0 if
 *  nothing was changed.
 */
int invalidateRecords(void)
{
  uint8_t i;
  int match = 0;

  /* Not enough time has elapsed to avoid an underflow error */
  if (millis() <= TIMEOUT_MS)
  {
    return 0;
  }

  /* Check if any records have expired */
  for (i=0; i<ARRAY_SIZE; i++)
  {
    if (records[i].timestamp) // Ignore zero"ed records
    {
      if (millis() - records[i].timestamp >= TIMEOUT_MS)
      {
        /* Record has expired, zero it out */
        memset(&records[i], 0, sizeof(node_record_t));
        records[i].rssi = -128;
        match++;
      }
    }
  }

  /* Resort the list if something was zero'ed out */
  if (match)
  {
    // Serial.printf("Invalidated %i records!\n", match);
    bubbleSort();    
  }

  return match;
}

/* This function attempts to insert the record if it is larger than the smallest valid RSSI entry */
/* Returns 1 if a change was made, otherwise 0 */
int insertRecord(node_record_t *record)
{
  uint8_t i;
  
  /* Invalidate results older than n milliseconds */
  invalidateRecords();
  
  /*  Record Insertion Workflow:
   *  
   *            START
   *              |
   *             \ /
   *        +-------------+
   *  1.    | BUBBLE SORT |   // Put list in known state!
   *        +-------------+
   *              |
   *        _____\ /_____
   *       /    ENTRY    \    YES
   *  2. <  EXISTS W/THIS > ------------------+
   *       \   ADDRESS?  /                    |
   *         -----------                      |
   *              | NO                        |
   *              |                           |
   *       ______\ /______                    |
   *      /      IS       \   YES             |
   *  3. < THERE A ZERO'ED >------------------+
   *      \    RECORD?    /                   |
   *        -------------                     |
   *              | NO                        |
   *              |                           |
   *       ______\ /________                  |
   *     /     IS THE       \ YES             |
   *  4.<  RECORD'S RSSI >=  >----------------|
   *     \ THE LOWEST RSSI? /                 |
   *       ----------------                   |
   *              | NO                        |
   *              |                           |
   *             \ /                         \ /
   *      +---------------+           +----------------+
   *      | IGNORE RECORD |           | REPLACE RECORD |
   *      +---------------+           +----------------+
   *              |                           |
   *              |                          \ /
   *             \ /                  +----------------+
   *             EXIT  <------------- |   BUBBLE SORT  |
   *                                  +----------------+
   */  

  /* 1. Bubble Sort 
   *    This puts the lists in a known state where we can make
   *    certain assumptions about the last record in the array. */
  bubbleSort();

  /* 2. Check for a match on existing device address */
  /*    Replace it if a match is found, then sort */
  uint8_t match = 0;
  for (i=0; i<ARRAY_SIZE; i++)
  {
    if (memcmp(records[i].addr, record->addr, 6) == 0)
    {
      match = 1;
    }
    if (match)
    {
      memcpy(&records[i], record, sizeof(node_record_t));
      goto sort_then_exit;
    }
  }

  /* 3. Check for zero'ed records */
  /*    Insert if a zero record is found, then sort */
  for (i=0; i<ARRAY_SIZE; i++)
  {
    if (records[i].rssi == -128)
    {
      memcpy(&records[i], record, sizeof(node_record_t));
      goto sort_then_exit;
    }
  }

  /* 4. Check RSSI of the lowest record */
  /*    Replace if >=, then sort */
  if (records[ARRAY_SIZE-1].rssi <= record->rssi)
  {
      memcpy(&records[ARRAY_SIZE-1], record, sizeof(node_record_t));
      goto sort_then_exit;
  }

  /* Nothing to do ... RSSI is lower than the last value, exit and ignore */
  return 0;

sort_then_exit:
  /* Bubble sort */
  bubbleSort();
  return 1;
}

// AUDIO HELPER --------------------------------------------
/// File listing helper
void printDirectory(File dir, int numTabs) {
   while(true) {
     
     File entry =  dir.openNextFile();
     if (! entry) {
       // no more files
       //Serial.println("**nomorefiles**");
       break;
     }

     for (uint8_t i=0; i<numTabs; i++) {
       if (DEBUG) Serial.print('\t');
     }

     if (DEBUG) Serial.print(entry.name());

     if (entry.isDirectory()) {
       if (DEBUG) Serial.println("/");
       printDirectory(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       if (DEBUG) Serial.print("\t\t");
       if (DEBUG) Serial.println(entry.size(), DEC);
     }

     entry.close();
   }
}
