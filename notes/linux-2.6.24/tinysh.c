typedef unsigned long size_t;

#define NULL ((void *)0)

#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_waitpid 7
#define SYS_execve 11
#define SYS_chdir 12
#define SYS_mknod 14
#define SYS_mount 21
#define SYS_dup2 63
#define SYS_reboot 88
#define SYS_getdents 141
#define SYS_getcwd 183
#define SYS_mkdir 39
#define SYS_sync 36

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define S_IFCHR 0020000

#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_RESTART 0x01234567
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

static long sys0(long n)
{
	long r;
	asm volatile ("int $0x80" : "=a"(r) : "0"(n) : "memory");
	return r;
}

static long sys1(long n, long a)
{
	long r;
	asm volatile ("int $0x80" : "=a"(r) : "0"(n), "b"(a) : "memory");
	return r;
}

static long sys2(long n, long a, long b)
{
	long r;
	asm volatile ("int $0x80" : "=a"(r) : "0"(n), "b"(a), "c"(b) : "memory");
	return r;
}

static long sys3(long n, long a, long b, long c)
{
	long r;
	asm volatile ("int $0x80" : "=a"(r) : "0"(n), "b"(a), "c"(b), "d"(c) : "memory");
	return r;
}

static long sys4(long n, long a, long b, long c, long d)
{
	long r;
	asm volatile ("int $0x80" : "=a"(r) : "0"(n), "b"(a), "c"(b), "d"(c), "S"(d) : "memory");
	return r;
}

static long sys5(long n, long a, long b, long c, long d, long e)
{
	long r;
	asm volatile ("int $0x80" : "=a"(r) : "0"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
	return r;
}

static int strlen(const char *s)
{
	int n = 0;
	while (s && s[n])
		n++;
	return n;
}

static int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

static int streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

static void xputs(const char *s)
{
	sys3(SYS_write, 1, (long)s, strlen(s));
}

static void putnl(void)
{
	xputs("\n");
}

static void mkdir_p(const char *p)
{
	sys2(SYS_mkdir, (long)p, 0755);
}

static long makedev(int major, int minor)
{
	return ((major & 0xfff) << 8) | (minor & 0xff);
}

static void setup_system(void)
{
	long fd;

	mkdir_p("/dev");
	mkdir_p("/proc");
	mkdir_p("/sys");
	mkdir_p("/tmp");
	mkdir_p("/bin");

	sys3(SYS_mknod, (long)"/dev/console", S_IFCHR | 0600, makedev(5, 1));
	sys3(SYS_mknod, (long)"/dev/null", S_IFCHR | 0666, makedev(1, 3));
	sys3(SYS_mknod, (long)"/dev/ttyS0", S_IFCHR | 0600, makedev(4, 64));

	fd = sys3(SYS_open, (long)"/dev/console", O_RDWR, 0);
	if (fd >= 0) {
		sys2(SYS_dup2, fd, 0);
		sys2(SYS_dup2, fd, 1);
		sys2(SYS_dup2, fd, 2);
		if (fd > 2)
			sys1(SYS_close, fd);
	}

	sys5(SYS_mount, (long)"proc", (long)"/proc", (long)"proc", 0, 0);
	sys5(SYS_mount, (long)"sysfs", (long)"/sys", (long)"sysfs", 0, 0);
}

static int parse(char *line, char **argv, int max)
{
	int argc = 0;
	char *p = line;

	while (*p && argc < max - 1) {
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
			*p++ = 0;
		if (!*p)
			break;
		argv[argc++] = p;
		while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
			p++;
	}
	argv[argc] = NULL;
	return argc;
}

static int read_line(char *buf, int len)
{
	int n = 0;
	char c;

	while (n < len - 1) {
		long r = sys3(SYS_read, 0, (long)&c, 1);
		if (r <= 0)
			return r;
		if (c == '\r')
			c = '\n';
		if (c == 127 || c == 8) {
			if (n) {
				n--;
				xputs("\b \b");
			}
			continue;
		}
		sys3(SYS_write, 1, (long)&c, 1);
		buf[n++] = c;
		if (c == '\n')
			break;
	}
	buf[n] = 0;
	return n;
}

static void cmd_cat(int argc, char **argv)
{
	char buf[512];
	int i;

	if (argc < 2) {
		xputs("usage: cat FILE...\n");
		return;
	}
	for (i = 1; i < argc; i++) {
		long fd = sys3(SYS_open, (long)argv[i], O_RDONLY, 0);
		if (fd < 0) {
			xputs("cat: cannot open ");
			xputs(argv[i]);
			xputs("\n");
			continue;
		}
		for (;;) {
			long n = sys3(SYS_read, fd, (long)buf, sizeof(buf));
			if (n <= 0)
				break;
			sys3(SYS_write, 1, (long)buf, n);
		}
		sys1(SYS_close, fd);
	}
}

struct linux_dirent {
	unsigned long d_ino;
	unsigned long d_off;
	unsigned short d_reclen;
	char d_name[1];
};

static void cmd_ls(int argc, char **argv)
{
	char buf[1024];
	const char *path = argc > 1 ? argv[1] : ".";
	long fd = sys3(SYS_open, (long)path, O_RDONLY, 0);

	if (fd < 0) {
		xputs("ls: cannot open ");
		xputs(path);
		xputs("\n");
		return;
	}
	for (;;) {
		long nread = sys3(SYS_getdents, fd, (long)buf, sizeof(buf));
		long bpos = 0;
		if (nread <= 0)
			break;
		while (bpos < nread) {
			struct linux_dirent *d = (struct linux_dirent *)(buf + bpos);
			if (!streq(d->d_name, ".") && !streq(d->d_name, "..")) {
				xputs(d->d_name);
				xputs("  ");
			}
			bpos += d->d_reclen;
		}
	}
	putnl();
	sys1(SYS_close, fd);
}

static void cmd_pwd(void)
{
	char buf[256];
	long r = sys2(SYS_getcwd, (long)buf, sizeof(buf));
	if (r < 0)
		xputs("pwd: getcwd failed\n");
	else {
		xputs(buf);
		putnl();
	}
}

static void cmd_echo(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		if (i > 1)
			xputs(" ");
		xputs(argv[i]);
	}
	putnl();
}

