#ifndef MY_ABC_HERE
#define MY_ABC_HERE
#endif
/* Copyright (c) 2000-2020 Synology Inc. All rights reserved. */

#ifdef MY_DEF_HERE

#include <linux/efi.h>
#include <linux/printk.h>
#include <linux/synolib.h>
#include <asm/bitops.h>
#include <asm/e820/types.h>
#include <asm/io.h>
#include <asm/setup.h>

/*
 * Kexec test result.
 *
 * See also include/lib/synolib.h
 */
unsigned long kexec_test_flags;

/*
 * Test whether boot protocol is newer than v2.08.
 *
 * According to Documentation/x86/boot.txt, setup_data field is added in
 * version v2.09.
 */
static bool test_boot_protocol_version(void)
{
	return 0x0209 <= boot_params.hdr.version;
}

/*
 * Test whether we skip compressed/head_64.S.
 *
 * On normal boot, arch/x86/boot/compressed/head_64.S will decompress the
 * kernel and jump to entry point.
 *
 * However, the above-mentioned operation will be skipped when the kernel is
 * booted with Kexec.
 *
 * Therefore, we insert a setup_data with type __DECOMPRESSION_TYPE__ in
 * arch/x86/boot/compressed/head_64.S and check whether it exists here.
 */
static void test_decompression(void)
{
	bool passed = false;
	struct setup_data *data;
	u64 pa_data;

	if (!test_boot_protocol_version()) {
		goto END;
	}

	pa_data = boot_params.hdr.setup_data;
	while (pa_data && !passed) {
		data = early_memremap(pa_data, sizeof(*data));
		passed = (__DECOMPRESSION_TYPE__ == data->type);
		pa_data = data->next;
		early_memunmap(data, sizeof(*data));
	}

END:
	if (!passed) {
		set_bit(KEXEC_TEST_DECOMPRESSION, &kexec_test_flags);
	}
}

/*
 * Test whether bootloader type is kexec-tools.
 *
 * According to Documentation/x86/boot.txt, the value 0xD indicates that the
 * bootloader is kexec-tools.
 */
static void test_bootloader(void)
{
	if (0xD == (boot_params.hdr.type_of_loader >> 4)) {
		set_bit(KEXEC_TEST_BOOTLOADER, &kexec_test_flags);
	}
}

/*
 * Test whether the minimal start address of usable memory in e820 table is
 * 0x100.
 *
 * According to kexec-tools source code, it will not report the memory before
 * address 0x100 as a usable memory.
 */
static void test_e820_table(void)
{
	bool passed = true;
	u32 i;

	for (i = 0; i < boot_params.e820_entries; ++i) {
		if (E820_TYPE_RAM == boot_params.e820_table[i].type &&
				0x100 == boot_params.e820_table[i].addr) {
			passed = false;
			break;
		}
	}

	if (!passed) {
		set_bit(KEXEC_TEST_E820_TABLE, &kexec_test_flags);
	}
}

/*
 * Test whether we receive setup_data with type SETUP_NONE or SETUP_EFI.
 *
 * To support EFI runtime services, Kexec-ed kernel receives related
 * addresses via setup_data with type SETUP_EFI, which are sent by first
 * kernel.
 *
 * However, after observation, it seems that the kexec-tools shipped in
 * XPEnology does not fill in type field or all fields.
 *
 * Therefore, we also regard SETUP_NONE as abnormal type because the type
 * should not be SETUP_NONE in normal case.
 */
static void test_setup_data(void)
{
	bool passed = true;
	struct setup_data *data;
	u64 pa_data;

	if (!test_boot_protocol_version()) {
		passed = false;
		goto END;
	}

	pa_data = boot_params.hdr.setup_data;
	while (pa_data && passed) {
		data = early_memremap(pa_data, sizeof(*data));
		passed = (SETUP_NONE != data->type && SETUP_EFI != data->type);
		pa_data = data->next;
		early_memunmap(data, sizeof(*data));
	}

END:
	if (!passed) {
		set_bit(KEXEC_TEST_SETUP_DATA, &kexec_test_flags);
	}
}

/*
 * Remove setup_data inserted by us.
 *
 * In order to hide our setup_data successfully, this function MUST be called
 * before boot_params_ksysfs_init(); otherwise, our setup_data can be accessed
 * via sysfs in /sys/kernel/boot_params/setup_data.
 *
 * The function boot_params_ksysfs_init() is located at
 * arch/x86/kernel/ksysfs.c, and it will be called at arch-level initcall.
 */
static void remove_decompression_setup_data(void)
{
	bool found = false;
	struct setup_data *data;
	u64 pa_data, pa_prev, pa_next;

	if (!test_boot_protocol_version()) {
		goto END;
	}

	pa_data = boot_params.hdr.setup_data;
	while (pa_data) {
		data = early_memremap(pa_data, sizeof(*data));
		if (__DECOMPRESSION_TYPE__ == data->type) {
			found = true;
			data->type = 0;
		}
		pa_next = data->next;
		early_memunmap(data, sizeof(*data));

		if (found) {
			break;
		}

		pa_prev = pa_data;
		pa_data = pa_next;
	}

	if (!pa_data) {
		goto END;
	}

	if (pa_data == boot_params.hdr.setup_data) {
		boot_params.hdr.setup_data = pa_next;
	} else {
		data = early_memremap(pa_prev, sizeof(*data));
		data->next = pa_next;
		early_memunmap(data, sizeof(*data));
	}

END:
	return;
}

/*
 * Perform all Kexec detections and set the corresponding bit at test fail.
 */
void __init syno_kexec_test_init(void)
{
	test_decompression();
	test_bootloader();
	test_e820_table();
	test_setup_data();

	remove_decompression_setup_data();
}

#endif /* MY_DEF_HERE */
