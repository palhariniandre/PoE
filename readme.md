# ESP32 + ENC28J60 + LED: validação Ethernet antes de Matter

Este projeto implementa a **Etapa 1** do roteiro: validar Ethernet com ESP32 + ENC28J60 via SPI antes de iniciar ESP-Matter. O firmware atual inicializa:

- NVS
- LED de teste no `GPIO26`
- SPI para o ENC28J60
- driver `espressif/enc28j60`
- `esp_netif`
- Ethernet
- eventos de link, IPv4 e IPv6
- logs no monitor serial

Matter ainda **não** é iniciado neste firmware. A regra do projeto é iniciar Matter somente depois de confirmar link Ethernet, interface criada, IPv4 para debug/ping e IPv6 link-local para Matter.

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

Evite `GPIO0`, `GPIO2`, `GPIO12` e `GPIO15` nesta fase, porque podem interferir no boot.

## Recomendações elétricas para o ENC28J60

- Use fios SPI curtos.
- Mantenha GND comum entre ESP32 e ENC28J60.
- Coloque um capacitor de `100 nF` perto do módulo.
- Coloque `10 uF` ou `47 uF` na alimentação do módulo.
- Comece com SPI em `8 MHz`.
- Evite protoboard muito bagunçada.
- Garanta fonte forte o suficiente; alguns módulos ENC28J60 têm consumo alto.

## Build e flash

O ESP-IDF informado está em:

```bash
/home/dede/esp/esp-idf
```

Prepare o ambiente e compile:

```bash
cd /home/dede/esp/PoE
source /home/dede/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
```

Grave e abra o monitor:

```bash
idf.py flash monitor
```

Se quiser apagar NVS/flash durante testes:

```bash
idf.py erase-flash
idf.py flash monitor
```

## Logs esperados

Com cabo Ethernet conectado ao switch/roteador, o monitor deve mostrar algo nesta linha:

```text
I app_led: LED de teste configurado no GPIO26
I app_eth: Inicializando SPI2: SCLK=18 MISO=19 MOSI=23 CS=5 INT=27, clock=8 MHz
I app_eth: Ethernet Started
I app_eth: Ethernet Link Up
I app_eth: Ethernet HW Addr xx:xx:xx:xx:xx:xx
I app_eth: Ethernet Got IPv4
I app_eth: ETHIP: 192.168.x.x
I app_eth: Ethernet Got IPv6: fe80::..., type: link-local
I app_eth: Ethernet pronta para a proxima etapa: Matter over Ethernet
I main: Etapa 1 concluida: Ethernet ENC28J60 validada. Matter ainda nao iniciado.
```

O firmware aborta com timeout se não receber link, IPv4 e IPv6 em até 30 segundos.

## Testes no notebook

Use o IPv4 mostrado no monitor:

```bash
ping <ipv4-do-esp32>
ip neigh
```

Para IPv6 link-local, use a interface correta do notebook:

```bash
ip addr
ping6 fe80::<ipv6-do-esp32>%<iface>
```

Em WSL2, mDNS/multicast pode não sair corretamente para a LAN dependendo do modo de rede. Para Matter, prefira Linux nativo ou confirme que multicast IPv6/mDNS funciona no ambiente.

## Estrutura implementada

```text
app_main()
 ├── nvs_flash_init()
 ├── app_led_init()
 ├── app_eth_init()
 ├── app_wait_for_eth_connected()
 ├── TODO app_matter_light_init()
 └── TODO esp_matter::start()
```

Arquivos principais:

- `main/main.cpp`: sequência de boot e espera da Ethernet.
- `main/app_eth.cpp`: SPI, ENC28J60, `esp_netif`, eventos, IPv4 e IPv6.
- `main/app_led.cpp`: controle básico do LED no `GPIO26`.
- `main/idf_component.yml`: dependência `espressif/enc28j60`.
- `sdkconfig.defaults`: IPv4/IPv6 e Ethernet SPI.

## Próxima etapa: Matter over Ethernet

Depois que ping e IPv6 estiverem estáveis:

