/*
 * que.h
 *
 *  Created on: Aug 27, 2020
 *      Author: Chathuranga
 */

#ifndef QUE_H_
#define QUE_H_

#include "stdint.h"

typedef void (*copy_t)(void *, void *);

typedef struct {
	uint16_t head;
	uint16_t tail;
	uint16_t buffer_size;
    uint16_t size_of_type;
	void * buffer;
	copy_t copy;    
}que_t;

int32_t que_pop(que_t* que, void * val);
void que_push(que_t* que, void * val);
uint16_t que_getsize(que_t* que);
void que_clear(que_t* que);

#endif /* QUE_H_ */
