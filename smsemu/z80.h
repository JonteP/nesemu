/*
 * z80.h
 *
 *  Created on: May 25, 2019
 *      Author: jonas
 */

#ifndef Z80_H_
#define Z80_H_
#include <stdint.h>

void opdecode(void), power_reset(void);
uint_fast8_t irqPulled;

#endif /* Z80_H_ */