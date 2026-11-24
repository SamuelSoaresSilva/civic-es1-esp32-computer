# Teste de bancada do OLED SSD1322

## Placa usada no projeto

A placa da foto tem o módulo marcado `ESP32-S3-N16R8`, duas fileiras laterais de pinos e dois conectores USB-C. Os botões na frente são `RST` e `BOOT`. No verso, `USB` identifica a porta nativa/OTG para o KKL e `COM` identifica a porta USB-UART para carregar o firmware e exportar o CSV.

Olhando a placa pela frente, com a antena para cima e os conectores para baixo, o pino `4` fica na fileira esquerda, logo abaixo de `RST`. Os pinos `10` a `14` ficam na mesma fileira, perto dos conectores. A tabela do switch e o teste estão em [PINAGEM.md](PINAGEM.md#switch-mecânico-de-teclado).

## Painel simulado do Civic

1. Ligue o OLED conforme a tabela de pinagem abaixo.
2. No Arduino IDE, instale o pacote **esp32 by Espressif Systems** e a biblioteca **U8g2**. Abra `painel_oled/painel_oled.ino`. O IDE inclui `fonte_painel.cpp` da mesma pasta na compilação.
3. Selecione o modelo da placa ou **ESP32S3 Dev Module** para uma placa genérica. Ajuste **Flash Size** à capacidade da placa, selecione a porta USB da placa e clique em **Carregar**. O painel começa ao ligar ou reiniciar, sem carro, adaptador, botão ou entrada serial.

O RPM aparece em destaque, acima da velocidade em km/h e da temperatura do motor em °C. À direita, o gráfico mostra cerca de 3,5 s de histórico de RPM, com as amostras mais recentes na extremidade direita.

O switch mecânico entre **GPIO 4 e GND** usa o pull-up interno, com debounce de 25 ms. O painel abre na tela principal e a simulação roda continuamente nas duas telas.

| Gesto | Ação |
| --- | --- |
| Clique curto e solte | Alterna entre o painel com gráfico de RPM e a tela com velocidade em destaque, que mostra RPM e temperatura à direita. |
| Segure por cerca de 0,8 s | Volta à tela principal enquanto o botão está pressionado. Soltar depois disso não troca a tela novamente. |

| Tempo no ciclo | O que observar |
| --- | --- |
| 0 a 3 s | Marcha lenta em 820 RPM e velocidade válida de 0 km/h. |
| 3 a 15 s | Aceleração e quedas de RPM nas trocas aos 7 e 12 s, enquanto a velocidade se mantém durante cada troca. |
| 15 a 23 s | Desaceleração até parar. |
| 24 a 26 s | As três leituras aparecem como `--` e o gráfico registra uma lacuna. Observe o retorno de `0 km/h` após a interrupção. |
| 28 s | O ciclo recomeça. |

A interface `AmostraPainel`, em `painel_oled/fonte_painel.h`, carrega valor, unidade e validade de cada leitura. Para ligar uma reprodução CSV ou um ELM327 Wi-Fi, substitua as chamadas a `lerSimulacao` por uma fonte que devolva a mesma amostra. Mantenha a consulta ou reprodução fora da renderização e devolva as leituras disponíveis sem bloquear a atualização. A escolha e a validação do protocolo da ECU ficam para uma etapa posterior.

## Coleta OBD2 sem notebook

Abra `coletor_kkl/coletor_kkl.ino` no Arduino IDE. O circuito reaproveita a pinagem do OLED abaixo e a coleta inicia automaticamente. A barra avança a cada consulta com resposta OBD válida. Ao atingir **300 respostas**, o ESP32 fecha o arquivo, confere seu tamanho e mostra **`CSV SALVO | PODE DESLIGAR`**. Aguarde essa mensagem antes de desligar a alimentação ou retirar o cabo. A meta fica na constante `SAMPLE_TARGET`.

1. Instale **U8g2** e **EspUsbHost 2.x**, de tanakamasayuki. Use o pacote **esp32 by Espressif Systems** a partir de 3.2.0 e selecione uma partição com sistema de arquivos. Em uma placa genérica, selecione **ESP32S3 Dev Module**, ajuste **Flash Size** para a capacidade da placa e deixe **USB CDC On Boot** desabilitado. Use a porta USB-UART para carregar e exportar os arquivos.
2. Na bancada, verifique o KKL no Windows pela tela **Gerenciador de Dispositivos → Propriedades → Detalhes → IDs de Hardware**. O sketch espera `VID_0403&PID_6001`. Ele mostra na tela o VID:PID encontrado, inclusive se for outro.
3. Alimente a placa por USB com um power bank ou uma fonte USB veicular. Conecte o KKL à porta **USB nativa/OTG** do ESP32-S3 com o adaptador USB-C. A porta e o adaptador precisam assumir o papel de host e fornecer 5 V no VBUS ao KKL. Confirme isso para a revisão da sua placa antes da coleta. Use o cabo KKL para a ligação com o carro.
4. Se aparecer `ERRO MONTANDO LITTLEFS`, confira primeiro a partição. Para inicializar uma partição nova, use o Monitor Serial da USB-UART a **115200 baud**, com fim de linha **Nova linha**, e envie `INIT FLASH` seguido de `START`. O comando de inicialização formata a partição de arquivos, então use-o somente se não houver dados a recuperar.
5. Com o carro parado e a ignição ligada, observe `INIT 5 BAUD` e depois `GRAVANDO`. O firmware tenta **ISO 9141-2** com inicialização lenta. Se a inicialização falhar, a barra fica parada. Para recuperar esse diagnóstico, o firmware atualiza o arquivo periodicamente e o fecha ao detectar a retirada do KKL. Mantenha o ESP32 alimentado até aparecer `USB REMOVIDO | CSV FECHADO`. Na bancada, também é possível fechar a sessão pela serial com `STOP`.
6. Depois da coleta, desconecte o KKL e conecte a porta USB-UART ao PC. Envie `LIST` para ver as sessões e `DUMP obd0001.csv` com o nome da sessão escolhida. Copie a saída a partir de `ms,event,hex` para um arquivo CSV.

O log registra tempo em milissegundos desde a abertura da sessão, tipo de evento e bytes em hexadecimal. Ele preserva os bytes recebidos da linha, inclusive eco do KKL quando presente. As consultas usam serviço OBD de leitura `01` e PIDs `00`, `0C`, `0D`, `05`, `11` e `04`. O contador avança uma vez por consulta com resposta positiva para o PID enviado e checksum válido. O OLED mostra o progresso e um trecho dos últimos bytes TX e RX. As linhas de erro e as tentativas de inicialização também ficam no CSV.

[Biblioteca EspUsbHost](https://github.com/tanakamasayuki/EspUsbHost) · [USB Host no ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/usb_host.html)

## Testes do OLED

O sketch usa o perfil NHD 256 × 64 da U8g2 com SPI de quatro fios por software e buffer completo. Os números dos pinos ficam explícitos no construtor para facilitar o primeiro teste.

## Ligações

Confira o modelo exato do OLED, a tensão de alimentação, os níveis lógicos e a seleção da interface na documentação do módulo antes de energizar. Se houver jumpers de interface, confirme que estão em SPI de quatro fios. Faça as ligações com o conjunto desligado e use terra comum.

| Sinal no módulo | Marcação na placa |
| --- | --- |
| SCK | `12` |
| SDA (dados SPI / MOSI) | `11` |
| CS | `10` |
| DC | `13` |
| RES | `14` |
| GND | GND |
| VCC | Alimentação especificada pelo fabricante do módulo |

Na frente da placa, com a antena para cima, os pinos 10 a 14 ficam no lado esquerdo, perto dos conectores USB-C. Use o número 14 para o fio RES do OLED. O pino RST no topo do ESP32 é o reset da própria placa. A alimentação do OLED depende da especificação do módulo. Use o pino 3V3 se o fabricante confirmar alimentação de 3,3 V e a placa puder fornecer a corrente necessária. O teste usa o ESP32 via USB, com o OBD2 desconectado.

## Executar

1. No Arduino IDE, instale o pacote de placas **esp32 by Espressif Systems** e a biblioteca **U8g2** pelo Gerenciador de Bibliotecas. O clone em `vendor/U8g2_Arduino` serve como referência local.
2. Abra `teste_ssd1322/teste_ssd1322.ino`, selecione o modelo da placa e a porta USB, compile e carregue.
3. Abra o Monitor Serial em **115200 baud**. O contador deve avançar a cada 250 ms. Na tela, confira a moldura inteira, o quadrado cheio no canto superior esquerdo, o quadrado vazado no inferior direito e a barra que percorre a tela.
4. Se o contador avançar e o OLED continuar apagado, confira alimentação, terra, seleção de interface, CS, D/C e reset. O log serial mostra que o ESP32 está executando o sketch. A confirmação do OLED é visual.

[Construtores oficiais da U8g2 para SSD1322](https://github.com/olikraus/u8g2/wiki/u8g2setupcpp#ssd1322-nhd_256x64)

## Tons de cinza e taxa de atualização

Abra `teste_gray_fps/teste_gray_fps.ino` no Arduino IDE e carregue com a mesma pinagem. A U8g2 inicializa o painel com o perfil já testado. O sketch escreve diretamente na memória do SSD1322 via SPI por hardware, com 4 bits por pixel (8.192 bytes por quadro). A tela deve mostrar 16 faixas verticais, incluindo o preto, e um marcador branco em movimento na parte inferior.

Com o Monitor Serial em **115200 baud**, o teste alterna entre 2, 4, 8 e 10 MHz, medindo dois segundos a cada frequência. Confira se a imagem fica estável em cada etapa e anote os quadros por segundo e o tempo médio por quadro. A medição inclui a atualização do marcador e a transferência do quadro. A frequência de varredura interna do painel é independente dessa medição.
