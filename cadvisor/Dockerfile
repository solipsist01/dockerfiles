FROM golang as build
RUN apt update && apt install -y git dmsetup
RUN git clone \
        --branch master \
        --depth 1 \
        https://github.com/google/cadvisor.git \
        /go/src/github.com/google/cadvisor
WORKDIR /go/src/github.com/google/cadvisor
RUN make build

FROM debian
COPY --from=build /go/src/github.com/google/cadvisor/cadvisor /usr/bin/cadvisor
EXPOSE 8080
ENTRYPOINT ["/usr/bin/cadvisor", "-logtostderr"]
