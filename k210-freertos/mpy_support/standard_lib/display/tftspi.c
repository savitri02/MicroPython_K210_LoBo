/*
 * This file is part of the MicroPython K210 project, https://github.com/loboris/MicroPython_K210_LoBo
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 LoBo (https://github.com/loboris)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
/*
 *  Author: LoBo (loboris@gmail.com, loboris.github)
 *
 *  Module supporting MAIX TFT displays
 * 
 * HIGH SPEED LOW LEVEL DISPLAY FUNCTIONS
 * USING DIRECT or DMA SPI TRANSFER MODEs
 *
*/

#include "mpconfigport.h"

#if MICROPY_USE_DISPLAY

#define USE_DISPLAY_TASK    0

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <devices.h>
#include "tftspi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "syslog.h"
#include "mphalport.h"
#include "gpiohs.h"


#define WAIT_CYCLE      4U
#define LCD_X_MAX   240
#define LCD_Y_MAX   320
#define TFT_BGR     8
// RGB to GRAYSCALE constants
// 0.2989  0.5870  0.1140
#define GS_FACT_R 0.2989
#define GS_FACT_G 0.4870
#define GS_FACT_B 0.2140

#define SPI_CHANNEL     (0)
#define DCX_IO          (38)
#define TFT_RST         (37)

enum _instruction_length
{
    INSTRUCTION_LEN_0 = 0,
    INSTRUCTION_LEN_8 = 8,
    INSTRUCTION_LEN_16 = 16,
    INSTRUCTION_LEN_32 = 32,
} ;

enum _address_length
{
    ADDRESS_LEN_0 = 0,
    ADDRESS_LEN_8 = 8,
    ADDRESS_LEN_16 = 16,
    ADDRESS_LEN_32 = 32,
} ;

enum _frame_length
{
    FRAME_LEN_0 = 0,
    FRAME_LEN_8 = 8,
    FRAME_LEN_16 = 16,
    FRAME_LEN_32 = 32,
} ;

volatile gpiohs_t* const gpiohs = (volatile gpiohs_t*)GPIOHS_BASE_ADDR;

static handle_t gio;
static handle_t spi0;
static handle_t spi_dfs8;
static handle_t spi_dfs16;
static handle_t spi_dfs32;

typedef struct {
    uint8_t dc;
    uint8_t type;
    uint16_t len;
    void *data;
} disp_msg_t;

#define DISP_NUM_FUNC           4
#define DISP_SPI_SLAVE_SELECT   3  // FUNC_SPI0_SS3

static bool display_pins_init = false;
static int tft_rst_gpionum = 0;
static int tft_dc_gpionum = 0;
static mp_fpioa_cfg_item_t disp_pin_func[DISP_NUM_FUNC];

// ====================================================
// ==== Global variables, default values ==============

bool use_frame_buffer = false;
// Converts colors to grayscale if set to 1
uint8_t gray_scale = 0;

// Display dimensions
int _width = DEFAULT_TFT_DISPLAY_WIDTH;
int _height = DEFAULT_TFT_DISPLAY_HEIGHT;

// Display type
uint8_t tft_disp_type = DEFAULT_DISP_TYPE;
uint8_t tft_touch_type = TOUCH_TYPE_NONE;

uint8_t TFT_RGB_BGR = 0;
uint8_t gamma_curve = 0;
uint32_t spi_speed = 4000000;

uint16_t tft_frame_buffer[DEFAULT_TFT_DISPLAY_WIDTH*DEFAULT_TFT_DISPLAY_HEIGHT] = {0};
// ====================================================

//static const char TAG[] = "[TFTSPI]";
static uint8_t invertrot = 1;

// ==== Functions =====================

/*
static void set_bit(volatile uint32_t *bits, uint32_t mask, uint32_t value)
{
    uint32_t org = (*bits) & ~mask;
    *bits = org | (value & mask);
}

static void set_bit_offset(volatile uint32_t *bits, uint32_t mask, size_t offset, uint32_t value)
{
    set_bit(bits, mask << offset, value << offset);
}

static void set_gpio_bit(volatile uint32_t *bits, size_t offset, uint32_t value)
{
    set_bit_offset(bits, 1, offset, value);
}
*/
//------------------------------------------------------
void gpiohs_set_pin(uint8_t pin, gpio_pin_value_t value)
{
    //set_gpio_bit(gpiohs->output_val.u32, pin, value);
    uint32_t org = (*gpiohs->output_val.u32) & ~(1 << pin);
    *gpiohs->output_val.u32 = org | (value & (1 << pin));
}

