#include "mapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "globals.h"
#include "6502.h"
#include "ppu.h"
#include "cartridge.h"

uint_fast8_t mapperInt = 0, expSound = 0,
			 prgBank[8], chrBank[8];
chrtype_t chrSource[0x8];
static inline void nametable_mirroring(uint_fast8_t);

/*/////////////////////////////////*/
/*               AxROM             */
/*/////////////////////////////////*/

/* TODO:
 *
 * -bus conflicts?
 *
 * Game specific:
 * -Battletoads: crashes at level 2 - timing issue
 * */
static inline void mapper_axrom(uint_fast16_t, uint_fast8_t);

void mapper_axrom(uint_fast16_t address, uint_fast8_t value) {
	prgBank[0] = ((value & 0x07) << 3);
	prgBank[1] = prgBank[0] + 1;
	prgBank[2] = prgBank[0] + 2;
	prgBank[3] = prgBank[0] + 3;
	prgBank[4] = prgBank[0] + 4;
	prgBank[5] = prgBank[0] + 5;
	prgBank[6] = prgBank[0] + 6;
	prgBank[7] = prgBank[0] + 7;
	prg_bank_switch();
	(value & 0x10) ? (cart.mirroring = 3) : (cart.mirroring = 2);
	nametable_mirroring(cart.mirroring);
}

/*/////////////////////////////////*/
/*               CNROM             */
/*/////////////////////////////////*/

/* TODO:
 *
 * -bus conflicts
 *
 * */
static inline void mapper_cnrom(uint_fast16_t, uint_fast8_t);

void mapper_cnrom (uint_fast16_t address, uint_fast8_t value) {
	chrBank[0] = (value << 3);
	chrBank[1] = chrBank[0] + 1;
	chrBank[2] = chrBank[0] + 2;
	chrBank[3] = chrBank[0] + 3;
	chrBank[4] = chrBank[0] + 4;
	chrBank[5] = chrBank[0] + 5;
	chrBank[6] = chrBank[0] + 6;
	chrBank[7] = chrBank[0] + 7;
	chr_bank_switch();
}

/*/////////////////////////////////*/
/*               UxROM             */
/*/////////////////////////////////*/

/* TODO:
 *
 * -implement bus conflict (test roms available)
 * -should mappers 71 and 232 go here?
 *
 */

static inline void mapper_uxrom(uint_fast16_t, uint_fast8_t);
void mapper_uxrom (uint_fast16_t address, uint_fast8_t value) {
	if (!strcmp(cart.slot,"un1rom"))
		value = ((value >> 2) & 0x07);
	if (!strcmp(cart.slot,"unrom_cc")) { /* switch 0xc000 */
		prgBank[0] = 0;
		prgBank[1] = 1;
		prgBank[2] = 2;
		prgBank[3] = 3;
		prgBank[4] = (value << 2);
		prgBank[5] = prgBank[4] + 1;
		prgBank[6] = prgBank[4] + 2;
		prgBank[7] = prgBank[4] + 3;
	} else {							 /* switch 0x8000 */
		prgBank[0] = (value << 2);
		prgBank[1] = prgBank[0] + 1;
		prgBank[2] = prgBank[0] + 2;
		prgBank[3] = prgBank[0] + 3;
		prgBank[4] = cart.pSlots - 4;
		prgBank[5] = cart.pSlots - 3;
		prgBank[6] = cart.pSlots - 2;
		prgBank[7] = cart.pSlots - 1;
	}
	prg_bank_switch();
}

/*/////////////////////////////////*/
/*              MMC 1              */
/*              SxROM              */
/*/////////////////////////////////*/

/* TODO:
 *
 * -PRG RAM banking
 */

/* mmc1 globals */
static uint_fast8_t mmc1Shift = 0, mmc1Buffer = 0, mmc1Reg0 = 0x0f, mmc1Reg1 = 0, mmc1Reg2 = 0, mmc1Reg3 = 0, mmc1PrgOffset = 0;
static inline void mapper_mmc1(uint_fast16_t, uint_fast8_t), mmc1_prg_bank_switch(), mmc1_chr_bank_switch();

void mapper_mmc1(uint_fast16_t address, uint_fast8_t value) {
	/* TODO: clean implementation - mmc1 checks write cycle instead */
	if (!dummywrite) {
	if (value & 0x80) {
		mmc1Shift = 0;
		mmc1Buffer = 0;
		mmc1Reg0 |= 0x0c;
	} else {
		mmc1Buffer = (mmc1Buffer & ~(1<<mmc1Shift)) | ((value & 1)<<mmc1Shift);
		if (mmc1Shift == 4) {
			switch ((address>>13) & 3) {
			case 0: /* Control register */
				mmc1Reg0 = mmc1Buffer;
				switch (mmc1Reg0&3) {
				case 0:
					cart.mirroring = 2;
					break;
				case 1:
					cart.mirroring = 3;
					break;
				case 2:
					cart.mirroring = 1;
					break;
				case 3:
					cart.mirroring = 0;
					break;
				}
				nametable_mirroring(cart.mirroring);
				mmc1_prg_bank_switch();
				mmc1_chr_bank_switch();
				break;
			case 1: /* CHR ROM low bank */
				mmc1Reg1 = mmc1Buffer;
				if (cart.prgSize == 0x80000) {
					mmc1PrgOffset = ((mmc1Reg1&0x10) << 2);
				}
				mmc1_chr_bank_switch();
				mmc1_prg_bank_switch();
				break;
			case 2: /* CHR ROM high bank (4k mode) */
				mmc1Reg2 = mmc1Buffer;
				if (cart.prgSize == 0x80000) {
					mmc1PrgOffset = ((mmc1Reg1&0x10) << 2);
				}
				mmc1_chr_bank_switch();
				mmc1_prg_bank_switch();
				break;
			case 3: /* PRG ROM bank */
				mmc1Reg3 = mmc1Buffer;
				if (strcmp(cart.mmc1_type,"MMC1A"))
					wramEnable = !((mmc1Reg3 >> 4) & 1);
				mmc1_prg_bank_switch();
				break;
		}
			mmc1Shift = 0;
			mmc1Buffer = 0;
		} else
			mmc1Shift++;
	}
	}
}

