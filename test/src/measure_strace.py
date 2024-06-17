import re
from tqdm import tqdm
import pandas as pd

# Regular expression to parse the strace output
strace_pattern = re.compile(r'(\w+)\((.*?)\)\s+=\s+(-?\d+|0x[0-9a-fA-F]+)(?:\s+\((.*?)\))?')

# Parse the strace output
def parse_strace_output(lines):
    parsed_data = []
    for line in tqdm(lines):
        match = strace_pattern.match(line)
        if match:
            syscall = match.group(1)
            args = match.group(2)
            result = match.group(3)
            error = match.group(4) if match.group(4) else None
            parsed_data.append({
                'syscall': syscall,
                'args': args,
                'result': result,
                'error': error
            })
    return parsed_data

def log(msg: str):
    print(msg, flush=True)

def collect_histograms(files, ignored_syscalls=[]):
    syscall_histograms = dict()
    for category, file in files.items():
        with open(file, 'r') as f:
            log(f"reading {file}")
            lines = f.readlines()

        # Parse the provided strace output
        log(f"parsing {file}")
        parsed_strace = parse_strace_output(lines)
        df = pd.DataFrame(parsed_strace)
        df = df[~df.syscall.isin(ignored_syscalls)]
        syscall_counts = df['syscall'].value_counts()
        syscall_histograms[category] = syscall_counts
    return syscall_histograms

def merge_histograms(syscall_histograms, categoryA, categoryB, how='outer'):
    full_join = pd.merge(syscall_histograms[categoryA], syscall_histograms[categoryB], on='syscall', how=how, suffixes=(f"_{categoryA}", f"_{categoryB}"))
    outer_join = full_join[full_join.isna().any(axis=1)]
    return full_join, outer_join

files = {
        "allnet": "./strace-allnet",
        "nonet": "./strace-nonet",
        "vmux": "./strace-vmux"
         }
ignored_syscalls = [
        "restart_syscall" # not a syscall we can block
        ]
categories = files.keys()
syscall_histograms = collect_histograms(files, ignored_syscalls=ignored_syscalls)

log("")
log("vmux syscalls")
log(syscall_histograms['vmux'])

full, outer = merge_histograms(syscall_histograms, "allnet", "vmux", how="left")
log(outer.to_string())
vmux_can_prohibit = outer

full, outer = merge_histograms(syscall_histograms, "allnet", "vmux", how="right")
log(outer.to_string())
vmux_needs_additionally = outer

log(f"vmux can block {len(vmux_can_prohibit)} syscalls, but needs {len(vmux_needs_additionally)} additional syscalls")

new = len(syscall_histograms['vmux'])
old = len(syscall_histograms['allnet'])
decrease = (new - old) / old
log(f"change of {decrease*100}%")

log("crucially, it can restrict syscalls to open network connections from 'listen(for any ip)' to connect(to vmux socket)'")

log("WARNING: note that you need to collect the traces (just vm-strace*) while doing a network request at least once")
