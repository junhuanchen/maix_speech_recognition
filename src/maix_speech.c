#include "Maix_Speech.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sysctl.h"
#include "plic.h"
#include "uarths.h"
#include "sr_util/g_def.h"
#include "i2s.h"
#include "fpioa.h"
#include "printf.h"
#include "sr_util/VAD.h"
#include "sr_util/MFCC.h"
#include "sr_util/DTW.h"
#include "sr_util/flash.h"
#include "sr_util/ADC.h"

#define USART1_printk Serial.printk

uint16_t VcBuf[atap_len];
atap_tag atap_arg;
valid_tag valid_voice[max_vc_con];
v_ftr_tag ftr_curr;

#define save_ok 0
#define VAD_fail 1
#define MFCC_fail 2
#define Flash_fail 3

#define FFT_N 512

uint16_t rx_buf[FRAME_LEN];
uint32_t g_rx_dma_buf[FRAME_LEN * 2];
uint64_t fft_out_data[FFT_N / 2];

static enum SrState {
    Init,
    Idle,
    Ready,
    MaybeNoise,
    Restrain,
    Speek,
    Done,
} sr_state = Init; // 识别状态 default 0

uint32_t sr_result = -1;

u32 atap_tag_mid_val;       //语音段中值 相当于有符号的0值 用于短时过零率计算
u16 atap_tag_n_thl = 10000; //噪声阈值，用于短时过零率计算
u16 atap_tag_z_thl;         //短时过零率阈值，超过此阈值，视为进入过渡段。
u32 atap_tag_s_thl;         //短时累加和阈值，超过此阈值，视为进入过渡段。

volatile uint32_t g_index = 0;
volatile uint8_t uart_rec_flag;
volatile uint32_t receive_char;
volatile enum SrI2sFlag {
    NONE,
    FIRST,
    SECOND
} i2s_recv_flag = NONE;
volatile uint8_t i2s_start_flag = 0;

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
    static u16 frame_index;
    const u16 num = atap_len / frame_mov;
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

int sr_begin()
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
    dmac_set_irq(DMAC_CHANNEL3, sr_i2s_dma_irq, NULL, 3);
    i2s_receive_data_dma(I2S_DEVICE_0, &g_rx_dma_buf[0], frame_mov * 2, DMAC_CHANNEL3);

    /* Enable the machine interrupt */
    sysctl_enable_irq();
    return 0;
}

int sr_record(uint8_t keyword_num, uint8_t model_num)
{
    if (keyword_num > 10)
        return -1;
    if (model_num > 4)
        return -2;

    if (sr_state == Init)
    {
        g_index = 0;
        i2s_recv_flag = NONE;
        i2s_start_flag = 1;
        sr_state = Idle;
    }
    else if (sr_state == Done)
    {
        uint32_t addr = ftr_start_addr + keyword_num * size_per_comm + model_num * size_per_ftr;
        if (save_ftr_mdl(&ftr_curr, addr) == 0)
        {
            sr_state = Idle;
            return Done;
        }
    }
    return sr_state;
}

int sr_recognize()
{
    if (sr_state == Init)
    {
        g_index = 0;
        i2s_recv_flag = NONE;
        sr_state = Idle;
        i2s_start_flag = 1;
    }
    else if (sr_state == Done)
    {
        u32 i = 0;
        u16 min_comm = 0;
        u32 min_dis = dis_max;
        uint32_t cycle0 = read_csr(mcycle);
        for (u32 ftr_addr = ftr_start_addr; ftr_addr < ftr_end_addr; ftr_addr += size_per_ftr)
        {
            //  ftr_mdl=(v_ftr_tag*)ftr_addr;
            v_ftr_tag *ftr_mdl = (v_ftr_tag *)(&ftr_save[ftr_addr / size_per_ftr]);
            u32 cur_dis = ((ftr_mdl->save_sign) == save_mask) ? dtw(ftr_mdl, &ftr_curr) : dis_err;
            if ((ftr_mdl->save_sign) == save_mask)
            {
                printk("no. %d, ftr_mdl->frm_num %d, ftr_mdl->save_mask %d, ", i + 1, ftr_mdl->frm_num, ftr_mdl->save_sign);
                printk("cur_dis %d, ftr_curr.frm_num %d\n", cur_dis, ftr_curr.frm_num);
            }
            if (cur_dis < min_dis)
            {
                min_dis = cur_dis;
                min_comm = i + 1;
                printk("min_comm: %d >\r\n", min_comm);
            }
            i++;
        }
        uint32_t cycle1 = read_csr(mcycle) - cycle0;
        printk("[INFO] recg cycle = 0x%08x\n", cycle1);
        if (min_comm % 4)
            min_comm = min_comm / ftr_per_comm + 1;
        else
            min_comm = min_comm / ftr_per_comm;
        //USART1_printk("recg end ");
        printk("min_comm: %d >\r\n", min_comm);

        sr_state = Idle;
        return Done;
    }
    return sr_state;
}

int sr_addVoiceModel(uint8_t keyword_num, uint8_t model_num, const int16_t *voice_model, uint16_t frame_num)
{
    ftr_save[keyword_num * 4 + model_num].save_sign = save_mask;
    ftr_save[keyword_num * 4 + model_num].frm_num = frame_num;

    for (int i = 0; i < (vv_frm_max * mfcc_num); i++)
        ftr_save[keyword_num * 4 + model_num].mfcc_dat[i] = voice_model[i];
    return 0;
}
