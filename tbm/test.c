#include "tbmlib.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

void f(FibCidr *cidr, uint32_t *value, void *aux) {
    printf("cidr->ip %x, cidr->cidr %d\n", cidr->ip, cidr->cidr);
}

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

    tbm_iterate(&tbm, f, NULL);
    err = tbm_remove(&tbm, cidr);
    assert(!err);
    err = tbm_lookup(&ret, &tbm, 0x0);
    assert(err);
    tbm_free(&tbm);
}
