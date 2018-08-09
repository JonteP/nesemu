#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>	/* malloc; exit */
#include <string.h>	/* memcpy */
#include <time.h>
#include <unistd.h>
#include "globals.h"
#include "nestools.h"
#include "SDL.h"
#include "ppu.h"
#include "apu.h"
#include "6502.h"
#include "sha.h"
#include "parser.h"
#include "tree.h"
#include "mapper.h"
#include "my_sdl.h"

/* TODO:
 *
 * -battery backed saves
 * -cycle correct cpu emulation (prefetches, read-modify-writes and everything)
 * -dot rendering with correct timing
 * -FIR low pass filtering of sound
 * -fix sound buffering (if broken)
 *
 * Features:
 * -file load routines
 * -oam viewer
 * -save states
 */

static inline void extract_xml_data(xmlNode * s_node), xml_hash_compare(xmlNode * c_node), parse_xml_file(xmlNode * a_node);

/* constants */
const uint8_t id[4] = { 0x4e, 0x45, 0x53, 0x1a };
gameInfos gameInfo;
gameFeatures cart;
int psize, csize;
xmlDoc *nesXml;
xmlNode *root;
xmlChar *sphash, *schash;
unsigned char phash[SHA_DIGEST_LENGTH], chash[SHA_DIGEST_LENGTH];

uint8_t quit = 0, ctrb = 0, ctrb2 = 0, ctr1 = 0, ctr2 = 0, nmiAlreadyDone = 0,
		nmiDelayed = 0;
uint8_t header[0x10], cpu[0x10000] = { 0 }, wramEnable = 0, openBus;
uint8_t *prg, *chr, *cram;
uint16_t ppu_wait = 0, apu_wait = 0, nmi_wait = 0;
uint8_t mirroring[4][4] = { { 0, 0, 1, 1 },
							{ 0, 1, 0, 1 },
							{ 0, 0, 0, 0 },
							{ 1, 1, 1, 1 } };
int32_t cpucc = 0;
FILE *rom, *logfile;

int main() {
	rom = fopen("/home/jonas/eclipse-workspace/"
			"mmc1/dw3.nes", "rb");
	if (rom == NULL) {
		printf("Error: No such file\n");
		exit(EXIT_FAILURE);
	}
	fread(header, sizeof(header), 1, rom);
	for (int i = 0; i < sizeof(id); i++) {
		if (header[i] != id[i]) {
			printf("Error: Invalid iNES header!\n");
			exit(EXIT_FAILURE);
		}
	}

	/* read data from ROM */
	psize = header[4] * PRG_BANK<<2;
	csize = header[5] * CHR_BANK<<3;

	prg = malloc(psize);
	fread(prg, psize, 1, rom);
	SHA1(prg,psize,phash);

	sphash = malloc(SHA_DIGEST_LENGTH*2+1);
	for (int i = 0; i<sizeof(phash); i++) {
		sprintf(sphash+i*2, "%02x", phash[i]);
	}
	nesXml = xmlReadFile("/home/jonas/eclipse-workspace/nes.xml", NULL, 0);
	root = xmlDocGetRootElement(nesXml);
	parse_xml_file(root);
	xmlFreeDoc(nesXml);
	xmlCleanupParser();

	if (csize) {
		chr = malloc(csize);
		fread(chr, csize, 1, rom);
	} else {
		csize = cart.cramSize;
		chr = malloc(csize);
	}
	fclose(rom);

	printf("PCB: %s\n",cart.pcb);
	printf("PRG size: %i bytes\n",cart.prgSize);
	printf("CHR size: %i bytes\n",cart.chrSize);
	printf("WRAM size: %i bytes\n",cart.wramSize);
	printf("BWRAM size: %i bytes\n",cart.bwramSize);
	printf("CHRRAM size: %i bytes\n",cart.cramSize);
/*	mirrmode = (header[6] & 1); */
	/* 0 = horizontal mirroring
	 * 1 = vertical mirroring
	 * 2 = one screen, low page
	 * 3 = one screen, high page
	 * 4 = 4 screen
	 */

	logfile = fopen("/home/jonas/eclipse-workspace/logfile.txt","w");
	if (logfile==NULL)
		printf("Error: Could not create logfile\n");

	power_reset(0);
	init_sdl();
	init_time();

	while (quit == 0) {
			if (nmiDelayed) {
				nmiDelayed = 0;
			}
			run_ppu(ppu_wait);
			run_apu(apu_wait);
			opdecode();

			/* Interrupt handling */
			if (nmiIsTriggered >= ppucc-1) /*TODO: correct behavior? Probably depends on opcode */
				nmiDelayed = 1;
			if ((ppuController & 0x80) && nmiIsTriggered && !nmiAlreadyDone && !nmiDelayed) {
				interrupt_handle(NMI);
				nmiAlreadyDone = 1;
				nmiIsTriggered = 0;
			}
	}
	fclose(logfile);
	free(prg);
	free(chr);
	close_sdl();
}

