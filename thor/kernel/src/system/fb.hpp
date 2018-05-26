#ifndef THOR_SRC_SYSTEM_FB
#define THOR_SRC_SYSTEM_FB

#include "../generic/usermem.hpp"

namespace thor {

struct FbInfo {
	uint64_t address;
	uint64_t pitch;
	uint64_t width;
	uint64_t height;
	uint64_t bpp;
	uint64_t type;
	
	void *window;
	frigg::SharedPtr<Memory> memory;
};

void initializeFb(uint64_t address, uint64_t pitch, uint64_t width,
		uint64_t height, uint64_t bpp, uint64_t type);

} // namespace thor

#endif // THOR_SRC_SYSTEM_FB
