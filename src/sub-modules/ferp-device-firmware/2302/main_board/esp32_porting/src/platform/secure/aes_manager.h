#ifndef __AES_MANAGER_H__
#define __AES_MANAGER_H__

#include "error.h"
#include "device_config.h"

ret_t aes_setkey(unsigned char* key);
ret_t aes_decrypt(unsigned char * cypertext, unsigned int input_size, char * plaintext, unsigned int output_size);
ret_t aes_encrypt(char * plaintext, unsigned int input_size, unsigned char * cypertext, unsigned int output_size);

#endif //__AES_MANAGER_H__