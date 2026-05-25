#include "ds18b20.h"
#include "led_panel.h"
#include "ds3231.h"
#include "logo.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

#define MENU_TIMEOUT_US   (10 * 1000000)  // 10 seconds

bool ds18b20_is_present(ds18b20_t *dev)
{
    return dev->present;
}

static int menu_active = 0;
static int64_t last_button_time = 0;

bool stop_flag = false;
bool mode_flag = false;
bool mode_entering = false;
bool format_flag = false;
bool format_entering = false;
int temporal_brightness = 0;

// Return 1=Sunday ... 7=Saturday to match DS3231
static int calculate_weekday(int day, int month, int year)
{
    if (month < 3) {
        month += 12;
        year--;
    }
    int K = year % 100;
    int J = year / 100;
    int h = (day + 13*(month + 1)/5 + K + K/4 + J/4 + 5*J) % 7;
    // Zeller's: 0=Saturday, 1=Sunday, ..., 6=Friday
    int d = ((h + 6) % 7) + 1; // 1=Sunday ... 7=Saturday
    return d;
}

typedef enum {
    BTN_MENU = 0,
    BTN_UP,
    BTN_DOWN
} button_t;

#define BTN_NONE ((button_t)-1)

static QueueHandle_t button_queue;

static void IRAM_ATTR button_isr_handler(void* arg)
{
    last_button_time = esp_timer_get_time();  // refresh timeout
	button_t btn = (button_t)(uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(button_queue, &btn, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

static void init_buttons(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_MENU) | (1ULL << PIN_UP) | (1ULL << PIN_DOWN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;  // use only external pull-ups
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;    // trigger on press (falling edge)
    gpio_config(&io_conf);

    // Create queue
    button_queue = xQueueCreate(10, sizeof(button_t));

    // Install ISR service
    gpio_install_isr_service(0);

    // Add ISR handlers
    gpio_isr_handler_add(PIN_MENU, button_isr_handler, (void*)(uint32_t)BTN_MENU);
    gpio_isr_handler_add(PIN_UP,   button_isr_handler, (void*)(uint32_t)BTN_UP);
    gpio_isr_handler_add(PIN_DOWN, button_isr_handler, (void*)(uint32_t)BTN_DOWN);
}


typedef enum {
    MENU_IDLE,
    MENU_BRIGHTNESS,
    MENU_HOUR,
    MENU_MINUTE,
    MENU_DAY,
    MENU_MONTH,
    MENU_YEAR
} menu_state_t;

static menu_state_t menu_state = MENU_IDLE;
static int brightness_level = 5; // 1–10
static ds3231_time_t tmp_time;   // temporary time editing


typedef enum {
    OFF = 0,
    ON,
} hour_format;

hour_format clock_format = OFF;


/* ===================================================== */

static int button_pin(button_t btn)
{
    switch (btn)
    {
        case BTN_MENU: return PIN_MENU;
        case BTN_UP:   return PIN_UP;
        case BTN_DOWN: return PIN_DOWN;
        default:       return -1;
    }
}

/* ===================================================== */

static bool all_buttons_released(void)
{
    return gpio_get_level(PIN_MENU) &&
           gpio_get_level(PIN_UP)   &&
           gpio_get_level(PIN_DOWN);
}

/* ===================================================== */

static void exit_menu(void)
{
    menu_active = 0;
    menu_state = MENU_IDLE;
    stop_flag = false;
    scroll_stop();
}

/* ===================================================== */

static void enter_menu(ds3231_dev_t *rtc)
{
    ESP_ERROR_CHECK(ds3231_get_time(rtc, &tmp_time));

    temporal_brightness = brightness_level;

    menu_state = MENU_BRIGHTNESS;
    menu_active = 1;
    stop_flag = true;
    scroll_stop();
    last_button_time = esp_timer_get_time();

    printf("Menu entered\n");
}

/* ===================================================== */

static void enter_mode_change(void)
{
    stop_flag = true;
    mode_flag = true;
    scroll_stop();

    printf("Mode entered\n");
}

/* ===================================================== */

static void enter_format_change(void)
{
    stop_flag = true;
    format_flag = true;
    scroll_stop();

    printf("Format entered\n");
}

/* ===================================================== */

static void save_menu_values(ds3231_dev_t *rtc)
{
    tmp_time.second = 0;

    ESP_ERROR_CHECK(ds3231_set_time(rtc, &tmp_time));

    brightness_level = temporal_brightness;
    save_brightness(brightness_level);

    set_global_brightness(brightness_level * 10);

    printf("Menu end -> exiting\n");

    exit_menu();
}

/* ===================================================== */

static void update_weekday(void)
{
    tmp_time.day_of_week = calculate_weekday(
        tmp_time.day,
        tmp_time.month,
        tmp_time.year
    );
}

/* ===================================================== */

static void handle_idle_button(button_t btn, ds3231_dev_t *rtc)
{
    switch (btn)
    {
        case BTN_MENU:
            enter_menu(rtc);
            break;

        case BTN_UP:
            enter_mode_change();
            break;

        case BTN_DOWN:
            enter_format_change();
            break;

        default:
            break;
    }
}

/* ===================================================== */

static void handle_menu_button(button_t btn, ds3231_dev_t *rtc)
{
    last_button_time = esp_timer_get_time();

    if (menu_state == MENU_IDLE)
    {
        handle_idle_button(btn, rtc);
        return;
    }

    switch (menu_state)
    {
        case MENU_BRIGHTNESS:
            if (btn == BTN_UP && temporal_brightness < 10)
                temporal_brightness++;

            if (btn == BTN_DOWN && temporal_brightness > 1)
                temporal_brightness--;

            if (btn == BTN_MENU)
                menu_state = MENU_HOUR;

            break;

        case MENU_HOUR:
            if (btn == BTN_UP)
                tmp_time.hour = (tmp_time.hour + 1) % 24;

            if (btn == BTN_DOWN)
                tmp_time.hour = (tmp_time.hour + 23) % 24;

            if (btn == BTN_MENU)
                menu_state = MENU_MINUTE;

            break;

        case MENU_MINUTE:
            if (btn == BTN_UP)
                tmp_time.minute = (tmp_time.minute + 1) % 60;

            if (btn == BTN_DOWN)
                tmp_time.minute = (tmp_time.minute + 59) % 60;

            if (btn == BTN_MENU)
                menu_state = MENU_DAY;

            break;

        case MENU_DAY:
            if (btn == BTN_UP)
                tmp_time.day = (tmp_time.day % 31) + 1;

            if (btn == BTN_DOWN)
                tmp_time.day = ((tmp_time.day + 29) % 31) + 1;

            update_weekday();

            if (btn == BTN_MENU)
                menu_state = MENU_MONTH;

            break;

        case MENU_MONTH:
            if (btn == BTN_UP)
                tmp_time.month = (tmp_time.month % 12) + 1;

            if (btn == BTN_DOWN)
                tmp_time.month = ((tmp_time.month + 10) % 12) + 1;

            update_weekday();

            if (btn == BTN_MENU)
                menu_state = MENU_YEAR;

            break;

        case MENU_YEAR:
            if (btn == BTN_UP)
                tmp_time.year = 2000 + ((tmp_time.year - 2000 + 1) % 100);

            if (btn == BTN_DOWN)
                tmp_time.year = 2000 + ((tmp_time.year - 2000 + 99) % 100);

            update_weekday();

            if (btn == BTN_MENU)
                save_menu_values(rtc);

            break;

        default:
            break;
    }
}

static void menu_task(void *arg)
{
    ds3231_dev_t *rtc = (ds3231_dev_t *)arg;

    static TickType_t press_start[3] = {0, 0, 0};

    button_t last_btn = BTN_NONE;
    button_t pending_hold_btn = BTN_NONE;

    TickType_t repeat_time = 0;

    bool ignore_until_release = false;

    while (1)
    {
        button_t btn;
        TickType_t now = xTaskGetTickCount();

        if (xQueueReceive(button_queue, &btn, pdMS_TO_TICKS(10)))
        {
            if ((now - press_start[btn]) < pdMS_TO_TICKS(DEBOUNCE_MS))
                continue;

            press_start[btn] = now;
            last_btn = btn;
            repeat_time = now + pdMS_TO_TICKS(REPEAT_DELAY);

            if (!menu_active)
            {
                pending_hold_btn = btn;
            }
            else
            {
                if (!ignore_until_release)
                    handle_menu_button(btn, rtc);
            }
        }
        else
        {
            if (!menu_active && pending_hold_btn != BTN_NONE)
            {
                int pin = button_pin(pending_hold_btn);

                if (pin >= 0 && !gpio_get_level(pin))
                {
                    if ((now - press_start[pending_hold_btn]) >= pdMS_TO_TICKS(1000))
                    {
                        handle_menu_button(pending_hold_btn, rtc);

                        pending_hold_btn = BTN_NONE;
                        last_btn = BTN_NONE;
                        ignore_until_release = true;
                    }
                }
            }

            if (menu_active &&
                last_btn != BTN_NONE &&
                (now - press_start[last_btn]) >= pdMS_TO_TICKS(REPEAT_DELAY))
            {
                if ((now - repeat_time) >= pdMS_TO_TICKS(REPEAT_RATE))
                {
                    handle_menu_button(last_btn, rtc);
                    repeat_time = now;
                }
            }
        }

        if (all_buttons_released())
        {
            last_btn = BTN_NONE;
            pending_hold_btn = BTN_NONE;
            ignore_until_release = false;
        }

        if (menu_active)
        {
            int64_t now_us = esp_timer_get_time();

            if ((now_us - last_button_time > MENU_TIMEOUT_US) &&
                menu_state != MENU_YEAR)
            {
                printf("Menu timeout -> exiting\n");
                exit_menu();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static ds18b20_t sensor;
static int16_t current_temp = 0;
static bool temp_valid = false;

void temp_task(void *arg)
{
    const TickType_t period = pdMS_TO_TICKS(5000);
    TickType_t last_wake = xTaskGetTickCount();

    while (1)
    {
        int16_t t;

        // Start measurement
        if (ds18b20_start_conversion(&sensor) == ESP_OK)
        {
            vTaskDelay(pdMS_TO_TICKS(750)); // conversion time

            if (ds18b20_read_scratchpad_temp(&sensor, &t) == ESP_OK)
            {
                current_temp = t;

                if (current_temp < -9)
                    current_temp = -9;

                temp_valid = true;
            }
            else
            {
                temp_valid = false;
            }
        }
        else
        {
            temp_valid = false;
        }

        vTaskDelayUntil(&last_wake, period);
    }
}

typedef enum {
    DISPLAY_LOGO = 0,
    DISPLAY_TIME,
    //DISPLAY_LOGO2,
    DISPLAY_DATE,
	DISPLAY_TEMPERATURE
} display_mode_t;

typedef enum {
    UNO = 1,
    DOS,
    TRES,
	ROTATION,
} display_mode_t_0;

display_mode_t_0 mode0 = ROTATION;

const char *dias_semana[] = {
    "DOMINGO", "LUNES", "MARTES", "MIERCOLES", "JUEVES", "VIERNES", "SABADO"
};

const char *meses[] = {
    "ENERO", "FEBRERO", "MARZO", "ABRIL", "MAYO", "JUNIO",
    "JULIO", "AGOSTO", "SEPTIEMBRE", "OCTUBRE", "NOVIEMBRE", "DICIEMBRE"
};

static void get_temp_color(int *r, int *g, int *b)
{
    if (current_temp < 10)
    {
        *r = 255;
        *g = 255;
        *b = 255;      // white
    }
    else if (current_temp < 20)
    {
        *r = 0;
        *g = 255;
        *b = 255;      // cyan
    }
    else if (current_temp < 30)
    {
        *r = 255;
        *g = 65;
        *b = 0;        // orange
    }
    else
    {
        *r = 255;
        *g = 0;
        *b = 0;        // red
    }
}

/* ===================================================== */

static int get_display_hour(const ds3231_time_t *time)
{
    int hour = (int)time->hour;

    if (hour < 0)
        hour = 0;

    if (hour > 23)
        hour = 23;

    if (clock_format)
        return hour;     // 24H format

    int hour12 = hour % 12;

    if (hour12 == 0)
        hour12 = 12;

    return hour12;
}

static int safe_minute(const ds3231_time_t *time)
{
    int minute = (int)time->minute;

    if (minute < 0)
        minute = 0;

    if (minute > 59)
        minute = 59;

    return minute;
}

static int safe_second(const ds3231_time_t *time)
{
    int second = (int)time->second;

    if (second < 0)
        second = 0;

    if (second > 59)
        second = 59;

    return second;
}

/* ===================================================== */

static void make_temp_text(char *buf, size_t size, bool with_c)
{
    if (temp_valid)
    {
        if (with_c)
            snprintf(buf, size, "%d*C", current_temp);
        else
            snprintf(buf, size, "%d*", current_temp);
    }
    else
    {
        snprintf(buf, size, "T E");
    }
}

/* ===================================================== */

static int get_weekday_index(const ds3231_time_t *time)
{
    return (time->day_of_week - 1) % 7;
}

/* ===================================================== */

static void start_date_scroll_if_needed(
    const ds3231_time_t *time,
    int y,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    int speed_px_per_sec
)
{
    int weekday_index = get_weekday_index(time);

    static char buf_date[64];

    snprintf(
        buf_date,
        sizeof(buf_date),
        "%s %d %s %04d",
        dias_semana[weekday_index],
        time->day,
        meses[time->month - 1],
        time->year
    );

    if (!scroll_is_active())
    {
        scroll_start(buf_date, y, r, g, b, speed_px_per_sec);
    }

    scroll_update();
}

/* ===================================================== */

static void draw_logo_screen(void)
{
    draw_bitmap_rgb(0, 0, logo_bitmap, LOGO_WIDTH, LOGO_HEIGHT);
}

/* ===================================================== */

/*
static void draw_logo2_screen(void)
{
    draw_bitmap_rgb(0, 0, logo_bitmap2, LOGO_WIDTH, LOGO_HEIGHT);
}
*/

/* ===================================================== */

static void draw_time_screen(const ds3231_time_t *time, int r_temp, int g_temp, int b_temp)
{
    int hour = get_display_hour(time);
    int minute = safe_minute(time);
    int second = safe_second(time);

    char buf_time[24];

    if (hour < 10)
    {
        snprintf(
            buf_time,
            sizeof(buf_time),
            " %1d:%02d:%02d",
            hour,
            minute,
            second
        );
    }
    else
    {
        snprintf(
            buf_time,
            sizeof(buf_time),
            "%02d:%02d:%02d",
            hour,
            minute,
            second
        );
    }

    draw_text(4, 1, buf_time, 255, 255, 255);

    char buf_temp[16];
    make_temp_text(buf_temp, sizeof(buf_temp), true);
    draw_text(20, 22, buf_temp, r_temp, g_temp, b_temp);

    start_date_scroll_if_needed(time, 12, 0, 255, 0, 10);
}

/* ===================================================== */

static void draw_big_clock_screen(const ds3231_time_t *time, int r_temp, int g_temp, int b_temp)
{
    char buf_hour[4];
    char buf_minute[8];
    char buf_second[8];

    int pos_hour = 0;
    int hour = get_display_hour(time);
    int minute = safe_minute(time);
    int second = safe_second(time);

    bool colon_on = (second % 2) == 0;

    if (!clock_format)
    {
        if (time->hour > 11)
            draw_text_5(51, 16, "&$", 255, 255, 255);   // PM
        else
            draw_text_5(51, 16, "#$", 255, 255, 255);   // AM
    }
    else
    {
        snprintf(buf_second, sizeof(buf_second), "%02d", second);
        draw_text_5(51, 16, buf_second, 255, 255, 255);
    }

    snprintf(buf_minute, sizeof(buf_minute), "%02d", minute);

    if (minute % 10 == 1)
        pos_hour = 2;

		if (hour < 10)
		{
		    buf_hour[0] = ' ';
		    buf_hour[1] = '0' + hour;
		    buf_hour[2] = '\0';

		    pos_hour -= 1;

		    draw_text_2(pos_hour - 1, 14, buf_hour, 255, 255, 255);
		    draw_text_2(27 + pos_hour + 1, 14, buf_minute, 255, 255, 255);
		}
		else
		{
		    buf_hour[0] = '0' + (hour / 10);
		    buf_hour[1] = '0' + (hour % 10);
		    buf_hour[2] = '\0';

		    if (hour > 19)
		    {
		        pos_hour += 1;

		        if (minute % 10 == 1)
		            pos_hour -= 1;
		    }

		    draw_text_2(pos_hour, 14, buf_hour, 255, 255, 255);
		    draw_text_2(27 + pos_hour, 14, buf_minute, 255, 255, 255);
		}

    if (!clock_format)
        draw_text_4(23 + pos_hour, 17, colon_on ? "!" : " ", 255, 255, 255);
    else
        draw_text_4(23 + pos_hour, 17, "!", 255, 255, 255);

    char buf_temp[20];
    make_temp_text(buf_temp, sizeof(buf_temp), false);

    draw_text_6(50, 26, buf_temp, r_temp, g_temp, b_temp);
    draw_text_4(57, 26, "#", r_temp, g_temp, b_temp);
    draw_text_6(60, 26, "$", r_temp, g_temp, b_temp);

    start_date_scroll_if_needed(time, 2, 0, 0, 255, 10);
}

/* ===================================================== */

static int get_weekday_x_position(int weekday_index)
{
    if (weekday_index == 0 || weekday_index == 5)
        return 1 + 6;

    if (weekday_index == 1)
        return 2 + 12;

    if (weekday_index == 2 || weekday_index == 4 || weekday_index == 6)
        return 1 + 9;

    return 0;
}

/* ===================================================== */

static void draw_date_temp_screen(const ds3231_time_t *time, int r_temp, int g_temp, int b_temp)
{
    int weekday_index = get_weekday_index(time);
    int pos_day = get_weekday_x_position(weekday_index);

    char buf_day[32];

    snprintf(buf_day, sizeof(buf_day), "%s", dias_semana[weekday_index]);
    draw_text(1 + pos_day, 1, buf_day, 0, 255, 0);

    char buf_date[32];

    snprintf(
        buf_date,
        sizeof(buf_date),
        "%02d-%02d-%02d",
        time->day,
        time->month,
        time->year - 2000
    );

    draw_text(4, 11, buf_date, 0, 0, 255);

    int hour = get_display_hour(time);
    int minute = safe_minute(time);
    int second = safe_second(time);

    bool colon_on = (second % 2) == 0;

    char buf_time[24];

    if (hour < 10)
    {
        snprintf(
            buf_time,
            sizeof(buf_time),
            colon_on ? " %1d:%02d" : " %1d %02d",
            hour,
            minute
        );
    }
    else
    {
        snprintf(
            buf_time,
            sizeof(buf_time),
            colon_on ? "%02d:%02d" : "%02d %02d",
            hour,
            minute
        );
    }

    draw_text(2, 22, buf_time, 255, 255, 255);

    char buf_temp[16];
    make_temp_text(buf_temp, sizeof(buf_temp), false);
    draw_text(43, 22, buf_temp, r_temp, g_temp, b_temp);
}

/* ===================================================== */

void draw_display(display_mode_t mode, ds3231_time_t *time)
{
    clear_back_buffer();

    int r_temp = 0;
    int g_temp = 0;
    int b_temp = 0;

    get_temp_color(&r_temp, &g_temp, &b_temp);

    switch (mode)
    {
        case DISPLAY_LOGO:
            draw_logo_screen();
            break;

/*
case DISPLAY_LOGO2:
    draw_logo2_screen();
    break;
*/

        case DISPLAY_TIME:
            draw_time_screen(time, r_temp, g_temp, b_temp);
            break;

        case DISPLAY_DATE:
            draw_big_clock_screen(time, r_temp, g_temp, b_temp);
            break;

        case DISPLAY_TEMPERATURE:
            draw_date_temp_screen(time, r_temp, g_temp, b_temp);
            break;

        default:
            break;
    }

    swap_buffers();
}

static void show_message(
    const char *msg,
    int x,
    int y,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint32_t delay_ms
)
{
    clear_back_buffer();
    draw_text(x, y, msg, r, g, b);
    swap_buffers();

    if (delay_ms > 0)
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

/* ===================================================== */

static void run_display_for(
    ds3231_dev_t *rtc,
    display_mode_t display,
    uint32_t duration_ms
)
{
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t duration_ticks = pdMS_TO_TICKS(duration_ms);

    ds3231_time_t now;

    scroll_stop();

    while ((xTaskGetTickCount() - start_tick) < duration_ticks)
    {
        ESP_ERROR_CHECK(ds3231_get_time(rtc, &now));

        draw_display(display, &now);

        // 50 ms update period, but split into 10 ms slices
        // so buttons / stop_flag can interrupt faster.
        for (uint32_t elapsed = 0; elapsed < 50; elapsed += 10)
        {
            if (stop_flag)
                return;

            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (stop_flag)
            return;
    }
}

/* ===================================================== */

static void draw_menu_screen(void)
{
    char buf[32];
    int x = 0;

    switch (menu_state)
    {
        case MENU_BRIGHTNESS:
            snprintf(buf, sizeof(buf), "BRILLO:%d", temporal_brightness);
            x = 1;
            break;

        case MENU_HOUR:
            snprintf(buf, sizeof(buf), " HORA:%02d", tmp_time.hour);
            x = 0;
            break;

        case MENU_MINUTE:
            snprintf(buf, sizeof(buf), "MINUTO:%02d", tmp_time.minute);
            x = 1;
            break;

        case MENU_DAY:
            snprintf(buf, sizeof(buf), " DIA:%02d", tmp_time.day);
            x = 0;
            break;

        case MENU_MONTH:
            snprintf(buf, sizeof(buf), " MES:%02d", tmp_time.month);
            x = 0;
            break;

        case MENU_YEAR:
            snprintf(buf, sizeof(buf), " A|O:%02d", tmp_time.year - 2000);
            x = 0;
            break;

        default:
            snprintf(buf, sizeof(buf), " ");
            x = 0;
            break;
    }

    show_message(buf, x, 8, 255, 0, 0, 0);
}

/* ===================================================== */

static uint32_t get_rotation_duration_ms(
    display_mode_t display,
    int mode_interval_s
)
{
    switch (display)
    {
        case DISPLAY_LOGO:
        //case DISPLAY_LOGO2:
            return (mode_interval_s * 1000) / 7;

        case DISPLAY_TIME:
        case DISPLAY_DATE:
        case DISPLAY_TEMPERATURE:
        default:
            return mode_interval_s * 1000;
    }
}

/* ===================================================== */

void drawing_task(void *arg)
{
    ds3231_dev_t *rtc = (ds3231_dev_t *)arg;

    display_mode_t mode = DISPLAY_LOGO;
    const int mode_interval_s = 21;

    char buf[32];

    // Startup logo
    clear_back_buffer();
    draw_bitmap_rgb(0, 0, logo_bitmap, LOGO_WIDTH, LOGO_HEIGHT);
    swap_buffers();
    vTaskDelay(pdMS_TO_TICKS(3000));

    // Startup mode message
    snprintf(buf, sizeof(buf), "MODO:%d", mode0);
    show_message(buf, 8, 8, 255, 0, 0, 3000);

    while (1)
    {
        /* ================= MENU ================= */

        if (menu_state != MENU_IDLE)
        {
            draw_menu_screen();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* ================= NORMAL DISPLAY ================= */

        switch (mode0)
        {
            case ROTATION:
            {
                uint32_t duration_ms = get_rotation_duration_ms(mode, mode_interval_s);

                run_display_for(rtc, mode, duration_ms);

                mode++;

                if (mode > DISPLAY_TEMPERATURE)
                    mode = DISPLAY_LOGO;

                break;
            }

            case UNO:
            {
                run_display_for(
                    rtc,
                    DISPLAY_LOGO,
                    (mode_interval_s * 1000) / 7
                );

                if (!stop_flag)
                {
                    run_display_for(
                        rtc,
                        DISPLAY_TIME,
                        mode_interval_s * 1000
                    );
                }

                break;
            }

            case DOS:
            {
                run_display_for(
                    rtc,
                    DISPLAY_LOGO,
                    (mode_interval_s * 1000) / 7
                );

                if (!stop_flag)
                {
                    run_display_for(
                        rtc,
                        DISPLAY_DATE,
                        (mode_interval_s * 1000)
                    );
                }

                break;
            }

            case TRES:
            {
                run_display_for(
                    rtc,
                    DISPLAY_LOGO,
                    (mode_interval_s * 1000) / 7
                );

                if (!stop_flag)
                {
                    run_display_for(
                        rtc,
                        DISPLAY_TEMPERATURE,
                        mode_interval_s * 1000
                    );
                }

                break;
            }

            default:
            {
                mode0 = ROTATION;
                mode = DISPLAY_LOGO;
                break;
            }
        }

        /* ================= FORMAT BUTTON ================= */

        if (format_flag)
        {
            format_flag = false;
            stop_flag = false;

            clock_format = 1 - clock_format;
            save_format(clock_format);

            show_message(
                !clock_format ? "24HRS:OFF" : "24HRS:ON",
                1,
                8,
                255,
                0,
                0,
                1000
            );
        }

        /* ================= MODE BUTTON ================= */

        if (mode_flag)
        {
            mode_flag = false;
            stop_flag = false;

            mode0++;
			
			/* ================= MODE LIMIT ================= */

			if (mode0 > ROTATION)
			{
			    mode = DISPLAY_LOGO;
			    mode0 = UNO;
			    save_mode(mode0);
			}
			else {
				save_mode(mode0);				
			}


            snprintf(buf, sizeof(buf), "MODO:%d", mode0);
            show_message(buf, 8, 8, 255, 0, 0, 1000);
        }
    }
}


// ---------------- example usage in app_main ----------------
void app_main(void)
{
    init_pins();

	init_oe_pwm();           // initialize OE PWM
	init_nvs_brightness();	
	brightness_level = load_brightness();
	
	if (brightness_level > 10) brightness_level = 10;	
	
	set_global_brightness(brightness_level * 10);	
	
	mode0 = load_mode();

	if (mode0 > ROTATION)
	    mode0 = ROTATION;

		if (mode0 < UNO)
		    mode0 = UNO;	
	
	clock_format = load_format();
	
	
	if (clock_format > 1) clock_format = 0; 

    ds3231_dev_t rtc;
    ESP_ERROR_CHECK(init_ds3231(&rtc));
    
    if (ds18b20_init(&sensor, GPIO_NUM_27) != ESP_OK) {
    ESP_LOGW("MAIN", "DS18B20 init issue");
}

	ds3231_time_t now;
	ds3231_get_time(&rtc, &now);

	if(now.year < 2025)
	{
		ds3231_time_t set_time = {2025, 8, 18, 13, 52, 0, 2};
    	ESP_ERROR_CHECK(ds3231_set_time(&rtc, &set_time));
	}

	init_planes();

    // Clear both buffers first time
    memset((void*)fbA, 0, sizeof(fbA));
    memset((void*)fbB, 0, sizeof(fbB));

	init_buttons();

    // Start refresh task (pin-driving) on core 0
	xTaskCreatePinnedToCore(refresh_task, "refresh_task", 2048, NULL, 1, NULL, 0);
	xTaskCreatePinnedToCore(drawing_task, "DrawTime", 4096, &rtc, 1, NULL, 1);	
	xTaskCreatePinnedToCore(temp_task,      "TempTask",      1024, NULL, 2, NULL, 1);
	xTaskCreatePinnedToCore(menu_task, "MenuTask", 4096, &rtc, 2, NULL, 1);

    while (true) 
	{
        vTaskDelay(pdMS_TO_TICKS(1));

    }
}

