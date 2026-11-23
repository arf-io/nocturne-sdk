// Command signal runs a keyboard-controlled meter with the installed SDK.
package main

import (
	"bufio"
	"errors"
	"fmt"
	"os"

	"github.com/arf-io/nocturne-sdk/bindings/go"
)

const width = 12

var palette = nocturne.Style{
	Foreground: nocturne.Color{Red: 100, Green: 230, Blue: 190, Alpha: 255},
	Background: nocturne.Color{Red: 24, Green: 32, Blue: 46, Alpha: 255},
}

type meter struct {
	nocturne.ComponentBase
	level uint32
}

func (*meter) Measure(*nocturne.Measure, nocturne.Constraints) (nocturne.Size, error) {
	return nocturne.Size{Columns: width, Rows: 1}, nil
}

func (signal *meter) Paint(paint *nocturne.Paint, assigned nocturne.Size) error {
	for column := range min(width, assigned.Columns) {
		glyph := "."
		if column < signal.level {
			glyph = "#"
		}
		if err := paint.Glyph(nocturne.Point{X: int32(column)}, glyph, 1, palette); err != nil {
			return err
		}
	}
	return nil
}

func (signal *meter) Event(event *nocturne.Event) error {
	if event.Kind() != nocturne.KeyEvent {
		return nil
	}
	key, err := event.Key()
	if err != nil || key.State != nocturne.KeyPress {
		return err
	}
	switch key.Code {
	case '+':
		signal.level = min(width, signal.level+1)
	case '-':
		if signal.level > 0 {
			signal.level--
		}
	default:
		return nil
	}
	if err := event.Consume(); err != nil {
		return err
	}
	return event.Invalidate(nocturne.InvalidatePaint)
}

func render(tree *nocturne.Tree) error {
	size := nocturne.Size{Columns: width, Rows: 2}
	if err := tree.Publish(size); err != nil {
		return err
	}
	frame, err := tree.Frame()
	if err != nil {
		return err
	}
	for row := range size.Rows {
		for column := uint32(0); column < size.Columns; {
			cell, err := frame.Cell(nocturne.Point{X: int32(column), Y: int32(row)})
			if err != nil {
				return err
			}
			glyph := cell.Glyph
			if cell.Columns == 0 {
				glyph = " "
			}
			if _, err := fmt.Print(glyph); err != nil {
				return err
			}
			column += uint32(max(cell.Columns, 1))
		}
		if _, err := fmt.Println(); err != nil {
			return err
		}
	}
	return nil
}

func run() (err error) {
	tree, err := nocturne.NewTree()
	if err != nil {
		return err
	}
	defer func() { err = errors.Join(err, tree.Close()) }()
	passive := nocturne.MountOptions{Enabled: true}
	root, err := tree.MountBox(nocturne.Node{}, passive, nocturne.Box{Kind: nocturne.BoxColumn})
	if err != nil {
		return err
	}
	if _, err := tree.MountLabel(root, passive, "Signal level", palette); err != nil {
		return err
	}
	focusable := nocturne.MountOptions{Enabled: true, Focusable: true}
	node, err := tree.Mount(root, focusable, &meter{level: 4})
	if err != nil {
		return err
	}
	var now uint64
	if err := tree.Tick(now, nocturne.MotionFull); err != nil {
		return err
	}
	if err := render(tree); err != nil {
		return err
	}
	if err := tree.SetFocus(node); err != nil {
		return err
	}

	input := bufio.NewScanner(os.Stdin)
	for input.Scan() {
		line := input.Text()
		if line == "" {
			continue
		}
		switch line[0] {
		case 'q':
			return nil
		case '+', '-':
			now += 50_000
			if err := tree.Tick(now, nocturne.MotionFull); err != nil {
				return err
			}
			key := nocturne.KeyInput{Code: uint32(line[0]), State: nocturne.KeyPress}
			value := nocturne.Input{Kind: nocturne.KeyEvent, Key: key}
			if _, err := tree.Dispatch(now, value, nocturne.DispatchPolicy{}); err != nil {
				return err
			}
			if err := render(tree); err != nil {
				return err
			}
		}
	}
	return input.Err()
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
