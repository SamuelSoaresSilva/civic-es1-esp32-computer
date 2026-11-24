#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "boot_animacao.h"
#include "botao_painel.h"
#include "fonte_painel.h"

constexpr uint8_t CLK = 12;
constexpr uint8_t DATA = 11;
constexpr uint8_t CS = 10;
constexpr uint8_t DC = 13;
constexpr uint8_t RESET = 14;
constexpr uint8_t SWITCH_PIN = 4;
constexpr uint32_t SPI_HZ = 4000000;
constexpr int WIDTH = 256;
constexpr int HEIGHT = 64;
constexpr int DIVIDER_X = 110;
constexpr int GRAPH_X = 116;
constexpr int GRAPH_BOTTOM = 61;
constexpr int GRAPH_HEIGHT = 51;
constexpr int BAR_COUNT = 35;
constexpr int BAR_STEP = 4;
constexpr int BAR_WIDTH = 2;
constexpr int MIN_GRAY = 2;  // 2/15: primeiro nível acima de 10% da escala.
constexpr int RPM_MAX = 8000;
constexpr uint32_t SAMPLE_MS = 100;

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI oled(U8G2_R0, CLK, DATA, CS, DC, RESET);
static_assert(sizeof(u8g2_uint_t) >= 2, "SSD1322 256x64 requer U8G2_16BIT");
uint8_t pixels[WIDTH * HEIGHT / 2];
static_assert(sizeof(pixels) == BOOT_FRAME_BYTES, "Quadro de boot incompatível com o OLED");
Leitura history[BAR_COUNT];
size_t nextSample = 0;
uint32_t startedAt;
uint32_t lastSampleAt;
BotaoPainel botao;
bool telaVelocidade = false;
AmostraPainel amostraAtual;

void sendCommand(uint8_t command, const uint8_t *args, size_t size) {
  digitalWrite(DC, LOW);
  SPI.transfer(command);
  digitalWrite(DC, HIGH);
  if (size != 0) {
    SPI.writeBytes(args, size);
  }
}

void sendFrame() {
  // A janela do perfil NHD começa em 0x1c e endereça quatro pixels por coluna.
  const uint8_t columns[] = {0x1c, 0x5b};
  const uint8_t rows[] = {0, HEIGHT - 1};
  SPI.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
  digitalWrite(CS, LOW);
  sendCommand(0x15, columns, sizeof(columns));
  sendCommand(0x75, rows, sizeof(rows));
  sendCommand(0x5c, nullptr, 0);
  SPI.writeBytes(pixels, sizeof(pixels));
  digitalWrite(CS, HIGH);
  SPI.endTransaction();
}

void playBootAnimation() {
  memset(pixels, 0, sizeof(pixels));
  const uint8_t *input = bootDelta;
  const uint8_t *const end = bootDelta + sizeof(bootDelta);
  for (size_t frame = 0; frame < sizeof(bootDurationMs) / sizeof(bootDurationMs[0]); ++frame) {
    const uint32_t frameStartedAt = millis();
    size_t position = 0;
    while (position < sizeof(pixels)) {
      if (input == end) return;
      const uint8_t control = pgm_read_byte(input++);
      const size_t length = (control & 0x7f) + 1;
      if (length > sizeof(pixels) - position) return;
      if (control & 0x80) {
        if (length > static_cast<size_t>(end - input)) return;
        memcpy_P(pixels + position, input, length);
        input += length;
      }
      position += length;
    }
    sendFrame();

    const uint32_t duration = pgm_read_word(&bootDurationMs[frame]);
    const uint32_t elapsed = millis() - frameStartedAt;
    if (elapsed < duration) delay(duration - elapsed);
  }
}

void setPixel(int x, int y, uint8_t level) {
  uint8_t &pair = pixels[y * (WIDTH / 2) + x / 2];
  if (x % 2 == 0) {
    pair = (pair & 0x0f) | (level << 4);
  } else {
    pair = (pair & 0xf0) | level;
  }
}

void fillRect(int x, int y, int width, int height, uint8_t level) {
  for (int row = y; row < y + height; ++row) {
    for (int col = x; col < x + width; ++col) {
      setPixel(col, row, level);
    }
  }
}

