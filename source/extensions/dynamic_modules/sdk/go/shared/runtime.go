package shared

// RuntimeValueKind chooses the native runtime getter.
type RuntimeValueKind uint32

const (
	RuntimeBoolean RuntimeValueKind = iota
	RuntimeInteger
	RuntimeDouble
	RuntimeString
)

// RuntimeRequest reads one key, using the corresponding fallback for a missing or invalid scalar.
// String reads distinguish missing from empty. Boolean fallbacks are caller-provided, including
// for Envoy runtime-feature keys; other kinds cannot read runtime-feature keys.
type RuntimeRequest struct {
	Key             string
	Kind            RuntimeValueKind
	FallbackBoolean bool
	FallbackInteger uint64
	FallbackDouble  float64
}

// RuntimeCondition skips the batch when Key is present and equals Expected. Pass nil for the
// first or an unconditional read. The application must change this revision for every relevant
// data change or removal. This comparison does not establish freshness or authenticate data.
type RuntimeCondition struct {
	Key      string
	Expected string
}

// RuntimeValue contains a copied value. Only the field for the requested kind is meaningful.
// String values are Go-owned and remain valid after scratch is reused or Envoy updates runtime.
type RuntimeValue struct {
	Boolean       bool
	Integer       uint64
	Double        float64
	String        string
	StringPresent bool
}

// RuntimeReadStatus describes the whole call. Values are valid only on RuntimeReadOK.
type RuntimeReadStatus uint32

const (
	RuntimeReadOK RuntimeReadStatus = iota
	RuntimeReadUnchanged
	RuntimeReadBufferTooSmall
	RuntimeReadLimitExceeded
	RuntimeReadInvalidArgument
	RuntimeReadUnavailable
)

// RuntimeReader is an optional capability of HTTP request and configuration handles. Use a type
// assertion on an existing handle; existing custom implementations and mocks need not implement it.
// Call only from the current Envoy worker hook or the config's main-thread hook, never from a
// module-created goroutine using a retained host handle. The host does not retain any buffers.
type RuntimeReader interface {
	// ReadRuntimeBatch copies values from one merged snapshot. Scratch is reusable byte storage.
	// The returned size is bytes used on OK or required on BufferTooSmall. Other results return zero.
	// On BufferTooSmall, retry the entire batch with bounded scratch capacity; a retry may see a
	// newer snapshot. The method does not retry automatically or wait for RTDS.
	ReadRuntimeBatch(requests []RuntimeRequest, condition *RuntimeCondition, scratch []byte) ([]RuntimeValue, RuntimeReadStatus, int)
}
