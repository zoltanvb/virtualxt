// Copyright (c) 2019-2022 Andreas T Jonsson
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
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#ifndef _VXT_H_
#define _VXT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define VXT_VERSION_MAJOR 0
#define VXT_VERSION_MINOR 7
#define VXT_VERSION_PATCH 0

#define _VXT_STRINGIFY(x) #x
#define _VXT_VERSION(A, B, C) _VXT_STRINGIFY(A) "." _VXT_STRINGIFY(B) "." _VXT_STRINGIFY(C)
#define VXT_VERSION _VXT_VERSION(VXT_VERSION_MAJOR, VXT_VERSION_MINOR, VXT_VERSION_PATCH)

#if !defined(VXT_LIBC) && defined(TESTING)
    #define VXT_LIBC
#endif

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L
    #error libvxt require C11 support
#endif

#ifdef VXT_LIBC
    #include <stdint.h>
    #include <stdbool.h>

    typedef int8_t vxt_int8;
    typedef int16_t vxt_int16;
    typedef int32_t vxt_int32;

    typedef uint8_t vxt_byte;
    typedef uint16_t vxt_word;
    typedef uint32_t vxt_dword;
    typedef uint32_t vxt_pointer;
#else
    typedef char vxt_int8;
    typedef short vxt_int16;
    typedef int vxt_int32;

    typedef unsigned char vxt_byte;
    typedef unsigned short vxt_word;
    typedef unsigned int vxt_dword;
    typedef unsigned int vxt_pointer;

    #ifndef bool
        typedef _Bool bool;
        static const bool true = 1;
        static const bool false = 0;
    #endif

    #ifndef NULL
        #define NULL ((void*)0)
    #endif
#endif

#if defined(VXT_CLIB_ALLOCATOR) || defined(VXT_LIBC)
    #include <stdlib.h>
    static void *vxt_clib_malloc(void *p, int s) {
        return realloc(p, s);
    }
#endif

#ifdef _MSC_VER
   #define VXT_PACK(x) __pragma(pack(push, 1)) x __pragma(pack(pop))
#else
   #define VXT_PACK(x) x __attribute__((__packed__))
#endif

#define VXT_INVALID_DEVICE_ID ((vxt_device_id)0xFF)

#define _VXT_ERROR_CODES(x)                                                     \
    x(0, VXT_NO_ERROR,                  "no error")                             \
    x(1, VXT_INVALID_VERSION,           "invalid version")                      \
    x(2, VXT_INVALID_REGISTER_PACKING,  "invalid register size or packing")     \
    x(3, VXT_USER_TERMINATION,          "user requested termination")           \

#define _VXT_ERROR_ENUM(id, name, text) name = id,
typedef enum {_VXT_ERROR_CODES(_VXT_ERROR_ENUM)} vxt_error;
#undef _VXT_ERROR_ENUM

typedef vxt_byte vxt_device_id;

typedef struct system vxt_system;

typedef void *vxt_allocator(void*,int);

enum {
    VXT_CARRY     = 0x001,
    VXT_PARITY    = 0x004,
    VXT_AUXILIARY = 0x010,
    VXT_ZERO      = 0x040,
    VXT_SIGN      = 0x080,
    VXT_TRAP      = 0x100,
    VXT_INTERRUPT = 0x200,
    VXT_DIRECTION = 0x400,
    VXT_OVERFLOW  = 0x800
};

#define VXT_IO_MAP_SIZE 0x10000
#define VXT_MEM_MAP_SIZE 0x100000
#define VXT_MAX_PIREPHERALS 256

#define _VXT_REG(r) VXT_PACK(union {VXT_PACK(struct {vxt_byte r ## l; vxt_byte r ## h;}); vxt_word r ## x;})
struct vxt_registers {
    _VXT_REG(a);
    _VXT_REG(b);
    _VXT_REG(c);
    _VXT_REG(d);

    vxt_word cs, ss, ds, es;
    vxt_word sp, bp, si, di;
    vxt_word ip, flags;

