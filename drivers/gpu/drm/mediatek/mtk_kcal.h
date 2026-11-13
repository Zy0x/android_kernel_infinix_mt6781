/*
 * Copyright (C) 2026 Sol Kernel Project
 * Hardware KCAL Engine for MediaTek MT6781 (DISP_CCORR0)
 */

#ifndef _MTK_KCAL_H_
#define _MTK_KCAL_H_

#include <linux/types.h>

#define KCAL_NOMINAL_VAL 256
#define KCAL_SCALE_BASE  1024

bool mtk_kcal_is_enabled(void);
void mtk_kcal_get_matrix_and_offset(int matrix[3][3], int offset[3]);
void mtk_kcal_notify_display_power(bool is_on);

int mtk_kcal_init(void);
void mtk_kcal_exit(void);

#endif /* _MTK_KCAL_H_ */
