#ifndef __MAIX_SPEECH_RECOGNITION_H
#define __MAIX_SPEECH_RECOGNITION_H

#include "Arduino.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sysctl.h"
#include "plic.h"
#include "uarths.h"
#include "sr_util/g_def.h"
#include "i2s.h"

#include "sr_util/VAD.h"
#include "sr_util/MFCC.h"
#include "sr_util/DTW.h"
#include "sr_util/flash.h"
#include "sr_util/ADC.h"

int sr_begin();                                        //初始化i2s
int sr_record(uint8_t keyword_num, uint8_t model_num); //记录关键词
int sr_recognize();                                    //识别，返回关键词号
int sr_addVoiceModel(uint8_t keyword_num, uint8_t model_num, const int16_t *voice_model, uint16_t frame_num);
int sr_print_model(uint8_t keyword_num, uint8_t model_num);

#endif