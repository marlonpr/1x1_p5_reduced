#include "led_panel.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
//#include "font20x40.h"
#include "font12x16.h"
#include "font2x9.h"
#include "font5x5.h"
#include "font3x5.h"
#include "font10x15.h"
#include "soc/gpio_struct.h"  // for GPIO register access
#include "nvs_flash.h"
#include "nvs.h"


// Gamma corrected values for 3-bit PWM (0..7)
const uint8_t gamma_table[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,
    3,3,3,3,3,3,3,3,4,4,4,4,4,4,4,4,
    4,4,4,4,5,5,5,5,5,5,5,5,6,6,6,6,
    6,6,6,6,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
};




















/* ===================================================== */
/* Scroll text state                                    */
/* ===================================================== */

typedef struct
{
    char text[64];
    float x;
    int y;
    uint8_t r;
    uint8_t g;
    uint8_t b;
    bool active;
    TickType_t last_tick;
    int speed_px_per_sec;
} scroll_state_t;

static scroll_state_t scroll_state = {0};

/* ===================================================== */

void scroll_start(
    const char *text,
    int y,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    int speed_px_per_sec
)
{
    if (text == NULL)
        return;

    strncpy(scroll_state.text, text, sizeof(scroll_state.text) - 1);
    scroll_state.text[sizeof(scroll_state.text) - 1] = '\0';

    scroll_state.x = 63.0f;
    scroll_state.y = y;

    scroll_state.r = r;
    scroll_state.g = g;
    scroll_state.b = b;

    scroll_state.speed_px_per_sec = speed_px_per_sec;
    scroll_state.last_tick = xTaskGetTickCount();
    scroll_state.active = true;
}

/* ===================================================== */

void scroll_update(void)
{
    if (!scroll_state.active)
        return;

    TickType_t now = xTaskGetTickCount();
    TickType_t elapsed_ms =
        (now - scroll_state.last_tick) * portTICK_PERIOD_MS;

    if (elapsed_ms > 0)
    {
        float delta =
            ((float)scroll_state.speed_px_per_sec * (float)elapsed_ms) / 1000.0f;

        scroll_state.x -= delta;
        scroll_state.last_tick = now;
    }

    int draw_x = (int)scroll_state.x;

    draw_text(
        draw_x,
        scroll_state.y,
        scroll_state.text,
        scroll_state.r,
        scroll_state.g,
        scroll_state.b
    );

    int text_width = strlen(scroll_state.text) * FONT_WIDTH;

    if ((draw_x + text_width + (FONT_WIDTH * 4)) < 0)
    {
        scroll_state.x = 63.0f;
        scroll_state.active = false;
    }
}

/* ===================================================== */

void scroll_stop(void)
{
    scroll_state.active = false;
}

/* ===================================================== */

bool scroll_is_active(void)
{
    return scroll_state.active;
}





















//--------------------------------------------NVS-------------------------------------------------------

void init_nvs_brightness()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void save_brightness(uint8_t level)
{
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("settings", NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, "brightness", level));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
}

uint8_t load_brightness()
{
    nvs_handle_t nvs_handle;
    uint8_t level = 10; // default 10 if not set

    if (nvs_open("settings", NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, "brightness", &level);
        nvs_close(nvs_handle);
    }

    return level;
}


void save_mode(uint8_t value)
{
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("settings", NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, "mode", value));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
}

uint8_t load_mode()
{
    nvs_handle_t nvs_handle;
    uint8_t value = 4; // default 0 if not set

    if (nvs_open("settings", NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, "mode", &value);
        nvs_close(nvs_handle);
    }

    return value;
}











void save_format(uint8_t format)
{
    nvs_handle_t nvs_handle;
    ESP_ERROR_CHECK(nvs_open("settings", NVS_READWRITE, &nvs_handle));
    ESP_ERROR_CHECK(nvs_set_u8(nvs_handle, "format", format));
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
    nvs_close(nvs_handle);
}

