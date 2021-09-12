/*
    This file is part of duckOS.

    duckOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    duckOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with duckOS.  If not, see <https://www.gnu.org/licenses/>.

    Copyright (c) Byteduck 2016-2021. All rights reserved.
*/

#ifndef DUCKOS_DISKDEVICE_H
#define DUCKOS_DISKDEVICE_H

#include <kernel/time/Time.h>
#include <kernel/memory/LinkedMemoryRegion.h>
#include <kernel/memory/MemoryManager.h>
#include "BlockDevice.h"

class DiskDevice: public BlockDevice {
public:
	DiskDevice(unsigned major, unsigned minor): BlockDevice(major, minor) {}
	~DiskDevice();

	Result read_blocks(uint32_t block, uint32_t count, uint8_t *buffer) override final;
	Result write_blocks(uint32_t block, uint32_t count, const uint8_t *buffer) override final;

	virtual Result read_uncached_blocks(uint32_t block, uint32_t count, uint8_t *buffer) = 0;
	virtual Result write_uncached_blocks(uint32_t block, uint32_t count, const uint8_t *buffer) = 0;

	static size_t used_cache_memory();

private:
	class BlockCacheEntry {
	public:
		BlockCacheEntry() = default;
		BlockCacheEntry(size_t block, size_t size, uint8_t* data);
		size_t block = 0;
		size_t size = 0;
		uint8_t* data = nullptr;
		Time last_used = Time::now();
		bool dirty = false;
	};

	class BlockCacheRegion {
	public:
		explicit BlockCacheRegion(size_t start_block, size_t block_size);
		~BlockCacheRegion();
		ResultRet<BlockCacheEntry*> get_cached_block(size_t block);
		inline size_t num_blocks() { return PAGE_SIZE / block_size; }
		inline uint8_t* block_data(int index) { return (uint8_t*) (region.virt->start + block_size * index); }

		LinkedMemoryRegion region;
		kstd::vector<BlockCacheEntry> cached_blocks;
		size_t block_size;
		size_t start_block;
	};

	BlockCacheEntry* get_cache_entry(size_t block);
	inline size_t blocks_per_cache_region() { return PAGE_SIZE / block_size(); }
	inline size_t block_cache_region_start(size_t block) { return block - (block % blocks_per_cache_region()); }

	//TODO: Use a map for keeping track of cache regions (when implemented)
	//TODO: Free cache regions when low on memory
	kstd::vector<BlockCacheRegion*> _cache_regions;
	SpinLock _cache_lock;

	static size_t _used_cache_memory;
};

#endif //DUCKOS_DISKDEVICE_H
