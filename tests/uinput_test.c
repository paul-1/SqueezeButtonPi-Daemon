#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

static int calls;
static int failure_call;
static int failure_errno;
static int short_write;
static int errors;

static ssize_t mock_write(int fd, const void *buffer, size_t size)
{
	(void)fd;
	(void)buffer;
	if (++calls == failure_call) {
		errno = failure_errno;
		return short_write ? (ssize_t)size - 1 : -1;
	}
	return (ssize_t)size;
}

#define write mock_write
#include "../uinput.c"
#undef write

void _mylog(const char *file, int line, int priority, const char *format, ...)
{
	(void)file;
	(void)line;
	(void)format;
	assert(priority == LOG_ERR);
	errors++;
}

static void assert_unlocked(void)
{
	assert(pthread_mutex_trylock(&send_lock) == 0);
	assert(pthread_mutex_unlock(&send_lock) == 0);
}

int main(void)
{
	for (int stage = 1; stage <= 2; stage++) {
		for (int partial = 0; partial <= 1; partial++) {
			calls = errors = 0;
			failure_call = stage;
			failure_errno = EAGAIN;
			short_write = partial;
			assert(emitKey(30, 1) == 1);
			assert(errors == 1);
			assert_unlocked();
			failure_call = 0;
			assert(emitKey(30, 0) == 0);
			assert_unlocked();
		}
		calls = errors = 0;
		failure_call = stage;
		failure_errno = EINTR;
		short_write = 0;
		assert(emitKey(30, 1) == 0);
		assert(calls == 3);
		assert(errors == 0);
		assert_unlocked();
	}
	return 0;
}
