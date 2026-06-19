# Lampada PoE Matter over Ethernet com ESP32

Firmware para um driver de lampada PoE baseado em ESP32, Ethernet SPI com
ENC28J60 e ESP-Matter. O dispositivo funciona sem Wi-Fi como transporte
operacional e oferece controle de liga/desliga, intensidade por PWM e leitura
das tensoes de entrada e saida.

## Estado atual

Implementado e validado em hardware:

- ENC28J60 via SPI com IPv4 por DHCP e IPv6;
- recepcao multicast necessaria para NDP, mDNS e DNS-SD do Matter;
- Matter over Ethernet com comissionamento on-network;
- endpoint `Dimmable Light` controlando PWM de 20 kHz;
- ADC de entrada no GPIO35, escalado para 0 a 48 V;
- ADC de saida no GPIO34, escalado para 0 a 24 V;
- media de 32 amostras por canal a cada segundo;
- calibracao ADC pelo ESP-IDF, quando disponivel;
- endpoints Matter Electrical Sensor para as duas tensoes;
- leitura e subscription das tensoes pelo `chip-tool`.

## Arquitetura

```text
Controller Matter
(chip-tool, Home Assistant, Apple Home, Google Home)
                     |
              Ethernet / IPv6
                     |
                ENC28J60
                     |
                   ESP32
        +------------+-------------+
        |            |             |
 GPIO26 PWM     GPIO35 ADC     GPIO34 ADC
 MOSFET/luz     entrada 48 V   saida 24 V
```

O firmware esta dividido em:

| Arquivo | Responsabilidade |
|---|---|
| `main/app_eth.cpp` | SPI, ENC28J60, `esp_netif`, IPv4 e IPv6 |
| `main/app_matter_eth.cpp` | Integra a interface Ethernet externa ao Matter |
| `main/app_light_driver.cpp` | PWM local da lampada |
| `main/app_adc.cpp` | Amostragem, calibracao e escala das duas tensoes |
| `main/app_voltage_measurement.cpp` | Endpoints e delegates Matter dos ADCs |
| `main/app_matter.cpp` | Node, endpoints, callbacks e inicializacao Matter |
| `main/main.cpp` | Sequencia de inicializacao do firmware |

## Pinagem

### ENC28J60

| Sinal ENC28J60 | ESP32 |
|---|---|
| SCK | GPIO18 |
| MISO / SO | GPIO19 |
| MOSI / SI | GPIO23 |
| CS | GPIO5 |
| INT | GPIO27 |
| GND | GND |
| VCC | Conforme a especificacao do modulo utilizado |

O SPI esta configurado em 4 MHz para maior estabilidade em protoboard e
durante rajadas de trafego Matter/mDNS.

### Lampada e ADC

| Funcao | GPIO | Configuracao |
|---|---:|---|
| PWM da lampada | 26 | LEDC, 20 kHz, 10 bits |
| Tensao de entrada | 35 | ADC1_CH7, 0–3,3 V representa 0–48 V |
| Tensao de saida | 34 | ADC1_CH6, 0–3,3 V representa 0–24 V |

Os GPIOs 34 e 35 nunca devem receber 24 V ou 48 V diretamente. Use divisores
resistivos, protecao contra sobretensao e GND comum. Para maior precisao e para
evitar saturacao do ADC do ESP32, projete o divisor para aproximadamente
2,5–3,0 V na maior tensao esperada.

O GPIO26 deve acionar um gate driver ou MOSFET dimensionado para a carga. Nao
ligue uma lampada de potencia diretamente ao GPIO.

## Escala dos ADCs

O firmware usa as seguintes relacoes lineares:

```text
Vin  = tensao_no_GPIO35 * 48,0 / 3,3
Vout = tensao_no_GPIO34 * 24,0 / 3,3
```

Cada ciclo calcula a media de 32 amostras. O log aparece a cada cinco segundos:

```text
I app_adc: Vin=35.462 V (GPIO35=2438 mV raw=2796), Vout=11.527 V (GPIO34=1585 mV raw=1758)
```

Pequenas variacoes sao normais por ruido do ADC e tolerancia dos resistores.
Compare os valores com um multimetro antes de definir os fatores finais de
calibracao.

## Modelo Matter

| Endpoint | Device type / funcao | Clusters principais |
|---:|---|---|
| 0 | Root Node | Commissioning, Operational Credentials, Ethernet Diagnostics |
| 1 | Dimmable Light | On/Off, Level Control, Identify, Groups, Scenes |
| 2 | Electrical Sensor – entrada | Electrical Power Measurement / Voltage |
| 3 | Electrical Sensor – saida | Electrical Power Measurement / Voltage |

As tensoes Matter sao publicadas em milivolts e arredondadas em passos de
100 mV para evitar reports causados por ruido pequeno:

```text
35500 = 35,5 V
11600 = 11,6 V
```

### Callbacks

