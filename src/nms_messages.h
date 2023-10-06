#ifndef NMS_OPRA_MESSAGE_H
#define NMS_OPRA_MESSAGE_H

#include <stdint.h>
#include "xtypes.h"

#define NMS_OPRA_SYMBOL_SIZE (21)

#if defined(__linux__) || defined(_MSC_VER)
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif

struct nms_opra_quote_t // 39 - q
{
    xuint8 symbol[5];     //  0- 4
    xuint8 expiration[3]; //  5- 7
    XC_HITIME timestamp;  //  8-15
    xuint32 strike_price; // 16-19
    xuint32 bid_price;    // 20-23
    xuint32 ask_price;    // 24-27
    XC_VOLUME bid_size;   // 28-31
    XC_VOLUME ask_size;   // 32-35
    xuint8 bid_exchange;  //    36
    xuint8 ask_exchange;  //    37
    xuint8 condition;     //    38
};

struct nms_opra_trade_t // 30 - t
{
    xuint8 symbol[5];      //  0- 4
    xuint8 expiration[3];  //  5- 7
    XC_HITIME timestamp;   //  8-15
    xuint32 strike_price;  // 16-19
    xuint32 premium_price; // 20-23
    XC_VOLUME volume;      // 24-27
    xuint8 exchange;       //    28
    xuint8 condition;      //    29
};

union option_t
{
    struct nms_opra_quote_t quote;
    struct nms_opra_trade_t trade;
};

#if defined(__linux__) || defined(_MSC_VER)
#pragma pack(pop)
#else
#pragma pack()
#endif

#endif
