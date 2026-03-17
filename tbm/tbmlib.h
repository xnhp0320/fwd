#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifndef __c3__
#define __c3__

typedef void* c3typeid_t;
typedef void* c3fault_t;
typedef struct { void* ptr; size_t len; } c3slice_t;
typedef struct { void* ptr; c3typeid_t type; } c3any_t;

#endif

/* TYPES */
typedef struct tbmlib_cidr__Cidr$uint$__ tbmlib_cidr__Cidr$uint$;
struct tbmlib_cidr__Cidr$uint$__
{
	uint32_t ip;
	uint32_t cidr;
};
typedef tbmlib_cidr__Cidr$uint$ FibCidr;
typedef struct tbmlib__Tbm$5$uint$uint$__ tbmlib__Tbm$5$uint$uint$;
typedef struct tbmlib__TbmNode$5$uint$uint$__ tbmlib__TbmNode$5$uint$uint$;
typedef uint32_t tbmlib_bitmap__BitmapType$5$;
typedef tbmlib_bitmap__BitmapType$5$ tbmlib__BitmapType$5$uint$uint$;
struct tbmlib__TbmNode$5$uint$uint$__
{
	tbmlib__BitmapType$5$uint$uint$ external;
	tbmlib_bitmap__BitmapType$5$ __bits1;
	void* next;
};
struct tbmlib__Tbm$5$uint$uint$__
{
	tbmlib__TbmNode$5$uint$uint$ root;
	c3any_t alloc;
};
typedef tbmlib__Tbm$5$uint$uint$ FibTbm;
typedef void(*TbmIterateFunc)(FibCidr* cidr, uint32_t* value, void* aux);
typedef uint64_t std_core__usz;

/* FUNCTIONS */
extern void tbm_init(FibTbm* self, size_t nodes_cnt);
extern c3fault_t tbm_insert(FibTbm* self, FibCidr prefix, uint32_t v);
extern c3fault_t tbm_remove(FibTbm* self, FibCidr prefix);
extern void tbm_free(FibTbm* self);
extern void tbm_iterate(FibTbm* self, TbmIterateFunc f, void* aux);
extern c3fault_t tbm_lookup(uint32_t* return_ref, FibTbm* self, uint32_t v);
