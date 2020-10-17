#include "Maix_Speech.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sysctl.h"
#include "plic.h"
#include "uarths.h"
#include "i2s.h"
#include "fpioa.h"
#include "printf.h"

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "sysctl.h"
#include "plic.h"
#include "uarths.h"
#include "i2s.h"

#include "sr_util/VAD.h"
#include "sr_util/MFCC.h"
#include "sr_util/DTW.h"
#include "sr_util/ADC.h"

atap_tag atap_arg;
uint16_t VcBuf[atap_len];
v_ftr_tag ftr_curr;
valid_tag valid_voice[max_vc_con];

uint32_t atap_tag_mid_val;       //语音段中值 相当于有符号的0值 用于短时过零率计算
uint16_t atap_tag_n_thl = 10000; //噪声阈值，用于短时过零率计算
uint16_t atap_tag_z_thl = 0;     //短时过零率阈值，超过此阈值，视为进入过渡段。
uint32_t atap_tag_s_thl = 0;     //短时累加和阈值，超过此阈值，视为进入过渡段。

static enum SrState {
    Init,
    Idle,
    Ready,
    MaybeNoise,
    Restrain,
    Speek,
    Done,
} sr_state = Init; // 识别状态 default 0

volatile uint32_t g_index = 0;
volatile uint8_t i2s_start_flag = 0;
uint16_t rx_buf[FRAME_LEN];
uint32_t g_rx_dma_buf[FRAME_LEN * 2];
volatile enum SrI2sFlag {
    NONE,
    FIRST,
    SECOND
} i2s_recv_flag = NONE;

#define ftr_size 10 * 4
#define save_mask 12345
v_ftr_tag ftr_save[ftr_size];

#define sr_memset() for(uint16_t i = 0; i < ftr_size; i++) memset(&ftr_save[0], 0, sizeof(ftr_save[0]))

uint8_t sr_save_ftr_mdl(v_ftr_tag *ftr, uint32_t model_num)
{
	if (model_num < ftr_size) {
        ftr->save_sign = save_mask;
        ftr_save[model_num] = *ftr;
	}
    return -1;
}

void sr_set_model(uint8_t model_num, const int16_t *voice_model, uint16_t frame_num)
{
    ftr_save[model_num].save_sign = save_mask;
    ftr_save[model_num].frm_num = frame_num;
    memcpy(ftr_save[model_num].mfcc_dat, voice_model, sizeof(ftr_save[model_num].mfcc_dat));
}

void sr_print_model(uint8_t model_num)
{
  printk("frm_num=%d\n", ftr_save[model_num].frm_num);
  for (int i = 0; i < (vv_frm_max * mfcc_num); i++)
  {
    if (((i + 1) % 49) == 0) // next line
      printk("%d,\n", ftr_save[model_num].mfcc_dat[i]);
    else
      printk("%d, ", ftr_save[model_num].mfcc_dat[i]);
  }
  printk("\nprint model ok!\n");
}

int sr_i2s_dma_irq(void *ctx)
{
    uint32_t i;
    if (i2s_start_flag)
    {
        int16_t s_tmp;
        if (g_index)
        {
            i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[g_index], frame_mov * 2, DMAC_CHANNEL3);
            g_index = 0;
            for (i = 0; i < frame_mov; i++)
            {
                s_tmp = (int16_t)(g_rx_dma_buf[2 * i] & 0xffff); //g_rx_dma_buf[2 * i + 1] Low left
                rx_buf[i] = s_tmp + 32768;
            }
            i2s_recv_flag = FIRST;
        }
        else
        {
            i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[0], frame_mov * 2, DMAC_CHANNEL3);
            g_index = frame_mov * 2;
            for (i = frame_mov; i < frame_mov * 2; i++)
            {
                s_tmp = (int16_t)(g_rx_dma_buf[2 * i] & 0xffff); //g_rx_dma_buf[2 * i + 1] Low left
                rx_buf[i] = s_tmp + 32768;
            }
            i2s_recv_flag = SECOND;
        }
    }
    else
    {
        i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[0], frame_mov * 2, DMAC_CHANNEL3);
        g_index = frame_mov * 2;
    }

    uint16_t *v_dat = VcBuf;
    static uint16_t frame_index;
    const uint16_t num = atap_len / frame_mov;
    switch (sr_state)
    {
    case Init:
        break;
    case Idle:
        frame_index = 0;
        sr_state = Ready;
        break;
    case Ready: // 准备中
    {
        // get record data

        // memcpy(v_dat + frame_mov * frame_index,  (i2s_recv_flag == FIRST) ? rx_buf : rx_buf + frame_mov, frame_mov);

        if (i2s_recv_flag == FIRST)
        {
            for (i = 0; i < frame_mov; i++)
                v_dat[frame_mov * frame_index + i] = rx_buf[i];
        }
        else
        {
            for (i = 0; i < frame_mov; i++)
                v_dat[frame_mov * frame_index + i] = rx_buf[i + frame_mov];
        }

        frame_index++;
        if (frame_index >= num)
        {
            sr_state = MaybeNoise;
            break;
        }
        break;
    }
    case MaybeNoise: // 噪音判断
    {
        noise_atap(v_dat, atap_len, &atap_arg);
        // Serial.printk("s: %d > ", atap_arg.s_thl);
        if (atap_arg.s_thl > atap_tag_n_thl)
        {
            // Serial.printk("get noise again...\n");
            sr_state = Idle;
        }
        else
        {
            sr_state = Restrain; // into Recognizer
        }
        break;
    }
    case Restrain: // 背景噪音
    {
        if (i2s_recv_flag == FIRST)
        {
            for (i = 0; i < frame_mov; i++)
                v_dat[i + frame_mov] = rx_buf[i];
        }
        else
        {
            for (i = 0; i < frame_mov; i++)
                v_dat[i + frame_mov] = rx_buf[i + frame_mov];
        }
        sr_state = Speek;
    }
    case Speek: // 录音识别
    {
        if (i2s_recv_flag == FIRST)
        {
            for (i = 0; i < frame_mov; i++)
            {
                v_dat[i] = v_dat[i + frame_mov];
                v_dat[i + frame_mov] = rx_buf[i];
            }
        }
        else
        {
            for (i = 0; i < frame_mov; i++)
            {
                v_dat[i] = v_dat[i + frame_mov];
                v_dat[i + frame_mov] = rx_buf[i + frame_mov];
            }
        }
        if (VAD2(v_dat, valid_voice, &atap_arg) == 1)
        {
            // Serial.printk("vad ok\n");
            get_mfcc(&(valid_voice[0]), &ftr_curr, &atap_arg);
            if (ftr_curr.frm_num == 0)
            {
                // Serial.printk("MFCC fail ");
                // return 0;
                sr_state = Idle;
                break;
            }
            else
            {
                // Serial.printk("mfcc ok\n");
                sr_state = Done;
                break;
            }
            break;
        }
        break;
    }
    case Done: // 完成
    {
        break; // other
    }
    }
    // Serial.printk("s: %d >\r\n", sr_state);

    return 0;
}

