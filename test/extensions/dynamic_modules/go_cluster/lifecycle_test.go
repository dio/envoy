package e2e

import (
	"context"
	"crypto/sha256"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"
)

var fixtureClient = &http.Client{Timeout: 5 * time.Second}

const envoyCommit = "1b6b34b9d6f1b5980acc0e3d32892d920ee98536"

func TestMain(m *testing.M) {
	binary := os.Getenv("ENVOY_BIN")
	out, err := exec.Command(binary, "--version").CombinedOutput()
	if err != nil || !strings.Contains(string(out), envoyCommit) {
		fmt.Fprintf(os.Stderr, "missing or incompatible ENVOY_BIN: %s: %v\n", out, err)
		os.Exit(1)
	}
	file, err := os.Open(binary)
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	digest := sha256.New()
	_, err = io.Copy(digest, file)
	file.Close()
	if err != nil || fmt.Sprintf("%x", digest.Sum(nil)) != "3ae777a2f9a611017028e368450f3181c120bda52468b2822c0a7bbd64f3fb66" {
		fmt.Fprintln(os.Stderr, "Envoy binary checksum does not match the pinned SDK fixture runtime")
		os.Exit(1)
	}
	if _, err := os.Stat("libsdk_cluster_e2e.so"); err != nil {
		fmt.Fprintln(os.Stderr, "run make build:", err)
		os.Exit(1)
	}
	os.Exit(m.Run())
}

type controlState struct {
	mu       sync.Mutex
	events   map[string]chan struct{}
	releases map[string]chan struct{}
	shutdown func()
	admin    string
}

func (c *controlState) channel(m map[string]chan struct{}, key string) chan struct{} {
	c.mu.Lock()
	defer c.mu.Unlock()
	if m[key] == nil {
		m[key] = make(chan struct{})
	}
	return m[key]
}
func (c *controlState) event(key string) chan struct{}   { return c.channel(c.events, key) }
func (c *controlState) release(key string) chan struct{} { return c.channel(c.releases, key) }
func (c *controlState) mark(key string) {
	ch := c.event(key)
	c.mu.Lock()
	defer c.mu.Unlock()
	select {
	case <-ch:
	default:
		close(ch)
	}
}

func (c *controlState) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/mutation" || r.URL.Path == "/block-worker" {
		key := strings.TrimPrefix(r.URL.Path, "/")
		c.mark(key)
		select {
		case <-c.release(key):
		case <-r.Context().Done():
		}
	} else if r.URL.Path == "/teardown" {
		c.mark("teardown")
		select {
		case <-c.release("worker-teardown"):
		case <-r.Context().Done():
		}
	} else if id, ok := strings.CutPrefix(r.URL.Path, "/select/"); ok {
		c.mark("select/" + id)
		select {
		case <-c.release(id):
		case <-r.Context().Done():
		}
	} else if id, ok := strings.CutPrefix(r.URL.Path, "/event/"); ok {
		c.mark(id)

	}

}
func wait(t testing.TB, ch <-chan struct{}) {
	t.Helper()
	select {
	case <-ch:
	case <-time.After(10 * time.Second):
		t.Fatal("lifecycle barrier timed out")
	}
}

type fixtureOptions struct {
	clusterName string
	env         []string
}

