/*
  Faster serial functions

  fast_serial_read/fast_serial_read_until are blocking functions
  designed to receive data over a USB serial connection as fast as possible
  (hopefully at the limit of the drivers).
  fast_serial_read is generally much faster (~4x) than fast_serial_read_until,
  as it can read larger blocks of data without having to scan for the terminating character.
  Therefore, it is recommended to use fixed size blocks for large transmissions.

  fast_serial_write is a blocking function
  designed to send data over a USB serial connection as fast as possible
  (again hopefully at the limit of the drivers).

  The remaining functions are thin wrappers around TinyUSB functions;
  they are provided to simplify the API.

  Basic usage:
  Call fast_serial_init()
  In the main processing loop, call fast_serial_task.
  Place calls to fast_serial_read/fast_serial_read_until and fast_serial_write where appropriate.
 */
#include "tusb.h"

// Initialize the USB stack
static inline bool fast_serial_init(){
	return tusb_init();
}

// Get number of bytes available to read
static inline uint32_t fast_serial_read_available(){
	return tud_cdc_available();
}

// Get number of bytes available to write
static inline uint32_t fast_serial_write_available(){
	return tud_cdc_write_available();
}

// Read up to 64 bytes
static inline uint32_t fast_serial_read_atomic(char * buffer, uint32_t buffer_size){
	return tud_cdc_read(buffer, buffer_size);
}

// Read bytes (blocks until buffer_size is reached)
uint32_t fast_serial_read(const char * buffer, uint32_t buffer_size);

// Read bytes until terminator reached (blocks until terminator or buffer_size is reached)
uint32_t fast_serial_read_until(char * buffer, uint32_t buffer_size, char until);

// Clear read FIFO (without reading it)
static inline void fast_serial_read_flush(){
	tud_cdc_read_flush();
}

// Write bytes (without flushing, so limited to 64 bytes)
static inline uint32_t fast_serial_write_atomic(const char * buffer, uint32_t buffer_size){
	return tud_cdc_write(buffer, buffer_size);
}

// Write bytes (without flushing)
uint32_t fast_serial_write(const char * buffer, uint32_t buffer_size);

// print via fast_serial_write
int fast_serial_printf(const char * format, ...);

// Force write of data. Returns number of bytes written.
static inline uint32_t fast_serial_write_flush(){
	return tud_cdc_write_flush();
}

// Must be called regularly from main loop
static inline void fast_serial_task(){
	tud_task();
}
