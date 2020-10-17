#ifndef __MAIX_SPEECH_RECOGNITION_H
#define __MAIX_SPEECH_RECOGNITION_H

#include <stdint.h>

void sr_begin();
int sr_record(uint8_t model_num);
int sr_recognize();
void sr_set_model(uint8_t model_num, const int16_t *voice_model, uint16_t frame_num);
void sr_print_model(uint8_t model_num);

#endif