void sr_load(i2s_device_number_t device_num, dmac_channel_number_t channel_num, uint32_t priority)
{
    dmac_irq_register(channel_num, sr_i2s_dma_irq, NULL, priority);
    i2s_receive_data_dma(device_num, &g_rx_dma_buf[0], frame_mov * 2, channel_num);

    if (sr_state == Init)
    {
        g_index = 0;
        i2s_recv_flag = NONE;
        i2s_start_flag = 1;
        sr_state = Idle;
    }
}

void sr_free(i2s_device_number_t device_num)
{
    dmac_irq_unregister(device_num);
    if (sr_state != Init)
    {
        g_index = 0;
        i2s_recv_flag = NONE;
        i2s_start_flag = 0;
        sr_state = Init;
        sr_memset();
    }
}

void sr_begin()
{
    //io_mux_init
    fpioa_set_function(20, FUNC_I2S0_IN_D0);
    fpioa_set_function(18, FUNC_I2S0_SCLK);
    fpioa_set_function(19, FUNC_I2S0_WS);

    //i2s init
    i2s_init(I2S_DEVICE_0, I2S_RECEIVER, 0x3);

    i2s_rx_channel_config(I2S_DEVICE_0, I2S_CHANNEL_0,
                          RESOLUTION_16_BIT, SCLK_CYCLES_32,
                          TRIGGER_LEVEL_4, STANDARD_MODE);

    i2s_set_sample_rate(I2S_DEVICE_0, 8000);

    dmac_init();

    sr_load(I2S_DEVICE_0, DMAC_CHANNEL3, 3);

    /* Enable the machine interrupt */
    sysctl_enable_irq();
}

int sr_record(uint8_t model_num)
{
    if (sr_state == Done)
    {
        if (sr_save_ftr_mdl(&ftr_curr, model_num) == 0)
        {
            sr_state = Idle;
            return Done;
        }
    }
    return sr_state;
}

int sr_recognize()
{
    if (sr_state == Done)
    {
        int16_t min_comm = -1;
        uint32_t min_dis = dis_max;
        // uint32_t cycle0 = read_csr(mcycle);
        for (uint32_t ftr_num = 0; ftr_num < ftr_size; ftr_num += 1)
        {
            //  ftr_mdl=(v_ftr_tag*)ftr_num;
            v_ftr_tag *ftr_mdl = (v_ftr_tag *)(&ftr_save[ftr_num]);
            if ((ftr_mdl->save_sign) == save_mask)
            {
                printk("no. %d, ftr_mdl->frm_num %d, ", ftr_num, ftr_mdl->frm_num);
                
                uint32_t cur_dis = dtw(ftr_mdl, &ftr_curr);
                printk("cur_dis %d, ftr_curr.frm_num %d\n", cur_dis, ftr_curr.frm_num);
            
                if (cur_dis < min_dis)
                {
                    min_dis = cur_dis;
                    min_comm = ftr_num;
                    printk("min_comm: %d >\r\n", min_comm);
                }
            }
        }
        // uint32_t cycle1 = read_csr(mcycle) - cycle0;
        // printk("[INFO] recg cycle = 0x%08x\n", cycle1);
        //printk("recg end ");
        printk("min_comm: %d >\r\n", min_comm);

        sr_state = Idle;
        return Done;
    }
    return sr_state;
}
