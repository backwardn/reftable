// Copyright 2019 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "block.h"
#include "iter.h"
#include "record.h"
#include "reftable.h"
#include "tree.h"

struct file_block_source {
  int fd;
  uint64_t size;
};

static uint64_t file_size(void *b) {
  return ((struct file_block_source *)b)->size;
}

static void file_return_block(void *b, struct block *dest) {
  memset(dest->data, 0xff, dest->len);
  free(dest->data);
}

static void file_close(void *b) {
  int fd = ((struct file_block_source *)b)->fd;
  if (fd > 0) {
    close(fd);
    ((struct file_block_source *)b)->fd = 0;
  }

  free(b);
}

static int file_read_block(void *v, struct block *dest, uint64_t off,
                           uint32_t size) {
  struct file_block_source *b = (struct file_block_source *)v;
  assert(off + size <= b->size);
  dest->data = malloc(size);
  if (pread(b->fd, dest->data, size, off) != size) {
    return -1;
  }
  dest->len = size;
  return size;
}

struct block_source_vtable file_vtable = {
    .size = &file_size,
    .read_block = &file_read_block,
    .return_block = &file_return_block,
    .close = &file_close,
};

int block_source_from_file(struct block_source *bs, const char *name) {
  struct stat st = {};
  int err = 0;
  int fd = open(name, O_RDONLY);
  if (fd < 0) {
    if (errno == ENOENT) {
      return NOT_EXIST_ERROR;
    }
    return -1;
  }

  err = fstat(fd, &st);
  if (err < 0) {
    return -1;
  }

  {
    struct file_block_source *p = calloc(sizeof(struct file_block_source), 1);
    p->size = st.st_size;
    p->fd = fd;

    bs->ops = &file_vtable;
    bs->arg = p;
  }
  return 0;
}

int fd_writer(void *arg, byte *data, int sz) {
  int *fdp = (int *)arg;
  return write(*fdp, data, sz);
}
