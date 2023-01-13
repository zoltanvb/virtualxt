// Copyright (c) 2019-2022 Andreas T Jonsson <mail@andreasjonsson.se>
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
//    Portions Copyright (c) 2019-2022 Andreas T Jonsson <mail@andreasjonsson.se>
//
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.

#include "common.h"
#include "cpu.h"
#include "testing.h"

#if defined(VXT_CPU_286)
   #include "i286.inl"
#elif defined(VXT_CPU_V20)
   #include "v20.inl"
#else
   #include "i8088.inl"
#endif

static void prefetch(CONSTSP(cpu) p, int num) {
   int i = 0;
   for (; i < num; i++) {
      if (p->inst_queue_count >= 6)
         return;

      vxt_pointer ptr = VXT_POINTER(p->regs.cs, p->regs.ip + p->inst_queue_count);
      p->inst_queue_debug[p->inst_queue_count] = ptr;
      p->inst_queue[p->inst_queue_count++] = vxt_system_read_byte(p->s, ptr);
   }
   //if (i > 0)
   //   VXT_LOG("Prefetch %d bytes... %d", i, num);
}

static void do_exec(CONSTSP(cpu) p) {
   const CONSTSP(instruction) inst = &opcode_table[p->opcode];
   ENSURE(inst->opcode == p->opcode);

   // TODO: Remove this debug code.
   if (!p->opcode) {
      // Unlikely to be correct.
      if (++p->opcode_zero_count > 5) {
         p->regs.debug = true;
         p->opcode_zero_count = 0;
      }
   } else {
      p->opcode_zero_count = 0;
   }

   p->ea_cycles = 0;
   if (inst->modregrm)
      read_modregrm(p);
   inst->func(p, inst);
   p->cycles += p->ea_cycles;

   if (p->inst_queue_dirty) {
      p->inst_queue_count = 0;
   } else {
      #ifndef VXT_NO_PREFETCH
         prefetch(p, (p->cycles / 2) - p->bus_transfers); // TODO: Round up or down?
      #endif
   }
}

int cpu_step(CONSTSP(cpu) p) {
   VALIDATOR_BEGIN(p, &p->regs);
   prep_exec(p);
   if (!p->halt) {
      read_opcode(p);
      do_exec(p);
   } else {
      p->cycles++;
   }
   const CONSTSP(instruction) inst = &opcode_table[p->opcode];
   VALIDATOR_END(p, inst->name, p->opcode, inst->modregrm, p->cycles, &p->regs);
   return p->cycles;
}

void cpu_reset_cycle_count(CONSTSP(cpu) p) {
   p->cycles = 0;
}

void cpu_reset(CONSTSP(cpu) p) {
	p->trap = false;
   vxt_memclear(&p->regs, sizeof(p->regs));
   #ifdef VXT_CPU_286
      p->regs.flags = 0x2;
   #else
      p->regs.flags = 0xF002;
   #endif
   p->regs.cs = 0xFFFF;
   p->regs.debug = false;
   p->inst_queue_count = 0;
   cpu_reset_cycle_count(p);
}

vxt_byte cpu_read_byte(CONSTSP(cpu) p, vxt_pointer addr) {
    vxt_byte data = vxt_system_read_byte(p->s, addr);
    p->bus_transfers++;
    VALIDATOR_READ(p, addr, data);
    return data;
}

void cpu_write_byte(CONSTSP(cpu) p, vxt_pointer addr, vxt_byte data) {
    vxt_system_write_byte(p->s, addr, data);
    p->bus_transfers++;
    VALIDATOR_WRITE(p, addr, data);
}

vxt_word cpu_read_word(CONSTSP(cpu) p, vxt_pointer addr) {
    return WORD(cpu_read_byte(p, addr + 1), cpu_read_byte(p, addr));
}

void cpu_write_word(CONSTSP(cpu) p, vxt_pointer addr, vxt_word data) {
    vxt_system_write_byte(p->s, addr, LBYTE(data));
    vxt_system_write_byte(p->s, addr + 1, HBYTE(data));
}

TEST(register_layout,
    struct vxt_registers regs = {0};
    regs.ax = 0x0102;
    TENSURE(regs.ah == 1);
    TENSURE(regs.al == 2);

    regs.bx = 0x0304;
    TENSURE(regs.bh == 3);
    TENSURE(regs.bl == 4);
    TENSURE(regs.bx == 0x0304);

    // Make sure AX was not affected by BX.
    TENSURE(regs.ah == 1);
    TENSURE(regs.al == 2);
    TENSURE(regs.ax == 0x0102);

    regs.cx = 0x0506;
    TENSURE(regs.ch == 5);
    TENSURE(regs.cl == 6);
    TENSURE(regs.cx == 0x0506);

    regs.dx = 0x0708;
    TENSURE(regs.dh == 7);
    TENSURE(regs.dl == 8);
    TENSURE(regs.dx == 0x0708);
)
