#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "boot_animacao.h"
#include "botao_painel.h"
#include "fonte_painel.h"
#include "golfinhos_animacao.h"

constexpr uint8_t CLK = 12;
constexpr uint8_t DATA = 11;
constexpr uint8_t CS = 10;
constexpr uint8_t DC = 13;
constexpr uint8_t RESET = 14;
constexpr uint8_t SWITCH_PIN = 4;
constexpr uint32_t SPI_HZ = 4000000;
constexpr int WIDTH = 256;
constexpr int HEIGHT = 64;
constexpr int GRAPH_LEFT = 78;
constexpr int GRAPH_RIGHT = WIDTH - 3;
constexpr int HISTORY_SIZE = 35;
constexpr int BAR_WIDTH = 3;
constexpr int THIN_BAR_WIDTH = 2;
constexpr int BAR_STEP = 5;
constexpr int BARS_LEFT = GRAPH_RIGHT - (HISTORY_SIZE - 1) * BAR_STEP - BAR_WIDTH + 1;
static_assert(BARS_LEFT >= GRAPH_LEFT, "As barras de RPM precisam caber no grafico");
constexpr int MIN_GRAY = 2;  // 2/15: primeiro nível acima de 10% da escala.
constexpr int RPM_MAX = 8000;
constexpr int SHIFT_RPM = 6000;
constexpr uint32_t SHIFT_HALF_PERIOD_MS = 200;
constexpr uint32_t SAMPLE_MS = 100;
constexpr int DOLPHIN_WIDTH = 128;
constexpr int DOLPHIN_HEIGHT = 33;

U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI oled(U8G2_R0, CLK, DATA, CS, DC, RESET);
static_assert(sizeof(u8g2_uint_t) >= 2, "SSD1322 256x64 requer U8G2_16BIT");
uint8_t pixels[WIDTH * HEIGHT / 2];
static_assert(sizeof(pixels) == BOOT_FRAME_BYTES, "Quadro de boot incompatível com o OLED");
static_assert(DOLPHIN_WIDTH * DOLPHIN_HEIGHT / 2 == GOLFINHOS_FRAME_BYTES &&
              DOLPHIN_WIDTH * 2 == WIDTH, "Quadro dos golfinhos incompatível com o OLED");
uint8_t dolphinPixels[GOLFINHOS_FRAME_BYTES];
AmostraPainel history[HISTORY_SIZE];
size_t nextSample = 0;
uint32_t startedAt;
uint32_t lastSampleAt;
BotaoPainel botao;
enum class Tela {
  Geral,
  Giro,
  Temperaturas,
  Sonda,
  CorrecoesCombustivel,
  Tensao,
  Avanco,
  Apresentacao,
  Golfinhos,
  Total,
};
Tela telaAtual = Tela::Geral;
constexpr FontePainel fonte = lerSimulacao;
AmostraPainel amostraAtual;
bool shiftAtivo = false;
bool shiftIluminado = false;
uint32_t shiftInicio;

struct ClipeAnimacao {
  const uint8_t *delta;
  const uint8_t *end;
  const uint16_t *durations;
  size_t frameCount;
  uint8_t *buffer;
  size_t frameBytes;
};

const ClipeAnimacao clipeBoot = {
  bootDelta, bootDelta + sizeof(bootDelta), bootDurationMs,
  sizeof(bootDurationMs) / sizeof(bootDurationMs[0]), pixels, sizeof(pixels),
};
const ClipeAnimacao clipeGolfinhos = {
  golfinhosDelta, golfinhosDelta + sizeof(golfinhosDelta), golfinhosDurationMs,
  sizeof(golfinhosDurationMs) / sizeof(golfinhosDurationMs[0]), dolphinPixels, sizeof(dolphinPixels),
};

struct EstadoAnimacao {
  const uint8_t *input;
  size_t nextFrame;
  uint32_t frameStartedAt;
  uint32_t frameDuration;
};

EstadoAnimacao animacao;

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