void mmc1_prg_bank_switch() {
	 uint_fast8_t mmc1PrgSize = (mmc1Reg0 & 0x08); /* 0 32k, 1 16k */
	 uint_fast8_t mmc1PrgSelect = (mmc1Reg0 & 0x04); /* 0 low, 1 high */
	 uint_fast8_t nSlots = cart.pSlots;
	 if (cart.prgSize == 0x80000)
		 nSlots = nSlots >> 1;
	if (mmc1PrgSize) {
		if (mmc1PrgSelect) { /* switch 0x8000, fix 0xc000 to last bank */
			prgBank[0] = (mmc1Reg3 << 2) + mmc1PrgOffset;
			prgBank[1] = prgBank[0] + 1;
			prgBank[2] = prgBank[0] + 2;
			prgBank[3] = prgBank[0] + 3;
			prgBank[4] = nSlots - 4 + mmc1PrgOffset;
			prgBank[5] = prgBank[4] + 1;
			prgBank[6] = prgBank[4] + 2;
			prgBank[7] = prgBank[4] + 3;
		} else if (!mmc1PrgSelect) { /* switch 0xc000, fix 0x8000 to first bank */
			prgBank[0] = mmc1PrgOffset;
			prgBank[1] = prgBank[0] + 1;
			prgBank[2] = prgBank[0] + 2;
			prgBank[3] = prgBank[0] + 3;
			prgBank[4] = (mmc1Reg3 << 2) + mmc1PrgOffset;
			prgBank[5] = prgBank[4] + 1;
			prgBank[6] = prgBank[4] + 2;
			prgBank[7] = prgBank[4] + 3;
		}
	}
	else if (!mmc1PrgSize) {
		prgBank[0] = (mmc1Reg3 << 3) + mmc1PrgOffset;
		prgBank[1] = prgBank[0] + 1;
		prgBank[2] = prgBank[0] + 2;
		prgBank[3] = prgBank[0] + 3;
		prgBank[4] = prgBank[0] + 4;
		prgBank[5] = prgBank[0] + 5;
		prgBank[6] = prgBank[0] + 6;
		prgBank[7] = prgBank[0] + 7;
	}
	prg_bank_switch();
}

void mmc1_chr_bank_switch() {
	if (cart.chrSize)
	{
	uint_fast8_t mmc1ChrSize = (mmc1Reg0 & 0x10); /* 0 8k,  1 4k */
	if (mmc1ChrSize) { /* 4k banks */
		chrBank[0] = (mmc1Reg1 << 2);
		chrBank[1] = chrBank[0] + 1;
		chrBank[2] = chrBank[0] + 2;
		chrBank[3] = chrBank[0] + 3;
		chrBank[4] = (mmc1Reg2 << 2);
		chrBank[5] = chrBank[4] + 1;
		chrBank[6] = chrBank[4] + 2;
		chrBank[7] = chrBank[4] + 3;
	} else if (!mmc1ChrSize) { /* 8k bank */
		chrBank[0] = (mmc1Reg1 << 3);
		chrBank[1] = chrBank[0] + 1;
		chrBank[2] = chrBank[0] + 2;
		chrBank[3] = chrBank[0] + 3;
		chrBank[4] = chrBank[0] + 4;
		chrBank[5] = chrBank[0] + 5;
		chrBank[6] = chrBank[0] + 6;
		chrBank[7] = chrBank[0] + 7;
	}

	chr_bank_switch();
	}
}

/*/////////////////////////////////*/
/*              MMC 3              */
/*              TxROM              */
/*/////////////////////////////////*/

/* TODO:
 *
 *-Correct IRQ behavior - several games have issues
 * - implement TKSROM and TLSROM (mapper 118)
 *
 *Game specific:
 *-Rockman 5: IRQ issues - shaking screen
 */

static uint_fast8_t mmc3BankSelect = 0, mmc3Reg[0x08] = { 0 }, mmc3IrqEnable = 0,
		mmc3PramProtect, mmc3IrqLatch = 0, mmc3IrqReload = 0, mmc3IrqCounter = 0;
static chrtype_t mmc3ChrSource[0x8];
static inline void mapper_mmc3(uint_fast16_t, uint_fast8_t), mmc3_prg_bank_switch(), mmc3_chr_bank_switch();

void mapper_mmc3 (uint_fast16_t address, uint_fast8_t value) {
	switch ((address>>13) & 3) {
		case 0:
			if (!(address %2)) { /* Bank select (0x8000) */
				mmc3BankSelect = value;
				mmc3_chr_bank_switch();
				mmc3_prg_bank_switch();
			} else if (address %2) { /* Bank data (0x8001) */
				int bank = (mmc3BankSelect & 0x07);
				mmc3Reg[bank] = value;
				if (bank < 6)
				{
					if (!strcmp(cart.slot,"tqrom") && (value & 0x40))
					{
						mmc3ChrSource[bank] = CHR_RAM;
					}
					else if (!strcmp(cart.slot,"tqrom") && !(value & 0x40))
					{
						mmc3ChrSource[bank] = CHR_ROM;
					}
					mmc3_chr_bank_switch();
				}
				else
					mmc3_prg_bank_switch();
			}
			break;
		case 1:
			if (!(address %2)) { /* Mirroring (0xA000) */
				cart.mirroring = 1-(value & 0x01);
				nametable_mirroring(cart.mirroring);
			} else if (address%2) { /* PRG RAM protect (0xA001) */
				mmc3PramProtect = value;
			}
			break;
		case 2:
			if (!(address%2)) { /* IRQ latch (0xC000) */
				mmc3IrqLatch = value;
			} else if (address%2) { /* IRQ reload (0xC001) */
				mmc3IrqReload = 1;
				mmc3IrqCounter = 0;
			}
			break;
		case 3:
			if (!(address%2)) { /* IRQ disable and acknowledge (0xE000) */
				mmc3IrqEnable = 0;
				mapperInt = 0;
			} else if (address%2) { /* IRQ enable (0xE001) */
				mmc3IrqEnable = 1;
			}
			break;
	}
}

