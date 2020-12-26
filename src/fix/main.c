/*
 * This file is part of RGBDS.
 *
 * Copyright (c) 2020, Eldred habert and RGBDS contributors.
 *
 * SPDX-License-Identifier: MIT
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extern/getopt.h"

#include "helpers.h"
#include "platform.h"
#include "version.h"

#define UNSPECIFIED 0x100 // May not be in byte range

#define BANK_SIZE 0x4000

/* Short options */
static const char *optstring = "Ccf:i:jk:l:m:n:p:r:st:Vv";

/*
 * Equivalent long options
 * Please keep in the same order as short opts
 *
 * Also, make sure long opts don't create ambiguity:
 * A long opt's name should start with the same letter as its short opt,
 * except if it doesn't create any ambiguity (`verbose` versus `version`).
 * This is because long opt matching, even to a single char, is prioritized
 * over short opt matching
 */
static struct option const longopts[] = {
	{ "color-only",       no_argument,       NULL, 'C' },
	{ "color-compatible", no_argument,       NULL, 'c' },
	{ "fix-spec",         required_argument, NULL, 'f' },
	{ "game-id",          required_argument, NULL, 'i' },
	{ "non-japanese",     no_argument,       NULL, 'j' },
	{ "new-licensee",     required_argument, NULL, 'k' },
	{ "old-licensee",     required_argument, NULL, 'l' },
	{ "mbc-type",         required_argument, NULL, 'm' },
	{ "rom-version",      required_argument, NULL, 'n' },
	{ "pad-value",        required_argument, NULL, 'p' },
	{ "ram-size",         required_argument, NULL, 'r' },
	{ "sgb-compatible",   no_argument,       NULL, 's' },
	{ "title",            required_argument, NULL, 't' },
	{ "version",          no_argument,       NULL, 'V' },
	{ "validate",         no_argument,       NULL, 'v' },
	{ NULL,               no_argument,       NULL, 0   }
};

static void printUsage(void)
{
	fputs(
"Usage: rgbfix [-jsVv] [-C | -c] [-f <fix_spec>] [-i <game_id>] [-k <licensee>]\n"
"              [-l <licensee_byte>] [-m <mbc_type>] [-n <rom_version>]\n"
"              [-p <pad_value>] [-r <ram_size>] [-t <title_str>] [<file> ...]\n"
"Useful options:\n"
"    -m, --mbc-type <value>      set the MBC type byte to this value; refer\n"
"                                  to the man page for a list of values\n"
"    -p, --pad-value <value>     pad to the next valid size using this value\n"
"    -r, --ram-size <code>       set the cart RAM size byte to this value\n"
"    -V, --version               print RGBFIX version and exit\n"
"    -v, --validate              fix the header logo and both checksums (-f lhg)\n"
"\n"
"For help, use `man rgbfix' or go to https://rgbds.gbdev.io/docs/\n",
	      stderr);
}

enum MbcType {
	ROM  = 0x00,
	ROM_RAM = 0x08,
	ROM_RAM_BATTERY = 0x09,

	MBC1 = 0x01,
	MBC1_RAM = 0x02,
	MBC1_RAM_BATTERY = 0x03,

	MBC2 = 0x05,
	MBC2_BATTERY = 0x06,

	MMM01 = 0x0B,
	MMM01_RAM = 0x0C,
	MMM01_RAM_BATTERY = 0x0D,

	MBC3 = 0x11,
	MBC3_TIMER_BATTERY = 0x0F,
	MBC3_TIMER_RAM_BATTERY = 0x10,
	MBC3_RAM = 0x12,
	MBC3_RAM_BATTERY = 0x13,

	MBC5 = 0x19,
	MBC5_RAM = 0x1A,
	MBC5_RAM_BATTERY = 0x1B,
	MBC5_RUMBLE = 0x1C,
	MBC5_RUMBLE_RAM = 0x1D,
	MBC5_RUMBLE_RAM_BATTERY = 0x1E,

	MBC6 = 0x20,

	MBC7_SENSOR_RUMBLE_RAM_BATTERY = 0x22,

	POCKET_CAMERA = 0xFC,

	BANDAI_TAMA5 = 0xFD,

	HUC3 = 0xFE,

	HUC1_RAM_BATTERY = 0xFF,

	// Error values
	MBC_NONE = UNSPECIFIED, // No MBC specified, do not act on it
	MBC_BAD, // Specified MBC does not exist / syntax error
	MBC_WRONG_FEATURES, // MBC incompatible with specified features
	MBC_BAD_RANGE, // MBC number out of range
};

/**
 * @return False on failure
 */
