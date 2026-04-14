#ifndef BASE64DECODER_H
#define BASE64DECODER_H

#include <cstdint>
#include <cstddef>

class Base64Decoder {
public:
    /**
     * Calculate the maximum size of the output buffer required to decode a given base64 string.
     * This does not decode the input.
     */
    static size_t calculateDecodedSize(size_t base64Length);

    /**
     * Decode base64 input into the given output buffer.
     * @param input         - base64 encoded char* buffer
     * @param inputLength   - length of base64 encoded data
     * @param outputBuffer  - pointer to pre-allocated buffer of size >= calculateDecodedSize()
     * @param outputLength  - will be filled with the number of decoded bytes
     * @return true if decoding is successful, false if invalid input or buffer too small
     */
    static bool decode(const char* input, size_t inputLength, uint8_t* outputBuffer, size_t& outputLength);

private:
    static const uint8_t decodeTable[256];
};

class Base64Encoder {
public:
    /**
     * Calculate the maximum size of the output buffer required to encode a given binary buffer.
     */
    static size_t calculateEncodedSize(size_t binaryLength);

    /**
     * Encode binary input into base64 string.
     * @param input         - binary input buffer
     * @param inputLength   - length of binary input
     * @param outputBuffer  - pointer to pre-allocated char buffer of size >= calculateEncodedSize()
     * @param outputLength  - will be filled with the number of encoded chars (excluding null terminator)
     * @return true if encoding is successful, false if invalid input or buffer too small
     */
    static bool encode(const uint8_t* input, size_t inputLength, char* outputBuffer, size_t& outputLength);

private:
    static const char encodeTable[65];
};

#endif // BASE64DECODER_H
