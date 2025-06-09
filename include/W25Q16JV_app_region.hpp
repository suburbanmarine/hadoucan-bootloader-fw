#include "lfs_int_w25q16jv.hpp"

class W25Q16JV_app_region : public lfs_int_w25q16jv
{
public:

	size_t get_start_bytes() override
	{
		return 0;
	}
	size_t get_len_bytes() override
	{
		// total size in bytes 2*1024*1024;
		// total block64 32*64k;

		//assign 75% or 1536kB to this region

		return 24*64*1024;
	}
};