static bool readMBCSlice(char const **name, char const *expected)
{
	while (*expected) {
		char c = *(*name)++;

		if (c == '\0') // Name too short
			return false;

		if (c >= 'a' && c <= 'z') // Perform the comparison case-insensitive
			c = c - 'a' + 'A';
		else if (c == '_') // Treat underscores as spaces
			c = ' ';

		if (c != *expected++)
			return false;
	}
	return true;
}

static enum MbcType parseMBC(char const *name)
{
	if (name[0] >= '0' && name[0] <= '9') {
		// Parse number, and return it as-is (unless it's too large)
		char *endptr;
		unsigned long mbc = strtoul(name, &endptr, 0);

		if (*endptr)
			return MBC_BAD;
		if (mbc > 0xFF)
			return MBC_BAD_RANGE;
		return mbc;

	} else {
		// Begin by reading the MBC type:
		uint16_t mbc;
		char const *ptr = name;

		// Trim off leading whitespace
		while (*ptr == ' ' || *ptr == '\t')
			ptr++;

#define tryReadSlice(expected) \
do { \
	if (!readMBCSlice(&ptr, expected)) \
		return MBC_BAD; \
} while (0)

		switch (*ptr++) {
		case 'R': // ROM / ROM_ONLY
		case 'r':
			tryReadSlice("OM");
			// Handle optional " ONLY"
			while (*ptr == ' ' || *ptr == '\t' || *ptr == '_')
				ptr++;
			if (*ptr == 'O' || *ptr == 'o') {
				ptr++;
				tryReadSlice("NLY");
			}
			mbc = ROM;
			break;

		case 'M': // MBC{1, 2, 3, 5, 6, 7} / MMM01
		case 'm':
			switch (*ptr++) {
			case 'B':
			case 'b':
				switch (*ptr++) {
				case 'C':
				case 'c':
					break;
				default:
					return MBC_BAD;
				}
				switch (*ptr++) {
				case '1':
					mbc = MBC1;
					break;
				case '2':
					mbc = MBC2;
					break;
				case '3':
					mbc = MBC3;
					break;
				case '5':
					mbc = MBC5;
					break;
				case '6':
					mbc = MBC6;
					break;
				case '7':
					mbc = MBC7_SENSOR_RUMBLE_RAM_BATTERY;
					break;
				default:
					return MBC_BAD;
				}
				break;
			case 'M':
			case 'm':
				tryReadSlice("M01");
				mbc = MMM01;
				break;
			default:
				return MBC_BAD;
			}
			break;

		case 'P': // POCKET_CAMERA
		case 'p':
			tryReadSlice("OCKET CAMERA");
			mbc = POCKET_CAMERA;
			break;

		case 'B': // BANDAI_TAMA5
		case 'b':
			tryReadSlice("ANDAI TAMA5");
			mbc = BANDAI_TAMA5;
			break;

		case 'T': // TAMA5
		case 't':
			tryReadSlice("AMA5");
			mbc = BANDAI_TAMA5;
			break;

		case 'H': // HuC{1, 3}
		case 'h':
			tryReadSlice("UC");
			switch (*ptr++) {
			case '1':
				mbc = HUC1_RAM_BATTERY;
				break;
			case '3':
				mbc = HUC3;
				break;
			default:
				return MBC_BAD;
			}
			break;

		default:
			return MBC_BAD;
		}

		// Read "additional features"
		uint8_t features = 0;
#define RAM 0x80
#define BATTERY 0x40
#define TIMER 0x20
#define RUMBLE 0x10
#define SENSOR 0x08

		for (;;) {
			// Trim off trailing whitespace
			while (*ptr == ' ' || *ptr == '\t' || *ptr == '_')
				ptr++;

			// If done, start processing "features"
			if (!*ptr)
				break;
			// We expect a '+' at this point
			if (*ptr++ != '+')
				return MBC_BAD;
			// Trim off leading whitespace
			while (*ptr == ' ' || *ptr == '\t' || *ptr == '_')
				ptr++;

			switch (*ptr++) {
			case 'B': // BATTERY
			case 'b':
				tryReadSlice("ATTERY");
				features |= BATTERY;
				break;

			case 'R': // RAM or RUMBLE
			case 'r':
				switch (*ptr++) {
				case 'U':
				case 'u':
					tryReadSlice("MBLE");
					features |= RUMBLE;
					break;
				case 'A':
				case 'a':
					if (*ptr != 'M' && *ptr != 'm')
						return MBC_BAD;
					ptr++;
					features |= RAM;
					break;
				default:
					return MBC_BAD;
				}
				break;

			case 'S': // SENSOR
			case 's':
				tryReadSlice("ENSOR");
				features |= SENSOR;
				break;

			case 'T': // TIMER
			case 't':
				tryReadSlice("IMER");
				features |= TIMER;
				break;

			default:
				return MBC_BAD;
			}
		}
#undef tryReadSlice

		switch (mbc) {
		case ROM:
			if (!features)
				break;
			mbc = ROM_RAM - 1;
			static_assert(ROM_RAM + 1 == ROM_RAM_BATTERY, "Enum sanity check failed!");
			static_assert(MBC1 + 1 == MBC1_RAM, "Enum sanity check failed!");
			static_assert(MBC1 + 2 == MBC1_RAM_BATTERY, "Enum sanity check failed!");
			static_assert(MMM01 + 1 == MMM01_RAM, "Enum sanity check failed!");
			static_assert(MMM01 + 2 == MMM01_RAM_BATTERY, "Enum sanity check failed!");
			// fallthrough
		case MBC1:
		case MMM01:
			if (features == RAM)
				mbc++;
			else if (features == (RAM | BATTERY))
				mbc += 2;
			else if (features)
				return MBC_WRONG_FEATURES;
			break;

		case MBC2:
			if (features == BATTERY)
				mbc = MBC2_BATTERY;
			else if (features)
				return MBC_WRONG_FEATURES;
			break;

		case MBC3:
			// Handle timer, which also requires battery
			if (features & (TIMER & BATTERY)) {
				features &= ~(TIMER | BATTERY); // Reset those bits
				mbc = MBC3_TIMER_BATTERY;
				// RAM is handled below
			}
			static_assert(MBC3 + 1 == MBC3_RAM, "Enum sanity check failed!");
			static_assert(MBC3 + 2 == MBC3_RAM_BATTERY, "Enum sanity check failed!");
			static_assert(MBC3_TIMER_BATTERY + 1 == MBC3_TIMER_RAM_BATTERY,
				      "Enum sanity check failed!");
			if (features == RAM)
				mbc++;
			else if (features == (RAM | BATTERY))
				mbc += 2;
			else if (features)
				return MBC_WRONG_FEATURES;
			break;

		case MBC5:
			if (features & RUMBLE) {
				features &= ~RUMBLE;
				mbc = MBC5_RUMBLE;
			}
			static_assert(MBC5 + 1 == MBC5_RAM, "Enum sanity check failed!");
			static_assert(MBC5 + 2 == MBC5_RAM_BATTERY, "Enum sanity check failed!");
			static_assert(MBC5_RUMBLE + 1 == MBC5_RUMBLE_RAM, "Enum sanity check failed!");
			static_assert(MBC5_RUMBLE + 2 == MBC5_RUMBLE_RAM_BATTERY,
				      "Enum sanity check failed!");
			if (features == RAM)
				mbc++;
			else if (features == (RAM | BATTERY))
				mbc += 2;
			else if (features)
				return MBC_WRONG_FEATURES;
			break;

		case MBC6:
		case POCKET_CAMERA:
		case BANDAI_TAMA5:
		case HUC3:
			// No extra features accepted
			if (features)
				return MBC_WRONG_FEATURES;
			break;

		case MBC7_SENSOR_RUMBLE_RAM_BATTERY:
			if (features != (SENSOR | RUMBLE | RAM | BATTERY))
				return MBC_WRONG_FEATURES;
			break;

		case HUC1_RAM_BATTERY:
			if (features != (RAM | BATTERY)) // HuC1 expects RAM+BATTERY
				return MBC_WRONG_FEATURES;
			break;
		}

		// Trim off trailing whitespace
		while (*ptr == ' ' || *ptr == '\t')
			ptr++;

		// If there is still something past the whitespace, error out
		if (*ptr)
			return MBC_BAD;

		return mbc;
	}
}

