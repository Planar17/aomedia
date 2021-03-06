/*
 * Copyright (c) 2016, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#if !defined(_dering_H)
#define _dering_H (1)

#include "odintrin.h"

#define OD_DERINGSIZES (2)

#define OD_DERING_SIZE_LOG2 (3)

#define OD_DERING_NBLOCKS (OD_BSIZE_MAX / 8)

/* We need to buffer three vertical lines. */
#define OD_FILT_VBORDER (3)
/* We only need to buffer three horizontal lines too, but let's make it four
   to make vectorization easier. */
#define OD_FILT_HBORDER (32)
#define OD_FILT_BSTRIDE \
  ALIGN_POWER_OF_TWO(OD_BSIZE_MAX + 2 * OD_FILT_HBORDER, 5)

#define OD_DERING_VERY_LARGE (30000)
#define OD_DERING_INBUF_SIZE \
  (OD_FILT_BSTRIDE * (OD_BSIZE_MAX + 2 * OD_FILT_VBORDER))

extern const int OD_DIRECTION_OFFSETS_TABLE[8][3];

typedef struct {
  unsigned char by;
  unsigned char bx;
} dering_list;

typedef int (*od_filter_dering_direction_func)(uint16_t *y, int ystride,
                                               const uint16_t *in,
                                               int threshold, int dir);
void copy_dering_16bit_to_16bit(uint16_t *dst, int dstride, uint16_t *src,
                                dering_list *dlist, int dering_count,
                                int bsize);

void od_dering(uint16_t *y, uint16_t *in, int xdec,
               int dir[OD_DERING_NBLOCKS][OD_DERING_NBLOCKS], int pli,
               dering_list *dlist, int dering_count, int threshold,
               int clpf_strength, int clpf_damping, int coeff_shift);
int od_filter_dering_direction_4x4_c(uint16_t *y, int ystride,
                                     const uint16_t *in, int threshold,
                                     int dir);
int od_filter_dering_direction_8x8_c(uint16_t *y, int ystride,
                                     const uint16_t *in, int threshold,
                                     int dir);
#endif
