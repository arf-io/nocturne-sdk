use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

const COMPONENTS: [(&str, &str); 4] = [
    ("CARGO_FEATURE_POSIX_HOST", "posix_host"),
    ("CARGO_FEATURE_GRAPH", "graph"),
    ("CARGO_FEATURE_SPATIAL", "spatial"),
    ("CARGO_FEATURE_SIMULATION", "simulation"),
];

fn header_component(header: &str) -> Option<&'static str> {
    match header {
        "posix_host.h" => Some("posix_host"),
        "windows_host.h" => Some("windows_host"),
        "graph.h" => Some("graph"),
        "spatial.h" | "spatial_mesh.h" | "spatial_surface.h" => Some("spatial"),
        "simulation.h" => Some("simulation"),
        _ => None,
    }
}

fn read_file(path: &Path) -> String {
    fs::read_to_string(path).unwrap_or_else(|err| panic!("reading {}: {err}", path.display()))
}

fn main() {
    println!("cargo:rerun-if-env-changed=NOCTURNE_SDK_PREFIX");
    println!("cargo:rerun-if-env-changed=CMAKE_TOOLCHAIN_FILE");
    println!("cargo:rerun-if-env-changed=LIBCLANG_PATH");
    println!("cargo:rerun-if-changed=native/CMakeLists.txt");

    let prefix = PathBuf::from(
        env::var_os("NOCTURNE_SDK_PREFIX")
            .expect("set NOCTURNE_SDK_PREFIX to an installed Nocturne C SDK prefix"),
    )
    .canonicalize()
    .expect("resolve NOCTURNE_SDK_PREFIX");
    let components: Vec<_> = COMPONENTS
        .iter()
        .filter_map(|&(feature, name)| env::var_os(feature).map(|_| name))
        .collect();
    let out = PathBuf::from(env::var_os("OUT_DIR").expect("Cargo OUT_DIR"));
    let source = out.join("native-config");
    fs::create_dir_all(&source).expect("create CMake discovery source directory");
    fs::write(
        source.join("CMakeLists.txt"),
        include_bytes!("native/CMakeLists.txt"),
    )
    .expect("stage CMake discovery project");
    let resolved = out.join("native-resolution");
    let mut cmake = Command::new("cmake");
    cmake
        .arg("-S")
        .arg(&source)
        .arg("-B")
        .arg(&resolved)
        .args(["-U", "Nocturne_DIR"])
        .arg("-DCMAKE_BUILD_TYPE=Release")
        .arg(format!("-DNOCTURNE_SDK_PREFIX={}", prefix.display()))
        .arg(format!(
            "-DNOCTURNE_RUST_COMPONENTS={}",
            components.join(";")
        ));
    if let Some(toolchain) = env::var_os("CMAKE_TOOLCHAIN_FILE") {
        let toolchain = PathBuf::from(toolchain);
        println!("cargo:rerun-if-changed={}", toolchain.display());
        cmake.arg(format!("-DCMAKE_TOOLCHAIN_FILE={}", toolchain.display()));
    }
    let status = cmake.status().expect("run CMake to find the installed SDK");
    assert!(
        status.success(),
        "finding the installed Nocturne SDK failed"
    );

    let resolved = resolved.join("Release");
    let includes = read_file(&resolved.join("include-dirs"));
    let mut builder = bindgen::Builder::default()
        .allowlist_type("Nt.*")
        .allowlist_function("nt_.*")
        .allowlist_var("NT_.*")
        .prepend_enum_name(false)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()));
    let mut headers = Vec::new();
    for dir in includes.lines().filter(|line| !line.is_empty()) {
        let namespace = Path::new(dir).join("nocturne");
        println!("cargo:rerun-if-changed={}", namespace.display());
        builder = builder.clang_arg(format!("-I{dir}"));
        for entry in fs::read_dir(&namespace).expect("read installed Nocturne headers") {
            let entry = entry.expect("read SDK header directory entry");
            let name = entry
                .file_name()
                .into_string()
                .expect("UTF-8 SDK header name");
            if name.ends_with(".h")
                && header_component(&name).is_none_or(|value| components.contains(&value))
            {
                headers.push(name);
            }
        }
    }
    headers.sort_unstable();
    headers.dedup();
    let mut umbrella = headers
        .into_iter()
        .map(|name| format!("#include <nocturne/{name}>\n"))
        .collect::<String>();
    // Clang evaluates the installed ABI macro even when bindgen does not
    // emit the UINT32_C expansion as a Rust constant.
    umbrella.push_str("enum { NT_RUST_HEADER_ABI_VERSION = NT_ABI_VERSION };\n");
    builder
        .header_contents("nocturne-installed.h", &umbrella)
        .generate()
        .expect("generate Rust bindings from installed Nocturne headers")
        .write_to_file(out.join("raw.rs"))
        .expect("write installed Nocturne bindgen output");

    for line in read_file(&resolved.join("archives")).lines() {
        let (name, path) = line
            .split_once('\t')
            .expect("malformed SDK archive manifest");
        let archive = Path::new(path);
        assert!(
            archive.is_file(),
            "missing SDK archive {}",
            archive.display()
        );
        println!("cargo:rerun-if-changed={path}");
        println!(
            "cargo:rustc-link-search=native={}",
            archive.parent().unwrap().display()
        );
        let library = if name == "nocturne" {
            name.to_owned()
        } else {
            format!("nocturne_{name}")
        };
        println!("cargo:rustc-link-lib=static={library}");
    }
    if env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        println!("cargo:rustc-link-lib=m");
    }
}