static char const *mbcName(enum MbcType type)
{
	switch (type) {
	case ROM:
		return "ROM";
	case ROM_RAM:
		return "ROM+RAM";
	case ROM_RAM_BATTERY:
		return "ROM+RAM+BATTERY";
	case MBC1:
		return "MBC1";
	case MBC1_RAM:
		return "MBC1+RAM";
	case MBC1_RAM_BATTERY:
		return "MBC1+RAM+BATTERY";
	case MBC2:
		return "MBC2";
	case MBC2_BATTERY:
		return "MBC2+BATTERY";
	case MMM01:
		return "MMM01";
	case MMM01_RAM:
		return "MMM01+RAM";
	case MMM01_RAM_BATTERY:
		return "MMM01+RAM+BATTERY";
	case MBC3:
		return "MBC3";
	case MBC3_TIMER_BATTERY:
		return "MBC3+TIMER+BATTERY";
	case MBC3_TIMER_RAM_BATTERY:
		return "MBC3+TIMER+RAM+BATTERY";
	case MBC3_RAM:
		return "MBC3+RAM";
	case MBC3_RAM_BATTERY:
		return "MBC3+RAM+BATTERY";
	case MBC5:
		return "MBC5";
	case MBC5_RAM:
		return "MBC5+RAM";
	case MBC5_RAM_BATTERY:
		return "MBC5+RAM+BATTERY";
	case MBC5_RUMBLE:
		return "MBC5+RUMBLE";
	case MBC5_RUMBLE_RAM:
		return "MBC5+RUMBLE+RAM";
	case MBC5_RUMBLE_RAM_BATTERY:
		return "MBC5+RUMBLE+RAM+BATTERY";
	case MBC6:
		return "MBC6";
	case MBC7_SENSOR_RUMBLE_RAM_BATTERY:
		return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
	case POCKET_CAMERA:
		return "POCKET CAMERA";
	case BANDAI_TAMA5:
		return "BANDAI TAMA5";
	case HUC3:
		return "HUC3";
	case HUC1_RAM_BATTERY:
		return "HUC1+RAM+BATTERY";

	// Error values
	case MBC_NONE:
	case MBC_BAD:
	case MBC_WRONG_FEATURES:
	case MBC_BAD_RANGE:
		unreachable_();
	}

	unreachable_();
}

