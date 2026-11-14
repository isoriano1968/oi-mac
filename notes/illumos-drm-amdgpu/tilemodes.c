/* tilemodes: print the SI tiling tables libdrm reads (GB_TILE_MODE0..31) */
#include <stdio.h>
#include <fcntl.h>
#include <xf86drm.h>
#include <amdgpu.h>
int
main(int argc, char **argv)
{
	amdgpu_device_handle dev;
	struct amdgpu_gpu_info gi;
	uint32_t maj, min;
	int fd = open(argc > 1 ? argv[1] : "/dev/dri/card0", O_RDWR), i;

	if (fd < 0 || amdgpu_device_initialize(fd, &maj, &min, &dev) != 0 ||
	    amdgpu_query_gpu_info(dev, &gi) != 0) {
		printf("init failed\n");
		return 1;
	}
	printf("gb_addr_cfg 0x%x  mc_arb_ramcfg 0x%x\n",
	    gi.gb_addr_cfg, gi.mc_arb_ramcfg);
	for (i = 0; i < 32; i++)
		printf("tile_mode[%2d] 0x%08x%s", i, gi.gb_tile_mode[i],
		    (i % 4) == 3 ? "\n" : "  ");
	amdgpu_device_deinitialize(dev);
	return 0;
}