void mmc3_prg_bank_switch() {
	if (mmc3BankSelect & 0x40) {
		prgBank[0] = cart.pSlots - 4;
		prgBank[1] = cart.pSlots - 3;
		prgBank[4] = (mmc3Reg[6] << 1);
		prgBank[5] = prgBank[4] + 1;
	} else if (!(mmc3BankSelect & 0x40)) {
		prgBank[0] = (mmc3Reg[6] << 1);
		prgBank[1] = prgBank[0] + 1;
		prgBank[4] = cart.pSlots - 4;
		prgBank[5] = cart.pSlots - 3;
	}
	prgBank[2] = (mmc3Reg[7] << 1);
	prgBank[3] = prgBank[2] + 1;
	prg_bank_switch();
}

void mmc3_chr_bank_switch()
{
	if (mmc3BankSelect & 0x80)
	{
		chrBank[0] = mmc3Reg[2];
		chrBank[1] = mmc3Reg[3];
		chrBank[2] = mmc3Reg[4];
		chrBank[3] = mmc3Reg[5];
		chrBank[4] = (mmc3Reg[0] & 0xfe);
		chrBank[5] = (mmc3Reg[0] | 0x01);
		chrBank[6] = (mmc3Reg[1] & 0xfe);
		chrBank[7] = (mmc3Reg[1] | 0x01);
		chrSource[0] = mmc3ChrSource[2];
		chrSource[1] = mmc3ChrSource[3];
		chrSource[2] = mmc3ChrSource[4];
		chrSource[3] = mmc3ChrSource[5];
		chrSource[4] = mmc3ChrSource[0];
		chrSource[5] = mmc3ChrSource[0];
		chrSource[6] = mmc3ChrSource[1];
		chrSource[7] = mmc3ChrSource[1];
	}
	else if (!(mmc3BankSelect & 0x80))
	{
		chrBank[0] = (mmc3Reg[0] & 0xfe);
		chrBank[1] = (mmc3Reg[0] | 0x01);
		chrBank[2] = (mmc3Reg[1] & 0xfe);
		chrBank[3] = (mmc3Reg[1] | 0x01);
		chrBank[4] = mmc3Reg[2];
		chrBank[5] = mmc3Reg[3];
		chrBank[6] = mmc3Reg[4];
		chrBank[7] = mmc3Reg[5];
		chrSource[0] = mmc3ChrSource[0];
		chrSource[1] = mmc3ChrSource[0];
		chrSource[2] = mmc3ChrSource[1];
		chrSource[3] = mmc3ChrSource[1];
		chrSource[4] = mmc3ChrSource[2];
		chrSource[5] = mmc3ChrSource[3];
		chrSource[6] = mmc3ChrSource[4];
		chrSource[7] = mmc3ChrSource[5];
	}
	chr_bank_switch();
}

void mmc3_irq ()
{
	if (mmc3IrqReload)
	{
		mmc3IrqReload = 0;
		mmc3IrqCounter = mmc3IrqLatch;
		if (mmc3IrqEnable && !mmc3IrqCounter)
		{
			mapperInt = 1;
			irqPulled = 1;
			mmc3IrqReload = 1;
		}
	}
	else if (mmc3IrqCounter > 0)
	{
		mmc3IrqCounter--;
		if (mmc3IrqCounter == 0)
			{
				mmc3IrqReload = 1;
				if (mmc3IrqEnable)
				{
					mapperInt = 1;
					irqPulled = 1;
				}
			}
	}
}

/*-----------------------------------IREM------------------------------------*/

/*/////////////////////////////////*/
/*            Irem G-101           */
/*/////////////////////////////////*/

static inline void mapper_g101(uint_fast16_t, uint_fast8_t);
static uint_fast8_t g101Prg0, g101PrgMode;

void mapper_g101(uint_fast16_t address, uint_fast8_t value)
{
	if ((address & 0xf007) >= 0x8000 && (address & 0xf007) < 0x9000) {
		g101Prg0 = (value << 1);
		if (g101PrgMode) {
			prgBank[0] = cart.pSlots - 4;
			prgBank[1] = cart.pSlots - 3;
			prgBank[4] = g101Prg0;
			prgBank[5] = g101Prg0 + 1;
		} else {
			prgBank[0] = g101Prg0;
			prgBank[1] = g101Prg0 + 1;
			prgBank[4] = cart.pSlots - 4;
			prgBank[5] = cart.pSlots - 3;
		}
		prg_bank_switch();
	} else if ((address & 0xf007) >= 0x9000 && (address & 0xf007) < 0xa000) {
		g101PrgMode = (value & 0x02);
		if (!(cart.mirroring == 3))
		{
			cart.mirroring = (value & 0x01) ? 0 : 1;
			nametable_mirroring(cart.mirroring);
		}
		if (g101PrgMode) {
			prgBank[0] = cart.pSlots - 4;
			prgBank[1] = cart.pSlots - 3;
			prgBank[4] = g101Prg0;
			prgBank[5] = g101Prg0 + 1;
		} else {
			prgBank[0] = g101Prg0;
			prgBank[1] = g101Prg0 + 1;
			prgBank[4] = cart.pSlots - 4;
			prgBank[5] = cart.pSlots - 3;
		}
		prg_bank_switch();
	} else if ((address & 0xf007) >= 0xa000 && (address & 0xf007) < 0xb000) {
		prgBank[2] = (value << 1);
		prgBank[3] = prgBank[2] + 1;
		prg_bank_switch();
	} else if ((address & 0xf007) == 0xb000) {
		chrBank[0] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb001) {
		chrBank[1] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb002) {
		chrBank[2] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb003) {
		chrBank[3] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb004) {
		chrBank[4] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb005) {
		chrBank[5] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb006) {
		chrBank[6] = value;
		chr_bank_switch();
	} else if ((address & 0xf007) == 0xb007) {
		chrBank[7] = value;
		chr_bank_switch();
	}
}

