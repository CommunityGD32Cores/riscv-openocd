/***************************************************************************
 *   Copyright (C) 2024 by GigaDevice Semiconductor, Inc.                  *
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
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <target/algorithm.h>
#define BIT(x)                          ((uint32_t)((uint32_t)0x01U<<(x)))
#define BITS(start, end)                ((0xFFFFFFFFUL << (start)) & (0xFFFFFFFFUL >> (31U - (uint32_t)(end))))
#define GET_BITS(regval, start, end)    (((regval) & BITS((start), (end))) >> (start))

/* FMC and option byte definition */
#define FMC_BASE                         0x40022000

/* registers definitions */
#define FMC_WS                           0x40022000
#define FMC_KEY                          0x40022004
#define FMC_OBKEY                        0x40022008
#define FMC_STAT                         0x4002200C
#define FMC_CTL                          0x40022010
#define FMC_ADDR                         0x40022014
#define FMC_OBSTAT                       0x4002201C
#define FMC_OBR                          0x40022040
#define FMC_OBUSER                       0x40022044
#define FMC_OBWRP0                       0x40022048
#define FMC_OBWRP1                       0x4002204C
#define FMC_PID0                         0x40022100
#define FMC_PID1                         0x40022104

/* FMC_STAT */
#define FMC_STAT_BUSY                     BIT(0)
#define FMC_STAT_WPERR                    BIT(4)
#define FMC_STAT_ENDF                     BIT(5)

/* FMC_CTL */
#define FMC_CTL_PG                        BIT(0)
#define FMC_CTL_PER                       BIT(1)
#define FMC_CTL_MER                       BIT(2)
#define FMC_CTL_OBPG                      BIT(4)
#define FMC_CTL_OBER                      BIT(5)
#define FMC_CTL_START                     BIT(6)
#define FMC_CTL_LK                        BIT(7)
#define FMC_CTL_OBWEN                     BIT(9)
#define FMC_CTL_ERRIE                     BIT(10)
#define FMC_CTL_ENDIE                     BIT(12)
#define FMC_CTL_OBSTART                   BIT(14)
#define FMC_CTL_OBRLD                     BIT(15)

/* FMC_OBSTAT */
#define FMC_OBSTAT_SPC                    BIT(1)
#define FMC_OBSTAT_WP                     BIT(2)

/* FMC_OBR */
#define FMC_OBR_SPC                       BITS(0, 7)
#define FMC_OBR_NWDG_HW                   BIT(9)
#define FMC_OBR_NRST_STDBY                BIT(10)
#define FMC_OBR_NSRT_DPSLP                BIT(11)
#define FMC_OBR_SRAM1_RST                 BIT(12)

/* FMC_OBUSER */
#define FMC_OBUSER_USER                   BITS(0, 31)

/* FMC_OBWRP0 */
#define FMC_OBWRP0_WRP0_SPAGE             BITS(0, 9)
#define FMC_OBWRP0_WRP0_EPAGE             BITS(16, 25)

/* FMC_OBWRP1 */
#define FMC_OBWRP1_WRP1_SPAGE             BITS(0, 9)
#define FMC_OBWRP1_WRP1_EPAGE             BITS(16, 25)

/* unlock keys */
#define UNLOCK_KEY0                       0x45670123
#define UNLOCK_KEY1                       0xCDEF89AB

/* read protect configuration */
#define FMC_NSPC                          ((uint8_t)0xAAU)

/* FMC time out */
#define FMC_TIMEOUT_COUNT                  0x01000000

/* number of flash bank */
#define gd32vw55x_FLASH_BANKS 1

/* option bytes structure */
struct gd32vw55x_options {
	uint8_t spc;
	uint32_t user;
	uint32_t wrp[2];
};

/* gd32vw55x flash bank type structure */
struct gd32vw55x_flash_bank_type {
	uint32_t bank_base;
	uint32_t bank_size;
	uint32_t bank_page_size;
	uint32_t wrp_page_size;
};

/* gd32vw55x flash bank structure */
struct gd32vw55x_flash_bank {
	struct gd32vw55x_options option_bytes;
	int probed;
	uint32_t cpu_id;
	uint32_t dbg_id;
	uint32_t flash_size;
	struct gd32vw55x_flash_bank_type gd32vw55x_bank[gd32vw55x_FLASH_BANKS];
};

static int gd32vw55x_mass_erase(struct flash_bank *bank);
static int gd32vw55x_write_block(struct flash_bank *bank, const uint8_t *buffer,
		uint32_t offset, uint32_t count);

