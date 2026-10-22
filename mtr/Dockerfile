FROM eclipse-temurin:25-jdk AS builder

WORKDIR /build

RUN apt-get update && \
    apt-get install -y git && \
    rm -rf /var/lib/apt/lists/*

RUN git clone --depth=1 --branch stonecutter \
    https://github.com/Minecraft-Transit-Railway/Minecraft-Transit-Railway.git .

RUN chmod +x gradlew

ARG GITHUB_TOKEN
ARG GITHUB_ACTOR

ENV GITHUB_TOKEN=$GITHUB_TOKEN
ENV GITHUB_ACTOR=$GITHUB_ACTOR

RUN mkdir -p /root/.gradle && \
    printf "gpr.user=%s\ngpr.key=%s\n" "$GITHUB_ACTOR" "$GITHUB_TOKEN" \
    > /root/.gradle/gradle.properties

RUN ./gradlew tasks --all --no-daemon

RUN ./gradlew :1.21.4-neoforge:build --no-daemon

FROM debian:bookworm-slim

WORKDIR /artifacts
COPY --from=builder /build/**/build/libs/*.jar ./

CMD ["bash"]

FROM debian:bookworm-slim AS artifacts

WORKDIR /out

COPY --from=builder /build/versions ./versions

RUN mkdir -p /out/jars && \
    find /out/versions -type f -name "*.jar" -exec cp {} /out/jars/ \;

CMD ["bash"]