/*/////////////////////////////////*/
/*           Holy Diver            */
/*/////////////////////////////////*/

static inline void mapper_holydivr(uint_fast16_t, uint_fast8_t);

void mapper_holydivr(uint_fast16_t address, uint_fast8_t value)
{
	prgBank[0] = ((value & 0x7) << 2);
	prgBank[1] = prgBank[0] + 1;
	prgBank[2] = prgBank[0] + 2;
	prgBank[3] = prgBank[0] + 3;
	prg_bank_switch();
	nametable_mirroring((value >> 3) & 1);
	chrBank[0] = ((value >> 4) << 3);
	chrBank[1] = chrBank[0] + 1;
	chrBank[2] = chrBank[0] + 2;
	chrBank[3] = chrBank[0] + 3;
	chrBank[4] = chrBank[0] + 4;
	chrBank[5] = chrBank[0] + 5;
	chrBank[6] = chrBank[0] + 6;
	chrBank[7] = chrBank[0] + 7;
	chr_bank_switch();
}

/*/////////////////////////////////*/
/*             lrog017             */
/*/////////////////////////////////*/

static inline void mapper_lrog017(uint_fast16_t, uint_fast8_t);

void mapper_lrog017(uint_fast16_t address, uint_fast8_t value)
{
	chrBank[0] = ((value >> 4) << 1);
	chrBank[1] = chrBank[0] + 1;
	chr_bank_switch();
	prgBank[0] = ((value & 0xf) << 3);
	prgBank[1] = prgBank[0] + 1;
	prgBank[2] = prgBank[0] + 2;
	prgBank[3] = prgBank[0] + 3;
	prgBank[4] = prgBank[0] + 4;
	prgBank[5] = prgBank[0] + 5;
	prgBank[6] = prgBank[0] + 6;
	prgBank[7] = prgBank[0] + 7;
	prg_bank_switch();
}

void reset_lrog017()
{
	chrSource[0] = CHR_ROM;
	chrSource[1] = CHR_ROM;
	chrSource[2] = CHR_RAM;
	chrSource[3] = CHR_RAM;
	chrSource[4] = CHR_RAM;
	chrSource[5] = CHR_RAM;
	chrSource[6] = CHR_RAM;
	chrSource[7] = CHR_RAM;
	prgBank[0] = 0;
	prgBank[1] = 1;
	prgBank[2] = 2;
	prgBank[3] = 3;
	prgBank[4] = cart.pSlots - 4;
	prgBank[5] = cart.pSlots - 3;
	prgBank[6] = cart.pSlots - 2;
	prgBank[7] = cart.pSlots - 1;
	prg_bank_switch();
	chrBank[0] = 0;
	chrBank[1] = 1;
	chrBank[2] = 0;
	chrBank[3] = 1;
	chrBank[4] = 2;
	chrBank[5] = 3;
	chrBank[6] = 4;
	chrBank[7] = 5;
	chr_bank_switch();
	nameSlot[0] = chrRam + 0x1800;
	nameSlot[1] = chrRam + 0x1c00;
	nameSlot[2] = ciram;
	nameSlot[3] = ciram + 0x400;
}

/*-----------------------------------KONAMI------------------------------------*/

/*/////////////////////////////////*/
/*            Konami VRC           */
/*/////////////////////////////////*/

static inline void vrc_clock_irq();
static uint_fast8_t vrcIrqControl = 0, vrcIrqLatch, vrcIrqCounter, vrcIrqCycles[3] = { 114, 114, 113 }, vrcIrqCc = 0;
static int16_t vrcIrqPrescale;

/*/////////////////////////////////*/
/*              VRC 1              */
/*/////////////////////////////////*/

uint_fast8_t vrc1Chr0, vrc1Chr1;
static inline void mapper_vrc1(uint_fast16_t, uint_fast8_t);

void mapper_vrc1(uint_fast16_t address, uint_fast8_t value)
{
	if (address >= 0x8000 && address <= 0x8fff) /* PRG select 0 */
	{
		prgBank[0] = ((value & 0x0f) << 1);
		prgBank[1] = prgBank[0] + 1;
		prg_bank_switch();
	}
	if (address >= 0xa000 && address <= 0xafff) /* PRG select 1 */
	{
		prgBank[2] = ((value & 0x0f) << 1);
		prgBank[3] = prgBank[2] + 1;
		prg_bank_switch();
	}
	if (address >= 0xc000 && address <= 0xcfff) /* PRG select 2 */
	{
		prgBank[4] = ((value & 0x0f) << 1);
		prgBank[5] = prgBank[4] + 1;
		prg_bank_switch();
	}
	if (address >= 0x9000 && address <= 0x9fff) /* Mirroring + CHR */
	{
		cart.mirroring = (1 - (value & 0x01));
		nametable_mirroring(cart.mirroring);
		vrc1Chr0 = ((vrc1Chr0 & 0x0f) | ((value & 0x02) << 3));
		vrc1Chr1 = ((vrc1Chr1 & 0x0f) | ((value & 0x04) << 2));
		chrBank[0] = (vrc1Chr0 << 2);
		chrBank[1] = chrBank[0] + 1;
		chrBank[2] = chrBank[0] + 2;
		chrBank[3] = chrBank[0] + 3;
		chrBank[4] = (vrc1Chr1 << 2);
		chrBank[5] = chrBank[4] + 1;
		chrBank[6] = chrBank[4] + 2;
		chrBank[7] = chrBank[4] + 3;
		chr_bank_switch();
	}
	if (address >= 0xe000 && address <= 0xefff) /* CHR select 0 */
	{
		vrc1Chr0 = ((vrc1Chr0 & 0x10) | (value & 0x0f));
		chrBank[0] = (vrc1Chr0 << 2);
		chrBank[1] = chrBank[0] + 1;
		chrBank[2] = chrBank[0] + 2;
		chrBank[3] = chrBank[0] + 3;
		chr_bank_switch();
	}
	if (address >= 0xf000 && address <= 0xffff) /* CHR select 1 */
	{
		vrc1Chr1 = ((vrc1Chr1 & 0x10) | (value & 0x0f));
		chrBank[4] = (vrc1Chr1 << 2);
		chrBank[5] = chrBank[4] + 1;
		chrBank[6] = chrBank[4] + 2;
		chrBank[7] = chrBank[4] + 3;
		chr_bank_switch();
	}
}

