#!/bin/bash

##
# build.sh - Convenience build script for wterm project
#
# This script provides easy commands for building, testing, and installing
# the wterm WiFi management tool using CMake.
##

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"

# Function to print colored output
print_status() {
    case $1 in
        "OK") echo -e "${GREEN}✅ $2${NC}" ;;
        "ERROR") echo -e "${RED}❌ $2${NC}" ;;
        "INFO") echo -e "${BLUE}ℹ️  $2${NC}" ;;
        "WARN") echo -e "${YELLOW}⚠️  $2${NC}" ;;
    esac
}

# Function to show usage
show_usage() {
    echo "wterm Build Script"
    echo "=================="
    echo ""
    echo "Usage: $0 [COMMAND] [OPTIONS]"
    echo ""
    echo "Commands:"
    echo "  clean       Clean build directory"
    echo "  debug       Build debug version"
    echo "  release     Build release version (default)"
    echo "  test        Build and run tests"
    echo "  install     Install to system"
    echo "  package     Create installation package"
    echo "  all         Clean, build, and test"
    echo "  help        Show this help message"
    echo ""
    echo "Options:"
    echo "  --verbose   Enable verbose output"
    echo "  --jobs N    Use N parallel jobs (default: auto-detect)"
    echo "  --sanitize  Enable sanitizers (debug only)"
    echo "  --coverage  Enable code coverage (debug only)"
    echo ""
    echo "Examples:"
    echo "  $0 release              # Build release version"
    echo "  $0 debug --sanitize     # Build debug with sanitizers"
    echo "  $0 test --verbose       # Run tests with verbose output"
    echo "  $0 all --jobs 4         # Clean, build, test with 4 jobs"
}

# Parse command line arguments
COMMAND="release"
VERBOSE=false
JOBS=$(nproc)
SANITIZE=false
COVERAGE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        clean|debug|release|test|install|package|all|help)
            COMMAND="$1"
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --sanitize)
            SANITIZE=true
            shift
            ;;
        --coverage)
            COVERAGE=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Set verbose flags
if $VERBOSE; then
    CMAKE_VERBOSE="--verbose"
    MAKE_VERBOSE="VERBOSE=1"
else
    CMAKE_VERBOSE=""
    MAKE_VERBOSE=""
fi

# Function to clean build directory
clean_build() {
    print_status "INFO" "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
    print_status "OK" "Build directory cleaned"
}

# Function to configure CMake
configure_cmake() {
    local build_type="$1"
    local extra_flags="$2"

    print_status "INFO" "Configuring CMake for $build_type build..."

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    local cmake_flags="-DCMAKE_BUILD_TYPE=$build_type"

    if [[ "$build_type" == "Debug" ]]; then
        if $SANITIZE; then
            cmake_flags="$cmake_flags -DENABLE_SANITIZERS=ON"
            print_status "INFO" "Sanitizers enabled"
        fi
        if $COVERAGE; then
            cmake_flags="$cmake_flags -DENABLE_COVERAGE=ON"
            print_status "INFO" "Code coverage enabled"
        fi
    fi

    cmake $cmake_flags $extra_flags "$PROJECT_ROOT"
    print_status "OK" "CMake configuration complete"
}

# Function to build project
build_project() {
    print_status "INFO" "Building project with $JOBS jobs..."

    cd "$BUILD_DIR"
    cmake --build . --parallel $JOBS $CMAKE_VERBOSE

    print_status "OK" "Build complete"
    print_status "INFO" "Executable: $BUILD_DIR/bin/wterm"
}

# Function to run tests
run_tests() {
    print_status "INFO" "Running tests..."

    cd "$BUILD_DIR"
    ctest --output-on-failure $CMAKE_VERBOSE

    if [ $? -eq 0 ]; then
        print_status "OK" "All tests passed"
    else
        print_status "ERROR" "Some tests failed"
        exit 1
    fi
}

# Function to install
install_project() {
    print_status "INFO" "Installing wterm..."

    cd "$BUILD_DIR"

    if [[ $EUID -ne 0 ]]; then
        print_status "WARN" "Installation requires root privileges"
        sudo cmake --install .
    else
        cmake --install .
    fi

    print_status "OK" "Installation complete"
    print_status "INFO" "You can now run 'wterm' from anywhere"
}

# Function to create package
create_package() {
    print_status "INFO" "Creating installation package..."

    cd "$BUILD_DIR"
    cpack

    print_status "OK" "Package created in $BUILD_DIR"
}

# Main command processing
case $COMMAND in
    help)
        show_usage
        ;;
    clean)
        clean_build
        ;;
    debug)
        configure_cmake "Debug"
        build_project
        ;;
    release)
        configure_cmake "Release"
        build_project
        ;;
    test)
        if [[ ! -d "$BUILD_DIR" ]]; then
            configure_cmake "Debug"
        fi
        build_project
        run_tests
        ;;
    install)
        if [[ ! -f "$BUILD_DIR/bin/wterm" ]]; then
            configure_cmake "Release"
            build_project
        fi
        install_project
        ;;
    package)
        configure_cmake "Release"
        build_project
        create_package
        ;;
    all)
        clean_build
        configure_cmake "Release"
        build_project
        run_tests
        ;;
    *)
        print_status "ERROR" "Unknown command: $COMMAND"
        show_usage
        exit 1
        ;;
esac

print_status "OK" "Build script completed successfully"