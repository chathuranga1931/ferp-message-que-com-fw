/*
 * que.c
 *
 *  Created on: Aug 27, 2020
 *      Author: Chathuranga
 */
#include "Arduino.h"
#include "logger.h"

#include "stdint.h"
#include "que.h"

int32_t que_pop(que_t * que, void * val){

	if(que_getsize(que) <= 0) return -1;

    uint16_t byte_location_of_buffer = ((que->tail)) * (que->size_of_type);
    uint8_t * dest = &(((uint8_t *)(que->buffer))[byte_location_of_buffer]);
	que->copy(val, dest);
    
	((que->tail))++;

	if(((que->tail)) >= que->buffer_size){
		((que->tail)) = 0;
	}

	return que_getsize(que);
}

void que_push(que_t * que, void * val){

    uint16_t byte_location_of_buffer = ((que->head)) * (que->size_of_type);
    uint8_t * dest = &(((uint8_t *)(que->buffer))[byte_location_of_buffer]);
	que->copy(dest, val );

	/* update the ring buffer */
	((que->head))++;
	if(((que->head)) >= que->buffer_size){
		((que->head)) = 0;
	}

	if(((que->tail)) == ((que->head))){
		((que->tail))++;
		if(((que->tail)) >= que->buffer_size){
			((que->tail)) = 0;
		}
	}
}

uint16_t que_getsize(que_t * que){

	uint16_t size;
	if(((que->head)) > ((que->tail))){
		size = ((que->head)) - ((que->tail));
	}
	else if(((que->head)) == ((que->tail))){
		size = ((que->head)) - ((que->tail));
	}
	else{
		size = ((que->head)) + (que->buffer_size - ((que->tail))) + 1;
	}

	return size;
}

void que_clear(que_t * que){
	((que->tail)) = 0;
	((que->head)) = 0;
}