uint8_t load_format()
{
    nvs_handle_t nvs_handle;
    uint8_t format = 0; // default 0 if not set

    if (nvs_open("settings", NVS_READONLY, &nvs_handle) == ESP_OK) {
        nvs_get_u8(nvs_handle, "format", &format);
        nvs_close(nvs_handle);
    }

    return format;
}










//--------------------------------------------------------------------------------------------------------------
static volatile uint8_t global_brightness = 100;  // 0..100%

// Initialize OE PWM
void init_oe_pwm(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode      = OE_SPEED_MODE,
        .duty_resolution = OE_DUTY_RES,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = OE_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t chan_conf = {
        .speed_mode     = OE_SPEED_MODE,
        .channel        = OE_CHANNEL,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = PIN_OE,
        .duty           = 0,               // start OFF
        .hpoint         = 0,
        .flags = {
            .output_invert = 1             // invert OE
        }
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan_conf));
}

// Update OE duty according to global brightness (0-100%)
static void update_oe_duty(void)
{
    uint32_t duty = (global_brightness * OE_MAX_DUTY) / 100; // scale 0..100 -> 0..63
    ESP_ERROR_CHECK(ledc_set_duty(OE_SPEED_MODE, OE_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(OE_SPEED_MODE, OE_CHANNEL));
}

// Set brightness as percentage (0-100)
void set_global_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    global_brightness = percent;
    update_oe_duty();
}
//--------------------------------------------------------------------------------------------------------------

void init_planes(void) {
    for (int p = 0; p < COLOR_DEPTH; ++p) {
        front_planes[p] = fbA[p];
        back_planes [p] = fbB[p];
    }
}



// ------------ Pins init -------------
void init_pins(void) {
    uint64_t mask = (1ULL<<PIN_R1) | (1ULL<<PIN_G1) | (1ULL<<PIN_B1)
                  | (1ULL<<PIN_R2) | (1ULL<<PIN_G2) | (1ULL<<PIN_B2)
                  | (1ULL<<PIN_A)  | (1ULL<<PIN_B)  | (1ULL<<PIN_C)
                  | (1ULL<<PIN_CLK)| (1ULL<<PIN_LAT);

    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    //gpio_set_level(PIN_OE, 1);
    gpio_set_level(PIN_LAT, 0);
    gpio_set_level(PIN_CLK, 0);
}

void clear_back_buffer(void) {
    for (int p = 0; p < COLOR_DEPTH; ++p) {
        memset(back_planes[p], 0, PHY_HEIGHT * PHY_WIDTH);
    }
}

void swap_buffers(void) {
    for (int p = 0; p < COLOR_DEPTH; ++p) {
        uint8_t (*tmp)[PHY_WIDTH] = front_planes[p];
        front_planes[p] = back_planes[p];
        back_planes[p]  = tmp;
    }
}

// ------------ Virtual->Physical mapping set_pixel -------------
//
// You draw in a virtual grid N_HOR x N_VER:
// panel_col = x / PANEL_WIDTH
// panel_row = y / PANEL_HEIGHT
// phys_panel_index = panel_row * N_HOR + panel_col   (linear horizontal chain)
// phys_x = phys_panel_index * PANEL_WIDTH + (x % PANEL_WIDTH)
// phys_y = y % PANEL_HEIGHT
//
void set_pixel(int x, int y, int r8, int g8, int b8) {
    if ((unsigned)x >= (unsigned)VIRT_WIDTH || (unsigned)y >= (unsigned)VIRT_HEIGHT) return;

    // same mapping as your code:
    int panel_col       = x / PANEL_WIDTH;
    int panel_row       = y / PANEL_HEIGHT;
    int phys_panel_idx  = panel_row * N_HOR + panel_col;
    int local_x         = x % PANEL_WIDTH;
    int local_y         = y % PANEL_HEIGHT;
    int phys_x          = phys_panel_idx * PANEL_WIDTH + local_x;
    int phys_y          = local_y;
    if ((unsigned)phys_x >= (unsigned)PHY_WIDTH || (unsigned)phys_y >= (unsigned)PHY_HEIGHT) return;

	// New gamma corrected:
	uint8_t rQ = gamma_table[r8];
	uint8_t gQ = gamma_table[g8];
	uint8_t bQ = gamma_table[b8];

    // Write each plane bit into the plane buffers
    for (int plane = 0; plane < COLOR_DEPTH; ++plane) {
        uint8_t v =
            (((rQ >> plane) & 1) ? 0x01 : 0) |
            (((gQ >> plane) & 1) ? 0x02 : 0) |
            (((bQ >> plane) & 1) ? 0x04 : 0);
        back_planes[plane][phys_y][phys_x] = v;
    }
}


