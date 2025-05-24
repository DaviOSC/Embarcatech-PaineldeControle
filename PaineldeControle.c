#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "lib/ssd1306.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/bootrom.h"
#include "stdio.h"
#include "config.h"

#define MAX_CONT 8
#define LED_PIN_RED 13
#define LED_PIN_GREEN 11
#define LED_PIN_BLUE 12
#define BUZZER_PIN 21
#define BUTTON_PIN_A 5
#define BUTTON_PIN_B 6
#define BUTTON_PIN_J 22

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ENDERECO 0x3C

volatile uint16_t usuariosAtivos = 0;
ssd1306_t ssd;
SemaphoreHandle_t xContadorSem;
SemaphoreHandle_t xResetSem;
SemaphoreHandle_t xDisplayMutex;

SemaphoreHandle_t xEntradaEventSem;
SemaphoreHandle_t xSaidaEventSem;
SemaphoreHandle_t xUsuariosMutex;

uint16_t eventosProcessados = 0;

void print_display(const char *linha1, const char *linha2, const char *linha3)
{
    xSemaphoreTake(xDisplayMutex, portMAX_DELAY);
    ssd1306_fill(&ssd, 0);

    // Moldura e linhas de organização
    ssd1306_rect(&ssd, 3, 3, 122, 60, true, false); // Retângulo de borda
    ssd1306_line(&ssd, 3, 23, 123, 23, true);       // Linha horizontal 1
    ssd1306_line(&ssd, 3, 43, 123, 43, true);       // Linha horizontal 2

    // Linhas de texto organizadas
    if (linha1)
        ssd1306_draw_string(&ssd, linha1, 8, 10);
    if (linha2)
        ssd1306_draw_string(&ssd, linha2, 8, 29);
    if (linha3)
        ssd1306_draw_string(&ssd, linha3, 8, 48);

    ssd1306_send_data(&ssd);
    xSemaphoreGive(xDisplayMutex);
}

void vTaskEntrada(void *params)
{
    char buffer[32];
    while (true)
    {
        if (xSemaphoreTake(xEntradaEventSem, portMAX_DELAY) == pdTRUE)
        {
            if (xSemaphoreTake(xContadorSem, 0) == pdTRUE)
            { // Tenta pegar uma vaga
                xSemaphoreTake(xUsuariosMutex, portMAX_DELAY);
                usuariosAtivos++;
                xSemaphoreGive(xUsuariosMutex);

                sprintf(buffer, "Usuarios: %d/%d", usuariosAtivos, MAX_CONT);
                print_display(buffer, "Entrada", "Registrada!");
            }
            else
            {
                beep(BUZZER_PIN, 100);
                xSemaphoreTake(xUsuariosMutex, portMAX_DELAY);
                uint16_t temp = usuariosAtivos;
                xSemaphoreGive(xUsuariosMutex);

                sprintf(buffer, "Usuarios: %d/%d", temp, MAX_CONT);
                print_display(buffer, "Limite", "Atingido!");
            }
        }
    }
}
void vTaskSaida(void *params)
{
    char buffer[32];
    while (true)
    {
        if (xSemaphoreTake(xSaidaEventSem, portMAX_DELAY) == pdTRUE)
        {
            xSemaphoreTake(xUsuariosMutex, portMAX_DELAY);
            if (usuariosAtivos > 0)
            {
                usuariosAtivos--;
                xSemaphoreGive(xUsuariosMutex);
                xSemaphoreGive(xContadorSem);
                sprintf(buffer, "Usuarios: %d/%d", usuariosAtivos, MAX_CONT);
                print_display(buffer, "Saida", "Registrada!");
            }
            else
            {
                xSemaphoreGive(xUsuariosMutex);
                sprintf(buffer, "Usuarios: %d/%d", usuariosAtivos, MAX_CONT);
                print_display(buffer, "Nenhum usuario", "Para sair!");
            }
        }
    }
}

