#include <qemu-plugin.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t g_insns;

static void tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    (void)id;
    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &g_insns, (uint64_t)n);
}

static void at_exit(qemu_plugin_id_t id, void *p)
{
    char line[128];
    (void)id; (void)p;
    snprintf(line, sizeof(line), "INSNS %" PRIu64 "\n", g_insns);
    qemu_plugin_outs(line);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    (void)info; (void)argc; (void)argv;
    qemu_plugin_register_vcpu_tb_trans_cb(id, tb_trans);
    qemu_plugin_register_atexit_cb(id, at_exit, NULL);
    return 0;
}