static void cmd_exec(int argc, char **argv)
{
	char *envp[] = { "HOME=/", "PATH=/bin:/sbin:/usr/bin:/usr/sbin", NULL };
	long pid;

	if (argc < 2) {
		xputs("usage: exec PATH [ARG...]\n");
		return;
	}
	pid = sys0(SYS_fork);
	if (pid == 0) {
		sys3(SYS_execve, (long)argv[1], (long)(argv + 1), (long)envp);
		xputs("exec failed\n");
		sys1(SYS_exit, 127);
	}
	if (pid < 0) {
		xputs("fork failed\n");
		return;
	}
	sys3(SYS_waitpid, pid, 0, 0);
}

static void cmd_ps(void)
{
	cmd_ls(2, (char *[]){ "ls", "/proc", NULL });
}

static void help(void)
{
	xputs("builtins: help cat cd echo exec int3 ls mount panic poweroff ps pwd reboot sync uname\n");
	xputs("debug: start qemu with -s -S, then gdb /tmp/linux-2.6.24-build/vmlinux and target remote :1234\n");
}

static void shell(void)
{
	char line[256];
	char *argv[32];

	for (;;) {
		int argc;
		xputs("tinysh# ");
		if (read_line(line, sizeof(line)) <= 0)
			continue;
		argc = parse(line, argv, 32);
		if (!argc)
			continue;

		if (streq(argv[0], "help"))
			help();
		else if (streq(argv[0], "cat"))
			cmd_cat(argc, argv);
		else if (streq(argv[0], "cd")) {
			if (argc < 2)
				xputs("usage: cd DIR\n");
			else if (sys1(SYS_chdir, (long)argv[1]) < 0)
				xputs("cd failed\n");
		} else if (streq(argv[0], "echo"))
			cmd_echo(argc, argv);
		else if (streq(argv[0], "exec"))
			cmd_exec(argc, argv);
		else if (streq(argv[0], "int3"))
			asm volatile ("int3");
		else if (streq(argv[0], "ls"))
			cmd_ls(argc, argv);
		else if (streq(argv[0], "mount"))
			cmd_cat(2, (char *[]){ "cat", "/proc/mounts", NULL });
		else if (streq(argv[0], "panic")) {
			long fd = sys3(SYS_open, (long)"/proc/sysrq-trigger", O_WRONLY, 0);
			if (fd >= 0) {
				sys3(SYS_write, fd, (long)"c", 1);
				sys1(SYS_close, fd);
			} else {
				xputs("cannot open /proc/sysrq-trigger\n");
			}
		} else if (streq(argv[0], "poweroff")) {
			sys0(SYS_sync);
			sys4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_POWER_OFF, 0);
		} else if (streq(argv[0], "ps"))
			cmd_ps();
		else if (streq(argv[0], "pwd"))
			cmd_pwd();
		else if (streq(argv[0], "reboot")) {
			sys0(SYS_sync);
			sys4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, LINUX_REBOOT_CMD_RESTART, 0);
		} else if (streq(argv[0], "sync"))
			sys0(SYS_sync);
		else if (streq(argv[0], "uname"))
			cmd_cat(2, (char *[]){ "cat", "/proc/version", NULL });
		else {
			xputs("unknown command: ");
			xputs(argv[0]);
			xputs("\n");
		}
	}
}

void _start(void)
{
	setup_system();
	xputs("\nLinux 2.6.24 tiny initramfs shell\n");
	help();
	shell();
	sys1(SYS_exit, 0);
}
