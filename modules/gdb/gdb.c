// Copyright (c) 2019-2023 Andreas T Jonsson <mail@andreasjonsson.se>
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software in
//    a product, an acknowledgment (see the following) in the product
//    documentation is required.
//
//    Portions Copyright (c) 2019-2023 Andreas T Jonsson <mail@andreasjonsson.se>
//
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.

#include <vxt/vxtu.h>

#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #define close closesocket
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
#endif

#define ALL_FLAGS (VXT_CARRY|VXT_PARITY|VXT_AUXILIARY|VXT_ZERO|VXT_SIGN|VXT_TRAP|VXT_INTERRUPT|VXT_DIRECTION|VXT_OVERFLOW)
#define MAX_BREAKPOINTS 64

typedef vxt_pointer address;
typedef vxt_dword reg;

enum GDB_REGISTER {
    GDB_CPU_I386_REG_EAX  = 0,
    GDB_CPU_I386_REG_ECX  = 1,
    GDB_CPU_I386_REG_EDX  = 2,
    GDB_CPU_I386_REG_EBX  = 3,
    GDB_CPU_I386_REG_ESP  = 4,
    GDB_CPU_I386_REG_EBP  = 5,
    GDB_CPU_I386_REG_ESI  = 6,
    GDB_CPU_I386_REG_EDI  = 7,
    GDB_CPU_I386_REG_PC   = 8,
    GDB_CPU_I386_REG_PS   = 9,
    GDB_CPU_I386_REG_CS   = 10,
    GDB_CPU_I386_REG_SS   = 11,
    GDB_CPU_I386_REG_DS   = 12,
    GDB_CPU_I386_REG_ES   = 13,
    GDB_CPU_I386_REG_FS   = 14,
    GDB_CPU_I386_REG_GS   = 15,
    GDB_CPU_NUM_REGISTERS = 16
};

struct breakpoint {
    vxt_pointer addr;
    int ty;
    int size;
};

struct gdb_state {
    int client;
    vxt_system *sys;

    struct breakpoint bps[MAX_BREAKPOINTS];
    int num_bps;

    int signum;
    reg registers[GDB_CPU_NUM_REGISTERS];
};

#include "gdbstub/gdbstub.h"

struct gdb {
	vxt_byte mem_map[VXT_MEM_MAP_SIZE];

    vxt_word port;
    int server;
    vxt_timer_id reconnect_timer;
    struct gdb_state state;
};

static vxt_byte mem_read(struct gdb *dbg, vxt_pointer addr) {
    vxt_system *s = vxt_pirepheral_system(VXT_GET_PIREPHERAL(dbg));

    if (dbg->state.client != -1) {
        struct vxt_registers *vreg = vxt_system_registers(s);
        for (int i = 0; i < dbg->state.num_bps; i++) {
            struct breakpoint *bp = &dbg->state.bps[i];
            if (VXT_POINTER(vreg->cs, vreg->ip) == bp->addr && bp->ty > 2) {
                VXT_LOG("Trigger memory read watch: 0x%X", addr);
                vreg->debug = true;
                break;
            }
        }
    }

    struct vxt_pirepheral *p = vxt_system_pirepheral(s, dbg->mem_map[addr >> 4]);
    return p->io.read(VXT_GET_DEVICE_PTR(p), addr);
}

static void mem_write(struct gdb *dbg, vxt_pointer addr, vxt_byte data) {
    vxt_system *s = vxt_pirepheral_system(VXT_GET_PIREPHERAL(dbg));

    if (dbg->state.client != -1) {
        struct vxt_registers *vreg = vxt_system_registers(s);
        for (int i = 0; i < dbg->state.num_bps; i++) {
            struct breakpoint *bp = &dbg->state.bps[i];
            if (VXT_POINTER(vreg->cs, vreg->ip) == bp->addr && (bp->ty == 2 || bp->ty == 4)) {
                VXT_LOG("Trigger memory write watch: 0x%X", addr);
                vreg->debug = true;
                break;
            }
        }
    }

    struct vxt_pirepheral *p = vxt_system_pirepheral(s, dbg->mem_map[addr >> 4]);
    p->io.write(VXT_GET_DEVICE_PTR(p), addr, data);
}