func start(t testing.TB, options ...fixtureOptions) (string, *controlState) {
	t.Helper()
	c := &controlState{events: map[string]chan struct{}{}, releases: map[string]chan struct{}{}}
	option := fixtureOptions{clusterName: "lifecycle"}
	if len(options) > 0 {
		option = options[0]
	}
	control := httptest.NewServer(c)
	t.Cleanup(control.Close)
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { fmt.Fprint(w, "upstream") }))
	t.Cleanup(upstream.Close)
	adminFile := filepath.Join(t.TempDir(), "admin")

	config := fmt.Sprintf(`admin:
  address: {socket_address: {address: 127.0.0.1, port_value: 0}}
static_resources:
  listeners:
  - name: ingress
    address: {socket_address: {address: 127.0.0.1, port_value: 0}}
    listener_filters: []
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress
          route_config:
            virtual_hosts:
            - name: all
              domains: ["*"]
              routes:
              - match: {prefix: /}
                route: {cluster: upstream, timeout: 15s}
          http_filters:
          - name: envoy.filters.http.router
            typed_config: {"@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router}
  clusters:
  - name: upstream
    connect_timeout: 1s
    lb_policy: CLUSTER_PROVIDED
    cluster_type:
      name: envoy.clusters.dynamic_modules
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_modules.v3.ClusterConfig
        dynamic_module_config: {name: sdk_cluster_e2e}
        cluster_name: %s
`, option.clusterName)
	configFile := filepath.Join(t.TempDir(), "envoy.yaml")
	if err := os.WriteFile(configFile, []byte(config), 0600); err != nil {
		t.Fatal(err)
	}
	logFile, err := os.Create(filepath.Join(t.TempDir(), "envoy.log"))
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		b, _ := os.ReadFile(logFile.Name())
		if t.Failed() {
			t.Log(string(b))
		}
		logFile.Close()
	})
	cwd, _ := os.Getwd()
	cmd := exec.Command(os.Getenv("ENVOY_BIN"), "-c", configFile, "--concurrency", "1", "--admin-address-path", adminFile, "--log-level", "warning", "--disable-hot-restart")
	cmd.Env = append(os.Environ(), "ENVOY_DYNAMIC_MODULES_SEARCH_PATH="+cwd, "UPSTREAM_CONTROL="+control.URL, "UPSTREAM_ADDRESS="+upstream.Listener.Addr().String())
	cmd.Env = append(cmd.Env, option.env...)
	cmd.Env = append(cmd.Env, "GODEBUG=cgocheck=1")
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	if err := cmd.Start(); err != nil {
		t.Fatal(err)
	}
	done := make(chan struct{})
	var exitErr error
	go func() { exitErr = cmd.Wait(); close(done) }()
	var stop sync.Once
	c.shutdown = func() {
		stop.Do(func() {
			cmd.Process.Signal(os.Interrupt)
			select {
			case <-done:
			case <-time.After(5 * time.Second):
				cmd.Process.Kill()
				<-done
			}
		})
	}
	t.Cleanup(c.shutdown)
	if option.clusterName == "lifecycle" {
		wait(t, c.event("initialized"))
	}
	// The admin API reports the actual listener port, allowing Envoy and the upstream to bind port 0.
	var address string
	deadline := time.NewTimer(10 * time.Second)
	defer deadline.Stop()
	ticker := time.NewTicker(10 * time.Millisecond)
	defer ticker.Stop()
	for address == "" {
		select {
		case <-done:
			t.Fatalf("Envoy exited: %v", exitErr)
		case <-deadline.C:
			t.Fatal("listener readiness timed out")
		case <-ticker.C:
		}
		b, err := os.ReadFile(adminFile)
		if err != nil {
			continue
		}
		c.admin = "http://" + strings.TrimSpace(string(b))
		r, err := fixtureClient.Get(c.admin + "/listeners")
		if err != nil {
			continue
		}
		b, _ = io.ReadAll(r.Body)
		r.Body.Close()
		for line := range strings.SplitSeq(string(b), "\n") {
			if a, ok := strings.CutPrefix(line, "ingress::"); ok {
				address = strings.TrimSpace(a)
			}
		}
	}
	if _, _, err := net.SplitHostPort(address); err != nil {
		t.Fatal(address, err)
	}
	return "http://" + address, c
}

func request(ctx context.Context, url, id string) error {
	r, _ := http.NewRequestWithContext(ctx, "GET", url, nil)
	r.Header.Set("x-operation", id)
	resp, err := http.DefaultClient.Do(r)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	b, err := io.ReadAll(resp.Body)
	if err == nil && (resp.StatusCode != 200 || string(b) != "upstream") {
		err = fmt.Errorf("status %d: %s", resp.StatusCode, b)
	}
	return err
}

func TestLifecycleImmediate(t *testing.T) {
	url, c := start(t)
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	if err := request(ctx, url, "immediate"); err != nil {
		t.Fatal(err)
	}
	wait(t, c.event("destroy/immediate"))
}

