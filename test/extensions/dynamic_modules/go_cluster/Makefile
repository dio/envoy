GOTOOLCHAIN ?= go1.26.7
export GOTOOLCHAIN

.PHONY: build test vet clean
build:
	GOWORK=off CGO_ENABLED=1 go build -trimpath -buildmode=c-shared -o libsdk_cluster_e2e.so ./cmd

test: build
	GOWORK=off go test -count=1 -v -timeout=90s .

vet:
	GOWORK=off go vet ./...

clean:
	rm -f libsdk_cluster_e2e.so libsdk_cluster_e2e.h
