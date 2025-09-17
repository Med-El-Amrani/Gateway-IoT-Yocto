#include "connector.h"
#include <errno.h>   /* for -ENOSYS */
#include <stddef.h>  /* for size_t, NULL */

/* Poll all connectors in an array (NULL-safe, skips missing poll) */
void connectors_poll(connector_t *arr, size_t n)
{
    if (!arr) return;
    for (size_t i = 0; i < n; ++i) {
        if (arr[i].poll) {
            arr[i].poll(&arr[i]);
        }
    }
}

/* Safe wrappers so callers don't need to NULL-check function pointers */
int connector_read(connector_t *c, const char *key, double *out)
{
    if (!c || !c->read) return -ENOSYS;
    return c->read(c, key, out);
}

int connector_write(connector_t *c, const char *key, double val)
{
    if (!c || !c->write) return -ENOSYS;
    return c->write(c, key, val);
}
