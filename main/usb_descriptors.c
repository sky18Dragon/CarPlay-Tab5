#include "usb_descriptors.h"
bool usb_descriptors_walk(const uint8_t *d, size_t n, descriptor_visitor_t visit, void *ctx)
{
    if (!d || n < 9 || d[0] < 9 || d[1] != 2) return false;
    size_t total = (size_t)d[2] | ((size_t)d[3] << 8);
    if (total < 9 || total > n) return false;
    for (size_t off = 0; off < total;) {
        if (total - off < 2) return false;
        size_t len = d[off];
        if (len < 2 || len > total - off) return false;
        if ((d[off + 1] == 4 && len < 9) || (d[off + 1] == 5 && len < 7)) return false;
        off += len;
    }
    if (visit) for (size_t off = 0; off < total; off += d[off]) visit(d + off, d[off], ctx);
    return true;
}
