/* Minimal utils.h - Contains only what's needed for compression_only implementation */

#pragma once

#include <iostream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cfloat>
#include <cmath>
#include <algorithm>
#include <assert.h>

// Version info
#define VERSION "1.1.8-minimal"

// Constants needed by compression_test
#define ZALPHA 2.5758293035489008
#define ITERMAX 1076
#define ABSEPSILON DBL_MIN
#define RELEPSILON DBL_EPSILON
#define DBL_INFINITY __builtin_inf()

// Constants needed by main
#define MIN_SIZE 1000000

// Macros needed by compression_test
#define INCLOSEDINTERVAL(x, a, b) (((a)>(b))?(((x)>=(b))&&((x)<=(a))):(((x)>=(a))&&((x)<=(b))))
#define INOPENINTERVAL(x, a, b) (((a)>(b))?(((x)>(b))&&((x)<(a))):(((x)>(a))&&((x)<(b))))

// Data structure for entropy assessment
typedef struct data_t {
    int word_size;         // bits per symbol
    int alph_size;         // symbol alphabet size
    uint8_t maxsymbol;     // the largest symbol present in the raw data stream
    uint8_t *rawsymbols;   // raw data words
    uint8_t *symbols;      // data words
    uint8_t *bsymbols;     // data words as binary string
    long len;              // number of words in data
    long blen;             // number of bits in data
} data_t;

// Stub for TestRunBase (to avoid dependency)
struct TestRunBase {
    int errorLevel = 0;
    std::string errorMsg = "";
};

using namespace std;

// Function needed by compression_test for floating point comparison
bool relEpsilonEqual(double A, double B, double maxAbsFactor, double maxRelFactor, uint32_t maxULP) {
    double diff;
    double absA, absB;
    uint64_t Aint;
    uint64_t Bint;

    assert(sizeof(uint64_t) == sizeof(double));
    assert(maxAbsFactor >= 0.0);
    assert(maxRelFactor >= 0.0);

    // NaN is by definition not equal to anything (including itself)
    if(std::isnan(A) || std::isnan(B)) {
        return false;
    }

    // Deals with equal infinities, and the corner case where they are actually copies
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
    if(A==B) {
        return true;
    }
#pragma GCC diagnostic pop

    // If either is infinity, but they are not equal, then they aren't close.
    if(std::isinf(A) || std::isinf(B)) {
        return false;
    }

    absA = fabs(A);
    absB = fabs(B);
    // Make sure that A is the closest to 0.
    if(absA > absB) {
        double tmp;

        // Swap A and B
        tmp = B;
        B=A;
        A=tmp;

        // Swap absA and absB
        tmp = absB;
        absB = absA;
        absA = tmp;
    }

    // Capture the difference of the largest magnitude from the smallest magnitude
    diff=fabs(B-A);

    // Is absA, diff, or absB * maxRelFactor subnormal?
    // Did diff overflow?
    // if absA is subnormal (effectively 0) or 0, then relative difference isn't meaningful, as fabs(B-A)/B≈1 for all values of B
    // In the instance of overflows, the resulting relative comparison will be nonsense.
    if((absA < DBL_MIN) || (diff < DBL_MIN) || std::isinf(diff) || (absB * maxRelFactor < DBL_MIN)) {
        // Yes. Relative closeness is going to be nonsense
        return diff <= maxAbsFactor;
    } else {
        // No. Using relative closeness is probably the right thing to do.
        // Proceeding roughly as per Knuth AoCP vol II (section 4.2.2)
        if(diff <= absB * maxRelFactor) {
            // These are relatively close
            return true;
        } 
    }

    // Neither A or B is subnormal, and they aren't close in the conventional sense, 
    // but perhaps that's just due to IEEE representation. Check to see if the value is within maxULP ULPs.

    // We can't meaningfully compare non-zero values with 0.0 in this way,
    // but absA >= DBL_MIN if we're here, so neither value is 0.0.

    // if they aren't the same sign, then these can't be only a few ULPs away from each other
    if(signbit(A) != signbit(B)) {
        return false;
    }

    // Note, casting from one type to another is undefined behavior, but memcpy will necessarily work
    memcpy(&Aint, &absA, sizeof(double));
    memcpy(&Bint, &absB, sizeof(double));
    // This should be true by the construction of IEEE doubles
    assert(Bint > Aint);

    return (Bint - Aint <= maxULP);
}

