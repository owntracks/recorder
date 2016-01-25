#ifndef BASE64_H_GUARD
#define BASE64_H_GUARD

#include <stddef.h>

char* base64_encode(const void* buf, size_t size);
void* base64_decode(const char* s, size_t *data_len);

#endif
