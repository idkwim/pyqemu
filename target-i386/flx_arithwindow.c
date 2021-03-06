#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <avl.h>
#include <unistd.h>

#include "flx_disas.h"
#include "flx_instrument.h"
#include "flx_arithwindow.h"
#include "flx_bbltranslate.h"
#include "flx_bbltrace.h"

/*
 * This module is used to calcualte arithmetic and bitwise fractions of executed instructions in
 * a predefined execution window. The window size parameter can be set through flx_arithwindow_enable
 */

arithwindow_handler flx_arithwindow_handler = NULL;

uint16_t arithwindow_cache[65536];

/*
 * Used to store the current basic block window to consider when doing
 * arithmetic fraction calculations
 */
typedef struct {
	flx_bbl** bbls;
	uint32_t window_size;
	uint32_t start_index;
	uint32_t end_index;
	uint32_t instructions;
	uint32_t arith_instructions;
	uint32_t mov_instructions;
	float    arith_percentage;
} bbl_window;

bbl_window flx_bbl_window;

static inline void flx_arithwindow_cache_init(void);
static inline int flx_arithwindow_cache_search(uint32_t);
static inline void flx_arithwindow_cache_del(uint32_t);
static inline void flx_arithwindow_cache_add(uint32_t);

static inline void flx_arithwindow_cache_init(void){
	memset(arithwindow_cache, 0, sizeof(arithwindow_cache));
}

static inline int flx_arithwindow_cache_search(uint32_t addr){
	if(arithwindow_cache[addr&0xffff] == addr>>16)
		return 1;
	return 0;
}
static inline void flx_arithwindow_cache_add(uint32_t addr){
	arithwindow_cache[addr&0xffff] = addr>>16;
}

static inline void flx_arithwindow_cache_del(uint32_t addr){
	arithwindow_cache[addr&0xffff] = 0;
}

void flx_arithwindow_init(arithwindow_handler handler){
	memset(&flx_bbl_window, 0, sizeof(flx_bbl_window));

	flx_arithwindow_handler = handler;

	flx_bbltrace_enable();
	flx_bbltranslate_enable();
}

void flx_arithwindow_destroy(void){
	flx_arithwindow_disable();
	free(flx_bbl_window.bbls);
}

/*
 * Activate heuristic and define parameters 
 */
void flx_arithwindow_enable(uint32_t window_size, float arith_percentage){
	flx_arithwindow_cache_init();
	flx_bbl_window.bbls = malloc(sizeof(flx_bbl*) * window_size);
	flx_bbl_window.window_size = window_size;
	flx_bbl_window.arith_percentage = arith_percentage;

	flx_bbltrace_register_handler(flx_arithwindow_bblexec);
	//flx_bbltranslate_register_handler(flx_arithwindow_bbltranslate);
	flx_state.arithwindow_active = 1;

	uint32_t i;
	for(i=0; i<window_size; ++i){
		flx_bbl_window.bbls[i] = malloc(sizeof(flx_bbl));
	}
}

/*
 * Deactivate heuristic
 */
void flx_arithwindow_disable(void){
	flx_bbltrace_unregister_handler(flx_arithwindow_bblexec);
	//flx_bbltranslate_unregister_handler(flx_arithwindow_bbltranslate);
	flx_state.arithwindow_active = 0;
	uint32_t i;
	for(i=0; i<flx_bbl_window.window_size; ++i){
		free(flx_bbl_window.bbls[i]);
	}
	free(flx_bbl_window.bbls);
}

/*
 * Called on every execution of a basic block;
 * calculates current arithmetic percentage and throws event if
 * result is above predefined threshold
 */
int flx_arithwindow_bblexec(uint32_t eip, uint32_t esp){
	flx_bbl* bbl = flx_bbl_search(eip);
	memcpy(flx_bbl_window.bbls[flx_bbl_window.end_index], bbl, sizeof(*bbl));

	assert(bbl->icount >= bbl->movcount);
	flx_bbl_window.instructions       += bbl->icount;
	flx_bbl_window.arith_instructions += bbl->arithcount;
	flx_bbl_window.mov_instructions   += bbl->movcount;

	flx_bbl_window.end_index += 1;
	flx_bbl_window.end_index %= flx_bbl_window.window_size;

	uint8_t recalculate = 0;
	while(flx_bbl_window.instructions >= flx_bbl_window.window_size){
		flx_bbl_window.instructions       -= flx_bbl_window.bbls[flx_bbl_window.start_index]->icount;
		flx_bbl_window.arith_instructions -= flx_bbl_window.bbls[flx_bbl_window.start_index]->arithcount;
		flx_bbl_window.mov_instructions   -= flx_bbl_window.bbls[flx_bbl_window.start_index]->movcount;

		flx_bbl_window.start_index += 1;
		flx_bbl_window.start_index %= flx_bbl_window.window_size;
		recalculate = 1;

	}

	if(recalculate){
		if((float)flx_bbl_window.arith_instructions / (float)(flx_bbl_window.instructions - flx_bbl_window.mov_instructions) >= flx_bbl_window.arith_percentage){
			uint32_t i;
			for(i=flx_bbl_window.start_index; i!= flx_bbl_window.end_index; i=(i+1)%flx_bbl_window.window_size){
		    		if(!flx_arithwindow_cache_search(flx_bbl_window.bbls[i]->addr)){
					flx_arithwindow_handler(flx_bbl_window.bbls[i]->addr);
					flx_arithwindow_cache_add(flx_bbl_window.bbls[i]->addr);
				}
			}
		}

	}

	return 0;
}