// ------------ HUB75 refresh task (1/4 scan) -------------
//
// Keeps your column-scanning logic pattern: total_cols = PANEL_WIDTH * PHYS_PANELS * 2
// panel_index  = col / PANEL_WIDTH  -> 0..(2*PHYS_PANELS-1)
// panel_in_row = panel_index / 2     (which physical panel in the chain)
// y1 = (panel_index % 2) ? row : row + scan_rows
// y2 = y1 + 2*scan_rows
//
void refresh_task(void *arg) {
    const int scan_rows  = PANEL_HEIGHT / 4;              // e.g., 8 for 32px
    const int total_cols = PANEL_WIDTH * PHYS_PANELS * 2; // two halves

    while (1) {
        for (int plane = 0; plane < COLOR_DEPTH; ++plane) {
            int weight = 1 << plane; // binary weight for PWM

            for (int row = 0; row < scan_rows; row++) {

                // OE off while we change address / shift
				// Duty = max → PWM is always HIGH → OE stays HIGH → panel off
				ledc_set_duty(OE_SPEED_MODE, OE_CHANNEL, 0);
				ledc_update_duty(OE_SPEED_MODE, OE_CHANNEL);

                // Set row address ABC
                uint32_t set_mask = 0, clr_mask = 0;
                if (row & 0x01) set_mask |= BIT_A; else clr_mask |= BIT_A;
                if (row & 0x02) set_mask |= BIT_B; else clr_mask |= BIT_B;
                if (row & 0x04) set_mask |= BIT_C; else clr_mask |= BIT_C;
                GPIO.out_w1ts = set_mask;
                GPIO.out_w1tc = clr_mask;

                // Shift out all columns (exactly your logic)
                for (int col = 0; col < total_cols; col++) {
                    int panel_index    = col / PANEL_WIDTH;      // 0..(2*PHYS_PANELS-1)
                    int local_x        = col % PANEL_WIDTH;
                    int panel_in_chain = panel_index / 2;        // 0..(PHYS_PANELS-1)
                    int fb_x           = panel_in_chain * PANEL_WIDTH + local_x;

                    uint32_t set2 = 0, clr2 = 0;

                    // Upper/lower halves for this scan step
                    int y1 = (panel_index % 2) ? row : row + scan_rows;
                    int y2 = y1 + 2 * scan_rows;

                    uint8_t p1 = 0, p2 = 0;
                    if ((unsigned)fb_x < (unsigned)PHY_WIDTH) {
                        if ((unsigned)y1 < (unsigned)PHY_HEIGHT) p1 = front_planes[plane][y1][fb_x];
                        if ((unsigned)y2 < (unsigned)PHY_HEIGHT) p2 = front_planes[plane][y2][fb_x];
                    }

                    // Row 1 colors
                    if (p1 & 0x01) set2 |= BIT_R1; else clr2 |= BIT_R1;
                    if (p1 & 0x02) set2 |= BIT_G1; else clr2 |= BIT_G1;
                    if (p1 & 0x04) set2 |= BIT_B1; else clr2 |= BIT_B1;

                    // Row 2 colors
                    if (p2 & 0x01) set2 |= BIT_R2; else clr2 |= BIT_R2;
                    if (p2 & 0x02) set2 |= BIT_G2; else clr2 |= BIT_G2;
                    if (p2 & 0x04) set2 |= BIT_B2; else clr2 |= BIT_B2;

                    GPIO.out_w1ts = set2;
                    GPIO.out_w1tc = clr2;

                    // Clock
                    GPIO.out_w1ts = BIT_CLK;
                    GPIO.out_w1tc = BIT_CLK;
                }

                // Latch
                GPIO.out_w1ts = BIT_LAT;
                GPIO.out_w1tc = BIT_LAT;

				// Duty = 0 → PWM is always LOW → OE stays LOW → panel on
				update_oe_duty();

                for (int t = 0; t < weight; ++t) { // Enable for weighted time slice
                    esp_rom_delay_us(BASE_US);    // very short unit, e.g. 20 µs
                }
            }
        }
    }
}

