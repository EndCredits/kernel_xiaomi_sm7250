#include <linux/moduleparam.h>
#include <linux/fs.h>

#include "apk_sign.h"
#include "klog.h"

static __always_inline int check_v2_signature(char *path, unsigned expected_size,
				  unsigned expected_hash)
{
	unsigned char buffer[0x11] = { 0 };
	u32 size4;
	u64 size8, size_of_block;

	loff_t pos;

	int sign = -1;
	struct file *fp = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		pr_err("open %s error.", path);
		return PTR_ERR(fp);
	}

	sign = 1;
	// https://en.wikipedia.org/wiki/Zip_(file_format)#End_of_central_directory_record_(EOCD)
	for (int i = 0;; ++i) {
		unsigned short n;
		pos = generic_file_llseek(fp, -i - 2, SEEK_END);
		kernel_read(fp, &n, 2, &pos);
		if (n == i) {
			pos -= 22;
			kernel_read(fp, &size4, 4, &pos);
			if ((size4 ^ 0xcafebabeu) == 0xccfbf1eeu) {
				break;
			}
		}
		if (i == 0xffff) {
			pr_info("error: cannot find eocd\n");
			goto clean;
		}
	}

	pos += 12;
	// offset
	kernel_read(fp, &size4, 0x4, &pos);
	pos = size4 - 0x18;

	kernel_read(fp, &size8, 0x8, &pos);
	kernel_read(fp, buffer, 0x10, &pos);
	if (strcmp((char *)buffer, "APK Sig Block 42")) {
		goto clean;
	}

	pos = size4 - (size8 + 0x8);
	kernel_read(fp, &size_of_block, 0x8, &pos);
	if (size_of_block != size8) {
		goto clean;
	}

	for (;;) {
		uint32_t id;
		uint32_t offset;
		kernel_read(fp, &size8, 0x8, &pos); // sequence length
		if (size8 == size_of_block) {
			break;
		}
		kernel_read(fp, &id, 0x4, &pos); // id
		offset = 4;
		pr_info("id: 0x%08x\n", id);
		if ((id ^ 0xdeadbeefu) == 0xafa439f5u ||
			(id ^ 0xdeadbeefu) == 0x2efed62f) {
			kernel_read(fp, &size4, 0x4,
					&pos); // signer-sequence length
			kernel_read(fp, &size4, 0x4, &pos); // signer length
			kernel_read(fp, &size4, 0x4,
					&pos); // signed data length
			offset += 0x4 * 3;

			kernel_read(fp, &size4, 0x4,
					&pos); // digests-sequence length
			pos += size4;
			offset += 0x4 + size4;

			kernel_read(fp, &size4, 0x4,
					&pos); // certificates length
			kernel_read(fp, &size4, 0x4,
					&pos); // certificate length
			offset += 0x4 * 2;
#if 0
			int hash = 1;
			signed char c;
			for (unsigned i = 0; i < size4; ++i) {
				kernel_read(fp, &c, 0x1, &pos);
				hash = 31 * hash + c;
			}
			offset += size4;
			pr_info("    size: 0x%04x, hash: 0x%08x\n", size4, ((unsigned) hash) ^ 0x14131211u);
#else
			if (size4 == expected_size) {
				int hash = 1;
				signed char c;
				for (unsigned i = 0; i < size4; ++i) {
					kernel_read(fp, &c, 0x1, &pos);
					hash = 31 * hash + c;
				}
				offset += size4;
				if ((((unsigned)hash) ^ 0x14131211u) ==
					expected_hash) {
					sign = 0;
					break;
				}
			}
			// don't try again.
			break;
#endif
		}
		pos += (size8 - offset);
	}

clean:
	filp_close(fp, 0);

	return sign;
}

#ifdef CONFIG_KSU_DEBUG

unsigned ksu_expected_size = EXPECTED_SIZE;
unsigned ksu_expected_hash = EXPECTED_HASH;

module_param(ksu_expected_size, uint, S_IRUSR | S_IWUSR);
module_param(ksu_expected_hash, uint, S_IRUSR | S_IWUSR);

int is_manager_apk(char *path)
{
	return check_v2_signature(path, ksu_expected_size, ksu_expected_hash);
}

#else


int is_manager_apk(char *path)
{
	return check_v2_signature(path, EXPECTED_SIZE, EXPECTED_HASH);
}

#endif