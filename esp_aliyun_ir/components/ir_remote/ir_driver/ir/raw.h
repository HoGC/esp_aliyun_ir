#pragma once

#include <ir/rx.h>

#ifdef __cplusplus
extern "C" {
#endif


// API to send raw commands
//
// Args:
//   - widths - pulse periods: positive values - period of carrier,
//       negative - silence
//   - count - number of pulses to send
//
// Returns:
//   0 on success; negative value - error code
int ir_raw_send(int32_t *widths, uint16_t count);

// RAW decoder: uses decoded buffer as an array of int16_t to store pulses
ir_decoder_t *ir_raw_make_decoder();


#ifdef __cplusplus
}
#endif
