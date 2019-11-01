#include <string.h>

#include "api.h"
#include "basics.h"
#include "record.h"
#include "test_framework.h"

void varint_roundtrip() {
  uint64 inputs[] = {0,
                     1,
                     27,
                     127,
                     128,
                     257,
                     4096,
                     ((uint64)1 << 63),
                     ((uint64)1 << 63) + ((uint64)1 << 63) - 1};
  for (int i = 0; i < ARRAYSIZE(inputs); i++) {
    byte dest[10];

    slice out = {.buf = dest, .len = 10, .cap = 10};

    uint64 in = inputs[i];
    int n = put_var_int(out, in);
    assert(n > 0);
    out.len = n;

    uint64 got;
    n = get_var_int(&got, out);
    assert(n > 0);

    assert(got == in);
  }
}

void test_common_prefix() {
  struct {
    const char *a, *b;
    int want;
  } cases[] = {
      {"abc", "ab", 2},
      {"", "abc", 0},
      {"abc", "abd", 2},
      {"abc", "pqr", 0},
  };

  for (int i = 0; i < ARRAYSIZE(cases); i++) {
    slice a = {};
    slice b = {};
    slice_set_string(&a, cases[i].a);
    slice_set_string(&b, cases[i].b);

    int got = common_prefix_size(a, b);
    assert(got == cases[i].want);

    free(slice_yield(&a));
    free(slice_yield(&b));
  }
}

void set_hash(byte *h, int j) {
  for (int i = 0; i < HASH_SIZE; i++) {
    h[i] = (j >> i) & 0xff;
  }
}

void test_ref_record_roundtrip() {
  byte testHash1[HASH_SIZE] = {};
  byte testHash2[HASH_SIZE] = {};

  set_hash(testHash1, 1);
  set_hash(testHash1, 2);
  for (int i = 1; i <= 3; i++) {
    printf("subtest %d\n", i);
    ref_record in = {};
    switch (i) {
    case 1:
      in.value = testHash1;
      break;
    case 2:
      in.value = testHash1;
      in.target_value = testHash2;
      break;
    case 3:
      in.target = "target";
      break;
    }

    in.ref_name = "refs/heads/master";
    byte buf[1024];
    slice key = {};
    ref_record_key((record *)&in, &key);

    slice dest = {
        .buf = buf,
        .len = sizeof(buf),
    };
    int n = ref_record_encode((record *)&in, dest);
    assert(n > 0);

    ref_record out = {};
    n = ref_record_decode((record *)&out, key, i, dest);
    assert(n > 0);

    assert((out.value != NULL) == (in.value != NULL));
    assert((out.target_value != NULL) == (in.target_value != NULL));
    assert((out.target != NULL) == (in.target != NULL));
    free(slice_yield(&key));
  }
}

void test_u24_roundtrip() {
  uint32 in = 0x112233;
  byte dest[3];

  put_u24(dest, in);
  uint32 out = get_u24(dest);
  assert(in == out);
}

void test_key_roundtrip() {
  slice dest = {}, last_key = {}, key = {}, roundtrip = {};

  slice_resize(&dest, 1024);
  slice_set_string(&last_key, "refs/heads/master");
  slice_set_string(&key, "refs/tags/bla");

  bool restart;
  byte extra = 6;
  int n = encode_key(&restart, dest, last_key, key, extra);
  assert(!restart);
  assert(n > 0);

  byte rt_extra;
  int m = decode_key(&roundtrip, &rt_extra, last_key, dest);
  assert(n == m);
  printf("%s\n", slice_to_string(roundtrip));
  assert(slice_equal(key, roundtrip));
  assert(rt_extra == extra);
}

void test_obj_record_roundtrip() {
  byte testHash1[HASH_SIZE] = {};
  set_hash(testHash1, 1);
  uint64 till9[] = {1, 2, 3, 4, 500, 600, 700, 800, 9000};

  obj_record recs[3] = {{
                            .hash_prefix = testHash1,
                            .hash_prefix_len = 5,
                            .offsets = till9,
                            .offset_len = 3,
                        },
                        {
                            .hash_prefix = testHash1,
                            .hash_prefix_len = 5,
                            .offsets = till9,
                            .offset_len = 9,
                        },
                        {
                            .hash_prefix = testHash1,
                            .hash_prefix_len = 5,
                        }

  };
  for (int i = 0; i < ARRAYSIZE(recs); i++) {
    printf("subtest %d\n", i);
    obj_record in = recs[i];
    byte buf[1024];
    slice key = {};
    obj_record_key((record *)&in, &key);

    slice dest = {
        .buf = buf,
        .len = sizeof(buf),
    };
    int n = obj_record_encode((record *)&in, dest);
    assert(n > 0);

    byte extra = obj_record_val_type((record *)&in);
    obj_record out = {};
    n = obj_record_decode((record *)&out, key, extra, dest);
    assert(n > 0);

    assert(in.hash_prefix_len == out.hash_prefix_len);
    assert(in.offset_len == out.offset_len);

    assert(0 == memcmp(in.hash_prefix, out.hash_prefix, in.hash_prefix_len));
    assert(0 ==
           memcmp(in.offsets, out.offsets, sizeof(uint64) * in.offset_len));
    free(slice_yield(&key));
  }
}

void test_index_record_roundtrip() {
  index_record in = {.offset = 42};

  slice_set_string(&in.last_key, "refs/heads/master");

  slice key = {};
  index_record_key((record *)&in, &key);

  assert(0 == slice_compare(key, in.last_key));

  byte buf[1024];
  slice dest = {
      .buf = buf,
      .len = sizeof(buf),
  };
  int n = index_record_encode((record *)&in, dest);
  assert(n > 0);

  byte extra = index_record_val_type((record *)&in);
  index_record out = {};
  n = index_record_decode((record *)&out, key, extra, dest);
  assert(n > 0);

  assert(in.offset == out.offset);

  free(slice_yield(&key));
  free(slice_yield(&in.last_key));
}

int main() {
  add_test_case("varint_roundtrip", &varint_roundtrip);
  add_test_case("test_key_roundtrip", &test_key_roundtrip);
  add_test_case("test_common_prefix", &test_common_prefix);
  add_test_case("test_ref_record_roundtrip", &test_ref_record_roundtrip);
  add_test_case("test_obj_record_roundtrip", &test_obj_record_roundtrip);
  add_test_case("test_obj_record_roundtrip", &test_index_record_roundtrip);
  add_test_case("test_u24_roundtrip", &test_u24_roundtrip);
  test_main();
}
