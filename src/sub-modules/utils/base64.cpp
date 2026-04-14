#include "base64.hpp"

// Base64 alphabet
const char Base64Encoder::encodeTable[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t Base64Encoder::calculateEncodedSize(size_t binaryLength) {
    return ((binaryLength + 2) / 3) * 4;
}

bool Base64Encoder::encode(const uint8_t* input, size_t inputLength,
                           char* outputBuffer, size_t& outputLength) {
    if (!input || !outputBuffer || inputLength == 0) {
        outputLength = 0;
        return false;
    }

    size_t outIdx = 0;
    size_t i = 0;

    while (i < inputLength) {
        size_t remain = inputLength - i;

        uint32_t octet_a = input[i++];
        uint32_t octet_b = (remain > 1) ? input[i++] : 0;
        uint32_t octet_c = (remain > 2) ? input[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        outputBuffer[outIdx++] = encodeTable[(triple >> 18) & 0x3F];
        outputBuffer[outIdx++] = encodeTable[(triple >> 12) & 0x3F];

        if (remain > 1) {
            outputBuffer[outIdx++] = encodeTable[(triple >> 6) & 0x3F];
        } else {
            outputBuffer[outIdx++] = '=';
        }

        if (remain > 2) {
            outputBuffer[outIdx++] = encodeTable[triple & 0x3F];
        } else {
            outputBuffer[outIdx++] = '=';
        }
    }

    outputLength = outIdx;
    return true;
}

// ---------------------- Decoder ----------------------

const uint8_t Base64Decoder::decodeTable[256] = {
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,
    52,53,54,55,56,57,58,59,60,61,64,64,64, 0,64,64,
    64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,
    64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,
    // Remaining rows filled with 64 (invalid)
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64
};

size_t Base64Decoder::calculateDecodedSize(size_t base64Length) {
    return (base64Length / 4) * 3;
}

bool Base64Decoder::decode(const char* input, size_t inputLength,
                           uint8_t* outputBuffer, size_t& outputLength) {
    if (!input || !outputBuffer || inputLength == 0) {
        outputLength = 0;
        return false;
    }

    size_t outIdx = 0;
    uint32_t buffer = 0;
    int bits_collected = 0;

    for (size_t i = 0; i < inputLength; ++i) {
        unsigned char ch = static_cast<unsigned char>(input[i]);
        if (ch == '=') {
            // padding: stop decoding
            break;
        }

        uint8_t val = decodeTable[ch];
        if (val == 64) {
            // skip invalid/whitespace
            continue;
        }

        buffer = (buffer << 6) | val;
        bits_collected += 6;

        if (bits_collected >= 8) {
            bits_collected -= 8;
            outputBuffer[outIdx++] = static_cast<uint8_t>((buffer >> bits_collected) & 0xFF);
        }
    }

    outputLength = outIdx;
    return true;
}

