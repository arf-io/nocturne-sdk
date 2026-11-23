# Surface Lab

Surface Lab draws the waterline of a small wave field and a heave rail
with a `B` marker for the floating body's vertical position. The panel
shows the sample time, displaced volume, and heave. It uses the
installed simulation target, the ordinary panel and line plot, and a
small custom paint component.

Build against a Nocturne prefix that contains the `simulation`
component. The same example is in `share/nocturne/examples/surface-lab`
in the alpha archive.

```sh
cmake -S examples/surface-lab -B build-surface-lab -DCMAKE_PREFIX_PATH=/path/to/nocturne-install
cmake --build build-surface-lab
./build-surface-lab/surface_lab 0
./build-surface-lab/surface_lab 80
ctest --test-dir build-surface-lab --output-on-failure
```

The optional step count defaults to 80. Every step advances 10 ms of
caller-owned simulation time. Wave samples provide a tangent plane for
hull immersion. Buoyancy supplies the load for the body step. The
example presents one plain frame and can run in scripts or pipes.

The frame check verifies that the stock plot and custom marker change
as the model advances, and that removing the panel removes its rendered
children from the next published frame.