void drawDigits(int x, int y, const char *text, int scale, uint8_t level) {
  // Bits em ordem dos segmentos A, B, C, D, E, F e G.
  constexpr uint8_t masks[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66,
                               0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  constexpr uint8_t segments[][4] = {
      {1, 0, 4, 1}, {5, 1, 1, 4}, {5, 6, 1, 4}, {1, 10, 4, 1},
      {0, 6, 1, 4}, {0, 1, 1, 4}, {1, 5, 4, 1},
  };

  for (; *text; ++text) {
    if (*text == '-') {
      fillRect(x + scale, y + 5 * scale, 4 * scale, scale, level);
      x += 8 * scale;
      continue;
    }
    const uint8_t mask = masks[*text - '0'];
    for (int i = 0; i < 7; ++i) {
      if (mask & (1 << i)) {
        const auto &rect = segments[i];
        fillRect(x + rect[0] * scale, y + rect[1] * scale,
                 rect[2] * scale, rect[3] * scale, level);
      }
    }
    x += 8 * scale;
  }
}

void formatValue(const Leitura &leitura, char (&text)[7]) {
  if (leitura.valida) {
    snprintf(text, sizeof(text), "%d", static_cast<int>(leitura.valor));
  } else {
    strcpy(text, "--");
  }
}

void recordSample(uint32_t elapsedMs) {
  amostraAtual = lerSimulacao(elapsedMs);
  history[nextSample] = amostraAtual.rpm;
  nextSample = (nextSample + 1) % BAR_COUNT;
}

void copyTextMask(int width, int brightTop, int brightBottom) {
  // A U8g2 fornece a máscara de texto para o quadro em tons de cinza.
  const uint8_t *textMask = oled.getBufferPtr();
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < width; ++x) {
      if (textMask[(y / 8) * WIDTH + x] & (1 << (y % 8))) {
        setPixel(x, y, y >= brightTop && y < brightBottom ? 15 : 10);
      }
    }
  }
}

void drawSpeedPanel(const AmostraPainel &amostra) {
  char value[7];
  formatValue(amostra.velocidade, value);
  drawDigits(4, 6, value, 4, 15);

  fillRect(198, 3, 1, 59, 3);
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawUTF8(4, 62, amostra.velocidade.unidade);
  formatValue(amostra.rpm, value);
  oled.drawStr(207, 15, value);
  oled.drawUTF8(207, 26, amostra.rpm.unidade);
  formatValue(amostra.temperatura, value);
  oled.drawStr(207, 44, value);
  oled.drawUTF8(207, 55, amostra.temperatura.unidade);
  copyTextMask(WIDTH, 0, 0);
}

void drawPanel(const AmostraPainel &amostra) {
  memset(pixels, 0, sizeof(pixels));
  if (telaVelocidade) {
    drawSpeedPanel(amostra);
    sendFrame();
    return;
  }
  for (int y = 3; y <= GRAPH_BOTTOM; ++y) {
    setPixel(DIVIDER_X, y, 3);
  }
  for (int x = GRAPH_X; x < WIDTH - 2; ++x) {
    setPixel(x, GRAPH_BOTTOM, 3);
  }

  for (int column = 0; column < BAR_COUNT; ++column) {
    const Leitura &rpm = history[(nextSample + column) % BAR_COUNT];
    if (!rpm.valida) {
      continue;
    }
    const int height = max(1, constrain(static_cast<int>(rpm.valor), 0, RPM_MAX)
                                  * GRAPH_HEIGHT / RPM_MAX);
    const int left = GRAPH_X + column * BAR_STEP;
    const int maxGray = column == BAR_COUNT - 1 ? 15 : 14;
    const int steps = max(1, height - 1);
    const int stepsSquared = steps * steps;
    for (int y = GRAPH_BOTTOM - height; y < GRAPH_BOTTOM; ++y) {
      // A curva quadrática mantém a base escura e concentra o brilho no topo.
      const int rise = GRAPH_BOTTOM - 1 - y;
      const uint8_t level =
          MIN_GRAY + (rise * rise * (maxGray - MIN_GRAY) + stepsSquared / 2) / stepsSquared;
      for (int x = left; x < left + BAR_WIDTH; ++x) {
        setPixel(x, y, level);
      }
    }
  }

  char value[7];
  formatValue(amostra.rpm, value);
  drawDigits(3, 13, value, 2, 15);
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawUTF8(3, 10, amostra.rpm.unidade);
  oled.drawUTF8(3, 62, amostra.velocidade.unidade);
  oled.drawUTF8(57, 62, amostra.temperatura.unidade);

  oled.setFont(u8g2_font_9x15_tr);
  formatValue(amostra.velocidade, value);
  oled.drawStr(3, 51, value);
  formatValue(amostra.temperatura, value);
  oled.drawStr(57, 51, value);

  copyTextMask(DIVIDER_X, 38, 52);
  sendFrame();
}

void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  oled.begin();
  SPI.begin(CLK, -1, DATA, -1);
  playBootAnimation();
  startedAt = millis();
  lastSampleAt = startedAt;
  recordSample(0);
  drawPanel(amostraAtual);
}

void loop() {
  const uint32_t now = millis();
  const bool eraTelaVelocidade = telaVelocidade;
  const AcaoBotao acao = botao.atualizar(digitalRead(SWITCH_PIN) == LOW, now);
  if (acao == AcaoBotao::Clique) {
    telaVelocidade = !telaVelocidade;
  } else if (acao == AcaoBotao::PressaoLonga) {
    telaVelocidade = false;
  }

  const bool novaAmostra = now - lastSampleAt >= SAMPLE_MS;
  if (novaAmostra) {
    lastSampleAt = now;
    recordSample(now - startedAt);
  }
  if (novaAmostra || telaVelocidade != eraTelaVelocidade) {
    drawPanel(amostraAtual);
  }
}
