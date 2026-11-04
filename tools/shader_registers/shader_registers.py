"""Driver register counts of engine shader entry points through VK_KHR_pipeline_executable_properties.

usage:
  python tools/shader_registers/shader_registers.py voxel_lighting:fragmentMarched gi_rays:shadeMain:compute
  python tools/shader_registers/shader_registers.py --scan              every fragment and compute entry
  python tools/shader_registers/shader_registers.py --scan volumetrics  every entry of some shaders

Entries compile like the player (Slang -O3, ray query on, reversed Z). Fragment entries pair with the module's
vertexMain. Build the native half first with cmake --build <dir> --target shader_registers.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = '.exe' if os.name == 'nt' else ''

# Ampere and Ada hold 64K registers and 48 warps per SM and allocate 8 registers per thread at a time
REGISTER_FILE = 65536
MAX_WARPS = 48


def newest(paths):
    paths = [p for p in paths if os.path.isfile(p)]
    return max(paths, key=os.path.getmtime) if paths else None


def find_slangc(explicit):
    found = explicit or os.environ.get('SLANGC') or newest(
        glob.glob(os.path.join(ROOT, '*', 'vcpkg_installed', '*', 'tools', 'shader-slang', 'slangc' + EXE)) +
        glob.glob(os.path.join(ROOT, 'build', '*', 'vcpkg_installed', '*', 'tools', 'shader-slang', 'slangc' + EXE)))
    return found or shutil.which('slangc')


def find_tool(explicit):
    return explicit or os.environ.get('SHADER_REGISTERS') or newest(
        glob.glob(os.path.join(ROOT, 'out', '*', 'shader_registers', 'shader_registers' + EXE)))


def scan_entries(shader_dir, names):
    entries = []
    stage_line = re.compile(r'\[shader\("(fragment|compute)"\)\]')
    signature = re.compile(r'^\s*[A-Za-z_][\w<>]*\s+(\w+)\s*\(')
    files = sorted(glob.glob(os.path.join(shader_dir, '*.slang')))
    for path in files:
        module = os.path.splitext(os.path.basename(path))[0]
        if names and module not in names:
            continue
        lines = open(path, encoding='utf-8').read().splitlines()
        has_vertex = any(re.search(r'\bvertexMain\s*\(', l) for l in lines)
        for i, line in enumerate(lines):
            match = stage_line.search(line)
            if not match:
                continue
            for following in lines[i + 1:i + 6]:
                found = signature.match(following)
                if found:
                    stage = match.group(1)
                    if stage == 'fragment' and not has_vertex:
                        break
                    entries.append(f'{module}:{found.group(1)}' + (':compute' if stage == 'compute' else ''))
                    break
    return entries


def compile_entry(args, module, entry, stage, out):
    defines = [f'-DTOAST_RAY_QUERY={args.rq}', f'-DTOAST_TRACE_RAY_QUERY={args.rq}', '-DTOAST_REVERSED_Z=1'] + \
              [f'-D{d}' for d in args.define]
    command = [args.slangc, module + '.slang', '-I', '.', '-target', 'spirv', '-profile', 'spirv_1_6', '-entry', entry, '-stage',
               stage, f'-O{args.opt}', '-fvk-use-entrypoint-name', '-matrix-layout-column-major', '-o', out] + defines
    result = subprocess.run(command, cwd=args.shaders, capture_output=True, text=True)
    if result.returncode != 0 or not os.path.exists(out):
        errors = [l.strip() for l in result.stderr.splitlines() if 'error' in l]
        raise RuntimeError(' | '.join(errors[:2]) or 'slangc failed')


def measure(args, spec, scratch):
    parts = spec.split(':')
    module, entry = parts[0], parts[1]
    stage = parts[2] if len(parts) > 2 else 'fragment'
    main = os.path.join(scratch, 'main.spv')
    compile_entry(args, module, entry, stage, main)
    command = [args.tool, main]
    if stage == 'fragment':
        vertex = os.path.join(scratch, 'vertex.spv')
        compile_entry(args, module, 'vertexMain', 'vertex', vertex)
        command.append(vertex)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or 'pipeline creation failed')
    return dict(line.split('|', 1) for line in result.stdout.splitlines() if '|' in line)


def warps(registers):
    allocated = (registers + 7) // 8 * 8
    return min(MAX_WARPS, REGISTER_FILE // (32 * max(allocated, 1)))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('specs', nargs='*', help='shader:entry or shader:entry:compute')
    parser.add_argument('--scan', action='store_true', help='measure every fragment and compute entry, of the named shaders if any')
    parser.add_argument('--opt', type=int, default=3, help='Slang optimization level, the player uses 3')
    parser.add_argument('--rq', type=int, default=1, help='TOAST_RAY_QUERY and TOAST_TRACE_RAY_QUERY')
    parser.add_argument('--define', action='append', default=[], help='extra NAME=VALUE')
    parser.add_argument('--shaders', default=os.path.join(ROOT, 'engine', 'assets', 'shaders'))
    parser.add_argument('--slangc')
    parser.add_argument('--tool')
    parser.add_argument('--all', action='store_true', help='print every statistic the driver reports')
    args = parser.parse_args()

    args.slangc = find_slangc(args.slangc)
    args.tool = find_tool(args.tool)
    if not args.slangc:
        sys.exit('slangc not found, pass --slangc or set SLANGC')
    if not args.tool:
        sys.exit('shader_registers not built, run cmake --build <dir> --target shader_registers')

    specs = scan_entries(args.shaders, set(args.specs)) if args.scan else args.specs
    if not specs:
        sys.exit('nothing to measure')

    scratch = tempfile.mkdtemp(prefix='shader_registers_')
    try:
        for spec in specs:
            try:
                stats = measure(args, spec, scratch)
            except RuntimeError as error:
                print(f'{spec:50s} ERROR {error}')
                continue
            registers = int(stats.get('Register Count', 0))
            if args.all:
                details = '  '.join(f'{k}={v}' for k, v in stats.items() if k != 'Local Memory Size')
            else:
                details = f'{int(stats.get("Binary Size", 0)) // 1024:5d} KB'
                if 'Shared Memory Size' in stats:
                    details += f'  {stats["Shared Memory Size"]} B shared'
            print(f'{spec:50s} {registers:4d} regs {warps(registers):3d} warps  {details}')
            sys.stdout.flush()
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == '__main__':
    main()
