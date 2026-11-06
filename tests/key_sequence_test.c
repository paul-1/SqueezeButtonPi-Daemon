#include <assert.h>
#include "../control.c"

static int calls;
static int failure_call;
static int errors;

int emitKey(int key, int value)
{
	assert(key == 30);
	assert(value == (calls % 2 == 0));
	return ++calls == failure_call;
}

void _mylog(const char *file, int line, int priority, const char *format, ...)
{
	(void)file;
	(void)line;
	(void)format;
	if (priority == LOG_ERR)
		errors++;
}

int main(void)
{
	for (int stage = 1; stage <= 2; stage++) {
		calls = errors = 0;
		failure_call = stage;
		send_key_seq(30, 3);
		assert(calls == 2);
		assert(errors == 1);
	}
	calls = errors = 0;
	failure_call = 0;
	send_key_seq(30, 3);
	assert(calls == 6);
	assert(errors == 0);
	return 0;
}
