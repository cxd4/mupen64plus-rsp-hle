/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - adpcm.c                                         *
 *   Mupen64Plus homepage: http://code.google.com/p/mupen64plus/           *
 *   Copyright (C) 2012 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hle.h"
#include "arithmetic.h"

/* types definition */
typedef uint16_t (*get_predicted_frame_t)(int16_t *dst, uint16_t src, unsigned char scale);

/* local functions prototypes */
static unsigned int get_scale_shift(unsigned char scale, unsigned char range);
static int16_t get_predicted_sample(uint8_t byte, uint8_t mask, unsigned lshift, unsigned rshift);
static uint16_t get_predicted_frame_4bits(int16_t *dst, uint16_t src, unsigned char scale);
static uint16_t get_predicted_frame_2bits(int16_t *dst, uint16_t src, unsigned char scale);
static void decode_8_samples(int16_t *dst, const int16_t *src, const int16_t *cb_entry);
static int16_t* decode_frames(get_predicted_frame_t get_predicted_frame, int16_t *dst, uint16_t src, int count, const int16_t *codebook);

/* global functions */
void adpcm_decode(
        bool init, bool loop, bool two_bits_per_sample,
        const int16_t* codebook, uint32_t loop_address, uint32_t last_frame_address,
        uint16_t in, uint16_t out, int count)
{
    int16_t *dst = (int16_t*)(rsp.DMEM + out);

    /* init/load last frame */
    if (init)
    {
        memset(dst, 0, 32);
    }
    else
    {
        void *src = (loop) ? &rsp.RDRAM[loop_address] : &rsp.RDRAM[last_frame_address];
        memcpy(dst, src, 32);
    }
    dst += 16;
   
    /* decode frames */
    dst = (two_bits_per_sample)
        ? decode_frames(get_predicted_frame_2bits, dst, in, count, codebook)
        : decode_frames(get_predicted_frame_4bits, dst, in, count, codebook);

    /* save last frame */
    dst -= 16;
    memcpy(&rsp.RDRAM[last_frame_address], dst, 32);
}

/* local functions */
static unsigned int get_scale_shift(unsigned char scale, unsigned char range)
{
    return (scale < range) ? range - scale : 0;
}

static int16_t get_predicted_sample(uint8_t byte, uint8_t mask, unsigned lshift, unsigned rshift)
{
    int16_t sample = ((uint16_t)byte & (uint16_t)mask) << lshift;
    sample >>= rshift; /* signed */
    return sample;
}

static uint16_t get_predicted_frame_4bits(int16_t *dst, uint16_t src, unsigned char scale)
{
    unsigned int i;
    uint8_t byte;
    
    unsigned int rshift = get_scale_shift(scale, 12);

    for(i = 0; i < 8; ++i)
    {
        byte = rsp.DMEM[(src++)^S8];

        *(dst++) = get_predicted_sample(byte, 0xf0,  8, rshift);
        *(dst++) = get_predicted_sample(byte, 0x0f, 12, rshift);
    }

    return src;
}

static uint16_t get_predicted_frame_2bits(int16_t *dst, uint16_t src, unsigned char scale)
{
    unsigned int i;
    uint8_t byte;

    unsigned int rshift = get_scale_shift(scale, 14);

    for(i = 0; i < 4; ++i)
    {
        byte = rsp.DMEM[(src++)^S8];

        *(dst++) = get_predicted_sample(byte, 0xc0,  8, rshift);
        *(dst++) = get_predicted_sample(byte, 0x30, 10, rshift);
        *(dst++) = get_predicted_sample(byte, 0x0c, 12, rshift);
        *(dst++) = get_predicted_sample(byte, 0x03, 14, rshift);
    }

    return src;
}

static void decode_8_samples(int16_t *dst, const int16_t *src, const int16_t *cb_entry)
{
    const int16_t * const book1 = cb_entry;
    const int16_t * const book2 = cb_entry + 8;

    const int16_t l1 = dst[-2 ^ S];
    const int16_t l2 = dst[-1 ^ S];

    size_t i;
    int32_t accu;

    for(i = 0; i < 8; ++i)
    {
        accu = src[i] << 11;
        accu += book1[i]*l1 + book2[i]*l2 + rdot(i, book2, src);
        dst[i ^ S] = clamp_s16(accu >> 11);
    }
}

static int16_t* decode_frames(get_predicted_frame_t get_predicted_frame, int16_t *dst, uint16_t src, int count, const int16_t *codebook)
{
    uint8_t predictor;
    const int16_t *cb_entry;
    unsigned char scale;
    int16_t frame[16];

    while (count > 0)
    {
        predictor = rsp.DMEM[(src++)^S8];

        scale = (predictor & 0xf0) >> 4;
        cb_entry = codebook + ((predictor & 0xf) << 4);

        src = get_predicted_frame(frame, src, scale);

        decode_8_samples(dst, frame    , cb_entry); dst += 8; 
        decode_8_samples(dst, frame + 8, cb_entry); dst += 8; 
        
        --count;
    }

    return dst;
}

