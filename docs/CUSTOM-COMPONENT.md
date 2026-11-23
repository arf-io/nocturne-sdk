# Mount a clock-driven custom component

Field Log's pulse displays one of four phases at a fixed width. It is a
leaf that owns no terminal I/O. Build the installed example from the
[quickstart](QUICKSTART.md) to run the exact implementation.
[Pulse implementation](../examples/field-log/pulse.c)

## Connect state to the tree

`Pulse` holds a cell style and the current phase. The application stores
it in the live `FieldLog` state and mounts a pointer to it with the
`pulse_ops` descriptor.

```c
NtNodeId pulse_node;
const NtMountOptions visible = { .enabled = true };
NtResult result = nt_tree_mount(tree, header, &pulse_ops, &model->pulse,
                                visible, &pulse_node);
```

In this fragment `tree` is the live tree, `header` is its mounted row, and
`model` is the caller's `FieldLog`. The tree copies the descriptor. Keep
the state alive through detachment. For a heap-owned component, provide a
`dispose` callback and transfer the cleanup obligation only after a
successful mount.

## Implement the lifecycle

- `measure` requests seven columns and one row, then fits the result to
  the incoming bounds. Measure and layout may run again on resize. A
  container would measure and place its direct children using those
  contexts. This leaf returns an error if it receives children.
- `update` divides the caller's microsecond time by the period to find the
  current phase, requests paint on a changed phase, and schedules the next
  boundary. A delayed step skips expired phases. With reduced motion the
  phase is steady and the component stops scheduling ticks.
- `paint` draws the glyphs of the current phase only in assigned cells.
  Its coordinates are node-local. The paint context clips to the
  candidate placement.
- An interactive component can add `event`. Check `nt_event_phase` before
  handling a key or pointer event. Use the reply to request focus, capture
  or repaint, and use `nt_event_emit` to send an app action after the
  callback. Handle cancellation and focus loss for any held gesture.

Successful publication makes a frame and its geometry visible together.
The previous frame remains available after failure. Keep device I/O,
sleeping, application actions, and mutation of the component tree in the
caller. Field Log uses a monotonic clock, asks the tree and decoder for
their deadlines, and lets input wake the poll early.

[Component protocol and errors](COMPONENT-PROTOCOL.md) · [Custom pulse and host loop](../examples/field-log)
