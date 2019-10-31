#ifndef SLICE_H
#define SLICE_H

#include "basics.h"

typedef struct {
  byte *buf;
  int len;
  int cap;
} slice;

void slice_set_string(slice* dest, const char *);
char *slice_to_string(slice src);
bool slice_equal(slice a, slice b);
byte *slice_yield(slice *s);
void slice_copy(slice *dest, slice src);
void slice_resize(slice *s, int l);
int slice_compare(slice a, slice b);

#endif
