#!/usr/sbin/dtrace -qs
/*
 * Trace the close path of a DRM file on the Mac Pro (amdgpu bring-up).
 * Silent until the target process calls close(2); from then on every
 * function entry in amdgpu, drm_core, drm_kms, drm_helpers and linuxkpi,
 * from any thread (work queues too), plus every return value of amdgpu's.
 * Stream it off the machine, it may not survive:
 *   ssh -tt mac 'sudo -n dtrace -qs close-trace.d -c "./amdtest /dev/dri/renderD128 o"'
 */
#pragma D option switchrate=1000hz
#pragma D option bufsize=16m
#pragma D option dynvarsize=8m

BEGIN { go = 0; printf("trace start %d\n", timestamp); }

syscall::close:entry
/pid == $target/
{
	go = 1;
	printf("%d %d close(%d)\n", timestamp, cpu, arg0);
}

fbt:amdgpu::entry,
fbt:drm_core::entry,
fbt:drm_kms::entry,
fbt:drm_helpers::entry,
fbt:linuxkpi::entry
/go/
{
	printf("%d %d %s>%s:%s\n", timestamp, cpu, execname, probemod, probefunc);
}

fbt:amdgpu::return
/go/
{
	printf("%d %d %s<%s:%s = %d\n", timestamp, cpu, execname, probemod,
	    probefunc, (int)arg1);
}

syscall::close:return
/pid == $target && go/
{
	printf("%d %d close returned %d\n", timestamp, cpu, (int)arg1);
}
