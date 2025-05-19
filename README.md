# Sistema de Monitoramento e Alerta de Enchentes

Um sistema embarcado para monitoramento de níveis de água e precipitação, com alertas visuais e sonoros para prevenção de enchentes.

## Visão Geral

Este projeto utiliza um microcontrolador Raspberry Pi Pico executando FreeRTOS para criar um sistema de monitoramento em tempo real que detecta níveis críticos de água e volume de chuva. Quando os níveis atingem patamares de risco, o sistema aciona alertas sonoros e visuais para indicar a necessidade de evacuação.


## Componentes de Hardware

- Raspberry Pi Pico (RP2040)
- Display OLED SSD1306 (I2C)
- Matriz de LEDs WS2812B
- LEDs RGB
- Joystick analógico (para simulação de sensores)
- Buzzer piezoelétrico
- Botões de controle

## Conexões de Hardware

| Componente | Pino |
|------------|------|
| Display OLED SDA | 14 |
| Display OLED SCL | 15 |
| LED RGB (vermelho) | 13 |
| LED RGB (verde) | 11 |
| LED RGB (azul) | 12 |
| Joystick Y | 27 |
| Joystick X | 26 |
| Buzzer | 21 |
| Botão A | 5 |
| Botão B | 6 |

## Estrutura do Software

O sistema utiliza FreeRTOS para gerenciar múltiplas tarefas simultâneas:

1. **Tarefa de Sensores** - Lê os dados analógicos que representam níveis de água e volume de chuva
2. **Tarefa de Display** - Atualiza a interface do usuário no display OLED
3. **Tarefa de Buzzer** - Gera alertas sonoros quando condições críticas são detectadas
4. **Tarefa de Matriz LED** - Apresenta padrões visuais para indicar diferentes níveis de risco
5. **Tarefa de LEDs RGB** - Fornece indicação adicional do status do sistema

A comunicação entre as tarefas é realizada através de filas FreeRTOS.

## Funcionalidades

### Monitoramento
- Leitura contínua dos níveis de água e volume de chuva
- Apresentação dos valores atuais no display OLED

### Sistema de Alerta
- **Alerta Sonoro**: Buzzer emite um som pulsante de 1000Hz quando detectada situação crítica
- **Alerta Visual**:
  - LEDs RGB piscam em vermelho durante alertas
  - Matriz de LEDs mostra guarda chuva em azul para chuva moderada
  - Matriz de LEDs mostra exclamação em vermelho para situações críticas
  - Display OLED exibe a mensagem "EVACUE O LOCAL" durante alertas

### Condições de Alerta
- Volume de chuva >= 80%
- Nível de água >= 70%

## Simulação de Sensores
Como o sistema utiliza um joystick para simular os sensores:

- Joystick Horizontal (X): Controla o nível simulado de água
- Joystick Vertical (Y): Controla o volume simulado de chuva