/*/////////////////////////////////*/
/*            VRC 2 / 4            */
/*/////////////////////////////////*/

static uint_fast8_t vrc24SwapMode = 0;
static uint_fast16_t vrcChr0 = 0, vrcChr1 = 0, vrcChr2 = 0, vrcChr3 = 0,
		 vrcChr4 = 0, vrcChr5 = 0, vrcChr6 = 0, vrcChr7 = 0;
uint_fast8_t wramBit = 0, wramBitVal;

static inline void mapper_vrc24(uint_fast16_t, uint_fast8_t), vrc24_chr_bank_switch();

void mapper_vrc24(uint_fast16_t address, uint_fast8_t value) {
	/* reroute addressing */
	if (cart.vrc24Prg1 > 1)
		address = (address & 0xff00) | ((address>>(cart.vrc24Prg1-1)) & 0x02) | ((address>>cart.vrc24Prg0) & 0x01);
	else
		address = (address & 0xff00) | ((address<<(1-cart.vrc24Prg1)) & 0x02) | ((address>>cart.vrc24Prg0) & 0x01);

	/* handle register writes */
	if ((address&0xf003) >= 0x8000 && (address&0xf003) <= 0x8003) { /* PRG select 0 */
		if (vrc24SwapMode)
		{
			prgBank[0] = cart.pSlots - 4;
			prgBank[1] = prgBank[0] + 1;
			prgBank[4] = (value << 1);
			prgBank[5] = prgBank[4] + 1;
		}
		else
		{
			prgBank[0] = (value << 1);
			prgBank[1] = prgBank[0] + 1;
			prgBank[4] = cart.pSlots - 4;
			prgBank[5] = prgBank[4] + 1;
		}
		prg_bank_switch();
	} else if ((address&0xf003) >= 0xa000  && (address&0xf003) <= 0xa003) { /* PRG select 1 */
		prgBank[2] = (value << 1);
		prgBank[3] = prgBank[2] + 1;
		prg_bank_switch();
	} else if ((address&0xf003) >= 0x9000  && (address&0xf003) <= 0x9003) { /* mirroring control */
		if (!strcmp(cart.slot,"vrc4") && (address&0xf003) >= 0x9002) {
			vrc24SwapMode = ((value >> 1) & 0x01);
			if (vrc24SwapMode)
			{
				prgBank[0] = cart.pSlots - 4;
				prgBank[1] = prgBank[0] + 1;
				prgBank[4] = (value << 1);
				prgBank[5] = prgBank[4] + 1;
			}
			else
			{
				prgBank[0] = (value << 1);
				prgBank[1] = prgBank[0] + 1;
				prgBank[4] = cart.pSlots - 4;
				prgBank[5] = prgBank[4] + 1;
			}
			prg_bank_switch();
		} else if (!strcmp(cart.slot,"vrc4") && (address&0xf003) < 0x9002) {
			switch (value & 0x03) {
			case 0:
				cart.mirroring = 1;
				break;
			case 1:
				cart.mirroring = 0;
				break;
			case 2:
				cart.mirroring = 2;
				break;
			case 3:
				cart.mirroring = 3;
				break;
			}
			nametable_mirroring(cart.mirroring);
		} else if (!strcmp(cart.slot,"vrc2")) {
			cart.mirroring = (value&1) ? 0 : 1;
			nametable_mirroring(cart.mirroring);
		}
	} else if ((address&0xf003) == 0xb000) { /* CHR select 0 low */
		chrBank[0] = (chrBank[0] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xb001) { /* CHR select 0 high */
		chrBank[0] = (chrBank[0] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xb002) { /* CHR select 1 low */
		chrBank[1] = (chrBank[1] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xb003) { /* CHR select 1 high */
		chrBank[1] = (chrBank[1] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xc000) { /* CHR select 2 low */
		chrBank[2] = (chrBank[2] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xc001) { /* CHR select 2 high */
		chrBank[2] = (chrBank[2] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xc002) { /* CHR select 3 low */
		chrBank[3] = (chrBank[3] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xc003) { /* CHR select 3 high */
		chrBank[3] = (chrBank[3] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xd000) { /* CHR select 4 low */
		chrBank[4] = (chrBank[4] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xd001) { /* CHR select 4 high */
		chrBank[4] = (chrBank[4] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xd002) { /* CHR select 5 low */
		chrBank[5] = (chrBank[5] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xd003) { /* CHR select 5 high */
		chrBank[5] = (chrBank[5] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xe000) { /* CHR select 6 low */
		chrBank[6] = (chrBank[6] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xe001) { /* CHR select 6 high */
		chrBank[6] = (chrBank[6] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xe002) { /* CHR select 7 low */
		chrBank[7] = (chrBank[7] & 0x1f0) | (value & 0xf);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xe003) { /* CHR select 7 high */
		chrBank[7] = (chrBank[7] & 0xf) | ((value & 0x1f) << 4);
		chr_bank_switch();
	} else if ((address&0xf003) == 0xf000) { /* IRQ Latch low */
		vrcIrqLatch = (vrcIrqLatch & 0xf0) | (value & 0x0f);
	} else if ((address&0xf003) == 0xf001) { /* IRQ Latch high */
		vrcIrqLatch = (vrcIrqLatch & 0x0f) | ((value & 0x0f) << 4);
	} else if ((address&0xf003) == 0xf002) { /* IRQ Control */
		vrcIrqControl = (value & 0x07);
		if (vrcIrqControl & 0x02) {
			vrcIrqCounter = vrcIrqLatch;
			vrcIrqPrescale = vrcIrqCycles[0];
			vrcIrqCc = 0;
		}
		mapperInt = 0;
	} else if ((address&0xf003) == 0xf003) { /* IRQ Acknowledge */
		mapperInt = 0;
		vrcIrqControl = ((vrcIrqControl & 0x04) | ((vrcIrqControl & 0x01) << 1) | (vrcIrqControl & 0x01));
	}
}

void vrc24_chr_bank_switch() {
	if (!strcmp(cart.slot,"vrc2")) {
		if (cart.vrc24Chr) {
			chr_1_0((vrcChr0 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_1((vrcChr1 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_2((vrcChr2 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_3((vrcChr3 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_4((vrcChr4 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_5((vrcChr5 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_6((vrcChr6 & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_7((vrcChr7 & ((cart.chrSize >> 10) -1)) * 0x400);
		} else if (!cart.vrc24Chr) {
			chr_1_0(((vrcChr0 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_1(((vrcChr1 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_2(((vrcChr2 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_3(((vrcChr3 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_4(((vrcChr4 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_5(((vrcChr5 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_6(((vrcChr6 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
			chr_1_7(((vrcChr7 >> 1) & ((cart.chrSize >> 10) -1)) * 0x400);
		}
	} else if (!strcmp(cart.slot,"vrc4")){
		chr_1_0((vrcChr0 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_1((vrcChr1 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_2((vrcChr2 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_3((vrcChr3 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_4((vrcChr4 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_5((vrcChr5 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_6((vrcChr6 & ((cart.chrSize >> 10) -1)) * 0x400);
		chr_1_7((vrcChr7 & ((cart.chrSize >> 10) -1)) * 0x400);
	}
}

/*/////////////////////////////////*/
/*              VRC 6              */
/*/////////////////////////////////*/

static uint_fast8_t vrc6Pulse1Mode, vrc6Pulse1Duty, vrc6Pulse1Volume, vrc6Pulse2Mode, vrc6Pulse2Duty, vrc6Pulse2Volume,
					vrc6SawAccumulator,	vrc6Pulse1Enable, vrc6Pulse2Enable, vrc6SawEnable, vrc6Pulse1DutyCounter, vrc6Pulse2DutyCounter,
					vrc6SawAccCounter = 0, vrc6SawAcc = 0;
static uint_fast16_t vrc6Pulse1Period, vrc6Pulse2Period, vrc6SawPeriod, vrc6Pulse1Counter = 0, vrc6Pulse2Counter = 0, vrc6SawCounter = 0;
static inline void mapper_vrc6(uint_fast16_t, uint_fast8_t);

void mapper_vrc6(uint_fast16_t address, uint_fast8_t value)
{
	address = (address & 0xff00) | ((address<<(1-cart.vrc6Prg1)) & 0x02) | ((address>>cart.vrc6Prg0) & 0x01);
	if ((address&0xf003) >= 0x8000 && (address&0xf003) <= 0x8003) /* 16k PRG select */
	{
		prgBank[0] = ((value & 0x0f) << 2);
		prgBank[1] = prgBank[0] + 1;
		prgBank[2] = prgBank[0] + 2;
		prgBank[3] = prgBank[0] + 3;
		prg_bank_switch();
	}
	else if ((address&0xf003) >= 0xc000 && (address&0xf003) <= 0xc003) /* 8k PRG select */
	{
		prgBank[4] = ((value & 0x1f) << 1);
		prgBank[5] = prgBank[4] + 1;
		prg_bank_switch();
	}
	else if ((address&0xf003) == 0xb003) /* PPU banking */
	{
	/*	printf("Mirroring: %02x, %i,%i\n",value,scanline,ppudot); */
		switch (value & 0x0c)
		{
		case 0x00:
			cart.mirroring = 1;
			break;
		case 0x04:
			cart.mirroring = 0;
			break;
		case 0x08:
			cart.mirroring = 2;
			break;
		case 0x0c:
			cart.mirroring = 3;
			break;
		}
		nametable_mirroring(cart.mirroring);
		wramEnable = (value >> 7);
	}
	else if ((address&0xf003) == 0xd000) /* CHR select */
	{
		chrBank[0] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xd001) /* CHR select */
	{
		chrBank[1] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xd002) /* CHR select */
	{
		chrBank[2] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xd003) /* CHR select */
	{
		chrBank[3] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xe000) /* CHR select */
	{
		chrBank[4] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xe001) /* CHR select */
	{
		chrBank[5] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xe002) /* CHR select */
	{
		chrBank[6] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xe003) /* CHR select */
	{
		chrBank[7] = value;
		chr_bank_switch();
	}
	else if ((address&0xf003) == 0xf000) /* IRQ Latch */
	{
		vrcIrqLatch = value;
	}
	else if ((address&0xf003) == 0xf001) /* IRQ Control */
	{
		vrcIrqControl = (value & 0x07);
		if (vrcIrqControl & 0x02)
		{
			vrcIrqCounter = vrcIrqLatch;
			vrcIrqPrescale = vrcIrqCycles[0];
			vrcIrqCc = 0;
		}
		mapperInt = 0;
	}
	else if ((address&0xf003) == 0xf002) /* IRQ Acknowledge */
	{
		mapperInt = 0;
		vrcIrqControl = ((vrcIrqControl & 0x04) | ((vrcIrqControl & 0x01) << 1) | (vrcIrqControl & 0x01));
	}
	else if ((address&0xf003) == 0x9000) /* Pulse 1 control */
	{
		vrc6Pulse1Mode = (value & 0x80);
		vrc6Pulse1Duty = ((value >> 4) & 7);
		vrc6Pulse1Volume = (value & 0xf);
	}
	else if ((address&0xf003) == 0x9001) /* Pulse 1 period low */
	{
		vrc6Pulse1Period = ((vrc6Pulse1Period & 0x0f00) | value);
	}
	else if ((address&0xf003) == 0x9002) /* Pulse 1 period high */
	{
		vrc6Pulse1Period = ((vrc6Pulse1Period & 0x00ff) | ((value & 0xf) << 8));
		vrc6Pulse1Enable = (value & 0x80);
		if (!vrc6Pulse1Enable)
			vrc6Pulse1DutyCounter = 15;
	}
	else if ((address&0xf003) == 0x9003) /* Pulse 1 frequency */
	{
		/* unused by commerical games */
	}
	else if ((address&0xf003) == 0xa000) /* Pulse 2 control */
	{
		vrc6Pulse2Mode = (value & 0x80);
		vrc6Pulse2Duty = ((value >> 4) & 7);
		vrc6Pulse2Volume = (value & 0xf);
	}
	else if ((address&0xf003) == 0xa001) /* Pulse 2 period low */
	{
		vrc6Pulse2Period = ((vrc6Pulse2Period & 0x0f00) | value);
	}
	else if ((address&0xf003) == 0xa002) /* Pulse 2 period high */
	{
		vrc6Pulse2Period = ((vrc6Pulse2Period & 0x00ff) | ((value & 0xf) << 8));
		vrc6Pulse2Enable = (value & 0x80);
		if (!vrc6Pulse2Enable)
			vrc6Pulse2DutyCounter = 15;
	}
	else if ((address&0xf003) == 0xa003) /* Pulse 2 frequency */
	{
		/* unused by commerical games */
	}
	else if ((address&0xf003) == 0xb000) /* Saw accumulator */
	{
		vrc6SawAccumulator = (value & 0x3f);
	}
	else if ((address&0xf003) == 0xb001) /* Saw period low */
	{
		vrc6SawPeriod = ((vrc6SawPeriod & 0x0f00) | value);
	}
	else if ((address&0xf003) == 0xb002) /* Saw period high */
	{
		vrc6SawPeriod = ((vrc6SawPeriod & 0x00ff) | ((value & 0xf) << 8));
		vrc6SawEnable = (value & 0x80);
		if (!vrc6SawEnable)
		{
			vrc6SawAccCounter = 0;
			vrc6SawAcc = 0;
		}
	}
}

float vrc6_sound()
{
	uint_fast8_t sample1 = 0;
	uint_fast8_t sample2 = 0;
	uint_fast8_t sample3 = (vrc6SawAcc >> 3);

	if (((vrc6Pulse1DutyCounter <= vrc6Pulse1Duty) || vrc6Pulse1Mode) && vrc6Pulse1Enable)
		sample1 = vrc6Pulse1Volume;
	if (!vrc6Pulse1Counter)
	{
		vrc6Pulse1Counter = vrc6Pulse1Period;
		if (!vrc6Pulse1DutyCounter)
			vrc6Pulse1DutyCounter = 15;
		else if (vrc6Pulse1Enable)
		{
			vrc6Pulse1DutyCounter--;
		}
	}
	else
		vrc6Pulse1Counter--;

	if (((vrc6Pulse2DutyCounter <= vrc6Pulse2Duty) || vrc6Pulse2Mode) && vrc6Pulse2Enable)
			sample2 = vrc6Pulse2Volume;
	if (!vrc6Pulse2Counter)
	{
		vrc6Pulse2Counter = vrc6Pulse2Period;
		if (!vrc6Pulse2DutyCounter)
			vrc6Pulse2DutyCounter = 15;
		else if (vrc6Pulse2Enable)
		{
			vrc6Pulse2DutyCounter--;
		}
	}
	else
		vrc6Pulse2Counter--;

	if (!vrc6SawCounter)
	{
		vrc6SawCounter = vrc6SawPeriod;
		if (vrc6SawAccCounter == 13)
		{
			vrc6SawAccCounter = 0;
			vrc6SawAcc = 0;
		}
		else if (vrc6SawEnable)
		{
			vrc6SawAccCounter++;
			if (!(vrc6SawAccCounter%2))
			{
				vrc6SawAcc += vrc6SawAccumulator;
			}
		}
	}
	else
		vrc6SawCounter--;
	return ((sample1 + sample2 + sample3));
}

void vrc_clock_irq()
{
	if (vrcIrqCounter == 0xff) {
		mapperInt = 1;
		irqPulled = 1; /* otherwise gets read too late */
		vrcIrqCounter = vrcIrqLatch;
	}
	else
	{
		vrcIrqCounter++;
	}
}

void vrc_irq() {
	if ((vrcIrqControl & 0x02)) {
		if (!(vrcIrqControl & 0x04)) {
			vrcIrqPrescale--;
			if (vrcIrqPrescale == 0)
				{
				vrcIrqPrescale = vrcIrqCycles[vrcIrqCc++];
				if (vrcIrqCc == 3)
					vrcIrqCc = 0;
				vrc_clock_irq();
				}
		} else if (vrcIrqControl & 0x04) {
			vrc_clock_irq();
		}
	}
}

void reset_default()
{
	if (cart.chrSize)
	{
		chrSource[0] = CHR_ROM;
		chrSource[1] = CHR_ROM;
		chrSource[2] = CHR_ROM;
		chrSource[3] = CHR_ROM;
		chrSource[4] = CHR_ROM;
		chrSource[5] = CHR_ROM;
		chrSource[6] = CHR_ROM;
		chrSource[7] = CHR_ROM;
	}
	else
	{
		chrSource[0] = CHR_RAM;
		chrSource[1] = CHR_RAM;
		chrSource[2] = CHR_RAM;
		chrSource[3] = CHR_RAM;
		chrSource[4] = CHR_RAM;
		chrSource[5] = CHR_RAM;
		chrSource[6] = CHR_RAM;
		chrSource[7] = CHR_RAM;

	}
	prgBank[0] = 0;
	prgBank[1] = 1;
	prgBank[2] = 2;
	prgBank[3] = 3;
	prgBank[4] = cart.pSlots - 4;
	prgBank[5] = cart.pSlots - 3;
	prgBank[6] = cart.pSlots - 2;
	prgBank[7] = cart.pSlots - 1;
	prg_bank_switch();
	chrBank[0] = 0;
	chrBank[1] = 1;
	chrBank[2] = 2;
	chrBank[3] = 3;
	chrBank[4] = 4;
	chrBank[5] = 5;
	chrBank[6] = 6;
	chrBank[7] = 7;
	chr_bank_switch();
	nametable_mirroring(cart.mirroring);
}

void prg_bank_switch()
{
	prgSlot[0] = &prg[((prgBank[0] & (cart.pSlots - 1)) << 12)];
	prgSlot[1] = &prg[((prgBank[1] & (cart.pSlots - 1)) << 12)];
	prgSlot[2] = &prg[((prgBank[2] & (cart.pSlots - 1)) << 12)];
	prgSlot[3] = &prg[((prgBank[3] & (cart.pSlots - 1)) << 12)];
	prgSlot[4] = &prg[((prgBank[4] & (cart.pSlots - 1)) << 12)];
	prgSlot[5] = &prg[((prgBank[5] & (cart.pSlots - 1)) << 12)];
	prgSlot[6] = &prg[((prgBank[6] & (cart.pSlots - 1)) << 12)];
	prgSlot[7] = &prg[((prgBank[7] & (cart.pSlots - 1)) << 12)];
}

void chr_bank_switch()
{
	chrSlot[0] = (chrSource[0] == CHR_RAM) ? &chrRam[((chrBank[0] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[0] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[1] = (chrSource[1] == CHR_RAM) ? &chrRam[((chrBank[1] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[1] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[2] = (chrSource[2] == CHR_RAM) ? &chrRam[((chrBank[2] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[2] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[3] = (chrSource[3] == CHR_RAM) ? &chrRam[((chrBank[3] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[3] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[4] = (chrSource[4] == CHR_RAM) ? &chrRam[((chrBank[4] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[4] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[5] = (chrSource[5] == CHR_RAM) ? &chrRam[((chrBank[5] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[5] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[6] = (chrSource[6] == CHR_RAM) ? &chrRam[((chrBank[6] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[6] & ((cart.chrSize >> 10) - 1)) << 10)];
	chrSlot[7] = (chrSource[7] == CHR_RAM) ? &chrRam[((chrBank[7] & ((cart.cramSize >> 10) - 1)) << 10)] : &chrRom[((chrBank[7] & ((cart.chrSize >> 10) - 1)) << 10)];
}

void nametable_mirroring(uint_fast8_t mode)
{
	switch (mode)
	{
	case 0: /* horizontal */
		nameSlot[0] = ciram;
		nameSlot[1] = ciram;
		nameSlot[2] = ciram + 0x400;
		nameSlot[3] = ciram + 0x400;
		break;
	case 1: /* vertical */
		nameSlot[0] = ciram;
		nameSlot[1] = ciram + 0x400;
		nameSlot[2] = ciram;
		nameSlot[3] = ciram + 0x400;
		break;
	case 2: /* one-page low */
		nameSlot[0] = ciram;
		nameSlot[1] = ciram;
		nameSlot[2] = ciram;
		nameSlot[3] = ciram;
		break;
	case 3: /* one-page high */
		nameSlot[0] = ciram + 0x400;
		nameSlot[1] = ciram + 0x400;
		nameSlot[2] = ciram + 0x400;
		nameSlot[3] = ciram + 0x400;
		break;
	}
}

void init_mapper() {
	if(!strcmp(cart.slot,"nrom")) {
		reset_default();
	} else if(!strcmp(cart.slot,"sxrom") ||
				!strcmp(cart.slot,"sxrom_a") ||
					!strcmp(cart.slot,"sorom") ||
						!strcmp(cart.slot,"sorom_a")) {
		if ((cart.wramSize+cart.bwramSize) && (!strcmp(cart.mmc1_type,"MMC1A") ||
				!strcmp(cart.mmc1_type,"MMC1B1") ||
					!strcmp(cart.mmc1_type,"MMC1B1-H") ||
						!strcmp(cart.mmc1_type,"MMC1B2") ||
							!strcmp(cart.mmc1_type,"MMC1B3"))) {
			wramEnable = 1;
		}
		write_mapper_register = &mapper_mmc1;
		reset_default();
	} else if(!strcmp(cart.slot,"uxrom") ||
				!strcmp(cart.slot,"un1rom") ||
					!strcmp(cart.slot,"unrom_cc")) {
		write_mapper_register = &mapper_uxrom;
		reset_default();
	} else if (!strcmp(cart.slot,"cnrom")) {
		write_mapper_register = &mapper_cnrom;
		reset_default();
	} else if (!strcmp(cart.slot,"axrom")) {
		write_mapper_register = &mapper_axrom;
		reset_default();
	} else if (!strcmp(cart.slot,"txrom") || !strcmp(cart.slot,"tqrom")) {
		write_mapper_register = &mapper_mmc3;
		reset_default();
		memcpy(&mmc3ChrSource, &chrSource, sizeof(chrSource));
	} else if (!strcmp(cart.slot,"vrc1")) {
		write_mapper_register = &mapper_vrc1;
		reset_default();
	}
	else if (!strcmp(cart.slot,"vrc2") ||
			!strcmp(cart.slot,"vrc4")) {
		write_mapper_register = &mapper_vrc24;
		if (!strcmp(cart.slot,"vrc2") && (!cart.wramSize && !cart.bwramSize))
			wramBit = 1;
		reset_default();
	} else if (!strcmp(cart.slot,"vrc6")) {
		write_mapper_register = &mapper_vrc6;
	    expansion_sound = &vrc6_sound;
	    expSound = 1;
		reset_default();
	} else if (!strcmp(cart.slot,"g101")) {
		write_mapper_register = &mapper_g101;
		reset_default();
	} else if (!strcmp(cart.slot,"lrog017")) {
		write_mapper_register = &mapper_lrog017;
		reset_lrog017();
	} else if (!strcmp(cart.slot,"holydivr")) {
			write_mapper_register = &mapper_holydivr;
			reset_default();
		}
	else
		reset_default();
}
