
#include <string.h>

#include "mbedtls/aes.h"
#include "error.h"
#include "logger.h"

#define ENCRYPT_BYTE_LENGTH  (16)

mbedtls_aes_context aes;

static unsigned char _key[ENCRYPT_BYTE_LENGTH] = {0};

ret_t aes_setkey(unsigned char* key){

    ret_t ret = ret_Success;
    do{
        memccpy(_key, key, ENCRYPT_BYTE_LENGTH, ENCRYPT_BYTE_LENGTH);
    }while(false);

    return ret;
}

ret_t aes_encrypt(char * plaintext, unsigned int input_size, unsigned char * cypertext, unsigned int output_size){

    ret_t ret = ret_Success;

    do{

        if(cypertext == nullptr){
            ret = ret_Err_Gen_NullP;
            break;
        }

        if(plaintext == nullptr){
            ret = ret_Err_Gen_NullP;
            break;
        }

        unsigned int expected_cyptertext_size = input_size%ENCRYPT_BYTE_LENGTH == 0 ? input_size : (input_size/ENCRYPT_BYTE_LENGTH + 1)*ENCRYPT_BYTE_LENGTH;
        if(output_size < expected_cyptertext_size){
            ret = ret_Err_Gen_SmallBuffer;
            break;
        }

        mbedtls_aes_init( &aes );
        mbedtls_aes_setkey_enc( &aes, (const unsigned char*) _key, ENCRYPT_BYTE_LENGTH * 8 );
        for(unsigned int i=0; i<expected_cyptertext_size; i+=ENCRYPT_BYTE_LENGTH){
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, (const unsigned char*)plaintext + i , cypertext + i);
        }
        mbedtls_aes_free( &aes );

        // for (int i = 0; i < ENCRYPT_BYTE_LENGTH; i++) {
        //     char str[3];
        //     sprintf(str, "%02x", (int)cypertext[i]);
        //     logger.log_(str);
        // }

    }while(false);

    return ret;
}


ret_t aes_decrypt(unsigned char * cypertext, unsigned int input_size, char * plaintext, unsigned int output_size){

    ret_t ret = ret_Success;

    do{

        if(cypertext == nullptr){
            ret = ret_Err_Gen_NullP;
            break;
        }

        if(plaintext == nullptr){
            ret = ret_Err_Gen_NullP;
            break;
        }

        input_size = (input_size/ENCRYPT_BYTE_LENGTH)*ENCRYPT_BYTE_LENGTH;
        // logger.log("Input Size = " + String(input_size));

        if(output_size < input_size){
            ret = ret_Err_Gen_InvalidParam;
            break;
        }

        mbedtls_aes_init( &aes );
        mbedtls_aes_setkey_dec( &aes, (const unsigned char*) _key, ENCRYPT_BYTE_LENGTH * 8 );
        for(unsigned int i=0; i<input_size; i+=ENCRYPT_BYTE_LENGTH){
            mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, (const unsigned char*)cypertext + i, (unsigned char *)plaintext + i);
        }
        mbedtls_aes_free( &aes );

        // logger.log(String(plaintext));

    }while(false);

    return ret;

}
