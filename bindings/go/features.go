package nocturne

var linked struct{ graph, posixHost, windowsHost, spatial, simulation bool }

// AvailableComponents reports the native modules selected at build time.
func AvailableComponents() []string {
	components := []string{"nocturne"}
	if linked.posixHost {
		components = append(components, "posix_host")
	}
	if linked.windowsHost {
		components = append(components, "windows_host")
	}
	if linked.graph {
		components = append(components, "graph")
	}
	if linked.spatial {
		components = append(components, "spatial")
	}
	if linked.simulation {
		components = append(components, "simulation")
	}
	return components
}
