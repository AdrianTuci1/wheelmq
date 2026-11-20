# Stage 1: Compile static Linux binary using musl libc
FROM alpine:3.19 AS builder
RUN apk add --no-cache gcc musl-dev make
WORKDIR /build
COPY . .
RUN CFLAGS="-Wall -Wextra -Werror -O3 -std=c11 -Iinclude -pthread -static" make clean all

# Stage 2: Ultra-minimal production runtime image (~3 MB)
FROM alpine:3.19
RUN apk add --no-cache ca-certificates curl
COPY --from=builder /build/bin/wheelmq /usr/local/bin/wheelmq
COPY --from=builder /build/bin/wheelmq-cli /usr/local/bin/wheelmq-cli

EXPOSE 7000
ENTRYPOINT ["wheelmq"]
CMD ["--id", "node-1", "--bind", "0.0.0.0", "--port", "7000"]
