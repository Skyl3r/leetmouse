#include "util.h"
#include <linux/kernel.h>   //fixed-len datatypes
#include <linux/string.h>   //memcpy

//Converts string into float.
inline void atof(const char *str, int len, float *result)
{
    float tmp = 0.0f;
    unsigned int i, j, pos = 0;
    signed char sign = 0;
    int is_whole = 0;
    char c;

    *result = 0.0f;

    for(i = 0; i < len; i++){
        c = str[i];
        if(c == ' ') continue;              //Skip any white space
        if(c == 0) break;                   //End of str
        if(!sign && c == '-'){              //Sign is negative
            sign = -1;
            continue;
        }
        if(c == '.'){                       //Switch from whole to decimal
            is_whole = 1;
            //... We hit the decimal point. Rescale the float to the whole number part
            for(j = 1; j < pos; j++) *result *= 10.0f;
            pos = 1;
            continue;
        }

        if(!(c >= 48 && c <= 57)) break;    //After all previous checks, the remaining characters HAVE to be digits. Otherwise break
        if(!sign) sign = 1;                 //If no sign was yet applied, it has to be positive
        
        //Shift digit to the right... (see above, what we do, when we hit the decimal point)
        tmp = 1;
        for(j = 0; j < pos; j++) tmp /= 10.0f;
        *result += tmp*(c-48);
        pos++;
    }
    //We never hit the decimal point: Rescale here, as we do up in the if(c == '.') statement
    if(is_whole)
        for(j = 1; j < pos; j++) *result *= 10.0f;
    *result *= sign;
}

// Rounds (up/down) depending on sign
inline int Leet_round(float x)
{
    if (x >= 0) {
        return (int)(x + 0.5f);
    } else {
        return (int)(x - 0.5f);
    }
}

// What do we have here? Code from Quake 3, which is also GPL.
// https://en.wikipedia.org/wiki/Fast_inverse_square_root
// Copyright (C) 1999-2005 Id Software, Inc.
inline void Q_sqrt(float *number)
{
    long i;
    float x2, y;
    const float threehalfs = 1.5F;

    x2 = (*number) * 0.5F;
    y  = (*number);
    i  = * ( long * ) &y;                       // evil floating point bit level hacking
    i  = 0x5f3759df - ( i >> 1 );               // what the fuck?
    y  = * ( float * ) &i;
    y  = y * ( threehalfs - ( x2 * y * y ) );   // 1st iteration
    //	y  = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed

    *number = 1 / y;
}


//This is the most crudest HID descriptor parser EVER.
//We will skip most control words until we found an interesting one
//We also assume, that the first button-definition we will find is the most important one,
//so we will ignore any further button definitions

struct parser_context {
    unsigned char id;                           // Report ID
    unsigned int offset;                        // Local offset in this report ID context
};

#define NUM_CONTEXTS 32                             // This should be more than enough for a HID mouse. If we exceed this number, the parser below will eventually fail
#define SET_ENTRY(entry, _id, _offset, _size, _sign) \
    entry.id = _id;                                 \
    entry.offset = _offset;                         \
    entry.size = _size;                             \
    entry.sgn = _sign;

