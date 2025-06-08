/* compression_minimal.cpp - Complete standalone implementation */

// Include our minimal utils
#include "utils_minimal.h"
#include <getopt.h>
#include <limits.h>
#include <errno.h>

// Compression test implementation (copied from compression_test.h)
inline void kahan_add(double &sum, double &comp, double in) {
    double y, t; 

    y = in - comp;
    t = sum + y;
    comp = (t - sum) - y;
    sum = t;
}

double G(double z, int d, long num_blocks) {
    double Ai=0.0, Ai_comp=0.0;
    double firstSum=0.0, firstSum_comp=0.0;
    long v = num_blocks - d;
    double Ad1;

    long double Bi;
    long double Bterm;
    long double ai;
    long double aiScaled;
    bool underflowTruncate;

    assert(d>0);
    assert(num_blocks>d);

    //i=2
    Bterm = (1.0L-(long double)z);
    //Note: B_1 isn't needed, as a_1 = 0
    //B_2
    Bi = Bterm;

    //Calculate A_{d+1}
    for(int i=2; i<=d; i++) {
        //calculate the a_i term
        kahan_add(Ai, Ai_comp, log2l((long double)i)*Bi);

        //Calculate B_{i+1}
        Bi *= Bterm;
    }

    //Store A_{d+1}
    Ad1 = Ai;

    underflowTruncate = false;
    //Now calculate A_{num_blocks} and the sum of sums term (firstsum)
    for(long i=d+1; i<=num_blocks-1; i++) {
        //calculate the a_i term
        ai = log2l((long double)i)*Bi;

        //Calculate A_{i+1}
        kahan_add(Ai, Ai_comp, (double)ai);
        //Sum in A_{i+1} into the firstSum

        //Calculate the tail of the sum of sums term (firstsum)
        aiScaled = (long double)(num_blocks-i) * ai;
        if((double)aiScaled > 0.0) {
            kahan_add(firstSum, firstSum_comp, (double)aiScaled);
        } else {
            underflowTruncate = true;
            break;
        }

        //Calculate B_{i+1}
        Bi *= Bterm;
    }

    //Ai now contains A_{num_blocks} and firstsum contains the tail
    //finalize the calculation of firstsum
    kahan_add(firstSum, firstSum_comp, ((double)(num_blocks-d))*Ad1);

    //Calculate A_{num_blocks+1}
    if(!underflowTruncate) {
        ai = log2l((long double)num_blocks)*Bi;
        kahan_add(Ai, Ai_comp, (double)ai);
    }

    return 1/(double)v * z*(z*firstSum + (Ai - Ad1));
}

double com_exp(double p, unsigned int alph_size, int d, long num_blocks) {
    double q = (1.0-p)/((double)alph_size-1.0);
    return G(p, d, num_blocks) + ((double)alph_size-1.0) * G(q, d, num_blocks);
}

