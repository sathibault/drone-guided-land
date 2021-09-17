#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>

// Period between frames
#define FRAME_RATE_MS 1000

#define VERBOSE 1
#define DUMP_CNN 0
#define IMAGE_LOGGER 0

int fileNo; // Current file num for logging
File imgfile; // Output image file
unsigned long deadline; // Time of next capture

// 48KB of working memory for image and CNN
uint8_t maps[49152];

// Declarations for logging to on-board flash

#if defined(EXTERNAL_FLASH_USE_QSPI)
Adafruit_FlashTransport_QSPI flashTransport;
#elif defined(EXTERNAL_FLASH_USE_SPI)
Adafruit_FlashTransport_SPI flashTransport(EXTERNAL_FLASH_USE_CS, EXTERNAL_FLASH_USE_SPI);
#else
#error No QSPI/SPI flash are defined on your board variant.h !
#endif

Adafruit_SPIFlash flash(&flashTransport);
FatFileSystem fatfs;

////////// CNN implementation (gndnet.cpp)
extern void gndnet(uint8_t *maps);

////////// Prototypes

void makeFilename(char *filename, int n);
int findNextFileNo(int cur);
void open_new(int no);
void readBlock(unsigned char *data, int len);
void writeHeaderPGM(int w, int h);
void writePGM(int w, int h);
void writeBmp(int w, int h);
int score_site(int8_t *activation, int y, int x, int sz);
void halt();


#define CHUNK_SZ 1280 // Read 4 rows at a time from FPGA
#define CHUNK_CNT 80 // 320x320 => 80 chunks


////////// Setup

void setup() {
  // Initialize FPGA module pins
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH); // FPGA PROGN (SPI_SEL)
  pinMode(9, OUTPUT);
  digitalWrite(9, HIGH); // FPGA HOST SSN
  pinMode(5, OUTPUT);
  digitalWrite(5, LOW); // FPGA CAPTURE REQ
  
  Serial.begin(115200);

  delay(4000);
#if IMAGE_LOGGER
if (!flash.begin()) {
    Serial.println("Error, failed to initialize flash chip!");
    while(1);
  }

  if (!fatfs.begin(&flash)) {
    Serial.println("Error, failed to mount newly formatted filesystem!");
    Serial.println("Was the flash chip formatted with the fatfs_format example?");
    while(1);
  }
  fileNo = findNextFileNo(0);
#endif

  pinMode(PIN_SPI_MISO, INPUT);
  pinMode(PIN_SPI_MOSI, OUTPUT);
  pinMode(PIN_SPI_SCK, OUTPUT);
  SPI.begin();

  // Capture may get triggered during power-up, clear FPGA data
  uint8_t data[CHUNK_SZ];
  for (int i = 0; i < CHUNK_CNT; i++) {
      readBlock(data, CHUNK_SZ);
  }

  delay(4000);
  deadline = millis();
}

////////// Main loop

void loop()
{
  int i, x, off;
  uint8_t data[CHUNK_SZ];

  unsigned long now = millis();
  if (now >= deadline) {
    unsigned long start = now;

    digitalWrite(5, HIGH); // FPGA CAPTURE REQ
    digitalWrite(5, LOW);

    SPI.beginTransaction(SPISettings(3000000, MSBFIRST, SPI_MODE0));
    i = 0;
    while (1) {
      digitalWrite(9, LOW); // FPGA HOST SSN
      x = SPI.transfer(0);
      digitalWrite(9, HIGH); // FPGA HOST SSN
      // wait for begin image magic number 0x5a
      if (x == 0x5a)
        break;
      // if not ready should expect retry magic number 0x5a, otherwise we are out of sync somehow
      if (x != 0x96) {
        SPI.endTransaction();
        Serial.print(x);
        Serial.println(" capture error");
        halt();
      }
      i++;
    }
    SPI.endTransaction();

#if VERBOSE
    Serial.print("ready in ");
    Serial.println(i);
#endif

#if IMAGE_LOGGER
    open_new(fileNo);
    writeHeaderPGM(320, 320);
#endif

    off = 0;
    for (i = 0; i < CHUNK_CNT; i++) {
      // Read 4 rows of Bayer image from FPGA
      readBlock(data, CHUNK_SZ);

      // Convert to 1 row of RGB
      debayer(data, 320, 2, 2, maps+off, 80*80);
      off += 80; // move offset 1 row forward for next iteration

#if IMAGE_LOGGER
      imgfile.write(data, CHUNK_SZ);
#endif
    }

#if IMAGE_LOGGER
    imgfile.close();
    fileNo += 1;
#endif

#if VERBOSE
    Serial.print(millis()-start);
    Serial.println(" capture");
#endif

    // Invoke CNN
    start = millis();
    gndnet(maps);

#if VERBOSE
    Serial.print(millis()-start);
    Serial.println(" CNN");
#endif

#if DUMP_CNN
    for (int y=0; y<108; y++) {
      for (int x=0; x<36; x++) {
        Serial.print(maps[y*36+x]);
        Serial.print(' ');
      }
      Serial.println("");
    }
#endif

    // Pixels per meter - when deployed this will come from the flight controller's height estimate
    int sz = 44;
    int half_sz = sz/2;
    int diag = (7 * sz) / 10;

    // Center position
    int top = 160 - half_sz;
    int left = 160 - half_sz;

    // Compute current site score
    Serial.print("C ");
    Serial.println(score_site((int8_t *)(maps+1296), top, left, sz));

    // Compute site score in eight directions
    Serial.print("N ");
    Serial.println(score_site((int8_t *)(maps+1296), top-sz, left, sz));

    Serial.print("NE ");
    Serial.println(score_site((int8_t *)(maps+1296), top-diag, left+diag, sz));

    Serial.print("E ");
    Serial.println(score_site((int8_t *)(maps+1296), top, left+sz, sz));

    Serial.print("SE ");
    Serial.println(score_site((int8_t *)(maps+1296), top+diag, left+diag, sz));

    Serial.print("S ");
    Serial.println(score_site((int8_t *)(maps+1296), top+sz, left, sz));

    Serial.print("SW ");
    Serial.println(score_site((int8_t *)(maps+1296), top+diag, left-diag, sz));

    Serial.print("W ");
    Serial.println(score_site((int8_t *)(maps+1296), top, left-sz, sz));

    Serial.print("NW ");
    Serial.println(score_site((int8_t *)(maps+1296), top-diag, left-diag, sz));
    
    deadline += FRAME_RATE_MS;
  } else {
    delay(deadline - now);
  }
}

