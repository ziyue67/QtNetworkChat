FROM debian:bookworm AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        libgl1-mesa-dev \
        libssl-dev \
        ninja-build \
        pkg-config \
        qt6-base-dev \
        qt6-multimedia-dev \
        qt6-websockets-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DQTNETWORKCHAT_E2E_ENABLE_PRODUCTION_CRYPTO=ON \
        -DQTNETWORKCHAT_E2E_LINK_PRODUCTION_ADAPTER=ON \
    && cmake --build /build --target qqnt_server --parallel

FROM debian:bookworm-slim AS runtime

ARG VCS_REF=unknown
ARG BUILD_VERSION=dev

LABEL org.opencontainers.image.title="QtNetworkChat server" \
      org.opencontainers.image.description="Headless QtNetworkChat TCP server" \
      org.opencontainers.image.source="https://github.com/ziyue67/QtNetworkChat" \
      org.opencontainers.image.licenses="MIT" \
      org.opencontainers.image.revision="$VCS_REF" \
      org.opencontainers.image.version="$BUILD_VERSION"

ENV DEBIAN_FRONTEND=noninteractive \
    QTNETWORKCHAT_APPDATA_DIR=/data \
    QTNETWORKCHAT_DB_DRIVER=QPSQL \
    QTNETWORKCHAT_REDIS=1 \
    QQNT_SERVER_PORT=8888

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        libqt6core6 \
        libqt6gui6 \
        libqt6network6 \
        libqt6sql6 \
        libqt6sql6-psql \
        libqt6sql6-sqlite \
        libssl3 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 10001 qqnt \
    && useradd --uid 10001 --gid qqnt --no-create-home --home-dir /data --shell /usr/sbin/nologin qqnt \
    && install -d -o qqnt -g qqnt /data

COPY --from=build /build/qqnt_server /usr/local/bin/qqnt_server
COPY LICENSE /usr/share/licenses/qtnetworkchat/LICENSE

WORKDIR /data
USER 10001:10001
VOLUME ["/data"]
EXPOSE 8888

HEALTHCHECK --interval=15s --timeout=5s --start-period=20s --retries=5 \
    CMD ["bash", "-ec", "exec 3<>/dev/tcp/127.0.0.1/${QQNT_SERVER_PORT}"]

ENTRYPOINT ["/usr/local/bin/qqnt_server"]
CMD ["--port", "8888"]
