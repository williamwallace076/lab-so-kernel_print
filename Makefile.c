# Makefile for Information Server (IS)
.include <bsd.own.mk>

PROG=	is
SRCS=	main.c dmp.c dmp_kernel.c dmp_pm.c dmp_fs.c dmp_rs.c dmp_ds.c dmp_vm.c ps2.c

DPADD+=	${LIBSYS}
LDADD+=	-lsys

CPPFLAGS.dmp_fs.c+=	-I${NETBSDSRCDIR}/minix/servers \
   			-I${NETBSDSRCDIR}/minix/fs
CPPFLAGS.dmp_kernel.c+=	-I${NETBSDSRCDIR}/minix
CPPFLAGS.dmp_rs.c+=	-I${NETBSDSRCDIR}/minix
CPPFLAGS.dmp_vm.c+=	-I${NETBSDSRCDIR}/minix

# Adding CPPFLAGS for ps2.c if needed
CPPFLAGS.ps2.c+=	-I${NETBSDSRCDIR}/minix

# This setting must match the kernel's, as it affects the IRQ hooks table size.
.if ${USE_APIC} != "no"
CFLAGS+= -DUSE_APIC
.endif

.include <minix.service.mk>
