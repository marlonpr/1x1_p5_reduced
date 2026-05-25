#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "font6x9.h"


// ------------ CONFIG: panel + layout ------------
#define PANEL_WIDTH    64
#define PANEL_HEIGHT   32    // must be divisible by 4 (1/4 scan)
#define N_HOR          1     // logical panels horizontally
#define N_VER          1     // logical panels vertically

// Virtual drawing size (what you use in set_pixel)
#define VIRT_WIDTH   (PANEL_WIDTH  * N_HOR)
#define VIRT_HEIGHT  (PANEL_HEIGHT * N_VER)

// Physical chain: all panels wired as one long horizontal row
#define PHYS_PANELS  (N_HOR * N_VER)
#define PHY_WIDTH    (PANEL_WIDTH  * PHYS_PANELS)
#define PHY_HEIGHT   (PANEL_HEIGHT)

// ------------ GPIO PINS (adjust as needed) ------------
#define PIN_R1  GPIO_NUM_2
#define PIN_G1  GPIO_NUM_4
#define PIN_B1  GPIO_NUM_5
#define PIN_R2  GPIO_NUM_18
#define PIN_G2  GPIO_NUM_19
#define PIN_B2  GPIO_NUM_25
#define PIN_CLK GPIO_NUM_13
#define PIN_LAT GPIO_NUM_12
#define PIN_OE  GPIO_NUM_14
#define PIN_A   GPIO_NUM_15
#define PIN_B   GPIO_NUM_26
#define PIN_C   GPIO_NUM_23


//temperature sensor GPIO_NUM_27 ( INITIALIZE IN MAIN)


// Precalculate bitmasks for speed
#define BIT_R1 (1 << PIN_R1)
#define BIT_G1 (1 << PIN_G1)
#define BIT_B1 (1 << PIN_B1)
#define BIT_R2 (1 << PIN_R2)
#define BIT_G2 (1 << PIN_G2)
#define BIT_B2 (1 << PIN_B2)

#define BIT_A  (1 << PIN_A)
#define BIT_B  (1 << PIN_B)
#define BIT_C  (1 << PIN_C)

#define BIT_CLK (1 << PIN_CLK)
#define BIT_LAT (1 << PIN_LAT)
#define BIT_OE (1 << PIN_OE)

//LEDC
#define OE_DUTY_RES       LEDC_TIMER_6_BIT      // 6-bit PWM
#define OE_MAX_DUTY       ((1 << OE_DUTY_RES)-1)  // 63
#define OE_FREQ_HZ        1000000               // 1 MHz safe for 6-bit
#define OE_SPEED_MODE     LEDC_HIGH_SPEED_MODE
#define OE_CHANNEL        LEDC_CHANNEL_0

// ===== Brightness / PWM config =====
#define COLOR_DEPTH   3              // 3 bits per color (0..7). You can try 4 later.
#define BASE_US       30             // base OE time per LSB slice (tune 15–30us)

//buttons
#define PIN_MENU    GPIO_NUM_33
#define PIN_UP      GPIO_NUM_32
#define PIN_DOWN    GPIO_NUM_16

#define DEBOUNCE_MS    500      // minimum time between presses
#define REPEAT_DELAY   500     // initial delay before repeating
#define REPEAT_RATE    500    // repeat interval while holding


// Plane buffers: same physical size, but one per PWM plane
// Each cell packs R,G,B bits like your old buffer: bit0=R, bit1=G, bit2=B
static uint8_t (*front_planes[COLOR_DEPTH])[PHY_WIDTH];
static uint8_t (*back_planes [COLOR_DEPTH])[PHY_WIDTH];

// Allocate concrete storage (A/B for double buffering)
static uint8_t fbA[COLOR_DEPTH][PHY_HEIGHT][PHY_WIDTH];
static uint8_t fbB[COLOR_DEPTH][PHY_HEIGHT][PHY_WIDTH];

extern bool stop_flag;


typedef struct {
    const char *text;   // text to scroll (can include '\n' for multiple lines)
    int  pos_x;         // current horizontal offset in pixels
    int  speed;         // pixels per frame
    int  lines;         // number of lines
	int color;
	int done;
} scroll_text_t;













/* ===================================================== */
/* Scroll text API                                      */
/* ===================================================== */

void scroll_start(
    const char *text,
    int y,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    int speed_px_per_sec
);

void scroll_update(void);

void scroll_stop(void);

bool scroll_is_active(void);



















void init_pins(void);
void init_oe_pwm(void);
void set_global_brightness(uint8_t level);
void refresh_task(void *arg);
void clear_back_buffer(void);
void swap_buffers(void);










void draw_text(int x, int y, const char *s, int r, int g, int b);
void scroll_text(const char *text, int y, int r, int g, int b, int speed_ms);
void draw_char(int x, int y, char c, int r, int g, int b);








void draw_text_2(int x, int y, const char *s, int r, int g, int b);
void scroll_text_2(const char *text, int y, int r, int g, int b, int speed_ms);
void draw_char_2(int x, int y, char c, int r, int g, int b);




void draw_char_4(int x, int y, char c, int r, int g, int b);

void draw_text_4(int x, int y, const char *text, int r, int g, int b);






void draw_char_5(int x, int y, char c, int r, int g, int b);

void draw_text_5(int x, int y, const char *text, int r, int g, int b);




void draw_char_6(int x, int y, char c, int r, int g, int b);

void draw_text_6(int x, int y, const char *text, int r, int g, int b);






















void init_planes(void);
void draw_bitmap_rgb(int x0, int y0, const uint32_t *bmp, int w, int h);
void set_global_brightness_pct(uint8_t percent);

void set_pixel(int x, int y, int r8, int g8, int b8);

void init_nvs_brightness();

void save_brightness(uint8_t level);
uint8_t load_brightness();




void save_mode(uint8_t value);
uint8_t load_mode();


void save_format(uint8_t value);
uint8_t load_format();










