/*
 * chksum.h - utilities for calculating checksums
 */

#pragma once

#include <stdint.h>
#include <string.h>

/**
 * chksum_internet - performs an internet checksum on a buffer
 * @buf: the buffer
 * @len: the length in bytes
 *
 * An internet checksum is a 16-bit one's complement sum. Details
 * are described in RFC 1071.
 *
 * Returns a 16-bit checksum value.
 */
static inline uint16_t chksum_internet(const void *buf, int len)
{       
        uint32_t sum = 0;

        char* data = (char*) buf;

        for(int i = 0; i <= len; i+=2){
                uint16_t word;
                memcpy(&word, data + i, 2);
                sum += word;
                if (sum > 0xFFFF) {
                        sum -= 0xFFFF;
                }       
        }

        if(len % 2 == 1){
                uint16_t word=0;
                memcpy(&word,data + len - 1, 1); 
                sum += word;
                if (sum > 0xFFFF) {
                        sum -= 0xFFFF;
                }
        }

        return ~(sum & 0xFFFF);
}