// Section 6.3.4 - Compression Estimate
double compression_test(uint8_t* data, long len, const int verbose, const char *label) {
    int j, d, b = 6;
    long i, num_blocks, v;
    unsigned int block, alph_size = 1 << b; 
    unsigned int dict[alph_size];
    double X=0.0, X_comp=0.0;
    double sigma=0.0, sigma_comp=0.0;
    double p, entEst;
    double ldomain, hdomain, lbound, hbound, lvalue, hvalue, pVal, lastP;

    d = 1000;
    num_blocks = len/b;

    if(num_blocks <= d) {
        printf("\t*** Warning: not enough samples to run compression test (need more than %d) ***\n", d);
        return -1.0;
    }

    // create dictionary
    for(i = 0; i < alph_size; i++) dict[i] = 0;
    for(i = 0; i < d; i++) {
        block = 0;
        for(j = 0; j < b; j++) block |= (data[i*b + j] & 0x1) << (b-j-1);
        dict[block] = i+1;
    }

    // test data against dictionary
    v = num_blocks - d;
    for(i = d; i < num_blocks; i++) {
        block = 0;
        for(j = 0; j < b; j++) block |= (data[i*b + j] & 0x1) << (b-j-1);
        kahan_add(X, X_comp, log2(i+1-dict[block]));
        kahan_add(sigma, sigma_comp, log2(i+1-dict[block])*log2(i+1-dict[block]));
        dict[block] = i+1;
    }

    // compute mean and stdev
    X /= v;
    sigma = 0.5907 * sqrt(sigma/(v-1.0) - X*X);

    if(verbose == 2) {
        printf("%s Compression Estimate: X-bar = %.17g, ", label, X);
        printf("sigma-hat = %.17g, ", sigma);
    } else if(verbose == 3) {
        printf("%s Compression Estimate: X-bar = %.17g\n", label, X);
        printf("%s Compression Estimate: sigma-hat = %.17g\n", label, sigma);
    }

    // binary search for p
    X -= ZALPHA * sigma/sqrt(v);

    if(verbose == 3) printf("%s Compression Estimate: X-bar' = %.17g\n", label, X);

    if(com_exp(1.0/(double)alph_size, alph_size, d, num_blocks) > X) {
        ldomain = 1.0 / (double)alph_size;
        hdomain = 1.0;

        lbound = ldomain;
        hbound = hdomain;

        lvalue = DBL_INFINITY;
        hvalue = -DBL_INFINITY;

        p = (lbound + hbound) / 2.0;
        pVal = com_exp(p, alph_size, d, num_blocks);

        for(j=0; j<ITERMAX; j++) {
            if(relEpsilonEqual(pVal, X, ABSEPSILON, RELEPSILON, 4)) break;

            if(X < pVal) {
                lbound = p;
                lvalue = pVal;
            } else {
                hbound = p;
                hvalue = pVal;
            }

            if(lbound >= hbound) {
                p = fmin(fmax(lbound, hbound),hdomain);
                break;
            }

            if(!(INCLOSEDINTERVAL(lbound, ldomain, hdomain) && INCLOSEDINTERVAL(hbound,  ldomain, hdomain))) {
                p = ldomain;
                break;
            }

            if(!INCLOSEDINTERVAL(X, lvalue, hvalue)) {
                p = ldomain;
                break;
            }

            lastP = p;
            p = (lbound + hbound) / 2.0;

            if(!INOPENINTERVAL(p,  lbound, hbound)) {
                p = hbound;
                break;
            }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
            if(lastP == p) {
                p = hbound;
                break;
            }
#pragma GCC diagnostic pop

            pVal = com_exp(p, alph_size, d, num_blocks);

            if(!INCLOSEDINTERVAL(pVal, lvalue, hvalue)) {
                p = hbound;
                break;
            }
        }
    } else {
        p = -1.0;
    }

    if(p > 1.0 / (double)alph_size) {
        entEst = -log2(p)/b;
        if(verbose == 3) printf("%s Compression Estimate: Found p.\n", label);
    } else {
        p = 1.0 / (double)alph_size;
        entEst = 1.0;
        if(verbose == 3) printf("%s Compression Estimate: Could Not Find p. Proceeding with the lower bound for p.\n", label);
    }

    if(verbose == 2) printf("p = %.17g\n", p);
    else if(verbose == 3) {
        printf("%s Compression Estimate: p = %.17g\n", label, p);
        printf("%s Compression Estimate: min entropy = %.17g\n", label, entEst);
    }

    return entEst;
}

// Main function (your compression_only main with minimal modifications)
[[ noreturn ]] void print_usage() {
    printf("Usage is: compression_minimal [-i|-c] [-a|-t] [-v] [-q] [-l <index>,<samples> ] <file_name> [bits_per_symbol]\n\n");
    printf("\t <file_name>: Must be relative path to a binary file with at least 1 million entries (samples).\n");
    printf("\t [bits_per_symbol]: Must be between 1-8, inclusive. By default this value is inferred from the data.\n");
    printf("\t [-i|-c]: '-i' for initial entropy estimate, '-c' for conditioned sequential dataset entropy estimate. The initial entropy estimate is the default.\n");
    printf("\t [-a|-t]: '-a' produces the 'H_bitstring' assessment using all read bits, '-t' truncates the bitstring used to produce the `H_bitstring` assessment to %d bits. Test all data by default.\n", MIN_SIZE);
    printf("\t -v: Optional verbosity flag for more output. Can be used multiple times.\n");
    printf("\t -q: Quiet mode, less output to screen. This will override any verbose flags.\n");
    printf("\t -l <index>,<samples>\tRead the <index> substring of length <samples>.\n");
    printf("\n");
    printf("\t This is a minimal implementation that runs ONLY the compression test from NIST SP800-90B.\n");
    printf("\t Zero external dependencies - produces identical results to the full NIST implementation.\n");
    printf("\n");
    printf("\t --version: Prints tool version information\n");
    printf("\n");
    exit(-1);
}

