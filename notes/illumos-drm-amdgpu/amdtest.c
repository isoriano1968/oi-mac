/*
 * amdtest: minimal amdgpu user-space test for the Mac Pro 2013 bring-up
 * (test a: open, version, device init, queries, close; no rendering).
 * Every step is printed unbuffered before it runs, so a streamed trace
 * shows the last step reached when the machine panics.
 *
 * Build: gcc -O1 -g -o amdtest amdtest.c -I/usr/include/libdrm \
 *   -L/usr/lib/xorg/amd64 -R/usr/lib/xorg/amd64 -ldrm_amdgpu -ldrm
 * Run:   ./amdtest [/dev/dri/renderD128] [steps] [seconds]
 *   steps: o = open+version only, q = + device init and queries,
 *   c = + deinit and close (default "c"), s = + sensors (clocks, temperature,
 *   power, load) once a second for [seconds], then deinit and close,
 *   r = + SI dpm registers (needs patch 0021): raw values, then per second
 *   GPU busy samples and the SMC's current/target level for [seconds],
 *   d = + the defined CG registers (needs patch 0022, its second version) on stdout,
 *   as si-pm-regdump.py prints it on Linux, sampled for [seconds],
 *   g = + the GPIO pads (needs patch 0027) with GRBM_STATUS and the current
 *   level, once a second for [seconds]; bits 5 (AC/DC) and 6 (VR hot).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>

static void
step(const char *s)
{
	fprintf(stderr, "amdtest: %s\n", s);
}

int
main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	char mode = argc > 2 ? argv[2][0] : 'c';
	amdgpu_device_handle dev;
	uint32_t maj, min;
	struct amdgpu_gpu_info gi;
	struct drm_amdgpu_info_vram_gtt vg;
	drmVersionPtr v;
	int fd, r;

	setvbuf(stderr, NULL, _IONBF, 0);
	step("open");
	fd = open(path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror(path);
		return (1);
	}
	step("drmGetVersion");
	v = drmGetVersion(fd);
	if (v == NULL) {
		step("drmGetVersion failed");
		return (1);
	}
	fprintf(stderr, "amdtest: driver %s %d.%d.%d (%s)\n", v->name,
	    v->version_major, v->version_minor, v->version_patchlevel,
	    v->desc);
	drmFreeVersion(v);
	if (mode == 'o') {
		step("close (after open only)");
		close(fd);
		step("done");
		return (0);
	}

	step("amdgpu_device_initialize");
	r = amdgpu_device_initialize(fd, &maj, &min, &dev);
	if (r != 0) {
		fprintf(stderr, "amdtest: device_initialize %d (%s)\n", r,
		    strerror(-r));
		return (1);
	}
	fprintf(stderr, "amdtest: drm interface %u.%u\n", maj, min);

	step("query_gpu_info");
	r = amdgpu_query_gpu_info(dev, &gi);
	if (r == 0)
		fprintf(stderr, "amdtest: asic 0x%x family %u rev %u, "
		    "%u CUs, sclk max %llu kHz, mclk max %llu kHz\n",
		    gi.asic_id, gi.family_id, gi.chip_rev, gi.cu_active_number,
		    (unsigned long long)gi.max_engine_clk,
		    (unsigned long long)gi.max_memory_clk);
	else
		fprintf(stderr, "amdtest: query_gpu_info %d\n", r);

	step("query_info VRAM_GTT");
	r = amdgpu_query_info(dev, AMDGPU_INFO_VRAM_GTT, sizeof (vg), &vg);
	if (r == 0)
		fprintf(stderr, "amdtest: VRAM %llu MB (cpu visible %llu MB), "
		    "GTT %llu MB\n",
		    (unsigned long long)(vg.vram_size >> 20),
		    (unsigned long long)(vg.vram_cpu_accessible_size >> 20),
		    (unsigned long long)(vg.gtt_size >> 20));
	else
		fprintf(stderr, "amdtest: query VRAM_GTT %d\n", r);

	step("marketing name");
	fprintf(stderr, "amdtest: %s\n",
	    amdgpu_get_marketing_name(dev) != NULL ?
	    amdgpu_get_marketing_name(dev) : "(none)");

	if (mode == 'q') {
		step("exit without deinit/close (kernel closes the fd)");
		return (0);
	}
	if (mode == 's') {
		/*
		 * Sensors (power management), sampled once a second for
		 * argv[3] seconds (default 1): engine and memory clock (MHz),
		 * GPU temperature (milli-degrees C), average power (W); a
		 * sensor the kernel does not have prints its error.
		 */
		static const struct {
			uint32_t id;
			const char *name;
		} sens[] = {
			{ AMDGPU_INFO_SENSOR_GFX_SCLK, "sclk MHz" },
			{ AMDGPU_INFO_SENSOR_GFX_MCLK, "mclk MHz" },
			{ AMDGPU_INFO_SENSOR_GPU_TEMP, "temp mC" },
			{ AMDGPU_INFO_SENSOR_GPU_AVG_POWER, "power W" },
			{ AMDGPU_INFO_SENSOR_GPU_LOAD, "load %" },
		};
		int secs = argc > 3 ? atoi(argv[3]) : 1, t, i;

		for (t = 0; t < secs; t++) {
			fprintf(stderr, "amdtest: t=%d", t);
			for (i = 0; i < (int)(sizeof (sens) / sizeof (sens[0]));
			    i++) {
				uint32_t val = 0;

				r = amdgpu_query_sensor_info(dev, sens[i].id,
				    sizeof (val), &val);
				if (r == 0)
					fprintf(stderr, "  %s %u", sens[i].name,
					    val);
				else
					fprintf(stderr, "  %s err %d",
					    sens[i].name, r);
			}
			fprintf(stderr, "\n");
			if (t + 1 < secs)
				sleep(1);
		}
	}
	if (mode == 'r') {
		/*
		 * SI dpm registers (patch 0021 makes them readable), sampled
		 * every 50 ms for argv[3] seconds (default 5): the raw values
		 * once, then per second how often the GPU was busy (GRBM_STATUS
		 * GUI_ACTIVE) and the SMC's current and target level
		 * (TARGET_AND_CURRENT_PROFILE_INDEX bits 7:4 and 3:0).
		 */
		static const struct {
			uint32_t off;
			const char *name;
		} regs[] = {
			{ 0x1E0, "GENERAL_PWRMGT" },
			{ 0x1E2, "SCLK_PWRMGT_CNTL" },
			{ 0x1E6, "TARGET_AND_CURRENT_PROFILE_INDEX" },
			{ 0x1EF, "CG_FTV" },
			{ 0x1FF, "CG_BSP" },
			{ 0x201, "CG_GIT" },
			{ 0x203, "CG_SSP" },
			{ 0x20A, "CG_DISPLAY_GAP_CNTL" },
			{ 0x2004, "GRBM_STATUS" },
		};
		int secs = argc > 3 ? atoi(argv[3]) : 5, t, k, i;
		uint32_t v;

		for (i = 0; i < (int)(sizeof (regs) / sizeof (regs[0])); i++) {
			r = amdgpu_read_mm_registers(dev, regs[i].off, 1,
			    0xffffffff, 0, &v);
			if (r == 0)
				fprintf(stderr, "amdtest: %-34s 0x%08x\n",
				    regs[i].name, v);
			else
				fprintf(stderr, "amdtest: %-34s err %d\n",
				    regs[i].name, r);
		}
		for (t = 0; t < secs; t++) {
			int busy = 0, n = 0, cur[16] = { 0 }, tgt[16] = { 0 };

			for (k = 0; k < 20; k++) {
				uint32_t g = 0, ix = 0;

				if (amdgpu_read_mm_registers(dev, 0x2004, 1,
				    0xffffffff, 0, &g) == 0 &&
				    amdgpu_read_mm_registers(dev, 0x1E6, 1,
				    0xffffffff, 0, &ix) == 0) {
					n++;
					busy += (g >> 31) & 1;
					cur[(ix >> 4) & 0xf]++;
					tgt[ix & 0xf]++;
				}
				usleep(50000);
			}
			fprintf(stderr, "amdtest: t=%d busy %d/%d  current", t,
			    busy, n);
			for (i = 0; i < 16; i++)
				if (cur[i])
					fprintf(stderr, " L%d:%d", i, cur[i]);
			fprintf(stderr, "  target");
			for (i = 0; i < 16; i++)
				if (tgt[i])
					fprintf(stderr, " L%d:%d", i, tgt[i]);
			fprintf(stderr, "\n");
		}
	}
	if (mode == 'd') {
		/*
		 * The SI CG registers patch 0022 makes readable (the ones
		 * smu_6_0_d.h defines; never the undefined offsets between
		 * them, which wedge the block), in the format of
		 * notes/macpro-2013/si-pm-regdump.py on Linux: all once, then
		 * [seconds] * 10 samples 100 ms apart with only those that
		 * changed.
		 */
		static const uint32_t cg[] = {
			0x180, 0x181, 0x182, 0x183, 0x185, 0x186, 0x188, 0x189,
			0x18b, 0x197, 0x198, 0x199, 0x19b, 0x19c, 0x1c0, 0x1c1,
			0x1c2, 0x1c4, 0x1c5, 0x1d5, 0x1d6, 0x1d7, 0x1dc, 0x1dd,
			0x1e0, 0x1e1, 0x1e2, 0x1e6, 0x1ef, 0x1f0, 0x1ff, 0x200,
			0x201, 0x203, 0x20a, 0x21e, 0x21f, 0x221, 0x22e, 0x2004,
		};
#define	NCG	(sizeof (cg) / sizeof (cg[0]))
		static uint32_t prev[NCG], cur[NCG];
		int samples = (argc > 3 ? atoi(argv[3]) : 2) * 10, k, i;

		for (k = -1; k < samples; k++) {
			for (i = 0; i < (int)NCG; i++) {
				cur[i] = 0xdeadbeef;
				(void) amdgpu_read_mm_registers(dev, cg[i], 1,
				    0xffffffff, 0, &cur[i]);
			}
			if (k < 0) {
				for (i = 0; i < (int)NCG; i++)
					printf("0x%04x 0x%08x\n", cg[i], cur[i]);
			} else {
				int n = 0;

				printf("# sample %d:", k);
				for (i = 0; i < (int)NCG; i++) {
					if (cur[i] == prev[i])
						continue;
					printf(" 0x%04x=0x%08x", cg[i], cur[i]);
					n++;
				}
				printf("%s\n", n ? "" : " no change");
			}
			memcpy(prev, cur, sizeof (prev));
			if (k + 1 < samples)
				usleep(100000);
		}
	}
	if (mode == 'g') {
		/*
		 * The GPIO pads patch 0027 makes readable, once a second for
		 * [seconds], with GRBM_STATUS and the SMC's current level: the
		 * SMC watches GPIO 5 (AC/DC, polarity high) and GPIO 6
		 * (regulator hot). Per pad: Y = pin level, A = output value,
		 * EN = output enable, MASK = GPIO mode.
		 */
		static const uint32_t gp[] = { 0x5e5, 0x5e6, 0x5e7, 0x5e8, 0x5e9 };
		static const char *gn[] = { "STRENGTH", "MASK", "A", "EN", "Y" };
		uint32_t v[5], grbm, prof;
		int secs = argc > 3 ? atoi(argv[3]) : 1, k, i;

		for (k = 0; k < secs; k++) {
			printf("# %d:", k);
			for (i = 0; i < 5; i++) {
				v[i] = 0xdeadbeef;
				(void) amdgpu_read_mm_registers(dev, gp[i], 1,
				    0xffffffff, 0, &v[i]);
				printf(" %s=0x%08x", gn[i], v[i]);
			}
			grbm = prof = 0xdeadbeef;
			(void) amdgpu_read_mm_registers(dev, 0x2004, 1,
			    0xffffffff, 0, &grbm);
			(void) amdgpu_read_mm_registers(dev, 0x1E6, 1,
			    0xffffffff, 0, &prof);
			printf(" GRBM_STATUS=0x%08x PROFILE_INDEX=0x%08x\n",
			    grbm, prof);
			printf("#   GPIO5 Y%u A%u EN%u MASK%u   GPIO6 Y%u A%u EN%u "
			    "MASK%u\n", (v[4] >> 5) & 1, (v[2] >> 5) & 1,
			    (v[3] >> 5) & 1, (v[1] >> 5) & 1, (v[4] >> 6) & 1,
			    (v[2] >> 6) & 1, (v[3] >> 6) & 1, (v[1] >> 6) & 1);
			if (k + 1 < secs)
				sleep(1);
		}
	}
	step("amdgpu_device_deinitialize");
	amdgpu_device_deinitialize(dev);
	step("close");
	close(fd);
	step("done");
	return (0);
}
