#ifndef _CSD_LEVELDB_FUNC_H
#define _CSD_LEVELDB_FUNC_H

#include "csd_user_func.h"

#define kEncodedLength 48
#define LEVEL_kBlockTrailerSize 5
#define LEVELDB_BLOCK_SIZE 1024

static const uint64_t kTableMagicNumber = 0xdb4775248b80fb57ull;
struct level_decode_info {
	uint32_t shared;
	uint32_t non_shared;
	uint32_t value_length;
	char key[34];
	char value[5000];
};

char *Level_GetVarint64Ptr(char *p, char *limit, uint64_t *value)
{
	uint64_t result = 0;
	for (uint32_t shift = 0; shift <= 64 && p < limit; shift += 7) {
		uint64_t byte = *(uint8_t *)p;
		p++;
		if (byte & 128) {
			// More bytes are present
			result |= ((byte & 127) << shift);
		} else {
			result |= (byte << shift);
			*value = result;
			return (char *)p;
		}
	}
	return NULL;
}

char *Level_GetVarint32Ptr(char *p, char *limit, uint32_t *value)
{
	if (p < limit) {
		uint32_t result = *(uint8_t *)p;
		if ((result & 128) == 0) {
			*value = result;
			return p + 1;
		}
	}

	// GetVarint32PtrFallback
	uint32_t result = 0;
	for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7) {
		uint32_t byte = *(uint8_t *)p;
		p++;
		if (byte & 128) {
			// More bytes are present
			result |= ((byte & 127) << shift);
		} else {
			result |= (byte << shift);
			*value = result;
			return (char *)p;
		}
	}
	return NULL;
}

uint32_t Level_DecodeFixed32(char *ptr)
{
	const uint8_t *buffer = (const uint8_t *)ptr;

	return ((uint32_t)(buffer[0])) | ((uint32_t)(buffer[1]) << 8) | ((uint32_t)(buffer[2]) << 16) |
		   ((uint32_t)(buffer[3]) << 24);
}

char *Level_DecodeEntry(char *p, char *limit, uint32_t *shared, uint32_t *non_shared, uint32_t *value_length)
{
	if (limit - p < 3)
		return NULL;
	*shared = ((uint8_t *)p)[0];
	*non_shared = ((uint8_t *)p)[1];
	*value_length = ((uint8_t *)p)[2];
	if ((*shared | *non_shared | *value_length) < 128) {
		// Fast path: all three values are encoded in one byte each
		p += 3;
	} else {
		if ((p = Level_GetVarint32Ptr(p, limit, shared)) == NULL)
			return NULL;
		if ((p = Level_GetVarint32Ptr(p, limit, non_shared)) == NULL)
			return NULL;
		if ((p = Level_GetVarint32Ptr(p, limit, value_length)) == NULL)
			return NULL;
	}

	if ((limit - p) < (*non_shared + *value_length)) {
		return NULL;
	}
	return p;
}

char *Level_EncodeVarint64(char *dst, uint64_t v)
{
	static const int B = 128;
	uint8_t *ptr = (uint8_t *)dst;
	while (v >= B) {
		*(ptr++) = v | B;
		v >>= 7;
	}
	*(ptr++) = (uint8_t)v;
	return (char *)ptr;
}

char *Level_PutVarint64(char *dst, uint64_t v)
{
	char buf[10];
	char *ptr = Level_EncodeVarint64(buf, v);
	memcpy(dst, buf, ptr - buf);
	return dst + (ptr - buf);
}

char *Level_EncodeVarint32(char *dst, uint32_t v)
{
	// Operate on characters as unsigneds
	uint8_t *ptr = (uint8_t *)dst;
	static const int B = 128;
	if (v < (1 << 7)) {
		*(ptr++) = v;
	} else if (v < (1 << 14)) {
		*(ptr++) = v | B;
		*(ptr++) = v >> 7;
	} else if (v < (1 << 21)) {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = v >> 14;
	} else if (v < (1 << 28)) {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = (v >> 14) | B;
		*(ptr++) = v >> 21;
	} else {
		*(ptr++) = v | B;
		*(ptr++) = (v >> 7) | B;
		*(ptr++) = (v >> 14) | B;
		*(ptr++) = (v >> 21) | B;
		*(ptr++) = v >> 28;
	}
	return (char *)ptr;
}

char *Level_PutVarint32(char *dst, uint32_t v)
{
	char buf[5];
	char *ptr = Level_EncodeVarint32(buf, v);
	memcpy(dst, buf, ptr - buf);
	return dst + (ptr - buf);
}

void Level_EncodeFixed32(char *dst, uint32_t value)
{
	uint8_t *buffer = (uint8_t *)dst;

	// Recent clang and gcc optimize this to a single mov / str instruction.
	buffer[0] = (uint8_t)(value);
	buffer[1] = (uint8_t)(value >> 8);
	buffer[2] = (uint8_t)(value >> 16);
	buffer[3] = (uint8_t)(value >> 24);
}

char *Level_PutFixed32(char *dst, uint32_t value)
{
	char buf[sizeof(value)];
	Level_EncodeFixed32(buf, value);
	memcpy(dst, buf, sizeof(value));
	return dst + sizeof(value);
}

#endif