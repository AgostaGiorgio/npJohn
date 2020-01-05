#ifndef STDIO_H
    #include <stdio.h>
#endif

#ifndef STDLIB_H
    #include <stdlib.h>
#endif

#ifndef STRING_H
    #include <string.h>
#endif

#ifndef PASSWORD_H
    #include "../password/password.h"
#endif

#ifndef DIGEST_H
    #include "../hash/digest.h"
#endif

#define HANDLE_H
#define MAXCHAR 1000

typedef struct passwordList{  
    Password  obj; 
    struct passwordList *next; 
}PasswordList;

typedef struct dictList{
    char* word;
    struct dictList* next;
}DictList;

void addElement(PasswordList** head, Password* obj);
PasswordList* createStruct(char* fileName);
DictList* importFileDict(char* fileName);
void freePass(PasswordList* head);
void freeDict(DictList* head);

#include "input_handler.c"