- `app_event_cb`: eventos de IP, comissionamento, janela e fabrics;
- `app_attribute_update_cb`: converte `OnOff` e `CurrentLevel` em PWM;
- `app_identification_cb`: recebe comandos Identify e atualmente registra log;
- `adc_update_callback`: agenda a publicacao das tensoes na task Matter;
- handlers Ethernet: link, IPv4 e IPv6.

## Ambiente

- ESP-IDF: `v5.4.1`;
- ESP-Matter component: `~1.3.1`;
- ENC28J60 component: `^1.1.0`;
- target: ESP32;
- flash: 4 MB com duas particoes OTA.

Use um terminal separado do ambiente Pigweed/CHIP Tool:

```bash
cd ~/esp/esp-idf-v5.4.1
export IDF_PYTHON_ENV_PATH=~/.espressif/python_env/idf5.4_py3.12_env
source ./export.sh

cd ~/esp/PoE
idf.py build
```

## Flash e monitor

```bash
cd ~/esp/PoE
idf.py -p /dev/ttyUSB0 flash monitor
```

O firmware novo pode ser gravado sem apagar a flash, preservando a fabric
Matter existente. Use `erase-flash` somente quando quiser apagar todo o estado
de comissionamento:

```bash
idf.py -p /dev/ttyUSB0 erase-flash flash monitor
```

Logs importantes:

```text
ENC28J60 promiscuous mode enabled for IPv6/NDP/mDNS
Ethernet Link Up
Ethernet Got IPv6: fe80::..., type: link-local
Matter Dimmable Light criado no endpoint 0x1
Electrical Sensor entrada 48 V criado no endpoint 0x2
Electrical Sensor saida 24 V criado no endpoint 0x3
Callbacks Matter das tensoes DC iniciados
```

## CHIP Tool

Ative o ambiente do CHIP Tool em outro terminal:

```bash
cd ~/esp/esp-idf-v5.4.1/esp-matter/connectedhomeip/connectedhomeip
export PW_ACTIVATE_SKIP_CHECKS=1
source ./scripts/activate.sh
```

Os exemplos abaixo usam Node ID `0x7283`, passcode `20202021` e discriminator
`3840`.

### Primeiro comissionamento

Execute apenas quando o ESP estiver anunciando `_matterc._udp` e a janela de
comissionamento estiver aberta:

```bash
./out/chip-tool/chip-tool pairing onnetwork-long 0x7283 20202021 3840 --timeout 120
```

Depois de comissionado, o ESP anuncia `_matter._tcp`. Nao execute o pareamento
novamente; use diretamente os comandos operacionais.

### Consultar endpoints

```bash
./out/chip-tool/chip-tool descriptor read server-list 0x7283 0
./out/chip-tool/chip-tool descriptor read device-type-list 0x7283 1
./out/chip-tool/chip-tool descriptor read device-type-list 0x7283 2,3
```

### Controlar a lampada

```bash
./out/chip-tool/chip-tool onoff on 0x7283 1
./out/chip-tool/chip-tool levelcontrol move-to-level 50 0 0 0 0x7283 1
./out/chip-tool/chip-tool levelcontrol move-to-level 180 0 0 0 0x7283 1
./out/chip-tool/chip-tool onoff off 0x7283 1
```

### Ler os ADCs

```bash
# Entrada, endpoint 2
./out/chip-tool/chip-tool electricalpowermeasurement read voltage 0x7283 2

# Saida, endpoint 3
./out/chip-tool/chip-tool electricalpowermeasurement read voltage 0x7283 3

# As duas tensoes
./out/chip-tool/chip-tool electricalpowermeasurement read voltage 0x7283 2,3

# Confirmar que a medicao e DC
./out/chip-tool/chip-tool electricalpowermeasurement read power-mode 0x7283 2,3
```

Subscription continua das tensoes, com intervalo entre 1 e 10 segundos:

```bash
./out/chip-tool/chip-tool electricalpowermeasurement subscribe voltage 1 10 0x7283 2,3
```

## Rede e diagnostico

```bash
ip -brief addr
ping 192.168.1.101
ping -6 fe80::861f:e8ff:fe32:bff7%enp4s0
avahi-browse -rt _matterc._udp
avahi-browse -rt _matter._tcp
```

Matter depende de IPv6 multicast para NDP, mDNS e DNS-SD. O ENC28J60 esta em
modo promiscuo nesta versao do firmware porque o controle publico de filtro
all-multicast nao esta disponivel no ESP-IDF 5.4.

## Limitacoes antes de producao

Este ainda e um firmware de desenvolvimento. Antes de fabricar ou vender:

- substituir passcode, discriminator, VID/PID e certificados de teste;
- provisionar DAC e chave privada unicos por unidade;
- implementar OTA assinada e rollback;
- habilitar Secure Boot, Flash Encryption e NVS Encryption;
- adicionar factory reset por botao fisico;
- calibrar individualmente os divisores ADC;
- implementar protecoes de subtensao, sobretensao, sobrecorrente e temperatura;
- avaliar a troca do ENC28J60 por um controlador Ethernet recomendado para
  projetos novos;
- executar certificacao Matter, ensaios eletricos, EMC e regulatorios.