void vTaskReset(void *params)
{
    while (true)
    {
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE)
        {
            beep(BUZZER_PIN, 100);
            vTaskDelay(pdMS_TO_TICKS(100));
            beep(BUZZER_PIN, 100);

            xSemaphoreTake(xUsuariosMutex, portMAX_DELAY);
            usuariosAtivos = 0;
            xSemaphoreGive(xUsuariosMutex);

            for (int i = 0; i < MAX_CONT; i++)
            {
                xSemaphoreGive(xContadorSem);
            }
            char buffer[32];
            sprintf(buffer, "Usuarios: %d/%d", usuariosAtivos, MAX_CONT);
            print_display(buffer, "Sistema", "Resetado!");
        }
    }
}

void set_led_rgb(bool r, bool g, bool b)
{
    gpio_put(LED_PIN_RED, r);
    gpio_put(LED_PIN_GREEN, g);
    gpio_put(LED_PIN_BLUE, b);
}

void vTaskLed(void *params)
{
    while (true)
    {
        xSemaphoreTake(xUsuariosMutex, portMAX_DELAY);
        uint16_t n = usuariosAtivos;
        xSemaphoreGive(xUsuariosMutex);

        if (n == 0)
        {
            set_led_rgb(0, 0, 1);
        }
        else if (n < MAX_CONT - 1)
        {
            set_led_rgb(0, 1, 0);
        }
        else if (n == MAX_CONT - 1)
        {
            set_led_rgb(1, 1, 0);
        }
        else if (n >= MAX_CONT)
        {
            set_led_rgb(1, 0, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
// ISR do botão A (incrementa o semáforo de contagem)
void gpio_A_callback(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xEntradaEventSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
void gpio_B_callback(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xSaidaEventSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ISR do botão de reset
void gpio_reset_callback(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

static uint32_t last_button_time = 0;
#define DEBOUNCE_MS 200

void gpio_irq_handler(uint gpio, uint32_t events)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_button_time > DEBOUNCE_MS)
    {
        last_button_time = now;
        if (gpio == BUTTON_PIN_B)
        {
            gpio_B_callback(gpio, events);
        }
        else if (gpio == BUTTON_PIN_A)
        {
            gpio_A_callback(gpio, events);
        }
        else if (gpio == BUTTON_PIN_J)
        {
            gpio_reset_callback(gpio, events);
        }
    }
}

int main()
{
    stdio_init_all();

    pwm_init_buzzer(BUZZER_PIN);

    // Configuração do LED RGB
    gpio_init(LED_PIN_RED);
    gpio_set_dir(LED_PIN_RED, GPIO_OUT);
    gpio_init(LED_PIN_GREEN);
    gpio_set_dir(LED_PIN_GREEN, GPIO_OUT);
    gpio_init(LED_PIN_BLUE);
    gpio_set_dir(LED_PIN_BLUE, GPIO_OUT);
    // Inicialização do display
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    // Configura os botões
    gpio_init(BUTTON_PIN_A);
    gpio_set_dir(BUTTON_PIN_A, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_A);

    gpio_init(BUTTON_PIN_B);
    gpio_set_dir(BUTTON_PIN_B, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_B);

    gpio_init(BUTTON_PIN_J);
    gpio_set_dir(BUTTON_PIN_J, GPIO_IN);
    gpio_pull_up(BUTTON_PIN_J);

    gpio_set_irq_enabled_with_callback(BUTTON_PIN_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_PIN_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_PIN_J, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    gpio_set_irq_enabled(BUTTON_PIN_B, GPIO_IRQ_EDGE_FALL, true);

    xContadorSem = xSemaphoreCreateCounting(MAX_CONT, MAX_CONT);
    xEntradaEventSem = xSemaphoreCreateBinary();
    xSaidaEventSem = xSemaphoreCreateBinary();
    xResetSem = xSemaphoreCreateBinary();
    xDisplayMutex = xSemaphoreCreateMutex();
    xUsuariosMutex = xSemaphoreCreateMutex();

    // Cria tarefa
    xTaskCreate(vTaskLed, "LedTask", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskEntrada, "TaskEntrada", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "TaskSaida", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "TaskReset", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    print_display("Esperando", "Input", "Para iniciar");

    vTaskStartScheduler();
    panic_unsupported();
}