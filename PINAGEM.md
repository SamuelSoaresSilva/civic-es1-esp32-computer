# Pinagem do ESP32-S3-N16R8

## OLED SSD1322 em SPI de quatro fios

| OLED | Marcação na placa | Função |
| --- | --- | --- |
| CS | `10` | Seleção do display |
| DC | `13` | Seleção de dados ou comando |
| RES | `14` | Reset do display |
| SDA | `11` | Dados SPI (MOSI) |
| SCK | `12` | Clock SPI |
| VCC | 3V3, se compatível | Confirme que o módulo aceita 3,3 V e que a placa fornece a corrente necessária |
| GND | GND | Terra comum |

Os quatro sketches do projeto usam esse mapa. O pino RST da placa reinicia o próprio ESP32.

## Switch mecânico de teclado

| Switch | Marcação na placa | Função |
| --- | --- | --- |
| Terminal 1 | `4` | Entrada do botão com `INPUT_PULLUP` |
| Terminal 2 | GND | Terra comum com o OLED |

O painel usa o pino `4` para navegar entre as telas. Na frente da placa, com a antena para cima e os conectores USB-C para baixo, ele fica na fileira esquerda, logo abaixo de `RST`.

Com a placa desligada, ligue um terminal do contato ao pino `4` e o outro ao GND comum. Um switch mecânico simples de dois terminais pode ser ligado em qualquer sentido. Se houver terminais extras, identifique com um multímetro o par que tem continuidade somente quando o switch é pressionado. Configure `INPUT_PULLUP` e o resistor interno manterá o pino em `HIGH` quando solto. Ao pressionar, o contato fecha para GND e a leitura passa para `LOW`.

### Teste separado no Arduino IDE

Carregue este exemplo como um sketch separado e abra o Monitor Serial em 115200 baud. Cada pressão estável imprime uma linha. O exemplo usa 25 ms de debounce e roda sem `delay()`.

```cpp
constexpr uint8_t SWITCH_PIN = 4;
constexpr uint32_t DEBOUNCE_MS = 25;
int lastReading;
int stableState;
uint32_t lastChange;

void setup() {
  Serial.begin(115200);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  lastReading = stableState = digitalRead(SWITCH_PIN);
  lastChange = millis();
}

void loop() {
  const uint32_t now = millis();
  const int reading = digitalRead(SWITCH_PIN);
  if (reading != lastReading) {
    lastReading = reading;
    lastChange = now;
  }

  if (reading != stableState && now - lastChange >= DEBOUNCE_MS) {
    stableState = reading;
    if (stableState == LOW) {
      Serial.println("Switch pressionado");
    }
  }
}
```

## Cabo KKL

O KKL usa a porta USB nativa/OTG. Os pinos `19` (D−) e `20` (D+) são as linhas dessa interface e ficam reservados para ela. A porta USB-UART é usada para carregar o firmware e exportar o CSV. [Guia de hardware da Espressif](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)
