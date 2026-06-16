#ifndef __USER_HYBRID_ENCODING_H__
#define __USER_HYBRID_ENCODING_H__

#include "csd_user_func.h"
#include "bpacking_default.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

uint64_t current_value_;
uint64_t buffered_values_[8];
int repeat_count_;
int num_buffered_values_;
int literal_count_;
uint8_t *literal_indicator_byte_;

int byte_offset_;
int bit_offset_;
size_t max_bytes_;
uint64_t buffered_value_;
uint8_t *buffer_;
int bit_width_;

int64_t BytesForBits(int64_t bits)
{
	// This formula avoids integer overflow on very large `bits`
	return (bits >> 3) + ((bits & 7) != 0);
}
int64_t CeilDiv(int64_t value, int64_t divisor)
{
	return (value == 0) ? 0 : 1 + (value - 1) / divisor;
}

uint64_t TrailingBits(uint64_t v, int num_bits)
{
	if (num_bits == 0)
		return 0;
	if (num_bits >= 64)
		return v;
	int n = 64 - num_bits;
	return (v << n) >> n;
}

void BitWriterFlush(bool align)
{
	int num_bytes = BytesForBits(bit_offset_);
	memcpy(buffer_ + byte_offset_, &buffered_value_, num_bytes);

	if (align) {
		buffered_value_ = 0;
		byte_offset_ += num_bytes;
		bit_offset_ = 0;
	}
}

int bytes_written(void)
{
	return byte_offset_ + BytesForBits(bit_offset_);
}

uint8_t *GetNextBytePtr(int num_bytes)
{
	BitWriterFlush(true); // align
	if (byte_offset_ + num_bytes > max_bytes_) {
		return NULL;
	}
	uint8_t *ptr = buffer_ + byte_offset_;
	byte_offset_ += num_bytes;
	return ptr;
}

uint64_t ReadLittleEndianWord(const uint8_t *buffer, int bytes_remaining)
{
	uint64_t le_value = 0;
	if (bytes_remaining >= 8) {
		memcpy(&le_value, buffer, 8);
	} else {
		memcpy(&le_value, buffer, bytes_remaining);
	}
	return le_value;
}

bool PutAligned(uint8_t val, int num_bytes)
{
	uint8_t *ptr = GetNextBytePtr(num_bytes);
	if (ptr == NULL) {
		return false;
	}
	// skip ToLittleEndian due to uint8_t
	memcpy(ptr, &val, num_bytes);
	return true;
}

bool GetAligned(int num_bytes, uint8_t *v)
{
	if (num_bytes > sizeof(uint8_t)) {
		return false;
	}

	int bytes_read = BytesForBits(bit_offset_);
	if (byte_offset_ + bytes_read + num_bytes > max_bytes_) {
		return false;
	}

	// Advance byte_offset_ to next unread byte and read num_bytes
	byte_offset_ += bytes_read;
	memcpy(v, buffer_ + byte_offset_, num_bytes);
	byte_offset_ += num_bytes;

	bit_offset_ = 0;
	buffered_value_ = ReadLittleEndianWord(buffer_ + byte_offset_, max_bytes_ - byte_offset_);
	return true;
}

bool GetAligneduint64_t(int num_bytes, uint64_t *v)
{
	if (num_bytes > sizeof(uint64_t)) {
		return false;
	}

	int bytes_read = BytesForBits(bit_offset_);
	if (byte_offset_ + bytes_read + num_bytes > max_bytes_) {
		return false;
	}

	// Advance byte_offset to next unread byte and read num_bytes
	byte_offset_ += bytes_read;
	memcpy(v, buffer_ + byte_offset_, num_bytes);
	byte_offset_ += num_bytes;

	bit_offset_ = 0;
	buffered_value_ = ReadLittleEndianWord(buffer_ + byte_offset_, max_bytes_ - byte_offset_);
	return true;
}

bool PutVlqInt(uint32_t v)
{
	bool result = true;
	while ((v & 0xFFFFFF80UL) != 0UL) {
		result &= PutAligned((uint8_t)((v & 0x7F) | 0x80), 1);
		v >>= 7;
	}
	result &= PutAligned((uint8_t)(v & 0x7F), 1);

	return result;
}

