FROM cgra-gcc13:review
RUN apt-get update && apt-get install -y --no-install-recommends clang-18 libclang-rt-18-dev && rm -rf /var/lib/apt/lists/*
