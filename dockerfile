FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install required packages for both original and minimal builds
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    libomp-dev \
    libdivsufsort-dev \
    libbz2-dev \
    libjsoncpp-dev \
    libssl-dev \
    libmpfr-dev \
    xxd \
    bc \
    && rm -rf /var/lib/apt/lists/*

# Copy the entire repository structure
COPY . /opt/entropy/

# Build original compression_only implementation
WORKDIR /opt/entropy/cpp
RUN echo "=== DEBUGGING MAKEFILE ISSUE ==="
RUN echo "Available files:" && ls -la *compression*
RUN echo ""
RUN echo "Makefile compression targets:" && grep -A 3 "compression" Makefile
RUN echo ""
RUN echo "All available Make targets:" && grep "^[a-zA-Z_][a-zA-Z0-9_]*:" Makefile
RUN echo ""

# Try different build approaches
RUN echo "=== Trying different build methods ===" && \
    echo "Method 1: make compression_only" && \
    (make compression_only 2>&1 || echo "Method 1 failed") && \
    echo "" && \
    echo "Method 2: make ea_compression_only" && \
    (make ea_compression_only 2>&1 || echo "Method 2 failed") && \
    echo "" && \
    echo "Method 3: manual build" && \
    g++ -std=c++11 -fopenmp -O2 -ffloat-store -I/usr/include/jsoncpp \
        compression_only_main.cpp -o ea_compression_only_manual \
        -lbz2 -lpthread -ldivsufsort -ldivsufsort64 -ljsoncpp -lcrypto && \
    echo "Manual build succeeded" && \
    echo ""

# Check what we ended up with
RUN echo "=== Build results ===" && \
    ls -la ea_compression_only* && \
    echo ""

# Use whatever binary we have (prefer original make target, fall back to manual)
RUN if [ -f ea_compression_only ]; then \
        echo "Using ea_compression_only from make"; \
    elif [ -f ea_compression_only_manual ]; then \
        echo "Using manual build" && \
        mv ea_compression_only_manual ea_compression_only; \
    else \
        echo "No compression binary found!" && exit 1; \
    fi

RUN echo "Final binary:" && ls -la ea_compression_only

# Build minimal implementation from subdirectory
WORKDIR /opt/entropy/cpp/compression-extracted
RUN echo "Building minimal implementation..." && \
    g++ -std=c++11 -O2 -s compression_minimal.cpp -o compression_minimal

# Move minimal binary to main directory for easier access
RUN cp compression_minimal /opt/entropy/compression_minimal

# Verify both implementations exist
WORKDIR /opt/entropy
RUN echo "Original implementation:" && ls -la cpp/ea_compression_only
RUN echo "Minimal implementation:" && ls -la compression_minimal

# Create validation script using simple approach
RUN printf '#!/bin/bash\n' > /opt/validate.sh
RUN printf 'echo "=== NIST SP800-90B Compression Test Validation ==="\n' >> /opt/validate.sh
RUN printf 'echo "Comparing Original vs Minimal Implementation"\n' >> /opt/validate.sh
RUN printf 'echo "================================================"\n' >> /opt/validate.sh
RUN printf 'cd /opt/entropy/bin\n' >> /opt/validate.sh
RUN printf 'echo "Available test files:"\n' >> /opt/validate.sh
RUN printf 'ls -la *.bin\n' >> /opt/validate.sh
RUN printf 'echo ""\n' >> /opt/validate.sh
RUN printf 'echo "Testing with multiple files and bit widths..."\n' >> /opt/validate.sh
RUN printf 'passed=0\n' >> /opt/validate.sh
RUN printf 'total=0\n' >> /opt/validate.sh
RUN printf 'for file in truerand_8bit.bin data.pi.bin normal.bin; do\n' >> /opt/validate.sh
RUN printf '  if [ ! -f "$file" ]; then continue; fi\n' >> /opt/validate.sh
RUN printf '  for bits in 1 4 8; do\n' >> /opt/validate.sh
RUN printf '    echo "Testing $file with $bits bits..."\n' >> /opt/validate.sh
RUN printf '    total=$((total + 1))\n' >> /opt/validate.sh
RUN printf '    orig=$(/opt/entropy/cpp/ea_compression_only -q "$file" $bits 2>/dev/null)\n' >> /opt/validate.sh
RUN printf '    mini=$(/opt/entropy/compression_minimal -q "$file" $bits 2>/dev/null)\n' >> /opt/validate.sh
RUN printf '    if [ "$orig" = "$mini" ]; then\n' >> /opt/validate.sh
RUN printf '      echo "✅ PASS: $file ($bits bits) - Results identical ($orig)"\n' >> /opt/validate.sh
RUN printf '      passed=$((passed + 1))\n' >> /opt/validate.sh
RUN printf '    else\n' >> /opt/validate.sh
RUN printf '      echo "❌ FAIL: $file ($bits bits) - Results differ (orig: $orig, mini: $mini)"\n' >> /opt/validate.sh
RUN printf '    fi\n' >> /opt/validate.sh
RUN printf '  done\n' >> /opt/validate.sh
RUN printf 'done\n' >> /opt/validate.sh
RUN printf 'echo ""\n' >> /opt/validate.sh
RUN printf 'echo "Results: $passed/$total tests passed"\n' >> /opt/validate.sh
RUN printf 'if [ $passed -eq $total ]; then\n' >> /opt/validate.sh
RUN printf '  echo "🎉 ALL TESTS PASSED!"\n' >> /opt/validate.sh
RUN printf 'else\n' >> /opt/validate.sh
RUN printf '  echo "❌ Some tests failed"\n' >> /opt/validate.sh
RUN printf 'fi\n' >> /opt/validate.sh
RUN chmod +x /opt/validate.sh

# Create quick test script
RUN printf '#!/bin/bash\n' > /opt/quick_test.sh
RUN printf 'cd /opt/entropy/bin\n' >> /opt/quick_test.sh
RUN printf 'test_file="truerand_8bit.bin"\n' >> /opt/quick_test.sh
RUN printf 'if [ ! -f "$test_file" ]; then\n' >> /opt/quick_test.sh
RUN printf '  test_file=$(ls *.bin | head -1)\n' >> /opt/quick_test.sh
RUN printf 'fi\n' >> /opt/quick_test.sh
RUN printf 'echo "Quick test with $test_file..."\n' >> /opt/quick_test.sh
RUN printf 'echo ""\n' >> /opt/quick_test.sh
RUN printf 'echo "Original implementation:"\n' >> /opt/quick_test.sh
RUN printf '/opt/entropy/cpp/ea_compression_only -v "$test_file" 8\n' >> /opt/quick_test.sh
RUN printf 'echo ""\n' >> /opt/quick_test.sh
RUN printf 'echo "Minimal implementation:"\n' >> /opt/quick_test.sh
RUN printf '/opt/entropy/compression_minimal -v "$test_file" 8\n' >> /opt/quick_test.sh
RUN chmod +x /opt/quick_test.sh

# Create size comparison script
RUN printf '#!/bin/bash\n' > /opt/size_comparison.sh
RUN printf 'echo "=== BINARY SIZE COMPARISON ==="\n' >> /opt/size_comparison.sh
RUN printf 'echo "Original implementation:"\n' >> /opt/size_comparison.sh
RUN printf 'ls -lh /opt/entropy/cpp/ea_compression_only\n' >> /opt/size_comparison.sh
RUN printf 'echo ""\n' >> /opt/size_comparison.sh
RUN printf 'echo "Minimal implementation:"\n' >> /opt/size_comparison.sh
RUN printf 'ls -lh /opt/entropy/compression_minimal\n' >> /opt/size_comparison.sh
RUN printf 'echo ""\n' >> /opt/size_comparison.sh
RUN printf 'orig_size=$(stat -c%%s /opt/entropy/cpp/ea_compression_only)\n' >> /opt/size_comparison.sh
RUN printf 'mini_size=$(stat -c%%s /opt/entropy/compression_minimal)\n' >> /opt/size_comparison.sh
RUN printf 'reduction=$((100 - (mini_size * 100 / orig_size)))\n' >> /opt/size_comparison.sh
RUN printf 'echo "Size reduction: ${reduction}%% smaller"\n' >> /opt/size_comparison.sh
RUN chmod +x /opt/size_comparison.sh

# Set working directory
WORKDIR /opt

# Default command runs validation
CMD ["/opt/validate.sh"]