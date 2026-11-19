#ifndef FLOW_IR_IO_H
#define FLOW_IR_IO_H

#include "flow_ir.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool flow_ir_load_file(flow_ir_prog_t *out, const char *path, uint8_t *scratch_buf,
                                     size_t scratch_cap) {
    if (!out || !path || !scratch_buf || scratch_cap < FLOW_IR_HEADER_BYTES + 8U) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    size_t n = fread(scratch_buf, 1U, scratch_cap, fp);
    int extra = fgetc(fp);
    fclose(fp);
    if (extra != EOF) {
        return false;
    }
    return flow_ir_load_bytes(out, scratch_buf, n);
}

static inline bool flow_ir_save_file(const flow_ir_prog_t *p, const char *path,
                                     uint8_t *scratch_buf, size_t scratch_cap) {
    if (!path) {
        return false;
    }
    size_t len = 0U;
    if (!flow_ir_encode_bytes(p, scratch_buf, scratch_cap, &len)) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        return false;
    }
    size_t wr = fwrite(scratch_buf, 1U, len, fp);
    fclose(fp);
    return wr == len;
}

#ifdef __cplusplus
}
#endif

#endif /* FLOW_IR_IO_H */
