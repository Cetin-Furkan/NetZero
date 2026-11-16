# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux unix domain socket layer.
#

ccflags-y += -std=gnu23 -O3
ifdef CONFIG_X86_64
ccflags-y += -march=tigerlake -mtune=tigerlake -mno-sse -mno-mmx -mno-sse2 -mno-3dnow -mno-avx
endif

obj-$(CONFIG_UNIX)	+= unix.o

unix-y			:= af_unix.o garbage.o
unix-$(CONFIG_SYSCTL)	+= sysctl_net_unix.o
unix-$(CONFIG_BPF_SYSCALL) += unix_bpf.o

obj-$(CONFIG_UNIX_DIAG)	+= unix_diag.o
unix_diag-y		:= diag.o