// set_pixel(x, y, r, g, b) is your existing function
// VIRT_WIDTH, VIRT_HEIGHT are the virtual drawing dimensions

// ----------------- Draw a single character -----------------
void draw_char(int x, int y, char c, int r, int g, int b) {
    if (c < 32 || c > 126) c = '?';  // fallback
    int index = c - 32;

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            if (font6x9[index][row][col]) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < VIRT_WIDTH && py >= 0 && py < VIRT_HEIGHT) {
                    set_pixel(px, py, r, g, b);
                }
            }
        }
    }
}

// ----------------- Draw a string with 1-pixel spacing -----------------
void draw_text(int x, int y, const char *text, int r, int g, int b) {
    int cursor_x = x;
    while (*text) {
        draw_char(cursor_x, y, *text, r, g, b);
        cursor_x += FONT_WIDTH + 1;  // advance cursor by font width + 1 pixel spacing
        text++;
    }
}


// ----------------- Scroll text horizontally (improved) -----------------
void scroll_text(const char *text, int y, int r, int g, int b, int speed_ms) {
    int len = strlen(text);
    int text_width = len * (FONT_WIDTH + 1); // include spacing

    // Precompute virtual width including the panel width
    int total_scroll = text_width + VIRT_WIDTH;

    // For each frame
    for (int scroll_x = 0; scroll_x < total_scroll; scroll_x++) {

        // Draw only visible area
        for (int plane = 0; plane < COLOR_DEPTH; plane++) {
            // Optionally zero only the part that will change
            memset(back_planes[plane], 0, PHY_HEIGHT * PHY_WIDTH);
        }

        int cursor_x = VIRT_WIDTH - scroll_x;
        const char *p = text;
        while (*p) {
            draw_char(cursor_x, y, *p, r, g, b);
            cursor_x += FONT_WIDTH + 1;
            p++;
        }

        swap_buffers();
		if (stop_flag) break;   // condition to exit early                     // atomic plane swap
        vTaskDelay(pdMS_TO_TICKS(speed_ms));
    }
}

// Draws RGB logo from 1D bitmap
void draw_bitmap_rgb(int x0, int y0, const uint32_t *bmp, int w, int h) {
    int idx = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = bmp[idx++];
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8)  & 0xFF;
            uint8_t b = c & 0xFF;
            set_pixel(x0 + x, y0 + y, r, g, b);
        }
    }
}


























// set_pixel(x, y, r, g, b) is your existing function
// VIRT_WIDTH, VIRT_HEIGHT are the virtual drawing dimensions

// ----------------- Draw a single character -----------------          !!      //----------- USE ANOTHER FONT_HEIGHT , WIDTH AND  EG: FONT6X9
void draw_char_2(int x, int y, char c, int r, int g, int b) {
    if (c < 32 || c > 126) c = '?';  // fallback
    int index = c - 32;

    for (int row = 0; row < FONT_HEIGHT_5; row++) {
        for (int col = 0; col < FONT_WIDTH_5; col++) {
            if (font10x15[index][row][col]) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < VIRT_WIDTH && py >= 0 && py < VIRT_HEIGHT) {
                    set_pixel(px, py, r, g, b);
                }
            }
        }
    }
}

