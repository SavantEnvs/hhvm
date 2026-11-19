/*
 * libFuzzer harness for HHVM's HDF config parser (hphp/neo, ClearSilver HDF).
 *
 * Entry point: hdf_read_string(), the call behind HPHP::Hdf::fromString()
 * (hphp/util/hdf.cpp) — HHVM's `.hdf` config / `-v` option text. String parsing
 * runs with INCLUDE_ERROR, so `#include` lines are rejected and no file is read.
 *
 * Per input: parse into a fresh HDF tree, serialize it (hdf_write_string, used by
 * Hdf::toString), parse the serialization back, walk the tree the way Hdf's
 * iterators do, deep-copy it, then free everything. No state survives an input
 * except neo's one-time error-type registry (nerr_init), which is read-only after
 * the first call, and the PRNG hdf_write_string uses to pick heredoc terminators
 * (neo_rand), which is re-seeded at the start of every input.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "hphp/neo/neo_hdf.h"
#include "hphp/neo/neo_err.h"
#include "hphp/neo/neo_str.h"
#include "hphp/neo/neo_rand.h"

/* NEOERR chains are heap-allocated (nerr_raise/nerr_pass); HHVM turns them into
 * an exception and drops them. Free them here so a long run doesn't grow. */
static void drop_err(NEOERR *err) {
  while (err != STATUS_OK && err != INTERNAL_ERR) {
    NEOERR *next = err->next;
    free(err);
    err = next;
  }
}

static void walk(HDF *node, int depth) {
  NEOERR *err = STATUS_OK;
  for (; node; node = hdf_obj_next(node)) {
    (void)hdf_obj_name(node);
    const char *v = hdf_obj_value(node, &err);
    drop_err(err); err = STATUS_OK;
    if (v) (void)strlen(v);
    (void)hdf_is_wildcard(node);
    if (depth < 64) {
      HDF *child = hdf_obj_child(node, &err);
      drop_err(err); err = STATUS_OK;
      walk(child, depth + 1);
    }
  }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  char *buf = (char *)malloc(size + 1);
  if (!buf) return 0;
  memcpy(buf, data, size);
  buf[size] = '\0';

  neo_seed_rand(0);

  HDF *hdf = NULL;
  NEOERR *err = hdf_init(&hdf);
  if (err != STATUS_OK) { drop_err(err); free(buf); return 0; }

  err = hdf_read_string(hdf, buf);
  if (err == STATUS_OK) {
    NEOERR *e2 = STATUS_OK;
    walk(hdf_obj_child(hdf, &e2), 0);
    drop_err(e2);

    char *dump = NULL;
    NEOERR *we = hdf_write_string(hdf, &dump);
    if (we == STATUS_OK && dump) {
      HDF *again = NULL;
      NEOERR *ie = hdf_init(&again);
      if (ie == STATUS_OK) {
        drop_err(hdf_read_string(again, dump));
        hdf_destroy(&again);
      } else {
        drop_err(ie);
      }
    }
    drop_err(we);
    free(dump);

    NEOSTRING str;
    string_init(&str);
    drop_err(hdf_dump_str(hdf, NULL, 1, &str));
    string_clear(&str);

    HDF *copy = NULL;
    NEOERR *ce = hdf_init(&copy);
    if (ce == STATUS_OK) {
      drop_err(hdf_copy(copy, "", hdf));
      hdf_destroy(&copy);
    } else {
      drop_err(ce);
    }
  } else {
    /* HHVM formats the error message before throwing (Hdf::CheckNeoError). */
    NEOSTRING str;
    string_init(&str);
    nerr_error_string(err, &str);
    string_clear(&str);
    drop_err(err);
  }

  hdf_destroy(&hdf);
  free(buf);
  return 0;
}