static bool hasRAM(enum MbcType type)
{
	switch (type) {
	case ROM:
	case MBC1:
	case MBC2: // Technically has RAM, but not marked as such
	case MBC2_BATTERY:
	case MMM01:
	case MBC3:
	case MBC3_TIMER_BATTERY:
	case MBC5:
	case MBC5_RUMBLE:
	case MBC6: // TODO: not sure
	case BANDAI_TAMA5: // TODO: not sure
	case MBC_NONE:
	case MBC_BAD:
	case MBC_WRONG_FEATURES:
	case MBC_BAD_RANGE:
		return false;

	case ROM_RAM:
	case ROM_RAM_BATTERY:
	case MBC1_RAM:
	case MBC1_RAM_BATTERY:
	case MMM01_RAM:
	case MMM01_RAM_BATTERY:
	case MBC3_TIMER_RAM_BATTERY:
	case MBC3_RAM:
	case MBC3_RAM_BATTERY:
	case MBC5_RAM:
	case MBC5_RAM_BATTERY:
	case MBC5_RUMBLE_RAM:
	case MBC5_RUMBLE_RAM_BATTERY:
	case MBC7_SENSOR_RUMBLE_RAM_BATTERY:
	case POCKET_CAMERA:
	case HUC3:
	case HUC1_RAM_BATTERY:
		return true;
	}

	unreachable_();
}

static uint8_t nbErrors;

static format_(printf, 1, 2) void report(char const *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (nbErrors != UINT8_MAX)
		nbErrors++;
}

static const uint8_t ninLogo[] = {
	0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
	0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
	0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
	0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
	0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
	0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
};

static enum { DMG, BOTH, CGB } model = DMG; // If DMG, byte is left alone
#define   FIX_LOGO        0x80
#define TRASH_LOGO        0x40
#define   FIX_HEADER_SUM  0x20
#define TRASH_HEADER_SUM  0x10
#define   FIX_GLOBAL_SUM  0x08
#define TRASH_GLOBAL_SUM  0x04
static uint8_t fixSpec = 0;
static const char *gameID = NULL;
static uint8_t gameIDLen;
static bool japanese = true;
static const char *newLicensee = NULL;
static uint8_t newLicenseeLen;
static uint16_t oldLicensee = UNSPECIFIED;
static enum MbcType cartridgeType = MBC_NONE;
static uint16_t romVersion = UNSPECIFIED;
static uint16_t padValue = UNSPECIFIED;
static uint16_t ramSize = UNSPECIFIED;
static bool sgb = false; // If false, SGB flags are left alone
static const char *title = NULL;
static uint8_t titleLen;

static uint8_t maxTitleLen(void)
{
	return gameID ? 11 :  model != DMG ? 15 : 16;
}

static ssize_t readBytes(int fd, uint8_t *buf, size_t len)
{
	// POSIX specifies that lengths greater than SSIZE_MAX yield implementation-defined results
	assert(len <= SSIZE_MAX);

	ssize_t total = 0;

	while (len) {
		ssize_t ret = read(fd, buf, len);

		if (ret == -1 && errno != EINTR) // Return errors, unless we only were interrupted
			return -1;
		// EOF reached
		if (ret == 0)
			return total;
		// If anything was read, accumulate it, and continue
		if (ret != -1) {
			total += ret;
			len -= ret;
			buf += ret;
		}
	}

	return total;
}

