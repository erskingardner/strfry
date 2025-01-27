FROM alpine:3.18.3 AS build

ENV TZ=UTC

WORKDIR /build

COPY . .

RUN \
  apk --no-cache add \
    linux-headers \
    git \
    g++ \
    make \
    perl \
    pkgconfig \
    libtool \
    ca-certificates \
    libressl-dev \
    zlib-dev \
    lmdb-dev \
    flatbuffers-dev \
    libsecp256k1-dev \
    zstd-dev \
  && rm -rf /var/cache/apk/* \
  && git submodule update --init \
  && make setup-golpe \
  && make -j2

FROM alpine:3.18.3

WORKDIR /app

RUN \
  apk --no-cache add \
    lmdb \
    flatbuffers \
    libsecp256k1 \
    libb2 \
    zstd \
    libressl \
    curl \
  && rm -rf /var/cache/apk/* \
  && mkdir -p /app/data /app/config

COPY --from=build /build/strfry strfry

EXPOSE 7777

VOLUME ["/app/data", "/app/config"]

ENTRYPOINT ["/app/strfry"]
CMD ["relay"]
