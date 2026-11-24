#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>

constexpr uint8_t CLK = 12;
constexpr uint8_t MOSI_PIN = 11;
constexpr uint8_t CS = 10;
constexpr uint8_t DC = 13;
constexpr uint8_t RESET = 14;
constexpr size_t WIDTH = 256;
constexpr size_t HEIGHT = 64;
constexpr size_t ROW_BYTES = WIDTH / 2;
constexpr uint8_t FIRST_COL = 0x1c;  // Offset do perfil NHD.
constexpr uint8_t LAST_COL = FIRST_COL + WIDTH / 4 - 1;
constexpr uint32_t SPI_RATES[] = {2000000, 4000000, 8000000, 10000000};

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI oled(U8G2_R0, CLK, MOSI_PIN, CS, DC, RESET);
uint8_t frame[HEIGHT][ROW_BYTES];

void report(const char *mode, uint32_t hz, uint32_t count, uint32_t elapsed) {
  Serial.printf("%s | SPI %.1f MHz | %.1f quadros/s | %.2f ms/quadro\n",
                mode, hz / 1000000.0, count * 1000000.0 / elapsed,
                elapsed / (count * 1000.0));
}

void command(uint8_t cmd, const uint8_t *args, size_t length) {
  digitalWrite(DC, LOW);
  SPI.transfer(cmd);
  digitalWrite(DC, HIGH);
  if (length != 0) {
    SPI.writeBytes(args, length);
  }
}

void sendGray(uint32_t hz) {
  const uint8_t cols[] = {FIRST_COL, LAST_COL};
  const uint8_t rows[] = {0, HEIGHT - 1};
  SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
  digitalWrite(CS, LOW);
  command(0x15, cols, sizeof(cols));
  command(0x75, rows, sizeof(rows));
  command(0x5c, nullptr, 0);
  SPI.writeBytes(&frame[0][0], sizeof(frame));
  digitalWrite(CS, HIGH);
  SPI.endTransaction();
}

void setup() {
  Serial.begin(115200);
  oled.begin();  // Reutiliza a inicialização que já funcionou no painel.

  for (size_t y = 0; y < HEIGHT - 8; ++y) {
    for (size_t x = 0; x < ROW_BYTES; ++x) {
      const uint8_t gray = x / 8;
      frame[y][x] = (gray << 4) | gray;
    }
  }

  SPI.begin(CLK, -1, MOSI_PIN, -1);
}

void loop() {
  static size_t marker = 0;
  for (uint32_t hz : SPI_RATES) {
    Serial.printf("Testando SPI a %.1f MHz\n", hz / 1000000.0);
    uint32_t count = 0;
    const uint32_t started = micros();
    uint32_t elapsed;
    do {
      for (size_t y = HEIGHT - 8; y < HEIGHT; ++y) {
        frame[y][marker] = 0;
      }
      marker = (marker + 1) % ROW_BYTES;
      for (size_t y = HEIGHT - 8; y < HEIGHT; ++y) {
        frame[y][marker] = 0xff;
      }
      sendGray(hz);
      ++count;
      elapsed = micros() - started;
    } while (elapsed < 2000000);
    report("Gray4", hz, count, elapsed);
    delay(1);
  }
}