int parse_report_desc(unsigned char *buffer, int buffer_len, struct report_positions *pos)
{
    int r_count = 0, r_size = 0, r_sgn = 0, len = 0;
    int r_usage[2];
    unsigned char ctl, button = 0;
    unsigned char *data;

    unsigned int n, i = 0;

    //Parsing contexts are activated by the  "Report ID" tag. The parser will switch between contexts, when it sees this keyword.
    int context_found;
    struct parser_context contexts[NUM_CONTEXTS];    // We allow up to NUM_CONTEXTS different parsing contexts. Any further will be ignored.
    struct parser_context *c = contexts;             // The current context

    r_usage[0] = 0;
    r_usage[1] = 0;
    pos->report_id_tagged = 0;

    //Initialize contexts to zero
    for(n = 0; n < NUM_CONTEXTS; n++){
        contexts[n].id = 0;
        contexts[n].offset = 0;
    }

    while(i < buffer_len){
        ctl = buffer[i] & 0xFC;                     // Control word with the length-bits stripped
        len = buffer[i] & 0x03;                     // Length of the the proceeding data, following the control word (in bytes)
        if(i < buffer_len) data = buffer + i + 1;   // Beginning of data after the control word

        // ######## Global items
        //Determine the size
        if(ctl == D_REPORT_SIZE)  r_size = (int) data[0];
        if(ctl == D_REPORT_COUNT) r_count = (int) data[0];

        //Switch context, if a "Report ID" control word has been found.
        if(ctl == D_REPORT_ID){
            pos->report_id_tagged = 1;
            // Search all available contexts for a match...
            context_found = 0;
            for(n = 0; n < NUM_CONTEXTS; n++){
                if(contexts[n].id == data[0]){
                    c = contexts + n;
                    c->id = data[0];
                    context_found = 1;
                    break;
                }
            }
            // No existing parsing context matching the Report ID found. Create a new one!
            if(!context_found){
                for(n = 0; n < NUM_CONTEXTS; n++){
                    if(contexts[n].id == 0){
                        c = contexts + n;
                        c->id = data[0];
                        c->offset = 8;              // Since we use a Report ID , which preceeds the actual report (1 byte), all offsets are shifted
                        break;
                    }
                }
            }
        }

        //Determine sign.
        if(ctl == D_LOGICAL_MINIMUM){
            switch(len){
            case 1:
                r_sgn = ((int) *((__s8*) data)) < 0;
                break;
            case 2:
                r_sgn = (__s16) le16_to_cpu(*((__s16*) data)) < 0;
                break;
            //case 4:
            //Sure, we could also now check 4 bytes, as it is written down in the HID specs...
            //However, my extract_at function does not support 4-byte numbers and I don't see, why I should implement it. A mouse should never need to send 4-bytes long messages IMHO.
            }
        }

        // ######## Local items (sort of...) - While a button is described via a global Usage Page (Button), other controls like the Wheel or Pointer Axis are described via local 'Usage' tags.
        //Determine standard usage
        if((ctl == D_USAGE_PAGE || ctl == D_USAGE) && len == 1){
            if(
                data[0] == D_USAGE_BUTTON ||
                data[0] == D_USAGE_WHEEL ||
                data[0] == D_USAGE_X ||
                data[0] == D_USAGE_Y
            ) {
                if(!r_usage[0]){
                    r_usage[0] = (int) data[0];
                } else {
                    r_usage[1] = (int) data[0];
                }
            }
        }

        // ######## Main items
        //Check, if we reached the end of this input data type
        if(ctl == D_INPUT || ctl == D_FEATURE){
            //Assign usage to pos
            if(!button && r_usage[0] == D_USAGE_BUTTON){
                SET_ENTRY(pos->button, c->id, c->offset, r_size*r_count, r_sgn);
                button = 1;
            }
            for(n = 0; n < 2; n++){
                switch(r_usage[n]){
                case D_USAGE_X:
                    SET_ENTRY(pos->x, c->id, c->offset + r_size*n, r_size, r_sgn);
                    break;
                case D_USAGE_Y:
                    SET_ENTRY(pos->y, c->id, c->offset + r_size*n, r_size, r_sgn);
                    break;
                }

            }
            if(r_usage[0] == D_USAGE_WHEEL){
                SET_ENTRY(pos->wheel, c->id, c->offset, r_size*r_count, r_sgn);
            }
            //Reset (some) local tags
            r_usage[0] = 0;
            r_usage[1] = 0;
            //Increment offset
            c->offset += r_size*r_count;
        }
        i += len + 1;
    }

    printk("BTN\t(%d): Offset %u\tSize %u\t Sign %u",   pos->button.id ,    (unsigned int) pos->button.offset,  pos->button.size,   pos->button.sgn);
    printk("X\t(%d): Offset %u\tSize %u\t Sign %u",     pos->x.id,          (unsigned int) pos->x.offset,       pos->x.size,        pos->x.sgn);
    printk("Y\t(%d): Offset %u\tSize %u\t Sign %u",     pos->x.id,          (unsigned int) pos->y.offset,       pos->y.size,        pos->x.sgn);
    printk("WHL\t(%d): Offset %u\tSize %u\t Sign %u",   pos->wheel.id,      (unsigned int) pos->wheel.offset,   pos->wheel.size,    pos->wheel.sgn);

    return 0;
}