bool GetVlqInt(uint32_t *v)
{
	uint32_t tmp = 0;

	for (int i = 0; i < 5; i++) {
		uint8_t byte = 0;
		if (!GetAligned(1, &byte)) {
			return false;
		}
		tmp |= ((uint32_t)(byte & 0x7F)) << (7 * i);

		if ((byte & 0x80) == 0) {
			*v = tmp;
			return true;
		}
	}

	return false;
}

void CheckBufferFull(void)
{
	// Skip implementing for now. We can assume that the buffer is large enough
}

void FlushRepeatedRun(void)
{
	bool result = true;
	// The lsb of 0 indicates this is a repeated run
	int32_t indicator_value = repeat_count_ << 1 | 0;
	result &= PutVlqInt(indicator_value);
	result &= PutAligned(current_value_, CeilDiv(bit_width_, 8));
	NVMEV_ASSERT(result);
	num_buffered_values_ = 0;
	repeat_count_ = 0;
	CheckBufferFull();
}

bool PutValue(uint64_t v, int num_bits)
{
	if (((byte_offset_ * 8) + bit_offset_ + num_bits) > (max_bytes_ * 8)) {
		return false;
	}

	buffered_value_ |= v << bit_offset_;
	bit_offset_ += num_bits;

	if (bit_offset_ >= 64) {
		// Flush buffered_values_ and write out bits of v that did not fit
		memcpy(buffer_ + byte_offset_, &buffered_value_, 8);
		buffered_value_ = 0;
		byte_offset_ += 8;
		bit_offset_ -= 64;
		buffered_value_ = (num_bits - bit_offset_ == 64) ? 0 : (v >> (num_bits - bit_offset_));
	}
	return true;
}

void FlushLiteralRun(bool update_indicate_byte)
{
	if (literal_indicator_byte_ == NULL) {
		// The literal indicator byte has not been reserved yet, get one now.
		literal_indicator_byte_ = GetNextBytePtr(1);
		NVMEV_ASSERT(literal_indicator_byte_ != NULL);
	}

	// Write all the buffered values as bit packed literals
	for (int i = 0; i < num_buffered_values_; ++i) {
		bool success = PutValue(buffered_values_[i], bit_width_);
		NVMEV_ASSERT(success);
	}
	num_buffered_values_ = 0;

	if (update_indicate_byte) {
		// At this point we need to write the indicator byte for the literal run.
		// We only reserve one byte, to allow for streaming writes of literal values.
		// The logic makes sure we flush literal runs often enough to not overrun
		// the 1 byte.
		NVMEV_ASSERT(literal_count_ % 8 == 0);
		int num_groups = literal_count_ / 8;
		// printk("%d, %d\n", literal_count_, num_groups);
		int32_t indicator_value = (num_groups << 1) | 1;
		NVMEV_ASSERT((indicator_value & 0xFFFFFF00) == 0);
		*literal_indicator_byte_ = (uint8_t)indicator_value;
		literal_indicator_byte_ = NULL;
		literal_count_ = 0;
		CheckBufferFull();
	}
}

/// Flush the values that have been buffered.  At this point we decide whether
/// we need to switch between the run types or continue the current one.
void FlushBufferedValues(bool done)
{
	if (repeat_count_ >= 8) {
		// Clear the buffered values.  They are part of the repeated run now and we
		// don't want to flush them out as literals.
		num_buffered_values_ = 0;
		if (literal_count_ != 0) {
			NVMEV_ASSERT(literal_count_ % 8 == 0);
			NVMEV_ASSERT(repeat_count_ == 8);
			FlushLiteralRun(true);
		}
		NVMEV_ASSERT(literal_count_ == 0);
		return;
	}

	literal_count_ += num_buffered_values_;
	NVMEV_ASSERT(literal_count_ % 8 == 0);
	int num_groups = literal_count_ / 8;
	if (num_groups + 1 >= (1 << 6)) {
		NVMEV_ASSERT(literal_indicator_byte_ != NULL);
		FlushLiteralRun(true);
	} else {
		FlushLiteralRun(done);
	}
	repeat_count_ = 0;
}