static bool open_server_socket(struct gdb *dbg) {
    struct sockaddr_in addr;
    vxt_memclear(&addr, sizeof(addr));
    addr.sin_port = htons(dbg->port);
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_family = AF_INET;

    if ((dbg->server = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        VXT_LOG("ERROR: Could not open socket for GDB server!");
        return false;
    }

    #ifdef _WIN32
        unsigned long mode = 1;
        ioctlsocket(dbg->server, FIONBIO, &mode);
    #else
        fcntl(dbg->server, F_SETFL, O_NONBLOCK);
    #endif

    if (bind(dbg->server, (struct sockaddr*)&addr, sizeof(addr))) {
        VXT_LOG("ERROR: Could not bind GDB server socket!");
        return false;
    }

    if (listen(dbg->server, 1)) {
        VXT_LOG("ERROR: No clients connected!");
        return false;
    }
    return true;
}

static bool accept_client(struct gdb *dbg, vxt_system *sys) {
    if (dbg->state.client != -1)
        return false;

    struct sockaddr_in addr;
    socklen_t ln = sizeof(addr);
    if ((dbg->state.client = accept(dbg->server, (struct sockaddr*)&addr, &ln)) == -1)
        return false;

    dbg->state.num_bps = 0;
    dbg->state.sys = sys;
    vxt_system_registers(dbg->state.sys)->debug = true;
    VXT_LOG("Client connected!");
    return true;
}

static vxt_error install(struct gdb *dbg, vxt_system *s) {
    struct vxt_pirepheral *p = VXT_GET_PIREPHERAL(dbg);

    #ifdef _WIN32
        WSADATA ws_data;
        if (WSAStartup(MAKEWORD(2, 2), &ws_data)) {
            VXT_LOG("ERROR: WSAStartup failed!");
            return VXT_USER_ERROR(0);
        }
    #endif

    if (!open_server_socket(dbg))
        return VXT_USER_ERROR(1);

    const vxt_byte *mem = vxt_system_mem_map(s);
    for (int i = 0; i < VXT_MEM_MAP_SIZE; i++)
        dbg->mem_map[i] = mem[i];

    vxt_system_install_mem(s, p, 0, 0xFFFFF);
    vxt_system_install_timer(s, p, 0);
    dbg->reconnect_timer = vxt_system_install_timer(s, p, 1000000);

    return VXT_NO_ERROR;
}

static vxt_error timer(struct gdb *dbg, vxt_timer_id id, int cycles) {
    (void)cycles;
    vxt_system *s = vxt_pirepheral_system(VXT_GET_PIREPHERAL(dbg));
    if (id == dbg->reconnect_timer) {
        accept_client(dbg, s);
        return VXT_NO_ERROR;
    }

    struct vxt_registers *vreg = vxt_system_registers(s);
    for (int i = 0; i < dbg->state.num_bps; i++) {
        struct breakpoint *bp = &dbg->state.bps[i];
        if (VXT_POINTER(vreg->cs, vreg->ip) == bp->addr && bp->ty < 2) {
            vreg->debug = true;
            break;
        }
    }
    
    if (vreg->debug) {
        VXT_LOG("Debug trap!");

        while (dbg->state.client == -1)
            accept_client(dbg, s);

        dbg->state.signum = 5;
        reg *r = dbg->state.registers;
        
        r[GDB_CPU_I386_REG_EAX] = vreg->ax;
        r[GDB_CPU_I386_REG_EBX] = vreg->bx;
        r[GDB_CPU_I386_REG_ECX] = vreg->cx;
        r[GDB_CPU_I386_REG_EDX] = vreg->dx;

        r[GDB_CPU_I386_REG_EBP] = vreg->bp;
        r[GDB_CPU_I386_REG_ESI] = vreg->si;
        r[GDB_CPU_I386_REG_EDI] = vreg->di;
        
        r[GDB_CPU_I386_REG_CS] = vreg->cs;
        r[GDB_CPU_I386_REG_SS] = vreg->ss;
        r[GDB_CPU_I386_REG_DS] = vreg->ds;
        r[GDB_CPU_I386_REG_ES] = vreg->es;

        r[GDB_CPU_I386_REG_PS] = vreg->flags;
        r[GDB_CPU_I386_REG_PC] = vreg->cs * 16 + vreg->ip;
        r[GDB_CPU_I386_REG_ESP] = vreg->ss * 16 + vreg->sp;
        r[GDB_CPU_I386_REG_FS] = r[GDB_CPU_I386_REG_GS] = 0;

        if (gdb_main(&dbg->state)) {
            VXT_LOG("Client disconnected!");
            close(dbg->state.client);
            dbg->state.client = -1;
            vreg->debug = false;
        }

        vreg->ax = (vxt_word)r[GDB_CPU_I386_REG_EAX];
        vreg->bx = (vxt_word)r[GDB_CPU_I386_REG_EBX];
        vreg->cx = (vxt_word)r[GDB_CPU_I386_REG_ECX];
        vreg->dx = (vxt_word)r[GDB_CPU_I386_REG_EDX];
        
        vreg->bp = (vxt_word)r[GDB_CPU_I386_REG_EBP];
        vreg->si = (vxt_word)r[GDB_CPU_I386_REG_ESI];
        vreg->di = (vxt_word)r[GDB_CPU_I386_REG_EDI];
        
        vreg->cs = (vxt_word)r[GDB_CPU_I386_REG_CS];
        vreg->ss = (vxt_word)r[GDB_CPU_I386_REG_SS];
        vreg->ds = (vxt_word)r[GDB_CPU_I386_REG_DS];
        vreg->es = (vxt_word)r[GDB_CPU_I386_REG_ES];

        vreg->flags = (vxt_word)(r[GDB_CPU_I386_REG_PS] & ALL_FLAGS) | 0xF002;
        vreg->ip = (vxt_word)(r[GDB_CPU_I386_REG_PC] - r[GDB_CPU_I386_REG_CS] * 16);
        vreg->sp = (vxt_word)(r[GDB_CPU_I386_REG_ESP] - r[GDB_CPU_I386_REG_SS] * 16);
    }
    return VXT_NO_ERROR;
}

static enum vxt_pclass pclass(struct gdb *dbg) {
    (void)dbg; return VXT_PCLASS_DEBUGGER;
}

static const char *name(struct gdb *dbg) {
    (void)dbg; return "GDB Server";
}

static vxt_error destroy(struct gdb *dbg) {
    if (dbg->state.client != -1)
        close(dbg->state.client);
    if (dbg->server != -1)
        close(dbg->server);
    vxt_system_allocator(VXT_GET_SYSTEM(dbg))(VXT_GET_PIREPHERAL(dbg), 0);
    return VXT_NO_ERROR;
}

int gdb_sys_getc(struct gdb_state *state) {
    char buf[1] = {GDB_EOF};
    if (recv(state->client, buf, 1, 0) != 1) {
        close(state->client);
        state->client = -1;
        return GDB_EOF;
    }
    return *buf;
}

int gdb_sys_putchar(struct gdb_state *state, int ch) {
    char buf[1] = {(char)ch};
    ssize_t ret = send(state->client, buf, 1, 0);
    if (ret != 1) {
        close(state->client);
        state->client = -1;
        return GDB_EOF;
    }
    return (int)ret;
}

int gdb_sys_mem_readb(struct gdb_state *state, address addr, char *val) {
    *val = (char)vxt_system_read_byte(state->sys, addr);
    return 0;
}

int gdb_sys_mem_writeb(struct gdb_state *state, address addr, char val) {
    vxt_system_write_byte(state->sys, addr, (vxt_byte)val);
    return 0;
}

int gdb_sys_continue(struct gdb_state *state) {
    vxt_system_registers(state->sys)->debug = false;
    return 0;
}

int gdb_sys_step(struct gdb_state *state) {
    (void)state;
    return 0;
}

int gdb_sys_insert(struct gdb_state *state, unsigned int ty, address addr, unsigned int kind) {
    if (state->num_bps == MAX_BREAKPOINTS)
        return -1;

    struct breakpoint *bp = &state->bps[state->num_bps++];
    bp->addr = (vxt_pointer)addr;
    bp->ty = (int)ty;
    bp->size = (int)kind;

    if (kind > 1)
        VXT_LOG("WARNING: GDB server only supports single byte watches!");
    
    VXT_LOG("Insert breakpoint at: 0x%X", addr);
    return 0;
}

int gdb_sys_remove(struct gdb_state *state, unsigned int ty, address addr, unsigned int kind) {
    for (int i = 0; i < state->num_bps; i++) {
        struct breakpoint *bp = &state->bps[i];
        if (bp->addr == (vxt_pointer)addr && bp->ty == (int)ty && bp->size == (int)kind) {
            *bp = state->bps[--state->num_bps];
            VXT_LOG("Remove breakpoint at: 0x%X", addr);
            return 0;
        }
    }
    return -1;
}

VXTU_MODULE_CREATE(gdb, {
    DEVICE->port = (vxt_word)atoi(ARGS);
    DEVICE->server = DEVICE->state.client = -1; 

    PIREPHERAL->install = &install;
    PIREPHERAL->timer = &timer;
    PIREPHERAL->pclass = &pclass;
    PIREPHERAL->name = &name;
    PIREPHERAL->destroy = &destroy;
    PIREPHERAL->io.read = &mem_read;
    PIREPHERAL->io.write = &mem_write;
})
