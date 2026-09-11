/* Per-translation-block instruction counts, keyed by the block's start address.
   A shell step maps those to symbols with nm. */
#include <qemu-plugin.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAX_TB 65536
static struct { uint64_t pc; uint64_t count; uint32_t insns; } g_tb[MAX_TB];
static size_t g_n;

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    uint64_t pc = qemu_plugin_tb_vaddr(tb);
    size_t i;

    (void)id;
    for (i = 0; i < g_n; ++i) {
        if (g_tb[i].pc == pc && g_tb[i].insns == (uint32_t)n) {
            qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                    &g_tb[i].count, 1);
            return;
        }
    }
    if (g_n < MAX_TB) {
        g_tb[g_n].pc = pc;
        g_tb[g_n].insns = (uint32_t)n;
        qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                                &g_tb[g_n].count, 1);
        ++g_n;
    }
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
    char line[128];
    size_t i;

    (void)id; (void)p;
    for (i = 0; i < g_n; ++i) {
        snprintf(line, sizeof line, "TB %" PRIx64 " %" PRIu32 " %" PRIu64 "\n",
                 g_tb[i].pc, g_tb[i].insns, g_tb[i].count);
        qemu_plugin_outs(line);
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info,
                                           int argc, char **argv)
{
    (void)info; (void)argc; (void)argv;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
