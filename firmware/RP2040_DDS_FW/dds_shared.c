#include "dds_state.h"
#include "pico/critical_section.h"
/* The lock covers only struct copies, never DDS sample generation or I/O.
 * IRQ-safe lock prevents Core 1's I2C ISR interrupting its own lock holder. */
static critical_section_t lock;
static dds_request_t request;
static dds_status_t status;
void dds_shared_init(void) {
    critical_section_init(&lock);
    request.freq_mHz = status.freq_mHz = DEFAULT_FREQ_MHZ;
}
void dds_request_read(dds_request_t *out) {
    critical_section_enter_blocking(&lock); *out = request; critical_section_exit(&lock);
}
void dds_request_write(const dds_request_t *in) {
    critical_section_enter_blocking(&lock); request = *in; critical_section_exit(&lock);
}
void dds_status_read(dds_status_t *out) {
    critical_section_enter_blocking(&lock); *out = status; critical_section_exit(&lock);
}
void dds_status_write(const dds_status_t *in) {
    critical_section_enter_blocking(&lock); status = *in; critical_section_exit(&lock);
}