// ----------------- Draw a string with 1-pixel spacing -----------------            !!            //----------- USE ANOTHER FONT_HEIGHT , WIDTH AND  EG: FONT6X9
void draw_text_2(int x, int y, const char *text, int r, int g, int b) {
    int cursor_x = x;
    while (*text) {
        draw_char_2(cursor_x, y, *text, r, g, b);
        cursor_x += FONT_WIDTH_5 + 1;  // advance cursor by font width + 1 pixel spacing
        text++;
    }
}






// set_pixel(x, y, r, g, b) is your existing function
// VIRT_WIDTH, VIRT_HEIGHT are the virtual drawing dimensions

// ----------------- Draw a single character -----------------                //----------- USE ANOTHER FONT_HEIGHT , WIDTH AND  EG: FONT6X9
void draw_char_4(int x, int y, char c, int r, int g, int b) {
    if (c < 32 || c > 126) c = '?';  // fallback
    int index = c - 32;

    for (int row = 0; row < FONT_HEIGHT_4; row++) {
        for (int col = 0; col < FONT_WIDTH_4; col++) {
            if (font2x9[index][row][col]) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < VIRT_WIDTH && py >= 0 && py < VIRT_HEIGHT) {
                    set_pixel(px, py, r, g, b);
                }
            }
        }
    }
}

// ----------------- Draw a string with 1-pixel spacing -----------------
void draw_text_4(int x, int y, const char *text, int r, int g, int b) {
    int cursor_x = x;
    while (*text) {
        draw_char_4(cursor_x, y, *text, r, g, b);
        cursor_x += FONT_WIDTH_4 + 1;  // advance cursor by font width + 1 pixel spacing
        text++;
    }
}








// ----------------- Draw a single character -----------------                //----------- USE ANOTHER FONT_HEIGHT , WIDTH AND  EG: FONT6X9


void draw_char_5(int x, int y, char c, int r, int g, int b) {
    if (c < 32 || c > 126) c = '?';  // fallback
    int index = c - 32;

    for (int row = 0; row < FONT_HEIGHT_8; row++) {
        for (int col = 0; col < FONT_WIDTH_8; col++) {
            if (font5x5[index][row][col]) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < VIRT_WIDTH && py >= 0 && py < VIRT_HEIGHT) {
                    set_pixel(px, py, r, g, b);
                }
            }
        }
    }
}

// ----------------- Draw a string with 1-pixel spacing -----------------
void draw_text_5(int x, int y, const char *text, int r, int g, int b) {
    int cursor_x = x;
    while (*text) {
        draw_char_5(cursor_x, y, *text, r, g, b);
        cursor_x += FONT_WIDTH_8 + 1;  // advance cursor by font width + 1 pixel spacing
        text++;
    }
}






// ----------------- Draw a single character -----------------                //----------- USE ANOTHER FONT_HEIGHT , WIDTH AND  EG: FONT6X9


void draw_char_6(int x, int y, char c, int r, int g, int b) {
    if (c < 32 || c > 126) c = '?';  // fallback
    int index = c - 32;

    for (int row = 0; row < FONT_HEIGHT_9; row++) {
        for (int col = 0; col < FONT_WIDTH_9; col++) {
            if (font3x5[index][row][col]) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < VIRT_WIDTH && py >= 0 && py < VIRT_HEIGHT) {
                    set_pixel(px, py, r, g, b);
                }
            }
        }
    }
}

// ----------------- Draw a string with 1-pixel spacing -----------------
void draw_text_6(int x, int y, const char *text, int r, int g, int b) {
    int cursor_x = x;
    while (*text) {
        draw_char_6(cursor_x, y, *text, r, g, b);
        cursor_x += FONT_WIDTH_9 + 1;  // advance cursor by font width + 1 pixel spacing
        text++;
    }
}




