#include "config.h"

SemaphoreHandle_t xContadorSem; // Semáforo para controlar o número de usuários ativos
SemaphoreHandle_t xUsuariosMutex; // Mutex para controlar o acesso à variável usuariosAtivos
SemaphoreHandle_t xResetSem; // Semáforo para o botão de reset
SemaphoreHandle_t xEntradaEventSem; // Semáforo para eventos de entrada
SemaphoreHandle_t xSaidaEventSem;// Semáforo para eventos de saída
SemaphoreHandle_t xDisplayMutex; // Mutex para acesso ao display


void print_display(const char *linha1, const char *linha2, const char *linha3)
{
    xSemaphoreTake(xDisplayMutex, portMAX_DELAY); // Bloqueia o acesso ao display
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
    xSemaphoreGive(xDisplayMutex); // Libera o acesso ao display
}

void vTaskEntrada(void *params)
{
    char buffer[32];
    while (true)
    {
        if (xSemaphoreTake(xEntradaEventSem, portMAX_DELAY) == pdTRUE) // Evento de entrada
        {
            if (xSemaphoreTake(xContadorSem, 0) == pdTRUE)  // Tenta pegar uma vaga
            {
                xSemaphoreTake(xUsuariosMutex, portMAX_DELAY); // Bloqueia o acesso à variável usuariosAtivos
                usuariosAtivos++; // Incrementa o número de usuários ativos
                xSemaphoreGive(xUsuariosMutex); // Libera o acesso à variável usuariosAtivos

                sprintf(buffer, "Quant: %d/%d", usuariosAtivos, MAX_CONT);
                print_display(buffer, "Entrada", "Registrada!");
            }
            else
            {
                beep(BUZZER_PIN, 100);
                xSemaphoreTake(xUsuariosMutex, portMAX_DELAY); // Bloqueia o acesso à variável usuariosAtivos
                uint16_t temp = usuariosAtivos; // Captura o número atual de usuários ativos
                xSemaphoreGive(xUsuariosMutex); // Libera o acesso à variável usuariosAtivos

                sprintf(buffer, "Quant: %d/%d", temp, MAX_CONT);
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
        if (xSemaphoreTake(xSaidaEventSem, portMAX_DELAY) == pdTRUE) // Evento de saída
        {
            xSemaphoreTake(xUsuariosMutex, portMAX_DELAY); // Bloqueia o acesso à variável usuariosAtivos
            if (usuariosAtivos > 0)
            {
                usuariosAtivos--;
                xSemaphoreGive(xUsuariosMutex); // Libera o acesso à variável usuariosAtivos
                xSemaphoreGive(xContadorSem); // Libera uma vaga no semáforo de contagem
                sprintf(buffer, "Quant: %d/%d", usuariosAtivos, MAX_CONT);
                print_display(buffer, "Saida", "Registrada!");
            }
            else
            {
                xSemaphoreGive(xUsuariosMutex);
                sprintf(buffer, "Quant: %d/%d", usuariosAtivos, MAX_CONT);
                print_display(buffer, "Nenhum usuario", "Para sair!");
            }
        }
    }
}

void vTaskReset(void *params)
{
    while (true)
    {
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) // Evento de reset
        {
            beep(BUZZER_PIN, 100);
            vTaskDelay(pdMS_TO_TICKS(100));
            beep(BUZZER_PIN, 100);

            xSemaphoreTake(xUsuariosMutex, portMAX_DELAY); // Bloqueia o acesso à variável usuariosAtivos
            usuariosAtivos = 0;
            xSemaphoreGive(xUsuariosMutex); // Libera o acesso à variável usuariosAtivos

            // Reseta o semáforo de contagem iterando até o máximo
            for (int i = 0; i < MAX_CONT; i++)
            {
                xSemaphoreGive(xContadorSem);
            }

            char buffer[32];
            sprintf(buffer, "Quant: %d/%d", usuariosAtivos, MAX_CONT);
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
        xSemaphoreTake(xUsuariosMutex, portMAX_DELAY); // Bloqueia o acesso à variável usuariosAtivos
        uint16_t n = usuariosAtivos;
        xSemaphoreGive(xUsuariosMutex); // Libera o acesso à variável usuariosAtivos

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
        vTaskDelay(pdMS_TO_TICKS(100)); // Aguarda 100 ms
    }
}
// ISR do botão A (incrementa o semáforo de contagem)
void gpio_A_callback(uint gpio, uint32_t events)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xEntradaEventSem, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
// ISR do botão B (decrementa o semáforo de contagem)
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

    // Cria semáforos e mutexes
    xContadorSem = xSemaphoreCreateCounting(MAX_CONT, MAX_CONT);
    xEntradaEventSem = xSemaphoreCreateBinary();
    xSaidaEventSem = xSemaphoreCreateBinary();
    xResetSem = xSemaphoreCreateBinary();
    xDisplayMutex = xSemaphoreCreateMutex();
    xUsuariosMutex = xSemaphoreCreateMutex();

    // Cria tarefas
    xTaskCreate(vTaskLed, "LedTask", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskEntrada, "TaskEntrada", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "TaskSaida", configMINIMAL_STACK_SIZE, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "TaskReset", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

    // Mensagem inicial
    print_display("Pressione", "Qualquer botao", "Para iniciar");

    vTaskStartScheduler();
    panic_unsupported();
}