static ssize_t writeBytes(int fd, void *buf, size_t len)
{
	// POSIX specifies that lengths greater than SSIZE_MAX yield implementation-defined results
	assert(len <= SSIZE_MAX);

	ssize_t total = 0;

	while (len) {
		ssize_t ret = write(fd, buf, len);

		if (ret == -1 && errno != EINTR) // Return errors, unless we only were interrupted
			return -1;
		// EOF reached
		if (ret == 0)
			return total;
		// If anything was read, accumulate it, and continue
		if (ret != -1) {
			total += ret;
			len -= ret;
		}
	}

	return total;
}

/**
 * @param input File descriptor to be used for reading
 * @param output File descriptor to be used for writing, may be equal to `input`
 * @param name The file's name, to be displayed for error output
 * @param fileSize The file's size if known, 0 if not.
 */
static void processFile(int input, int output, char const *name, off_t fileSize)
{
	// Both of these should be true for seekable files, and neither otherwise
	if (input == output)
		assert(fileSize != 0);
	else
		assert(fileSize == 0);

	uint8_t rom0[BANK_SIZE];
	ssize_t rom0Len = readBytes(input, rom0, sizeof(rom0));

	if (rom0Len == -1) {
		report("FATAL: Failed to read \"%s\"'s header: %s\n", name, strerror(errno));
		return;
	} else if (rom0Len < 0x150) {
		report("FATAL: \"%s\" too short, expected at least 336 ($150) bytes, got only %ld\n",
		       name, rom0Len);
		return;
	}
	// Accept partial reads if the file contains at least the header

	if (fixSpec & (FIX_LOGO | TRASH_LOGO)) {
		if (fixSpec & FIX_LOGO) {
			memcpy(&rom0[0x104], ninLogo, sizeof(ninLogo));
		} else {
			for (uint8_t i = 0; i < sizeof(ninLogo); i++)
				rom0[i + 0x104] = ~ninLogo[i];
		}
	}

	if (title)
		memcpy(&rom0[0x134], title, titleLen);

	if (gameID)
		memcpy(&rom0[0x13f], gameID, gameIDLen);

	if (model != DMG)
		rom0[0x143] = model == BOTH ? 0x80 : 0xc0;

	if (newLicensee)
		memcpy(&rom0[0x144], newLicensee, newLicenseeLen);

	if (sgb)
		rom0[0x146] = 0x03;

	// If a valid MBC was specified...
	if (cartridgeType < MBC_NONE)
		rom0[0x147] = cartridgeType;

	if (ramSize != UNSPECIFIED)
		rom0[0x149] = ramSize;

	if (!japanese)
		rom0[0x14a] = 0x01;

	if (oldLicensee != UNSPECIFIED)
		rom0[0x14b] = oldLicensee;

	if (romVersion != UNSPECIFIED)
		rom0[0x14c] = romVersion;

	// Remain to be handled the ROM size, and header checksum.
	// The latter depends on the former, and so will be handled after it.
	// The former requires knowledge of the file's total size, so read that first.

	uint16_t globalSum = 0;

	// To keep file sizes fairly reasonable, we'll cap the amount of banks at 65536
	// Official mappers only go up to 512 banks, but at least the TPP1 spec allows up to
	// 65536 banks = 1 GiB.
	// This should be reasonable for the time being, and may be extended later.
	uint8_t *romx = NULL; // Pointer to ROMX banks when they've been buffered
	uint32_t nbBanks = 1; // Number of banks *targeted*, including ROM0
	size_t totalRomxLen = 0; // *Actual* size of ROMX data
	uint8_t bank[BANK_SIZE]; // Temp buffer used to store a whole bank's worth of data

	// Handle ROMX
	if (input == output) {
		if (fileSize >= 0x10000 * BANK_SIZE) {
			report("FATAL: \"%s\" has more than 65536 banks\n", name);
			return;
		}
		// This should be guaranteed from the size cap...
		static_assert(0x10000 * BANK_SIZE <= SSIZE_MAX, "Max input file size too large for OS");
		// Compute number of banks and ROMX len from file size
		nbBanks = (fileSize + (BANK_SIZE - 1)) / BANK_SIZE;
		//      = ceil(totalRomxLen / BANK_SIZE)
		totalRomxLen = fileSize >= BANK_SIZE ? fileSize - BANK_SIZE : 0;
	} else if (rom0Len == BANK_SIZE) {
		// Copy ROMX when reading a pipe, and we're not at EOF yet
		for (;;) {
			romx = realloc(romx, nbBanks * BANK_SIZE);
			if (!romx) {
				report("FATAL: Failed to realloc ROMX buffer: %s\n",
				       strerror(errno));
				return;
			}
			ssize_t bankLen = readBytes(input, &romx[(nbBanks - 1) * BANK_SIZE],
						    BANK_SIZE);

			// Update bank count, ONLY IF at least one byte was read
			if (bankLen) {
				// We're gonna read another bank, check that it won't be too much
				static_assert(0x10000 * BANK_SIZE <= SSIZE_MAX, "Max input file size too large for OS");
				if (nbBanks == 0x10000) {
					report("FATAL: \"%s\" has more than 65536 banks\n", name);
					free(romx);
					return;
				}
				nbBanks++;

				// Update global checksum, too
				for (uint16_t i = 0; i < bankLen; i++)
					globalSum += romx[totalRomxLen + i];
				totalRomxLen += bankLen;
			}
			// Stop when an incomplete bank has been read
			if (bankLen != BANK_SIZE)
				break;
		}
	}

	// Handle setting the ROM size if padding was requested
	// Pad to the next valid power of 2. This is because padding is required by flashers, which
	// flash to ROM chips, whose size is always a power of 2... so there'd be no point in
	// padding to something else.
	// Additionally, a ROM must be at least 32k, so we guarantee a whole amount of banks...
	if (padValue != UNSPECIFIED) {
		// We want at least 2 banks
		if (nbBanks == 1) {
			if (rom0Len != sizeof(rom0)) {
				memset(&rom0[rom0Len], padValue, sizeof(rom0) - rom0Len);
				// The global checksum hasn't taken ROM0 into consideration yet!
				// ROM0 was padded, so treat it as entirely written: update its size
				// Update how many bytes were read in total, too
				rom0Len = sizeof(rom0);
			}
			nbBanks = 2;
		} else {
			assert(rom0Len == sizeof(rom0));
		}
		assert(nbBanks >= 2);
		// Alter number of banks to reflect required value
		// x&(x-1) is zero iff x is a power of 2, or 0; we know for sure it's non-zero,
		// so this is true (non-zero) when we don't have a power of 2
		if (nbBanks & (nbBanks - 1))
			nbBanks = 1 << (CHAR_BIT * sizeof(nbBanks) - clz(nbBanks));
		// Write final ROM size
		rom0[0x148] = ctz(nbBanks / 2);
		// Alter global checksum based on how many bytes will be added (not counting ROM0)
		globalSum += padValue * ((nbBanks - 1) * BANK_SIZE - totalRomxLen);
	}

	// Handle the header checksum after the ROM size has been written
	if (fixSpec & (FIX_HEADER_SUM | TRASH_HEADER_SUM)) {
		uint8_t sum = 0;

		for (uint16_t i = 0x134; i < 0x14d; i++)
			sum -= rom0[i] + 1;
		rom0[0x14d] = fixSpec & TRASH_HEADER_SUM ? ~sum : sum;
	}

	if (fixSpec & (FIX_GLOBAL_SUM | TRASH_GLOBAL_SUM)) {
		// Computation of the global checksum assumes 0s being stored in its place
		rom0[0x14e] = 0;
		rom0[0x14f] = 0;
		for (uint16_t i = 0; i < rom0Len; i++)
			globalSum += rom0[i];
		// Pipes have already read ROMX and updated globalSum, but not regular files
		if (input == output) {
			for (;;) {
				ssize_t romxLen = readBytes(input, bank, sizeof(bank));

				for (uint16_t i = 0; i < romxLen; i++)
					globalSum += bank[i];
				if (romxLen != sizeof(bank))
					break;
			}
		}

		if (fixSpec & TRASH_GLOBAL_SUM)
			globalSum = ~globalSum;
		rom0[0x14e] = globalSum >> 8;
		rom0[0x14f] = globalSum & 0xff;
	}

	// In case the output depends on the input, reset to the beginning of the file, and only
	// write the header
	if (input == output) {
		if (lseek(output, 0, SEEK_SET) == (off_t)-1) {
			report("FATAL: Failed to rewind \"%s\": %s\n", name, strerror(errno));
			goto free_romx;
		}
		// If modifying the file in-place, we only need to edit the header
		// However, padding may have modified ROM0 (added padding), so don't in that case
		if (padValue == UNSPECIFIED)
			rom0Len = 0x150;
	}
	ssize_t writeLen = writeBytes(output, rom0, rom0Len);

	if (writeLen == -1) {
		report("FATAL: Failed to write \"%s\"'s ROM0: %s\n", name, strerror(errno));
		goto free_romx;
	} else if (writeLen < rom0Len) {
		report("FATAL: Could only write %ld of \"%s\"'s %ld ROM0 bytes\n",
		       writeLen, name, rom0Len);
		goto free_romx;
	}

	// Output ROMX if it was buffered
	if (romx) {
		writeLen = writeBytes(output, romx, totalRomxLen);
		if (writeLen == -1) {
			report("FATAL: Failed to write \"%s\"'s ROMX: %s\n", name, strerror(errno));
			goto free_romx;
		} else if (writeLen < totalRomxLen) {
			report("FATAL: Could only write %ld of \"%s\"'s %ld ROMX bytes\n",
			       writeLen, name, totalRomxLen);
			goto free_romx;
		}
	}

	// Output padding
	if (padValue != UNSPECIFIED) {
		if (input == output) {
			if (lseek(output, 0, SEEK_END) == (off_t)-1) {
				report("FATAL: Failed to seek to end of \"%s\": %s\n",
				       name, strerror(errno));
				goto free_romx;
			}
		}
		memset(bank, padValue, sizeof(bank));
		size_t len = (nbBanks - 1) * BANK_SIZE - totalRomxLen; // Don't count ROM0!

		while (len) {
			static_assert(sizeof(bank) <= SSIZE_MAX, "Bank too large for reading");
			size_t thisLen = len > sizeof(bank) ? sizeof(bank) : len;
			ssize_t ret = writeBytes(output, bank, thisLen);

			if (ret != thisLen) {
				report("FATAL: Failed to write \"%s\"'s padding: %s\n",
				       name, strerror(errno));
				break;
			}
			len -= thisLen;
		}
	}

free_romx:
	free(romx);
}

