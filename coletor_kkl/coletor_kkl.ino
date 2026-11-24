#include <Arduino.h>
#include <EspUsbHost.h>
#include <LittleFS.h>
#include <U8g2lib.h>
#include <atomic>

// Mesma pinagem do teste SSD1322.
U8G2_SSD1322_NHD_256X64_F_4W_SW_SPI oled(U8G2_R0, 12, 11, 10, 13, 14);
constexpr uint16_t SAMPLE_TARGET = 300;
constexpr uint16_t FTDI_VID = 0x0403;
constexpr uint16_t FT232_PID = 0x6001;
constexpr uint8_t FTDI_SET_DATA = 4;
constexpr uint16_t FTDI_8N1 = 8;
constexpr uint16_t FTDI_BREAK = 0x4000;
constexpr uint8_t POLL_PIDS[] = {0x00, 0x0C, 0x0D, 0x0C, 0x05, 0x0D, 0x0C, 0x11, 0x04};

EspUsbHost usb;
EspUsbHostCdcSerial kkl(usb);
std::atomic<uint8_t> usbAddress{0};
std::atomic<uint32_t> usbId{0};
File logFile;
char path[24];
char lastTx[43];
char lastRx[43];
const char *status = "AGUARDANDO USB";
bool storageReady = false;
bool hostReady = false;
bool stopped = false;
bool sessionReady = false;
uint32_t bootSessionMs = 0;
size_t expectedFileSize = 0;
uint16_t samples = 0;
uint32_t retryAt = 0;
uint32_t nextQuery = 0;
uint32_t lastFlush = 0;
size_t pollIndex = 0;
uint8_t unanswered = 0;