    bool debug;
};
#undef _VXT_REG

struct vxt_step {
    int cycles;
    bool halted;
    vxt_error err;
};

struct vxt_io {
    vxt_byte (*in)(void*,vxt_word);
    void (*out)(void*,vxt_word,vxt_byte);

    vxt_byte (*read)(void*,vxt_pointer);
    void (*write)(void*,vxt_pointer,vxt_byte);
};

/// Represents a IO or memory mapped device.
struct vxt_pirepheral {
    void *userdata;         /// userdata is expected to be the internal device representation.
    vxt_device_id id;

	vxt_error (*install)(vxt_system*,struct vxt_pirepheral*);
    vxt_error (*destroy)(void*);
    vxt_error (*reset)(void*);
    const char* (*name)(void*);
    vxt_error (*step)(void*,int);

    struct vxt_io io;
};

/// @private
extern int _vxt_system_register_size(void);

/// @private
extern vxt_error _vxt_system_initialize(vxt_system *s);

extern vxt_allocator *vxt_static_allocator(void *mem, int size);

extern const char *vxt_error_str(vxt_error err);
extern const char *vxt_lib_version(void);
extern void vxt_set_logger(int (*f)(const char*, ...));
extern int vxt_lib_version_major(void);
extern int vxt_lib_version_minor(void);
extern int vxt_lib_version_patch(void);

static vxt_error vxt_system_initialize(vxt_system *s) {
    if (_vxt_system_register_size() != sizeof(struct vxt_registers))
        return VXT_INVALID_REGISTER_PACKING;
    //if (vxt_lib_version_major() != VXT_VERSION_MAJOR || vxt_lib_version_minor() < VXT_VERSION_MINOR)
    if (vxt_lib_version_major() != VXT_VERSION_MAJOR || vxt_lib_version_minor() != VXT_VERSION_MINOR)
        return VXT_INVALID_VERSION;
    return _vxt_system_initialize(s);
}

extern vxt_system *vxt_system_create(vxt_allocator *alloc, struct vxt_pirepheral *devs[]);
extern vxt_error vxt_system_destroy(vxt_system *s);
extern struct vxt_step vxt_system_step(vxt_system *s, int cycles);
extern void vxt_system_reset(vxt_system *s);
extern struct vxt_registers *vxt_system_registers(vxt_system *s);

extern void vxt_system_set_userdata(vxt_system *s, void *data);
extern void *vxt_system_userdata(vxt_system *s);
extern vxt_allocator *vxt_system_allocator(vxt_system *s);

extern const vxt_byte *vxt_system_io_map(vxt_system *s);
extern const vxt_byte *vxt_system_mem_map(vxt_system *s);
extern const struct vxt_pirepheral *vxt_system_pirepheral(vxt_system *s, vxt_byte idx);

extern void vxt_system_install_io_at(vxt_system *s, struct vxt_pirepheral *dev, vxt_word addr);
extern void vxt_system_install_mem_at(vxt_system *s, struct vxt_pirepheral *dev, vxt_pointer addr);
extern void vxt_system_install_io(vxt_system *s, struct vxt_pirepheral *dev, vxt_word from, vxt_word to);
extern void vxt_system_install_mem(vxt_system *s, struct vxt_pirepheral *dev, vxt_pointer from, vxt_pointer to);

extern vxt_byte vxt_system_read_byte(vxt_system *s, vxt_pointer addr);
extern void vxt_system_write_byte(vxt_system *s, vxt_pointer addr, vxt_byte data);
extern vxt_word vxt_system_read_word(vxt_system *s, vxt_pointer addr);
extern void vxt_system_write_word(vxt_system *s, vxt_pointer addr, vxt_word data);

/// @private
_Static_assert(sizeof(vxt_pointer) == 4 && sizeof(vxt_int32) == 4, "invalid integer size");

#ifdef __cplusplus
}
#endif

#endif
