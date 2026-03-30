FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential cmake libcurl4-openssl-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc) --target bolt

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libcurl4 ca-certificates bubblewrap git && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/bolt /usr/local/bin/bolt

WORKDIR /workspace
ENTRYPOINT ["bolt"]
CMD ["agent"]
