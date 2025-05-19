#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "FreeRTOSConfig.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "lib/ws2818b.h"
#include "ws2818b.pio.h"

// Pinos do display OLED via I2C
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define DISPLAY_ADDR 0x3C

// Pinos dos LEDs RGB
#define LED_R 13
#define LED_G 11
#define LED_B 12

// Pinos dos sensores e atuadores
#define JOYSTICK_Y 27
#define JOYSTICK_X 26
#define BUZZER 21
#define BTN_A 5
#define BTN_B 6

// Estrutura para dados dos sensores
typedef struct
{
    uint16_t nivelAgua;
    uint16_t volumeChuva;
} DadosSensor;

// Variáveis globais para PWM
static uint32_t clockSistema = 125000000;
static uint32_t divisorClock = 0;
static uint32_t valorWrap = 0;
static uint fatiaSlice = 0;
static uint canalPwm = 0;

// Variáveis para debounce de botão
static volatile uint32_t ultimoTempoPressionado = 0;
static const uint32_t tempoDebounceMs = 260;

// Fila para comunicação entre tarefas
static QueueHandle_t filaDadosSensor;

// Declarações de funções
void configurarBotao(uint pino);
void configurarAdc(void);
void configurarLedRgb(uint pino);
void configurarI2c(uint baudKhz);
void ajustarFrequenciaPwm(float freq);
void configurarDisplayOled(ssd1306_t *display);
void tarefaSensores(void *pvParameters);
void tarefaDisplay(void *pvParameters);
void tarefaBuzzer(void *pvParameters);
void tarefaMatrizLed(void *pvParameters);
void tarefaLedsRgb(void *pvParameters);
void inicializarHardware(void);

/**
 * @brief Configura um pino GPIO como entrada para botão com resistor pull-up
 * @param pino Número do pino GPIO a ser configurado
 */
void configurarBotao(uint pino)
{
    gpio_init(pino);
    gpio_set_dir(pino, GPIO_IN);
    gpio_pull_up(pino);
}

/**
 * @brief Inicializa o ADC e configura os pinos do joystick para leitura analógica
 */
void configurarAdc(void)
{
    adc_init();
    adc_gpio_init(JOYSTICK_X);
    adc_gpio_init(JOYSTICK_Y);
}

/**
 * @brief Configura um pino GPIO como saída para controlar um LED
 * @param pino Número do pino GPIO a ser configurado
 */
void configurarLedRgb(uint pino)
{
    gpio_init(pino);
    gpio_set_dir(pino, GPIO_OUT);
}

/**
 * @brief Inicializa o barramento I2C para comunicação com o display OLED
 * @param baudKhz Taxa de transferência em kilohertz
 */
void configurarI2c(uint baudKhz)
{
    i2c_init(I2C_PORT, baudKhz * 1000);

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
}

/**
 * @brief Configura o PWM para gerar uma frequência específica para o buzzer
 * @param freq Frequência desejada em Hertz
 * @note Se freq <= 0, o PWM é desativado
 */
void ajustarFrequenciaPwm(float freq)
{
    if (freq <= 0.0f)
    {
        pwm_set_enabled(fatiaSlice, false);
        return;
    }

    divisorClock = clockSistema / (uint32_t)(freq * 1000);
    valorWrap = clockSistema / (divisorClock * (uint32_t)freq) - 1;

    pwm_set_clkdiv_int_frac(fatiaSlice, divisorClock, 0);
    pwm_set_wrap(fatiaSlice, valorWrap);
    pwm_set_chan_level(fatiaSlice, canalPwm, valorWrap / 2);
}

/**
 * @brief Inicializa e configura o display OLED SSD1306
 * @param display Ponteiro para a estrutura do display
 */
void configurarDisplayOled(ssd1306_t *display)
{
    ssd1306_init(display, WIDTH, HEIGHT, false, DISPLAY_ADDR, I2C_PORT);
    ssd1306_config(display);
    ssd1306_send_data(display);

    ssd1306_fill(display, false);
    ssd1306_send_data(display);
}

/**
 * @brief Tarefa FreeRTOS para leitura periódica dos sensores
 * @param pvParameters Parâmetros da tarefa (não utilizados)
 * @note Lê o nível de água e volume de chuva e envia para a fila
 */