#undef trySeek

static bool processFilename(char const *name)
{
	nbErrors = 0;
	if (!strcmp(name, "-")) {
		setmode(STDIN_FILENO, O_BINARY);
		setmode(STDOUT_FILENO, O_BINARY);
		name = "<stdin>";
		processFile(STDIN_FILENO, STDOUT_FILENO, name, 0);

	} else {
		// POSIX specifies that the results of O_RDWR on a FIFO are undefined.
		// However, this is necessary to avoid a TOCTTOU, if the file was changed between
		// `stat()` and `open(O_RDWR)`, which could trigger the UB anyway.
		// Thus, we're going to hope that either the `open` fails, or it succeeds but IO
		// operations may fail, all of which we handle.
		int input = open(name, O_RDWR | O_BINARY);
		struct stat stat;

		if (input == -1) {
			report("FATAL: Failed to open \"%s\" for reading+writing: %s\n",
				name, strerror(errno));
			goto fail;
		}

		if (fstat(input, &stat) == -1) {
			report("FATAL: Failed to stat \"%s\": %s\n", name, strerror(errno));
		} else if (!S_ISREG(stat.st_mode)) { // FIXME: Do we want to support other types?
			report("FATAL: \"%s\" is not a regular file, and thus cannot be modified in-place\n",
				name);
		} else if (stat.st_size < 0x150) {
			// This check is in theory redundant with the one in `processFile`, but it
			// prevents passing a file size of 0, which usually indicates pipes
			report("FATAL: \"%s\" too short, expected at least 336 ($150) bytes, got only %ld\n",
			       name, stat.st_size);
		} else {
			processFile(input, input, name, stat.st_size);
		}

		close(input);
	}
	if (nbErrors)
fail:
		fprintf(stderr, "Fixing \"%s\" failed with %u error%s\n",
			name, nbErrors, nbErrors == 1 ? "" : "s");
	return nbErrors;
}

