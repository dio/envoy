package abi

import (
	"runtime"
	"sync"
	"unsafe"
)

const numManagerShards = 32

type managedValue[T any] struct {
	item  *T
	token *uint64
	pin   runtime.Pinner
}

// The managers keep Go objects alive and expose only pinned, pointer-free tokens to Envoy.
type manager[T any] struct {
	data  [numManagerShards]map[uintptr]*managedValue[T]
	mutex [numManagerShards]sync.Mutex
}

func (m *manager[T]) record(item *T) unsafe.Pointer {
	// Pin only the token; the object graph stays in Go. Go references to a removed token also
	// prevent its address being reused while shared-data metadata still refers to that token.
	entry := &managedValue[T]{item: item, token: new(uint64)}
	entry.pin.Pin(entry.token)
	pointer := unsafe.Pointer(entry.token)
	index := (uintptr(pointer) >> 4) % numManagerShards
	m.mutex[index].Lock()
	defer m.mutex[index].Unlock()
	// Assume the map is initialized.
	m.data[index][uintptr(pointer)] = entry
	return pointer
}

func (m *manager[T]) unwrap(itemPtr unsafe.Pointer) *T {
	return m.search(uintptr(itemPtr))
}

func (m *manager[T]) search(key uintptr) *T {
	index := (key >> 4) % numManagerShards
	m.mutex[index].Lock()
	defer m.mutex[index].Unlock()
	entry := m.data[index][key]
	if entry == nil {
		return nil
	}
	return entry.item
}

func (m *manager[T]) remove(itemPtr unsafe.Pointer) {
	index := (uintptr(itemPtr) >> 4) % numManagerShards
	m.mutex[index].Lock()
	defer m.mutex[index].Unlock()
	if entry, exists := m.data[index][uintptr(itemPtr)]; exists {
		delete(m.data[index], uintptr(itemPtr))
		entry.pin.Unpin()
	}
}

func newManager[T any]() *manager[T] {
	m := &manager[T]{}
	for i := 0; i < numManagerShards; i++ {
		m.data[i] = make(map[uintptr]*managedValue[T])
	}
	return m
}
