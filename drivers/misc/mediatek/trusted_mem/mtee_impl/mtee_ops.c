/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define PR_FMT_HEADER_MUST_BE_INCLUDED_BEFORE_ALL_HDRS
#include "private/tmem_pr_fmt.h" PR_FMT_HEADER_MUST_BE_INCLUDED_BEFORE_ALL_HDRS

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/slab.h>
#if defined(CONFIG_MTK_GZ_KREE)
#include <kree/system.h>
#include <kree/mem.h>
#include <kree/tz_mod.h>
#endif

#include "private/mld_helper.h"
#include "private/tmem_device.h"
#include "private/tmem_error.h"
#include "private/tmem_utils.h"
#include "private/secmem_ext.h"
/* clang-format off */
#include "mtee_impl/mtee_priv.h"
/* clang-format on */

static const char mem_srv_name[] = "com.mediatek.geniezone.srv.mem";

#define LOCK_BY_CALLEE (0)
#if LOCK_BY_CALLEE
#define MTEE_SESSION_LOCK_INIT() mutex_init(&sess_data->lock)
#define MTEE_SESSION_LOCK() mutex_lock(&sess_data->lock)
#define MTEE_SESSION_UNLOCK() mutex_unlock(&sess_data->lock)
#else
#define MTEE_SESSION_LOCK_INIT()
#define MTEE_SESSION_LOCK()
#define MTEE_SESSION_UNLOCK()
#endif

struct MTEE_SESSION_DATA {
	KREE_SESSION_HANDLE session_handle;
	KREE_SECUREMEM_HANDLE append_mem_handle;
#if LOCK_BY_CALLEE
	struct mutex lock;
#endif
};

static struct MTEE_SESSION_DATA *
MTEE_create_session_data(enum TRUSTED_MEM_TYPE mem_type)
{
	struct MTEE_SESSION_DATA *sess_data;

	sess_data = mld_kmalloc(sizeof(struct MTEE_SESSION_DATA), GFP_KERNEL);
	if (INVALID(sess_data)) {
		pr_err("%s:%d %d:out of memory!\n", __func__, __LINE__,
		       mem_type);
		return NULL;
	}

	memset(sess_data, 0x0, sizeof(struct MTEE_SESSION_DATA));

	MTEE_SESSION_LOCK_INIT();
	return sess_data;
}

static void MTEE_destroy_session_data(struct MTEE_SESSION_DATA *sess_data)
{
	if (VALID(sess_data))
		mld_kfree(sess_data);
}