//---------------------------
static void set_dcx_control()
{
    //gpio_set_pin_value(gio, tft_dc_gpionum, GPIO_PV_LOW);
    gpiohs_set_pin(tft_dc_gpionum, GPIO_PV_LOW);
}

//------------------------
static void set_dcx_data()
{
    //gpio_set_pin_value(gio, tft_dc_gpionum, GPIO_PV_HIGH);
    gpiohs_set_pin(tft_dc_gpionum, GPIO_PV_HIGH);
}

//----------------------
uint32_t tft_set_speed()
{
    spi_dev_set_clock_rate(spi_dfs8, 3000000);
    uint32_t speed = (uint32_t)spi_dev_set_clock_rate(spi_dfs16, spi_speed);
    spi_dev_set_clock_rate(spi_dfs32, spi_speed);
    return speed;
}

//-----------------------------
static bool tft_hard_init(void)
{
    // Configure display pins
    if (!display_pins_init) {
        // FPIOA configuration
        tft_dc_gpionum = gpiohs_get_free();
        if (tft_dc_gpionum < 0) return false;
        tft_rst_gpionum = gpiohs_get_free();
        if (tft_rst_gpionum < 0) {
            gpiohs_set_free(tft_dc_gpionum);
            return false;
        }

        disp_pin_func[0] = (mp_fpioa_cfg_item_t){tft_dc_gpionum, DCX_IO, FUNC_GPIOHS0 + tft_dc_gpionum};
        disp_pin_func[1] = (mp_fpioa_cfg_item_t){-1, 36, FUNC_SPI0_SS3};
        disp_pin_func[2] = (mp_fpioa_cfg_item_t){-1, 39, FUNC_SPI0_SCLK};
        disp_pin_func[3] = (mp_fpioa_cfg_item_t){tft_rst_gpionum, TFT_RST, FUNC_GPIOHS0 + tft_rst_gpionum};

        if (!fpioa_check_pins(DISP_NUM_FUNC, disp_pin_func, GPIO_FUNC_DISP)) {
            gpiohs_set_free(tft_dc_gpionum);
            gpiohs_set_free(tft_rst_gpionum);
            return false;
        }
        // Setup and mark used pins
        fpioa_setup_pins(DISP_NUM_FUNC, disp_pin_func);
        fpioa_setused_pins(DISP_NUM_FUNC, disp_pin_func, GPIO_FUNC_DISP);
        display_pins_init = true;
    }

    // open gpiohs device
    gio = io_open("/dev/gpio0");
    if (gio == 0) return false;

    gpio_set_drive_mode(gio, tft_dc_gpionum, GPIO_DM_OUTPUT);
    gpio_set_pin_value(gio, tft_dc_gpionum, GPIO_PV_HIGH);

    gpio_set_drive_mode(gio, tft_rst_gpionum, GPIO_DM_OUTPUT);
    gpio_set_pin_value(gio, tft_rst_gpionum, GPIO_PV_HIGH);

    // Set SPI0_D0-D7 DVP_D0-D7 as spi and dvp data pin
    sysctl_set_spi0_dvp_data(1);

    // openSPI device
    spi0 = io_open("/dev/spi0");
    if (spi0 == 0) return false;

    spi_dfs8 = spi_get_device(spi0, SPI_MODE_0, SPI_FF_OCTAL, 1 << DISP_SPI_SLAVE_SELECT, FRAME_LEN_8);
    if (spi_dfs8 == 0) return false;
    spi_dev_config_non_standard(spi_dfs8, INSTRUCTION_LEN_8, ADDRESS_LEN_0, WAIT_CYCLE, SPI_AITM_AS_FRAME_FORMAT);

    spi_dfs16 = spi_get_device(spi0, SPI_MODE_0, SPI_FF_OCTAL, 1 << DISP_SPI_SLAVE_SELECT, FRAME_LEN_16);
    if (spi_dfs16 == 0) return false;
    spi_dev_config_non_standard(spi_dfs16, INSTRUCTION_LEN_16, ADDRESS_LEN_0, WAIT_CYCLE, SPI_AITM_AS_FRAME_FORMAT);

    spi_dfs32 = spi_get_device(spi0, SPI_MODE_0, SPI_FF_OCTAL, 1 << DISP_SPI_SLAVE_SELECT, FRAME_LEN_32);
    if (spi_dfs32 == 0) return false;
    spi_dev_config_non_standard(spi_dfs32, INSTRUCTION_LEN_0, ADDRESS_LEN_32, WAIT_CYCLE, SPI_AITM_AS_FRAME_FORMAT);

    tft_set_speed();

    return true;
}