void expandDolphinFrame() {
  for (int y = 0; y < HEIGHT; ++y) {
    const uint8_t *source = dolphinPixels + (y * DOLPHIN_HEIGHT / HEIGHT) * (DOLPHIN_WIDTH / 2);
    uint8_t *target = pixels + y * (WIDTH / 2);
    for (int x = 0; x < DOLPHIN_WIDTH / 2; ++x) {
      const uint8_t pair = source[x];
      target[x * 2] = (pair & 0xf0) | (pair >> 4);
      target[x * 2 + 1] = (pair & 0x0f) | (pair << 4);
    }
  }
}

void resetAnimation(const ClipeAnimacao &clipe, uint32_t now) {
  memset(clipe.buffer, 0, clipe.frameBytes);
  animacao = {clipe.delta, 0, now, 0};
}

bool updateAnimation(const ClipeAnimacao &clipe, uint32_t now, bool repeat) {
  if (now - animacao.frameStartedAt < animacao.frameDuration) {
    return true;
  }
  if (animacao.nextFrame == clipe.frameCount) {
    if (!repeat) {
      return false;
    }
    // O primeiro quadro é um delta em relação a uma tela vazia.
    resetAnimation(clipe, now);
  }

  size_t position = 0;
  while (position < clipe.frameBytes) {
    if (animacao.input == clipe.end) return false;
    const uint8_t control = pgm_read_byte(animacao.input++);
    const size_t length = (control & 0x7f) + 1;
    if (length > clipe.frameBytes - position) return false;
    if (control & 0x80) {
      if (length > static_cast<size_t>(clipe.end - animacao.input)) return false;
      memcpy_P(clipe.buffer + position, animacao.input, length);
      animacao.input += length;
    }
    position += length;
  }
  if (clipe.buffer == dolphinPixels) {
    expandDolphinFrame();
  }
  sendFrame();
  animacao.frameStartedAt = now;
  animacao.frameDuration = pgm_read_word(&clipe.durations[animacao.nextFrame++]);
  return true;
}

void playBootAnimation() {
  constexpr int voltas = 2;
  for (int volta = 0; volta < voltas; ++volta) {
    resetAnimation(clipeBoot, millis());
    while (updateAnimation(clipeBoot, millis(), false)) {
      const uint32_t elapsed = millis() - animacao.frameStartedAt;
      if (elapsed < animacao.frameDuration) delay(animacao.frameDuration - elapsed);
    }
    if (animacao.nextFrame != clipeBoot.frameCount) return;
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

int digitAdvance(char character) {
  return character == '.' ? 3 : 8;
}

int digitsWidth(const char *text) {
  int width = 0;
  for (; *text; ++text) {
    width += digitAdvance(*text);
  }
  return width;
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
    } else if (*text == '.') {
      fillRect(x, y + 10 * scale, 2 * scale, scale, level);
    } else if (*text >= '0' && *text <= '9') {
      const uint8_t mask = masks[*text - '0'];
      for (int i = 0; i < 7; ++i) {
        if (mask & (1 << i)) {
          const auto &rect = segments[i];
          fillRect(x + rect[0] * scale, y + rect[1] * scale,
                   rect[2] * scale, rect[3] * scale, level);
        }
      }
    }
    x += digitAdvance(*text) * scale;
  }
}

void drawShiftLight(bool iluminado) {
  if (!iluminado) {
    memset(pixels, 0, sizeof(pixels));
    sendFrame();
    return;
  }

  constexpr int diagonal = (WIDTH - 1) + (HEIGHT - 1) * 4;
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
      const uint8_t brilho = 4 + (x + y * 4) * 11 / diagonal;
      setPixel(x, y, brilho);
    }
  }

  constexpr uint8_t letras[][7] = {
      {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110},  // S
      {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},  // H
      {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111},  // I
      {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000},  // F
      {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},  // T
  };
  constexpr int escala = 7;
  constexpr int quantidade = sizeof(letras) / sizeof(letras[0]);
  constexpr int esquerda = (WIDTH - (quantidade * 6 - 1) * escala) / 2;
  constexpr int topo = (HEIGHT - 7 * escala) / 2;
  for (int letra = 0; letra < quantidade; ++letra) {
    for (int linha = 0; linha < 7; ++linha) {
      for (int coluna = 0; coluna < 5; ++coluna) {
        if (letras[letra][linha] & (1 << (4 - coluna))) {
          fillRect(esquerda + (letra * 6 + coluna) * escala, topo + linha * escala,
                   escala, escala, 0);
        }
      }
    }
  }
  sendFrame();
}

