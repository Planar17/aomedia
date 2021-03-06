/*
 * Copyright (c) 2017, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#include "av1/common/scan.h"
#include "av1/common/blockd.h"
#include "av1/common/pred_common.h"
#include "av1/encoder/cost.h"
#include "av1/encoder/encodetxb.h"
#include "av1/encoder/tokenize.h"

void av1_alloc_txb_buf(AV1_COMP *cpi) {
#if 0
  AV1_COMMON *cm = &cpi->common;
  int mi_block_size = 1 << MI_SIZE_LOG2;
  // TODO(angiebird): Make sure cm->subsampling_x/y is set correctly, and then
  // use precise buffer size according to cm->subsampling_x/y
  int pixel_stride = mi_block_size * cm->mi_cols;
  int pixel_height = mi_block_size * cm->mi_rows;
  int i;
  for (i = 0; i < MAX_MB_PLANE; ++i) {
    CHECK_MEM_ERROR(
        cm, cpi->tcoeff_buf[i],
        aom_malloc(sizeof(*cpi->tcoeff_buf[i]) * pixel_stride * pixel_height));
  }
#else
  (void)cpi;
#endif
}

void av1_free_txb_buf(AV1_COMP *cpi) {
#if 0
  int i;
  for (i = 0; i < MAX_MB_PLANE; ++i) {
    aom_free(cpi->tcoeff_buf[i]);
  }
#else
  (void)cpi;
#endif
}

static void write_golomb(aom_writer *w, int level) {
  int x = level + 1;
  int i = x;
  int length = 0;

  while (i) {
    i >>= 1;
    ++length;
  }
  assert(length > 0);

  for (i = 0; i < length - 1; ++i) aom_write_bit(w, 0);

  for (i = length - 1; i >= 0; --i) aom_write_bit(w, (x >> i) & 0x01);
}

void av1_write_coeffs_txb(const AV1_COMMON *const cm, MACROBLOCKD *xd,
                          aom_writer *w, int block, int plane,
                          const tran_low_t *tcoeff, uint16_t eob,
                          TXB_CTX *txb_ctx) {
  aom_prob *nz_map;
  aom_prob *eob_flag;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  const PLANE_TYPE plane_type = get_plane_type(plane);
  const TX_SIZE tx_size = get_tx_size(plane, xd);
  const TX_TYPE tx_type = get_tx_type(plane_type, xd, block, tx_size);
  const SCAN_ORDER *const scan_order =
      get_scan(cm, tx_size, tx_type, is_inter_block(mbmi));
  const int16_t *scan = scan_order->scan;
  int c;
  int is_nz;
  const int bwl = b_width_log2_lookup[txsize_to_bsize[tx_size]] + 2;
  const int seg_eob = 16 << (tx_size << 1);
  uint8_t txb_mask[32 * 32] = { 0 };
  uint16_t update_eob = 0;

  aom_write(w, eob == 0, cm->fc->txb_skip[tx_size][txb_ctx->txb_skip_ctx]);

  if (eob == 0) return;

  nz_map = cm->fc->nz_map[tx_size][plane_type];
  eob_flag = cm->fc->eob_flag[tx_size][plane_type];

  for (c = 0; c < eob; ++c) {
    int coeff_ctx = get_nz_map_ctx(tcoeff, txb_mask, scan[c], bwl);
    int eob_ctx = get_eob_ctx(tcoeff, scan[c], bwl);

    tran_low_t v = tcoeff[scan[c]];
    is_nz = (v != 0);

    if (c == seg_eob - 1) break;

    aom_write(w, is_nz, nz_map[coeff_ctx]);

    if (is_nz) {
      aom_write(w, c == (eob - 1), eob_flag[eob_ctx]);
    }
    txb_mask[scan[c]] = 1;
  }

  int i;
  for (i = 0; i < NUM_BASE_LEVELS; ++i) {
    aom_prob *coeff_base = cm->fc->coeff_base[tx_size][plane_type][i];

    update_eob = 0;
    for (c = eob - 1; c >= 0; --c) {
      tran_low_t v = tcoeff[scan[c]];
      tran_low_t level = abs(v);
      int sign = (v < 0) ? 1 : 0;
      int ctx;

      if (level <= i) continue;

      ctx = get_base_ctx(tcoeff, scan[c], bwl, i + 1);

      if (level == i + 1) {
        aom_write(w, 1, coeff_base[ctx]);
        if (c == 0) {
          aom_write(w, sign, cm->fc->dc_sign[plane_type][txb_ctx->dc_sign_ctx]);
        } else {
          aom_write_bit(w, sign);
        }
        continue;
      }
      aom_write(w, 0, coeff_base[ctx]);
      update_eob = AOMMAX(update_eob, c);
    }
  }

  for (c = update_eob; c >= 0; --c) {
    tran_low_t v = tcoeff[scan[c]];
    tran_low_t level = abs(v);
    int sign = (v < 0) ? 1 : 0;
    int idx;
    int ctx;

    if (level <= NUM_BASE_LEVELS) continue;

    if (c == 0) {
      aom_write(w, sign, cm->fc->dc_sign[plane_type][txb_ctx->dc_sign_ctx]);
    } else {
      aom_write_bit(w, sign);
    }

    // level is above 1.
    ctx = get_level_ctx(tcoeff, scan[c], bwl);
    for (idx = 0; idx < COEFF_BASE_RANGE; ++idx) {
      if (level == (idx + 1 + NUM_BASE_LEVELS)) {
        aom_write(w, 1, cm->fc->coeff_lps[tx_size][plane_type][ctx]);
        break;
      }
      aom_write(w, 0, cm->fc->coeff_lps[tx_size][plane_type][ctx]);
    }
    if (idx < COEFF_BASE_RANGE) continue;

    // use 0-th order Golomb code to handle the residual level.
    write_golomb(w, level - COEFF_BASE_RANGE - 1 - NUM_BASE_LEVELS);
  }
}

void av1_write_coeffs_mb(const AV1_COMMON *const cm, MACROBLOCK *x,
                         aom_writer *w, int plane) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  BLOCK_SIZE bsize = mbmi->sb_type;
  struct macroblockd_plane *pd = &xd->plane[plane];

#if CONFIG_CB4X4
  const BLOCK_SIZE plane_bsize = get_plane_block_size(bsize, pd);
#else
  const BLOCK_SIZE plane_bsize =
      get_plane_block_size(AOMMAX(bsize, BLOCK_8X8), pd);
#endif
  const int num_4x4_w = block_size_wide[plane_bsize] >> tx_size_wide_log2[0];
  const int num_4x4_h = block_size_high[plane_bsize] >> tx_size_high_log2[0];
  TX_SIZE tx_size = get_tx_size(plane, xd);
  const int bkw = tx_size_wide_unit[tx_size];
  const int bkh = tx_size_high_unit[tx_size];
  const int step = tx_size_wide_unit[tx_size] * tx_size_high_unit[tx_size];
  int row, col;
  int block = 0;
  for (row = 0; row < num_4x4_h; row += bkh) {
    for (col = 0; col < num_4x4_w; col += bkw) {
      tran_low_t *tcoeff = BLOCK_OFFSET(x->mbmi_ext->tcoeff[plane], block);
      uint16_t eob = x->mbmi_ext->eobs[plane][block];
      TXB_CTX txb_ctx = { x->mbmi_ext->txb_skip_ctx[plane][block],
                          x->mbmi_ext->dc_sign_ctx[plane][block] };
      av1_write_coeffs_txb(cm, xd, w, block, plane, tcoeff, eob, &txb_ctx);
      block += step;
    }
  }
}

static INLINE void get_base_ctx_set(const tran_low_t *tcoeffs,
                                    int c,  // raster order
                                    const int bwl,
                                    int ctx_set[NUM_BASE_LEVELS]) {
  const int row = c >> bwl;
  const int col = c - (row << bwl);
  const int stride = 1 << bwl;
  int mag[NUM_BASE_LEVELS] = { 0 };
  int idx;
  tran_low_t abs_coeff;
  int i;

  for (idx = 0; idx < BASE_CONTEXT_POSITION_NUM; ++idx) {
    int ref_row = row + base_ref_offset[idx][0];
    int ref_col = col + base_ref_offset[idx][1];
    int pos = (ref_row << bwl) + ref_col;

    if (ref_row < 0 || ref_col < 0 || ref_row >= stride || ref_col >= stride)
      continue;

    abs_coeff = abs(tcoeffs[pos]);

    for (i = 0; i < NUM_BASE_LEVELS; ++i) {
      ctx_set[i] += abs_coeff > i;
      if (base_ref_offset[idx][0] >= 0 && base_ref_offset[idx][1] >= 0)
        mag[i] |= abs_coeff > (i + 1);
    }
  }

  for (i = 0; i < NUM_BASE_LEVELS; ++i) {
    ctx_set[i] = (ctx_set[i] + 1) >> 1;

    if (row == 0 && col == 0)
      ctx_set[i] = (ctx_set[i] << 1) + mag[i];
    else if (row == 0)
      ctx_set[i] = 8 + (ctx_set[i] << 1) + mag[i];
    else if (col == 0)
      ctx_set[i] = 18 + (ctx_set[i] << 1) + mag[i];
    else
      ctx_set[i] = 28 + (ctx_set[i] << 1) + mag[i];
  }
  return;
}

int av1_cost_coeffs_txb(const AV1_COMMON *const cm, MACROBLOCK *x, int plane,
                        int block, TXB_CTX *txb_ctx, int *cul_level) {
  MACROBLOCKD *const xd = &x->e_mbd;
  const TX_SIZE tx_size = get_tx_size(plane, xd);
  const PLANE_TYPE plane_type = get_plane_type(plane);
  const TX_TYPE tx_type = get_tx_type(plane_type, xd, block, tx_size);
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  const struct macroblock_plane *p = &x->plane[plane];
  const int eob = p->eobs[block];
  const tran_low_t *const qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  int c, cost;
  const int seg_eob = AOMMIN(eob, (16 << (tx_size << 1)) - 1);
  int txb_skip_ctx = txb_ctx->txb_skip_ctx;
  aom_prob *nz_map = xd->fc->nz_map[tx_size][plane_type];

  const int bwl = b_width_log2_lookup[txsize_to_bsize[tx_size]] + 2;
  *cul_level = 0;
  // txb_mask is only initialized for once here. After that, it will be set when
  // coding zero map and then reset when coding level 1 info.
  uint8_t txb_mask[32 * 32] = { 0 };
  aom_prob(*coeff_base)[COEFF_BASE_CONTEXTS] =
      xd->fc->coeff_base[tx_size][plane_type];

  const SCAN_ORDER *const scan_order =
      get_scan(cm, tx_size, tx_type, is_inter_block(mbmi));
  const int16_t *scan = scan_order->scan;

  cost = 0;

  if (eob == 0) {
    cost = av1_cost_bit(xd->fc->txb_skip[tx_size][txb_skip_ctx], 1);
    return cost;
  }

  cost = av1_cost_bit(xd->fc->txb_skip[tx_size][txb_skip_ctx], 0);

  for (c = 0; c < eob; ++c) {
    tran_low_t v = qcoeff[scan[c]];
    int is_nz = (v != 0);
    int level = abs(v);

    if (c < seg_eob) {
      int coeff_ctx = get_nz_map_ctx(qcoeff, txb_mask, scan[c], bwl);
      cost += av1_cost_bit(nz_map[coeff_ctx], is_nz);
    }

    if (is_nz) {
      int ctx_ls[NUM_BASE_LEVELS] = { 0 };
      int sign = (v < 0) ? 1 : 0;

      // sign bit cost
      if (c == 0) {
        int dc_sign_ctx = txb_ctx->dc_sign_ctx;

        cost += av1_cost_bit(xd->fc->dc_sign[plane_type][dc_sign_ctx], sign);
      } else {
        cost += av1_cost_bit(128, sign);
      }

      get_base_ctx_set(qcoeff, scan[c], bwl, ctx_ls);

      int i;
      for (i = 0; i < NUM_BASE_LEVELS; ++i) {
        if (level <= i) continue;

        if (level == i + 1) {
          cost += av1_cost_bit(coeff_base[i][ctx_ls[i]], 1);
          *cul_level += level;
          continue;
        }
        cost += av1_cost_bit(coeff_base[i][ctx_ls[i]], 0);
      }

      if (level > NUM_BASE_LEVELS) {
        int idx;
        int ctx;
        *cul_level += level;

        ctx = get_level_ctx(qcoeff, scan[c], bwl);

        for (idx = 0; idx < COEFF_BASE_RANGE; ++idx) {
          if (level == (idx + 1 + NUM_BASE_LEVELS)) {
            cost +=
                av1_cost_bit(xd->fc->coeff_lps[tx_size][plane_type][ctx], 1);
            break;
          }
          cost += av1_cost_bit(xd->fc->coeff_lps[tx_size][plane_type][ctx], 0);
        }

        if (idx >= COEFF_BASE_RANGE) {
          // residual cost
          int r = level - COEFF_BASE_RANGE - NUM_BASE_LEVELS;
          int ri = r;
          int length = 0;

          while (ri) {
            ri >>= 1;
            ++length;
          }

          for (ri = 0; ri < length - 1; ++ri) cost += av1_cost_bit(128, 0);

          for (ri = length - 1; ri >= 0; --ri)
            cost += av1_cost_bit(128, (r >> ri) & 0x01);
        }
      }

      if (c < seg_eob) {
        int eob_ctx = get_eob_ctx(qcoeff, scan[c], bwl);
        cost += av1_cost_bit(xd->fc->eob_flag[tx_size][plane_type][eob_ctx],
                             c == (eob - 1));
      }
    }

    txb_mask[scan[c]] = 1;
  }

  *cul_level = AOMMIN(63, *cul_level);
  // DC value
  set_dc_sign(cul_level, qcoeff[0]);

  return cost;
}

typedef struct TxbParams {
  const AV1_COMP *cpi;
  ThreadData *td;
  int rate;
} TxbParams;

static void update_txb_context(int plane, int block, int blk_row, int blk_col,
                               BLOCK_SIZE plane_bsize, TX_SIZE tx_size,
                               void *arg) {
  TxbParams *const args = arg;
  ThreadData *const td = args->td;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  (void)plane_bsize;
  av1_set_contexts(xd, pd, plane, tx_size, p->eobs[block] > 0, blk_col,
                   blk_row);
}

static void update_and_record_txb_context(int plane, int block, int blk_row,
                                          int blk_col, BLOCK_SIZE plane_bsize,
                                          TX_SIZE tx_size, void *arg) {
  TxbParams *const args = arg;
  const AV1_COMP *cpi = args->cpi;
  const AV1_COMMON *cm = &cpi->common;
  ThreadData *const td = args->td;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *p = &x->plane[plane];
  struct macroblockd_plane *pd = &xd->plane[plane];
  MB_MODE_INFO *mbmi = &xd->mi[0]->mbmi;
  int eob = p->eobs[block], update_eob = 0;
  const PLANE_TYPE plane_type = pd->plane_type;
  const tran_low_t *qcoeff = BLOCK_OFFSET(p->qcoeff, block);
  tran_low_t *tcoeff = BLOCK_OFFSET(x->mbmi_ext->tcoeff[plane], block);
  const int segment_id = mbmi->segment_id;
  const int16_t *scan, *nb;
  const TX_TYPE tx_type = get_tx_type(plane_type, xd, block, tx_size);
  const SCAN_ORDER *const scan_order =
      get_scan(cm, tx_size, tx_type, is_inter_block(mbmi));
  const int ref = is_inter_block(mbmi);
  unsigned int(*const counts)[COEFF_CONTEXTS][ENTROPY_TOKENS] =
      td->rd_counts.coef_counts[tx_size][plane_type][ref];
  const uint8_t *const band = get_band_translate(tx_size);
  const int seg_eob = get_tx_eob(&cpi->common.seg, segment_id, tx_size);
  int c, i;
  TXB_CTX txb_ctx;
  get_txb_ctx(plane_bsize, tx_size, plane, pd->above_context + blk_col,
              pd->left_context + blk_row, &txb_ctx);
  const int bwl = b_width_log2_lookup[txsize_to_bsize[tx_size]] + 2;
  int cul_level = 0;
  unsigned int(*nz_map_count)[SIG_COEF_CONTEXTS][2];
  uint8_t txb_mask[32 * 32] = { 0 };

  nz_map_count = &td->counts->nz_map[tx_size][plane_type];

  scan = scan_order->scan;
  nb = scan_order->neighbors;

  memcpy(tcoeff, qcoeff, sizeof(*tcoeff) * seg_eob);

  (void)nb;
  (void)counts;
  (void)band;

  ++td->counts->txb_skip[tx_size][txb_ctx.txb_skip_ctx][eob == 0];
  x->mbmi_ext->txb_skip_ctx[plane][block] = txb_ctx.txb_skip_ctx;

  x->mbmi_ext->eobs[plane][block] = eob;

  if (eob == 0) {
    av1_set_contexts(xd, pd, plane, tx_size, 0, blk_col, blk_row);
    return;
  }

  // update_tx_type_count(cm, mbmi, td, plane, block);

  for (c = 0; c < eob; ++c) {
    tran_low_t v = qcoeff[scan[c]];
    int is_nz = (v != 0);
    int coeff_ctx = get_nz_map_ctx(tcoeff, txb_mask, scan[c], bwl);
    int eob_ctx = get_eob_ctx(tcoeff, scan[c], bwl);

    if (c == seg_eob - 1) break;

    ++(*nz_map_count)[coeff_ctx][is_nz];

    if (is_nz) {
      ++td->counts->eob_flag[tx_size][plane_type][eob_ctx][c == (eob - 1)];
    }
    txb_mask[scan[c]] = 1;
  }

  // Reverse process order to handle coefficient level and sign.
  for (i = 0; i < NUM_BASE_LEVELS; ++i) {
    update_eob = 0;
    for (c = eob - 1; c >= 0; --c) {
      tran_low_t v = qcoeff[scan[c]];
      tran_low_t level = abs(v);
      int ctx;

      if (level <= i) continue;

      ctx = get_base_ctx(tcoeff, scan[c], bwl, i + 1);

      if (level == i + 1) {
        ++td->counts->coeff_base[tx_size][plane_type][i][ctx][1];
        if (c == 0) {
          int dc_sign_ctx = txb_ctx.dc_sign_ctx;

          ++td->counts->dc_sign[plane_type][dc_sign_ctx][v < 0];
          x->mbmi_ext->dc_sign_ctx[plane][block] = dc_sign_ctx;
        }
        cul_level += level;
        continue;
      }
      ++td->counts->coeff_base[tx_size][plane_type][i][ctx][0];
      update_eob = AOMMAX(update_eob, c);
    }
  }

  for (c = update_eob; c >= 0; --c) {
    tran_low_t v = qcoeff[scan[c]];
    tran_low_t level = abs(v);
    int idx;
    int ctx;

    if (level <= NUM_BASE_LEVELS) continue;

    cul_level += level;
    if (c == 0) {
      int dc_sign_ctx = txb_ctx.dc_sign_ctx;

      ++td->counts->dc_sign[plane_type][dc_sign_ctx][v < 0];
      x->mbmi_ext->dc_sign_ctx[plane][block] = dc_sign_ctx;
    }

    // level is above 1.
    ctx = get_level_ctx(tcoeff, scan[c], bwl);
    for (idx = 0; idx < COEFF_BASE_RANGE; ++idx) {
      if (level == (idx + 1 + NUM_BASE_LEVELS)) {
        ++td->counts->coeff_lps[tx_size][plane_type][ctx][1];
        break;
      }
      ++td->counts->coeff_lps[tx_size][plane_type][ctx][0];
    }
    if (idx < COEFF_BASE_RANGE) continue;

    // use 0-th order Golomb code to handle the residual level.
  }
  cul_level = AOMMIN(63, cul_level);

  // DC value
  set_dc_sign(&cul_level, tcoeff[0]);
  av1_set_contexts(xd, pd, plane, tx_size, cul_level, blk_col, blk_row);

#if CONFIG_ADAPT_SCAN
  // Since dqcoeff is not available here, we pass qcoeff into
  // av1_update_scan_count_facade(). The update behavior should be the same
  // because av1_update_scan_count_facade() only cares if coefficients are zero
  // or not.
  av1_update_scan_count_facade((AV1_COMMON *)cm, td->counts, tx_size, tx_type,
                               qcoeff, eob);
#endif
}

void av1_update_txb_context(const AV1_COMP *cpi, ThreadData *td,
                            RUN_TYPE dry_run, BLOCK_SIZE bsize, int *rate,
                            const int mi_row, const int mi_col) {
  const AV1_COMMON *const cm = &cpi->common;
  MACROBLOCK *const x = &td->mb;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0]->mbmi;
  const int ctx = av1_get_skip_context(xd);
  const int skip_inc =
      !segfeature_active(&cm->seg, mbmi->segment_id, SEG_LVL_SKIP);
  struct TxbParams arg = { cpi, td, 0 };
  (void)rate;
  (void)mi_row;
  (void)mi_col;
  if (mbmi->skip) {
    if (!dry_run) td->counts->skip[ctx][1] += skip_inc;
    reset_skip_context(xd, bsize);
    return;
  }

  if (!dry_run) {
    td->counts->skip[ctx][0] += skip_inc;
    av1_foreach_transformed_block(xd, bsize, update_and_record_txb_context,
                                  &arg);
  } else if (dry_run == DRY_RUN_NORMAL) {
    av1_foreach_transformed_block(xd, bsize, update_txb_context, &arg);
  } else {
    printf("DRY_RUN_COSTCOEFFS is not supported yet\n");
    assert(0);
  }
}
