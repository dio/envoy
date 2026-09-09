package abi

import (
	"runtime"
	"sync"
	"testing"
	"unsafe"
)

func TestManagerUsesOpaqueTokenAndRetainsObject(t *testing.T) {
	m := newManager[[]byte]()
	item := []byte("retained")
	token := m.record(&item)
	if token == unsafe.Pointer(&item) {
		t.Fatal("returned the Go object rather than a pointer-free token")
	}
	runtime.GC()
	if got := m.unwrap(token); got == nil || string(*got) != "retained" {
		t.Fatalf("lost registered object: %v", got)
	}
	m.remove(token)
	if m.unwrap(token) != nil {
		t.Fatal("removed object is still registered")
	}
	m.remove(token)
	if m.unwrap(nil) != nil {
		t.Fatal("nil token resolved")
	}
	for range 128 {
		next := m.record(&item)
		if next == token {
			t.Fatal("reused a token address while Go still retains the old token")
		}
		m.remove(next)
	}
	runtime.KeepAlive(token)
}

func TestManagerConcurrentLifetimes(t *testing.T) {
	m := newManager[int]()
	var workers sync.WaitGroup
	for worker := range 8 {
		workers.Add(1)
		go func() {
			defer workers.Done()
			for range 128 {
				token := m.record(&worker)
				if got := m.unwrap(token); got == nil || *got != worker {
					t.Error("token resolved to another worker's object")
				}
				m.remove(token)
			}
		}()
	}
	workers.Wait()
	for _, shard := range m.data {
		if len(shard) != 0 {
			t.Fatal("registry retains destroyed objects")
		}
	}
}