void drawBars(Leitura AmostraPainel::*signal, int top, int bottom,
              float minimum, float maximum, int barWidth = BAR_WIDTH) {
  const auto row = [=](float value) {
    const float ratio = constrain((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
    return bottom - static_cast<int>(roundf(ratio * (bottom - top)));
  };
  const bool signedRange = minimum < 0 && maximum > 0;
  const int baseline = signedRange ? row(0) : bottom;
  if (signedRange) {
    fillRect(GRAPH_RIGHT + 1, baseline, WIDTH - GRAPH_RIGHT - 1, 1, 8);
  }

  for (int column = 0; column < HISTORY_SIZE; ++column) {
    const Leitura &reading = history[(nextSample + column) % HISTORY_SIZE].*signal;
    if (!reading.valida || !isfinite(reading.valor)) {
      continue;
    }
    const int left = BARS_LEFT + column * BAR_STEP + (BAR_WIDTH - barWidth);
    const int maxGray = column == HISTORY_SIZE - 1 ? 15 : 14;
    const int tip = row(reading.valor);
    if (signedRange && tip == baseline) {
      fillRect(left, baseline, barWidth, 1, maxGray);
      continue;
    }
    const int direction = tip > baseline ? 1 : -1;
    const int height = max(1, abs(tip - baseline));
    const int steps = height - 1;
    const int stepsSquared = steps * steps;
    for (int rise = 0; rise < height; ++rise) {
      // O brilho aumenta em direção à ponta da barra.
      const uint8_t level = steps == 0 ? maxGray :
          MIN_GRAY + (rise * rise * (maxGray - MIN_GRAY) + stepsSquared / 2) / stepsSquared;
      fillRect(left, baseline + direction * (rise + 1), barWidth, 1, level);
    }
  }
}

void drawValue(int x, int y, int right, const Leitura &reading, const char *unit,
               int decimals, int scale) {
  char value[48];
  bool valid = reading.valida && isfinite(reading.valor);
  if (valid) {
    snprintf(value, sizeof(value), "%.*f", decimals, reading.valor);
  } else {
    strcpy(value, "--");
  }

  oled.setFont(u8g2_font_6x12_tf);
  const int unitSpace = valid && *unit ? 2 + oled.getUTF8Width(unit) : 0;
  const int numberWidth = digitsWidth(value) - 2;  // Remove o espaço após o último dígito.
  if (x + numberWidth * scale + unitSpace > right) {
    scale = 1;
  }
  if (x + numberWidth * scale + unitSpace > right) {
    strcpy(value, "--");
    valid = false;
  }
  drawDigits(x, y, value, scale, 15);
  if (valid && *unit) {
    oled.drawUTF8(x + numberWidth * scale + 2, y + 11 * scale - 1, unit);
  }
}

void drawMetric(int x, int top, int right, const char *label, const Leitura &reading,
                const char *unit, int decimals = 0, int scale = 1) {
  oled.setFont(u8g2_font_6x12_tf);
  oled.drawUTF8(x, top + 10, label);
  drawValue(x, top + 12, right, reading, unit, decimals, scale);
}

constexpr char rpmIcon[][17] = {
    "................",
    ".....######.....",
    "...##......##...",
    "..#....+.....#..",
    ".#..+.....+...#.",
    ".#.........#..#.",
    "#.+.......#....#",
    "#........#.....#",
    "#......##....+.#",
    "#.+....##......#",
    ".#............#.",
    ".#....++++....#.",
    "..#..........#..",
    "...##########...",
    "................",
    "................",
};

constexpr char coolantIcon[][17] = {
    "................",
    "......###.......",
    ".....#...#......",
    ".....#...#.##...",
    ".....#.+.#......",
    ".....#.+.#.##...",
    ".....#.+.#......",
    "....#..+..#.....",
    "....#..+..#.....",
    ".....#####......",
    "................",
    ".##...##...##...",
    "#..###..###..##.",
    "................",
    ".##...##...##...",
    "#..###..###..##.",
};

constexpr char throttleIcon[][17] = {
    "................",
    ".....######.....",
    "...##......##...",
    "..#..........#..",
    "..#.......##.#..",
    ".#.......##...#.",
    ".#......##....#.",
    "##.....##.....##",
    "##....##......##",
    ".#...##.......#.",
    ".#.###........#.",
    "..###........#..",
    "..#..........#..",
    "...##......##...",
    ".....######.....",
    "................",
};

constexpr char batteryIcon[][17] = {
    "................",
    "................",
    "...###....###...",
    "...###....###...",
    ".##############.",
    ".#............#.",
    ".#........+...#.",
    ".#........+...#.",
    ".#.+++..+++++.#.",
    ".#........+...#.",
    ".#........+...#.",
    ".#............#.",
    ".##############.",
    "................",
    "................",
    "................",
};

void drawOverviewIcon(int x, int y, const char (&icon)[16][17]) {
  for (int row = 0; row < 16; ++row) {
    for (int col = 0; col < 16; ++col) {
      if (icon[row][col] == '#') {
        setPixel(x + col, y + row, 13);
      } else if (icon[row][col] == '+') {
        setPixel(x + col, y + row, 9);
      }
    }
  }
}

void drawPanel(Tela tela, const AmostraPainel &amostra) {
  memset(pixels, 0, sizeof(pixels));
  oled.clearBuffer();
  switch (tela) {
    case Tela::Geral:
      drawOverviewIcon(4, 5, rpmIcon);
      drawOverviewIcon(132, 5, coolantIcon);
      drawOverviewIcon(4, 39, throttleIcon);
      drawOverviewIcon(132, 39, batteryIcon);
      drawMetric(26, 0, 126, "RPM", amostra.rpm, "");
      drawMetric(154, 0, WIDTH, "ARREFECIMENTO", amostra.arrefecimento, "\xC2\xB0" "C");
      drawMetric(26, 34, 126, "BORBOLETA", amostra.borboleta, "%");
      drawMetric(154, 34, WIDTH, "BATERIA", amostra.tensao, "V", 2);
      break;
    case Tela::Giro: {
      Leitura rpm = amostra.rpm;
      if (isfinite(rpm.valor)) {
        rpm.valor = constrain(rpm.valor, 0.0f, 9999.0f);
      }
      drawBars(&AmostraPainel::rpm, 10, 61, 0, RPM_MAX);
      drawMetric(3, 1, GRAPH_LEFT, "RPM", rpm, "", 0, 2);
      drawMetric(3, 40, GRAPH_LEFT, "BORBOLETA", amostra.borboleta, "%");
      break;
    }
    case Tela::Temperaturas:
      drawMetric(3, 1, GRAPH_LEFT, "AR ADMISSÃO", amostra.arAdmissao, "\xC2\xB0" "C");
      drawMetric(3, 33, GRAPH_LEFT, "LÍQUIDO", amostra.arrefecimento, "\xC2\xB0" "C");
      drawBars(&AmostraPainel::arAdmissao, 3, 30, -20, 80, THIN_BAR_WIDTH);
      drawBars(&AmostraPainel::arrefecimento, 34, 61, 0, 120, THIN_BAR_WIDTH);
      break;
    case Tela::Sonda:
      drawMetric(3, 15, GRAPH_LEFT, "SONDA", amostra.sonda, "V", 2, 2);
      drawBars(&AmostraPainel::sonda, 6, 61, 0, 1);
      break;
    case Tela::CorrecoesCombustivel:
      drawMetric(3, 1, GRAPH_LEFT, "STFT", amostra.ajusteCurto, "%", 1);
      drawMetric(3, 33, GRAPH_LEFT, "LTFT", amostra.ajusteLongo, "%", 1);
      drawBars(&AmostraPainel::ajusteCurto, 3, 30, -25, 25, THIN_BAR_WIDTH);
      drawBars(&AmostraPainel::ajusteLongo, 34, 61, -25, 25, THIN_BAR_WIDTH);
      break;
    case Tela::Tensao:
      drawMetric(3, 15, GRAPH_LEFT, "BATERIA", amostra.tensao, "V", 2, 2);
      drawBars(&AmostraPainel::tensao, 6, 61, 10, 16);
      break;
    case Tela::Avanco:
      drawMetric(3, 15, GRAPH_LEFT, "AVANÇO", amostra.avanco, "\xC2\xB0", 1, 2);
      drawBars(&AmostraPainel::avanco, 6, 61, -15, 50);
      break;
    case Tela::Apresentacao:
    case Tela::Golfinhos:
    case Tela::Total:
      break;
  }

  // A U8g2 fornece a mascara de texto para o quadro em tons de cinza.
  const uint8_t *mask = oled.getBufferPtr();
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
      if (mask[(y / 8) * WIDTH + x] & (1 << (y % 8))) {
        setPixel(x, y, 15);
      }
    }
  }
  sendFrame();
}

