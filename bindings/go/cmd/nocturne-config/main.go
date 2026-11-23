// Command nocturne-config emits cgo build settings for an installed Nocturne SDK.
package main

import (
	"crypto/sha256"
	_ "embed"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"slices"
	"strings"
)

//go:embed CMakeLists.txt
var discovery []byte

var optional = []string{"posix_host", "windows_host", "graph", "spatial", "simulation"}

func main() {
	prefix := flag.String("prefix", os.Getenv("NOCTURNE_SDK_PREFIX"), "installed C SDK prefix")
	components := flag.String("components", "", "optional components as a comma-separated list")
	flag.Parse()
	if err := configure(*prefix, *components); err != nil {
		fmt.Fprintln(os.Stderr, "nocturne-config:", err)
		os.Exit(1)
	}
}

func selectComponents(names string) ([]string, error) {
	requested := make(map[string]bool)
	for _, field := range strings.Split(names, ",") {
		name := strings.TrimSpace(field)
		if name == "" {
			continue
		}
		if !slices.Contains(optional, name) {
			return nil, fmt.Errorf("unknown Nocturne component %q", name)
		}
		requested[name] = true
	}
	if requested["simulation"] {
		requested["spatial"] = true
	}
	var selected []string
	for _, name := range optional {
		if requested[name] {
			selected = append(selected, name)
		}
	}
	return selected, nil
}

func configure(prefix, names string) error {
	if prefix == "" {
		return errors.New("supply -prefix or set NOCTURNE_SDK_PREFIX")
	}
	path, err := filepath.Abs(prefix)
	if err != nil {
		return err
	}
	selected, err := selectComponents(names)
	if err != nil {
		return err
	}

	scratch, err := os.MkdirTemp("", "nocturne-go-config-")
	if err != nil {
		return err
	}
	defer os.RemoveAll(scratch)
	if err = os.WriteFile(filepath.Join(scratch, "CMakeLists.txt"), discovery, 0600); err != nil {
		return err
	}
	build := filepath.Join(scratch, "build")
	command := exec.Command("cmake", "-S", scratch, "-B", build, "-DCMAKE_BUILD_TYPE=Release",
		"-DNOCTURNE_SDK_PREFIX="+path, "-DNOCTURNE_GO_COMPONENTS="+strings.Join(selected, ";"))
	command.Stdout = os.Stderr
	command.Stderr = os.Stderr
	if err = command.Run(); err != nil {
		return fmt.Errorf("could not configure the installed SDK: %w", err)
	}

	released := filepath.Join(build, "Release")
	includes, err := readLines(filepath.Join(released, "includes.txt"))
	if err != nil {
		return err
	}
	archives, err := readLines(filepath.Join(released, "archives.txt"))
	if err != nil {
		return err
	}
	identity, err := sdkIdentity(includes, archives)
	if err != nil {
		return err
	}
	flags := []string{"-DNOCTURNE_GO_SDK_SHA256=sha256_" + identity}
	for _, dir := range includes {
		flags = append(flags, "-I"+dir)
	}
	cflags, err := joinGoFlags(flags)
	if err != nil {
		return err
	}
	target, err := os.ReadFile(filepath.Join(released, "target-system.txt"))
	if err != nil {
		return err
	}
	if strings.TrimSpace(string(target)) != "Windows" {
		archives = append(archives, "-lm")
	}
	ldflags, err := joinGoFlags(archives)
	if err != nil {
		return err
	}
	var tags []string
	for _, name := range selected {
		tags = append(tags, "nocturne_"+name)
	}
	fmt.Printf("export CGO_CFLAGS=%s\nexport CGO_LDFLAGS=%s\nexport NOCTURNE_GO_TAGS=%s\n",
		shellQuote(cflags), shellQuote(ldflags), shellQuote(strings.Join(tags, ",")))
	return nil
}

func readLines(path string) ([]string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	lines := strings.Split(strings.TrimSuffix(string(data), "\n"), "\n")
	if len(lines) == 1 && lines[0] == "" {
		return nil, fmt.Errorf("no installed SDK paths in %s", path)
	}
	return lines, nil
}

// Include headers and archives in the Go cache identity even when the SDK is
// replaced in place under the same prefix.
func sdkIdentity(includes, archives []string) (string, error) {
	files := slices.Clone(archives)
	for _, directory := range includes {
		public := filepath.Join(directory, "nocturne")
		headers, err := os.ReadDir(public)
		if err != nil {
			return "", err
		}
		for _, header := range headers {
			if !header.IsDir() && filepath.Ext(header.Name()) == ".h" {
				files = append(files, filepath.Join(public, header.Name()))
			}
		}
	}
	digest := sha256.New()
	for _, name := range files {
		fmt.Fprintf(digest, "%d:%s\n", len(name), name)
		file, err := os.Open(name)
		if err != nil {
			return "", err
		}
		count, err := io.Copy(digest, file)
		closeErr := file.Close()
		if err != nil {
			return "", err
		}
		if closeErr != nil {
			return "", closeErr
		}
		fmt.Fprintf(digest, "\n%d\n", count)
	}
	return fmt.Sprintf("%x", digest.Sum(nil)), nil
}

// The Go tool treats each quoted field as a literal flag.
func joinGoFlags(flags []string) (string, error) {
	quoted := make([]string, 0, len(flags))
	for _, value := range flags {
		switch {
		case !strings.ContainsAny(value, "\t\r\n '\""):
			quoted = append(quoted, value)
		case !strings.Contains(value, "'"):
			quoted = append(quoted, "'"+value+"'")
		case !strings.Contains(value, "\""):
			quoted = append(quoted, "\""+value+"\"")
		default:
			return "", fmt.Errorf("the Go compiler cannot quote %q", value)
		}
	}
	return strings.Join(quoted, " "), nil
}

func shellQuote(value string) string {
	return "'" + strings.ReplaceAll(value, "'", "'\"'\"'") + "'"
}
