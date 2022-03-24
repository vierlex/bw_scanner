#include <SD.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <Adafruit_NeoPixel.h>

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

Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ400);

int howClose = 0;
int currentUUID = 0;

void setup() {
  Serial.begin(115200);
  
  // for nrf52840 with native usb
  while (!Serial) { delay(1); }
  delay(500);  

  Serial.println("Lets go!");  
  
  if (!musicPlayer.begin()) { // initialise the music player
    Serial.println("what");    
    Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1);
  }
  Serial.println(F("VS1053 found"));

  //musicPlayer.sineTest(0x44, 500);
  
  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }

  // list files
  printDirectory(SD.open("/"), 0);
  
  // Set volume for left, right channels. lower numbers == louder volume!
  musicPlayer.setVolume(20,20);

  // Play in background via interrupt
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT); 

  pixel.begin();
  pixel.setBrightness(255);
  pixel.setPixelColor(0, pixel.Color(140, 140, 140));  
  pixel.show();
  
  pinMode(PLAY_BUTTON, INPUT);
}

void loop() {
  
  Serial.println("looping!");
  int buttonPress = LOW;

  while (1) {

    // TODO Bluefruit report

    switch (howClose) {
      case 0:
        //pixel.clear();        
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
        Serial.print("Stopping current Track.");
        musicPlayer.stopPlaying();
      }
      else {
        switch (currentUUID) {
          case 0:
            Serial.print("Playing Station 1!");
            musicPlayer.startPlayingFile("/station1.mp3");
            break;
          default:
            break;      
        }
      }
      howClose = 0;
      delay(500);      
    }
    if (buttonPress == HIGH) {
      howClose++;
      Serial.print("trying to play: ");
      Serial.println(howClose);
      delay(500);
    }
  }

}


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
       Serial.print('\t');
     }
     Serial.print(entry.name());
     if (entry.isDirectory()) {
       Serial.println("/");
       printDirectory(entry, numTabs+1);
     } else {
       // files have sizes, directories do not
       Serial.print("\t\t");
       Serial.println(entry.size(), DEC);
     }
     entry.close();
   }
}