bool deadlineReached(uint32_t deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

void drawStatus() {
  char line[43];
  oled.clearBuffer();
  oled.drawStr(0, 11, status);
  const uint32_t id = usbId.load();
  snprintf(line, sizeof(line), "%s | USB %04lX:%04lX",
           path[0] ? path + 1 : "CSV",
           static_cast<unsigned long>(id >> 16), static_cast<unsigned long>(id & 0xFFFF));
  oled.drawStr(0, 23, line);
  oled.drawStr(0, 35, lastTx);
  oled.drawStr(0, 47, lastRx);
  oled.drawFrame(0, 51, 168, 12);
  const uint16_t fill = uint32_t(samples) * 164 / SAMPLE_TARGET;
  if (fill) oled.drawBox(2, 53, fill, 8);
  snprintf(line, sizeof(line), "%u/%u", unsigned(samples), unsigned(SAMPLE_TARGET));
  oled.drawStr(176, 61, line);
  oled.sendBuffer();
}

void finish(const char *message) {
  if (logFile) {
    logFile.flush();
    logFile.close();
  }
  stopped = true;
  sessionReady = false;
  status = message;
}

void logRow(uint32_t at, const char *event, const uint8_t *bytes, size_t count) {
  if (!logFile) return;
  char hex[3 * 64 + 1] = {};
  for (size_t i = 0; i < count; ++i) {
    snprintf(hex + 3 * i, sizeof(hex) - 3 * i, "%02X%s", bytes[i],
             i + 1 == count ? "" : " ");
  }
  char row[240];
  const int length = snprintf(row, sizeof(row), "%lu,%s,%s\n",
                              static_cast<unsigned long>(at - bootSessionMs), event, hex);
  if (length < 0 || size_t(length) >= sizeof(row) ||
      logFile.write(reinterpret_cast<const uint8_t *>(row), length) != size_t(length)) {
    finish("ERRO GRAVANDO CSV");
    return;
  }
  expectedFileSize += length;
  if (!strcmp(event, "TX") || !strcmp(event, "INIT_TX") || !strcmp(event, "INIT_5BAUD")) {
    snprintf(lastTx, sizeof(lastTx), "TX %.39s", hex);
  }
  if (!strcmp(event, "RX") || !strcmp(event, "INIT_RX") || !strcmp(event, "TIMEOUT")) {
    snprintf(lastRx, sizeof(lastRx), "RX %.39s", count ? hex : "TIMEOUT");
  }
}

bool openLog() {
  if (logFile) return true;
  if (!storageReady) return false;
  for (unsigned i = 1; i <= 9999; ++i) {
    snprintf(path, sizeof(path), "/obd%04u.csv", i);
    if (LittleFS.exists(path)) continue;
    logFile = LittleFS.open(path, FILE_WRITE);
    constexpr char header[] = "ms,event,hex\n";
    if (!logFile || logFile.print(header) != sizeof(header) - 1) return false;
    bootSessionMs = lastFlush = millis();
    samples = 0;
    expectedFileSize = sizeof(header) - 1;
    lastTx[0] = lastRx[0] = '\0';
    return true;
  }
  return false;
}

size_t readBytes(uint8_t *buffer, size_t capacity, uint32_t firstTimeout, uint32_t gap) {
  size_t count = 0;
  uint32_t deadline = millis() + firstTimeout;
  while (count < capacity && !deadlineReached(deadline) && usbAddress.load()) {
    const int byte = kkl.read();
    if (byte < 0) {
      delay(1);
      continue;
    }
    buffer[count++] = byte;
    deadline = millis() + gap;
  }
  return count;
}

bool setBreak(uint8_t address, bool enabled) {
  return usb.vendorControlOut(FTDI_SET_DATA, FTDI_8N1 | (enabled ? FTDI_BREAK : 0),
                              1, nullptr, 0, address, 100);
}

bool slowInit(uint8_t address) {
  const uint8_t target = 0x33;
  uint8_t received[64];
  status = "ISO 9141 | INIT 5 BAUD";
  drawStatus();
  if (!kkl.setBaudRate(10400) || !setBreak(address, false)) return false;
  delay(300);
  unsigned drained = 0;
  while (drained < sizeof(received) && kkl.read() >= 0) ++drained;

  const uint32_t started = millis();
  // Quadro UART 8N1 de 0x33, com 200 ms por bit via BREAK do FT232.
  const uint16_t bits = (uint16_t(target) << 1) | 0x200;
  bool wasLow = false;
  for (unsigned i = 0; i < 10; ++i) {
    const bool low = !(bits & (1U << i));
    if (low != wasLow && !setBreak(address, low)) {
      setBreak(address, false);
      logRow(started, "INIT_USB_ERROR", nullptr, 0);
      return false;
    }
    wasLow = low;
    const uint32_t end = started + (i + 1) * 200;
    while (!deadlineReached(end)) delay(1);
  }

  // Ignora o eco do wake-up na procura pelo sync, mas o guarda no arquivo.
  size_t count = 0;
  uint32_t deadline = millis() + 350;
  uint8_t keys[2];
  size_t keyCount = 0;
  bool sync = false;
  while (count < sizeof(received) && !deadlineReached(deadline) && usbAddress.load()) {
    const int byte = kkl.read();
    if (byte < 0) {
      delay(1);
      continue;
    }
    received[count++] = byte;
    if (!sync) {
      sync = byte == 0x55;
    } else {
      keys[keyCount++] = byte;
      if (keyCount == sizeof(keys)) break;
    }
  }
  const uint32_t keysAt = millis();
  const bool isoKeys = keyCount == 2 && keys[0] == keys[1] &&
                       (keys[0] == 0x08 || keys[0] == 0x94);
  uint8_t ack = 0;
  bool sentAck = false;
  uint32_t ackAt = 0;
  uint8_t confirmation[64];
  size_t confirmedBytes = 0;
  if (isoKeys) {
    delay(30);
    ack = ~keys[1];
    ackAt = millis();
    sentAck = kkl.write(ack) == 1;
    if (sentAck) confirmedBytes = readBytes(confirmation, sizeof(confirmation), 100, 25);
  }
  const uint32_t confirmationAt = millis();
  logRow(started, "INIT_5BAUD", &target, 1);
  if (count) logRow(keysAt, "INIT_RX", received, count);
  if (sentAck) logRow(ackAt, "INIT_TX", &ack, 1);
  if (confirmedBytes) logRow(confirmationAt, "INIT_RX", confirmation, confirmedBytes);
  if (!confirmedBytes) logRow(confirmationAt, isoKeys ? "INIT_NO_ACK" : "INIT_NO_ISO_KEYS", nullptr, 0);
  for (size_t i = 0; i < confirmedBytes; ++i) {
    if (confirmation[i] == uint8_t(~target)) return true;
  }
  return false;
}

bool hasValidResponse(const uint8_t *received, size_t count, uint8_t pid) {
  const size_t dataBytes = pid == 0x00 ? 4 : (pid == 0x0C ? 2 : 1);
  const size_t frameSize = 3 + 2 + dataBytes + 1;
  for (size_t offset = 0; offset + frameSize <= count; ++offset) {
    const uint8_t *frame = received + offset;
    if (frame[0] != 0x48 || frame[1] != 0x6B || frame[3] != 0x41 || frame[4] != pid) continue;
    uint8_t checksum = 0;
    for (size_t i = 0; i + 1 < frameSize; ++i) checksum += frame[i];
    if (checksum == frame[frameSize - 1]) return true;
  }
  return false;
}

void queryPid() {
  const uint8_t pid = POLL_PIDS[pollIndex];
  pollIndex = (pollIndex + 1) % sizeof(POLL_PIDS);
  uint8_t request[] = {0x68, 0x6A, 0xF1, 0x01, pid, 0};
  for (size_t i = 0; i + 1 < sizeof(request); ++i) request[5] += request[i];
  const uint32_t sentAt = millis();
  if (kkl.write(request, sizeof(request)) != sizeof(request)) {
    logRow(sentAt, "TX_USB_ERROR", request, sizeof(request));
    sessionReady = false;
    retryAt = millis() + 3000;
    return;
  }

  uint8_t received[64];
  const size_t count = readBytes(received, sizeof(received), 150, 65);
  const uint32_t receivedAt = millis();
  logRow(sentAt, "TX", request, sizeof(request));
  if (count) logRow(receivedAt, "RX", received, count);
  if (stopped) return;
  if (count == sizeof(received)) {
    logRow(receivedAt, "RX_BUFFER_LIMIT", nullptr, 0);
    sessionReady = false;
    retryAt = millis() + 3000;
    if (!stopped) status = "LIMITE RX | VEJA CSV";
    return;
  }
  const bool onlyEcho = count == sizeof(request) && !memcmp(request, received, count);
  if (!hasValidResponse(received, count, pid)) {
    logRow(receivedAt, !count || onlyEcho ? "TIMEOUT" : "INVALID_RESPONSE", nullptr, 0);
    if (stopped) return;
    if (++unanswered >= 8) {
      sessionReady = false;
      retryAt = millis() + 3000;
      if (!stopped) status = "SEM RESPOSTA | ISO 9141";
    }
  } else {
    unanswered = 0;
    if (++samples == SAMPLE_TARGET) {
      logRow(millis(), "TARGET_REACHED", nullptr, 0);
      if (stopped) return;
      finish("VERIFICANDO CSV");
      File saved = LittleFS.open(path, FILE_READ);
      status = saved && saved.size() == expectedFileSize
                   ? "CSV SALVO | PODE DESLIGAR"
                   : "ERRO AO VERIFICAR CSV";
    }
  }
  nextQuery = millis() + 55;
}

void handleConsole() {
  static char command[32];
  static size_t length = 0;
  while (Serial0.available()) {
    const char c = Serial0.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (length + 1 < sizeof(command)) command[length++] = c;
      continue;
    }
    command[length] = '\0';
    length = 0;
    if (!strcmp(command, "STOP")) {
      finish("CSV FECHADO");
    } else if (!strcmp(command, "INIT FLASH") && !storageReady) {
      storageReady = LittleFS.format() && LittleFS.begin(false);
      Serial0.println(storageReady ? "LittleFS pronto. Envie START." : "Falha LittleFS.");
    } else if (!strcmp(command, "START") && storageReady && hostReady && !logFile) {
      stopped = false;
      retryAt = millis();
      status = "AGUARDANDO FT232";
    } else if (!strcmp(command, "LIST") && !logFile && storageReady) {
      File root = LittleFS.open("/");
      for (File file = root.openNextFile(); file; file = root.openNextFile()) {
        Serial0.printf("%s %u bytes\n", file.name(), unsigned(file.size()));
      }
    } else if (!strncmp(command, "DUMP obd", 8) && !logFile && storageReady &&
               strlen(command) == 16 && strspn(command + 8, "0123456789") == 4 &&
               !strcmp(command + 12, ".csv")) {
      char selected[16];
      snprintf(selected, sizeof(selected), "/%s", command + 5);
      File file = LittleFS.open(selected, FILE_READ);
      if (!file) {
        Serial0.println("Arquivo indisponivel");
        continue;
      }
      uint8_t buffer[256];
      while (file.available()) {
        const size_t n = file.read(buffer, sizeof(buffer));
        if (!n) break;
        Serial0.write(buffer, n);
      }
    } else {
      Serial0.println("STOP, LIST, DUMP obd0001.csv, START");
      if (!storageReady) Serial0.println("INIT FLASH apaga a particao LittleFS.");
    }
  }
}

