/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */
extern void unthrottle_offline_rt_rqs(struct rq *rq);
DECLARE_PER_CPU(struct hmp_domain *, hmp_cpu_domain);
#include "../../drivers/misc/mediatek/base/power/include/mtk_upower.h"
extern int l_plus_cpu;
extern unsigned long get_cpu_util(int cpu);
#ifdef CONFIG_SMP
#ifdef CONFIG_ARM64
extern unsigned long arch_scale_get_max_freq(int cpu);
extern unsigned long arch_scale_get_min_freq(int cpu);
#else
static inline unsigned long arch_scale_get_max_freq(int cpu) { return 0; }
static inline unsigned long arch_scale_get_min_freq(int cpu) { return 0; }
#endif
#endif
#define SCHED_ENHANCED_ATTR 0x40000
int select_task_prefer_cpu(struct task_struct *p, int new_cpu);
int
sched_setattr_enhanced(struct task_struct *p, const struct sched_attr *attr);

int task_prefer_little(struct task_struct *p);
int task_prefer_big(struct task_struct *p);
int task_prefer_fit(struct task_struct *p, int cpu);
int task_prefer_match(struct task_struct *p, int cpu);
int
task_prefer_match_on_cpu(struct task_struct *p, int src_cpu, int target_cpu);

#define LB_POLICY_SHIFT 16
#define LB_CPU_MASK ((1 << LB_POLICY_SHIFT) - 1)

#define LB_PREV          (0x0  << LB_POLICY_SHIFT)
#define LB_FORK          (0x1  << LB_POLICY_SHIFT)
#define LB_SMP           (0x2  << LB_POLICY_SHIFT)
#define LB_HMP           (0x4  << LB_POLICY_SHIFT)
#define LB_EAS           (0x8  << LB_POLICY_SHIFT)
#define LB_EAS_AFFINE   (0x18  << LB_POLICY_SHIFT)
#define LB_EAS_LB       (0x28  << LB_POLICY_SHIFT)