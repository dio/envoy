package sdk

import (
	"testing"

	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

type testClusterConfigFactory struct {
	factory shared.ClusterFactory
	config  []byte
}

type testClusterFactory struct {
	shared.EmptyClusterFactory
}

func (*testClusterFactory) Create(shared.ClusterHandle) shared.Cluster {
	return &shared.EmptyCluster{}
}

func (f *testClusterConfigFactory) Create(config []byte) (shared.ClusterFactory, error) {
	f.config = append([]byte{}, config...)
	return f.factory, nil
}

func TestClusterConfigFactories(t *testing.T) {
	const name = "go-sdk-cluster-test"
	delete(clusterConfigFactoryRegistry, name)
	t.Cleanup(func() {
		delete(clusterConfigFactoryRegistry, name)
	})

	factory := &testClusterFactory{}
	configFactory := &testClusterConfigFactory{factory: factory}
	RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{
		name: configFactory,
	})

	if got := GetClusterConfigFactory(name); got != configFactory {
		t.Fatalf("GetClusterConfigFactory(%q) = %T, want registered factory", name, got)
	}

	got, err := NewClusterFactory(name, []byte("config"))
	if err != nil {
		t.Fatalf("NewClusterFactory(%q) returned error: %v", name, err)
	}
	if got != factory {
		t.Fatalf("NewClusterFactory(%q) = %T, want registered cluster factory", name, got)
	}
	if string(configFactory.config) != "config" {
		t.Fatalf("config = %q, want %q", configFactory.config, "config")
	}
}

func TestNewClusterFactoryUnknownName(t *testing.T) {
	if _, err := NewClusterFactory("missing-go-sdk-cluster", nil); err == nil {
		t.Fatal("NewClusterFactory() returned nil error for an unknown cluster")
	}
}

func TestRegisterClusterConfigFactoriesDuplicatePanics(t *testing.T) {
	const name = "go-sdk-cluster-duplicate-test"
	delete(clusterConfigFactoryRegistry, name)
	t.Cleanup(func() {
		delete(clusterConfigFactoryRegistry, name)
	})

	RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{
		name: &testClusterConfigFactory{},
	})

	defer func() {
		if recover() == nil {
			t.Fatal("RegisterClusterConfigFactories() did not panic for a duplicate name")
		}
	}()
	RegisterClusterConfigFactories(map[string]shared.ClusterConfigFactory{
		name: &testClusterConfigFactory{},
	})
}