void recordSample(const AmostraPainel &amostra) {
  amostraAtual = amostra;
  history[nextSample] = amostra;
  nextSample = (nextSample + 1) % HISTORY_SIZE;
}

void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  oled.begin();
  SPI.begin(CLK, -1, DATA, -1);
  playBootAnimation();
  startedAt = millis();
  lastSampleAt = startedAt;
  recordSample(fonte(0));
  drawPanel(telaAtual, amostraAtual);
}

void loop() {
  const uint32_t now = millis();
  const Tela anterior = telaAtual;
  const AcaoBotao acao = botao.atualizar(digitalRead(SWITCH_PIN) == LOW, now);
  if (acao == AcaoBotao::Clique) {
    telaAtual = static_cast<Tela>((static_cast<int>(telaAtual) + 1) % static_cast<int>(Tela::Total));
  } else if (acao == AcaoBotao::PressaoLonga) {
    telaAtual = Tela::Geral;
  }

  const bool novaAmostra = now - lastSampleAt >= SAMPLE_MS;
  if (novaAmostra) {
    lastSampleAt = now;
    recordSample(fonte(now - startedAt));
  }

  const bool precisaShift = telaAtual == Tela::Giro && amostraAtual.rpm.valida &&
                           amostraAtual.rpm.valor > SHIFT_RPM;
  const bool mudouShift = precisaShift != shiftAtivo;
  shiftAtivo = precisaShift;
  if (shiftAtivo) {
    if (mudouShift) {
      shiftInicio = now;
    }
    const bool iluminado = ((now - shiftInicio) / SHIFT_HALF_PERIOD_MS) % 2 == 0;
    if (mudouShift || iluminado != shiftIluminado) {
      shiftIluminado = iluminado;
      drawShiftLight(iluminado);
    }
    return;
  }

  if (telaAtual == Tela::Apresentacao || telaAtual == Tela::Golfinhos) {
    const ClipeAnimacao &clipe = telaAtual == Tela::Apresentacao ? clipeBoot : clipeGolfinhos;
    if (anterior != telaAtual) {
      resetAnimation(clipe, now);
    }
    if (updateAnimation(clipe, now, true)) {
      return;
    }
    telaAtual = Tela::Geral;
  }
  if (novaAmostra || telaAtual != anterior || mudouShift) {
    drawPanel(telaAtual, amostraAtual);
  }
}