void tarefaSensores(void *pvParameters)
{
    DadosSensor dados;

    for (;;)
    {
        // Leitura do nível de água
        adc_select_input(1);
        dados.nivelAgua = (adc_read() * 100) / 4095;

        // Leitura do volume de chuva
        adc_select_input(0);
        dados.volumeChuva = (adc_read() * 100) / 4095;

        // Envio dos dados para as outras tarefas
        xQueueSend(filaDadosSensor, &dados, portMAX_DELAY);

        // Aguarda antes da próxima leitura
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarefa FreeRTOS para atualização do display OLED
 * @param pvParameters Parâmetros da tarefa (não utilizados)
 * @note Recebe dados dos sensores e atualiza a interface visual
 */
void tarefaDisplay(void *pvParameters)
{
    ssd1306_t display;
    configurarDisplayOled(&display);

    DadosSensor dados;
    bool modoAlerta = false;
    bool corFonte = true;
    char textoBuffer[100];

    ssd1306_fill(&display, !corFonte);

    for (;;)
    {
        // Limpa o display e desenha o quadro
        ssd1306_fill(&display, !corFonte);
        ssd1306_rect(&display, 0, 0, 124, 60, corFonte, !corFonte);
        ssd1306_line(&display, 0, 18, 123, 18, corFonte);

        // Título
        ssd1306_draw_string(&display, "reservatorio", 20, 6);

        // Recebe os dados dos sensores
        if (xQueueReceive(filaDadosSensor, &dados, portMAX_DELAY) == pdTRUE)
        {
            printf("Rio: %u%%  Chuva: %u%%\n", dados.nivelAgua, dados.volumeChuva);

            // Verifica se deve ativar o modo de alerta
            modoAlerta = dados.volumeChuva >= 80 || dados.nivelAgua >= 70;

            // Exibe os dados dos sensores
            snprintf(textoBuffer, sizeof(textoBuffer), "Rio: %u%%", dados.nivelAgua);
            ssd1306_draw_string(&display, textoBuffer, 10, 40);

            snprintf(textoBuffer, sizeof(textoBuffer), "Chuva: %u%%", dados.volumeChuva);
            ssd1306_draw_string(&display, textoBuffer, 10, 50);

            // Exibe aviso se em modo de alerta
            if (modoAlerta)
            {
                ssd1306_draw_string(&display, "Evacue o local", 7, 27);
            }
        }

        // Atualiza o display
        ssd1306_send_data(&display);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

/**
 * @brief Tarefa FreeRTOS para controle do buzzer de alerta
 * @param pvParameters Parâmetros da tarefa (não utilizados)
 * @note Emite um som de alerta pulsante quando em condição crítica
 */
void tarefaBuzzer(void *pvParameters)
{
    const uint16_t freqAlerta = 1000;  // Frequência fixa de 1000Hz
    const uint16_t intervaloPulso = 250; // 250ms ligado, 250ms desligado
    
    DadosSensor dados;

    for (;;)
    {
        if (xQueueReceive(filaDadosSensor, &dados, portMAX_DELAY) == pdTRUE)
        {
            bool alerta = dados.volumeChuva >= 80 || dados.nivelAgua >= 70;

            if (alerta)
            {
                // Configura a frequência fixa de alerta
                ajustarFrequenciaPwm((float)freqAlerta);
                
                // Liga o buzzer
                pwm_set_enabled(fatiaSlice, true);
                vTaskDelay(pdMS_TO_TICKS(intervaloPulso));
                
                // Desliga o buzzer
                pwm_set_enabled(fatiaSlice, false);
                vTaskDelay(pdMS_TO_TICKS(intervaloPulso));
                
                // Verifica se saiu do modo de alerta
                xQueuePeek(filaDadosSensor, &dados, 0);
                if (dados.volumeChuva < 80 && dados.nivelAgua < 70)
                {
                    pwm_set_enabled(fatiaSlice, false);
                }
            }
            else
            {
                // Desliga o buzzer quando não em alerta
                pwm_set_enabled(fatiaSlice, false);
                gpio_put(BUZZER, 0);
            }
        }
    }
}

/**
 * @brief Tarefa FreeRTOS para controle da matriz de LEDs
 * @param pvParameters Parâmetros da tarefa (não utilizados)
 * @note Exibe diferentes padrões de cores nos LEDs conforme o nível dos sensores
 */
void tarefaMatrizLed(void *pvParameters)
{
    DadosSensor dados;
    
    for (;;)
    {
        // Limpa todos os LEDs primeiro
        npClear();
        
        if (xQueueReceive(filaDadosSensor, &dados, portMAX_DELAY) == pdTRUE)
        {
            // Verifica se o nível de chuva ou o nível de água está crítico
            if (dados.volumeChuva >= 80 || dados.nivelAgua >= 70)
            {
                // Define quais LEDs serão acesos em vermelho para indicar alerta
                uint8_t ledsVermelhos[] = {2, 12, 17, 22};
                uint8_t numLedsVermelhos = sizeof(ledsVermelhos) / sizeof(ledsVermelhos[0]);
                
                // Acende os LEDs em vermelho
                for (uint i = 0; i < numLedsVermelhos; i++)
                {
                    npSetLED(ledsVermelhos[i], 150, 0, 0);
                }
            }
            else if (dados.volumeChuva > 1) // verifica se há chuva
            {
                // Define quais LEDs serão acesos em verde para indicar chuva
                uint8_t ledsVerdes[] = {2, 3, 7, 10, 11, 12, 13, 14, 16, 17, 18, 22};
                uint8_t numLedsVerdes = sizeof(ledsVerdes) / sizeof(ledsVerdes[0]);
                
                // Acende os LEDs em verde
                for (uint i = 0; i < numLedsVerdes; i++)
                {
                    npSetLED(ledsVerdes[i], 0, 0, 150);
                }
            }
            else if (dados.volumeChuva == 0) // verifica se não há chuva
            {
                npClear();
            }
            npWrite();
        }
        npWrite();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Tarefa FreeRTOS para controle dos LEDs RGB
 * @param pvParameters Parâmetros da tarefa (não utilizados)
 * @note Pisca LEDs vermelhos em caso de alerta ou LEDs verdes em operação normal
 */
void tarefaLedsRgb(void *pvParameters)
{
    DadosSensor dados;
    bool modoAlerta = false;

    for (;;)
    {
        if (xQueueReceive(filaDadosSensor, &dados, portMAX_DELAY) == pdTRUE)
        {
            modoAlerta = dados.volumeChuva >= 80 || dados.nivelAgua >= 70;

            if (modoAlerta)
            {
                // Modo de alerta: pisca vermelho
                gpio_put(LED_R, 1);
                gpio_put(LED_G, 0);
                gpio_put(LED_B, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
                gpio_put(LED_R, 0);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            else
            {
                // Modo normal: pisca verde
                gpio_put(LED_R, 0);
                gpio_put(LED_G, 1);
                gpio_put(LED_B, 0);
                vTaskDelay(pdMS_TO_TICKS(600));
                gpio_put(LED_G, 0); 
                vTaskDelay(pdMS_TO_TICKS(400));
            }
        }
    }
}

/**
 * @brief Inicializa todos os periféricos do hardware
 * @note Configura LEDs, matriz de LEDs, ADC, botões, I2C e PWM
 */
void inicializarHardware(void)
{
    // Inicialização do stdio
    stdio_init_all();

    // Inicialização dos LEDs RGB (ordem alterada)
    configurarLedRgb(LED_G);
    configurarLedRgb(LED_R);
    configurarLedRgb(LED_B);

    // Inicialização da matriz de LEDs
    npInit(LED_PIN);
    npClear();
    npWrite();
    npSetBrightness(255);

    // Inicialização do ADC para o joystick
    configurarAdc();

    // Inicialização dos botões
    configurarBotao(BTN_A);
    configurarBotao(BTN_B);

    // Inicialização do I2C para OLED
    configurarI2c(400);

    // Inicialização do PWM para o buzzer
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    fatiaSlice = pwm_gpio_to_slice_num(BUZZER);
    canalPwm = pwm_gpio_to_channel(BUZZER);
    pwm_config config = pwm_get_default_config();
    pwm_init(fatiaSlice, &config, true);
    pwm_set_enabled(fatiaSlice, false);
}

/**
 * @brief Função principal do programa
 * @return Inteiro indicando status de saída (nunca retorna se o scheduler iniciar)
 * @note Inicializa o hardware, cria a fila de comunicação e as tarefas FreeRTOS
 */
int main()
{
    // Inicialização do hardware
    inicializarHardware();

    // Criação da fila de comunicação
    filaDadosSensor = xQueueCreate(5, sizeof(DadosSensor));

    if (filaDadosSensor != NULL)
    {
        // Criação das tarefas em ordem diferente
        xTaskCreate(tarefaLedsRgb, "RGB LEDs", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
        xTaskCreate(tarefaBuzzer, "Alarme", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
        xTaskCreate(tarefaMatrizLed, "Matriz LED", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);
        xTaskCreate(tarefaDisplay, "Display OLED", configMINIMAL_STACK_SIZE * 4, NULL, tskIDLE_PRIORITY, NULL);
        xTaskCreate(tarefaSensores, "Sensores", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY, NULL);

        // Inicia o scheduler
        vTaskStartScheduler();
    }

    // Nunca deve chegar aqui se o scheduler iniciar
    panic_unsupported();
}
