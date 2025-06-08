FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install required packages for both original and minimal builds
RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    g++ \
    libomp-dev \
    libdivsufsort-dev \
    libbz2-dev \
    libjsoncpp-dev \
    libssl-dev \
    libmpfr-dev \
    python3 \
    python3-pip \
    xxd \
    bc \
    && rm -rf /var/lib/apt/lists/*

# Clone the repository
RUN git clone https://github.com/dr-ke/SP800-90B_EntropyAssessment.git /opt/entropy

# Set working directory
WORKDIR /opt/entropy/

# Choose the branch with compression only test
RUN git checkout feature/fast-compression-test

# Build original implementation
WORKDIR /opt/entropy/cpp
RUN make compression_only

# Build minimal implementation (assuming files are in repo)
WORKDIR /opt/entropy
RUN g++ -std=c++11 -O2 -s compression_minimal.cpp -o compression_minimal

# Use existing test data from the repository
WORKDIR /opt/entropy/bin

# Create validation script using simple approach
RUN printf '#!/bin/bash\n' > /opt/validate.sh
RUN printf 'echo "=== NIST SP800-90B Compression Test Validation ==="\n' >> /opt/validate.sh
RUN printf 'echo "Comparing Original vs Minimal Implementation"\n' >> /opt/validate.sh
RUN printf 'echo "================================================"\n' >> /opt/validate.sh
RUN printf 'cd /opt/entropy/bin\n' >> /opt/validate.sh
RUN printf 'echo "Available files:"\n' >> /opt/validate.sh
RUN printf 'ls -la *.bin 2>/dev/null || echo "No .bin files found"\n' >> /opt/validate.sh
RUN printf 'echo ""\n' >> /opt/validate.sh
RUN printf 'echo "Testing with first available large file..."\n' >> /opt/validate.sh
RUN printf 'test_file=$(ls -S *.bin 2>/dev/null | head -1)\n' >> /opt/validate.sh
RUN printf 'if [ -z "$test_file" ]; then\n' >> /opt/validate.sh
RUN printf '  echo "No test files found"\n' >> /opt/validate.sh
RUN printf '  exit 1\n' >> /opt/validate.sh
RUN printf 'fi\n' >> /opt/validate.sh
RUN printf 'echo "Using file: $test_file"\n' >> /opt/validate.sh
RUN printf 'echo ""\n' >> /opt/validate.sh
RUN printf 'for bits in 1 4 8; do\n' >> /opt/validate.sh
RUN printf '  echo "Testing with $bits bits..."\n' >> /opt/validate.sh
RUN printf '  orig=$(/opt/entropy/cpp/ea_compression_only -q "$test_file" $bits 2>/dev/null)\n' >> /opt/validate.sh
RUN printf '  mini=$(/opt/entropy/compression_minimal -q "$test_file" $bits 2>/dev/null)\n' >> /opt/validate.sh
RUN printf '  if [ "$orig" = "$mini" ]; then\n' >> /opt/validate.sh
RUN printf '    echo "✅ PASS: $bits bits - Results identical ($orig)"\n' >> /opt/validate.sh
RUN printf '  else\n' >> /opt/validate.sh
RUN printf '    echo "❌ FAIL: $bits bits - Results differ (orig: $orig, mini: $mini)"\n' >> /opt/validate.sh
RUN printf '  fi\n' >> /opt/validate.sh
RUN printf 'done\n' >> /opt/validate.sh
RUN printf 'echo "Validation complete!"\n' >> /opt/validate.sh
RUN chmod +x /opt/validate.sh

# Create quick test script
RUN printf '#!/bin/bash\n' > /opt/quick_test.sh
RUN printf 'cd /opt/entropy/bin\n' >> /opt/quick_test.sh
RUN printf 'test_file=$(ls -S *.bin 2>/dev/null | head -1)\n' >> /opt/quick_test.sh
RUN printf 'if [ -z "$test_file" ]; then\n' >> /opt/quick_test.sh
RUN printf '  echo "No test files found"\n' >> /opt/quick_test.sh
RUN printf '  exit 1\n' >> /opt/quick_test.sh
RUN printf 'fi\n' >> /opt/quick_test.sh
RUN printf 'echo "Quick test with $test_file..."\n' >> /opt/quick_test.sh
RUN printf 'echo ""\n' >> /opt/quick_test.sh
RUN printf 'echo "Original:"\n' >> /opt/quick_test.sh
RUN printf '/opt/entropy/cpp/ea_compression_only -v "$test_file" 8\n' >> /opt/quick_test.sh
RUN printf 'echo ""\n' >> /opt/quick_test.sh
RUN printf 'echo "Minimal:"\n' >> /opt/quick_test.sh
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