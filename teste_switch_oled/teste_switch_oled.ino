#include <Arduino.h>
#include <U8g2lib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

constexpr uint8_t SWITCH_PIN = 4;
constexpr uint8_t OLED_CLK = 12;
constexpr uint8_t OLED_MOSI = 11;
constexpr uint8_t OLED_CS = 10;
constexpr uint8_t OLED_DC = 13;
constexpr uint8_t OLED_RESET = 14;
constexpr uint32_t DEBOUNCE_MS = 5;

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI display(
    U8G2_R0, OLED_CLK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RESET);

static_assert(sizeof(u8g2_uint_t) >= 2, "SSD1322 256x64 requer U8G2_16BIT");

struct SwitchStatus {
  int state;
  uint32_t pressCount;
};

void drawStatus(const SwitchStatus& status);

QueueHandle_t switchUpdates;

void drawStatus(const SwitchStatus& status) {
  char counter[32];
  snprintf(counter, sizeof(counter), "Apertos: %lu",
           static_cast<unsigned long>(status.pressCount));

  display.clearBuffer();
  display.drawFrame(0, 0, 256, 64);
  display.setFont(u8g2_font_6x12_tr);
  display.drawStr(12, 15, "Teste do switch | Pino 4");
  display.setFont(u8g2_font_10x20_tr);
  display.drawStr(12, 37, status.state == LOW ? "PRESSIONADO" : "SOLTO");
  display.setFont(u8g2_font_6x12_tr);
  display.drawStr(12, 55, counter);
  display.sendBuffer();
}

void readSwitch(void*) {
  int lastReading = digitalRead(SWITCH_PIN);
  SwitchStatus status{lastReading, 0};
  uint32_t lastChange = millis();
  xQueueOverwrite(switchUpdates, &status);

  for (;;) {
    const int reading = digitalRead(SWITCH_PIN);
    const uint32_t now = millis();
    if (reading != lastReading) {
      lastReading = reading;
      lastChange = now;
    }

    if (reading != status.state && now - lastChange >= DEBOUNCE_MS) {
      status.state = reading;
      if (status.state == LOW) {
        ++status.pressCount;
      }
      // O total acumula todos os apertos mesmo se o OLED pular um estado.
      xQueueOverwrite(switchUpdates, &status);
    }
    vTaskDelay(1);
  }
}

void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  display.begin();
  switchUpdates = xQueueCreate(1, sizeof(SwitchStatus));
  if (switchUpdates == nullptr ||
      xTaskCreate(readSwitch, "switch", 2048, nullptr, 2, nullptr) != pdPASS) {
    if (switchUpdates != nullptr) {
      vQueueDelete(switchUpdates);
      switchUpdates = nullptr;
    }
    display.setFont(u8g2_font_6x12_tr);
    display.drawStr(12, 25, "Erro ao iniciar leitura");
    display.sendBuffer();
  }
}

void loop() {
  if (switchUpdates == nullptr) {
    delay(1000);
    return;
  }

  SwitchStatus status;
  if (xQueueReceive(switchUpdates, &status, pdMS_TO_TICKS(100)) == pdTRUE) {
    drawStatus(status);
  }
}