static int MTEE_session_open(void **peer_data, void *priv)
{
	int ret = 0;
	struct MTEE_SESSION_DATA *sess_data;
	struct mtee_peer_ops_priv_data *priv_data =
		(struct mtee_peer_ops_priv_data *)priv;

	UNUSED(priv);

	sess_data = MTEE_create_session_data(priv_data->mem_type);
	if (INVALID(sess_data)) {
		pr_err("[%d] Create session data failed: out of memory!\n",
		       priv_data->mem_type);
		return TMEM_MTEE_CREATE_SESSION_FAILED;
	}

	MTEE_SESSION_LOCK();

	ret = KREE_CreateSession(mem_srv_name, &sess_data->session_handle);
	if (ret != 0) {
		pr_err("[%d] MTEE open session failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MTEE_CREATE_SESSION_FAILED;
	}

	*peer_data = (void *)sess_data;
	MTEE_SESSION_UNLOCK();
	return TMEM_OK;
}

static int MTEE_session_close(void *peer_data, void *priv)
{
	int ret;
	struct MTEE_SESSION_DATA *sess_data =
		(struct MTEE_SESSION_DATA *)peer_data;
	struct mtee_peer_ops_priv_data *priv_data =
		(struct mtee_peer_ops_priv_data *)priv;

	UNUSED(priv);
	MTEE_SESSION_LOCK();

	ret = KREE_CloseSession(sess_data->session_handle);
	if (ret != 0) {
		pr_err("[%d] MTEE close session failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MTEE_CLOSE_SESSION_FAILED;
	}

	MTEE_SESSION_UNLOCK();
	MTEE_destroy_session_data(sess_data);
	return TMEM_OK;
}

static int MTEE_alloc(u32 alignment, u32 size, u32 *refcount, u32 *sec_handle,
		      u8 *owner, u32 id, u32 clean, void *peer_data, void *priv)
{
	int ret;
	struct MTEE_SESSION_DATA *sess_data =
		(struct MTEE_SESSION_DATA *)peer_data;
	struct mtee_peer_ops_priv_data *priv_data =
		(struct mtee_peer_ops_priv_data *)priv;

	UNUSED(priv);
	MTEE_SESSION_LOCK();

	if (clean) {
		ret = KREE_ION_ZallocChunkmem(sess_data->session_handle,
					      sess_data->append_mem_handle,
					      sec_handle, alignment, size);
	} else {
		ret = KREE_ION_AllocChunkmem(sess_data->session_handle,
					     sess_data->append_mem_handle,
					     sec_handle, alignment, size);
	}

	if (ret != 0) {
		pr_err("[%d] MTEE alloc chunk memory failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MTEE_ALLOC_CHUNK_FAILED;
	}

	*refcount = 1;
	MTEE_SESSION_UNLOCK();
	return TMEM_OK;
}

static int MTEE_free(u32 sec_handle, u8 *owner, u32 id, void *peer_data,
		     void *priv)
{
	int ret;
	struct MTEE_SESSION_DATA *sess_data =
		(struct MTEE_SESSION_DATA *)peer_data;
	struct mtee_peer_ops_priv_data *priv_data =
		(struct mtee_peer_ops_priv_data *)priv;

	UNUSED(priv);
	MTEE_SESSION_LOCK();

	ret = KREE_ION_UnreferenceChunkmem(sess_data->session_handle,
					   sec_handle);
	if (ret != 0) {
		pr_err("[%d] MTEE free chunk memory failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MTEE_FREE_CHUNK_FAILED;
	}

	MTEE_SESSION_UNLOCK();
	return TMEM_OK;
}

static int MTEE_mem_reg_cfg_notify_tee(enum TRUSTED_MEM_TYPE mem_type, u64 pa,
				       u32 size)
{
	switch (mem_type) {
	case TRUSTED_MEM_PROT:
#if defined(CONFIG_MTK_SECURE_MEM_SUPPORT)                                     \
	&& defined(CONFIG_MTK_CAM_SECURITY_SUPPORT)
		return secmem_fr_set_prot_shared_region(pa, size);
#else
		return TMEM_OK;
#endif
	case TRUSTED_MEM_SDSP_SHARED:
#if defined(CONFIG_MTK_SDSP_SHARED_MEM_SUPPORT)
		return secmem_set_sdsp_shared_region(pa, size);
#else
		return TMEM_OK;
#endif
	case TRUSTED_MEM_SVP:
	case TRUSTED_MEM_WFD:
	case TRUSTED_MEM_SVP_VIRT_2D_FR:
	case TRUSTED_MEM_HAPP_EXTRA:
	case TRUSTED_MEM_HAPP:
	case TRUSTED_MEM_SDSP:
	default:
		return TMEM_OK;
	}
}

static int MTEE_mem_reg_add(u64 pa, u32 size, void *peer_data, void *priv)
{
	int ret;
	struct MTEE_SESSION_DATA *sess_data =
		(struct MTEE_SESSION_DATA *)peer_data;
	struct mtee_peer_ops_priv_data *priv_data =
		(struct mtee_peer_ops_priv_data *)priv;
	KREE_SHAREDMEM_PARAM mem_param;

	UNUSED(priv);
	mem_param.buffer = (void *)pa;
	mem_param.size = size;
	mem_param.mapAry = NULL;
#if defined(CONFIG_MTK_MTEE_MULTI_CHUNK_SUPPORT)
	mem_param.region_id = priv_data->mem_type;
#endif

	MTEE_SESSION_LOCK();

	ret = KREE_AppendSecureMultichunkmem(sess_data->session_handle,
					     &sess_data->append_mem_handle,
					     &mem_param);
	if (ret != 0) {
		pr_err("[%d] MTEE append reg mem failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MOCK_APPEND_MEMORY_FAILED;
	}

	ret = MTEE_mem_reg_cfg_notify_tee(priv_data->mem_type, pa, size);
	if (ret != 0) {
		pr_err("[%d] MTEE notify reg mem add to TEE failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MOCK_NOTIFY_MEM_ADD_CFG_TO_TEE_FAILED;
	}

	MTEE_SESSION_UNLOCK();
	return TMEM_OK;
}

static int MTEE_mem_reg_remove(void *peer_data, void *priv)
{
	int ret;
	struct MTEE_SESSION_DATA *sess_data =
		(struct MTEE_SESSION_DATA *)peer_data;
	struct mtee_peer_ops_priv_data *priv_data =
		(struct mtee_peer_ops_priv_data *)priv;

	UNUSED(priv);
	MTEE_SESSION_LOCK();

	ret = KREE_ReleaseSecureMultichunkmem(sess_data->session_handle,
					      sess_data->append_mem_handle);
	if (ret != 0) {
		pr_err("[%d] MTEE release reg mem failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MTEE_RELEASE_MEMORY_FAILED;
	}

	ret = MTEE_mem_reg_cfg_notify_tee(priv_data->mem_type, 0x0ULL, 0x0);
	if (ret != 0) {
		pr_err("[%d] MTEE notify reg mem remove to TEE failed:%d\n",
		       priv_data->mem_type, ret);
		MTEE_SESSION_UNLOCK();
		return TMEM_MTEE_NOTIFY_MEM_REMOVE_CFG_TO_TEE_FAILED;
	}

	MTEE_SESSION_UNLOCK();
	return TMEM_OK;
}

static int MTEE_invoke_command(struct trusted_driver_cmd_params *invoke_params,
			       void *peer_data, void *priv)
{
	UNUSED(invoke_params);
	UNUSED(peer_data);
	UNUSED(priv);

	pr_err("%s:%d operation is not implemented yet!\n", __func__, __LINE__);
	return TMEM_OPERATION_NOT_IMPLEMENTED;
}

static struct trusted_driver_operations mtee_peer_ops = {
	.session_open = MTEE_session_open,
	.session_close = MTEE_session_close,
	.memory_alloc = MTEE_alloc,
	.memory_free = MTEE_free,
	.memory_grant = MTEE_mem_reg_add,
	.memory_reclaim = MTEE_mem_reg_remove,
	.invoke_cmd = MTEE_invoke_command,
};

void get_mtee_peer_ops(struct trusted_driver_operations **ops)
{
	pr_info("MTEE_PEER_OPS\n");
	*ops = &mtee_peer_ops;
}