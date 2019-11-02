#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "block.h"
#include "record.h"
#include "tree.h"
#include "writer.h"

typedef struct {
  bool present;
  uint64 offset;
  uint64 index_offset;
} reader_offsets;

typedef struct {
  block_source_ops *block_ops;
  void *block_source_arg;

  uint64 size;
  uint32 block_size;
  uint64 min_update_index;
  uint64 max_update_index;
  int object_id_len;

  reader_offsets ref_offsets;
  reader_offsets obj_offsets;
  reader_offsets log_offsets;
} reader;

reader_offsets* reader_offsets_for(reader* r, byte typ) {
  switch (typ) {
  case BLOCK_TYPE_REF:
    return &r->ref_offsets;
  case BLOCK_TYPE_LOG:
    return &r->log_offsets;
  case BLOCK_TYPE_OBJ:
    return &r->obj_offsets;
  }
  abort();
}

int reader_get_block(reader *r, byte **dest, uint64 off, uint32 sz) {
  if (off >= r->size) {
    return 0;
  }

  if (off + sz > r->size) {
    sz = r->size - off;
  }

  return r->block_ops->read_block(r->block_source_arg, dest, off, sz);
}

void reader_return_block(reader *r, byte*p) {
  r->block_ops->return_block(r->block_source_arg, p);
}

int init_reader(reader *r, block_source_ops *ops, void *block_source_arg) {
  memset(r, 0, sizeof(reader));
  r->size = ops->size(block_source_arg) - FOOTER_SIZE;
  r->block_source_arg = block_source_arg;
  r->block_ops = ops;

  byte *footer = NULL;
  int n = reader_get_block(r, &footer, r->size, FOOTER_SIZE);
  if (n != FOOTER_SIZE) {
    return IO_ERROR;
  }

  byte *f = footer;
  if (memcmp(f, "REFT", 4)) {
    return FORMAT_ERROR;
  }
  f += 4;
  byte version = *f++;
  if (version != 1) {
    return FORMAT_ERROR;
  }
  r->block_size = get_u24(f);

  f += 3;
  r->min_update_index = get_u64(f);
  f += 8;
  r->max_update_index = get_u64(f);
  f += 8;

  uint64 ref_index_off = get_u64(f);
  f += 8;
  uint64 obj_off = get_u64(f);
  f += 8;

  r->object_id_len = obj_off & ((1 << 5) - 1);
  obj_off >>= 5;

  uint64 obj_index_off = get_u64(f);
  f += 8;
  uint64 log_off = get_u64(f);
  f += 8;
  uint64 log_index_off = get_u64(f);
  f += 8;

  byte *header;
  n = reader_get_block(r, &header, 0, HEADER_SIZE + 1);
  if (n != HEADER_SIZE) {
    return IO_ERROR;
  }

  byte first_block_typ = header[HEADER_SIZE];
  r->ref_offsets.present = (first_block_typ == BLOCK_TYPE_REF);
  r->ref_offsets.offset = 0;
  r->ref_offsets.index_offset = ref_index_off;

  r->log_offsets.present = (first_block_typ == BLOCK_TYPE_LOG || log_off > 0);
  r->log_offsets.offset = log_off;
  r->log_offsets.index_offset = log_index_off;

  r->obj_offsets.present = obj_off > 0;
  r->obj_offsets.offset = obj_off;
  r->obj_offsets.index_offset = obj_index_off;

  ops->return_block(block_source_arg, footer);
  ops->return_block(block_source_arg, header);
  return 0;
}

typedef struct {
  reader *r;
  byte typ;
  uint64 block_off;
  block_iter *bi;
} table_iter;

void table_iter_copy_from(table_iter *dst , table_iter *src) {
  *dst=  *src;
  dst->bi = malloc(sizeof(block_iter));
  *dst->bi = *src->bi;
}

int table_iter_next_in_block(table_iter* ti, record rec) {
  int res = block_iter_next(ti->bi, rec);
  if (res == 0 && record_type(rec) == BLOCK_TYPE_REF) {
    ((ref_record*)rec.data)->update_index += ti->r->min_update_index;
  }

  return res;
}

void table_iter_block_done(table_iter *ti) {
  reader_return_block(ti->r, ti->bi->br->block);
  free(ti->bi->br);
  ti->bi->br = NULL;

  ti->bi->last_key.len = 0;
  ti->bi->next_off = 0;
}


int32 reader_block_size(reader *r, byte *typ, uint64 off) {
  if (off == 0) {
    off = 24;
  }

  byte *head;
  int err = reader_get_block(r, &head, off, 4);
    if (err < 0) {
      return err;
    }
  int32 result;
  if (!is_block_type(head[0])) {
    result = 0;
    goto exit;
  }

  result = get_u24(head + 1);
 exit:
  reader_return_block(r, head);
  return result;
}


int reader_init_block_reader(reader* r, block_reader* br, uint64 next_off, byte typ) {
  if (next_off > r->size){
    return 0;
  }

  byte block_typ = 0;
  int32 block_size = reader_block_size(r, &block_typ, next_off);
  if (block_size < 0) {
    return block_size;
  }
  if (block_typ != typ) {
    return 0;
  }

  byte *block = NULL;
  int32 read_size = reader_get_block(r, &block, next_off, block_size);
  if (read_size <= 0) {
    return read_size;
  }

  uint32 header_off;
  if (next_off == 0) {
    header_off = HEADER_SIZE;
  }

  block_reader_init(br, block, header_off, r->block_size);
  return 0;
}

int table_iter_next_block(table_iter *ti) {
  uint64 next_block_off = ti->block_off + ti->bi->br->full_block_size;

  block_reader br = {};
  int err = reader_init_block_reader(ti->r, &br, next_block_off, ti->typ);
  if (err != 0) {
    return err;
  }

  block_reader* brp = malloc(sizeof(block_reader));
  *brp = br;
  table_iter_block_done(ti);
  block_reader_start(brp, ti->bi);
  ti->block_off = next_block_off;
  return 0;
}

int table_iter_next(table_iter* ti, record rec) {
  if (ti->bi == NULL) {
    return 1;
  }

  int err = table_iter_next_in_block(ti, rec);
  if (err <= 0) {
    return err;
  }

  err = table_iter_next_block(ti);
  if (err < 0) {
    return err;
  }

  return block_iter_next(ti->bi, rec);
}

int reader_table_iter_at(reader *r, table_iter *ti,  uint64 off, byte typ) {
  block_reader br;
  int err = reader_init_block_reader(r, &br, off, typ);
  if (err != 0) {
    return err;
  }

  block_reader*brp = malloc(sizeof(block_reader));
  *brp = br;
  ti->r = r;
  ti->typ = block_reader_type(brp);
  ti->block_off = off;
  block_reader_start(brp, ti->bi);
  return 0;
}

int reader_start(reader* r, table_iter *ti, byte typ , bool index ) {
  reader_offsets *offs = reader_offsets_for(r, typ);
  uint64 off = offs->offset;
  if (index) {
    off = offs->index_offset;
    if (off == 0) {
      return 1;
    }
    typ = BLOCK_TYPE_INDEX;
  }

  return reader_table_iter_at(r, ti,  off, typ);
}

int reader_seek_internal(reader* r, iterator* it, record rec) {
  // XXX
  return 0;
}

int reader_seek(reader* r, iterator* it, record rec) {
  byte typ = record_type(rec);
  
  reader_offsets *offs = reader_offsets_for(r, typ);
  if (!offs->present) {
    iterator_set_empty(it);
    return 0;
  }

  return reader_seek_internal(r, it, rec);
}

