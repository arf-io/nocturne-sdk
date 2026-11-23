# Model Bench

Model Bench combines an ordinary Nocturne editor with an interactive view
of a textured mesh. The example uses only installed public headers and
the spatial and POSIX host targets.

Build it against a relocated SDK prefix or the extracted alpha archive.

```sh
cmake -S examples/model-bench -B build-model-bench -DCMAKE_PREFIX_PATH=/path/to/Nocturne-alpha
cmake --build build-model-bench
./build-model-bench/model_bench
```

For the copy shipped in the alpha archive, point `-S` at
`share/nocturne/examples/model-bench` inside the prefix.

The editor starts focused. Type a note, then use Tab to reach the mesh.
Left and Right orbit the camera. Enter raises or lowers the mesh.
Click a covered cell to see its object and face IDs. Ctrl+Q exits.
In a cell-only monochrome terminal, `#` marks the covered mesh cells.
Use Tab, the arrows and Enter to operate that view.

The app paints a candidate raster during publication and uses the
previous raster for pointer events until the new frame commits. Its
resize callback updates the raster and restores eligible focus before
the host reads the next input batch. Geometry, bindings and selection
policy belong to the example.

Run the real-terminal check with a locally installed tmux.

```sh
python3 examples/model-bench/verify_terminal.py ./build-model-bench/model_bench
```
