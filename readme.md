# ESP32 + ENC28J60 + Matter On/Off Light via Ethernet

Este projeto valida Matter over Ethernet em etapas usando:

- ESP32
- ENC28J60 via SPI
- LED de teste no `GPIO26`
- ESP-Matter como dispositivo `On/Off Light`
- Sem Wi-Fi como transporte operacional

A Etapa 1 já foi validada em hardware: link Ethernet, IPv4 por DHCP e IPv6. A Etapa 2 agora está implementada no firmware: depois que a Ethernet fica pronta, o app cria um endpoint Matter `On/Off Light` e inicia `esp_matter::start()`.

## Arquitetura

```text
Notebook Linux / Ubuntu / WSL2
        |
Switch ou roteador Ethernet
        |
Módulo ENC28J60
        |
ESP32
        |
GPIO26 -> resistor 220R/330R -> LED -> GND
```

## Ligações

### ENC28J60

| ENC28J60 | ESP32 |
|---|---|
| VCC | 3V3 ou 5V, conforme o módulo |
| GND | GND |
| SCK | GPIO18 |
| SO / MISO | GPIO19 |
| SI / MOSI | GPIO23 |
| CS | GPIO5 |
| INT | GPIO27 |

### LED

| Componente | Ligação |
|---|---|
| GPIO26 | resistor 220R ou 330R |
| resistor | anodo do LED |
| catodo do LED | GND |

Evite `GPIO0`, `GPIO2`, `GPIO12` e `GPIO15`.

## Build

O ESP-IDF usado neste ambiente está em:

```bash
/home/dede/esp/esp-idf-v5.2.7
```

Compile:

```bash
cd /home/dede/esp/PoE
source /home/dede/esp/esp-idf-v5.2.7/export.sh
idf.py build
```

Se os `managed_components` forem apagados ou baixados novamente, o primeiro `idf.py build` pode falhar depois de baixar o ESP-Matter, porque o componente volta ao driver Ethernet RMII/IP101 padrão. Nesse caso, reaplique a adaptação e compile de novo:

```bash
tools/patch_esp_matter_spi_eth.sh
idf.py build
```

Essa adaptação evita que o ESP-Matter v1.3 tente inicializar Ethernet RMII/IP101 interna. Neste projeto, a interface Ethernet correta é a ENC28J60 já inicializada por `app_eth.cpp`.

## Flash

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Se o dispositivo já foi comissionado e você quer parear de novo:

```bash
idf.py -p /dev/ttyUSB0 erase-flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Fluxo do firmware

```text
app_main()
 ├── nvs_flash_init()
 ├── app_led_init()
 ├── app_eth_init()
 ├── app_wait_for_eth_connected()
 ├── app_matter_light_init()
 └── esp_matter::start()
```

Matter só inicia depois que:

- link Ethernet está ativo
- `esp_netif` está criada
- IPv4 foi recebido para debug/ping
- IPv6 link-local está disponível

## Endpoint Matter

- Endpoint `0`: root Matter
- Endpoint `1`: `On/Off Light`

Mapeamento:

```text
Matter OnOff false -> GPIO26 baixo -> LED apagado
Matter OnOff true  -> GPIO26 alto  -> LED aceso
```

O callback está em `main/app_matter.cpp` e aplica `OnOff` no `app_led_set()`.

## Logs esperados

Com cabo Ethernet conectado:

```text
I app_led: LED de teste configurado no GPIO26
I app_eth: Ethernet Link Up
I app_eth: ETHIP: 192.168.x.x
I app_eth: Ethernet Got IPv6: fe80::..., type: link-local
I app_eth: Ethernet pronta para a proxima etapa: Matter over Ethernet
I main: Etapa 1 concluida: Ethernet ENC28J60 validada.
I app_matter: Matter On/Off Light criado no endpoint 0x1
I app_matter: Matter iniciado sobre a interface Ethernet ja validada
I main: Etapa 2 iniciada: Matter On/Off Light aguardando comissionamento via Ethernet.
```

Também deve aparecer no log do Matter:

```text
Using externally initialized SPI Ethernet interface
```

## Testes de rede

Use o IPv4 mostrado no monitor:

```bash
ping <ipv4-do-esp32>
ip neigh
```

Para IPv6 link-local:

```bash
ip addr
ping6 fe80::<ipv6-do-esp32>%<iface>
```

Em WSL2, mDNS/multicast pode falhar dependendo do modo de rede. Para Matter, Linux nativo costuma ser mais previsível.

## Descoberta Matter com Avahi

```bash
sudo apt install avahi-utils
avahi-browse -rt _matterc._udp
```

Em modo de comissionamento deve aparecer uma instância `_matterc._udp` com endereço, porta e TXT records de comissionamento. Depois de pareado, o anúncio commissionable pode sumir.

## chip-tool

Pareamento on-network:

```bash
chip-tool pairing onnetwork 0x7283 20202021
```

Controle do LED:

```bash
chip-tool onoff on 0x7283 0x1
chip-tool onoff off 0x7283 0x1
chip-tool onoff toggle 0x7283 0x1
```

Se precisar remover do controller:

```bash
chip-tool pairing unpair 0x7283
```
