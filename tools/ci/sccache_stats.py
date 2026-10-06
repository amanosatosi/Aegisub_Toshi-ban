"""Print compact sccache statistics and verify the daemon's actual cache mode."""
import json
import os
from pathlib import Path
import re
import subprocess


def summary(info, expected, actual):
    stats = info['stats']
    hits = sum(stats['cache_hits']['counts'].values())
    misses = sum(stats['cache_misses']['counts'].values())
    rate = 100 * hits / (hits + misses) if hits + misses else 0
    raw_errors = stats['cache_write_errors']
    # 0.18.0's ReadOnlyStorage rejects put() locally, incrementing this raw
    # counter without contacting GHA. Keep the counter visible and label it.
    backend_errors = 0 if actual == 'ReadOnly' else raw_errors
    rows = {
        'Compile requests': stats['compile_requests'],
        'Cache hits': hits,
        'Cache misses': misses,
        'Cache hit percentage': f'{rate:.2f}%',
        'Cache write successes': stats['cache_writes'],
        'Cache write errors (raw sccache counter)': raw_errors,
        'GHA upload errors': backend_errors,
        'Non-cacheable compilations': stats['non_cacheable_compilations'],
        'Non-cacheable calls': stats['requests_not_cacheable'],
    }
    print(f'Configured mode: {expected}; actual server mode: {actual}')
    print('Cache location:', info['cache_location'])
    print('Base directories:', ', '.join(info['basedirs']))
    for label, value in rows.items():
        print(f'{label}: {value}')
    if actual == 'ReadOnly':
        print('Read-only: zero GHA uploads; raw write errors are local read-only refusals.')
    expected_actual = {'READ_ONLY': 'ReadOnly', 'READ_WRITE': 'ReadWrite'}[expected]
    if actual != expected_actual:
        raise RuntimeError(f'Cache server fell back to {actual}; expected {expected_actual}')
    if actual == 'ReadOnly' and stats['cache_writes']:
        raise RuntimeError('Read-only server unexpectedly reported successful writes')
    return rows


def main():
    expected = os.environ['SCCACHE_GHA_RW_MODE']
    info = json.loads(subprocess.check_output(['sccache', '--show-stats', '--stats-format=json'], text=True))
    log = Path(os.environ['SCCACHE_ERROR_LOG']).read_text(encoding='utf-8', errors='replace')
    modes = re.findall(r'server has setup with (ReadOnly|ReadWrite)', log)
    if not modes:
        raise RuntimeError('Cannot verify actual sccache server mode from startup log')
    rows = summary(info, expected, modes[-1])
    if rows['GHA upload errors']:
        diagnostics = [line for line in log.splitlines()
                       if 'ghac' in line.lower() and ('WARN' in line or 'ERROR' in line)]
        for line in diagnostics[:3]:
            print('Backend diagnostic:', re.sub(r'https?://\S+', '<URL>', line))
    # The JSON is printed in the log as well, so no perpetual artifact upload.
    print('Sccache JSON:', json.dumps(info, separators=(',', ':')))
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a', encoding='utf-8') as output:
        output.write(f'### MSVC compile cache ({expected})\n\n| Metric | Value |\n|---|---:|\n')
        for label, value in rows.items():
            output.write(f'| {label} | {value} |\n')
        if modes[-1] == 'ReadOnly':
            output.write('\nRaw write errors are locally rejected writes; the read-only backend sends no uploads.\n')


if __name__ == '__main__':
    main()
