/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "strbuf.h"

#include "system.h"
#include "reftable.h"
#include "basics.h"

#ifdef REFTABLE_STANDALONE

void strbuf_init(struct strbuf *s, size_t alloc)
{
	struct strbuf empty = STRBUF_INIT;
	*s = empty;
}

void strbuf_grow(struct strbuf *s, size_t extra)
{
	size_t newcap = s->len + extra + 1;
	if (newcap > s->cap) {
		s->buf = reftable_realloc(s->buf, newcap);
		s->cap = newcap;
	}
}

static void strbuf_resize(struct strbuf *s, int l)
{
	int zl = l + 1; /* one uint8_t for 0 termination. */
	assert(s->canary == STRBUF_CANARY);
	if (s->cap < zl) {
		int c = s->cap * 2;
		if (c < zl) {
			c = zl;
		}
		s->cap = c;
		s->buf = reftable_realloc(s->buf, s->cap);
	}
	s->len = l;
	s->buf[l] = 0;
}

void strbuf_setlen(struct strbuf *s, size_t l)
{
	assert(s->cap >= l + 1);
	s->len = l;
	s->buf[l] = 0;
}

void strbuf_reset(struct strbuf *s)
{
	strbuf_resize(s, 0);
}

void strbuf_addstr(struct strbuf *d, const char *s)
{
	int l1 = d->len;
	int l2 = strlen(s);
	assert(d->canary == STRBUF_CANARY);

	strbuf_resize(d, l2 + l1);
	memcpy(d->buf + l1, s, l2);
}

void strbuf_addbuf(struct strbuf *s, struct strbuf *a)
{
	int end = s->len;
	assert(s->canary == STRBUF_CANARY);
	strbuf_resize(s, s->len + a->len);
	memcpy(s->buf + end, a->buf, a->len);
}

char *strbuf_detach(struct strbuf *s, size_t *sz)
{
	char *p = NULL;
	p = (char *)s->buf;
	if (sz)
		*sz = s->len;
	s->buf = NULL;
	s->cap = 0;
	s->len = 0;
	return p;
}

void strbuf_release(struct strbuf *s)
{
	assert(s->canary == STRBUF_CANARY);
	s->cap = 0;
	s->len = 0;
	reftable_free(s->buf);
	s->buf = NULL;
}

int strbuf_cmp(const struct strbuf *a, const struct strbuf *b)
{
	int min = a->len < b->len ? a->len : b->len;
	int res = memcmp(a->buf, b->buf, min);
	assert(a->canary == STRBUF_CANARY);
	assert(b->canary == STRBUF_CANARY);
	if (res != 0)
		return res;
	if (a->len < b->len)
		return -1;
	else if (a->len > b->len)
		return 1;
	else
		return 0;
}

int strbuf_add(struct strbuf *b, const void *data, size_t sz)
{
	assert(b->canary == STRBUF_CANARY);
	strbuf_grow(b, sz);
	memcpy(b->buf + b->len, data, sz);
	b->len += sz;
	b->buf[b->len] = 0;
	return sz;
}

#endif

static uint64_t strbuf_size(void *b)
{
	return ((struct strbuf *)b)->len;
}


int strbuf_add_void(void *b, const void *data, size_t sz)
{
	strbuf_add((struct strbuf *)b, data, sz);
	return sz;
}

static void strbuf_return_block(void *b, struct reftable_block *dest)
{
	memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

static void strbuf_close(void *b)
{
}

static int strbuf_read_block(void *v, struct reftable_block *dest, uint64_t off,
			     uint32_t size)
{
	struct strbuf *b = (struct strbuf *)v;
	assert(off + size <= b->len);
	dest->data = reftable_calloc(size);
	memcpy(dest->data, b->buf + off, size);
	dest->len = size;
	return size;
}

struct reftable_block_source_vtable strbuf_vtable = {
	.size = &strbuf_size,
	.read_block = &strbuf_read_block,
	.return_block = &strbuf_return_block,
	.close = &strbuf_close,
};

void block_source_from_strbuf(struct reftable_block_source *bs,
			      struct strbuf *buf)
{
	assert(bs->ops == NULL);
	bs->ops = &strbuf_vtable;
	bs->arg = buf;
}

static void malloc_return_block(void *b, struct reftable_block *dest)
{
	memset(dest->data, 0xff, dest->len);
	reftable_free(dest->data);
}

struct reftable_block_source_vtable malloc_vtable = {
	.return_block = &malloc_return_block,
};

struct reftable_block_source malloc_block_source_instance = {
	.ops = &malloc_vtable,
};

struct reftable_block_source malloc_block_source(void)
{
	return malloc_block_source_instance;
}

int common_prefix_size(struct strbuf *a, struct strbuf *b)
{
	int p = 0;
	while (p < a->len && p < b->len) {
		if (a->buf[p] != b->buf[p]) {
			break;
		}
		p++;
	}

	return p;
}

struct strbuf reftable_empty_strbuf = STRBUF_INIT;
