#include "bridge.h"
#include <errno.h>

/* Apply all rules of a single bridge exactly once */
int bridge_apply_once(bridge_t *b)
{
    if (!b || !b->enabled)        return 0;
    if (!b->src || !b->dst)       return -EINVAL;
    if (!b->rules || b->nrules==0) return 0;

    int ok = 0;
    for (size_t i = 0; i < b->nrules; ++i) {
        const bridge_rule_t *r = &b->rules[i];
        if (!r->src_key || !r->dst_key) continue;

        double val = 0.0;
        int rc = connector_read(b->src, r->src_key, &val);
        if (rc < 0) {
            /* No data yet or not implemented: skip this rule */
            continue;
        }

        double out = (r->scale == 0.0 && r->offset == 0.0)
                       ? val
                       : ( (r->scale != 0.0 ? r->scale : 1.0) * val + r->offset );

        rc = connector_write(b->dst, r->dst_key, out);
        if (rc == 0) ok++;
        /* else: dst not ready / write not implemented → skip */
    }
    return ok;
}

/* Apply every bridge once; returns total successful rule transfers */
size_t bridges_apply_all(bridge_t *arr, size_t n)
{
    size_t total = 0;
    if (!arr) return 0;
    for (size_t i = 0; i < n; ++i) {
        int r = bridge_apply_once(&arr[i]);
        if (r > 0) total += (size_t)r;
    }
    return total;
}

/* A simple “tick”: poll all connectors first (to refresh caches/IO),
 * then run all bridges once.
 */
void bridges_tick(connector_t *conns, size_t nconns,
                  bridge_t *bridges, size_t nbridges)
{
    connectors_poll(conns, nconns);
    (void)bridges_apply_all(bridges, nbridges);
}