bool Put(uint64_t value)
{
	// printk("repeat_count_: %d, literal_count_: %d, num_buffered_value: %d, value: %d\n", repeat_count_, literal_count_, num_buffered_values_, value);
	if (current_value_ == value) {
		++repeat_count_;
		if (repeat_count_ > 8) {
			// This is just a continuation of the current run, no need to buffer the
			// values.
			// Note that this is the fast path for long repeated runs.
			return true;
		}
	} else {
		if (repeat_count_ >= 8) {
			// We had a run that was long enough but it has ended.  Flush the
			// current repeated run.
			NVMEV_ASSERT(literal_count_ == 0);
			FlushRepeatedRun();
		}
		repeat_count_ = 1;
		current_value_ = value;
	}

	buffered_values_[num_buffered_values_] = value;
	if (++num_buffered_values_ == 8) {
		NVMEV_ASSERT(literal_count_ % 8 == 0);
		FlushBufferedValues(false);
	}

	return true;
}

int Flush(void)
{
	if (literal_count_ > 0 || repeat_count_ > 0 || num_buffered_values_ > 0) {
		bool all_repeat = literal_count_ == 0 && (repeat_count_ == num_buffered_values_ || num_buffered_values_ == 0);
		// There is something pending, figure out if it's a repeated or literal run
		if (repeat_count_ > 0 && all_repeat) {
			FlushRepeatedRun();
		} else {
			NVMEV_ASSERT(literal_count_ % 8 == 0);
			// Buffer the last group of literals to 8 by padding with 0s.
			for (; num_buffered_values_ != 0 && num_buffered_values_ < 8; ++num_buffered_values_) {
				buffered_values_[num_buffered_values_] = 0;
			}
			literal_count_ += num_buffered_values_;
			FlushLiteralRun(true);
			repeat_count_ = 0;
		}
	}
	BitWriterFlush(false);

	return bytes_written();
}

int CountLeadingZeros(uint64_t value)
{
	int bitpos = 0;
	while (value != 0) {
		value >>= 1;
		++bitpos;
	}
	return 64 - bitpos;
}

int NumRequiredBits(uint64_t x)
{
	return 64 - CountLeadingZeros(x);
}

bool NextCounts(void)
{
	uint32_t indicator_value = 0;
	if (!GetVlqInt(&indicator_value))
		return false;

	// lsb indicates if it is a literal run or repeated run
	bool is_literal = (indicator_value & 1) == 1;
	uint32_t count = indicator_value >> 1;
	if (is_literal) {
		if (count == 0 || count > INT_MAX / 8) {
			return false;
		}
		literal_count_ = count * 8;
	} else {
		if (count == 0 || count > INT_MAX) {
			return false;
		}
		repeat_count_ = count;
		uint64_t value = 0;
		if (!GetAligneduint64_t(CeilDiv(bit_width_, 8), &value)) {
			return false;
		}
		current_value_ = value;
	}
	return true;
}

void GetValue_(int num_bits, uint64_t *v, int max_bytes, uint8_t *buffer, int *bit_offset, int *byte_offset,
			   uint64_t *buffered_values)
{
	*v = (uint64_t)TrailingBits(*buffered_values, *bit_offset + num_bits) >> *bit_offset;

	*bit_offset += num_bits;
	if (*bit_offset >= 64) {
		*byte_offset += 8;
		*bit_offset -= 64;

		*buffered_values = ReadLittleEndianWord(buffer + *byte_offset, max_bytes - *byte_offset);

		// Read bits of v that crossed into new buffered_values_
		if (num_bits - *bit_offset < 8 * sizeof(uint64_t)) {
			// if shift exponent(num_bits - *bit_offset) is not less than sizeof(T), *v will not
			// change and the following code may cause a runtime error that the shift exponent
			// is too large
			*v = *v | (TrailingBits(*buffered_values, *bit_offset) << (num_bits - *bit_offset));
		}
	}
}