int main(int argc, char* argv[]) {
    bool initial_entropy, all_bits;
    int verbose = 1;
    bool quietMode = false;
    char *file_path;
    double H_original, H_bitstring, ret_min_entropy, h_assessed;
    data_t data;
    int opt;
    unsigned long subsetIndex = ULONG_MAX;
    unsigned long subsetSize = 0;
    unsigned long long inint;
    char *nextOption;

    data.word_size = 0;
    initial_entropy = true;
    all_bits = true;

    for (int i = 0; i < argc; i++) {
        std::string Str = std::string(argv[i]);
        if ("--version" == Str) {
            printVersion("compression_minimal");
            exit(0);
        }
    }

    while ((opt = getopt(argc, argv, "icatvql:")) != -1) {
        switch (opt) {
            case 'i':
                initial_entropy = true;
                break;
            case 'c':
                initial_entropy = false;
                break;
            case 'a':
                all_bits = true;
                break;
            case 't':
                all_bits = false;
                break;
            case 'v':
                verbose++;
                break;
            case 'q':
                quietMode = true;
                break;
            case 'l':
                inint = strtoull(optarg, &nextOption, 0);
                if ((inint > ULONG_MAX) || (errno == EINVAL) || (nextOption == NULL) || (*nextOption != ',')) {
                    printf("Error on index/samples.\n");
                    print_usage();
                }
                subsetIndex = inint;

                nextOption++;

                inint = strtoull(nextOption, NULL, 0);
                if ((inint > ULONG_MAX) || (errno == EINVAL)) {
                    printf("Error on index/samples.\n");
                    print_usage();
                }
                subsetSize = inint;
                break;
            default:
                print_usage();
        }
    }

    argc -= optind;
    argv += optind;

    if ((argc != 1) && (argc != 2)) {
        printf("Incorrect usage.\n");
        print_usage();
    }

    if (quietMode) {
        verbose = 0;
    }

    file_path = argv[0];

    char hash[129]; // Increased size for dummy hash
    sha256_file(file_path, hash);

    if (argc == 2) {
        inint = atoi(argv[1]);
        if (inint < 1 || inint > 8) {
            printf("Invalid bits per symbol.\n");
            print_usage();
        } else {
            data.word_size = inint;
        }
    }

    if (verbose > 1) {
        if (subsetSize == 0) printf("Opening file: '%s' (hash %s)\n", file_path, hash);
        else printf("Opening file: '%s' (hash %s), reading block %ld of size %ld\n", file_path, hash, subsetIndex, subsetSize);
    }

    if (!read_file_subset(file_path, &data, subsetIndex, subsetSize, NULL)) {
        printf("Error reading file.\n");
        exit(-1);
    }

    if (verbose > 1) printf("Loaded %ld samples of %d distinct %d-bit-wide symbols\n", data.len, data.alph_size, data.word_size);

    if (data.alph_size <= 1) {
        printf("Symbol alphabet consists of 1 symbol. No entropy awarded...\n");
        free_data(&data);
        exit(-1);
    }

    if (!all_bits && (data.blen > MIN_SIZE)) data.blen = MIN_SIZE;

    if ((verbose > 1) && ((data.alph_size > 2) || !initial_entropy)) printf("Number of Binary Symbols: %ld\n", data.blen);
    if (data.len < MIN_SIZE) printf("\n*** Warning: data contains less than %d samples ***\n\n", MIN_SIZE);

    H_original = data.word_size;
    H_bitstring = 1.0;

    if ((verbose == 1) || (verbose == 2)) {
        printf("\nRunning Compression Test Only...\n\n");
    }

    // Section 6.3.4 - Estimate entropy with Compression Test (for bit strings only)
    if (((data.alph_size > 2) || !initial_entropy)) {
        ret_min_entropy = compression_test(data.bsymbols, data.blen, verbose, "Bitstring");
        if (ret_min_entropy >= 0) {
            if (verbose >= 1) printf("Compression Test Estimate (bit string) = %f / 1 bit(s)\n", ret_min_entropy);
            H_bitstring = std::min(ret_min_entropy, H_bitstring);
        } else {
            printf("Compression test failed for bitstring data\n");
            free_data(&data);
            exit(-1);
        }
    }

    if (initial_entropy && (data.alph_size == 2)) {
        ret_min_entropy = compression_test(data.symbols, data.len, verbose, "Literal");
        if (ret_min_entropy >= 0) {
            if (verbose >= 1) printf("Compression Test Estimate = %f / 1 bit(s)\n", ret_min_entropy);
            H_original = std::min(ret_min_entropy, H_original);
        } else {
            printf("Compression test failed for literal data\n");
            free_data(&data);
            exit(-1);
        }
    }

    // Calculate assessed entropy
    h_assessed = data.word_size;

    if ((data.alph_size > 2) || !initial_entropy) {
        h_assessed = std::min(h_assessed, H_bitstring * data.word_size);
    }

    if (initial_entropy) {
        h_assessed = std::min(h_assessed, H_original);
    }

    // Output results
    if ((verbose == 1) || (verbose == 2)) {
        printf("\n=== COMPRESSION TEST RESULTS ===\n");
        if (initial_entropy) {
            printf("H_original: %f\n", H_original);
            if (data.alph_size > 2) {
                printf("H_bitstring: %f\n", H_bitstring);
                printf("min(H_original, %d X H_bitstring): %f\n", data.word_size, std::min(H_original, data.word_size * H_bitstring));
            }
        } else {
            printf("h': %f\n", H_bitstring);
        }
        printf("\nAssessed Min-Entropy (Compression Test Only): %f\n", h_assessed);
        printf("=================================\n");
    } else if (verbose > 2) {
        if ((data.alph_size > 2) || !initial_entropy) {
            printf("H_bitstring = %.17g\n", H_bitstring);
            printf("H_bitstring Per Symbol = %.17g\n", H_bitstring * data.word_size);
        }

        if (initial_entropy) {
            printf("H_original = %.17g\n", H_original);
        }

        printf("Assessed min entropy (compression only): %.17g\n", h_assessed);
    } else if (verbose == 0) {
        printf("%.6f\n", h_assessed);
    }

    free_data(&data);
    return 0;
}