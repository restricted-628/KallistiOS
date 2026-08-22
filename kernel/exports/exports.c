/* KallistiOS ##version##

   exports.c
   Copyright (C) 2003 Megan Potter
   Copyright (C) 2024 Ruslan Rostovtsev
   Copyright (C) 2026 Joseph Black

*/

/*

Just a quick interface to actually make use of all those nifty kernel
export tables. This just does a linear search through the two tables
to look for a symbol, for now. This should be optimized later with a
binary search or something, but loading a new program is generally expected
to be a somewhat slow process anyway.

*/

#include <string.h>
#include <kos/nmmgr.h>
#include <kos/exports.h>

static symtab_handler_t st_kern = {
    {
        "sym/kernel/kernel",
        0,
        0x00010000,
        0,
        NMMGR_TYPE_SYMTAB,
        NMMGR_LIST_INIT
    },
    kernel_symtab
};

static symtab_handler_t st_arch = {
    {
        "sym/kernel/arch",
        0,
        0x00010000,
        0,
        NMMGR_TYPE_SYMTAB,
        NMMGR_LIST_INIT
    },
    arch_symtab
};

static symtab_handler_t st_subarch = {
    {
        "sym/kernel/subarch",
        0,
        0x00010000,
        0,
        NMMGR_TYPE_SYMTAB,
        NMMGR_LIST_INIT
    },
    subarch_symtab
};

void export_init(void) {
    /* Add our two export tables */
    nmmgr_handler_add(&st_kern.nmmgr);
    nmmgr_handler_add(&st_arch.nmmgr);
    nmmgr_handler_add(&st_subarch.nmmgr);
}

export_sym_t *export_lookup(const char *name) {
    nmmgr_handler_t *nmmgr;
    char path[NAME_MAX];
    size_t index;
    int i;
    symtab_handler_t *sth;

    for(index = 0;
        nmmgr_handler_get_path(index, NMMGR_TYPE_SYMTAB, 0, 0, path,
                               sizeof(path)) == 0;
        ++index) {
        nmmgr = nmmgr_lookup_ref(path);

        if(!nmmgr)
            continue;

        sth = (symtab_handler_t *)nmmgr;

        /* First look through the kernel table */
        for(i = 0; /* */; i++) {
            if(sth->table[i].name == NULL)
                break;

            if(!strcmp(name, sth->table[i].name)) {
                export_sym_t *result = sth->table + i;

                nmmgr_handler_release(nmmgr);
                return result;
            }
        }

        nmmgr_handler_release(nmmgr);
    }

    return NULL;
}

export_sym_t *export_lookup_path(const char *name, const char *path) {
    nmmgr_handler_t *nmmgr;
    symtab_handler_t *sth;
    int i;

    /* Get the name manager list */
    nmmgr = nmmgr_lookup_ref(path);

    if(nmmgr == NULL) {
        return NULL;
    }
    sth = (symtab_handler_t *)nmmgr;

    for(i = 0; sth->table[i].name; i++) {
        if(!strcmp(name, sth->table[i].name)) {
            export_sym_t *result = sth->table + i;

            nmmgr_handler_release(nmmgr);
            return result;
        }
    }

    nmmgr_handler_release(nmmgr);
    return NULL;
}

export_sym_t *export_lookup_addr(uintptr_t addr) {
    nmmgr_handler_t *nmmgr;
    char path[NAME_MAX];
    size_t index;
    int i;
    symtab_handler_t *sth;

    uintptr_t dist = ~0;
    export_sym_t *best = NULL;

    for(index = 0;
        nmmgr_handler_get_path(index, NMMGR_TYPE_SYMTAB, 0, 0, path,
                               sizeof(path)) == 0;
        ++index) {
        nmmgr = nmmgr_lookup_ref(path);

        if(!nmmgr)
            continue;

        sth = (symtab_handler_t *)nmmgr;

        /* First look through the kernel table */
        for(i = 0; sth->table[i].name; i++) {
            if(addr - sth->table[i].ptr < dist) {
                dist = addr - sth->table[i].ptr;
                best = sth->table + i;
            }
        }

        nmmgr_handler_release(nmmgr);
    }

    return best;
}
