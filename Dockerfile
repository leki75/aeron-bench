FROM gcr.io/alpacahq/aeron-driver-c:1.42.1 AS aeron


FROM debian:12-slim AS base
RUN apt-get update && \
    apt-get install --no-install-recommends -y libnuma1 && \
    rm -rf /var/lib/apt/lists/*
COPY --from=aeron /usr/local/lib/libaeron_static.a /usr/local/lib/


FROM base AS devel
COPY --from=aeron /usr/local/include/ /usr/local/include/aeron/


FROM devel AS builder
RUN apt-get update && \
    apt-get install --no-install-recommends -y gcc libc6-dev make && \
    rm -rf /var/lib/apt/lists/*
WORKDIR /usr/local/src/aeron-bench
COPY Makefile Makefile
COPY src/ src/
RUN ln -s /usr/local/lib
RUN ln -s /usr/local/include
RUN make


FROM base
COPY --from=builder /usr/local/src/aeron-bench/build/aeron-bench-sub /usr/local/bin/
COPY --from=builder /usr/local/src/aeron-bench/build/aeron-bench-pub /usr/local/bin/