//----------------------------------------
static void tft_write_command(uint8_t cmd)
{
    set_dcx_control();
    io_write(spi_dfs8, (const uint8_t *)(&cmd), 1);
}

//------------------------------------------------------------
static void tft_write_byte(uint8_t* data_buf, uint32_t length)
{
    set_dcx_data();
    io_write(spi_dfs8, (const uint8_t *)(data_buf), length);
}

//-------------------------------------------------------------
static void tft_write_half(uint16_t* data_buf, uint32_t length)
{
    set_dcx_data();
    io_write(spi_dfs16, (const uint8_t *)(data_buf), length * 2);
}

/*
//-------------------------------------------------------------
static void tft_write_word(uint32_t* data_buf, uint32_t length)
{
    set_dcx_data();
    io_write(spi_dfs32, (const uint8_t *)data_buf, length * 4);
}
*/

//-------------------------------------------------------
static void tft_fill_data(uint32_t data, uint32_t length)
{
    set_dcx_data();
    spi_dev_fill(spi_dfs32, 0, data, data, length/2 - 1);
}


//---------------------------------
static bool tft_init(uint8_t hw_sw)
{
    uint8_t data;

    if (!tft_hard_init()) return false;

    if (hw_sw & 0x02) {
        // hard reset
        gpio_set_pin_value(gio, tft_rst_gpionum, GPIO_PV_LOW);
        mp_hal_delay_ms(120);
        gpio_set_pin_value(gio, tft_rst_gpionum, GPIO_PV_HIGH);
    }
    if (hw_sw & 0x01) {
        // soft reset
        tft_write_command(SOFTWARE_RESET);
        mp_hal_delay_ms(120);
    }
    // exit sleep
    tft_write_command(SLEEP_OFF);
    mp_hal_delay_ms(120);
    // pixel format
    tft_write_command(PIXEL_FORMAT_SET);
    data = 0x55;
    tft_write_byte(&data, 1);
    tft_write_command(DISPALY_ON);
    return true;
}

// Send command with data to display, display must be selected
//----------------------------------------------------------------------
void disp_spi_transfer_cmd_data(int8_t cmd, uint8_t *data, uint32_t len)
{
    tft_write_command(cmd);

	if ((len == 0) || (data == NULL)) return;

	tft_write_byte(data, len);
}

// Send 1 byte display command, display must be selected
//------------------------------------
void disp_spi_transfer_cmd(int8_t cmd)
{
    tft_write_command(cmd);
}

// Set the address window for display write & read commands, display must be selected
//-----------------------------------------------------------------------------------------
static void disp_spi_transfer_addrwin(uint16_t x1, uint16_t x2, uint16_t y1, uint16_t y2) {
    uint8_t data[4];

    data[0] = (uint8_t)(x1 >> 8);
    data[1] = (uint8_t)(x1);
    data[2] = (uint8_t)(x2 >> 8);
    data[3] = (uint8_t)(x2);
    tft_write_command(HORIZONTAL_ADDRESS_SET);
    tft_write_byte(data, 4);
    data[0] = (uint8_t)(y1 >> 8);
    data[1] = (uint8_t)(y1);
    data[2] = (uint8_t)(y2 >> 8);
    data[3] = (uint8_t)(y2);
    tft_write_command(VERTICAL_ADDRESS_SET);
    tft_write_byte(data, 4);
    tft_write_command(MEMORY_WRITE);
}

// Set display pixel at given coordinates to given color
//=================================================
void drawPixel(int16_t x, int16_t y, color_t color)
{
	if (use_frame_buffer) {
	    int pos = y*_width + x;
	    tft_frame_buffer[pos] = color;
	    return;
	}

	disp_spi_transfer_addrwin(x, x, y, y);

    tft_write_half(&color, 1);
}