1. Instalar/configurar ESP-Matter em `/home/dede/esp/esp-matter`.
2. Compilar primeiro o exemplo oficial `examples/light`.
3. Integrar `app_eth_init()` e `app_wait_for_eth_connected()` antes de `esp_matter::start()`.
4. Desabilitar Wi-Fi como transporte operacional.
5. Criar `On/Off Light` no endpoint `0x1`.
6. Mapear o cluster `OnOff` para o LED:

```text
Matter OnOff false -> GPIO26 baixo -> LED apagado
Matter OnOff true  -> GPIO26 alto  -> LED aceso
```

Comandos de teste futuros:

```bash
sudo apt install avahi-utils
avahi-browse -rt _matterc._udp

chip-tool pairing onnetwork 0x7283 20202021
chip-tool onoff on 0x7283 0x1
chip-tool onoff off 0x7283 0x1
chip-tool onoff toggle 0x7283 0x1
```

Para dimmer futuro com LEDC:

```text
Matter LevelControl 0   -> PWM 0%
Matter LevelControl 127 -> PWM aproximadamente 50%
Matter LevelControl 254 -> PWM 100%
```

```bash
chip-tool levelcontrol move-to-level 10 0 0 0 0x7283 0x1
chip-tool levelcontrol move-to-level 100 0 0 0 0x7283 0x1
chip-tool levelcontrol move-to-level 254 0 0 0 0x7283 0x1
```

## Troubleshooting rápido

- ESP32 não recebe IP: confira DHCP, cabo, switch, logs `ETHIP`, MAC e evento `IP_EVENT_ETH_GOT_IP`.
- ENC28J60 não dá link: confira VCC, GND, cabo, módulo, CS, INT e alimentação.
- `gpio_isr_handler_add`: o firmware instala `gpio_install_isr_service()` antes do driver ENC28J60; se esse erro aparecer, recompile e grave a versão atual.
- `tx_ready_sem expired`: normalmente indica interrupção `INT` não funcionando, fio `GPIO27` errado/solto, ISR GPIO não instalada, alimentação fraca ou SPI/fiação instável.
- IPv4 funciona mas IPv6 não aparece: confirme `CONFIG_LWIP_IPV6=y`, link ativo e evento `IP_EVENT_GOT_IP6`.
- Ping funciona mas Matter não comissiona: investigue IPv6, mDNS, firewall, WSL2 e multicast bloqueado.
- `avahi-browse` não mostra `_matterc._udp`: dispositivo pode já estar pareado, Matter pode não ter iniciado ou multicast/mDNS pode estar bloqueado.
- LED não responde futuramente ao Matter: confira endpoint `0x1`, callback `PRE_UPDATE`, cluster `OnOff` e polaridade do `GPIO26`.
- Funcionou uma vez e não pareia novamente: faça factory reset ou `idf.py erase-flash`; no controller, remova o nó com `chip-tool pairing unpair 0x7283`.
- Instabilidade no ENC28J60: reduza SPI para `8 MHz`, encurte fios, melhore desacoplamento e fonte.

## Produto final

O ENC28J60 é adequado para protótipo, mas para produto final com PoE e driver de LED considere:

- W5500
- Ethernet RMII com PHY dedicado
- placa ESP32 com Ethernet nativa
- hardware PoE com controlador PD adequado

Depois do checklist Matter, a substituição do LED por driver real deve seguir esta ideia:

```text
GPIO26 PWM -> entrada DIM/PWM do driver
GPIO25     -> ENABLE do driver
GND ESP32  -> GND de controle do driver, se o driver não for isolado
```

Este é o próximo passo, não a primeira etapa.

## Checklist alvo

```text
[ ] ESP32 inicializa sem erro
[ ] ENC28J60 dá link Ethernet
[ ] ESP32 recebe IP
[ ] notebook consegue pingar o ESP32
[ ] dispositivo anuncia Matter via mDNS
[ ] chip-tool faz pairing onnetwork
[ ] chip-tool onoff on acende o LED
[ ] chip-tool onoff off apaga o LED
[ ] chip-tool onoff toggle alterna o LED
[ ] reiniciar o ESP32 recupera a operação
[ ] factory reset permite novo comissionamento
```