int main(int argc, char *argv[])
{
	nbErrors = 0;
	char ch;

	while ((ch = musl_getopt_long_only(argc, argv, optstring, longopts, NULL)) != -1) {
		switch (ch) {
			size_t len;
#define parseByte(output, name) \
do { \
	char *endptr; \
	unsigned long tmp; \
	\
	if (optarg[0] == 0) { \
		report("error: Argument to option '" name "' may not be empty\n"); \
	} else { \
		if (optarg[0] == '$') { \
			tmp = strtoul(&optarg[1], &endptr, 16); \
		} else { \
			tmp = strtoul(optarg, &endptr, 0); \
		} \
		if (*endptr) \
			report("error: Expected number as argument to option '" name "', got %s\n", \
			       optarg); \
		else if (tmp > 0xFF) \
			report("error: Argument to option '" name "' is larger than 255: %lu\n", tmp); \
		else \
			output = tmp; \
	} \
} while (0)

		case 'C':
		case 'c':
			model = ch == 'c' ? BOTH : CGB;
			if (titleLen > 15) {
				titleLen = 15;
				fprintf(stderr, "warning: Truncating title \"%s\" to 15 chars\n",
					title);
			}
			break;

		case 'f':
			fixSpec = 0;
			while (*optarg) {
				switch (*optarg) {
#define SPEC_l FIX_LOGO
#define SPEC_L TRASH_LOGO
#define SPEC_h FIX_HEADER_SUM
#define SPEC_H TRASH_HEADER_SUM
#define SPEC_g FIX_GLOBAL_SUM
#define SPEC_G TRASH_GLOBAL_SUM
#define or(new, bad) \
do { \
	if (fixSpec & SPEC_##bad) \
		fprintf(stderr, \
			"warning: '" #new "' overriding '" #bad "' in fix spec\n"); \
	fixSpec = (fixSpec & ~SPEC_##bad) | SPEC_##new; \
} while (0)
				case 'l':
					or(l, L);
					break;
				case 'L':
					or(L, l);
					break;

				case 'h':
					or(h, H);
					break;
				case 'H':
					or(H, h);
					break;

				case 'g':
					or(g, G);
					break;
				case 'G':
					or(G, g);
					break;

				default:
					fprintf(stderr, "warning: Ignoring '%c' in fix spec\n",
						*optarg);
#undef or
				}
				optarg++;
			}
			break;

		case 'i':
			gameID = optarg;
			len = strlen(gameID);
			if (len > 4) {
				len = 4;
				fprintf(stderr, "warning: Truncating game ID \"%s\" to 4 chars\n",
					gameID);
			}
			gameIDLen = len;
			if (titleLen > 11) {
				titleLen = 11;
				fprintf(stderr, "warning: Truncating title \"%s\" to 11 chars\n",
					title);
			}
			break;

		case 'j':
			japanese = false;
			break;

		case 'k':
			newLicensee = optarg;
			len = strlen(newLicensee);
			if (len > 2) {
				len = 2;
				fprintf(stderr,
					"warning: Truncating new licensee \"%s\" to 2 chars\n",
					newLicensee);
			}
			newLicenseeLen = len;
			break;

		case 'l':
			parseByte(oldLicensee, "l");
			break;

		case 'm':
			cartridgeType = parseMBC(optarg);
			if (cartridgeType == MBC_BAD) {
				report("error: Unknown MBC \"%s\"\n", optarg);
			} else if (cartridgeType == MBC_WRONG_FEATURES) {
				report("error: Features incompatible with MBC (\"%s\")\n", optarg);
			} else if (cartridgeType == MBC_BAD_RANGE) {
				report("error: Specified MBC ID out of range 0-255: %s\n",
				       optarg);
			} else if (cartridgeType == ROM_RAM || cartridgeType == ROM_RAM_BATTERY) {
				fprintf(stderr, "warning: ROM+RAM / ROM+RAM+BATTERY are under-specified and poorly supported\n");
			}
			break;

		case 'n':
			parseByte(romVersion, "n");
			break;

		case 'p':
			parseByte(padValue, "p");
			break;

		case 'r':
			parseByte(ramSize, "r");
			break;

		case 's':
			sgb = true;
			break;

		case 't':
			title = optarg;
			len = strlen(title);
			uint8_t maxLen = maxTitleLen();

			if (len > maxLen) {
				len = maxLen;
				fprintf(stderr, "warning: Truncating title \"%s\" to %u chars\n",
					title, maxLen);
			}
			titleLen = len;
			break;

		case 'V':
			printf("rgbfix %s\n", get_package_version_string());
			exit(0);

		case 'v':
			fixSpec = FIX_LOGO | FIX_HEADER_SUM | FIX_GLOBAL_SUM;
			break;

		default:
			fprintf(stderr, "FATAL: unknown option '%c'\n", ch);
			printUsage();
			exit(1);
		}
#undef parseByte
	}

	if (ramSize != UNSPECIFIED && cartridgeType < UNSPECIFIED) {
		if (cartridgeType == ROM_RAM || cartridgeType == ROM_RAM_BATTERY) {
			if (ramSize != 1)
				fprintf(stderr, "warning: MBC \"%s\" should have 2kiB of RAM (-r 1)\n",
					mbcName(cartridgeType));
		} else if (hasRAM(cartridgeType)) {
			if (!ramSize) {
				fprintf(stderr,
					"warning: MBC \"%s\" has RAM, but RAM size was set to 0\n",
					mbcName(cartridgeType));
			} else if (ramSize == 1) {
				fprintf(stderr,
					"warning: RAM size 1 (2 kiB) was specified for MBC \"%s\"\n",
					mbcName(cartridgeType));
			} // TODO: check possible values?
		} else if (ramSize) {
			fprintf(stderr,
				"warning: MBC \"%s\" has no RAM, but RAM size was set to %u\n",
				mbcName(cartridgeType), ramSize);
		}
	}

	if (sgb && oldLicensee != UNSPECIFIED && oldLicensee != 0x33)
		fprintf(stderr,
			"warning: SGB compatibility enabled, but old licensee is %#x, not 0x33\n",
			oldLicensee);

	argv += optind;
	bool failed = nbErrors;

	if (!*argv) {
		failed |= processFilename("-");
	} else {
		do {
			failed |= processFilename(*argv);
		} while (*++argv);
	}

	return failed;
}
