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

#define BUFFER_SIZE 128

struct serial_mouse {
    struct vxt_pirepheral *uart;
    vxt_word base_port;

    vxt_byte buffer[BUFFER_SIZE];
    int buffer_len;
};

static vxt_byte pop_data(struct serial_mouse *m) {
    vxt_byte data = *m->buffer;
    memmove(m->buffer, &m->buffer[1], --m->buffer_len);
    return data;
}

static bool push_data(struct serial_mouse *m, vxt_byte data) {
    if (m->buffer_len == BUFFER_SIZE)
        return false;
    m->buffer[m->buffer_len++] = data;

    // If UART is busy, then we will get a ready callback instead.
    if (vxtu_uart_ready(m->uart)) {
        vxtu_uart_write(m->uart, *m->buffer);
        pop_data(m);
    }
    return true;
}

static void uart_ready_cb(struct vxt_pirepheral *uart, void *udata) {
    struct serial_mouse *m = (struct serial_mouse*)udata; (void)uart;
    if (m->buffer_len)
        vxtu_uart_write(uart, pop_data(m));
}

static void uart_config_cb(struct vxt_pirepheral *uart, const struct vxtu_uart_registers *regs, int idx, void *udata) {
    struct serial_mouse *m = (struct serial_mouse*)udata; (void)uart;

    // Modem Control Register
	if ((idx == 4) && (regs->mcr & 2)) {
        m->buffer_len = 0;
        push_data(m, 'M');
        VXT_LOG("Serial mouse detect!");
	}
}

static vxt_error install(struct serial_mouse *m, vxt_system *s) {
    for (int i = 0; i < VXT_MAX_PIREPHERALS; i++) {
        struct vxt_pirepheral *ip = vxt_system_pirepheral(s, (vxt_byte)i);
        if (ip && (vxt_pirepheral_class(ip) == VXT_PCLASS_UART) && (vxtu_uart_address(ip) == m->base_port)) {
            struct vxtu_uart_interface intrf = {
                .config = &uart_config_cb,
                .ready = &uart_ready_cb,
                .data = NULL,
                .udata = (void*)m
            };
            vxtu_uart_set_callbacks(ip, &intrf);
            m->uart = ip;
            break;
        }
    }
    if (!m->uart) {
        VXT_LOG("Could not find UART with base address: 0x%X", m->base_port);
        return VXT_USER_ERROR(0);
    }
    return VXT_NO_ERROR;
}

static const char *name(struct serial_mouse *m) {
    (void)m;
    return "Microsoft Serial Mouse";
}

VXT_API struct vxt_pirepheral *vxtu_mouse_create(vxt_allocator *alloc, vxt_word base_port) VXT_PIREPHERAL_CREATE(alloc, serial_mouse, {
    DEVICE->base_port = base_port;

    PIREPHERAL->install = &install;
    PIREPHERAL->name = &name;
})

VXT_API bool vxtu_mouse_push_event(struct vxt_pirepheral *p, const struct vxtu_mouse_event *ev) {
    struct serial_mouse *m = VXT_GET_DEVICE(serial_mouse, p);
    vxt_byte upper = 0;
    if (ev->xrel < 0)
        upper = 0x3;
    if (ev->yrel < 0)
        upper |= 0xC;

    return push_data(m, 0x40 | ((ev->buttons & 3) << 4) | upper) &&
        push_data(m, (vxt_byte)(ev->xrel & 0x3F)) &&
        push_data(m, (vxt_byte)(ev->yrel & 0x3F));
}