FLASH_BANK_COMMAND_HANDLER(gd32vw55x_flash_bank_command)
{
	struct gd32vw55x_flash_bank *gd32vw55x_info;

	if (CMD_ARGC < 6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	gd32vw55x_info = malloc(sizeof(struct gd32vw55x_flash_bank));

	bank->driver_priv = gd32vw55x_info;
	gd32vw55x_info->probed = 0;
	gd32vw55x_info->cpu_id = 0;
	gd32vw55x_info->dbg_id = 0;
	gd32vw55x_info->flash_size = bank->size;
	gd32vw55x_info->gd32vw55x_bank[0].bank_base = 0x08000000;
	gd32vw55x_info->gd32vw55x_bank[0].bank_size = bank->size;
	gd32vw55x_info->gd32vw55x_bank[0].bank_page_size = 4096;
	gd32vw55x_info->gd32vw55x_bank[0].wrp_page_size = 1;

	return ERROR_OK;
}

static int gd32vw55x_ready_wait(struct flash_bank *bank, int timeout)
{
	struct target *target = bank->target;
	uint32_t status;
	int retval = ERROR_OK;

	/* wait for FMC ready */
	do {
		alive_sleep(1);
		timeout--;
		retval = target_read_u32(target, FMC_STAT, &status);
		if (retval != ERROR_OK)
			return retval;
		LOG_DEBUG("status: 0x%" PRIx32 "", status);
	} while (((status & FMC_STAT_BUSY) != 0) && timeout);

	if (timeout == 0) {
		LOG_DEBUG("GD32: Flash ready wait ... timed out waiting for flash ready");
		return ERROR_FAIL;
	}

		target_read_u32(target, FMC_STAT, &status);
		LOG_INFO("fmc status = %x", status);
	if (status & FMC_STAT_WPERR) {
		LOG_ERROR("GD32: Flash ready wait ... gd32vw55x device is write/erase protected");
		retval = ERROR_FAIL;
	}

	/* clear all FMC status errors */
	if (status & (FMC_STAT_WPERR)) {
		target_write_u32(target, FMC_STAT, FMC_STAT_WPERR);
		LOG_INFO("GD32: clear all FMC status errors");
		target_read_u32(target, FMC_STAT, &status);
		LOG_INFO("fmc status = %x", status);
	}
	return retval;
}

static int gd32vw55x_ob_get(struct flash_bank *bank)
{
	uint32_t fmc_obstat_reg, fmc_obwrp1_reg, fmc_obwrp0_reg, fmc_obuser_reg, fmc_obr_reg;

	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;
	struct target *target = bank->target;

	gd32vw55x_info = bank->driver_priv;

	/* read current option bytes */
	int retval = target_read_u32(target, FMC_OBSTAT, &fmc_obstat_reg);
	if (retval != ERROR_OK) {
		return retval;
		LOG_INFO("GD32: Get option bytes status ... read FMC_OBSTAT error");
	}
	retval = target_read_u32(target, FMC_OBUSER, &fmc_obuser_reg);
	if (retval != ERROR_OK) {
		return retval;
		LOG_INFO("GD32: Get option bytes status ... read FMC_OBUSER error");
	}
	retval = target_read_u32(target, FMC_OBR, &fmc_obr_reg);
	if (retval != ERROR_OK) {
		return retval;
		LOG_INFO("GD32: Get option bytes status ... read FMC_OBR error");
	}

	gd32vw55x_info->option_bytes.user = fmc_obuser_reg;
	gd32vw55x_info->option_bytes.spc = (uint8_t)(fmc_obr_reg & 0xFF);

	if (fmc_obstat_reg & (BIT(1)))
		LOG_INFO("GD32: Get option bytes ... device level 1 protection bit set");
	else
		LOG_INFO("GD32: Get option bytes ... device no protection level bit set");

	/* each bit refers to a page protection */
	retval = target_read_u32(target, FMC_OBWRP0, &fmc_obwrp0_reg);
	if (retval != ERROR_OK) {
		return retval;
		LOG_INFO("GD32: Get option bytes ... read FMC_OBWRP0 error");
	}
	retval = target_read_u32(target, FMC_OBWRP1, &fmc_obwrp1_reg);
	if (retval != ERROR_OK) {
		return retval;
		LOG_INFO("GD32: Get option bytes ... read FMC_OBWRP1 error");
	}

	gd32vw55x_info->option_bytes.wrp[0] = fmc_obwrp0_reg;
	gd32vw55x_info->option_bytes.wrp[1] = fmc_obwrp1_reg;

	if (fmc_obstat_reg & (BIT(2)))
		LOG_INFO("GD32: Get option bytes ... device erase\program protection bit set");
	else
		LOG_INFO("GD32: Get option bytes ... device no erase\program protection bit set");

	return ERROR_OK;
}

static int gd32vw55x_ob_reload(struct flash_bank *bank)
{
	uint32_t retry = 100;
	struct target *target = bank->target;
	uint32_t fmc_ctl_reg, fmc_obr_reg, fmc_obstat_reg;

	/* read current options */
	gd32vw55x_ob_get(bank);

	/* unlock the main FMC operation */
	do {
		target_write_u32(target, FMC_KEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_KEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg = fmc_ctl_reg & FMC_CTL_LK;
	} while ((retry--) && fmc_ctl_reg);
	if (retry == 0) {
		LOG_DEBUG("GD32: Option bytes reload ... timed out waiting for flash unlock");
		return ERROR_FAIL;
	}
	retry = 100;
	/* unlock the option bytes operation */
	do {
		target_write_u32(target, FMC_OBKEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_OBKEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg &= FMC_CTL_OBWEN;
	} while ((retry--) && (!fmc_ctl_reg));
	if (retry == 0) {
		LOG_DEBUG("GD32: Option bytes reload ... timed out waiting for flash option byte unlock");
		return ERROR_FAIL;
	}
	target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
	fmc_ctl_reg |= FMC_CTL_OBRLD;
	target_write_u32(target, FMC_CTL, fmc_ctl_reg);
	struct gd32vw55x_flash_bank *gd32vw55x_info = bank->driver_priv;
	gd32vw55x_info->probed = 0;
	return ERROR_OK;
}

static int gd32vw55x_ob_erase(struct flash_bank *bank)
{
	uint32_t retry = 100;
	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;
	struct target *target = bank->target;
	uint32_t fmc_ctl_reg, fmc_obr_reg, fmc_obstat_reg;
	gd32vw55x_info = bank->driver_priv;

	/* read current options */
	gd32vw55x_ob_get(bank);

	/* unlock the main FMC operation */
	do {
		target_write_u32(target, FMC_KEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_KEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg = fmc_ctl_reg & FMC_CTL_LK;
	} while ((retry--) && fmc_ctl_reg);
	if (retry == 0) {
		LOG_DEBUG("GD32: Option bytes erase ... timed out waiting for flash unlock");
		return ERROR_FAIL;
	}
	retry = 100;
	/* unlock the option bytes operation */
	do {
		target_write_u32(target, FMC_OBKEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_OBKEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg &= FMC_CTL_OBWEN;
	} while ((retry--) && (!fmc_ctl_reg));
	if (retry == 0) {
		LOG_INFO("GD32: Option bytes erase ... timed out waiting for flash option byte unlock");
		return ERROR_FAIL;
	}

	/* erase option bytes : reset value */
	target_write_u32(target, FMC_OBWRP0, 0X000003FF);
	target_write_u32(target, FMC_OBWRP1, 0X000003FF);
	target_write_u32(target, FMC_OBUSER, 0XFFFFFFFF);
	target_write_u32(target, FMC_OBR, 0X00000EAA);

	target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
	fmc_ctl_reg |= FMC_CTL_OBSTART;
	target_write_u32(target, FMC_CTL, fmc_ctl_reg);

	int retval = gd32vw55x_ready_wait(bank, FMC_TIMEOUT_COUNT);
	if (retval != ERROR_OK) {
		LOG_DEBUG("GD32: Option byte erase ... waiting for option bytes erase failed");
		return retval;
	}

	target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
	target_read_u32 (target, FMC_OBR, &fmc_obr_reg);
	LOG_INFO("fmc_ctl_reg = %x", fmc_ctl_reg);
	LOG_INFO("fmc_obr_reg = %x", fmc_obr_reg);

	target_write_u32(target, FMC_CTL, FMC_CTL_LK);

	/* set SPC to no security protection */
	gd32vw55x_info->option_bytes.spc = FMC_NSPC;

	return ERROR_OK;
}

static int gd32vw55x_ob_write(struct flash_bank *bank)
{
	uint32_t retry = 100;
	uint32_t fmc_ctl_reg, fmc_obr_reg;
	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;
	struct target *target = bank->target;

	gd32vw55x_info = bank->driver_priv;

	/* unlock the main FMC operation */
	do {
		target_write_u32(target, FMC_KEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_KEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg &= FMC_CTL_LK;
	} while ((retry--) && fmc_ctl_reg);
	if (retry == 0) {
		LOG_DEBUG("GD32: Option bytes write ... timed out waiting for flash unlock");
		return ERROR_FAIL;
	}

	/* unlock the option bytes operation */
	do {
		target_write_u32(target, FMC_OBKEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_OBKEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg &= FMC_CTL_OBWEN;
	} while ((retry--) && (!fmc_ctl_reg));
	if (retry == 0) {
		LOG_DEBUG("GD32: Option bytes wirte ... timed out waiting for flash option byte unlock");
		return ERROR_FAIL;
	}

	/* program option bytes */
	target_write_u32(target, FMC_OBWRP0, gd32vw55x_info->option_bytes.wrp[0]);
	target_write_u32(target, FMC_OBWRP1, gd32vw55x_info->option_bytes.wrp[1]);
	target_write_u32(target, FMC_OBUSER, gd32vw55x_info->option_bytes.user);

	target_read_u32 (target, FMC_OBR, &fmc_obr_reg);
	fmc_obr_reg = ((fmc_obr_reg & 0xFFFFFF00) | gd32vw55x_info->option_bytes.spc);
	target_write_u32(target, FMC_OBR, fmc_obr_reg);

	target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
	fmc_ctl_reg |= FMC_CTL_OBSTART;
	target_write_u32(target, FMC_CTL, fmc_ctl_reg);
	target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
	LOG_DEBUG("fmc_ctl_reg = %x", fmc_ctl_reg);
	target_read_u32 (target, FMC_OBR, &fmc_obr_reg);
	LOG_DEBUG("fmc_obr_reg = %x", fmc_obr_reg);

	int retval = gd32vw55x_ready_wait(bank, FMC_TIMEOUT_COUNT);
	if (retval != ERROR_OK) {
		LOG_DEBUG("GD32: Option byte erase ... waiting for option bytes erase failed");
		return retval;
	}
	target_write_u32(target, FMC_CTL, FMC_CTL_LK);

	return ERROR_OK;
}

static int gd32vw55x_protect_check(struct flash_bank *bank)
{
	struct target *target = bank->target;
	struct gd32vw55x_flash_bank *gd32vw55x_info = bank->driver_priv;

	uint32_t fmc_obwrp0_reg, fmc_obwrp1_reg;
	uint32_t i, s;
	unsigned int num_bits;
	int set, set0, set1;

	target_read_u32(target, FMC_OBWRP0, &fmc_obwrp0_reg);
	target_read_u32(target, FMC_OBWRP1, &fmc_obwrp1_reg);

	/* each protection bit is for 4K pages */
	num_bits = (bank->num_sectors / gd32vw55x_info->gd32vw55x_bank[0].wrp_page_size);

	/* flash write/erase protection */
	if (((fmc_obwrp0_reg & 0xFFFF0000) >> 16) >= (fmc_obwrp0_reg & 0xFFFF)) {
		for (i = 0; i < num_bits; i++) {
			if (((fmc_obwrp0_reg & 0xFFFF) <= i) && (i <= ((fmc_obwrp0_reg & 0xFFFF0000) >> 16))) {
				set0 = 1;
				bank->sectors[i].is_protected = set0;
			} else {
				set0 = 0;
				bank->sectors[i].is_protected = set0;
			}
		}
	}
	if ((((fmc_obwrp1_reg & 0xFFFF0000) >> 16) >= (fmc_obwrp1_reg & 0xFFFF))) {
		for (i = 0; i < num_bits; i++) {
			if (((fmc_obwrp1_reg & 0xFFFF) <= i) && (i <= ((fmc_obwrp1_reg & 0xFFFF0000) >> 16))) {
				set1 = 1;
				set = bank->sectors[i].is_protected;
				bank->sectors[i].is_protected = set || set1;
			} else {
				set1 = 0;
				set = bank->sectors[i].is_protected;
				bank->sectors[i].is_protected = set || set1;
			}
		}
	}

	return ERROR_OK;
}

static int gd32vw55x_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
	struct target *target = bank->target;
	unsigned int i;
	uint32_t fmc_ctl_reg, fmc_obstat_reg;
	uint32_t retry = 100;
	int flag;

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	LOG_INFO("GD32: Flash erase ... sector erase(%u to %u)", first, last);

	target_read_u32(target, FMC_OBSTAT, &fmc_obstat_reg);
	LOG_DEBUG("fmc_obstat_reg = %x", fmc_obstat_reg);
	if ((fmc_obstat_reg & 0x6) != 0) {
		LOG_INFO("GD32: Flash erase ... go to ob_erase!!\n");
		flag = gd32vw55x_ob_erase(bank);
		if (flag != ERROR_OK) {
			LOG_INFO("gd32vw55x_ob_erase(bank) failed");
			return ERROR_FAIL;
	}
		LOG_INFO("GD32: Flash erase ... device is proteced, please reset device !!\n");
		return ERROR_FAIL;
	}

	if ((first == 0) && (last == (bank->num_sectors - 1)))
		return gd32vw55x_mass_erase(bank);

	/* unlock the main FMC operation */
	do {
		target_write_u32(target, FMC_KEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_KEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg = fmc_ctl_reg & FMC_CTL_LK;
	} while ((retry--) && fmc_ctl_reg);

	if (retry == 0) {
		LOG_INFO("GD32: Flash erase ...timed out waiting for flash unlock");
		return ERROR_FAIL;
	}

	for (i = first; i <= last; i++) {
		target_write_u32(target, FMC_CTL, FMC_CTL_PER);
		target_write_u32(target, FMC_ADDR, bank->base + bank->sectors[i].offset);
		target_write_u32(target, FMC_CTL, FMC_CTL_PER | FMC_CTL_START);

		int retval = gd32vw55x_ready_wait(bank, FMC_TIMEOUT_COUNT);
		if (retval != ERROR_OK)
			return retval;

		bank->sectors[i].is_erased = 1;
	}

	/* lock the main FMC operation */
	target_write_u32(target, FMC_CTL, FMC_CTL_LK);
	LOG_INFO("erase ok");

	return ERROR_OK;
}

static int gd32vw55x_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;
	struct target *target = bank->target;
	uint32_t wrp_tmp[2] = {0xFFFFFFFF, 0xFFFFFFFF};
	unsigned int i, reg, bit;
	int status;
	uint32_t fmc_obwrp0_reg, fmc_obwrp1_reg, ob_spc_user, start0, start1, end0, end1;

	gd32vw55x_info = bank->driver_priv;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("GD32: Protect ... Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if (first > last) {
		LOG_WARNING("the start and end protect sector number are invalid");
		return 0;
	}

	int retval = target_read_u32(target, FMC_OBWRP0, &fmc_obwrp0_reg);
	if (retval != ERROR_OK)
		return retval;

	retval = target_read_u32(target, FMC_OBWRP1, &fmc_obwrp1_reg);
	if (retval != ERROR_OK)
		return retval;

	if (set)
		fmc_obwrp0_reg = (last << 16) + first;
	else
		fmc_obwrp0_reg = 0x000003ff;
	fmc_obwrp1_reg = 0x000003ff;

	status = gd32vw55x_ob_erase(bank);
	if (status != ERROR_OK)
		return status;

	wrp_tmp[0] = fmc_obwrp0_reg;
	wrp_tmp[1] = fmc_obwrp1_reg;

	gd32vw55x_info->option_bytes.wrp[0] = wrp_tmp[0];
	gd32vw55x_info->option_bytes.wrp[1] = wrp_tmp[1];

	return gd32vw55x_ob_write(bank);
}

static int gd32vw55x_write_block(struct flash_bank *bank, const uint8_t *buffer,
		uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	uint32_t buffer_size = 16384;
	struct working_area *write_algorithm;
	struct working_area *source;
	uint32_t address = bank->base + offset;
	struct reg_param reg_params[5];
	int retval = ERROR_OK;

	static const uint8_t gd32vw55x_flash_write_code[] = {
0x6f, 0x00, 0x80, 0x00, 0x73, 0x00, 0x10, 0x00, 0x03, 0x2b, 0x06, 0x00 , 0x63, 0x0c, 0x0b, 0x04,
0x83, 0x2a, 0x46, 0x00, 0xb3, 0x87, 0x6a, 0x41, 0xe3, 0x88, 0x07, 0xfe , 0x03, 0xab, 0x0a, 0x00,
0x23, 0x20, 0x67, 0x01, 0x93, 0x8a, 0x4a, 0x00, 0x13, 0x07, 0x47, 0x00 , 0x83, 0x2b, 0xc5, 0x00,
0x93, 0xf7, 0x1b, 0x00, 0xe3, 0x9c, 0x07, 0xfe, 0x93, 0xf7, 0x4b, 0x01 , 0x63, 0x90, 0x07, 0x02,
0x63, 0xe6, 0xda, 0x00, 0x93, 0x0a, 0x06, 0x00, 0x93, 0x8a, 0x8a, 0x00 , 0x23, 0x22, 0x56, 0x01,
0x93, 0x85, 0xf5, 0xff, 0x63, 0x88, 0x05, 0x00, 0x6f, 0xf0, 0x1f, 0xfb , 0x13, 0x05, 0x00, 0x00,
0x23, 0x22, 0xa6, 0x00, 0x13, 0x85, 0x0b, 0x00, 0x6f, 0xf0, 0xdf, 0xf9};

	/* flash write code */
	if (target_alloc_working_area(target, sizeof(gd32vw55x_flash_write_code),
			&write_algorithm) != ERROR_OK) {
		LOG_WARNING("GD32: Flash block write ... no working area for block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	retval = target_write_buffer(target, write_algorithm->address,
			sizeof(gd32vw55x_flash_write_code), gd32vw55x_flash_write_code);

	if (retval != ERROR_OK) {
		target_free_working_area(target, write_algorithm);
		return retval;
	}

	/* memory buffer */
	while (target_alloc_working_area_try(target, buffer_size, &source) != ERROR_OK) {
		buffer_size /= 2;
		buffer_size &= ~3UL; /* Make sure it's 4 byte aligned */
		if (buffer_size <= 256) {
			/* we already allocated the writing code, but failed to get a
			 * buffer, free the algorithm */
			target_free_working_area(target, write_algorithm);

			LOG_WARNING("GD32: Flash block write ... no large enough working area available, can't do block memory writes");
			return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
		}
	}

	init_reg_param(&reg_params[0], "a0", 32, PARAM_IN_OUT);	/* flash base (in), status (out) */
	init_reg_param(&reg_params[1], "a1", 32, PARAM_OUT);	/* count (word-32bit) */
	init_reg_param(&reg_params[2], "a2", 32, PARAM_OUT);	/* buffer start */
	init_reg_param(&reg_params[3], "a3", 32, PARAM_OUT);	/* buffer end */
	init_reg_param(&reg_params[4], "a4", 32, PARAM_IN_OUT);	/* target address */

	uint32_t wp_addr = source->address;
	uint32_t rp_addr = source->address + 4;
	uint32_t fifo_start_addr = source->address + 8;
	uint32_t fifo_end_addr = source->address + source->size;

	uint32_t wp = fifo_start_addr;
	uint32_t rp = fifo_start_addr;
	uint32_t thisrun_bytes = fifo_end_addr-fifo_start_addr-4;

	retval = target_write_u32(target, rp_addr, rp);
	if (retval != ERROR_OK)
		return retval;

	while (count > 0) {
		retval = target_read_u32(target, rp_addr, &rp);
		if (retval != ERROR_OK) {
			LOG_ERROR("GD32: Flash block write ... failed to get read pointer");
			break;
		}

		if (wp != rp) {
			LOG_ERROR("GD32: Flash block write ... failed to write flash ;;  rp = 0x%x ;;; wp = 0x%x", rp, wp);
			break;
		}
		wp = fifo_start_addr;
		rp = fifo_start_addr;
		retval = target_write_u32(target, rp_addr, rp);
		if (retval != ERROR_OK)
			break;
		/* Limit to the amount of data we actually want to write */
		if (thisrun_bytes > count * 4)
			thisrun_bytes = count * 4;

		/* Write data to fifo */
		retval = target_write_buffer(target, wp, thisrun_bytes, buffer);
		if (retval != ERROR_OK)
			break;

		/* Update counters and wrap write pointer */
		buffer += thisrun_bytes;
		count -= thisrun_bytes / 4;
		rp = fifo_start_addr;
		wp = fifo_start_addr+thisrun_bytes;

		/* Store updated write pointer to target */
		retval = target_write_u32(target, wp_addr, wp);
		if (retval != ERROR_OK)
			break;
		retval = target_write_u32(target, rp_addr, rp);
		if (retval != ERROR_OK)
			return retval;

		buf_set_u32(reg_params[0].value, 0, 32, FMC_BASE);
		buf_set_u32(reg_params[1].value, 0, 32, thisrun_bytes/4);
		buf_set_u32(reg_params[2].value, 0, 32, source->address);
		buf_set_u32(reg_params[3].value, 0, 32, source->address + source->size);
		buf_set_u32(reg_params[4].value, 0, 32, address);

		retval = target_run_algorithm(target, 0, NULL, 5, reg_params,
				write_algorithm->address, write_algorithm->address+4,
				10000, NULL);

		if (retval != ERROR_OK) {
			LOG_ERROR("GD32: Flash block write ... failed to execute algorithm at 0x%" TARGET_PRIxADDR ": %d",
					write_algorithm->address, retval);
			return retval;
			}
		address += thisrun_bytes;
	}

	if (retval == ERROR_FLASH_OPERATION_FAILED)
		LOG_ERROR("GD32: Flash block write ... error %d executing gd32xxx flash write algorithm", retval);

	target_free_working_area(target, source);
	target_free_working_area(target, write_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);
	destroy_reg_param(&reg_params[4]);

	return retval;
}

static int gd32vw55x_write(struct flash_bank *bank, const uint8_t *buffer,
		uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	uint8_t *new_buffer = NULL;
	uint32_t add_bytes;
	uint32_t fmc_ctl_reg;
	uint32_t retry = 100;

	if (bank->target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	LOG_DEBUG("bank=%p buffer=%p offset=%08" PRIx32 " count=%08" PRIx32 "",
		bank, buffer, offset, count);

	if (offset & 0x3) {
		LOG_ERROR("GD32: Flash write ... offset size must be word aligned");
		return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
	}

	/* support half-word program */
	add_bytes = 4 - count % 4;
	if (add_bytes) {
		new_buffer = malloc(count + add_bytes);
		if (new_buffer == NULL) {
			LOG_ERROR("GD32: Flash write ... not word to write and no memory for padding buffer");
			return ERROR_FAIL;
		}
		LOG_INFO("GD32: Flash write ... not words to write, padding with 0xff");
		memcpy(new_buffer, buffer, count);
		for (uint32_t i = 0; i < add_bytes; i++)
			new_buffer[count+i] = 0xff;
	}

	uint32_t words_remaining = (count + add_bytes) / 4;
	int retval;

	/* unlock the main FMC operation */
	do {
		target_write_u32(target, FMC_KEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_KEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg = fmc_ctl_reg & FMC_CTL_LK;
	} while ((retry--) && fmc_ctl_reg);

	if (retry == 0) {
		LOG_DEBUG("GD32: Flash write ... timed out waiting for flash unlock");
		return ERROR_FAIL;
	}

	target_write_u32(target, FMC_CTL, FMC_CTL_PG);

	LOG_INFO("GD32: Flash write ... words to be prgrammed = 0x%08" PRIx32 "", words_remaining);

	/* write block data */
	retval = gd32vw55x_write_block(bank, new_buffer, offset, words_remaining);

	if (retval == ERROR_TARGET_RESOURCE_NOT_AVAILABLE) {
		/* if block write failed (no sufficient working area),
		 * we use normal (slow) single halfword accesses */
		LOG_WARNING("GD32: Flash write ... couldn't use block writes, falling back to single memory accesses");

		while (words_remaining > 0) {
			uint32_t value;
			memcpy(&value, new_buffer, sizeof(uint32_t));

			retval = target_write_u32(target, bank->base + offset, value);

			retval = gd32vw55x_ready_wait(bank, 5);
			if (retval != ERROR_OK)
				return retval;

			words_remaining--;
			new_buffer += 4;
			offset += 4;
		}
	}

	target_write_u32(target, FMC_CTL, FMC_CTL_LK);

	if (new_buffer)
		free(new_buffer);

	return retval;
}

static int gd32vw55x_probe(struct flash_bank *bank)
{
	struct gd32vw55x_flash_bank *gd32vw55x_info = bank->driver_priv;
	struct target *target = bank->target;
	uint32_t flash_size_reg, dbg_id_reg;
	unsigned int i;
	uint16_t max_flash_size_in_kb, flash_size;
	uint32_t base_address = 0x08000000;
	int retval;

	gd32vw55x_info->probed = 0;

	/* gd32vw55x device id register address */
	dbg_id_reg = 0xE0044000;

	/* read gd32vw55x device id register */
	target_read_u32(target, dbg_id_reg, &gd32vw55x_info->dbg_id);
	flash_size_reg = 0x1FFFF7E0;
	gd32vw55x_info->gd32vw55x_bank[0].bank_page_size = 4096;
	gd32vw55x_info->gd32vw55x_bank[0].wrp_page_size = 1;
	max_flash_size_in_kb = 4096;
	LOG_INFO("device id = 0x%08" PRIx32 "", gd32vw55x_info->dbg_id);

	/* get target device flash size */
	retval = target_read_u16(target, flash_size_reg, &flash_size);
	gd32vw55x_info->flash_size = (uint32_t)flash_size;

	/* target device default flash size */
	if (retval != ERROR_OK || gd32vw55x_info->flash_size == 0xffff || gd32vw55x_info->flash_size == 0) {
		LOG_WARNING("gd32vw55x flash size failed, probe inaccurate - assuming %dk flash",
			max_flash_size_in_kb);
		gd32vw55x_info->flash_size = max_flash_size_in_kb;
	}

	/* if the user sets the size manually then ignore the probed value
	 * this allows us to work around devices that have a invalid flash size register value */
	if (gd32vw55x_info->gd32vw55x_bank[0].bank_size) {
		LOG_INFO("ignoring flash probed value, using configured bank size");
		gd32vw55x_info->flash_size = gd32vw55x_info->gd32vw55x_bank[0].bank_size / 1024;
	}

	LOG_INFO("flash size = %dkbytes", gd32vw55x_info->flash_size);

	if (bank->sectors) {
		free(bank->sectors);
		bank->sectors = NULL;
	}

	/* bank->sectors */
	bank->base = base_address;
	bank->num_sectors = gd32vw55x_info->flash_size * 1024 / gd32vw55x_info->gd32vw55x_bank[0].bank_page_size;
	bank->size = (bank->num_sectors * gd32vw55x_info->gd32vw55x_bank[0].bank_page_size);
	bank->sectors = malloc(sizeof(struct flash_sector) * bank->num_sectors);

	for (i = 0; i < bank->num_sectors; i++) {
		bank->sectors[i].offset = i * gd32vw55x_info->gd32vw55x_bank[0].bank_page_size;
		bank->sectors[i].size = gd32vw55x_info->gd32vw55x_bank[0].bank_page_size;
		bank->sectors[i].is_erased = -1;
		bank->sectors[i].is_protected = 1;
	}

	gd32vw55x_info->probed = 1;

	return ERROR_OK;
}

static int gd32vw55x_auto_probe(struct flash_bank *bank)
{
	struct gd32vw55x_flash_bank *gd32vw55x_info = bank->driver_priv;
	if (gd32vw55x_info->probed)
		return ERROR_OK;
	return gd32vw55x_probe(bank);
}

COMMAND_HANDLER(gd32vw55x_handle_lock_command)
{
	struct target *target = NULL;
	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ERROR_OK != retval)
		return retval;

	gd32vw55x_info = bank->driver_priv;

	target = bank->target;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}


	/* set security protection */
	gd32vw55x_info->option_bytes.spc = 0;
	target_read_u32(target, FMC_OBWRP0, &(gd32vw55x_info->option_bytes.wrp[0]));
	target_read_u32(target, FMC_OBWRP1, &(gd32vw55x_info->option_bytes.wrp[1]));
	target_read_u32(target, FMC_OBUSER, &(gd32vw55x_info->option_bytes.user));

	if (gd32vw55x_ob_write(bank) != ERROR_OK) {
		command_print(CMD, "gd32vw55x failed to lock device");
		return ERROR_OK;
	}

	command_print(CMD, "gd32vw55x locked");

	return ERROR_OK;
}

COMMAND_HANDLER(gd32vw55x_handle_unlock_command)
{
	struct target *target = NULL;
	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ERROR_OK != retval)
		return retval;

	gd32vw55x_info = bank->driver_priv;

	target = bank->target;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* set no security protection */
	gd32vw55x_info->option_bytes.spc = FMC_NSPC;
	target_read_u32(target, FMC_OBWRP0, &(gd32vw55x_info->option_bytes.wrp[0]));
	target_read_u32(target, FMC_OBWRP1, &(gd32vw55x_info->option_bytes.wrp[1]));
	target_read_u32(target, FMC_OBUSER, &(gd32vw55x_info->option_bytes.user));

	if (gd32vw55x_ob_write(bank) != ERROR_OK) {
		command_print(CMD, "gd32vw55x failed to lock device");
		return ERROR_OK;
	}

	command_print(CMD, "gd32vw55x unlocked.\n"
			"INFO: a reset or power cycle is required "
			"for the new settings to take effect.");

	return ERROR_OK;
}

COMMAND_HANDLER(gd32vw55x_handle_ob_reload_command)
{
	struct target *target = NULL;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ERROR_OK != retval)
		return retval;
	if (gd32vw55x_ob_reload(bank) != ERROR_OK) {
		command_print(CMD, "gd32vw55x failed to reload option byte");
		return ERROR_OK;
	}
	command_print(CMD, "gd32vw55x option load completed. Power-on reset might be required");

	return ERROR_OK;
}

COMMAND_HANDLER(gd32vw55x_handle_ob_read_command)
{
	uint32_t fmc_obstat_reg, fmc_obr_reg, fmc_obuser_reg, fmc_obwrp0_reg, fmc_obwrp1_reg ;
	struct target *target = NULL;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ERROR_OK != retval)
		return retval;

	target = bank->target;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	retval = target_read_u32(target, FMC_OBSTAT, &fmc_obstat_reg);
	if (retval != ERROR_OK)
		return retval;
	command_print(CMD, "Option byte: 0x%" PRIx32 "", fmc_obstat_reg);

	retval = target_read_u32(target, FMC_OBR, &fmc_obr_reg);
	if (retval != ERROR_OK)
		return retval;
	command_print(CMD, "FMC_OBR is: 0x%" PRIx32 "", fmc_obr_reg);

	retval = target_read_u32(target, FMC_OBWRP0, &fmc_obwrp0_reg);
	if (retval != ERROR_OK)
		return retval;
	command_print(CMD, "FMC_OBWRP0 is: 0x%" PRIx32 "", fmc_obwrp0_reg);

	retval = target_read_u32(target, FMC_OBWRP1, &fmc_obwrp1_reg);
	if (retval != ERROR_OK)
		return retval;
	command_print(CMD, "FMC_OBWRP1 is: 0x%" PRIx32 "", fmc_obwrp1_reg);

	if ((fmc_obstat_reg >> 1) & 1)
		command_print(CMD, "Security protection on");
	else
		command_print(CMD, "No security protection");

	if ((fmc_obstat_reg >> 2) & 1)
		command_print(CMD, "erase\program protection on");
	else
		command_print(CMD, "No erase\program protection");
	return ERROR_OK;
}

COMMAND_HANDLER(gd32vw55x_handle_ob_write_command)
{
	struct target *target = NULL;
	struct gd32vw55x_flash_bank *gd32vw55x_info = NULL;
	uint32_t obwrp0, obwrp1, user;
	uint8_t spc, tzen;
	uint32_t value = 0;

	if (CMD_ARGC < 3)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ERROR_OK != retval)
		return retval;

	gd32vw55x_info = bank->driver_priv;

	target = bank->target;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	retval = gd32vw55x_ob_get(bank);
	if (ERROR_OK != retval)
		return retval;

	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[2], value);

	if (strcmp("USER", CMD_ARGV[1]) == 0) {
		user = value;
		gd32vw55x_info->option_bytes.user = user;
		LOG_INFO("user = value = %x", value);
	} else if (strcmp("SPC", CMD_ARGV[1]) == 0) {
		spc = value;
		gd32vw55x_info->option_bytes.spc = spc;
		LOG_INFO("secmcfg0 = value = %x", value);
	} else if (strcmp("WRP0", CMD_ARGV[1]) == 0) {
		obwrp0 = value;
		gd32vw55x_info->option_bytes.wrp[0] = value;
		LOG_INFO("WRP0 = value = %x", value);
	} else if (strcmp("WRP1", CMD_ARGV[1]) == 0) {
		obwrp1 = value;
		gd32vw55x_info->option_bytes.wrp[1] = obwrp1;
		LOG_INFO("WRP1 = value = %x", value);
	} else
		return ERROR_COMMAND_SYNTAX_ERROR;

	if (gd32vw55x_ob_write(bank) != ERROR_OK) {
		command_print(CMD, "gd32vw55x failed to write options");
		return ERROR_OK;
	}

	command_print(CMD, "gd32vw55x write options complete.\n"
				"INFO: a reset or power cycle is required "
				"for the new settings to take effect.");

	return ERROR_OK;
}

static int gd32vw55x_mass_erase(struct flash_bank *bank)
{
	struct target *target = bank->target;
	uint32_t fmc_ctl_reg;
	uint32_t retry = 100;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* unlock the main FMC operation */
	do {
		target_write_u32(target, FMC_KEY, UNLOCK_KEY0);
		target_write_u32(target, FMC_KEY, UNLOCK_KEY1);

		target_read_u32 (target, FMC_CTL, &fmc_ctl_reg);
		fmc_ctl_reg = fmc_ctl_reg & FMC_CTL_LK;
	} while ((retry--) && fmc_ctl_reg);

	if (retry == 0) {
		LOG_DEBUG("GD32: Flash mass erase ...timed out waiting for flash unlock");
		return ERROR_FAIL;
	}

	/* mass erase flash memory */
	target_write_u32(target, FMC_CTL, FMC_CTL_MER);
	target_write_u32(target, FMC_CTL, FMC_CTL_MER | FMC_CTL_START);

	int retval = gd32vw55x_ready_wait(bank, FMC_TIMEOUT_COUNT);
	if (retval != ERROR_OK)
		return retval;

	target_write_u32(target, FMC_CTL, FMC_CTL_LK);

	return ERROR_OK;
}

COMMAND_HANDLER(gd32vw55x_handle_mass_erase_command)
{
	unsigned int i;

	if (CMD_ARGC < 1)
		return ERROR_COMMAND_SYNTAX_ERROR;

	struct flash_bank *bank;
	int retval = CALL_COMMAND_HANDLER(flash_command_get_bank, 0, &bank);
	if (ERROR_OK != retval)
		return retval;

	retval = gd32vw55x_mass_erase(bank);
	if (retval == ERROR_OK) {
		/* set all sectors as erased */
		for (i = 0; i < bank->num_sectors; i++)
			bank->sectors[i].is_erased = 1;

		command_print(CMD, "gd32vw55x mass erase complete");
	} else
		command_print(CMD, "gd32vw55x mass erase failed");

	return retval;
}

static const struct command_registration gd32vw55x_exec_command_handlers[] = {
	{
		.name = "lock",
		.handler = gd32vw55x_handle_lock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Lock entire flash device.",
	},
	{
		.name = "unlock",
		.handler = gd32vw55x_handle_unlock_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Unlock entire protected flash device.",
	},
	{
	.name = "ob_reload",
	.handler = gd32vw55x_handle_ob_reload_command,
	.mode = COMMAND_EXEC,
	.usage = "bank_id",
	.help = "Force re-load of device options (will cause device reset).",
	},
	{
		.name = "mass_erase",
		.handler = gd32vw55x_handle_mass_erase_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Erase entire flash device.",
	},
	{
		.name = "ob_read",
		.handler = gd32vw55x_handle_ob_read_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id",
		.help = "Read and display device option byte.",
	},
	{
		.name = "ob_write",
		.handler = gd32vw55x_handle_ob_write_command,
		.mode = COMMAND_EXEC,
		.usage = "bank_id ('USER'|'SPC'|'WRP0'|'WRP1') value ",
		.help = "Replace bits in device option byte.",
	},
	COMMAND_REGISTRATION_DONE
};

static const struct command_registration gd32vw55x_command_handlers[] = {
	{
		.name = "gd32vw55x",
		.mode = COMMAND_ANY,
		.help = "gd32vw55x flash command group",
		.usage = "",
		.chain = gd32vw55x_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};

const struct flash_driver gd32vw55x_flash = {
	.name = "gd32vw55x",
	.commands = gd32vw55x_command_handlers,
	.flash_bank_command = gd32vw55x_flash_bank_command,
	.erase = gd32vw55x_erase,
	.protect = gd32vw55x_protect,
	.write = gd32vw55x_write,
	.read = default_flash_read,
	.probe = gd32vw55x_probe,
	.auto_probe = gd32vw55x_auto_probe,
	.erase_check = default_flash_blank_check,
	.protect_check = gd32vw55x_protect_check,
	.free_driver_priv = default_flash_free_driver_priv,
};
