#pragma once

size_t b64_decoded_size(const char* in);
int b64_isvalidchar(char c);
int b64_decode(const char* in, unsigned char* out, size_t outlen);