void extract_xml_data(xmlNode * s_node) {
	xmlNode *cur_node = s_node->children;
	xmlChar *nam, *val;
	while (cur_node) {
		if (cur_node->type == XML_ELEMENT_NODE && !xmlStrcmp(cur_node->name, (xmlChar *)"feature")) {
			nam = xmlGetProp(cur_node, (xmlChar *)"name");
			val = xmlGetProp(cur_node, (xmlChar *)"value");
			if (!xmlStrcmp(nam,(xmlChar *)"slot"))
				strcpy(cart.slot,(char *)val);
			else if (!xmlStrcmp(nam,(xmlChar *)"pcb"))
				strcpy(cart.pcb,(char *)val);
			else if (!xmlStrcmp(nam,(xmlChar *)"vrc2-pin3"))
				cart.vrcPrg1 = strtol(val+5,NULL,10);
			else if (!xmlStrcmp(nam,(xmlChar *)"vrc2-pin4"))
				cart.vrcPrg0 = strtol(val+5,NULL,10);
			else if (!xmlStrcmp(nam,(xmlChar *)"vrc4-pin3"))
				cart.vrcPrg1 = strtol(val+5,NULL,10);
			else if (!xmlStrcmp(nam,(xmlChar *)"vrc4-pin4"))
				cart.vrcPrg0 = strtol(val+5,NULL,10);
			else if (!xmlStrcmp(nam,(xmlChar *)"vrc2-pin21")) {
				if (!xmlStrcmp(val,(xmlChar *)"NC"))
					cart.vrcChr = 0;
				else
					cart.vrcChr = 1;
			}
			else if (!xmlStrcmp(nam,(xmlChar *)"mmc1_type"))
				strcpy(cart.mmc1_type,(char *)val);
			else if (!xmlStrcmp(nam,(xmlChar *)"mirroring")) {
				if (!xmlStrcmp(val,(xmlChar *)"horizontal"))
					cart.mirroring = 0;
				else if (!xmlStrcmp(val,(xmlChar *)"vertical"))
					cart.mirroring = 1;
				else if (!xmlStrcmp(val,(xmlChar *)"high"))
					cart.mirroring = 3;
			}
			xmlFree(nam);
			xmlFree(val);

		} else if (cur_node->type == XML_ELEMENT_NODE && !xmlStrcmp(cur_node->name, (xmlChar *)"dataarea")) {
				nam = xmlGetProp(cur_node, (xmlChar *)"name");
				if (cur_node->children->next)
					val = xmlGetProp(cur_node->children->next, (xmlChar *)"size");
				else
					val = xmlGetProp(cur_node, (xmlChar *)"size");
				if (!xmlStrcmp(nam,(xmlChar *)"prg"))
					cart.prgSize = strtol(val,NULL,10);
				else if (!xmlStrcmp(nam,(xmlChar *)"chr"))
					cart.chrSize = strtol(val,NULL,10);
				else if (!xmlStrcmp(nam,(xmlChar *)"wram"))
					cart.wramSize = strtol(val,NULL,10);
				else if (!xmlStrcmp(nam,(xmlChar *)"bwram"))
					cart.bwramSize = strtol(val,NULL,10);
				else if (!xmlStrcmp(nam,(xmlChar *)"vram"))
					cart.cramSize = strtol(val,NULL,10);
				xmlFree(nam);
				xmlFree(val);
		}
		cur_node = cur_node->next;
	}
}

void xml_hash_compare(xmlNode * c_node) {
	xmlNode *cur_node = NULL;
	xmlChar *hashkey;
	for (cur_node = c_node; cur_node; cur_node = cur_node->next) {
		if (cur_node->type == XML_ELEMENT_NODE && !xmlStrcmp(cur_node->name, (const xmlChar *) "rom")) {
		    hashkey = xmlGetProp(cur_node, (xmlChar *)"sha1");
		    if (!xmlStrcmp(hashkey,sphash)) {
		    	extract_xml_data(cur_node->parent->parent);
		    }
		    xmlFree(hashkey);
		}
	}
}
void parse_xml_file(xmlNode * a_node) {
	xmlNode *cur_node = NULL;
	xmlChar *key;

    for (cur_node = a_node; cur_node; cur_node = cur_node->next) {
    	if (cur_node->type == XML_ELEMENT_NODE && !xmlStrcmp(cur_node->name, (const xmlChar *) "dataarea")) {
    		key = xmlGetProp(cur_node, (xmlChar *)"name");
    		if (!xmlStrcmp(key, (xmlChar *)"prg")) {
    			xml_hash_compare(cur_node->children);
    		}
    		xmlFree(key);
        }
        parse_xml_file(cur_node->children);
    }
}
