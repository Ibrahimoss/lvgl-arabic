/**
 * @file uart_keypad.c
 * The serial console as an LVGL keypad.
 *
 * QEMU has no touchscreen, so input comes from the monitor window: what you
 * type there arrives on UART0 and goes to the focused widget.
 *
 *   UTF-8 text          -> characters (Arabic included; the monitor sends UTF-8)
 *   Backspace           -> LV_KEY_BACKSPACE
 *   Delete              -> LV_KEY_DEL
 *   Enter               -> LV_KEY_ENTER
 *   arrows              -> LV_KEY_LEFT / RIGHT / UP / DOWN
 *   Home / End          -> LV_KEY_HOME / LV_KEY_END
 *   Tab / Shift+Tab     -> LV_KEY_NEXT / LV_KEY_PREV (move focus between widgets)
 *   Esc                 -> LV_KEY_ESC
 */

#include "uart_keypad.h"
#include <stdbool.h>
#include "driver/uart.h"
#include "driver/uart_vfs.h"

#define KEYPAD_UART UART_NUM_0
#define QUEUE_LEN   64

/*Terminals send arrow keys as ESC [ x in one burst, but the UART can hand the
 *bytes over in pieces. An ESC counts as the Esc key only after this long alone.*/
#define ESC_ALONE_MS 100

/*Ring buffer of decoded keys. LVGL keys for text are the UTF-8 bytes of the
 *character packed little-endian into a uint32_t (what lv_textarea_add_char() expects).*/
static uint32_t queue[QUEUE_LEN];
static uint32_t q_head;
static uint32_t q_tail;

/*Decoder state, kept across reads so split sequences still work*/
static uint32_t utf8_key;
static uint8_t utf8_have;
static uint8_t utf8_need;
static bool esc_active;
static uint32_t esc_tick;
static uint8_t esc_buf[8];
static uint8_t esc_len;
static bool last_was_cr;

static void (*command_cb)(uint8_t ctrl);

void uart_keypad_set_command_cb(void (*cb)(uint8_t ctrl))
{
    command_cb = cb;
}

static void push(uint32_t key)
{
    uint32_t next = (q_head + 1) % QUEUE_LEN;
    if(next == q_tail) return; /*full: drop the key*/
    queue[q_head] = key;
    q_head = next;
}

/*ANSI escape sequence (after ESC) to LVGL key; 0 if unknown*/
static uint32_t escape_to_key(const uint8_t * s, uint8_t len)
{
    if(len < 2) return 0;
    uint8_t final = s[len - 1];

    if(s[0] == 'O' && len == 2) { /*SS3: ESC O x*/
        switch(final) {
            case 'A': return LV_KEY_UP;
            case 'B': return LV_KEY_DOWN;
            case 'C': return LV_KEY_RIGHT;
            case 'D': return LV_KEY_LEFT;
            case 'H': return LV_KEY_HOME;
            case 'F': return LV_KEY_END;
            default: return 0;
        }
    }

    if(s[0] != '[') return 0;

    switch(final) { /*CSI: ESC [ ... x*/
        case 'A': return LV_KEY_UP;
        case 'B': return LV_KEY_DOWN;
        case 'C': return LV_KEY_RIGHT;
        case 'D': return LV_KEY_LEFT;
        case 'H': return LV_KEY_HOME;
        case 'F': return LV_KEY_END;
        case 'Z': return LV_KEY_PREV; /*Shift+Tab*/
        case '~':
            if(len == 3) {
                switch(s[1]) {
                    case '1': case '7': return LV_KEY_HOME;
                    case '4': case '8': return LV_KEY_END;
                    case '3': return LV_KEY_DEL;
                    default: return 0;
                }
            }
            return 0;
        default:
            return 0;
    }
}

static void feed(uint8_t b)
{
    if(esc_active) {
        if(esc_len == 0 && b != '[' && b != 'O') {
            /*a lone Esc followed by an ordinary byte*/
            esc_active = false;
            push(LV_KEY_ESC);
        }
        else {
            esc_buf[esc_len++] = b;
            bool done = (esc_len >= 2 && (esc_buf[0] == 'O' || (b >= 0x40 && b <= 0x7E)));
            if(done || esc_len == sizeof(esc_buf)) {
                uint32_t key = escape_to_key(esc_buf, esc_len);
                if(key) push(key);
                esc_active = false;
            }
            return;
        }
    }

    if(utf8_need) { /*continuation byte of a multi-byte character*/
        if((b & 0xC0) == 0x80) {
            utf8_key |= (uint32_t)b << (8 * utf8_have);
            utf8_have++;
            if(utf8_have == utf8_need) {
                push(utf8_key);
                utf8_need = 0;
            }
            return;
        }
        utf8_need = 0; /*broken sequence: drop it and treat b as a fresh byte*/
    }

    bool was_cr = last_was_cr;
    last_was_cr = false;

    if(b >= 0x80) {
        if((b & 0xE0) == 0xC0) utf8_need = 2;
        else if((b & 0xF0) == 0xE0) utf8_need = 3;
        else if((b & 0xF8) == 0xF0) utf8_need = 4;
        else return; /*invalid lead byte*/
        utf8_key = b;
        utf8_have = 1;
        return;
    }

    switch(b) {
        case 0x1B:
            esc_active = true;
            esc_len = 0;
            esc_tick = lv_tick_get();
            break;
        case '\r':
            last_was_cr = true;
            push(LV_KEY_ENTER);
            break;
        case '\n':
            if(!was_cr) push(LV_KEY_ENTER); /*CR LF is one Enter*/
            break;
        case 0x08:
        case 0x7F:
            push(LV_KEY_BACKSPACE);
            break;
        case '\t':
            push(LV_KEY_NEXT);
            break;
        default:
            if(b >= 0x20) push(b);            /*printable ASCII*/
            else if(command_cb) command_cb(b); /*other control codes: commands (main.c)*/
            break;
    }
}

static void poll_uart(void)
{
    uint8_t buf[64];
    int n;
    while((n = uart_read_bytes(KEYPAD_UART, buf, sizeof(buf), 0)) > 0) {
        for(int i = 0; i < n; i++) feed(buf[i]);
    }

    if(esc_active && esc_len == 0 && lv_tick_elaps(esc_tick) > ESC_ALONE_MS) {
        esc_active = false;
        push(LV_KEY_ESC);
    }
}

/*Each key is reported pressed, then released on the next call. Returning true
 *makes LVGL call again at once, so a burst of typing is handled in one cycle.*/
static bool read_cb(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
    static uint32_t last_key;
    static bool pressed;
    (void)drv;

    if(pressed) {
        pressed = false;
        data->key = last_key;
        data->state = LV_INDEV_STATE_REL;
        return q_head != q_tail;
    }

    poll_uart();

    if(q_head != q_tail) {
        last_key = queue[q_tail];
        q_tail = (q_tail + 1) % QUEUE_LEN;
        pressed = true;
        data->key = last_key;
        data->state = LV_INDEV_STATE_PR;
        return true;
    }

    data->key = last_key;
    data->state = LV_INDEV_STATE_REL;
    return false;
}

lv_indev_t * uart_keypad_create(void)
{
    /*RX through the driver; route printf through it too so the two don't collide*/
    ESP_ERROR_CHECK(uart_driver_install(KEYPAD_UART, 1024, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(KEYPAD_UART);

    static lv_indev_drv_t drv;
    lv_indev_drv_init(&drv);
    drv.type = LV_INDEV_TYPE_KEYPAD;
    drv.read_cb = read_cb;
    return lv_indev_drv_register(&drv);
}