func TestLifecycleCancellationAndConcurrentSelection(t *testing.T) {
	url, c := start(t)
	ctx, cancel := context.WithCancel(context.Background())
	first := make(chan error, 1)
	go func() { first <- request(ctx, url, "cancelled") }()
	wait(t, c.event("select/cancelled"))
	ctx2, cancel2 := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel2()
	second := make(chan error, 1)
	go func() { second <- request(ctx2, url, "survivor") }()
	wait(t, c.event("select/survivor"))
	cancel()
	<-first
	wait(t, c.event("destroy/cancelled"))
	close(c.release("cancelled"))
	wait(t, c.event("posted/cancelled/false"))
	close(c.release("survivor"))
	if err := <-second; err != nil {
		t.Fatal(err)
	}
	wait(t, c.event("destroy/survivor"))
}

func TestLifecycleLateCompletionAfterNewSelection(t *testing.T) {
	url, c := start(t)
	for i := 0; i < 20; i++ {
		oldID := fmt.Sprintf("old-%d", i)
		newID := fmt.Sprintf("new-%d", i)
		ctx, cancel := context.WithCancel(context.Background())
		old := make(chan error, 1)
		go func() { old <- request(ctx, url, oldID) }()
		wait(t, c.event("select/"+oldID))
		cancel()
		<-old
		wait(t, c.event("destroy/"+oldID))
		nextCtx, nextCancel := context.WithTimeout(context.Background(), 10*time.Second)
		next := make(chan error, 1)
		go func() { next <- request(nextCtx, url, newID) }()
		wait(t, c.event("select/"+newID))
		close(c.release(oldID))
		wait(t, c.event("posted/"+oldID+"/false"))
		close(c.release(newID))
		if err := <-next; err != nil {
			t.Fatal(err)
		}
		nextCancel()
		wait(t, c.event("destroy/"+newID))
	}
}

func TestLifecycleCancellationRacesPosting(t *testing.T) {
	url, c := start(t)
	for i := 0; i < 20; i++ {
		id := fmt.Sprintf("race-%d", i)
		ctx, cancel := context.WithCancel(context.Background())
		result := make(chan error, 1)
		go func() { result <- request(ctx, url, id) }()
		wait(t, c.event("select/"+id))
		barrier := make(chan struct{})
		posted := make(chan struct{})
		go func() { <-barrier; close(c.release(id)); close(posted) }()
		close(barrier)
		cancel()
		<-posted
		<-result
		wait(t, c.event("destroy/"+id))
	}
}

func TestLifecycleWorkerTeardown(t *testing.T) {
	url, c := start(t, fixtureOptions{clusterName: "lifecycle", env: []string{"UPSTREAM_TEARDOWN_BARRIER=true"}})
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	result := make(chan error, 1)
	go func() { result <- request(ctx, url, "teardown") }()
	wait(t, c.event("select/teardown"))
	stopped := make(chan struct{})
	go func() { c.shutdown(); close(stopped) }()
	wait(t, c.event("teardown"))
	close(c.release("teardown"))
	wait(t, c.event("posted/teardown/false"))
	close(c.release("worker-teardown"))
	wait(t, stopped)
	wait(t, c.event("worker-destroyed"))
	wait(t, c.event("cluster-destroyed"))
	<-result
}

func TestLifecycleQueuedCompletionRetainsHost(t *testing.T) {
	for _, cancelPosted := range []bool{false, true} {
		t.Run(fmt.Sprintf("cancel_after_post_%t", cancelPosted), func(t *testing.T) {
			url, c := start(t)
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()
			selected := make(chan error, 1)
			go func() { selected <- request(ctx, url, "leased") }()
			wait(t, c.event("select/leased"))
			blockCtx, blockCancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer blockCancel()
			blocked := make(chan error, 1)
			go func() { blocked <- request(blockCtx, url, "block-worker") }()
			wait(t, c.event("block-worker"))
			close(c.release("leased"))
			wait(t, c.event("posted/leased/true"))
			close(c.release("mutation"))
			wait(t, c.event("retiring"))
			select {
			case <-c.event("removed"):
				t.Fatal("removed a host retained by a queued completion")
			default:
			}
			if cancelPosted {
				cancel()
			}
			close(c.release("block-worker"))
			<-blocked
			err := <-selected
			if cancelPosted && err == nil {
				t.Fatal("cancelled client completed successfully")
			}
			if !cancelPosted && err != nil {
				t.Fatal("queued completion lost its host", err)
			}
			wait(t, c.event("destroy/leased"))
			wait(t, c.event("removed"))
			wait(t, c.event("membership/0"))
		})
	}
}