void setup() {
  Serial0.begin(115200);
  oled.begin();
  oled.setFont(u8g2_font_6x12_tr);
  storageReady = LittleFS.begin(false);
  if (!storageReady) finish("ERRO MONTANDO LITTLEFS");

  usb.onDeviceConnected([](const EspUsbHostDeviceInfo &device) {
    usbId.store((uint32_t(device.vid) << 16) | device.pid);
    if (device.vid == FTDI_VID && device.pid == FT232_PID) {
      usbAddress.store(device.address);
    }
  });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo &device) {
    if (usbAddress.load() == device.address) usbAddress.store(0);
    usbId.store(0);
  });
  hostReady = usb.begin() && kkl.begin(10400);
  if (!hostReady) finish("ERRO USB HOST");
  drawStatus();
}

void loop() {
  static uint32_t displayAt = 0;
  handleConsole();

  const uint8_t address = usbAddress.load();
  if (!stopped && !address) {
    if (logFile) {
      logRow(millis(), "USB_DISCONNECTED", nullptr, 0);
      finish("USB REMOVIDO | CSV FECHADO");
    } else {
      status = usbId.load() ? "USB DIFERENTE DE FT232" : "AGUARDANDO USB";
    }
  }
  if (!stopped && address && kkl.connected()) {
    if (!openLog()) {
      finish("ERRO ABRINDO CSV");
    } else if (!sessionReady && deadlineReached(retryAt)) {
      sessionReady = slowInit(address) && !stopped;
      if (!stopped) status = sessionReady ? "GRAVANDO | ISO 9141" : "INIT FALHOU | ISO 9141";
      retryAt = millis() + 3000;
      nextQuery = millis() + 55;
      pollIndex = unanswered = 0;
    } else if (sessionReady && deadlineReached(nextQuery)) {
      queryPid();
    }
  } else if (!stopped && address) {
    status = "FT232 | AGUARDANDO SERIAL";
  }
  if (logFile && millis() - lastFlush >= 1000) {
    logFile.flush();
    lastFlush = millis();
  }
  if (deadlineReached(displayAt)) {
    drawStatus();
    displayAt = millis() + 200;
  }
  delay(1);
}
