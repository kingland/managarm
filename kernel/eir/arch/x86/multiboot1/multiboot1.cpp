#include <stdint.h>
#include <frigg/c-support.h>
#include <eir/interface.hpp>
#include <eir-internal/arch.hpp>
#include <eir-internal/generic.hpp>
#include <eir-internal/debug.hpp>

namespace eir {

enum MbInfoFlags {
	kMbInfoPlainMemory = 1,
	kMbInfoBootDevice = 2,
	kMbInfoCommandLine = 4,
	kMbInfoModules = 8,
	kMbInfoFramebuffer = 12,
	kMbInfoSymbols = 16,
	kMbInfoMemoryMap = 32
};

struct MbModule {
	void *startAddress;
	void *endAddress;
	char *string;
	uint32_t reserved;
};

struct MbInfo {
	uint32_t flags;
	uint32_t memLower;
	uint32_t memUpper;
	uint32_t bootDevice;
	char *commandLine;
	uint32_t numModules;
	MbModule *modulesPtr;
	uint32_t numSymbols;
	uint32_t symbolSize;
	void *symbolsPtr;
	uint32_t stringSection;
	uint32_t memoryMapLength;
	void *memoryMapPtr;
	uint32_t padding[9];
	uint64_t fbAddress;
	uint32_t fbPitch;
	uint32_t fbWidth;
	uint32_t fbHeight;
	uint8_t fbBpp;
	uint8_t fbType;
	uint8_t colorInfo[6];
};

struct MbMemoryMap {
	uint32_t size;
	uint64_t baseAddress;
	uint64_t length;
	uint32_t type;
};

extern "C" void eirEnterKernel(uintptr_t, uint64_t, uint64_t);

extern "C" void eirMultiboot1Main(uint32_t info, uint32_t magic){
	if(magic != 0x2BADB002)
		eir::panicLogger() << "eir: Invalid multiboot1 signature, halting..." << frg::endlog;

	MbInfo* mb_info = reinterpret_cast<MbInfo*>(info);

	if(mb_info->flags & kMbInfoFramebuffer) {
		if(mb_info->fbAddress + mb_info->fbWidth * mb_info->fbPitch >= UINTPTR_MAX) {
			eir::infoLogger() << "eir: Framebuffer outside of addressable memory!"
					<< frg::endlog;
		}else if(mb_info->fbBpp != 32) {
			eir::infoLogger() << "eir: Framebuffer does not use 32 bpp!"
					<< frg::endlog;
		}else{
			setFbInfo(reinterpret_cast<void *>(mb_info->fbAddress), mb_info->fbWidth,
					mb_info->fbHeight, mb_info->fbPitch);
		}
	}

	initProcessorEarly();

	// Make sure we do not trash ourselfs or our boot modules.
	bootMemoryLimit = (uintptr_t)&eirImageCeiling;

	if((mb_info->flags & kMbInfoModules) != 0) {
		for(unsigned int i = 0; i < mb_info->numModules; i++) {
			uintptr_t ceil = (uintptr_t)mb_info->modulesPtr[i].endAddress;
			if(ceil > bootMemoryLimit)
				bootMemoryLimit = ceil;
		}
	}

	bootMemoryLimit = (bootMemoryLimit + address_t(pageSize - 1))
			& ~address_t(pageSize - 1);

	// Walk the memory map and retrieve all useable regions.
	assert(mb_info->flags & kMbInfoMemoryMap);
	eir::infoLogger() << "Memory map:" << frg::endlog;
	for(size_t offset = 0; offset < mb_info->memoryMapLength; ) {
		auto map = (MbMemoryMap *)((uintptr_t)mb_info->memoryMapPtr + offset);

		eir::infoLogger() << "    Type " << map->type << " mapping."
				<< " Base: 0x" << frg::hex_fmt{map->baseAddress}
				<< ", length: 0x" << frg::hex_fmt{map->length} << frg::endlog;

		offset += map->size + 4;
	}

	for(size_t offset = 0; offset < mb_info->memoryMapLength; ) {
		auto map = (MbMemoryMap *)((uintptr_t)mb_info->memoryMapPtr + offset);

		if(map->type == 1)
			createInitialRegion(map->baseAddress, map->length);

		offset += map->size + 4;
	}
	setupRegionStructs();

	eir::infoLogger() << "Kernel memory regions:" << frg::endlog;
	for(size_t i = 0; i < numRegions; ++i) {
		if(regions[i].regionType == RegionType::null)
			continue;
		eir::infoLogger() << "    Memory region [" << i << "]."
				<< " Base: 0x" << frg::hex_fmt{regions[i].address}
				<< ", length: 0x" << frg::hex_fmt{regions[i].size} << frg::endlog;
		if(regions[i].regionType == RegionType::allocatable)
			eir::infoLogger() << "        Buddy tree at 0x" << frg::hex_fmt{regions[i].buddyTree}
					<< ", overhead: 0x" << frg::hex_fmt{regions[i].buddyOverhead}
					<< frg::endlog;
	}

	assert((mb_info->flags & kMbInfoModules) != 0);
	assert(mb_info->numModules >= 2);
	MbModule *kernel_module = &mb_info->modulesPtr[0];

	uint64_t kernel_entry = 0;
	initProcessorPaging((void *)kernel_module->startAddress, kernel_entry);

	assert(mb_info->flags & kMbInfoCommandLine);
	auto *info_ptr = generateInfo(mb_info->commandLine);

	auto modules = bootAlloc<EirModule>(mb_info->numModules - 1);
	for(size_t i = 0; i < mb_info->numModules - 1; i++) {
		MbModule &image_module = mb_info->modulesPtr[i + 1];
		modules[i].physicalBase = (EirPtr)image_module.startAddress;
		modules[i].length = (EirPtr)image_module.endAddress
				- (EirPtr)image_module.startAddress;

		size_t name_length = strlen(image_module.string);
		char *name_ptr = bootAlloc<char>(name_length);
		memcpy(name_ptr, image_module.string, name_length);
		modules[i].namePtr = mapBootstrapData(name_ptr);
		modules[i].nameLength = name_length;
	}

	info_ptr->numModules = mb_info->numModules - 1;
	info_ptr->moduleInfo = mapBootstrapData(modules);
	
	if(mb_info->flags & kMbInfoFramebuffer) {
		auto framebuf = &info_ptr->frameBuffer;
		framebuf->fbAddress = mb_info->fbAddress;
		framebuf->fbPitch = mb_info->fbPitch;
		framebuf->fbWidth = mb_info->fbWidth;
		framebuf->fbHeight = mb_info->fbHeight;
		framebuf->fbBpp = mb_info->fbBpp;
		framebuf->fbType = mb_info->fbType;

		assert(mb_info->fbAddress & ~(pageSize - 1));
		for(address_t pg = 0; pg < mb_info->fbPitch * mb_info->fbHeight; pg += 0x1000)
			mapSingle4kPage(0xFFFF'FE00'4000'0000 + pg, mb_info->fbAddress + pg,
					PageFlags::write, CachingMode::writeCombine);
		mapKasanShadow(0xFFFF'FE00'4000'0000, mb_info->fbPitch * mb_info->fbHeight);
		unpoisonKasanShadow(0xFFFF'FE00'4000'0000, mb_info->fbPitch * mb_info->fbHeight);
		framebuf->fbEarlyWindow = 0xFFFF'FE00'4000'0000;
	}

	eir::infoLogger() << "Leaving Eir and entering the real kernel" << frg::endlog;

	eirEnterKernel(eirPml4Pointer, kernel_entry,
			0xFFFF'FE80'0001'0000);  
}

} // namespace eir