// Memory cleanup function
void free_data(data_t *dp) {
    if(dp->symbols != NULL) free(dp->symbols);
    if(dp->rawsymbols != NULL) free(dp->rawsymbols);
    if((dp->word_size > 1) && (dp->bsymbols != NULL)) free(dp->bsymbols);
} 

// Simplified file hash function (replaces OpenSSL dependency)
bool sha256_file(const char *file_path, char *hash_output) {
    // Stub replacement for OpenSSL SHA-256
    // Just verify file exists and return a dummy hash
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        return false;
    }
    fclose(file);
    
    // Fill with dummy hash value
    strcpy(hash_output, "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    return true;
}

// File reading function (simplified version without complex error handling)
bool read_file_subset(const char *file_path, data_t *dp, unsigned long subsetIndex, unsigned long subsetSize, TestRunBase *testRun) {
    FILE *file; 
    int mask, j, max_symbols;
    long rc, i;
    long fileLen;

    file = fopen(file_path, "rb");
    if(!file) {
        if (testRun) {
            testRun->errorLevel = -1;
            testRun->errorMsg = "Error: could not open file";
        }
        printf("Error: could not open '%s'\n", file_path);
        return false;
    }

    rc = (long)fseek(file, 0, SEEK_END);
    if(rc < 0) {
        if (testRun) {
            testRun->errorLevel = -1;
            testRun->errorMsg = "Error: fseek failed";
        }
        printf("Error: fseek failed\n");
        fclose(file);
        return false;
    }

    fileLen = ftell(file);
    if(fileLen < 0) {
        if (testRun) {
            testRun->errorLevel = -1;
            testRun->errorMsg = "Error: ftell failed";
        }
        printf("Error: ftell failed\n");
        fclose(file);
        return false;
    }

    rewind(file);

    if(subsetSize == 0) {
        dp->len = fileLen;
    } else {
        rc = (long)fseek(file, subsetIndex*subsetSize, SEEK_SET);
        if(rc < 0) {
            if (testRun) {
                testRun->errorLevel = -1;
                testRun->errorMsg = "Error: fseek failed";
            }
            printf("Error: fseek failed\n");
            fclose(file);
            return false;
        }

        dp->len = std::min(fileLen - (long)(subsetIndex*subsetSize), (long)subsetSize);
    }

    if(dp->len == 0) {
        if (testRun) {
            testRun->errorLevel = -1;
            testRun->errorMsg = "Error: file is empty";
        }
        printf("Error: '%s' is empty\n", file_path);
        fclose(file);
        return false;
    }

    dp->symbols = (uint8_t*)malloc(sizeof(uint8_t)*dp->len);
    dp->rawsymbols = (uint8_t*)malloc(sizeof(uint8_t)*dp->len);
    if((dp->symbols == NULL) || (dp->rawsymbols == NULL)) {
        if (testRun) {
            testRun->errorLevel = -1;
            testRun->errorMsg = "Error: failure to initialize memory for symbols";
        }
        printf("Error: failure to initialize memory for symbols\n");
        fclose(file);
        if(dp->symbols != NULL) {
            free(dp->symbols);
            dp->symbols = NULL;
        }
        if(dp->rawsymbols != NULL) {
            free(dp->rawsymbols);
            dp->rawsymbols = NULL;
        }
        return false;
    }

    rc = fread(dp->symbols, sizeof(uint8_t), dp->len, file);
    if(rc != dp->len) {
        if (testRun) {
            testRun->errorLevel = -1;
            testRun->errorMsg = "Error: file read failure";
        }
        printf("Error: file read failure\n");
        fclose(file);
        free(dp->symbols);
        dp->symbols = NULL;
        free(dp->rawsymbols);
        dp->rawsymbols = NULL;
        return false;
    }
    fclose(file);

    // Determine word size if not specified
    if(dp->word_size == 0) {
        uint8_t datamask = 0;
        uint8_t curbit = 0x80;

        for(i = 0; i < dp->len; i++) {
            datamask = datamask | dp->symbols[i];
        }

        for(i=8; (i>0) && ((datamask & curbit) == 0); i--) {
            curbit = curbit >> 1;
        }

        dp->word_size = i;
    } else {
        uint8_t datamask = 0;
        uint8_t curbit = 0x80;

        for(i = 0; i < dp->len; i++) {
            datamask = datamask | dp->symbols[i];
        }

        for(i=8; (i>0) && ((datamask & curbit) == 0); i--) {
            curbit = curbit >> 1;
        }

        if( i < dp->word_size ) {
            printf("Warning: Symbols appear to be narrower than described.\n");
            if (testRun) testRun->errorMsg = "Warning: Symbols appear to be narrower than described.";
        } else if( i > dp->word_size ) {
            if (testRun) {
                testRun->errorLevel = -1;
                testRun->errorMsg = "Error: Incorrect bit width specification: Data does not fit within described bit width.";
            }
            printf("Incorrect bit width specification: Data (%ld) does not fit within described bit width: %d.\n", i, dp->word_size); 
            free(dp->symbols);
            dp->symbols = NULL;
            free(dp->rawsymbols);
            dp->rawsymbols = NULL;
            return false;
        }
    }

    memcpy(dp->rawsymbols, dp->symbols, sizeof(uint8_t)* dp->len);
    dp->maxsymbol = 0;

    max_symbols = 1 << dp->word_size;
    int symbol_map_down_table[max_symbols];

    // Create symbols (samples) and check if they need to be mapped down
    dp->alph_size = 0;
    memset(symbol_map_down_table, 0, max_symbols*sizeof(int));
    mask = max_symbols-1;
    for(i = 0; i < dp->len; i++) { 
        dp->symbols[i] &= mask;
        if(dp->symbols[i] > dp->maxsymbol) dp->maxsymbol = dp->symbols[i];
        if(symbol_map_down_table[dp->symbols[i]] == 0) symbol_map_down_table[dp->symbols[i]] = 1;
    }

    for(i = 0; i < max_symbols; i++) {
        if(symbol_map_down_table[i] != 0) symbol_map_down_table[i] = (uint8_t)dp->alph_size++;
    }

    // Create bsymbols (bitstring) using the non-mapped data
    dp->blen = dp->len * dp->word_size;
    if(dp->word_size == 1) dp->bsymbols = dp->symbols;
    else {
        dp->bsymbols = (uint8_t*)malloc(dp->blen);
        if(dp->bsymbols == NULL) {
            if (testRun) {
                testRun->errorLevel = -1;
                testRun->errorMsg = "Error: failure to initialize memory for bsymbols";
            }
            printf("Error: failure to initialize memory for bsymbols\n");
            free(dp->symbols);
            dp->symbols = NULL;
            free(dp->rawsymbols);
            dp->rawsymbols = NULL;
            return false;
        }

        for(i = 0; i < dp->len; i++) {
            for(j = 0; j < dp->word_size; j++) {
                dp->bsymbols[i*dp->word_size+j] = (dp->symbols[i] >> (dp->word_size-1-j)) & 0x1;
            }
        }
    }

    // Map down symbols if less than 2^bits_per_word unique symbols
    if(dp->alph_size < dp->maxsymbol + 1) {
        for(i = 0; i < dp->len; i++) dp->symbols[i] = (uint8_t)symbol_map_down_table[dp->symbols[i]];
    } 

    return true;
}

// Version information function
void printVersion(string name) {
    cout << name << " " << VERSION << "\n\n";
    cout << "Minimal compression-only implementation of NIST SP800-90B\n";
    cout << "Based on NIST-developed software provided as a public service.\n\n";
}