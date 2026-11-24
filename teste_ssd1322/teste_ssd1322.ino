#include <Arduino.h>
#include <U8g2lib.h>

// GPIOs expostos na placa ESP32-S3-N16R8 do projeto.
constexpr uint8_t OLED_CLK = 12;
constexpr uint8_t OLED_MOSI = 11;
constexpr uint8_t OLED_CS = 10;
constexpr uint8_t OLED_DC = 13;
constexpr uint8_t OLED_RESET = 14;

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI display(
    U8G2_R0, OLED_CLK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RESET);

static_assert(sizeof(u8g2_uint_t) >= 2, "SSD1322 256x64 requer U8G2_16BIT");

void setup() {
  Serial.begin(115200);
  display.begin();
  display.setFont(u8g2_font_6x12_tr);
  Serial.printf("SSD1322: %u x %u, SPI 4 fios por software\n",
                static_cast<unsigned>(display.getDisplayWidth()),
                static_cast<unsigned>(display.getDisplayHeight()));
}

void loop() {
  static uint32_t quadro = 0;
  char texto[32];
  snprintf(texto, sizeof(texto), "Quadro %lu", static_cast<unsigned long>(quadro));

  display.clearBuffer();
  display.drawFrame(0, 0, 256, 64);
  display.drawBox(4, 4, 8, 8);
  display.drawFrame(244, 52, 8, 8);
  display.drawStr(20, 21, "SSD1322  256 x 64");
  display.drawStr(20, 40, texto);

  // Percorre toda a largura interna sem sobrepor os marcadores.
  const uint32_t passo = quadro % 48;
  display.drawBox(4 + passo * 5, 47, 8, 4);
  display.sendBuffer();

  Serial.println(texto);
  ++quadro;
  delay(250);
}