////////// SD functions

void makeFilename(char *filename, int n) {
  strcpy(filename, "/cam0000.pgm");
  for (int j = 7; j >= 4; j--) {
    filename[j] = '0' + n%10;
    n = n / 10;
  }
}

int findNextFileNo(int cur) {
  char filename[15];

  for (int i = cur; i < 10000; i++) {
    makeFilename(filename, i);
    // create if does not exist, do not open existing, write, sync after write
    if (! fatfs.exists(filename)) {
      return(i);
    }
  }
}

void open_new(int no) {
  char filename[15];
  makeFilename(filename, no);

  imgfile = fatfs.open(filename, FILE_WRITE);;
  if( ! imgfile ) {
    Serial.print("Couldnt create "); 
    Serial.println(filename);
    halt();
  }
#if VERBOSE
  Serial.print("Writing ");
  Serial.println(filename);
#endif
}


////////// FPGA functions

void readBlock(uint8_t *data, int len) {
  SPI.beginTransaction(SPISettings(3000000, MSBFIRST, SPI_MODE0));
  digitalWrite(9, LOW); // FPGA HOST SSN
  for (int i = 0; i < len; i++) {
    data[i] = SPI.transfer(0);
  }
  digitalWrite(9, HIGH); // FPGA HOST SSN
  SPI.endTransaction();
}


////////// Image functions

void writeHeaderPGM(int w, int h) {
  imgfile.print("P5\n");
  imgfile.print(w);
  imgfile.print(" ");
  imgfile.print(h);
  imgfile.print("\n255\n");
}

void writePGM(int w, int h) {
  writeHeaderPGM(w, h);
  unsigned char pat[4] = {0x40,0x80,0xc0,0xa0};
  for (int i=0; i<h; i++) {
    for (int j=0; j<w; j+=4)
      imgfile.write(pat,4);
  }
}

// Convert raw Bayer to RGB stored in channel/row/column order for CNN
// RGB values are scaled to 0-127 for CNN which expects int8_t values

void debayer(uint8_t *buf, int w, int h, int stride, uint8_t *out, int plane) {
  int x, y, dx, dy, dp;
  uint8_t *outp, *inp;
  uint8_t r,g,b;

  outp = (unsigned char *)out;
  dx = stride<<1;
  dy = stride<<1;
  for (y = 0; y < h; y+=dy) {
    inp = (unsigned char *)buf + y*w;
    for (x = 0; x < w; x+=dx) {
      // Bayer to RGB
      b = *inp;
      g = ((int)*(inp+1) + (int)*(inp+w))/2;
      r = *(inp+w+1);
      inp+=dx;
      // Right R/G/B planes
      *outp = (r>>1);
      *(outp+plane) = (g>>1);
      *(outp+2*plane) = (b>>1);
      outp += 1;
    }
  }
}

// Compute site scores
// x,y - top-left corner in image coordinates (i.e. 320x320)
// sz - length of side

int score_site(int8_t *activation, int y, int x, int sz) {
  int total = 0;
  int d = (sz * 36) / 320;
  int x0 = (x * 36) / 320;
  int x1 = x0 + d;
  int y0 = (y * 36) / 320;
  int y1 = y0 + d;
  for (int i = y0; i <= y1; i++) {
    for (int j = x0; j <= x1; j++) {
      total += activation[36*i + j];
    }
  }
  return total;
}


////////// Misc functions

void halt() {
  while (1) delay(1000);
}

