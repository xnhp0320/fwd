#include "tbmlib.h"
#include <assert.h>
#include <stdint.h>

int main() {
    FibTbm tbm = {};
    tbm_init(&tbm, 1000);
    FibCidr cidr = {0x0, 1};
    c3fault_t err = tbm_insert(&tbm, cidr, 23);
    assert(!err);
    uint32_t ret;
    err = tbm_lookup(&ret, &tbm, 0x0);
    assert(!err);
    assert(ret == 23);
    err = tbm_remove(&tbm, cidr);
    assert(!err);
    err = tbm_lookup(&ret, &tbm, 0x0);
    assert(err);
    tbm_free(&tbm);
}
