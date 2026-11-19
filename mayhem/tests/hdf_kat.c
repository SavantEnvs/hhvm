/*
 * Known-answer driver for mayhem/test.sh: parse one HDF file with
 * hdf_read_string() (HPHP::Hdf::fromString) and print either the
 * hdf_write_string() serialization (Hdf::toString) or the error message, plus
 * a few typed lookups, so test.sh can diff against golden output.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hphp/neo/neo_hdf.h"
#include "hphp/neo/neo_err.h"
#include "hphp/neo/neo_str.h"
#include "hphp/neo/neo_rand.h"

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s file.hdf [name...]\n", argv[0]); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  static char buf[1 << 20];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  neo_seed_rand(0);

  HDF *hdf = NULL;
  NEOERR *err = hdf_init(&hdf);
  if (err) { printf("INIT ERROR\n"); return 1; }
  err = hdf_read_string(hdf, buf);
  if (err != STATUS_OK) {
    NEOSTRING s; string_init(&s);
    nerr_error_string(err, &s);
    printf("ERROR %s\n", s.buf ? s.buf : "");
    string_clear(&s);
    return 0;
  }
  char *dump = NULL;
  err = hdf_write_string(hdf, &dump);
  if (err != STATUS_OK) { printf("WRITE ERROR\n"); return 1; }
  printf("%s", dump ? dump : "");
  free(dump);
  for (int i = 2; i < argc; i++) {
    HDF *node = NULL;
    err = hdf_get_node(hdf, argv[i], &node);
    NEOERR *ve = STATUS_OK;
    const char *v = (err == STATUS_OK && node) ? hdf_obj_value(node, &ve) : NULL;
    printf("GET %s = %s\n", argv[i], v ? v : "(null)");
  }
  hdf_destroy(&hdf);
  return 0;
}