int BitReaderGetBatch(int num_bits, uint64_t *v, int batch_size)
{
	NVMEV_ASSERT(buffer_ != NULL);

	int bit_offset = bit_offset_;
	int byte_offset = byte_offset_;
	uint64_t buffered_values = buffered_value_;
	int max_bytes = max_bytes_;
	uint8_t *buffer = buffer_;

	int64_t needed_bits = num_bits * batch_size;
	uint64_t kBitsPerByte = 8;
	int64_t remaining_bits = (max_bytes - byte_offset) * kBitsPerByte - bit_offset;
	if (remaining_bits < needed_bits) {
		batch_size = remaining_bits / num_bits;
	}

	int i = 0;
	if (bit_offset != 0) {
		for (; i < batch_size && bit_offset != 0; ++i) {
			GetValue_(num_bits, &v[i], max_bytes, buffer, &bit_offset, &byte_offset, &buffered_values);
		}
	}

	if (num_bits > 32) {
		// skip 64 for now
		printk("ERROR!!!!\n");
	} else {
		// PARQUET_TODO ->  revisit this limit if necessary
		enum { buffer_size = 1024 };
		uint32_t unpack_buffer[buffer_size];
		while (i < batch_size) {
			int unpack_size = MIN(buffer_size, batch_size - i);
			int num_unpacked =
				unpack32_default((uint32_t *)(buffer + byte_offset), unpack_buffer, unpack_size, num_bits);
			if (num_unpacked == 0) {
				break;
			}
			for (int k = 0; k < num_unpacked; ++k) {
				v[i + k] = unpack_buffer[k];
			}
			i += num_unpacked;
			byte_offset += (num_unpacked * num_bits) / 8;
		}
	}

	buffered_values = ReadLittleEndianWord(buffer + byte_offset, max_bytes - byte_offset);

	for (; i < batch_size; ++i) {
		GetValue_(num_bits, &v[i], max_bytes, buffer, &bit_offset, &byte_offset, &buffered_values);
	}

	bit_offset_ = bit_offset;
	byte_offset_ = byte_offset;
	buffered_value_ = buffered_values;

	return batch_size;
}

int GetBatch(uint64_t *values, int batch_size)
{
	int values_read = 0;

	uint64_t *out = values;

	while (values_read < batch_size) {
		int remaining = batch_size - values_read;

		if (repeat_count_ > 0) { // Repeated value case.
			int repeat_batch = MIN(remaining, repeat_count_);
			for (int i = 0; i < repeat_batch;
				 i++) { // std::fill(out, out + repeat_batch, static_cast<T>(current_value_));
				out[i] = current_value_;
			}

			repeat_count_ -= repeat_batch;
			values_read += repeat_batch;
			out += repeat_batch;
		} else if (literal_count_ > 0) {
			int literal_batch = MIN(remaining, literal_count_);
			int actual_read = BitReaderGetBatch(bit_width_, out, literal_batch);
			if (actual_read != literal_batch) {
				return values_read;
			}

			literal_count_ -= literal_batch;
			values_read += literal_batch;
			out += literal_batch;
		} else {
			if (!NextCounts())
				return values_read;
		}
	}

	return values_read;
}

size_t __hybrid_encoding(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	size_t value_count = size / sizeof(uint64_t);
	int max_value = 20;

	uint64_t *input = (uint64_t *)buf_in;

	/* Set Variables */
	buffer_ = (uint8_t *)buf_out;
	max_bytes_ = size;
	byte_offset_ = 0;
	bit_offset_ = 0;
	buffered_value_ = 0;

	bit_width_ = NumRequiredBits(max_value);
	current_value_ = 0;
	repeat_count_ = 0;
	num_buffered_values_ = 0;
	literal_count_ = 0;
	literal_indicator_byte_ = NULL;

	for (size_t i = 0; i < value_count; i++) {
		// printk("%lld\n", input[i]);
		Put(input[i]);
	}
	printk("flush\n");
	int encoded_len = Flush();

	printk("size: %lu(%zu), encoded_len: %d, bit_width_: %d\n", size, value_count, encoded_len,
		   NumRequiredBits(max_value));

	// uint8_t* output = (uint8_t*) buf_out;
	// for (int i = 0; i < encoded_len; i++) {
	//     printk("%02X ", output[i]);
	// }
	// printk("\n");

	return encoded_len;
}

size_t __hybrid_decoding(void *buf_in, void *buf_out, size_t size, void *param)
{
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	size_t value_count = temp->decoding_params.original_size / sizeof(uint64_t);
	uint64_t *output = (uint64_t *)buf_out;
	int max_value = temp->decoding_params.max_value;

	/* Set Variables */
	buffer_ = (uint8_t *)buf_in;
	max_bytes_ = temp->decoding_params.original_size;
	byte_offset_ = 0;
	bit_offset_ = 0;
	buffered_value_ = ReadLittleEndianWord(buffer_ + byte_offset_, max_bytes_ - byte_offset_);

	bit_width_ = NumRequiredBits(max_value);
	current_value_ = 0;
	repeat_count_ = 0;
	literal_count_ = 0;

	int items = GetBatch(output, value_count);
	printk("items: %d\n", items);

	// for (size_t i = 0; i < items; i++) {
	//     printk("%lld ", output[i]);
	// }
	// printk("\n");

	return items * sizeof(uint64_t);
}

#endif