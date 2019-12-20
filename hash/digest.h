#include <stdbool.h>

typedef enum {
    MD5_t,
    SHA1_t,
    SHA256_t,
    CRYPT_t,
    NONETYPE_t
} HASH_TYPES;

typedef struct{
    char* user;
    char* cryptHash;
    char* decryptHash;
}pass;

char* prepSaltedKey(char* key, char* salt);
void print(char* format,char* string); //printa solo se abilitato il flag di print
char* md5(char* plaintext, char* salt);
char* sha256(char* plaintext, char* salt);
char* sha1(char* plaintext, char* salt);
bool setDebugPrints(bool enabled);
char* unixCrypt(char* key, char* salt);
HASH_TYPES getHashType(pass obj);

#include "digest.c"
