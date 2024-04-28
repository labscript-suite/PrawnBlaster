#include "tusb.h"
#include "pico/unique_id.h"

#include <stdarg.h>

#include "fast_serial.h"

/*
  Serial functions

  These are mostly thin wrappers around TinyUSB functions;
  they are provided to simplify the API.
 */

// Read bytes (blocks until buffer_size is reached)
uint32_t fast_serial_read(const char * buffer, uint32_t buffer_size){
	uint32_t buffer_idx = 0;
	while(buffer_idx < buffer_size){
		uint32_t buffer_avail = buffer_size - buffer_idx;
		uint32_t read_avail = fast_serial_read_available();

		if(read_avail > 0){
			if(buffer_avail > read_avail){
				buffer_avail = read_avail;
			}

			buffer_idx += fast_serial_read_atomic(buffer + buffer_idx, buffer_avail);
		}

		fast_serial_task();
	}
	return buffer_size;
}

// Read bytes until terminator reached (blocks until terminator or buffer_size is reached)
uint32_t fast_serial_read_until(char * buffer, uint32_t buffer_size, char until){
	uint32_t buffer_idx = 0;
	while(buffer_idx < buffer_size - 1){
		while(fast_serial_read_available() > 0){
			int32_t next_char = tud_cdc_read_char();

			buffer[buffer_idx] = next_char;
			buffer_idx++;
			if(next_char == until){
				break;
			}
		}

		if(buffer_idx > 0 && buffer[buffer_idx-1] == until){
			break;
		}
		fast_serial_task();
	}
	buffer[buffer_idx] = '\0'; // Null terminate string
	return buffer_idx;
}

// Write bytes (without flushing, so limited to 64 bytes)
uint32_t fast_serial_write(const char * buffer, uint32_t buffer_size){
	uint32_t buffer_idx = 0;
	while(buffer_idx < buffer_size){
		uint32_t write_avail = fast_serial_write_available();

		if(write_avail > 0){
			if(buffer_size - buffer_idx < write_avail){
				write_avail = buffer_size - buffer_idx;
			}

			buffer_idx += fast_serial_write_atomic(buffer + buffer_idx, write_avail);
		}
		fast_serial_task();
		fast_serial_write_flush();
	}
	return buffer_size;
}

int fast_serial_printf(const char * format, ...){
	va_list va;
	va_start(va, format);
	char printf_buffer[128];
	int ret = vsnprintf(printf_buffer, 128, format, va);
	va_end(va);
	if(ret <= 0){
		return ret;
	}
	return fast_serial_write(printf_buffer, strnlen(printf_buffer, 128));
}

/*
  USB callbacks
*/

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts){}
void tud_cdc_rx_cb(uint8_t itf){}

/*
  USB descriptor setup

  We use the same VID, PID and ID as the Pi Pico would normally use.
 */
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0x2E8A,
    .idProduct          = 0x000A,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(){
	return (uint8_t const *) &desc_device;
}

enum{
	ITF_NUM_CDC = 0,
	ITF_NUM_CDC_DATA,
	ITF_NUM_TOTAL
};

#define EPNUM_CDC_NOTIF 0x81
#define EPNUM_CDC_OUT 0x02
#define EPNUM_CDC_IN 0x82

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

uint8_t const desc_configuration[] = {
	// Config number, interface count, string index, total length, attribute, power in mA
	TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
// Interface number, string index, EP notification address and size, EP data address (out, in) and size.
	TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64)
};

uint8_t const * tud_descriptor_configuration_cb(uint8_t index){
	return desc_configuration;
}

enum {
	STRID_LANGID = 0,
	STRID_MANUFACTURER,
	STRID_PRODUCT,
	STRID_SERIAL,
};

static char usb_serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

char const* string_desc_arr [] ={
	(const char[]) { 0x09, 0x04 }, // 0: is supported language is English (0x0409)
	"Raspberry Pi",                      // 1: Manufacturer
	"Pico",            // 2: Product
	usb_serial_str,                      // 3: Serials, should use chip ID
	"Board CDC", // 4: CDC Interface
};

static uint16_t _desc_str[32];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid){
	uint8_t chr_count;

	if(!usb_serial_str[0]){
		pico_get_unique_board_id_string(usb_serial_str, sizeof(usb_serial_str));
	}

	if(index == 0){
		memcpy(&_desc_str[1], string_desc_arr[0], 2);
		chr_count = 1;
	}
	else{
		if(!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))){
			return NULL;
		}

		const char* str = string_desc_arr[index];

		// Cap at max char
		chr_count = (uint8_t) strlen(str);
		if ( chr_count > 31 ) chr_count = 31;

		// Convert ASCII string into UTF-16
		for(uint8_t i = 0; i < chr_count; i++){
			_desc_str[1+i] = str[i];
		}
	}

	// first byte is length (including header), second byte is string type
	_desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8 ) | (2*chr_count + 2));

	return _desc_str;
}
