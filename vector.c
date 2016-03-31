#include <stdlib.h>
#include "vector.h"

#define FACTOR 1.5

typedef struct item {
    void *data;
} item_t;

static item_t *buf;
static unsigned int c = 0;
static unsigned int size = 0;

void vector_init(int sz)
{
    size = sz;
    buf = (item_t *) malloc(size * sizeof(struct item));
}

void vector_push_back(void *data)
{
    if (data) {
        if (c >= size) {
            item_t *newbuf;

            newbuf = (item_t *) realloc(buf, size * sizeof(struct item) * FACTOR);
            buf = newbuf;
            size = size * FACTOR;
        }
        buf[c++].data = data;
    }
}

inline void vector_pop_back()
{
    if (c) {
        free(buf[c].data);
        c--;
    }
}

inline void *vector_back()
{
    return buf[c].data;
}

inline void *vector_get_data(int i)
{
    if (i < c) {
        return buf[i].data;
    }
    return NULL;
}

inline int vector_size()
{
    return c;
}

void vector_clear()
{
    for (unsigned int i = 0; i < c; i++) {
        free(buf[i].data);
    }
    free(buf);
    size = 0;
}