//Shifts an array *data of byte-length data_len by amounts of +/- num bits to the right/left (limited to num = +/-8 max).
//Most certainly not the most elegant way to do this. However, it works.
inline void array_shift(unsigned char *data, int data_len, int num){
    int i;
    if(num == 0) return;

    if(num < 0){
        num *= -1;
        for(i = 0; i < data_len; i++){
            data[i] <<= num;
            if(i + 1 < data_len){
                data[i] |= data[i+1] >> (8 - num);
            }
        }
    } else {
        for(i = data_len - 1; i >= 0; i--){
            data[i] >>= num;
            if(i){
                data[i] |= data[i-1] << (8 - num);
            }
        }
    }
}

//Extracts a number from a raw USB stream, according to its position and size as stated in the report_entry
inline int extract_at(unsigned char *data, int data_len, struct report_entry *entry)
{
    int size = entry->size/8;           //Size of our data in bytes
    int i = entry->offset/8;            //Starting index of data[] to access in byte-aligned size
    char shift = entry->offset % 8;     //Remaining bits to shift left, until we reach our target data
    union {
        __u8 raw[4];    //Raw buffer of individual bytes. Must be of same length as "con" and at least 1 byte bigger than the biggest datatype you want to handle in here
        __u32 init;     //Continous buffer of aboves bytes (used for initialization)
        __s8 s8;        //Return value
        __s16 s16;      //Return value
        __u8 u8;        //Return value
        __u16 u16;      //Return value
    } buffer;

    //Data structure to read is bigger than a clear multiple of 8 bits. Read one more byte.
    if(entry->size % 8) size += 1;
    //Data structure to read is bigger than we can handle. Abort
    if(size > sizeof(buffer.init)) return 0;

    buffer.init = 0; //Initialized buffer to zero

    //Avoid access violation when using memcpy.
    if(i + size > data_len) return 0;

    //Create a local copy, that we can modify.
    memcpy(buffer.raw, data + i, size);

    if(shift)
        array_shift(buffer.raw,size,-1*shift);              //Truncate bits, that we copied over too much on the right
    
    if(entry->size <= 8){
        if(shift)
            buffer.raw[0] &= (0xFF << shift);               //Mask bits, which do not belong here
        if(entry->sgn)                                      //If the initial value was signed and if it was not exactly of size 16, we need to add the missing bits, which had been masked above with zeros.
            return (int) (buffer.u8 >> (entry->size - 1)) == 0 ? buffer.u8 : (-1 ^ (0xFF >> (8 - entry->size))) | buffer.s8;
        return (int) buffer.u8;
    }
    if(entry->size <= 16){
        if(shift)
            buffer.raw[1] &= (0xFF << shift);               //Mask bits, which do not belong here
        buffer.u16 = le16_to_cpu(buffer.u16);               //Convert to machine units (must be done on the unsigned bytes)
        if(entry->sgn)                                      //If the initial value was signed and if it was not exactly of size 16, we need to add the missing bits, which had been masked above with zeros.
            return (int) (buffer.s16 >> (entry->size - 1)) == 0 ? buffer.s16 : (-1 ^ (0xFFFF >> (16 - entry->size))) | buffer.s16;
        return (int) buffer.u16;
    }

    return 0; //All other lengths are not supported.
}

// Extracts the interesting mouse data from the raw USB data, according to the layout delcared in the report descriptor
int extract_mouse_events(unsigned char *buffer, int buffer_len, struct report_positions *pos, int *btn, int *x, int *y, int *wheel)
{
    unsigned char id = 0;
    if(pos->report_id_tagged)
        id = buffer[0];

    /*
    int i;
    printk(KERN_CONT "Raw: ");
    for(i = 0; i<buffer_len;i++){
        printk(KERN_CONT "0x%02x ", (int) buffer[i]);
    }
    printk(KERN_CONT "\n");
    */

    *btn = 0; *x = 0; *y = 0; *wheel = 0;
    if(pos->button.id == id)
        *btn =      extract_at(buffer, buffer_len, &pos->button);
    if(pos->x.id == id)
        *x =        extract_at(buffer, buffer_len, &pos->x);
    if(pos->y.id == id)
        *y =        extract_at(buffer, buffer_len, &pos->y);
    if(pos->wheel.id == id)
        *wheel =    extract_at(buffer, buffer_len, &pos->wheel);

    return 0;
}