// Write 'len' color data to TFT 'window' (x1,y2),(x2,y2)
//================================================================================
void TFT_pushColorRep(int x1, int y1, int x2, int y2, color_t color, uint32_t len)
{
    if (use_frame_buffer) {
        for (int y=y1; y<=y2; y++) {
            for (int x=x1; x<=x2; x++) {
                int pos = y*_width + x;
                tft_frame_buffer[pos] = color;
            }
        }
        return;
    }

    uint32_t data = ((uint32_t)color << 16) | (uint32_t)color;

    // ** Send address window **
	disp_spi_transfer_addrwin(x1, x2, y1, y2);

	tft_fill_data(data, len);
}

// Write 'len' color data to TFT 'window' (x1,y2),(x2,y2) from given buffer
// === Device must already be selected ===
//========================================================================
void send_data(int x1, int y1, int x2, int y2, uint32_t len, color_t *buf)
{
    if (use_frame_buffer) {
        int idx = 0;
        for (int y=y1; y<y2; y++) {
            for (int x=x1; x<x2; x++) {
                int pos = y*_width + x;
                tft_frame_buffer[pos] = buf[idx];
                idx++;
                if (idx >= len) return;
            }
        }
        return;
    }

    // ** Send address window **
    if (x2 > x1) x2 -= 1;
    if (y2 > y1) y2 -= 1;
    disp_spi_transfer_addrwin(x1, x2, y1, y2);

    // Send color buffer
    tft_write_half(buf, len);
}

//======================
void send_frame_buffer()
{
    if (use_frame_buffer) {
        // ** Send address window **
        disp_spi_transfer_addrwin(0, _width, 0, _height);

        // Send color buffer
        tft_write_half(tft_frame_buffer, _width*_height);
    }
}

//==================================
void _tft_setRotation(uint8_t rot) {
	uint8_t rotation = rot & 3; // can't be higher than 3
	uint8_t send = 1;
	uint8_t madctl = 0;
	uint16_t tmp;

    if ((rotation & 1)) {
        // in landscape modes must be width > height
        if (_width < _height) {
            tmp = _width;
            _width  = _height;
            _height = tmp;
        }
    }
    else {
        // in portrait modes must be width < height
        if (_width > _height) {
            tmp = _width;
            _width  = _height;
            _height = tmp;
        }
    }
    if (invertrot == 2) {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (MADCTL_MX | TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MY | TFT_RGB_BGR);
            break;
        }
    }
    else if (invertrot == 3) {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MX | MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MY | MADCTL_MX | TFT_RGB_BGR);
            break;
        }
    }
    else if (invertrot == 1) {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MY | MADCTL_MX | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MX | MADCTL_MV | TFT_RGB_BGR);
            break;
        }
    }
    else {
        switch (rotation) {
            case PORTRAIT:
            madctl = (MADCTL_MX | TFT_RGB_BGR);
            break;
            case LANDSCAPE:
            madctl = (MADCTL_MV | TFT_RGB_BGR);
            break;
            case PORTRAIT_FLIP:
            madctl = (MADCTL_MY | TFT_RGB_BGR);
            break;
            case LANDSCAPE_FLIP:
            madctl = (MADCTL_MX | MADCTL_MY | MADCTL_MV | TFT_RGB_BGR);
            break;
        }
    }
	if (send) {
    	disp_spi_transfer_cmd_data(MEMORY_ACCESS_CTL, &madctl, 1);
	}

}

//=================================================
void TFT_display_setvars(display_config_t *dconfig)
{
    // === SET GLOBAL VARIABLES ==========================
    tft_disp_type = dconfig->type;
    _width = dconfig->width;
    _height = dconfig->height;
    TFT_RGB_BGR = dconfig->bgr;
    gamma_curve = dconfig->gamma;
    spi_speed = dconfig->speed;
    invertrot = dconfig->invrot;
    // ===================================================
}

// Initialize the display
// ==================================================
int TFT_display_init(display_config_t *dconfig)
{
    TFT_display_setvars(dconfig);

    if (!tft_init(2)) return -1;
    vTaskDelay(100);

    return 0;
}

#endif // MICROPY_USE_DISPLAY

