#define CSDVIRT_USER_PROGRAM
#include <linux/crypto.h>

#include "nvmev.h"
#include "csd_slm.h"
#include "csd_user_func.h"
#include "user_func_hybrid_encoding.h"
#include "user_func_leveldb.h"
#include "user_func_crc32c.h"
#include "csd_dispatcher.h"

extern struct nvmev_dev *vdev;

static inline unsigned long long __get_wallclock(void)
{
	return cpu_clock(vdev->config.cpu_nr_dispatcher);
}

#if ((CSD_ENABLE) && (SUPPORT_ASYNC_COMMAND))
#define NVMEV_CSD_PROFILE_REAL_START(core_num, task_id) NVMEV_CSD_PROFILE_START("REALCOMPUTE", core_num, task_id, 0)
#define NVMEV_CSD_PROFILE_REAL_END(core_num, task_id) NVMEV_CSD_PROFILE_END("REALCOMPUTE", core_num, task_id, 0)
#else
#define NVMEV_CSD_PROFILE_REAL_START(core_num, task_id)
#define NVMEV_CSD_PROFILE_REAL_END(core_num, task_id)
#endif

size_t __csdvirt_run_program(int program_idx, void *buf_in, void *buf_out, size_t size, void *params)
{
	size_t ret = 0;
	if (program_idx == KEY_FINDER_PROGRAM_INDEX) {
		ret = __key_finder((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == KNN_PROGRAM_INDEX) {
		ret = __knn((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == STAT32_PROGRAM_INDEX) {
		ret = __statistic_int32((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == GREP_PROGRAM_INDEX) {
		ret = __grep((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == BTREE_PROGRAM_INDEX) {
		ret = __btree(buf_in, buf_out, size, params);
	} else if (program_idx == BTREE_PROGRAM_INDEX_TEMP) {
		ret = __btree_demand(buf_in, buf_out, size, params);
	} else if (program_idx == TPCH_FILTER_INDEX) {
		ret = __tpch_filter((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == COMPRESSION_INDEX) {
		ret = __compression((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == DECOMPRESSION_INDEX) {
		ret = __decompression((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == QUICK_SORT_INDEX) {
		ret = __quickSort((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == TPCH_STAT_PROGRAM_INDEX) {
		ret = __tpch_statistic((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == TPCH_FILTER2_INDEX) {
		ret = __tpch_filter2((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == COMPACTION_PROGRAM_INDEX) {
		ret = __compaction((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == LEVELDB_COMPACTION_PROGRAM_INDEX) {
		ret = __leveldb_compaction((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == CRC_CALCULATION_PROGRAM_INDEX) {
		ret = __crc_calculation((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == HYBRID_ENCODING_PROGRAM_INDEX) {
		ret = __hybrid_encoding((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == HYBRID_DECODING_PROGRAM_INDEX) {
		ret = __hybrid_decoding((char *)buf_in, (char *)buf_out, size, params);
	} else if (program_idx == FILTERED_STAT32_PROGRAM_INDEX) {
		ret = __filtered_statistic_int32((char *)buf_in, (char *)buf_out, size, params);
	} else {
		printk("INVALID PROGRAM_INDEX %d\n", program_idx);
	}

	return ret;
}

void *get_data_from_ptr(size_t ptr, size_t size)
{
	if (check_slm_data_ready(ptr, size, true) == false) {
		slm_request_demand_read(ptr, size);
		// return NULL;

		while (check_slm_data_ready(ptr, size, false) == false) {
			cond_resched();
		}
	}
	return (void *)ptr;
}

/*
 * check_data_using_ptr() function acts same as get_data_from_ptr().
 * However, it does not return the pointer address due to changed usage.
 * In __key_finder, __statistic_int32, __compaction, and decoding/encoding, 
 * get_data_from_ptr's usage changed to simply checking the availability of 
 * the desired data in SLM. Therefore, return value is not needed anymore.
 * Also, we needed to track the SLM waiting time for real compute time profiling.
 * Therfore, we added a new function, check_data_using_ptr().
 * get_data_from_ptr() is still used in other functions that are not 
 * related to cases that makes standard CSD usage difficult.
 */
int check_data_using_ptr(size_t ptr, size_t size, int pid, int host_id)
{
	if (check_slm_data_ready(ptr, size, true) == false) {
		slm_request_demand_read(ptr, size);
		// return NULL;

		/* In case of sequence of execution, check if prior execution is done */
		if (check_slm_output_data_finalized(ptr, size) == true) {
			return 0;
		}
		NVMEV_CSD_PROFILE_REAL_END(pid, host_id);
		while (check_slm_data_ready(ptr, size, false) == false) {
			cond_resched();
		}
		NVMEV_CSD_PROFILE_REAL_START(pid, host_id);
	}
	return 1;
}

void set_data_from_ptr(size_t dst, size_t src, size_t size)
{
	// printk("src: %lu, dst: %lu, size: %lu\n", src, dst, size);
	memcpy((void *)dst, (void *)src, size);

	notify_if_slm_data_ready(dst, size);
}

size_t __key_finder(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	size_t i = 0;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	unsigned int field = temp->key_finder_params.field;
	unsigned int value = temp->key_finder_params.value;
	// printk("field:%u, value:%u\n", field, value);
	size_t offset = 0;
	size_t physical_offset = 0;
	int pid = temp->profile_info.pid;
	int host_id = temp->profile_info.host_id;

	NVMEV_CSD_PROFILE_REAL_START(pid, host_id);

	struct key_finder_format *data_set = (struct key_finder_format *)buf_in;
	// struct key_finder_format* data_set = get_data_from_ptr((size_t) buf_in, size);
	for (i = 0; i < size / sizeof(struct key_finder_format); i++) {
		if (physical_offset % SLM_PAGE_SIZE == 0) {
			// check if SLM PAGE is available before accessing
			check_data_using_ptr((size_t)buf_in + physical_offset, SLM_PAGE_SIZE, pid, host_id);
		}

		if (data_set[i].data[field] == value) {
			offset = i * sizeof(struct key_finder_format);
			// memcpy(buf_out, &(data_set[i]), sizeof(struct key_finder_format));
			set_data_from_ptr((size_t)buf_out, (size_t)data_set + offset, sizeof(struct key_finder_format));
			buf_out = (char *)buf_out + sizeof(struct key_finder_format);
			result++;
			// printk("%d - 0x%x\n", i, data_set[i].data[5]);
		}
		physical_offset += sizeof(struct key_finder_format);
	}

	NVMEV_CSD_PROFILE_REAL_END(pid, host_id);

	return (result * sizeof(struct key_finder_format));
}

size_t __tpch_filter(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	size_t i = 0;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	unsigned int order_key = temp->tpch_filter_params.value1;
	unsigned int line_number = temp->tpch_filter_params.value2;
	// printk("field:%u, value:%u\n", field, value);

	struct tpch_filter_format *data_set = (struct tpch_filter_format *)get_data_from_ptr((size_t)buf_in, size);

	for (i = 0; i < size / sizeof(struct tpch_filter_format); i++) {
		if (data_set[i].l_orderkey == order_key && data_set[i].l_linenumber == line_number) {
			memcpy(buf_out, &(data_set[i]), sizeof(struct tpch_filter_format));
			buf_out = (char *)buf_out + sizeof(struct tpch_filter_format);
			result++;
			// printk("%d - 0x%x\n", i, data_set[i].data[5]);
		}
	}

	return (result * sizeof(struct tpch_filter_format));
}

size_t __statistic_int32(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t i = 0;
	//unsigned int* value = (unsigned int *) get_data_from_ptr((size_t) buf_in, size);
	unsigned int *value = (unsigned int *)buf_in;
	struct statistic_format *stat = (struct statistic_format *)buf_out;
	int pid = ((struct CSD_PARAMS *)param)->profile_info.pid;
	int host_id = ((struct CSD_PARAMS *)param)->profile_info.host_id;

	stat->sum = 0;
	stat->max = 0;
	stat->min = 0xFFFFFFFF;
	size_t physical_offset = 0;

	NVMEV_CSD_PROFILE_REAL_START(pid, host_id);

	for (i = 0; i < size / sizeof(unsigned int); i++) {
		if (physical_offset % SLM_PAGE_SIZE == 0) {
			// check if SLM PAGE is available before accessing
			check_data_using_ptr((size_t)buf_in + physical_offset, SLM_PAGE_SIZE, pid, host_id);
		}
		stat->sum += value[i];

		if (stat->max < value[i]) {
			stat->max = value[i];
		}
		if (stat->min > value[i]) {
			stat->min = value[i];
		}
		physical_offset += sizeof(unsigned int);
	}
	// printk("result : i:%lu, sum:%lu, max:%u, min:%u", i, stat->sum, stat->max, stat->min);
	NVMEV_CSD_PROFILE_REAL_END(pid, host_id);

	return (sizeof(struct statistic_format));
}

size_t __filtered_statistic_int32(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	size_t i = 0;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	unsigned int field = temp->key_finder_params.field;
	unsigned int value = temp->key_finder_params.value;
	struct statistic_format *stat = (struct statistic_format *)buf_out;
	size_t offset = 0;
	size_t physical_offset = 0;
	int pid = temp->profile_info.pid;
	int host_id = temp->profile_info.host_id;

	NVMEV_CSD_PROFILE_REAL_START(pid, host_id);

	struct key_finder_format *data_set = (struct key_finder_format *)buf_in;

	// struct key_finder_format* data_set = get_data_from_ptr((size_t) buf_in, size);
	for (i = 0; i < size / sizeof(struct key_finder_format); i++) {
		if (physical_offset % SLM_PAGE_SIZE == 0) {
			// check if SLM PAGE is available before accessing
			if (check_data_using_ptr((size_t)buf_in + physical_offset, SLM_PAGE_SIZE, pid, host_id) == 0) {
				break;
			}
		}

		stat->sum += data_set[i].data[7];

		if (stat->max < data_set[i].data[7]) {
			stat->max = data_set[i].data[7];
		}
		if (stat->min > data_set[i].data[7]) {
			stat->min = data_set[i].data[7];
		}

		physical_offset += sizeof(struct key_finder_format);
	}
	printk("result : i:%lu, sum:%lu, max:%u, min:%u", i, stat->sum, stat->max, stat->min);

	NVMEV_CSD_PROFILE_REAL_END(pid, host_id);

	return (sizeof(struct statistic_format));
}

size_t __tpch_statistic(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t i = 0;
	struct tpch_filter_format *data_set = (struct tpch_filter_format *)get_data_from_ptr((size_t)buf_in, size);
	struct statistic_format *stat = (struct statistic_format *)buf_out;

	for (i = 0; i < size / sizeof(struct tpch_filter_format); i++) {
		size_t j = 0;
		int *value = (int *)&(data_set[i]);
		if (value[0] == 0xBBBBBBBB) {
			struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
			temp->finish = 1;
			break;
		}
#if 0
        for (j = 0; j < sizeof(struct tpch_filter_format) / sizeof(int); j++) {
            stat->sum += value[j];

            if (stat->max < value[j]) {
                stat->max = value[j];
            }
            if (stat->min > value[j]) {
                stat->min = value[j];
            }
        }
#else
		stat->sum += data_set[i].l_extendedprice;
		if (stat->max < data_set[i].l_shipdate) {
			stat->max = data_set[i].l_shipdate;
		}
		if (stat->min > data_set[i].l_shipdate) {
			stat->min = data_set[i].l_shipdate;
		}
#endif
		// printk("result : i:%lu, sum:%lu, max:%u, min:%u", i, data_set[i].l_extendedprice, data_set[i].l_shipdate, data_set[i].l_shipdate);
	}
	// printk("result : i:%lu, sum:%lu, max:%u, min:%u", i, stat->sum, stat->max, stat->min);

	return (sizeof(struct statistic_format));
}

size_t __knn(void *buf_in, void *buf_out, size_t size, void *param)
{
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	unsigned int row_length = temp->knn_params.row_length;
	unsigned int item_size = temp->knn_params.item_size;
	unsigned int item_per_row = row_length / item_size;

	uint64_t size_o = 0;
	uint64_t *in = (uint64_t *)get_data_from_ptr((size_t)buf_in, size);
	uint64_t *out = (uint64_t *)buf_out;

	for (uint64_t row_p = 0; row_p < size; row_p += row_length) {
		uint64_t dist = 0;
		for (uint64_t j = 0; j < item_per_row - 1; j++) {
			uint64_t s = in[row_p / item_size + j + 1];
			uint64_t f = s - j;
			dist += (f > 0 ? f : -f);
		}
		out[size_o / sizeof(uint64_t)] = row_p;
		out[size_o / sizeof(uint64_t) + 1] = dist;
		size_o += sizeof(uint64_t) * 2;
	}
	// printk("[knn] expect_result : %llu\n", size_o);

	return size_o;
}

size_t __grep(void *buf_in, void *buf_out, size_t size, void *param)
{
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;

	size_t column_size = temp->grep_params.column_size;
	unsigned char *param_str = temp->grep_params.str;
	size_t str_len = temp->grep_params.str_len;

	size_t size_o = 0;
	char *in = (char *)get_data_from_ptr((size_t)buf_in, size);
	uint64_t *out = (uint64_t *)buf_out;
	uint64_t row_p = 0;
	uint64_t result = -1;
	bool matched = false;
	uint64_t j = 0;
	uint64_t k = 0;

	for (row_p = 0; row_p < size; row_p += column_size) {
		result = -1;
		for (j = 0; j < column_size; j++) {
			if (j + str_len <= column_size) {
				matched = 1;
				for (k = 0; k < str_len; k++) {
					matched &= (param_str[k] == in[row_p + j + k]);
				}
				if (matched) {
					result = j;
					break;
				}
			}
		}
		out[size_o] = result;
		size_o += 1;
	}

	return size_o * sizeof(uint64_t);
}

size_t __btree(void *buf_in, void *buf_out, size_t size, void *param)
{
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;

	struct BtreeLog *output;
	size_t key = temp->btree_params.key;
	size_t offset = 0; // root node

	bool not_find = true;
	size_t base_addr = (size_t)buf_in;
	struct BtreeNode *node = (struct BtreeNode *)get_data_from_ptr(base_addr, sizeof(struct BtreeNode));
	if (node == NULL) {
		return -1;
	}

	while (node->type != LEAF) {
		not_find = true;
		for (size_t i = 1; i < NODE_CAPACITY; i++) {
			if (key < node->key[i]) {
				node = (struct BtreeNode *)get_data_from_ptr(base_addr + node->ptr[i - 1], sizeof(struct BtreeNode));
				if (node == NULL) {
					return -1;
				}
				not_find = false;
				break;
			}
		}

		if (not_find == true) {
			node = (struct BtreeNode *)get_data_from_ptr(base_addr + node->ptr[NODE_CAPACITY - 1],
														 sizeof(struct BtreeNode));
			if (node == NULL) {
				return -1;
			}
		}
	}

	not_find = true;
	for (unsigned int i = 0; i < NODE_CAPACITY; ++i) {
		// printk("key:%lu, value:%lu", node->key[i], node->ptr[i]);
		if (node->key[i] == key) {
			not_find = false;
			offset = node->ptr[i];
			break;
		}
	}

	if (not_find == true) {
		return 0;
	}

	output = (struct BtreeLog *)get_data_from_ptr(base_addr + offset, sizeof(struct BtreeLog));
	if (output == NULL) {
		return -1;
	}

	memcpy(buf_out, output, sizeof(struct BtreeLog));
	printk("##Key:%lu, Offset:%lu, Output : %s", key, offset, (char *)output);
	return sizeof(struct BtreeLog);
}

size_t __btree_demand(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t retval = 0;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;

	struct BtreeLog *output;
	size_t key = temp->btree_params.key;
	size_t offset = 0; // root node

	bool not_find = true;
	char *base_addr = (char *)buf_in;
	struct BtreeNode *node = (struct BtreeNode *)base_addr;

	not_find = true;
	if (node->type != LEAF) {
		for (size_t i = 1; i < NODE_CAPACITY; i++) {
			if (key < node->key[i]) {
				retval = node->ptr[i - 1];
				not_find = false;
				break;
			}
		}

		if (not_find == true) {
			retval = node->ptr[NODE_CAPACITY - 1];
		}
	} else if (node->type == LEAF) {
		for (unsigned int i = 0; i < NODE_CAPACITY; ++i) {
			if (node->key[i] == key) {
				not_find = false;
				retval = node->ptr[i] | (1 << 31);
				break;
			}
		}

		if (not_find == true) {
			printk("not_find.....\n");
		}
	} else {
		printk("FAILED!\n");
	}
	return retval;
}

size_t __compression(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	int output_size = size;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;

	if (temp->compression_params.compress_type == 0) {
		// TODO: Add HW compression engine timing model & output

		memcpy(buf_out, buf_in, size);
	} else {
		int ret = crypto_comp_compress(vdev->tfm, buf_in, size, buf_out, &output_size);
		if (unlikely(ret)) {
			NVMEV_ERROR("Compression ERROR\n");
		}

		if (output_size % 4096 != 0) {
			output_size = ((output_size / 4096) + 1) * 4096;
		}
	}

	return output_size;
}

size_t __decompression(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	int output_size = size;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	size_t compressed_size = temp->compressed_size;

	get_data_from_ptr((size_t)buf_in, compressed_size);
	if (temp->decompression_params.decompress_type == 0) {
		// TODO: Add HW compression engine timing model & output

		memcpy(buf_out, buf_in, size);
	} else {
		int ret = crypto_comp_decompress(vdev->tfm, buf_in, compressed_size, buf_out, &output_size);
		if (unlikely(ret)) {
			NVMEV_ERROR("Decompression ERROR\n");
		}
	}

	// printk("output_size:%lu, %lu", compressed_size, output_size);

	return output_size;
}

void merge(void *buf_in, void *buf_out, size_t mid, size_t end)
{
	struct tpch_filter_format *data_in = (struct tpch_filter_format *)buf_in;
	struct tpch_filter_format *data_out = (struct tpch_filter_format *)buf_out;

	size_t i = 0;
	size_t j = 0;
	size_t k = 0;

	i = 0;
	j = mid;
	k = 0;
	while (i < mid && j < end) {
		if (data_in[i].l_orderkey <= data_in[j].l_orderkey) {
			data_out[k++] = data_in[i++];
		} else {
			data_out[k++] = data_in[j++];
		}
	}

	while (i < mid) {
		data_out[k++] = data_in[i++];
	}

	while (j < end) {
		data_out[k++] = data_in[j++];
	}

	memcpy(buf_in, buf_out, end * sizeof(struct tpch_filter_format));
}

// Merge Sort Function
size_t mergeSort(void *buf_in, void *buf_out, size_t size, void *param)
{
	struct tpch_filter_format *data_in = (struct tpch_filter_format *)buf_in;
	struct tpch_filter_format *data_out = (struct tpch_filter_format *)buf_out;
	size_t item_size = size / sizeof(struct tpch_filter_format);
	size_t mid = (item_size / 2);
	size_t mid_size = (item_size / 2) * sizeof(struct tpch_filter_format);

	if (item_size <= 1) {
		return size; // Already sorted
	}

	mergeSort(data_in, data_out, mid_size, param);
	mergeSort(&data_in[mid], &data_out[mid], size - mid_size, param);

	merge(buf_in, buf_out, mid, item_size);

	return size;
}

size_t __quickSort(void *buf_in, void *buf_out, size_t size, void *param)
{
	return mergeSort(buf_in, buf_out, size, param);
#if 0
    struct tpch_filter_format *arr = (struct tpch_filter_format *) buf_in;
    int *temp = (int *) buf_out;
    size_t item_size = size / sizeof(struct tpch_filter_format);    
    int top = -1;
    struct tpch_filter_format t;
    int p = 0;
    
    temp[++top] = 0;
    temp[++top] = item_size - 1;
    
    while (top >= 0) {
        int high = temp[top--];
        int low = temp[top--];
                
        int pivot = arr[high].l_orderkey;
        int i = (low - 1);
        int j = low;
        
        for (j = low; j <= high - 1; j++) {
            if (arr[j].l_orderkey < pivot) {
                i++;
                t = arr[i];
                arr[i] = arr[j];
                arr[j] = t;
            }
        }
        
        t = arr[i + 1];
        arr[i + 1] = arr[high];
        arr[high] = t;
        p = (i + 1);
        
        if (p - 1 > low) {
            temp[++top] = low;
            temp[++top] = p - 1;
        }
        
        if (p + 1 < high) {
            temp[++top] = p + 1;
            temp[++top] = high;
        }
    }

    memcpy(buf_out, buf_in, size);

    return size;
#endif
}

size_t __tpch_filter2(void *buf_in, void *buf_out, size_t size, void *param)
{
	static const size_t BUFFER_SIZE = 256;
	static const int FIELD_COUNT = tpch_field_count;
	static const char SEPERATOR = '|';
	static const char LINE_SEPERATOR = '\n';

	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	int tpch_num = temp->tpch_params.tpch_num;
	// char record_buffer[BUFFER_SIZE];

	size_t row_count = 0;
	size_t field_count = 0;
	size_t record_offset = 0;
	size_t field_offset = 0;
	char *input_start = (char *)get_data_from_ptr((size_t)buf_in, size);
	char *result_start = (char *)buf_out;

	char fields[FIELD_COUNT][256];

	for (size_t i = 0; i < size; i++) {
		// record_buffer[record_offset] = input_start[i];
		result_start[record_offset] = input_start[i];
		fields[field_count][field_offset++] = input_start[i];
		record_offset++;

		if (field_offset > BUFFER_SIZE) {
			size_t j = 0;
			printk("Error : %lu %lu, field_count:%lu, result_start:%lu", i, record_offset, field_count,
				   (size_t)result_start);
			printk("fields:%s", fields[field_count]);

			// BUG();
			record_offset = 0;
			break;
		}

		if (input_start[i] == SEPERATOR) {
			fields[field_count][field_offset - 1] = '\0';
			field_count++;
			field_offset = 0;
		}

		if (input_start[i] == LINE_SEPERATOR) {
			bool find = false;

			if ((field_count != FIELD_COUNT) || (row_count == 0)) {
				find = true;
			} else {
				if (tpch_num == 0) {
					find = true;
				}

				if (tpch_num == 1) {
					if (strcmp(fields[tpch_field_shipdate], "1998-09-02") <= 0) {
						find = true;
					}
				}

				if (tpch_num == 3) {
					if (strcmp(fields[tpch_field_shipdate], "1995-03-15") > 0) {
						find = true;
					}
				}

				if (tpch_num == 4) {
					if (strcmp(fields[tpch_field_commitdate], fields[tpch_field_receiptdate]) < 0) {
						find = true;
					}
				}

				// 6
				// $"l_shipdate" >= "1994-01-01" && $"l_shipdate" < "1995-01-01" && $"l_discount" >= 0.05 && $"l_discount" <= 0.07 && $"l_quantity" < 24
				if (tpch_num == 6) {
					if (strcmp(fields[tpch_field_discount], "0.05") >= 0) {
						if (strcmp(fields[tpch_field_discount], "0.07") <= 0) {
							//if (strcmp(fields[tpch_field_quantity], "24") < 0) {
							if (strcmp(fields[tpch_field_shipdate], "1994-01-01") >= 0) {
								if (strcmp(fields[tpch_field_shipdate], "1995-01-01") < 0) {
									find = true;
								}
							}
							//}
						}
					}
				}

				// 7
				//  "l_shipdate" >= "1995-01-01" && $"l_shipdate" <= "1996-12-31"
				if (tpch_num == 7) {
					// Q14
					if (strcmp(fields[tpch_field_shipdate], "1995-01-01") >= 0) {
						if (strcmp(fields[tpch_field_shipdate], "1996-12-31") <= 0) {
							find = true;
						}
					}
				}
				// 10
				// $"l_returnflag" === "R"
				if (tpch_num == 10) {
					// Q14
					if (strcmp(fields[tpch_field_returnflag], "R") == 0) {
						find = true;
					}
				}

				// 12
				// $"l_shipmode" === "MAIL" || $"l_shipmode" === "SHIP") && $"l_commitdate" < $"l_receiptdate" && $"l_shipdate" < $"l_commitdate" && $"l_receiptdate" >= "1994-01-01" && $"l_receiptdate" < "1995-01-01"
				if (tpch_num == 12) {
					if ((strcmp(fields[tpch_field_shipmode], "MAIL") == 0) ||
						(strcmp(fields[tpch_field_shipmode], "SHIP") == 0)) {
						if (strcmp(fields[tpch_field_commitdate], fields[tpch_field_receiptdate]) < 0) {
							if (strcmp(fields[tpch_field_shipdate], fields[tpch_field_commitdate]) < 0) {
								if (strcmp(fields[tpch_field_receiptdate], "1994-01-01") >= 0) {
									if (strcmp(fields[tpch_field_receiptdate], "1995-01-01") < 0) {
										find = true;
									}
								}
							}
						}
					}
				}

				// 14
				// $"l_shipdate" >= "1995-09-01" && $"l_shipdate" < "1995-10-01"
				if (tpch_num == 14) {
					// Q14
					if (strcmp(fields[tpch_field_shipdate], "1995-10-01") < 0) {
						if (strcmp(fields[tpch_field_shipdate], "1995-09-01") >= 0) {
							find = true;
						}
					}
				}

				// 15
				// $"l_shipdate" >= "1996-01-01" && $"l_shipdate" < "1996-04-01")
				if (tpch_num == 15) {
					// Q14
					if (strcmp(fields[tpch_field_shipdate], "1996-04-01") < 0) {
						if (strcmp(fields[tpch_field_shipdate], "1996-01-01") >= 0) {
							find = true;
						}
					}
				}

				// 19
				if (tpch_num == 19) {
					if ((strcmp(fields[tpch_field_shipmode], "AIR") == 0) ||
						(strcmp(fields[tpch_field_shipmode], "AIR REG") == 0)) {
						if (strcmp(fields[tpch_field_shipinstruct], "DELIVER IN PERSON") == 0) {
							find = true;
						}
					}
				}
				// 20
				if (tpch_num == 20) {
					// Q14
					if (strcmp(fields[tpch_field_shipdate], "1995-01-01") < 0) {
						if (strcmp(fields[tpch_field_shipdate], "1994-01-01") >= 0) {
							find = true;
						}
					}
				}
			}

			if (find == true) {
				result_start = result_start + record_offset;
			}

			field_count = 0;
			record_offset = 0;
			row_count = row_count + 1;
		}
	}
	result_start = result_start + record_offset;

	return (result_start - (char *)buf_out);
}

size_t __compaction(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	unsigned int nInput = temp->compaction_params.nInput;
	unsigned int key_size = temp->compaction_params.key_size;
	size_t sstable_size = size / nInput;
	size_t entry_size = sizeof(struct compaction_format);
	size_t sstable_item_count = sstable_size / entry_size;

	printk("compaction is about to begin with %lu %lu %lu\n", size, nInput, key_size);

	struct compaction_format *sstable_one = (struct compaction_format *)buf_in;
	struct compaction_format *sstable_two = (struct compaction_format *)((char *)buf_in + sstable_size);
	struct compaction_format *output_sstable = (struct compaction_format *)((char *)buf_out);

	size_t offset_one = 0;
	size_t offset_two = 0;
	size_t output_offset = 0;
	int cmp_result = 0;

	// Compaction Steps Regarding Demand-CSD
	// Step 1. Check if SLM_PAGE_SIZE amount of data is ready at offset_one and offset_two
	// Step 2. perform compaction and write the result to output_sstable
	// Step 3. Check if any of the offsets exceeded SLM_PAGE_SIZE
	// Step 3-1. If yes, perform get_data_from_ptr for the corresponding SSTable

	size_t physical_offset_one;
	size_t physical_offset_two;
	size_t physical_output_offset;
	while (offset_one < sstable_item_count && offset_two < sstable_item_count) {
		physical_offset_one = offset_one * entry_size;
		physical_offset_two = offset_two * entry_size;
		physical_output_offset = output_offset * entry_size;

		if (physical_offset_one % SLM_PAGE_SIZE == 0) {
			// printk("get data from ptr for sstable one. Current offset status: %lu %lu\n", physical_offset_one, physical_offset_two);

			get_data_from_ptr((size_t)sstable_one + physical_offset_one, SLM_PAGE_SIZE);
		}
		if (physical_offset_two % SLM_PAGE_SIZE == 0) {
			// printk("get data from ptr for sstable two. Current offset status: %lu %lu\n", physical_offset_one, physical_offset_two);

			get_data_from_ptr((size_t)sstable_two + physical_offset_two, SLM_PAGE_SIZE);
		}

		// printk("Notify entry offset: %lu %lu %lu\n", offset_one, offset_two, output_offset);
		cmp_result = memcmp(sstable_one[offset_one].key, sstable_two[offset_two].key, key_size);

		if (cmp_result == 0) {
			// key is identical
			set_data_from_ptr((size_t)output_sstable + physical_output_offset,
							  (size_t)sstable_one + physical_offset_one, entry_size);
			offset_one++;
			offset_two++;
		} else if (cmp_result < 0) {
			// offset_one is smaller
			set_data_from_ptr((size_t)output_sstable + physical_output_offset,
							  (size_t)sstable_one + physical_offset_one, entry_size);
			offset_one++;
		} else {
			// offset_two is smaller
			set_data_from_ptr((size_t)output_sstable + physical_output_offset,
							  (size_t)sstable_two + physical_offset_two, entry_size);
			offset_two++;
		}

		output_offset++;
	}

	if (offset_one == sstable_item_count) {
		while (offset_two < sstable_item_count) {
			physical_offset_two = offset_two * entry_size;
			physical_output_offset = output_offset * entry_size;

			set_data_from_ptr((size_t)output_sstable + physical_output_offset,
							  (size_t)sstable_two + physical_offset_two, entry_size);
			offset_two++;
			output_offset++;
		}
	} else if (offset_two == sstable_item_count) {
		while (offset_one < sstable_item_count) {
			physical_offset_one = offset_one * entry_size;
			physical_output_offset = output_offset * entry_size;

			set_data_from_ptr((size_t)output_sstable + physical_output_offset,
							  (size_t)sstable_one + physical_offset_one, entry_size);
			offset_one++;
			output_offset++;
		}
	}

	return (output_offset) * sizeof(struct compaction_format);
}

size_t __leveldb_finalized_sstable(void *buf_file_out, size_t output_offset, char *index_block_ptr,
								   char *index_block_append_ptr, char *index_restart_ptr,
								   char *index_restart_append_ptr, uint32_t restart_entry_count)
{
	/* File Finalize */
	size_t metaindex_offset, metaindex_size, index_offset, index_size;

	char trailer[kBlockTrailerSize];
	memset(trailer, 0, kBlockTrailerSize);

	// Write metaindex block (empty)
	metaindex_offset = output_offset;
	char metaindex_buf[sizeof(uint32_t) * 2];
	char *metaindex_append_ptr = metaindex_buf;
	metaindex_append_ptr = PutFixed32(metaindex_append_ptr, (uint32_t)0);
	metaindex_append_ptr = PutFixed32(metaindex_append_ptr, (uint32_t)1);
	set_data_from_ptr((size_t)buf_file_out + output_offset, metaindex_buf, metaindex_append_ptr - metaindex_buf);
	metaindex_size = (metaindex_append_ptr - metaindex_buf);
	output_offset += metaindex_size;
	set_crc32c_to_block(metaindex_buf, metaindex_size, trailer);
	set_data_from_ptr((size_t)buf_file_out + output_offset, trailer, kBlockTrailerSize);
	output_offset += kBlockTrailerSize;
	printk("finished metaindex block - %llu, %llu\n", metaindex_offset, metaindex_size);

	// Write index block
	index_offset = output_offset;
	char *index_buf = buf_file_out + output_offset; // need the whole index block including restart for CRC
	set_data_from_ptr((size_t)buf_file_out + output_offset, index_block_ptr, index_block_append_ptr - index_block_ptr);
	output_offset += index_block_append_ptr - index_block_ptr;
	set_data_from_ptr((size_t)buf_file_out + output_offset, index_restart_ptr,
					  index_restart_append_ptr - index_restart_ptr);
	output_offset += index_restart_append_ptr - index_restart_ptr;
	index_size = (index_block_append_ptr - index_block_ptr) + (index_restart_append_ptr - index_restart_ptr);
	set_crc32c_to_block(index_buf, index_size, trailer);
	set_data_from_ptr((size_t)buf_file_out + output_offset, trailer, kBlockTrailerSize);
	output_offset += kBlockTrailerSize;
	printk("finished index block - %llu %llu\n", index_offset, index_size);

	// Write footer
	char footer_buf[48]; // 2 * BlockHandle::kMaxEncodedLength(20) + magicnumber
	memset(footer_buf, 0, 48);
	char *footer_append_ptr = footer_buf;
	footer_append_ptr = PutVarint64(footer_append_ptr, metaindex_offset);
	footer_append_ptr = PutVarint64(footer_append_ptr, metaindex_size);
	footer_append_ptr = PutVarint64(footer_append_ptr, index_offset);
	footer_append_ptr = PutVarint64(footer_append_ptr, index_size);
	footer_append_ptr = footer_buf + 40; // 2 * kMaxEncodedLength(20)
	footer_append_ptr = PutFixed32(footer_append_ptr, (uint32_t)(kTableMagicNumber & 0xffffffffu));
	footer_append_ptr = PutFixed32(footer_append_ptr, (uint32_t)(kTableMagicNumber >> 32));
	set_data_from_ptr((size_t)buf_file_out + output_offset, footer_buf, 48);
	output_offset += 48;

	printk("finish this file with size: %lu\n", output_offset);

	return output_offset;
}

void __leveldb_handle_index_block(struct decode_info *index_info, char **index_block_append_ptr,
								  char **index_restart_append_ptr, uint32_t *restart_entry_count, size_t output_offset,
								  size_t indexblock_offset, size_t block_size)
{
	/* Index block entry handling */
	char handle_encoding[20];
	char *handle_encoding_ptr = handle_encoding;
	handle_encoding_ptr = PutVarint64(handle_encoding_ptr, output_offset);
	handle_encoding_ptr =
		PutVarint64(handle_encoding_ptr,
					(block_size - kBlockTrailerSize)); // block size includes kBlockTrailer which is not the entry size
	int index_value_size = handle_encoding_ptr - handle_encoding;

	*index_block_append_ptr = PutVarint32(*index_block_append_ptr, index_info->shared);
	*index_block_append_ptr = PutVarint32(*index_block_append_ptr, index_info->non_shared);
	*index_block_append_ptr = PutVarint32(*index_block_append_ptr, index_value_size);
	memcpy(*index_block_append_ptr, index_info->key, index_info->non_shared);
	*index_block_append_ptr += index_info->non_shared;
	memcpy(*index_block_append_ptr, handle_encoding, index_value_size);
	*index_block_append_ptr += index_value_size;

	// restart about index block should use indexblock_offset, not output_offset
	*index_restart_append_ptr = PutFixed32(*index_restart_append_ptr, indexblock_offset);
	(*restart_entry_count)++;

	return;
}

size_t __leveldb_compaction(void *buf_in, void *buf_out, size_t size, void *param)
{
	size_t result = 0;
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	struct decode_info info_first, info_second;
	int cmp_result = 0;
	int pid = temp->profile_info.pid;
	int host_id = temp->profile_info.host_id;
	size_t sstable_size = temp->leveldb_compaction_params.sstable_size;
	size_t datablock_threshold = temp->leveldb_compaction_params.datablock_threshold;
	bool crc_flag = temp->leveldb_compaction_params.crc_flag;

	char *first_level = (char *)buf_in;
	char *second_level = first_level + temp->leveldb_compaction_params.second_level_start;

	char *first_limit = first_level + temp->leveldb_compaction_params.first_level_size;
	char *second_limit = second_level + temp->leveldb_compaction_params.second_level_size;

	char *first_level_starting_offset = first_level; // for SLM request timing calculation
	char *second_level_starting_offset = second_level; // for SLM request timing calculation
	size_t output_offset = 0;

	char *tmp_first;
	char *tmp_second;

	size_t *file_size = (size_t *)((char *)buf_out + sizeof(int));
	struct compacted_file_metadata *meta =
		(struct compacted_file_metadata *)((char *)buf_out + sizeof(int) + sizeof(size_t) * MAX_OUTPUT_TABLES);
	int file_size_count = 0;
	void *buf_file_meta = buf_out;
	buf_out = buf_out + FRONT_METADATA_SIZE; // room for file size metadata

	void *buf_file_out = buf_out;
	size_t block_size = 0;
	char *index_block_ptr = kmalloc_node(1024 * 1024, GFP_KERNEL, 1);
	char *index_block_append_ptr = index_block_ptr;
	struct decode_info *index_info;
	char *index_restart_ptr = kmalloc_node(1024 * 1024, GFP_KERNEL, 1);
	char *index_restart_append_ptr = index_restart_ptr;
	uint32_t restart_entry_count = 0;

	char trailer[kBlockTrailerSize];
	memset(trailer, 0, kBlockTrailerSize);

	NVMEV_CSD_PROFILE_REAL_START(pid, host_id);
	printk("LevelDB compaction is about to begin\n");
	printk("%llu %llu %llu(%llu)\n", temp->leveldb_compaction_params.first_level_size,
		   temp->leveldb_compaction_params.second_level_size, sstable_size, datablock_threshold);

	/* 
	 * Due to encoding, entry size might not be aligned with SLM_PAGE_SIZE.
	 * Therefore, we will fetch one next SLM page before accessing the SLM page.
	 */
	check_data_using_ptr(first_level, SLM_PAGE_SIZE, pid, host_id);
	check_data_using_ptr(second_level, SLM_PAGE_SIZE, pid, host_id);

	size_t first_level_slm_page_num = get_slm_offset(first_level) / SLM_PAGE_SIZE; // SLM loaded page number
	size_t second_level_slm_page_num = get_slm_offset(second_level) / SLM_PAGE_SIZE;
	size_t last_first_level_slm_page_num = get_slm_offset(first_limit) / SLM_PAGE_SIZE;
	size_t last_second_level_slm_page_num = get_slm_offset(second_limit) / SLM_PAGE_SIZE;

	/* Base on the notion that we know we can skip the BlockTrailer */
	while (first_level < first_limit && second_level < second_limit) {
		tmp_first = first_level;
		tmp_second = second_level;

		if ((get_slm_offset(first_level) / SLM_PAGE_SIZE) + 1 != first_level_slm_page_num &&
			first_level_slm_page_num < last_first_level_slm_page_num) {
			check_data_using_ptr((size_t)first_level + SLM_PAGE_SIZE, SLM_PAGE_SIZE, pid, host_id);
			first_level_slm_page_num++;
		}
		if ((get_slm_offset(second_level) / SLM_PAGE_SIZE) + 1 != second_level_slm_page_num &&
			second_level_slm_page_num < last_second_level_slm_page_num) {
			check_data_using_ptr((size_t)second_level + SLM_PAGE_SIZE, SLM_PAGE_SIZE, pid, host_id);
			second_level_slm_page_num++;
		}

		tmp_first =
			DecodeEntry(tmp_first, first_limit, &info_first.shared, &info_first.non_shared, &info_first.value_length);
		memcpy(info_first.key, tmp_first, info_first.non_shared);
		tmp_first =
			tmp_first + info_first.non_shared + info_first.value_length + 8 + kBlockTrailerSize; // Move to next block

		tmp_second = DecodeEntry(tmp_second, second_limit, &info_second.shared, &info_second.non_shared,
								 &info_second.value_length);
		memcpy(info_second.key, tmp_second, info_second.non_shared);
		tmp_second = tmp_second + info_second.non_shared + info_second.value_length + 8 +
					 kBlockTrailerSize; // Move to next block

		if (info_first.non_shared != info_second.non_shared) {
			NVMEV_ERROR("Key length mismatch: %d %d\n", info_first.non_shared, info_second.non_shared);
			BUG();
		}
		cmp_result = memcmp(info_first.key, info_second.key, (info_first.non_shared - 8)); // internal key = key + 8

		/* Copy KV pair (except CRC) depending on the cmp_result */
		if (cmp_result == 0) {
			// key is identical
			block_size = (tmp_first - first_level);
			set_data_from_ptr((size_t)buf_file_out + output_offset, (size_t)first_level,
							  (block_size - kBlockTrailerSize));
			index_info = &info_first;

			first_level = tmp_first;
			second_level = tmp_second;
		} else if (cmp_result < 0) {
			// level 1 is smaller
			block_size = (tmp_first - first_level);
			set_data_from_ptr((size_t)buf_file_out + output_offset, (size_t)first_level,
							  (block_size - kBlockTrailerSize));
			index_info = &info_first;

			first_level = tmp_first;
		} else {
			// level 2 is smaller
			block_size = (tmp_second - second_level);
			set_data_from_ptr((size_t)buf_file_out + output_offset, (size_t)second_level,
							  (block_size - kBlockTrailerSize));
			index_info = &info_second;

			second_level = tmp_second;
		}

		if (output_offset == 0) {
			memcpy(meta[file_size_count].smallest_key, index_info->key, index_info->non_shared); // smallest Key
			meta[file_size_count].smallest_klen = index_info->non_shared;
		}

		__leveldb_handle_index_block(index_info, &index_block_append_ptr, &index_restart_append_ptr,
									 &restart_entry_count, output_offset, (index_block_append_ptr - index_block_ptr),
									 block_size);

		/* Calculate CRC */
		if (crc_flag) {
			set_crc32c_to_block((char *)buf_file_out + output_offset, (block_size - kBlockTrailerSize), trailer);
		}
		set_data_from_ptr((size_t)buf_file_out + output_offset + (block_size - kBlockTrailerSize), trailer,
						  kBlockTrailerSize);

		output_offset += block_size;

		if (output_offset >= datablock_threshold) {
			memcpy(meta[file_size_count].largest_key, index_info->key, index_info->non_shared); // largest Key
			meta[file_size_count].largest_klen = index_info->non_shared;
			meta[file_size_count].datablock_size = output_offset;

			index_restart_append_ptr =
				PutFixed32(index_restart_append_ptr, restart_entry_count); // append entry count to index_restart
			printk("data block size: %lu\n", output_offset);
			printk("index block size: %lu\n", index_block_append_ptr - index_block_ptr);
			printk("index restart size: %lu (entry: %d)\n", index_restart_append_ptr - index_restart_ptr,
				   restart_entry_count);

			output_offset = __leveldb_finalized_sstable(buf_file_out, output_offset, index_block_ptr,
														index_block_append_ptr, index_restart_ptr,
														index_restart_append_ptr, restart_entry_count);

			if (output_offset > sstable_size) {
				NVMEV_ERROR("BUG!!! Large SSTable size\n");
				BUG();
			}

			// // Fill padding until max_file_size, No need to actually perform zero-padding
			notify_if_slm_data_ready((size_t)buf_file_out + output_offset, sstable_size - output_offset);

			// Reset metadata
			file_size[file_size_count++] = output_offset;
			buf_file_out = buf_out + sstable_size * file_size_count;
			output_offset = 0;
			memset(index_block_ptr, 0, 1024 * 1024);
			index_block_append_ptr = index_block_ptr;
			memset(index_restart_ptr, 0, 1024 * 1024);
			index_restart_append_ptr = index_restart_ptr;
			restart_entry_count = 0;
		}
	}

	if (second_level < second_limit) {
		first_level = second_level;
		first_limit = second_limit;
		first_level_slm_page_num = second_level_slm_page_num;
		last_first_level_slm_page_num = last_second_level_slm_page_num;
	}

	while (first_level < first_limit) {
		tmp_first = first_level;

		if ((get_slm_offset(first_level) / SLM_PAGE_SIZE) + 1 != first_level_slm_page_num &&
			first_level_slm_page_num < last_first_level_slm_page_num) {
			check_data_using_ptr((size_t)first_level + SLM_PAGE_SIZE, SLM_PAGE_SIZE, pid, host_id);
			first_level_slm_page_num++;
		}

		tmp_first =
			DecodeEntry(tmp_first, first_limit, &info_first.shared, &info_first.non_shared, &info_first.value_length);
		memcpy(info_first.key, tmp_first, info_first.non_shared);
		tmp_first = tmp_first + info_first.non_shared + info_first.value_length + 8 + kBlockTrailerSize;

		set_data_from_ptr((size_t)buf_file_out + output_offset, (size_t)first_level, (tmp_first - first_level));
		block_size = (tmp_first - first_level);
		index_info = &info_first;
		first_level = tmp_first;

		if (output_offset == 0) {
			memcpy(meta[file_size_count].smallest_key, index_info->key, index_info->non_shared); // smallest Key
			meta[file_size_count].smallest_klen = index_info->non_shared;
		}

		__leveldb_handle_index_block(index_info, &index_block_append_ptr, &index_restart_append_ptr,
									 &restart_entry_count, output_offset, (index_block_append_ptr - index_block_ptr),
									 block_size);

		/* Calculate CRC */
		if (crc_flag) {
			set_crc32c_to_block((char *)buf_file_out + output_offset, (block_size - kBlockTrailerSize), trailer);
		}
		set_data_from_ptr((size_t)buf_file_out + output_offset + (block_size - kBlockTrailerSize), trailer,
						  kBlockTrailerSize);

		output_offset += block_size;

		if (output_offset >= datablock_threshold) {
			memcpy(meta[file_size_count].largest_key, index_info->key, index_info->non_shared); // largest Key
			meta[file_size_count].largest_klen = index_info->non_shared;
			meta[file_size_count].datablock_size = output_offset;

			index_restart_append_ptr =
				PutFixed32(index_restart_append_ptr, restart_entry_count); // append entry count to index_restart
			printk("data block size: %lu\n", output_offset);
			printk("index block size: %lu\n", index_block_append_ptr - index_block_ptr);
			printk("index restart size: %lu (entry: %d)\n", index_restart_append_ptr - index_restart_ptr,
				   restart_entry_count);

			output_offset = __leveldb_finalized_sstable(buf_file_out, output_offset, index_block_ptr,
														index_block_append_ptr, index_restart_ptr,
														index_restart_append_ptr, restart_entry_count);

			if (output_offset > sstable_size) {
				NVMEV_ERROR("BUG!!! Large SSTable size\n");
			}

			// // Fill padding until max_file_size, No need to actually perform zero-padding
			notify_if_slm_data_ready((size_t)buf_file_out + output_offset, sstable_size - output_offset);

			// Reset metadata
			file_size[file_size_count++] = output_offset;
			buf_file_out = buf_out + sstable_size * file_size_count;
			output_offset = 0;
			memset(index_block_ptr, 0, 1024 * 1024);
			index_block_append_ptr = index_block_ptr;
			memset(index_restart_ptr, 0, 1024 * 1024);
			index_restart_append_ptr = index_restart_ptr;
			restart_entry_count = 0;
		}
	}

	if (output_offset > 0) {
		memcpy(meta[file_size_count].largest_key, index_info->key, index_info->non_shared); // largest Key
		meta[file_size_count].largest_klen = index_info->non_shared;
		meta[file_size_count].datablock_size = output_offset;

		index_restart_append_ptr =
			PutFixed32(index_restart_append_ptr, restart_entry_count); // append entry count to index_restart
		printk("data block size: %lu\n", output_offset);
		printk("index block size: %lu\n", index_block_append_ptr - index_block_ptr);
		printk("index restart size: %lu (entry: %d)\n", index_restart_append_ptr - index_restart_ptr,
			   restart_entry_count);

		output_offset = __leveldb_finalized_sstable(buf_file_out, output_offset, index_block_ptr,
													index_block_append_ptr, index_restart_ptr, index_restart_append_ptr,
													restart_entry_count);

		file_size[file_size_count++] = output_offset;
	}

	kfree(index_block_ptr);
	kfree(index_restart_ptr);

	memcpy(buf_file_meta, &file_size_count, sizeof(int));
	notify_if_slm_data_ready((size_t)buf_file_meta, SLM_PAGE_SIZE);

	NVMEV_CSD_PROFILE_REAL_END(pid, host_id);

	for (int i = 0; i < file_size_count; i++)
		printk("%d - %llu\n", i, file_size[i]);

	size_t total_io_size = 0;
	if (output_offset > 0) {
		// last file is small
		total_io_size = ((file_size_count - 1) * sstable_size) + file_size[file_size_count - 1];
	} else {
		total_io_size = ((file_size_count)*sstable_size);
	}

#if (SUPPORT_ASYNC_COMPUTE == 1)
	total_io_size += FRONT_METADATA_SIZE;
#endif

	return total_io_size;
}

size_t __crc_calculation(void *buf_in, void *buf_out, size_t size, void *param)
{
	struct CSD_PARAMS *temp = (struct CSD_PARAMS *)param;
	int pid = temp->profile_info.pid;
	int host_id = temp->profile_info.host_id;
	size_t sstable_size = temp->leveldb_crc_params.sstable_size;
	size_t datablock_threshold = temp->leveldb_crc_params.datablock_threshold;
	int num_cores = temp->leveldb_crc_params.num_cores;
	int core_id = temp->leveldb_crc_params.core_id;
	size_t consumed_offset = 0;
	size_t output_offset = 0;
	struct decode_info entry_info;
	size_t block_size;
	size_t nentry = 0;

	NVMEV_CSD_PROFILE_REAL_START(pid, host_id);

	char *input_start = (char *)buf_in + FRONT_METADATA_SIZE;
	char *output_start = (char *)buf_out + FRONT_METADATA_SIZE;
	char *input_ptr = input_start;
	char *output_ptr = output_start;
	char *input_limit = input_ptr + size;
	char *tmp_input_ptr;

	char trailer[kBlockTrailerSize];
	memset(trailer, 0, kBlockTrailerSize);

	check_data_using_ptr(input_ptr, SLM_PAGE_SIZE, pid, host_id);

	size_t input_ptr_slm_page_num = get_slm_offset(input_ptr) / SLM_PAGE_SIZE; // SLM loaded page number
	size_t last_input_ptr_slm_page_num = get_slm_offset(input_limit) / SLM_PAGE_SIZE;

	while (input_ptr < input_limit) {
		tmp_input_ptr = input_ptr;

		if ((get_slm_offset(input_ptr) / SLM_PAGE_SIZE) + 1 != input_ptr_slm_page_num &&
			input_ptr_slm_page_num < last_input_ptr_slm_page_num) {
			if (check_data_using_ptr((size_t)input_ptr + SLM_PAGE_SIZE, SLM_PAGE_SIZE, pid, host_id) == 0) {
				last_input_ptr_slm_page_num = input_ptr_slm_page_num; // current page is the last page

				/* Get final page leftovers and set the input_limit */
				size_t leftover = get_slm_last_page_leftover(input_ptr);

				input_limit = get_slm_addr(last_input_ptr_slm_page_num * SLM_PAGE_SIZE) + leftover;
				printk("[core%d] finalized input total size is: %llu (%llu, %llu)", core_id,
					   (input_limit - input_start), input_limit, input_start);

				if (!(input_ptr < input_limit)) {
					break;
				}
			}
			input_ptr_slm_page_num++;
		}

		tmp_input_ptr = DecodeEntry(tmp_input_ptr, input_limit, &entry_info.shared, &entry_info.non_shared,
									&entry_info.value_length);
		tmp_input_ptr = tmp_input_ptr + entry_info.non_shared + entry_info.value_length + 8 +
						kBlockTrailerSize; // Move to next block

		block_size = (tmp_input_ptr - input_ptr);
		if (block_size < 1024) {
			printk("[core%d] Wrong decode result: output_offset: %llu, input_offset: %llu\n", core_id, output_offset,
				   (input_ptr - input_start));
			break;
		}

		if (nentry % num_cores == core_id) {
			// printk("[core%d] calculate CRC for %lu ~ %lu\n", core_id, output_offset, output_offset + (block_size - kBlockTrailerSize));

			set_crc32c_to_block(input_ptr, (block_size - kBlockTrailerSize), trailer);

			set_data_from_ptr((size_t)output_ptr + output_offset, (size_t)input_ptr, (block_size - kBlockTrailerSize));
			set_data_from_ptr((size_t)output_ptr + output_offset + (block_size - kBlockTrailerSize), trailer,
							  kBlockTrailerSize);
		}

		input_ptr = tmp_input_ptr;
		output_offset += block_size;
		nentry++;

		if (output_offset >= datablock_threshold) {
			if (nentry % num_cores == core_id) {
				/* copy metaindex, index, footer */
				check_data_using_ptr((size_t)input_ptr, (sstable_size - output_offset), pid, host_id);
				set_data_from_ptr((size_t)output_ptr + output_offset, (size_t)input_ptr,
								  (sstable_size - output_offset));
				printk("[core%d] copy meta at %lu, %lu\n", core_id, output_offset, (sstable_size - output_offset));
			}
			input_ptr += (sstable_size - output_offset);
			output_ptr += sstable_size;
			printk("[core%d] output_offset: %lu, input_offset: %lu\n", core_id, output_offset,
				   (input_ptr - input_start));

			output_offset = 0;
			nentry = 0;
			input_ptr_slm_page_num = get_slm_offset(input_ptr) / SLM_PAGE_SIZE; // update slm page num
		}
	}

	printk("[core%d] break from while loop %llu %llu\n", core_id, output_offset, (input_ptr - input_start));

	if ((core_id == 0) && (input_ptr < input_limit)) {
		printk("[core%d] copy leftovers %llu\n", core_id, (input_limit - input_ptr));
		check_data_using_ptr((size_t)input_ptr, (input_limit - input_ptr), pid, host_id);
		set_data_from_ptr((size_t)output_ptr + output_offset, (size_t)input_ptr, (input_limit - input_ptr));
	}

	/* Copy FRONT_METADATA_SIZE */
	memcpy(buf_out, buf_in, FRONT_METADATA_SIZE);

	NVMEV_CSD_PROFILE_REAL_END(pid, host_id);

	return (input_limit - (char *)buf_in);
}
