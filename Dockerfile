# Stage 1: Build binary statically
FROM alpine:3.19 AS builder
RUN apk add --no-cache gcc musl-dev make
WORKDIR /build
COPY . .
RUN make clean && make all

# Stage 2: Minimal runtime image (~3 MB)
FROM alpine:3.19
RUN apk add --no-cache ca-certificates curl
COPY --from=builder /build/bin/wheelmq /usr/local/bin/wheelmq
COPY --from=builder /build/bin/wheelmq-cli /usr/local/bin/wheelmq-cli

EXPOSE 7000
ENTRYPOINT ["wheelmq"]
CMD ["--id", "node-1", "--bind", "0.0.0.0", "--port", "7000"]
