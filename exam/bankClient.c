
#include <string.h>
#include <stdlib.h>

#include "bankDefination.h"


int parse_transaction(const char *input_str, transaction_t *tx) {
    if (input_str == NULL || tx == NULL) return -1;

    char buffer[200];
    int i;
    for (i = 0; i < sizeof(buffer) - 1 && input_str[i]; i++) {
        buffer[i] = input_str[i];
    }
    buffer[i] = '\0';

    char *saveptr;
    char *token = strtok_r(buffer, " ", &saveptr);
    if (!token) return -1;
    safe_strcpy(tx->client_id, token, sizeof(tx->client_id));
    token = strtok_r(NULL, " ", &saveptr);
    if (!token) return -1;
    if (strcmp(token, "deposit") == 0) {
        tx->op = DEPOSIT;
    } else if (strcmp(token, "withdraw") == 0) {
        tx->op = WITHDRAW;
    } else {
        return -2;  // Unknown operation
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (!token) return -1;
    tx->amount = atoi(token);  

    return 0;
}

void safe_strcpy(char *dest, const char *src, int maxlen) {
    int i;
    for (i = 0; i < maxlen - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
}